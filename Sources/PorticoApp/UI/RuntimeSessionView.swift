import SwiftUI
import QuartzCore
import PorticoCore

/// Tela de execução: jogo (Metal) + controles + overlay configurável.
/// O overlay NÃO interrompe o runtime; a pausa é explícita.
struct RuntimeSessionView: View {
    @EnvironmentObject var model: AppModel
    @Environment(\.dismiss) private var dismiss
    let game: GameProfile

    @StateObject private var session: SessionController
    @State private var showOverlay = false

    init(game: GameProfile) {
        self.game = game
        _session = StateObject(wrappedValue: SessionController(game: game))
    }

    var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()

            // Superfície Metal
            MTKGameView(renderScale: session.renderScale) { renderer in
                session.attach(renderer: renderer)
            }
            .ignoresSafeArea()

            // Estado de carregamento visível (START -> Starting -> Initializing runtime -> Initializing graphics -> Running)
            if session.state != "Running" && session.lastFailure == nil {
                VStack(spacing: 16) {
                    ProgressView()
                        .tint(.white)
                        .scaleEffect(1.5)
                    Text(session.state)
                        .font(.headline)
                        .foregroundStyle(.white)
                    Text(session.detailedState)
                        .font(.caption)
                        .foregroundStyle(.white.opacity(0.7))
                        .multilineTextAlignment(.center)
                        .padding(.horizontal)
                    Text("[WINOS-GFX-INIT] \(session.state)")
                        .font(.system(size: 9, design: .monospaced))
                        .foregroundStyle(.white.opacity(0.4))
                }
                .padding(24)
                .background(RoundedRectangle(cornerRadius: 16).fill(Color.black.opacity(0.7)))
            }

            // Controles virtuais
            if session.showTouchControls && session.state == "Running" {
                VirtualControlsView(
                    layout: game.controles,
                    router: session.router,
                    opacityScale: showOverlay ? 0.35 : 1.0)
                    .ignoresSafeArea()
            }

            // Botão do overlay
            VStack {
                HStack {
                    VStack(alignment: .leading, spacing: 2) {
                        Text("FPS: \(String(format: "%.1f", session.fps))")
                            .font(.system(size: 10, design: .monospaced))
                            .foregroundStyle(.white.opacity(0.6))
                        Text(session.state)
                            .font(.system(size: 9, design: .monospaced))
                            .foregroundStyle(.white.opacity(0.4))
                    }
                    .padding(.leading, 12)
                    Spacer()
                    Button {
                        withAnimation(.easeInOut(duration: 0.2)) { showOverlay.toggle() }
                    } label: {
                        Image(systemName: "ellipsis.circle.fill")
                            .font(.system(size: 34))
                            .foregroundStyle(.white.opacity(0.7))
                            .padding(12)
                    }
                    .accessibilityLabel("Abrir menu do jogo")
                }
                Spacer()
            }

            // Overlay WinOS — identidade própria
            if showOverlay {
                WinOSOverlayView(
                    session: session,
                    game: game,
                    showOverlay: $showOverlay,
                    onEnd: {
                        session.end()
                        dismiss()
                    })
                .transition(.opacity)
            }

            // Falha de execução — WinOS branded com detalhes técnicos completos
            if let failure = session.lastFailure {
                ScrollView {
                    VStack(spacing: 16) {
                        WinOSLogoView(size: 48, showText: false)
                        Image(systemName: "xmark.octagon.fill")
                            .font(.system(size: 44))
                            .foregroundStyle(WinOSBrand.danger)
                        Text("WINOS RUNTIME ERROR")
                            .font(.system(.headline, design: .monospaced).weight(.bold))
                            .foregroundStyle(.white)
                        
                        VStack(alignment: .leading, spacing: 8) {
                            errorRow(label: "Stage", value: failure.stage)
                            errorRow(label: "Error", value: failure.code)
                            errorRow(label: "Code", value: "\(failure.code)")
                            errorRow(label: "File", value: game.executavel)
                            errorRow(label: "Runtime Path", value: game.caminho)
                            errorRow(label: "Architecture", value: game.arquiteturaExe)
                            errorRow(label: "PE Type", value: "\(game.tipo)")
                            errorRow(label: "Win32", value: failure.stage == "WIN32_INIT" ? "FAIL — \(failure.technicalDetail)" : "partial")
                            errorRow(label: "Graphics", value: "Metal — init OK")
                            errorRow(label: "Storage", value: game.caminho)
                            errorRow(label: "Details", value: failure.technicalDetail)
                        }
                        .padding(12)
                        .background(RoundedRectangle(cornerRadius: 10).fill(Color.black.opacity(0.5)))
                        
                        Text(failure.userMessage)
                            .font(.footnote)
                            .foregroundStyle(WinOSBrand.textSecondary)
                            .multilineTextAlignment(.leading)
                            .frame(maxWidth: .infinity, alignment: .leading)
                            .padding(.horizontal)
                        
                        HStack(spacing: 12) {
                            WinOSSecondaryButton(title: "Diagnostics", systemImage: "chart.bar.doc.horizontal") {
                                // Mostra diagnostics
                                NSLog("[WINOS-RUNTIME] Diagnostics requested for failure: %@", failure.technicalDetail)
                            }
                            WinOSPrimaryButton(title: "Retry", systemImage: "arrow.clockwise") {
                                session.retry()
                            }
                        }
                        WinOSPrimaryButton(title: "Voltar à biblioteca", systemImage: "arrow.left") {
                            session.end()
                            dismiss()
                        }
                    }
                    .padding(24)
                    .background(
                        RoundedRectangle(cornerRadius: 20)
                            .fill(WinOSBrand.card)
                            .overlay(RoundedRectangle(cornerRadius: 20).stroke(WinOSBrand.danger.opacity(0.5), lineWidth: 1))
                    )
                    .padding()
                }
            }
        }
        .statusBarHidden()
        .persistentSystemOverlays(.hidden)
        .onAppear { session.begin() }
        .onDisappear { session.end() }
    }
    
    private func errorRow(label: String, value: String) -> some View {
        HStack(alignment: .top, spacing: 8) {
            Text("\(label):")
                .font(.system(size: 10, weight: .bold, design: .monospaced))
                .foregroundStyle(WinOSBrand.accent)
                .frame(width: 90, alignment: .leading)
            Text(value)
                .font(.system(size: 10, weight: .medium, design: .monospaced))
                .foregroundStyle(.white.opacity(0.8))
                .multilineTextAlignment(.leading)
                .lineLimit(4)
            Spacer()
        }
    }
}

/// Controla uma sessão de execução: liga RuntimeManager, entrada, áudio e
/// gamepads ao jogo atual.
@MainActor
final class SessionController: ObservableObject {
    let game: GameProfile
    let router = InputRouter()
    let gamePads = GameControllerBridge()
    let audio = AVAudioEngineBackend()

    @Published var lastFailure: RuntimeFailure?
    @Published var fps: Double = 0
    @Published var framesPresented: UInt32 = 0
    @Published var isPaused = false
    @Published var volume: Double = 0.8
    @Published var muted = false

    private weak var renderer: MetalGameRenderer?
    private var displayTimer: Timer?
    private var started = false

    var showTouchControls: Bool { game.controles.showTouchControls }
    var renderScale: Double { 1.0 }
    var resolutionText: String { game.resolucao.description }

    init(game: GameProfile) {
        self.game = game
        self.volume = game.audio.volume
    }

    func attach(renderer: MetalGameRenderer) {
        self.renderer = renderer
    }

    @Published var state: String = "START"
    @Published var detailedState: String = "Inicializando..."

    func begin() {
        guard !started else { return }
        started = true
        state = "Starting"
        detailedState = "Inicializando runtime..."
        NSLog("[WINOS-RUNTIME-START] begin game=%@ tipo=%@ exe=%@", game.nome, "\(game.tipo)", game.executavel)

        guard let model = AppModel.shared else {
            NSLog("[WINOS-RUNTIME-ERROR] AppModel.shared nil")
            lastFailure = .backendUnavailable(reason: "AppModel.shared não inicializado")
            state = "Error"
            detailedState = "AppModel não inicializado"
            return
        }
        let config = model.effectiveConfig(for: game)
        NSLog("[WINOS-RUNTIME-START] effectiveConfig res=%@ fps=%@ renderer=%@", "\(config.resolution)", "\(config.fps)", "\(config.renderer)")

        model.runtime.events.onFPS = { [weak self] v in
            Task { @MainActor in
                self?.fps = v
                // NSLog("[WINOS-GFX-FRAME] FPS=%.1f", v)
            }
        }
        model.runtime.events.onFailure = { [weak self] f in
            NSLog("[WINOS-RUNTIME-ERROR] onFailure: %@ - %@", f.userMessage, f.technicalDetail)
            Task { @MainActor in
                self?.lastFailure = f
                self?.state = "Error"
                self?.detailedState = f.userMessage
            }
        }
        model.runtime.events.onStateChange = { [weak self] s in
            NSLog("[WINOS-RUNTIME-START] stateChange: %@", "\(s)")
            Task { @MainActor in
                self?.state = "\(s)"
            }
        }
        // Áudio: RuntimeManager expõe onAudioFrames diretamente, não em events
        model.runtime.onAudioFrames = { [weak self] pcm in
            // NSLog("[WINOS-GFX-FRAME] audio frames=%d", pcm.count)
            self?.audio.enqueue(interleaved: pcm)
        }

        do {
            state = "Initializing runtime"
            detailedState = "Preparando ambiente..."
            NSLog("[WINOS-RUNTIME-START] start profile=%@ config=%@", game.nome, "\(config)")
            try model.runtime.start(profile: game, config: config,
                                    environments: model.environments,
                                    clockNow: CACurrentMediaTime())
            state = "Initializing graphics"
            detailedState = "Inicializando Metal..."
            NSLog("[WINOS-GFX-INIT] Runtime started, initializing audio")
            try audio.initialize(sampleRate: Double(game.audio.sampleRate),
                                 bufferFrames: game.audio.bufferFrames)
            try audio.start()
            NSLog("[WINOS-GFX-INIT] Audio OK, attaching gamepads")
            gamePads.attach(router: router, enabled: config.physicalControllersEnabled)
            state = "Running"
            detailedState = "Executando - \(game.nome)"
            NSLog("[WINOS-RUNTIME-START] Runtime running, starting display loop fps=%d", model.config.effectiveFPS(for: game))
            startDisplayLoop(fps: model.config.effectiveFPS(for: game))
        } catch let failure as RuntimeFailure {
            NSLog("[WINOS-RUNTIME-ERROR] RuntimeFailure: %@ - %@", failure.userMessage, failure.technicalDetail)
            lastFailure = failure
            state = "Error"
            detailedState = failure.userMessage
        } catch {
            NSLog("[WINOS-RUNTIME-ERROR] Unknown error: %@", "\(error)")
            lastFailure = .backendUnavailable(reason: "\(error)")
            state = "Error"
            detailedState = "\(error)"
        }
    }

    private func startDisplayLoop(fps: Int) {
        NSLog("[WINOS-GFX-INIT] startDisplayLoop fps=%d", fps)
        renderer?.setVSyncLimit(fps)
        // O loop de frames é o draw(in:) do MTKView; aqui ligamos o tick do
        // runtime à taxa do CADisplayLink do MTKView via Timer de alta resolução
        // que apenas prepara o próximo GfxFrame.
        // Usa background queue para não bloquear UI thread (evita GetMessage blocking)
        let interval = 1.0 / Double(max(fps, 30))
        NSLog("[WINOS-GFX-FRAME] Timer interval=%.4f", interval)
        displayTimer = Timer.scheduledTimer(withTimeInterval: interval, repeats: true) { [weak self] _ in
            // Executa tick em background para não bloquear UI, mas atualiza renderer na main
            DispatchQueue.global(qos: .userInteractive).async {
                guard let self = self else { return }
                if self.isPaused { return }
                guard let model = AppModel.shared else { return }
                let frame = model.runtime.tick(now: CACurrentMediaTime(), input: self.router.state)
                DispatchQueue.main.async {
                    if let frame {
                        self.renderer?.execute(frame)
                        // NSLog("[WINOS-GFX-FRAME] frame executed")
                    }
                    self.framesPresented = model.runtime.framesPresented
                }
            }
        }
        NSLog("[WINOS-GFX-FRAME] Display loop started")
    }

    private func step() {
        guard !isPaused else { return }
        guard let model = AppModel.shared else { return }
        NSLog("[WINOS-GFX-FRAME] step called")
        let frame = model.runtime.tick(now: CACurrentMediaTime(), input: router.state)
        if let frame {
            renderer?.execute(frame)
            NSLog("[WINOS-GFX-FRAME] frame executed via step")
        }
        framesPresented = model.runtime.framesPresented
    }

    func retry() {
        lastFailure = nil
        state = "Retrying"
        detailedState = "Tentando novamente..."
        end()
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
            self.started = false
            self.begin()
        }
    }
    
    func togglePause() {
        guard let model = AppModel.shared else { return }
        if isPaused {
            model.runtime.resume()
            isPaused = false
        } else {
            model.runtime.pause()
            isPaused = true
        }
    }

    func setVolume(_ v: Double) {
        volume = v
        audio.setMix(AudioMixState(masterVolume: 1, gameVolume: v, muted: muted))
    }

    func setMuted(_ m: Bool) {
        muted = m
        audio.setMix(AudioMixState(masterVolume: 1, gameVolume: volume, muted: m))
    }

    func end() {
        displayTimer?.invalidate()
        displayTimer = nil
        audio.stop()
        gamePads.detach()
        AppModel.shared?.runtime.endGame(clockNow: CACurrentMediaTime())
        started = false
    }
}
