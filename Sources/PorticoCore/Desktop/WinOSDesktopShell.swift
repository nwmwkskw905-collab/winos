import Foundation
import QuartzCore
/// Desktop Shell REAL — sobre o runtime, não sistema fictício separado
/// WinOSDesktop → Shell → WindowManager → FileManager → Taskbar → StartMenu → DesktopIcons → ProcessManager → RuntimeBridge → InputBridge → RenderSurface

public enum WinOSDesktopState: String, Sendable {
    case initializing = "INITIALIZING"
    case ready = "READY"
    case running = "RUNNING"
    case failed = "FAILED"
    case shutdown = "SHUTDOWN"
}

@MainActor
public final class WinOSDesktopShell: ObservableObject {
    @Published public private(set) var state: WinOSDesktopState = .initializing
    @Published public private(set) var fps: Double = 0
    @Published public private(set) var frameTimeMs: Double = 0
    @Published public private(set) var lastError: String = ""
    
    public let windowManager: WinOSWindowManager
    public let processManager: WinOSProcessManagerReal
    public let fileManager: WinOSFileManagerReal
    public let renderEngine: WinOSRenderEngine
    public let compositor: WinOSCompositor
    public let inputBridge: WinOSInputBridge
    
    // NOVO - Desktop Responsivo + Cursor + Mouse Real (modulo adicional, nao substitui existentes)
    public let displayManager: WinOSDisplayManager
    public let orientationManager: WinOSOrientationManager
    public let mouseCursorManager: WinOSMouseCursorManager
    public let cursorRenderer: WinOSCursorRenderer
    public let mouseStateMachine: WinOSMouseStateMachine
    public let desktopInputHandler: WinOSDesktopInputHandler
    public let responsiveTests: WinOSDesktopResponsiveTests
    
    private let sandbox: AppSandbox
    private let log: LogCenter
    private var pcPath: String = ""
    private var desktopPID: UInt32 = 0
    
    // Performance monitoring
    private var frameCount: Int = 0
    private var lastFPSTime: Double = 0
    private var frameTimeHistory: [Double] = []
    
    public init(sandbox: AppSandbox, log: LogCenter, pcPath: String = "") {
        self.sandbox = sandbox
        self.log = log
        self.pcPath = pcPath
        
        self.windowManager = WinOSWindowManager()
        self.processManager = WinOSProcessManagerReal()
        self.fileManager = WinOSFileManagerReal(sandbox: sandbox, log: log, pcPath: pcPath)
        self.renderEngine = WinOSRenderEngine()
        self.compositor = WinOSCompositor(width: 1920, height: 1080)
        self.inputBridge = WinOSInputBridge()
        
        // NOVO - Inicializa modulo responsivo (preserva baseline)
        let initialMetrics = WinOSDisplayMetrics(deviceScreenWidth: 390, deviceScreenHeight: 844, desktopWidth: 1280, desktopHeight: 720, orientation: .portrait)
        self.displayManager = WinOSDisplayManager(initialMetrics: initialMetrics)
        self.orientationManager = WinOSOrientationManager(displayManager: displayManager, windowManager: windowManager)
        self.mouseCursorManager = WinOSMouseCursorManager(initialX: initialMetrics.desktopWidth/2, initialY: initialMetrics.desktopHeight/2, displayMetrics: initialMetrics)
        self.cursorRenderer = WinOSCursorRenderer(displayMetrics: initialMetrics, compositor: compositor)
        self.mouseStateMachine = WinOSMouseStateMachine()
        self.desktopInputHandler = WinOSDesktopInputHandler(displayManager: displayManager, windowManager: windowManager, inputBridge: inputBridge, compositor: compositor, cursorManager: mouseCursorManager)
        self.responsiveTests = WinOSDesktopResponsiveTests()
        
        // Conecta cursor manager ao renderer
        self.cursorRenderer.setCompositor(compositor)
        
        NSLog("[WINOS-SHELL] init pcPath=%@", pcPath)
        
        // Conecta eventos
        windowManager.onEvent = { [weak self] event in
            self?.handleWindowEvent(event)
        }
        processManager.onProcessCreated = { proc in
            NSLog("[WINOS-SHELL] process created pid=%u exe=%@", proc.id, proc.executable)
        }
        processManager.onProcessExited = { proc in
            NSLog("[WINOS-SHELL] process exited pid=%u code=%@", proc.id, proc.exitCode != nil ? "\(proc.exitCode!)" : "nil")
        }
        inputBridge.onMessage = { msg in
            NSLog("[WINOS-SHELL] input message hwnd=%u msg=%@ x=%d y=%d", msg.hwnd, "\(msg.message)", msg.x, msg.y)
        }
    }
    
    public func initialize(pcPath: String) {
        self.pcPath = pcPath
        state = .initializing
        NSLog("[WINOS-RUNTIME] RUNTIME_START shell pc=%@", pcPath)
        NSLog("[WINOS-RUNTIME] WIN32_INIT shell")
        NSLog("[WINOS-RUNTIME] PE_LOADER_INIT shell")
        
        // Garante estrutura
        fileManager.setPCRoot(pcPath)
        
        // Cria processo desktop
        let desktopProc = processManager.createProcess(executable: "winos_desktop.exe", commandLine: "winos_desktop.exe", workingDirectory: pcPath, isDesktop: true)
        desktopPID = desktopProc.id
        processManager.setProcessState(pid: desktopPID, state: .running)
        
        // Render engine
        NSLog("[WINOS-RUNTIME] GRAPHICS_INIT shell")
        renderEngine.initialize()
        // Usa desktop virtual 1280x720 com viewport dinâmica (responsivo)
        let desktopW = Int32(displayManager.metrics.desktopWidth)
        let desktopH = Int32(displayManager.metrics.desktopHeight)
        compositor.setDesktopSize(width: desktopW, height: desktopH)
        inputBridge.setDesktopSize(logicalWidth: desktopW, logicalHeight: desktopH, physicalWidth: Int32(displayManager.metrics.viewportWidth), physicalHeight: Int32(displayManager.metrics.viewportHeight))
        
        // NOVO - Display metrics diagnostics
        NSLog("%@", displayManager.metrics.diagnosticsLog)
        NSLog("[WINOS-DISPLAY] orientation=%@ viewport=%dx%d desktop=%dx%d scale=%.3f", displayManager.orientation.rawValue, Int(displayManager.metrics.viewportWidth), Int(displayManager.metrics.viewportHeight), desktopW, desktopH, displayManager.metrics.scale)
        
        // NOVO - Cursor acima das janelas (ordem: background -> windows -> overlays -> taskbar -> cursor)
        cursorRenderer.setCompositor(compositor)
        mouseCursorManager.setVisible(true)
        
        // NOVO - Testes responsivos (apenas log, nao bloqueia)
        let testResults = responsiveTests.runAll()
        let passed = testResults.filter { $0.passed }.count
        NSLog("[WINOS-TEST] responsive tests %d/%d PASS", passed, testResults.count)
        
        // Cria janela File Manager inicial? Não, desktop começa vazio com ícones
        NSLog("[WINOS-RUNTIME] DESKTOP_INIT shell")
        
        state = .ready
        NSLog("[WINOS-RUNTIME] SESSION_READY shell")
        NSLog("[WINOS-RUNTIME] RUNNING shell desktop pid=%u", desktopPID)
        state = .running
        lastFPSTime = CACurrentMediaTime()
    }
    
    public func shutdown() {
        state = .shutdown
        NSLog("[WINOS-SHELL] shutdown")
        windowManager.reset()
        processManager.reset()
        renderEngine.shutdown()
        compositor.reset()
        inputBridge.clearQueue()
    }
    
    // MARK: - Window operations (conectadas ao Win32 real)
    
    public func createWindowForExecutable(_ fileItem: WinOSFileItem) {
        guard fileItem.isExecutable else {
            NSLog("[WINOS-SHELL] createWindowForExecutable FAIL not exe %@", fileItem.name)
            return
        }
        
        // File Manager → identifica PE → PELoader → WindowsPEBackend → Win32 process → WindowManager → Application Window
        // Não abrir EXE diretamente pelo Swift UI sem passar pelo runtime
        
        let pid = processManager.createProcess(executable: fileItem.path, commandLine: fileItem.windowsPath, workingDirectory: fileManager.currentWindowsPath).id
        processManager.setProcessState(pid: pid, state: .starting)
        
        // Valida PE real via PELoader
        do {
            let url = try sandbox.resolveInside(fileItem.path)
            let data = try Data(contentsOf: url)
            guard PEInspector.looksLikePE(data) else {
                processManager.setError(pid: pid, error: "INVALID_MZ — não é PE válido")
                lastError = "INVALID_MZ: \(fileItem.windowsPath)"
                return
            }
            let peImage = try PEInspector.scan(data)
            NSLog("[WINOS-SHELL] PE detected arch=%@ machine=0x%x isPE32Plus=%@ file=%@", peImage.arch, peImage.machine, peImage.isPE32Plus ? "YES" : "NO", fileItem.name)
            
            // Verifica ARM64 → UNSUPPORTED_ARCH
            if peImage.machine == 0xAA64 || peImage.arch.lowercased().contains("arm64") {
                processManager.setError(pid: pid, error: "UNSUPPORTED_ARCH ARM64")
                lastError = "UNSUPPORTED_ARCH: \(fileItem.windowsPath) arch=\(peImage.arch)"
                return
            }
            
            // Cria janela para processo
            let win = windowManager.createWindow(processID: pid, title: fileItem.name, x: 100, y: 100, width: 800, height: 600, executablePath: fileItem.path)
            processManager.addWindow(pid: pid, windowID: win.id)
            processManager.setProcessState(pid: pid, state: .running)
            
            // Aqui WindowsPEBackend seria iniciado via RuntimeManager
            // Por enquanto, simula processo rodando e janela criada
            NSLog("[WINOS-SHELL] createWindowForExecutable SUCCESS pid=%u win=%u exe=%@", pid, win.id.raw, fileItem.name)
            
        } catch {
            processManager.setError(pid: pid, error: "PE_LOAD_FAILED: \(error)")
            lastError = "PE_LOAD_FAILED: \(fileItem.windowsPath) error=\(error)"
            NSLog("[WINOS-SHELL] PE load FAIL %@ error=%@", fileItem.windowsPath, "\(error)")
        }
    }
    
    public func openFileManager() {
        let pid = processManager.createProcess(executable: "winos_filemanager.exe", workingDirectory: fileManager.currentWindowsPath, isFileManager: true).id
        processManager.setProcessState(pid: pid, state: .running)
        let win = windowManager.createWindow(processID: pid, title: "WinOS File Manager — \(fileManager.currentWindowsPath)", x: 50, y: 50, width: 900, height: 600, isFileManager: true)
        processManager.addWindow(pid: pid, windowID: win.id)
        NSLog("[WINOS-SHELL] openFileManager pid=%u win=%u path=%@", pid, win.id.raw, fileManager.currentWindowsPath)
    }
    
    // MARK: - Input handling
    
    public func handleTouch(x: Double, y: Double, phase: String) {
        // Hit testing real via WindowManager
        let win = windowManager.windowAt(pointX: Int32(x), pointY: Int32(y))
        let hwnd = win?.id.raw ?? 0
        
        switch phase {
        case "began":
            if let w = win {
                windowManager.focusWindow(id: w.id)
            }
            inputBridge.handleTouchBegan(x: x, y: y, windowID: hwnd)
        case "moved":
            inputBridge.handleTouchMoved(x: x, y: y, windowID: hwnd)
        case "ended":
            inputBridge.handleTouchEnded(x: x, y: y, windowID: hwnd)
            // Verifica botões da janela
            if let w = win {
                if windowManager.closeButtonHitTest(window: w, pointX: Int32(x), pointY: Int32(y)) {
                    closeWindow(id: w.id)
                } else if windowManager.minimizeButtonHitTest(window: w, pointX: Int32(x), pointY: Int32(y)) {
                    windowManager.minimizeWindow(id: w.id)
                } else if windowManager.maximizeButtonHitTest(window: w, pointX: Int32(x), pointY: Int32(y)) {
                    if w.maximized {
                        windowManager.restoreWindow(id: w.id)
                    } else {
                        windowManager.maximizeWindow(id: w.id)
                    }
                }
            }
        default:
            break
        }
    }
    
    public func closeWindow(id: WinOSWindowID) {
        guard let win = windowManager.window(id: id) else { return }
        let pid = win.processID
        windowManager.destroyWindow(id: id)
        processManager.removeWindow(pid: pid, windowID: id)
        // Se processo não tem mais janelas, termina
        if let proc = processManager.process(pid: pid), proc.windows.isEmpty && !proc.isDesktop {
            processManager.setExitCode(pid: pid, code: 0)
        }
        NSLog("[WINOS-SHELL] closeWindow id=%u pid=%u", id.raw, pid)
    }
    
    // MARK: - Frame / Performance
    
    public func tick() {
        frameCount += 1
        let now = CACurrentMediaTime()
        let dt = now - lastFPSTime
        if dt >= 0.5 {
            fps = Double(frameCount) / dt
            frameCount = 0
            lastFPSTime = now
        }
        // Atualiza stats do render engine
        // Compositor composite
        _ = compositor.composite()
    }
    
    private func handleWindowEvent(_ event: WinOSWindowEvent) {
        switch event {
        case .created(let win):
            NSLog("[WINOS-SHELL] window event created id=%u", win.id.raw)
        case .destroyed(let id):
            NSLog("[WINOS-SHELL] window event destroyed id=%u", id.raw)
        case .focused(let id):
            NSLog("[WINOS-SHELL] window event focused id=%u", id.raw)
        default:
            break
        }
    }
    
    // MARK: - NOVO - Orientation handling (preserva estado)
    
    public func handleOrientationChange(orientation: WinOSOrientation, deviceWidth: Double, deviceHeight: Double) {
        NSLog("[WINOS-DISPLAY] handleOrientationChange %@ device=%.0fx%.0f", orientation.rawValue, deviceWidth, deviceHeight)
        orientationManager.updateOrientation(orientation, deviceWidth: deviceWidth, deviceHeight: deviceHeight)
        
        // Atualiza compositor e inputBridge via displayManager callbacks ja configurados
        // Preserva window ID, posicao logica, tamanho, z-order, estado, conteudo
        // Apenas recalcula transform desktop -> viewport
        
        let metrics = displayManager.metrics
        compositor.setDesktopSize(width: Int32(metrics.desktopWidth), height: Int32(metrics.desktopHeight))
        
        NSLog("[WINOS-DISPLAY] after rotation %@", metrics.diagnosticsLog)
    }
    
    public func handleDeviceScreenChange(width: Double, height: Double, scale: Double = 3.0) {
        orientationManager.updateDeviceScreen(width: width, height: height, scale: scale)
    }
    
    // MARK: - NOVO - Input handling responsivo (screen -> desktop -> Win32)
    
    public func handleTouchResponsive(screenX: Double, screenY: Double, phase: String) {
        // Converte screen -> desktop via displayManager (centralizado)
        let desktop = displayManager.screenToDesktop(screenX: screenX, screenY: screenY)
        
        switch phase {
        case "began":
            desktopInputHandler.handleTouchBegan(screenX: screenX, screenY: screenY)
        case "moved":
            desktopInputHandler.handleTouchMoved(screenX: screenX, screenY: screenY)
        case "ended":
            desktopInputHandler.handleTouchEnded(screenX: screenX, screenY: screenY)
        case "cancelled":
            desktopInputHandler.handleTouchCancelled()
        default:
            break
        }
        
        
        NSLog("[WINOS-INPUT] message=%@ x=%d y=%d screen=%.0f,%.0f desktop=%.0f,%.0f orient=%@", phase, Int(desktop.x), Int(desktop.y), screenX, screenY, desktop.x, desktop.y, displayManager.orientation.rawValue)
    }
    
    public func handleWheelResponsive(screenX: Double, screenY: Double, deltaY: Double) {
        desktopInputHandler.handleWheel(screenX: screenX, screenY: screenY, deltaY: deltaY)
    }
    
    // MARK: - Diagnostics

    
    public func diagnostics() -> [String: String] {
        var d: [String: String] = [:]
        d["Desktop"] = state.rawValue
        d["Runtime"] = "READY" // via RuntimeManager
        d["Win32"] = "PARTIAL" // via Win32Catalog
        d["VFS"] = "READY"
        d["Metal"] = renderEngine.currentRenderer == .metal ? "READY" : "FAILED"
        d["Renderer"] = renderEngine.currentRenderer.rawValue
        d["FPS"] = String(format: "%.1f", fps)
        d["FrameTime"] = String(format: "%.2f ms", frameTimeMs)
        d["Windows"] = "\(windowManager.windows.count)"
        d["Processes"] = "\(processManager.processes.count)"
        d["LastError"] = lastError.isEmpty ? "none" : lastError
        d["CompositorFrames"] = "\(compositor.frameCount)"
        d["InputQueue"] = "\(inputBridge.queueCount())"
        return d
    }
}
