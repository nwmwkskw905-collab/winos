import SwiftUI
import PorticoCore

/// Desktop REAL — WinOSDesktop Shell + WindowManager + FileManager + Taskbar + StartMenu + RenderEngine + InputBridge
/// Arquitetura: Desktop → Win32 → processo → VFS/Sandbox → renderização → tela
/// Não é simulação visual — cada janela/processo passa pelo runtime

struct WinOSRealDesktopView: View {
    @EnvironmentObject var model: AppModel
    var pc: EnvironmentProfile? = nil
    
    @StateObject private var shell: WinOSDesktopShell
    @State private var showingFileManager = false
    @State private var selectedWindowID: WinOSWindowID? = nil
    @State private var showingStartMenu = false
    @State private var currentTime = Date()
    @State private var timer: Timer?
    
    init(pc: EnvironmentProfile? = nil) {
        self.pc = pc
        // Cria shell com sandbox real
        let sandbox = AppSandbox.standard()
        let log = LogCenter()
        _shell = StateObject(wrappedValue: WinOSDesktopShell(sandbox: sandbox, log: log, pcPath: pc?.caminho ?? "Environments"))
    }
    
    @State private var orientation: UIDeviceOrientation = .portrait
    @State private var lastGeoSize: CGSize = .zero
    
    var body: some View {
        GeometryReader { geo in
            ZStack {
                // Wallpaper próprio WinOS — preserva aspect ratio via displayMetrics
                WinOSWallpaperView()
                    .onAppear {
                        updateDisplayMetrics(geo: geo)
                    }
                
                // Desktop virtual com escala dinâmica (aspect ratio preservado)
                // Calcula displayWidth/Height e offset para centralizar
                let metrics = shell.displayManager.metrics
                let scale = metrics.scale
                let offsetX = metrics.offsetX
                let offsetY = metrics.offsetY
                let displayW = metrics.displayWidth
                let displayH = metrics.displayHeight
                
                ZStack {
                    // Área de desktop com ícones — em desktop coords, escalada para viewport
                    desktopIconsArea
                    
                    // Janelas reais via WindowManager — em desktop coords
                    ForEach(shell.windowManager.visibleWindows(), id: \.id) { win in
                        WinOSWindowView(window: win, shell: shell)
                            .position(x: CGFloat(win.x + win.width/2), y: CGFloat(win.y + win.height/2))
                            .zIndex(Double(win.zIndex))
                            .onTapGesture {
                                shell.windowManager.focusWindow(id: win.id)
                            }
                    }
                }
                .frame(width: CGFloat(metrics.desktopWidth), height: CGFloat(metrics.desktopHeight))
                .scaleEffect(CGFloat(scale))
                .offset(x: CGFloat(offsetX - (CGFloat(metrics.desktopWidth) * CGFloat(scale) - CGFloat(metrics.desktopWidth))/2), y: CGFloat(offsetY - (CGFloat(metrics.desktopHeight) * CGFloat(scale) - CGFloat(metrics.desktopHeight))/2))
                // Nota: scaleEffect centraliza, offset ajusta para viewport
                
                // Cursor visual REAL — acima das janelas (zIndex 9999)
                // Ordem: background -> windows -> overlays -> taskbar -> cursor
                if shell.mouseCursorManager.cursor.visible {
                    let cursorScreen = shell.displayManager.desktopToScreen(desktopX: shell.mouseCursorManager.cursor.x, desktopY: shell.mouseCursorManager.cursor.y)
                    WinOSCursorView(cursor: shell.mouseCursorManager.cursor, displayMetrics: shell.displayManager.metrics)
                        .position(x: CGFloat(cursorScreen.x), y: CGFloat(cursorScreen.y))
                        .zIndex(9999)
                }
                
                // Taskbar funcional — sempre visível, fora da área escalada (ou dentro, mas com z alto)
                VStack {
                    Spacer()
                    WinOSTaskbarRealView(shell: shell, showingStartMenu: $showingStartMenu, currentTime: currentTime, pc: pc)
                }
                .zIndex(1000)
                
                // Start Menu
                if showingStartMenu {
                    WinOSStartMenuRealView(shell: shell, showingStartMenu: $showingStartMenu)
                        .transition(.move(edge: .bottom).combined(with: .opacity))
                        .zIndex(1001)
                }
            }
            .onAppear {
                startDesktop()
                startClock()
                updateDisplayMetrics(geo: geo)
                // Observa orientação
                NotificationCenter.default.addObserver(forName: UIDevice.orientationDidChangeNotification, object: nil, queue: .main) { _ in
                    updateDisplayMetrics(geo: geo)
                }
            }
            .onDisappear {
                shell.shutdown()
                timer?.invalidate()
                NotificationCenter.default.removeObserver(self, name: UIDevice.orientationDidChangeNotification, object: nil)
            }
            .onChange(of: geo.size) { oldSize, newSize in
                if newSize != lastGeoSize {
                    lastGeoSize = newSize
                    updateDisplayMetrics(geo: geo)
                }
            }
            // Input responsivo — screen -> desktop -> Win32
            .gesture(
                DragGesture(minimumDistance: 0, coordinateSpace: .local)
                    .onChanged { value in
                        // Usa handler responsivo que converte screen->desktop centralizado
                        shell.handleTouchResponsive(screenX: Double(value.location.x), screenY: Double(value.location.y), phase: value.translation == .zero ? "began" : "moved")
                    }
                    .onEnded { value in
                        shell.handleTouchResponsive(screenX: Double(value.location.x), screenY: Double(value.location.y), phase: "ended")
                    }
            )
            .simultaneousGesture(
                TapGesture(count: 2)
                    .onEnded { _ in
                        // Double tap — já tratado via state machine
                    }
            )
            .simultaneousGesture(
                LongPressGesture(minimumDuration: 0.6)
                    .onEnded { _ in
                        // Long press -> right click — já tratado via state machine
                    }
            )
        }
        .statusBarHidden()
        .persistentSystemOverlays(.hidden)
        .onReceive(NotificationCenter.default.publisher(for: UIDevice.orientationDidChangeNotification)) { _ in
            // Atualiza orientação
        }
    }
    
    private func updateDisplayMetrics(geo: GeometryProxy) {
        let size = geo.size
        let width = Double(size.width)
        let height = Double(size.height)
        // Detecta orientação
        let orient: WinOSOrientation = width > height ? .landscapeLeft : .portrait
        shell.handleDeviceScreenChange(width: width, height: height, scale: 3.0)
        shell.handleOrientationChange(orientation: orient, deviceWidth: width, deviceHeight: height)
        NSLog("[WINOS-DISPLAY] updateDisplayMetrics geo=%.0fx%.0f orient=%@ metrics=%@", width, height, orient.rawValue, shell.displayManager.metrics.description)
    }
    
    private var desktopIconsArea: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack(alignment: .top, spacing: 20) {
                VStack(spacing: 20) {
                    desktopIcon(icon: "folder.fill", title: "File Manager", subtitle: "C:\\") {
                        shell.openFileManager()
                    }
                    desktopIcon(icon: "pc", title: "This PC", subtitle: pc?.nome ?? "WinOS") {
                        shell.openFileManager()
                    }
                    desktopIcon(icon: "externaldrive.fill", title: "Games", subtitle: "C:\\Games") {
                        shell.fileManager.navigateToWindowsPath("C:\\Games")
                        shell.openFileManager()
                    }
                    desktopIcon(icon: "doc.text.magnifyingglass", title: "Diagnostics", subtitle: "System") {
                        // Abre diagnostics via shell
                    }
                }
                .padding(.leading, 20)
                .padding(.top, 20)
                
                Spacer()
            }
            Spacer()
        }
    }
    
    private func desktopIcon(icon: String, title: String, subtitle: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 6) {
                ZStack {
                    RoundedRectangle(cornerRadius: 10)
                        .fill(WinOSBrand.card.opacity(0.8))
                        .frame(width: 56, height: 56)
                    Image(systemName: icon)
                        .font(.system(size: 24))
                        .foregroundStyle(.white)
                }
                Text(title)
                    .font(.system(size: 11, weight: .medium, design: .rounded))
                    .foregroundStyle(.white)
                    .lineLimit(1)
                    .frame(width: 70)
                Text(subtitle)
                    .font(.system(size: 8, weight: .medium, design: .monospaced))
                    .foregroundStyle(WinOSBrand.textTertiary)
                    .lineLimit(1)
            }
        }
        .buttonStyle(.plain)
    }
    
    private func startDesktop() {
        let pcPath = pc?.caminho ?? "Environments/env-default"
        shell.initialize(pcPath: pcPath)
        NSLog("[WINOS-DESKTOP] Real desktop started pc=%@ path=%@", pc?.nome ?? "default", pcPath)
    }
    
    private func startClock() {
        timer = Timer.scheduledTimer(withTimeInterval: 1.0, repeats: true) { _ in
            currentTime = Date()
            shell.tick()
        }
    }
}

/// Janela REAL — com título, botões minimizar/maximizar/fechar, movimentação, foco, z-order, redimensionamento
struct WinOSWindowView: View {
    let window: WinOSWindow
    @ObservedObject var shell: WinOSDesktopShell
    @State private var dragOffset: CGSize = .zero
    @State private var isDragging = false
    
    var body: some View {
        ZStack(alignment: .top) {
            // Sombra
            RoundedRectangle(cornerRadius: 8)
                .fill(Color.black.opacity(0.3))
                .frame(width: CGFloat(window.width), height: CGFloat(window.height))
                .offset(x: 2, y: 2)
            
            // Janela
            VStack(spacing: 0) {
                // Title bar
                HStack(spacing: 8) {
                    Image(systemName: window.isFileManager ? "folder.fill" : "pc")
                        .font(.system(size: 12))
                        .foregroundStyle(.white.opacity(0.8))
                    
                    Text(window.title)
                        .font(.system(size: 12, weight: .medium, design: .rounded))
                        .foregroundStyle(.white)
                        .lineLimit(1)
                    
                    Spacer()
                    
                    // Botões reais: minimizar, maximizar, fechar
                    HStack(spacing: 4) {
                        Button {
                            shell.windowManager.minimizeWindow(id: window.id)
                        } label: {
                            Image(systemName: "minus")
                                .font(.system(size: 10, weight: .bold))
                                .foregroundStyle(.white.opacity(0.7))
                                .frame(width: 28, height: 28)
                                .background(Circle().fill(Color.white.opacity(0.1)))
                        }
                        .buttonStyle(.plain)
                        
                        Button {
                            if window.maximized {
                                shell.windowManager.restoreWindow(id: window.id)
                            } else {
                                shell.windowManager.maximizeWindow(id: window.id, desktopWidth: 1920, desktopHeight: 1080)
                            }
                        } label: {
                            Image(systemName: window.maximized ? "macwindow.on.rectangle" : "macwindow")
                                .font(.system(size: 10, weight: .bold))
                                .foregroundStyle(.white.opacity(0.7))
                                .frame(width: 28, height: 28)
                                .background(Circle().fill(Color.white.opacity(0.1)))
                        }
                        .buttonStyle(.plain)
                        
                        Button {
                            shell.closeWindow(id: window.id)
                        } label: {
                            Image(systemName: "xmark")
                                .font(.system(size: 10, weight: .bold))
                                .foregroundStyle(.white)
                                .frame(width: 28, height: 28)
                                .background(Circle().fill(WinOSBrand.danger))
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(.horizontal, 12)
                .frame(height: 28)
                .background(window.focused ? WinOSBrand.accent.opacity(0.9) : WinOSBrand.cardHighlight.opacity(0.9))
                .gesture(
                    DragGesture()
                        .onChanged { value in
                            if !isDragging {
                                isDragging = true
                                dragOffset = value.translation
                            } else {
                                let newX = window.x + Int32(value.translation.width - dragOffset.width)
                                let newY = window.y + Int32(value.translation.height - dragOffset.height)
                                shell.windowManager.moveWindow(id: window.id, x: newX, y: newY)
                                dragOffset = value.translation
                            }
                        }
                        .onEnded { _ in
                            isDragging = false
                            dragOffset = .zero
                        }
                )
                
                // Conteúdo da janela — se FileManager, mostra FileManagerRealView
                if window.isFileManager {
                    WinOSFileManagerRealContentView(shell: shell)
                        .frame(width: CGFloat(window.width), height: CGFloat(window.height - 28))
                } else {
                    // Conteúdo genérico para EXE
                    VStack {
                        Image(systemName: "pc")
                            .font(.system(size: 32))
                            .foregroundStyle(WinOSBrand.textTertiary)
                        Text(window.title)
                            .font(.system(size: 14, weight: .medium, design: .rounded))
                            .foregroundStyle(.white)
                        Text("PID: \(window.processID) | \(window.width)x\(window.height)")
                            .font(.system(size: 10, design: .monospaced))
                            .foregroundStyle(WinOSBrand.textTertiary)
                        Text("Executable: \(window.executablePath)")
                            .font(.system(size: 9, design: .monospaced))
                            .foregroundStyle(WinOSBrand.textTertiary)
                            .lineLimit(2)
                    }
                    .frame(width: CGFloat(window.width), height: CGFloat(window.height - 28))
                    .background(WinOSBrand.card.opacity(0.95))
                }
            }
            .frame(width: CGFloat(window.width), height: CGFloat(window.height))
            .background(RoundedRectangle(cornerRadius: 8).fill(WinOSBrand.card))
            .overlay(RoundedRectangle(cornerRadius: 8).stroke(window.focused ? WinOSBrand.accent : WinOSBrand.border, lineWidth: window.focused ? 2 : 1))
            .shadow(color: .black.opacity(0.4), radius: 12, x: 0, y: 6)
        }
        .frame(width: CGFloat(window.width), height: CGFloat(window.height))
    }
}

/// File Manager REAL content — usa VFS/Sandbox REAL
struct WinOSFileManagerRealContentView: View {
    @ObservedObject var shell: WinOSDesktopShell
    
    var body: some View {
        VStack(spacing: 0) {
            // Toolbar navegação real
            HStack(spacing: 8) {
                Button {
                    shell.fileManager.navigateBack()
                } label: {
                    Image(systemName: "chevron.left")
                        .foregroundStyle(shell.fileManager.historyIndex > 0 ? .white : .gray)
                }
                .disabled(shell.fileManager.historyIndex == 0)
                
                Button {
                    shell.fileManager.navigateForward()
                } label: {
                    Image(systemName: "chevron.right")
                        .foregroundStyle(shell.fileManager.historyIndex < shell.fileManager.history.count - 1 ? .white : .gray)
                }
                .disabled(shell.fileManager.historyIndex >= shell.fileManager.history.count - 1)
                
                Button {
                    shell.fileManager.navigateUp()
                } label: {
                    Image(systemName: "arrow.up")
                        .foregroundStyle(.white)
                }
                
                Text(shell.fileManager.currentWindowsPath)
                    .font(.system(size: 11, design: .monospaced))
                    .foregroundStyle(.white)
                    .lineLimit(1)
                
                Spacer()
                
                Button {
                    shell.fileManager.navigateToWindowsPath(shell.fileManager.currentWindowsPath)
                } label: {
                    Image(systemName: "arrow.clockwise")
                        .foregroundStyle(WinOSBrand.textSecondary)
                }
            }
            .padding(8)
            .background(WinOSBrand.cardHighlight)
            
            // Lista arquivos real
            if shell.fileManager.isLoading {
                ProgressView()
                    .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List {
                    ForEach(shell.fileManager.currentItems) { item in
                        HStack(spacing: 8) {
                            Image(systemName: item.isDirectory ? "folder.fill" : (item.isExecutable ? "pc" : "doc.fill"))
                                .foregroundStyle(item.isDirectory ? WinOSBrand.accent : (item.isExecutable ? WinOSBrand.success : WinOSBrand.textTertiary))
                            
                            VStack(alignment: .leading, spacing: 2) {
                                Text(item.name)
                                    .font(.system(size: 12, weight: .medium, design: .rounded))
                                    .foregroundStyle(.white)
                                    .lineLimit(1)
                                Text(item.windowsPath)
                                    .font(.system(size: 8, design: .monospaced))
                                    .foregroundStyle(WinOSBrand.textTertiary)
                                    .lineLimit(1)
                            }
                            
                            Spacer()
                            
                            VStack(alignment: .trailing, spacing: 2) {
                                Text(item.displaySize)
                                    .font(.system(size: 9, design: .monospaced))
                                    .foregroundStyle(WinOSBrand.textSecondary)
                                Text(item.ext.uppercased())
                                    .font(.system(size: 8, design: .monospaced))
                                    .foregroundStyle(WinOSBrand.textTertiary)
                            }
                        }
                        .contentShape(Rectangle())
                        .onTapGesture(count: 2) {
                            // Duplo clique → abrir
                            if item.isDirectory {
                                shell.fileManager.navigateToWindowsPath(item.windowsPath)
                            } else if item.isExecutable {
                                // File Manager → identifica PE → PELoader → WindowsPEBackend → Win32 process → WindowManager
                                shell.createWindowForExecutable(item)
                            }
                        }
                        .onTapGesture {
                            shell.fileManager.selectItem(item)
                        }
                        .listRowBackground(shell.fileManager.selectedItems.contains(item.path) ? WinOSBrand.accent.opacity(0.2) : WinOSBrand.card)
                    }
                }
                .listStyle(.plain)
                .background(WinOSBrand.background)
            }
            
            // Status bar
            HStack {
                Text("\(shell.fileManager.currentItems.count) itens")
                    .font(.system(size: 9, design: .monospaced))
                    .foregroundStyle(WinOSBrand.textTertiary)
                Spacer()
                if !shell.fileManager.lastError.isEmpty {
                    Text(shell.fileManager.lastError)
                        .font(.system(size: 8, design: .monospaced))
                        .foregroundStyle(WinOSBrand.danger)
                        .lineLimit(1)
                }
            }
            .padding(6)
            .background(WinOSBrand.cardHighlight)
        }
        .background(WinOSBrand.background)
        .onAppear {
            shell.fileManager.navigateToWindowsPath(shell.fileManager.currentWindowsPath)
        }
    }
}

/// Taskbar REAL — mostra Start, File Manager, apps abertas reais, focada, relógio, indicadores
struct WinOSTaskbarRealView: View {
    @ObservedObject var shell: WinOSDesktopShell
    @Binding var showingStartMenu: Bool
    var currentTime: Date
    var pc: EnvironmentProfile?
    
    var body: some View {
        HStack(spacing: 12) {
            // Start
            Button {
                showingStartMenu.toggle()
            } label: {
                HStack(spacing: 6) {
                    WinOSLogoView(size: 20, showText: false)
                    Text("Start")
                        .font(.system(size: 12, weight: .semibold, design: .rounded))
                        .foregroundStyle(.white)
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 6)
                .background(RoundedRectangle(cornerRadius: 8).fill(showingStartMenu ? WinOSBrand.accent : WinOSBrand.cardHighlight))
            }
            .buttonStyle(.plain)
            
            Divider()
                .frame(height: 24)
                .background(WinOSBrand.border)
            
            // File Manager
            Button {
                shell.openFileManager()
            } label: {
                Image(systemName: "folder.fill")
                    .font(.system(size: 16))
                    .foregroundStyle(.white)
                    .frame(width: 36, height: 28)
                    .background(RoundedRectangle(cornerRadius: 6).fill(WinOSBrand.cardHighlight))
            }
            .buttonStyle(.plain)
            
            // Apps abertas reais
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 6) {
                    ForEach(shell.windowManager.allWindows(), id: \.id) { win in
                        Button {
                            if win.minimized {
                                shell.windowManager.restoreWindow(id: win.id)
                            } else if win.focused {
                                shell.windowManager.minimizeWindow(id: win.id)
                            } else {
                                shell.windowManager.focusWindow(id: win.id)
                            }
                        } label: {
                            HStack(spacing: 4) {
                                Image(systemName: win.isFileManager ? "folder.fill" : "pc")
                                    .font(.system(size: 10))
                                Text(win.title)
                                    .font(.system(size: 10, weight: .medium, design: .rounded))
                                    .lineLimit(1)
                                    .frame(maxWidth: 80)
                            }
                            .foregroundStyle(.white)
                            .padding(.horizontal, 8)
                            .padding(.vertical, 4)
                            .background(
                                RoundedRectangle(cornerRadius: 6)
                                    .fill(win.focused ? WinOSBrand.accent : (win.minimized ? WinOSBrand.card.opacity(0.5) : WinOSBrand.cardHighlight))
                                    .overlay(RoundedRectangle(cornerRadius: 6).stroke(win.focused ? WinOSBrand.accent : Color.clear, lineWidth: 1))
                            )
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            
            Spacer()
            
            // Indicadores WinOS + relógio
            HStack(spacing: 8) {
                VStack(alignment: .trailing, spacing: 2) {
                    Text(currentTime, style: .time)
                        .font(.system(size: 11, weight: .medium, design: .monospaced))
                        .foregroundStyle(.white)
                    Text("\(shell.windowManager.windows.count) win | \(shell.processManager.processes.count) proc | FPS: \(String(format: "%.0f", shell.fps))")
                        .font(.system(size: 8, design: .monospaced))
                        .foregroundStyle(WinOSBrand.textTertiary)
                }
                
                WinOSLogoView(size: 20, showText: false)
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 8)
        .background(.ultraThinMaterial)
        .overlay(Rectangle().fill(WinOSBrand.border).frame(height: 1), alignment: .top)
    }
}

/// Start Menu REAL — File Manager, Settings, Diagnostics, Runtime Tests, Games, Applications, Power/Exit
struct WinOSStartMenuRealView: View {
    @ObservedObject var shell: WinOSDesktopShell
    @Binding var showingStartMenu: Bool
    
    var body: some View {
        VStack(alignment: .leading, spacing: 0) {
            Spacer()
            
            VStack(alignment: .leading, spacing: 0) {
                // Header
                HStack {
                    WinOSLogoView(size: 32, compact: true)
                    VStack(alignment: .leading, spacing: 2) {
                        Text("WinOS")
                            .font(.system(.headline, design: .rounded).weight(.bold))
                            .foregroundStyle(.white)
                        Text("Desktop Real — Win32/PE/VFS/Metal")
                            .font(.system(size: 9, design: .monospaced))
                            .foregroundStyle(WinOSBrand.textTertiary)
                    }
                    Spacer()
                    Button {
                        showingStartMenu = false
                    } label: {
                        Image(systemName: "xmark.circle.fill")
                            .foregroundStyle(WinOSBrand.textSecondary)
                    }
                }
                .padding(16)
                .background(WinOSBrand.cardHighlight)
                
                // Itens
                ScrollView {
                    VStack(alignment: .leading, spacing: 2) {
                        startItem(icon: "folder.fill", title: "File Manager", subtitle: "C:\\ — VFS Real") {
                            shell.openFileManager()
                            showingStartMenu = false
                        }
                        startItem(icon: "gearshape.fill", title: "Settings", subtitle: "Configurações") {
                            showingStartMenu = false
                        }
                        startItem(icon: "chart.bar.doc.horizontal", title: "Diagnostics", subtitle: "Runtime/Win32/VFS/Metal/FPS") {
                            showingStartMenu = false
                        }
                        startItem(icon: "checkmark.seal.fill", title: "Runtime Tests", subtitle: "3411 C + 76 PE") {
                            showingStartMenu = false
                        }
                        startItem(icon: "gamecontroller.fill", title: "Games", subtitle: "C:\\Games — PE x64/x86") {
                            shell.fileManager.navigateToWindowsPath("C:\\Games")
                            shell.openFileManager()
                            showingStartMenu = false
                        }
                        startItem(icon: "app.badge.fill", title: "Applications", subtitle: "Processos: \(shell.processManager.processes.count)") {
                            showingStartMenu = false
                        }
                        Divider()
                            .background(WinOSBrand.border)
                            .padding(.vertical, 8)
                        
                        // Processos reais
                        ForEach(shell.processManager.runningProcesses(), id: \.id) { proc in
                            HStack {
                                Image(systemName: proc.isFileManager ? "folder.fill" : "pc")
                                    .foregroundStyle(WinOSBrand.accent)
                                VStack(alignment: .leading, spacing: 2) {
                                    Text(proc.executable)
                                        .font(.system(size: 11, weight: .medium, design: .rounded))
                                        .foregroundStyle(.white)
                                        .lineLimit(1)
                                    Text("PID: \(proc.id) — \(proc.state.rawValue)")
                                        .font(.system(size: 8, design: .monospaced))
                                        .foregroundStyle(WinOSBrand.textTertiary)
                                }
                                Spacer()
                            }
                            .padding(.horizontal, 12)
                            .padding(.vertical, 4)
                        }
                        
                        Divider()
                            .background(WinOSBrand.border)
                            .padding(.vertical, 8)
                        
                        startItem(icon: "power", title: "Exit Runtime", subtitle: "Shutdown desktop", color: WinOSBrand.danger) {
                            shell.shutdown()
                            showingStartMenu = false
                        }
                    }
                    .padding(8)
                }
                
                // Footer com stats
                HStack {
                    Text("FPS: \(String(format: "%.1f", shell.fps)) | Win: \(shell.windowManager.windows.count) | Proc: \(shell.processManager.processes.count)")
                        .font(.system(size: 8, design: .monospaced))
                        .foregroundStyle(WinOSBrand.textTertiary)
                    Spacer()
                    Text("Renderer: \(shell.renderEngine.currentRenderer.rawValue)")
                        .font(.system(size: 8, design: .monospaced))
                        .foregroundStyle(WinOSBrand.textTertiary)
                }
                .padding(8)
                .background(WinOSBrand.card)
            }
            .frame(width: 320, height: 480)
            .background(RoundedRectangle(cornerRadius: 16).fill(WinOSBrand.card).shadow(color: .black.opacity(0.5), radius: 20, x: 0, y: 10))
            .overlay(RoundedRectangle(cornerRadius: 16).stroke(WinOSBrand.border, lineWidth: 1))
            .padding(.leading, 12)
            .padding(.bottom, 60) // acima da taskbar
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .bottomLeading)
        .background(Color.black.opacity(0.3).onTapGesture { showingStartMenu = false })
    }
    
    private func startItem(icon: String, title: String, subtitle: String, color: Color? = nil, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            HStack(spacing: 12) {
                ZStack {
                    RoundedRectangle(cornerRadius: 8)
                        .fill((color ?? WinOSBrand.accent).opacity(0.2))
                        .frame(width: 36, height: 36)
                    Image(systemName: icon)
                        .font(.system(size: 16))
                        .foregroundStyle(color ?? WinOSBrand.accent)
                }
                VStack(alignment: .leading, spacing: 2) {
                    Text(title)
                        .font(.system(size: 12, weight: .medium, design: .rounded))
                        .foregroundStyle(.white)
                    Text(subtitle)
                        .font(.system(size: 9, design: .monospaced))
                        .foregroundStyle(WinOSBrand.textTertiary)
                }
                Spacer()
                Image(systemName: "chevron.right")
                    .font(.system(size: 10))
                    .foregroundStyle(WinOSBrand.textTertiary)
            }
            .padding(.horizontal, 8)
            .padding(.vertical, 6)
            .background(RoundedRectangle(cornerRadius: 8).fill(Color.white.opacity(0.02)))
        }
        .buttonStyle(.plain)
    }
}


// MARK: - Cursor Visual REAL — acima das janelas, acompanha escala, portrait/landscape

struct WinOSCursorView: View {
    var cursor: WinOSMouseCursor
    var displayMetrics: WinOSDisplayMetrics
    
    var body: some View {
        ZStack {
            // Cursor arrow simples — visual real
            switch cursor.type {
            case .arrow:
                WinOSArrowCursor()
            case .hand:
                WinOSHandCursor()
            case .text:
                WinOSTextCursor()
            case .busy:
                WinOSBusyCursor()
            default:
                WinOSArrowCursor()
            }
            
            // Debug: mostra coordenadas quando dragging
            if cursor.isDragging {
                Circle()
                    .fill(Color.blue.opacity(0.3))
                    .frame(width: 20, height: 20)
            }
        }
        .frame(width: 20, height: 30)
        .opacity(cursor.visible ? 1 : 0)
        .scaleEffect(CGFloat(displayMetrics.scale)) // acompanha escala do desktop
    }
}

struct WinOSArrowCursor: View {
    var body: some View {
        // Arrow cursor desenhado com Path
        ZStack {
            Path { path in
                path.move(to: CGPoint(x: 0, y: 0))
                path.addLine(to: CGPoint(x: 0, y: 20))
                path.addLine(to: CGPoint(x: 4, y: 15))
                path.addLine(to: CGPoint(x: 7, y: 18))
                path.addLine(to: CGPoint(x: 8, y: 17))
                path.addLine(to: CGPoint(x: 5, y: 14))
                path.addLine(to: CGPoint(x: 9, y: 14))
                path.closeSubpath()
            }
            .fill(Color.white)
            .shadow(color: .black, radius: 1, x: 1, y: 1)
            
            Path { path in
                path.move(to: CGPoint(x: 0, y: 0))
                path.addLine(to: CGPoint(x: 0, y: 20))
                path.addLine(to: CGPoint(x: 4, y: 15))
                path.addLine(to: CGPoint(x: 7, y: 18))
                path.addLine(to: CGPoint(x: 8, y: 17))
                path.addLine(to: CGPoint(x: 5, y: 14))
                path.addLine(to: CGPoint(x: 9, y: 14))
                path.closeSubpath()
            }
            .stroke(Color.black, lineWidth: 0.5)
        }
        .frame(width: 16, height: 24)
    }
}

struct WinOSHandCursor: View {
    var body: some View {
        Image(systemName: "hand.point.up.left.fill")
            .font(.system(size: 16))
            .foregroundStyle(.white)
            .shadow(color: .black, radius: 1)
    }
}

struct WinOSTextCursor: View {
    var body: some View {
        Rectangle()
            .fill(Color.white)
            .frame(width: 2, height: 16)
            .shadow(color: .black, radius: 0.5)
    }
}

struct WinOSBusyCursor: View {
    @State private var rotation: Double = 0
    
    var body: some View {
        Circle()
            .trim(from: 0, to: 0.7)
            .stroke(Color.white, lineWidth: 2)
            .frame(width: 16, height: 16)
            .rotationEffect(.degrees(rotation))
            .onAppear {
                withAnimation(.linear(duration: 1).repeatForever(autoreverses: false)) {
                    rotation = 360
                }
            }
    }
}
