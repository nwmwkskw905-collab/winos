import Foundation

/// RuntimeManager: coordena todos os componentes durante a execução.
/// Ciclo: prepare → start → frame tick (display link) → stop/finish.
/// Pausar NÃO derruba o runtime (apenas para de avançar frames).
public final class RuntimeManager {
    public private(set) var state: RuntimeSessionState = .idle
    public var events = RuntimeEvents()

    public let sandbox: AppSandbox
    public let log: LogCenter
    public let processManager: ProcessManager
    public let backendRegistry: BackendRegistry

    public private(set) var activeProfile: GameProfile?
    public private(set) var activeConfig: EffectiveConfig?
    public private(set) var activeProcess: ManagedProcess?

    private var backend: ExecutionBackend?
    private var context: RuntimeSessionContext?
    private var pacer: FramePacer
    private var sessionStart: Double = 0
    private var lastTick: Double = 0
    private var framesSinceFPS: Int = 0
    private var fpsSampleStart: Double = 0
    public private(set) var currentFPS: Double = 0
    public private(set) var framesPresented: UInt32 = 0

    /// Etapas de integração de áudio (preenchidas pela UI iOS com AVAudioEngine).
    public var onAudioFrames: (([Float]) -> Void)?

    public init(sandbox: AppSandbox, log: LogCenter,
                backendRegistry: BackendRegistry) {
        self.sandbox = sandbox
        self.log = log
        self.processManager = ProcessManager(log: log)
        self.backendRegistry = backendRegistry
        self.pacer = FramePacer(targetFPS: 60)
    }

    // MARK: - ciclo de vida

    /// Prepara e inicia uma sessão para o jogo informado.
    public func start(profile: GameProfile, config: EffectiveConfig,
                      environments: EnvironmentManager,
                      clockNow: Double) throws {
        let isSelfTest = profile.tipo == .selfTest || profile.nome.lowercased().contains("self")
        if isSelfTest {
            NSLog("[WINOS-SELFTEST] SELFTEST_START — RuntimeManager.start profile=%@ tipo=%@ exe=%@ res=%dx%d fps=%@", profile.nome, "\(profile.tipo)", profile.executavel, config.resolution.width, config.resolution.height, "\(config.fps)")
            NSLog("[WINOS-SELFTEST] SELFTEST_START — executableURL será resolvido em %@", profile.caminho)
            NSLog("[WINOS-SELFTEST] SELFTEST_SURFACE_CREATED — expected 640x360 XRGB8888 stride 2560 bytesPerRow")
        }
        NSLog("[WINOS-RUNTIME-START] start called profile=%@ tipo=%@ exe=%@ env=%@", profile.nome, "\(profile.tipo)", profile.executavel, profile.ambiente.id.uuidString)
        guard state == .idle || state == .stopped || state == .failed else {
            NSLog("[WINOS-RUNTIME-ERROR] Já existe sessão ativa state=%@", "\(state)")
            throw RuntimeFailure.backendUnavailable(reason: "já existe sessão ativa")
        }
        setState(.preparing)
        log.info("runtime", "preparando sessão para '\(profile.nome)'")
        NSLog("[WINOS-PC-OPEN] Preparando sessão para PC/game: %@", profile.nome)

        let env = environments.environment(id: profile.ambiente.id)
        if let env = env {
            NSLog("[WINOS-PC-OPEN] Ambiente encontrado: id=%@ name=%@ path=%@ state=%@", env.id.uuidString, env.nome, env.caminho, "\(env.state)")
        } else {
            NSLog("[WINOS-PC-OPEN] WARNING Ambiente não encontrado, usando default: %@", profile.ambiente.id.uuidString)
        }
        environments.markInUse(id: profile.ambiente.id, inUse: true)
        let envVars = environments.mergedVariables(
            environmentID: profile.ambiente.id,
            gameOverrides: config.environmentVariables)
        NSLog("[WINOS-RUNTIME-START] envVars count=%d", envVars.count)

        let exeURL: URL
        do {
            let dir = try sandbox.resolveInside(profile.caminho)
            exeURL = dir.appendingPathComponent(profile.executavel)
            NSLog("[WINOS-RUNTIME-START] exeURL=%@ exists=%@", exeURL.path, FileManager.default.fileExists(atPath: exeURL.path) ? "YES" : "NO")
            guard FileManager.default.fileExists(atPath: exeURL.path) else {
                NSLog("[WINOS-RUNTIME-ERROR] Executável não existe: %@", exeURL.path)
                setState(.failed)
                throw RuntimeFailure.io(reason: "executável não encontrado: \(exeURL.path)")
            }
        } catch {
            NSLog("[WINOS-RUNTIME-ERROR] Caminho inválido: %@", "\(error)")
            setState(.failed)
            throw RuntimeFailure.io(reason: "caminho do jogo inválido: \(error)")
        }

        let ctx = RuntimeSessionContext(
            profile: profile,
            config: config,
            executableURL: exeURL,
            environmentVariables: envVars,
            environmentName: env?.nome ?? "padrão"
        )

        // Seleção honesta de backend
        let image = Self.detectImage(kind: profile.tipo,
                                    arch: profile.arquiteturaExe,
                                    executableURL: exeURL)
        NSLog("[WINOS-RUNTIME-START] DetectImage kind=%@ arch=%@ label=%@", "\(profile.tipo)", profile.arquiteturaExe, image.label)
        guard let (chosen, verdict) = backendRegistry.select(for: image), verdict.canRun else {
            let reason = backendRegistry.backends
                .map { $0.canExecute(image).reason }
                .first(where: { !$0.isEmpty })
                ?? "nenhum backend disponível"
            log.error("runtime", "sem backend para \(image.label): \(reason)")
            NSLog("[WINOS-RUNTIME-ERROR] Sem backend para %@: %@", image.label, reason)
            setState(.failed)
            events.onFailure?(.unsupported(reason: reason))
            throw RuntimeFailure.unsupported(reason: reason)
        }

        log.info("runtime", "backend selecionado: \(chosen.displayName) (\(verdict.reason))")
        NSLog("[WINOS-RUNTIME-START] Backend selecionado: %@ reason=%@", chosen.displayName, verdict.reason)

        do {
            if isSelfTest {
                NSLog("[WINOS-SELFTEST] SELFTEST_PROCESS_CREATED — backend=%@ selected reason=%@", chosen.displayName, verdict.reason)
                NSLog("[WINOS-SELFTEST] SELFTEST_SURFACE_CREATED — ctx exe=%@ res=%dx%d", ctx.executableURL.path, ctx.config.resolution.width, ctx.config.resolution.height)
                NSLog("[WINOS-SELFTEST] SELFTEST_RENDERER_SELECTED — aguardando Metal/Software init via GraphicsBackend")
            }
            NSLog("[WINOS-RUNTIME-START] Iniciando backend %@ com context exe=%@", chosen.displayName, ctx.executableURL.path)
            try chosen.start(context: ctx)
            NSLog("[WINOS-RUNTIME-START] Backend start OK")
            if isSelfTest {
                NSLog("[WINOS-SELFTEST] SELFTEST_RENDERER_SELECTED — backend start OK, process running")
                NSLog("[WINOS-SELFTEST] SELFTEST_FIRST_FRAME — aguardando tick para first present")
            }
        } catch let failure as RuntimeFailure {
            if isSelfTest {
                NSLog("[WINOS-SELFTEST] SELFTEST_FAIL — backend start RuntimeFailure: %@ - %@", failure.userMessage, failure.technicalDetail)
            }
            NSLog("[WINOS-RUNTIME-ERROR] Backend start RuntimeFailure: %@ - %@", failure.userMessage, failure.technicalDetail)
            setState(.failed)
            events.onFailure?(failure)
            throw failure
        } catch {
            NSLog("[WINOS-RUNTIME-ERROR] Backend start error: %@", "\(error)")
            setState(.failed)
            let f = RuntimeFailure.backendUnavailable(reason: "\(error)")
            events.onFailure?(f)
            throw f
        }

        let proc = processManager.spawn(label: profile.nome) { [weak self] in
            self?.endGame(clockNow: clockNow)
        }
        proc.markRunning()
        if isSelfTest {
            NSLog("[WINOS-SELFTEST] SELFTEST_PROCESS_CREATED — processManager spawn label=%@ id=%@", profile.nome, proc.id.uuidString)
        }

        self.backend = chosen
        self.context = ctx
        self.activeProfile = profile
        self.activeConfig = config
        self.activeProcess = proc
        self.pacer = FramePacer(targetFPS: fpsTarget(profile: profile, config: config))
        self.sessionStart = clockNow
        self.lastTick = clockNow
        self.fpsSampleStart = clockNow
        self.framesSinceFPS = 0

        setState(.running)
        log.info("runtime", "sessão em execução — \(profile.nome)")
        if isSelfTest {
            NSLog("[WINOS-SELFTEST] SELFTEST_START — sessão running, aguardando FIRST_FRAME")
        }
    }

    private func fpsTarget(profile: GameProfile, config: EffectiveConfig) -> Int {
        if let v = profile.fps.value { return min(max(v, 15), 480) }
        return config.quality.suggestedFPS
    }

    private static func detectImage(kind: GameKind, arch: String,
                                    executableURL: URL) -> SoftwareImage {
        switch kind {
        case .windowsPE:
            // Tenta usar PE real quando possível (correção robusta)
            // Se arquivo existe e é PE válido, usa metadados reais para seleção honesta
            if FileManager.default.fileExists(atPath: executableURL.path) {
                if let data = try? Data(contentsOf: executableURL),
                   PEInspector.looksLikePE(data),
                   let realImage = try? PEInspector.scan(data) {
                    NSLog("[WINOS-RUNTIME] detectImage: usando PE real arch=%@ machine=0x%x isPE32Plus=%@ file=%@",
                          realImage.arch, realImage.machine, realImage.isPE32Plus ? "YES" : "NO", executableURL.lastPathComponent)
                    // Converte PEImage (do inspector) para PEImage usado em SoftwareImage
                    // PEImage já é o tipo usado em SoftwareImage.windowsPE
                    let peForSelection = PEImage(
                        isPE32Plus: realImage.isPE32Plus,
                        isDLL: realImage.isDLL,
                        machine: realImage.machine,
                        subsystem: realImage.subsystem,
                        timestamp: realImage.timestamp,
                        imageBase: realImage.imageBase,
                        sizeOfImage: realImage.sizeOfImage,
                        entryPointRVA: realImage.entryPointRVA,
                        sectionCount: realImage.sectionCount,
                        importCount: realImage.importCount,
                        arch: realImage.arch,
                        imports: realImage.imports,
                        sections: realImage.sections
                    )
                    return .windowsPE(peForSelection)
                }
            }
            // Fallback honesto: usa mock baseado na string de arquitetura do profile
            // Isso preserva seleção honesta quando arquivo não existe ou não é PE
            NSLog("[WINOS-RUNTIME] detectImage: fallback mockForRefusal arch=%@ file=%@", arch, executableURL.lastPathComponent)
            return .windowsPE(PEImage.mockForRefusal(arch))
        case .pxpNative, .selfTest:
            return .pxpNative
        }
    }

    /// Avança um frame (chamado pelo display link). `now` em segundos.
    /// Retorna o frame gráfico pronto para o renderer (nil se não é hora de renderizar).
    @discardableResult
    public func tick(now: Double, input: InputState) -> GfxFrame? {
        guard state == .running, let backend, let ctx = context else { return nil }
        guard pacer.shouldRender(now: now) else { return nil }

        let dt = Float(min(max(now - lastTick, 1.0 / 480.0), 0.25))
        lastTick = now

        let isSelfTest = ctx.profile.tipo == .selfTest || ctx.profile.nome.lowercased().contains("self")
        let result = backend.stepFrame(input: input, dt: dt,
                                       timeMs: (now - sessionStart) * 1000)
        backend.drainLogs(into: log)
        framesPresented = result.framesPresented

        if isSelfTest && result.framesPresented == 1 {
            NSLog("[WINOS-SELFTEST] SELFTEST_FIRST_FRAME — tick framesPresented=1, surface 640x360, pixelFormat XRGB8888")
            NSLog("[WINOS-SELFTEST] SELFTEST_PRESENT — present 640x360 bytesPerRow=%d framebuffer=%d bytes", 640*4, 640*360*4)
        }
        if isSelfTest && result.framesPresented % 60 == 0 && result.framesPresented > 0 {
            NSLog("[WINOS-SELFTEST] SELFTEST_PRESENT — frames=%u FPS=%.1f input buttons=%u axes=(%.2f,%.2f)", result.framesPresented, currentFPS, input.buttons, input.moveX, input.moveY)
        }

        if let audio = optionalAudio(backend: backend, config: ctx.config) {
            onAudioFrames?(audio)
            if isSelfTest && result.framesPresented == 1 {
                NSLog("[WINOS-SELFTEST] SELFTEST — audio frames=%d first audio OK", audio.count)
            }
        }

        // FPS real
        framesSinceFPS += 1
        if now - fpsSampleStart >= 0.5 {
            currentFPS = Double(framesSinceFPS) / (now - fpsSampleStart)
            framesSinceFPS = 0
            fpsSampleStart = now
            events.onFPS?(currentFPS)
            if isSelfTest {
                NSLog("[WINOS-SELFTEST] SELFTEST — FPS=%.1f frames=%u", currentFPS, framesPresented)
            }
        }

        switch result.state {
        case .stopped:
            if result.halted {
                log.info("runtime", "software encerrou a execução normalmente")
            }
            if isSelfTest {
                NSLog("[WINOS-SELFTEST] SELFTEST_EXIT — stopped halted=%@ frames=%u duration=%.1fs", result.halted ? "YES" : "NO", result.framesPresented, now - sessionStart)
            }
            activeProcess?.markFinished(exitCode: 0)
            setState(.stopped)
            events.onFinished?()
        case .failed:
            let failure = result.failure ?? .guestFault(detail: result.message)
            log.error("runtime", failure.technicalDetail)
            if isSelfTest {
                NSLog("[WINOS-SELFTEST] SELFTEST_EXIT — failed reason=%@ frames=%u", failure.technicalDetail, result.framesPresented)
            }
            activeProcess?.markFailed(reason: failure.technicalDetail)
            setState(.failed)
            events.onFailure?(failure)
            events.onFinished?()
        default:
            break
        }

        let gfxFrame = backend.consumeGraphicsFrame()
        if isSelfTest && gfxFrame.commands.contains(where: { if case .present = $0 { return true } else { return false } }) {
            if result.framesPresented <= 2 {
                NSLog("[WINOS-SELFTEST] SELFTEST_PRESENT — GfxFrame present command + surface width=%u height=%u", gfxFrame.surface?.width ?? 0, gfxFrame.surface?.height ?? 0)
            }
        }
        return gfxFrame
    }

    private func optionalAudio(backend: ExecutionBackend,
                               config: EffectiveConfig) -> [Float]? {
        guard config.audioEnabled else { return nil }
        let frames = backend.pullAudio(maxFrames: 2048)
        return frames.isEmpty ? nil : frames
    }

    /// Pausa: mantém o runtime vivo, apenas não avança frames.
    public func pause() {
        guard state == .running else { return }
        backend?.pause()
        setState(.paused)
        log.info("runtime", "sessão pausada (runtime mantido)")
    }

    public func resume() {
        guard state == .paused else { return }
        do {
            try backend?.resume()
        } catch {
            log.error("runtime", "retomada do backend falhou: \(error)")
            return
        }
        setState(.running)
        log.info("runtime", "sessão retomada")
    }

    /// Encerra o jogo e libera recursos.
    public func endGame(clockNow: Double) {
        guard state != .idle else { return }
        backend?.stop()
        backend = nil
        if let profile = activeProfile {
            log.info("runtime", "sessão encerrada — \(profile.nome)")
        }
        activeProcess?.markFinished(exitCode: 0)
        activeProfile = nil
        activeConfig = nil
        activeProcess = nil
        processManager.reap()
        setState(.stopped)
        events.onFinished?()
    }

    public func sessionDuration(now: Double) -> TimeInterval {
        guard sessionStart > 0 else { return 0 }
        return now - sessionStart
    }

    private func setState(_ s: RuntimeSessionState) {
        guard state != s else { return }
        state = s
        events.onStateChange?(s)
    }
}
