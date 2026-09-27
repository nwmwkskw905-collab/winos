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
        guard state == .idle || state == .stopped || state == .failed else {
            throw RuntimeFailure.backendUnavailable(reason: "já existe sessão ativa")
        }
        setState(.preparing)
        log.info("runtime", "preparando sessão para '\(profile.nome)'")

        let env = environments.environment(id: profile.ambiente.id)
        environments.markInUse(id: profile.ambiente.id, inUse: true)
        let envVars = environments.mergedVariables(
            environmentID: profile.ambiente.id,
            gameOverrides: config.environmentVariables)

        let exeURL: URL
        do {
            let dir = try sandbox.resolveInside(profile.caminho)
            exeURL = dir.appendingPathComponent(profile.executavel)
        } catch {
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
        guard let (chosen, verdict) = backendRegistry.select(for: image), verdict.canRun else {
            let reason = backendRegistry.backends
                .map { $0.canExecute(image).reason }
                .first(where: { !$0.isEmpty })
                ?? "nenhum backend disponível"
            log.error("runtime", "sem backend para \(image.label): \(reason)")
            setState(.failed)
            events.onFailure?(.unsupported(reason: reason))
            throw RuntimeFailure.unsupported(reason: reason)
        }

        log.info("runtime", "backend selecionado: \(chosen.displayName) (\(verdict.reason))")

        do {
            try chosen.start(context: ctx)
        } catch let failure as RuntimeFailure {
            setState(.failed)
            events.onFailure?(failure)
            throw failure
        } catch {
            setState(.failed)
            let f = RuntimeFailure.backendUnavailable(reason: "\(error)")
            events.onFailure?(f)
            throw f
        }

        let proc = processManager.spawn(label: profile.nome) { [weak self] in
            self?.endGame(clockNow: clockNow)
        }
        proc.markRunning()

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
    }

    private func fpsTarget(profile: GameProfile, config: EffectiveConfig) -> Int {
        if let v = profile.fps.value { return min(max(v, 15), 480) }
        return config.quality.suggestedFPS
    }

    private static func detectImage(kind: GameKind, arch: String,
                                    executableURL: URL) -> SoftwareImage {
        switch kind {
        case .windowsPE: return .windowsPE(PEImage.mockForRefusal(arch))
        case .pxpNative, .selfTest: return .pxpNative
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

        let result = backend.stepFrame(input: input, dt: dt,
                                       timeMs: (now - sessionStart) * 1000)
        backend.drainLogs(into: log)
        framesPresented = result.framesPresented

        if let audio = optionalAudio(backend: backend, config: ctx.config) {
            onAudioFrames?(audio)
        }

        // FPS real
        framesSinceFPS += 1
        if now - fpsSampleStart >= 0.5 {
            currentFPS = Double(framesSinceFPS) / (now - fpsSampleStart)
            framesSinceFPS = 0
            fpsSampleStart = now
            events.onFPS?(currentFPS)
        }

        switch result.state {
        case .stopped:
            if result.halted {
                log.info("runtime", "software encerrou a execução normalmente")
            }
            activeProcess?.markFinished(exitCode: 0)
            setState(.stopped)
            events.onFinished?()
        case .failed:
            let failure = result.failure ?? .guestFault(detail: result.message)
            log.error("runtime", failure.technicalDetail)
            activeProcess?.markFailed(reason: failure.technicalDetail)
            setState(.failed)
            events.onFailure?(failure)
            events.onFinished?()
        default:
            break
        }

        return backend.consumeGraphicsFrame()
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
