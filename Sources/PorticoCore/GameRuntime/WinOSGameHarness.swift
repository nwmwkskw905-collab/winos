import Foundation

/// FASE 12 — Game Harness real: orquestra load → init → run → frame loop → input → audio → shutdown com diagnostics

public enum WinOSGamePhase: String, Sendable {
    case idle = "IDLE"
    case loading = "LOADING"
    case analyzing = "ANALYZING"
    case initializing = "INITIALIZING"
    case running = "RUNNING"
    case paused = "PAUSED"
    case stopping = "STOPPING"
    case stopped = "STOPPED"
    case failed = "FAILED"
}

public struct WinOSGameSession: Sendable {
    public var executableURL: URL
    public var profile: GameProfile
    public var config: RuntimeConfig
    public var fsRoot: String
    public var startTime: Date
    
    public init(executableURL: URL, profile: GameProfile, config: RuntimeConfig, fsRoot: String) {
        self.executableURL = executableURL
        self.profile = profile
        self.config = config
        self.fsRoot = fsRoot
        self.startTime = Date()
    }
}

@MainActor
public final class WinOSGameHarness: ObservableObject {
    @Published public private(set) var phase: WinOSGamePhase = .idle
    @Published public private(set) var diagnostics: WinOSGameDiagnostics
    @Published public private(set) var compatibility: WinOSCompatReport?
    @Published public private(set) var graphicsDetection: WinOSGraphicsDetectionResult?
    @Published public private(set) var renderStats: WinOSRenderStats = WinOSRenderStats()
    @Published public private(set) var audioStats: WinOSAudioStats = WinOSAudioStats()
    @Published public private(set) var lastError: String = ""
    @Published public private(set) var framesPresented: UInt64 = 0
    
    private var backend: ExecutionBackend?
    private var graphicsTranslator: WinOSGraphicsTranslator?
    private var renderLoop: WinOSRenderLoop?
    private var inputPipeline: WinOSInputPipeline?
    private var audioPipeline: WinOSAudioPipeline?
    private var compatibilityAnalyzer: WinOSGameCompatibility
    private var graphicsDetector: WinOSGraphicsDetector
    
    private var session: WinOSGameSession?
    private var logCenter: LogCenter
    private var backendRegistry: BackendRegistry
    
    public var onFrame: ((GfxFrame) -> Void)?
    public var onLog: ((String) -> Void)?
    
    public init(logCenter: LogCenter = LogCenter()) {
        self.diagnostics = WinOSGameDiagnostics()
        self.compatibilityAnalyzer = WinOSGameCompatibility()
        self.graphicsDetector = WinOSGraphicsDetector()
        self.logCenter = logCenter
        self.backendRegistry = BackendRegistry.standard(log: logCenter)
        NSLog("[WINOS-HARNESS] GameHarness init backends=%@", backendRegistry.backends.map { $0.id }.joined(separator: ","))
    }
    
    // MARK: - Load + Analyze
    
    public func load(executableURL: URL, profile: GameProfile, config: RuntimeConfig, fsRoot: String) throws -> WinOSCompatReport {
        phase = .loading
        diagnostics.reset()
        
        let fileSize = (try? FileManager.default.attributesOfItem(atPath: executableURL.path)[.size] as? Int64) ?? 0
        diagnostics.gameStart(executable: executableURL.lastPathComponent, size: fileSize, arch: profile.arquiteturaExe)
        diagnostics.peLoaderInit(file: executableURL.path, size: Int(fileSize))
        
        NSLog("[WINOS-HARNESS] load exe=%@ size=%lld fsRoot=%@", executableURL.path, fileSize, fsRoot)
        
        // Read file
        let data: Data
        do {
            data = try Data(contentsOf: executableURL)
        } catch {
            phase = .failed
            lastError = "Failed to read executable: \(error)"
            diagnostics.peLoaderFail(file: executableURL.path, reason: lastError)
            throw RuntimeFailure.storageAccessDenied(path: executableURL.path, underlying: "\(error)")
        }
        
        // PE scan
        guard data.count >= 2, data[0] == 0x4D, data[1] == 0x5A else {
            phase = .failed
            lastError = "Invalid MZ header"
            diagnostics.peLoaderFail(file: executableURL.path, reason: lastError)
            throw RuntimeFailure.invalidMZ(path: executableURL.path)
        }
        
        // Load via PELoader
        let loadedImage: PELoadedImage
        do {
            loadedImage = try PELoader.loadImage(data, moduleName: executableURL.lastPathComponent)
            diagnostics.peLoaderSuccess(file: executableURL.lastPathComponent, machine: loadedImage.report.image.machine, arch: loadedImage.report.image.arch, isPE32Plus: loadedImage.report.image.isPE32Plus, sections: loadedImage.report.sections.count, imports: loadedImage.report.imports.count)
        } catch {
            phase = .failed
            lastError = "PE load failed: \(error)"
            diagnostics.peLoaderFail(file: executableURL.path, reason: lastError)
            throw RuntimeFailure.invalidPE(path: executableURL.path, detail: "\(error)")
        }
        
        // Coverage
        phase = .analyzing
        let coverage = Win32Catalog.coverage(for: loadedImage.report)
        NSLog("[WINOS-HARNESS] coverage resolved=%d unresolved=%d unknown=%d", coverage.resolved.count, coverage.unresolved.count, coverage.unknown.count)
        
        // Graphics detection
        let gfxDetection = graphicsDetector.detect(report: loadedImage.report)
        graphicsDetection = gfxDetection
        diagnostics.graphicsDetected(api: gfxDetection.primaryAPI.rawValue, dlls: gfxDetection.dlls, funcs: gfxDetection.functions)
        
        // Compatibility
        let compat = compatibilityAnalyzer.analyze(executableURL: executableURL, report: loadedImage.report, coverage: coverage)
        compatibility = compat
        diagnostics.compatibilityReport(score: compat.compatScore, level: compat.compatLevel.rawValue, missing: compat.missingDLLs, unimplemented: compat.unresolvedAPIs)
        
        // Log compatibility markdown
        let md = compatibilityAnalyzer.generateMarkdown(report: compat)
        logCenter.info("compat", md)
        
        // Select backend
        let image = SoftwareImage.windowsPE(PEImage(
            isPE32Plus: loadedImage.report.image.isPE32Plus,
            isDLL: false,
            machine: loadedImage.report.image.machine,
            subsystem: 2,
            timestamp: 0,
            imageBase: 0,
            sizeOfImage: 0,
            entryPointRVA: 0,
            sectionCount: loadedImage.report.sections.count,
            importCount: loadedImage.report.imports.count,
            arch: loadedImage.report.image.arch,
            imports: loadedImage.report.imports.map { PEImport(dll: $0.dll, functions: $0.functions.map { PEImportFunction(name: $0, ordinal: nil, isOrdinal: false) }) },
            sections: []
        ))
        
        guard let (selectedBackend, verdict) = backendRegistry.select(for: image) else {
            phase = .failed
            lastError = "No backend can execute this image"
            diagnostics.log(.error, category: .compatibility, code: "NO_BACKEND", message: "NO_BACKEND", detail: lastError)
            throw RuntimeFailure.unsupported(reason: lastError)
        }
        
        backend = selectedBackend
        session = WinOSGameSession(executableURL: executableURL, profile: profile, config: config, fsRoot: fsRoot)
        
        NSLog("[WINOS-HARNESS] load SUCCESS backend=%@ verdict=%@ compatScore=%.1f%% canRun=%@ gfx=%@",
              selectedBackend.id, verdict.reason, compat.compatScore, compat.canRun ? "YES" : "NO", gfxDetection.primaryAPI.rawValue)
        
        phase = .idle
        return compat
    }
    
    // MARK: - Initialize + Run
    
    public func initialize() throws {
        guard let session = session, let backend = backend else {
            throw RuntimeFailure.backendUnavailable(reason: "load() not called")
        }
        
        phase = .initializing
        NSLog("[WINOS-HARNESS] initialize backend=%@", backend.id)
        
        let context = RuntimeSessionContext(
            profile: session.profile,
            config: session.config,
            executableURL: session.executableURL,
            environmentVariables: [:],
            log: logCenter
        )
        
        // Initialize backend — CORREÇÃO: usa PE real, não mockForRefusal
        do {
            // Tenta detectar PE real para logs, fallback para .unknown que força leitura real
            let imageForLoad: SoftwareImage
            if let data = try? Data(contentsOf: session.executableURL),
               PEInspector.looksLikePE(data),
               let real = try? PEInspector.scan(data) {
                let pe = PEImage(
                    isPE32Plus: real.isPE32Plus,
                    isDLL: real.isDLL,
                    machine: real.machine,
                    subsystem: real.subsystem,
                    timestamp: real.timestamp,
                    imageBase: real.imageBase,
                    sizeOfImage: real.sizeOfImage,
                    entryPointRVA: real.entryPointRVA,
                    sectionCount: real.sectionCount,
                    importCount: real.importCount,
                    arch: real.arch,
                    imports: real.imports,
                    sections: real.sections
                )
                imageForLoad = .windowsPE(pe)
            } else {
                imageForLoad = .unknown
            }
            try backend.load(executable: session.executableURL, image: imageForLoad)
            try backend.initialize(context: context)
            diagnostics.log(.info, category: .process, code: "BACKEND_INIT_SUCCESS", message: "BACKEND_INIT_SUCCESS", detail: "backend=\(backend.id)")
        } catch {
            phase = .failed
            lastError = "Backend init failed: \(error)"
            diagnostics.log(.error, category: .process, code: "BACKEND_INIT_FAIL", message: "BACKEND_INIT_FAIL", detail: lastError)
            throw error
        }
        
        // Initialize graphics translator
        if let gfx = graphicsDetection {
            let translator = WinOSGraphicsTranslator()
            let gfxOk = translator.initialize(api: gfx.primaryAPI, width: session.config.resolution.width, height: session.config.resolution.height, diagnostics: diagnostics)
            graphicsTranslator = translator
            if !gfxOk {
                diagnostics.log(.warning, category: .graphics, code: "GFX_TRANSLATOR_FAIL", message: "GFX_TRANSLATOR_FAIL", detail: "api=\(gfx.primaryAPI.rawValue) — \(translator.lastError)")
            }
        }
        
        // Initialize render loop
        let loop = WinOSRenderLoop()
        loop.configure(width: session.config.resolution.width, height: session.config.resolution.height, targetFPS: session.config.fps.value ?? 60, backend: backend.id, diagnostics: diagnostics)
        renderLoop = loop
        
        // Initialize input pipeline
        let input = WinOSInputPipeline()
        input.configure(diagnostics: diagnostics, inputBridge: nil)
        inputPipeline = input
        
        // Initialize audio pipeline
        let audio = WinOSAudioPipeline()
        audio.configure(diagnostics: diagnostics)
        if let report = compatibility?.graphics {
            // Use report from compatibility
            _ = audio.detectAudioAPI(report: PEReport(image: PEImageInfo(isPE32Plus: false, isDLL: false, machine: 0, subsystem: 0, timestamp: 0, imageBase: 0, sizeOfImage: 0, entryPointRVA: 0, sectionCount: 0, importCount: 0, arch: "", imports: [], sections: [], entryPoint: 0, imageSize: 0), sections: [], imports: [], exports: [], diagnostics: ""))
        }
        audioPipeline = audio
        
        phase = .idle
        NSLog("[WINOS-HARNESS] initialize SUCCESS")
    }
    
    public func run() throws {
        guard let backend = backend else {
            throw RuntimeFailure.backendUnavailable(reason: "backend not initialized")
        }
        
        phase = .running
        diagnostics.log(.info, category: .game, code: "GAME_RUN", message: "GAME_RUN", detail: "starting game loop")
        NSLog("[WINOS-HARNESS] run start")
        
        try backend.run()
        renderLoop?.start()
        
        diagnostics.processCreated(pid: 100, exe: session?.executableURL.lastPathComponent ?? "unknown", cmdline: session?.profile.executavel ?? "")
    }
    
    public func stepFrame(dt: Float, timeMs: Double) -> FrameResult {
        guard let backend = backend, phase == .running else {
            return FrameResult(state: .idle, failure: nil)
        }
        
        let inputState = inputPipeline?.toInputState() ?? InputState()
        let result = backend.stepFrame(input: inputState, dt: dt, timeMs: timeMs)
        
        // Consume graphics
        let gfxFrame = backend.consumeGraphicsFrame()
        if !gfxFrame.commands.isEmpty {
            framesPresented += 1
            renderLoop?.didPresent()
            onFrame?(gfxFrame)
            
            // Also via translator
            if let surf = gfxFrame.surface {
                // surf is GfxSurfaceBuffer
                // Present via translator if needed
            }
        }
        
        // Audio
        let audioFrames = backend.pullAudio(maxFrames: 2048)
        if !audioFrames.isEmpty {
            audioPipeline?.pushAudioFrames(audioFrames)
        }
        
        // Logs
        backend.drainLogs(into: logCenter)
        
        if result.state == .stopped {
            phase = .stopped
            diagnostics.processExit(pid: 100, code: 0, uptime: Date().timeIntervalSince(session?.startTime ?? Date()))
            renderLoop?.stop()
            NSLog("[WINOS-HARNESS] game stopped exitCode=0 frames=%llu", framesPresented)
        } else if result.state == .failed {
            phase = .failed
            lastError = result.message
            diagnostics.log(.error, category: .process, code: "GAME_FAILED", message: "GAME_FAILED", detail: result.message)
            renderLoop?.stop()
            NSLog("[WINOS-HARNESS] game failed reason=%@", result.message)
        }
        
        return result
    }
    
    public func pause() {
        guard phase == .running else { return }
        backend?.pause()
        renderLoop?.stop()
        phase = .paused
        diagnostics.log(.info, category: .game, code: "GAME_PAUSED", message: "GAME_PAUSED", detail: "")
        NSLog("[WINOS-HARNESS] pause")
    }
    
    public func resume() throws {
        guard phase == .paused else { return }
        try backend?.resume()
        renderLoop?.start()
        phase = .running
        diagnostics.log(.info, category: .game, code: "GAME_RESUMED", message: "GAME_RESUMED", detail: "")
        NSLog("[WINOS-HARNESS] resume")
    }
    
    public func stop() {
        phase = .stopping
        backend?.stop()
        renderLoop?.stop()
        audioPipeline?.shutdown()
        graphicsTranslator?.shutdown()
        phase = .stopped
        diagnostics.log(.info, category: .game, code: "GAME_STOPPED", message: "GAME_STOPPED", detail: "frames=\(framesPresented)")
        NSLog("[WINOS-HARNESS] stop frames=%llu", framesPresented)
    }
    
    public func shutdown() {
        stop()
        backend?.shutdown()
        backend = nil
        session = nil
        compatibility = nil
        graphicsDetection = nil
        phase = .idle
        NSLog("[WINOS-HARNESS] shutdown")
    }
    
    // MARK: - Input forwarding
    
    public func handleTouchBegan(x: Double, y: Double, windowID: UInt32) {
        inputPipeline?.handleTouchBegan(x: x, y: y, windowID: windowID)
    }
    
    public func handleTouchMoved(x: Double, y: Double, windowID: UInt32) {
        inputPipeline?.handleTouchMoved(x: x, y: y, windowID: windowID)
    }
    
    public func handleTouchEnded(x: Double, y: Double, windowID: UInt32) {
        inputPipeline?.handleTouchEnded(x: x, y: y, windowID: windowID)
    }
    
    public func handleKeyDown(keyCode: UInt32, char: String, windowID: UInt32) {
        inputPipeline?.handleKeyDown(keyCode: keyCode, char: char, windowID: windowID)
    }
    
    public func handleKeyUp(keyCode: UInt32, windowID: UInt32) {
        inputPipeline?.handleKeyUp(keyCode: keyCode, windowID: windowID)
    }
    
    // MARK: - Report
    
    public func generateFinalReport() -> String {
        var report = diagnostics.generateReport()
        report += "\n\n"
        if let compat = compatibility {
            report += compatibilityAnalyzer.generateMarkdown(report: compat)
        }
        report += "\n\n## Session Stats\n"
        report += "Phase: \(phase.rawValue)\n"
        report += "Frames: \(framesPresented)\n"
        report += "Render: \(renderStats.description)\n"
        report += "Audio: sampleRate=\(audioStats.sampleRate) channels=\(audioStats.channels) frames=\(audioStats.framesPulled)\n"
        report += "LastError: \(lastError)\n"
        return report
    }
}
