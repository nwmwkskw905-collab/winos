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

            // Controles virtuais
            if session.showTouchControls {
                VirtualControlsView(
                    layout: game.controles,
                    router: session.router,
                    opacityScale: showOverlay ? 0.35 : 1.0)
                    .ignoresSafeArea()
            }

            // Botão do overlay
            VStack {
                HStack {
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

            // Falha de execução — WinOS branded
            if let failure = session.lastFailure {
                VStack(spacing: 16) {
                    WinOSLogoView(size: 48, showText: false)
                    Image(systemName: "xmark.octagon.fill")
                        .font(.system(size: 44))
                        .foregroundStyle(WinOSBrand.danger)
                    Text("Falha na execução")
                        .font(.system(.headline, design: .rounded).weight(.bold))
                        .foregroundStyle(.white)
                    Text(failure.userMessage)
                        .font(.footnote)
                        .foregroundStyle(WinOSBrand.textSecondary)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal)
                    Text(failure.technicalDetail)
                        .font(.system(size: 10, weight: .medium, design: .monospaced))
                        .foregroundStyle(WinOSBrand.textTertiary)
                        .multilineTextAlignment(.center)
                        .padding(.horizontal)
                    WinOSPrimaryButton(title: "Voltar à biblioteca", systemImage: "arrow.left") {
                        session.end()
                        dismiss()
                    }
                }
                .padding(24)
                .background(
                    RoundedRectangle(cornerRadius: 20)
                        .fill(WinOSBrand.card)
                        .overlay(RoundedRectangle(cornerRadius: 20).stroke(WinOSBrand.border, lineWidth: 1))
                )
                .padding()
            }
        }
        .statusBarHidden()
        .persistentSystemOverlays(.hidden)
        .onAppear { session.begin() }
        .onDisappear { session.end() }
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

    func begin() {
        guard !started else { return }
        started = true

        guard let model = AppModel.shared else {
            lastFailure = .backendUnavailable(reason: "AppModel.shared não inicializado")
            return
        }
        let config = model.effectiveConfig(for: game)

        model.runtime.events.onFPS = { [weak self] v in
            Task { @MainActor in self?.fps = v }
        }
        model.runtime.events.onFailure = { [weak self] f in
            Task { @MainActor in self?.lastFailure = f }
        }
        // Áudio: RuntimeManager expõe onAudioFrames diretamente, não em events
        model.runtime.onAudioFrames = { [weak self] pcm in
            self?.audio.enqueue(interleaved: pcm)
        }

        do {
            try model.runtime.start(profile: game, config: config,
                                    environments: model.environments,
                                    clockNow: CACurrentMediaTime())
            try audio.initialize(sampleRate: Double(game.audio.sampleRate),
                                 bufferFrames: game.audio.bufferFrames)
            try audio.start()
            gamePads.attach(router: router, enabled: config.physicalControllersEnabled)
            startDisplayLoop(fps: model.config.effectiveFPS(for: game))
        } catch let failure as RuntimeFailure {
            lastFailure = failure
        } catch {
            lastFailure = .backendUnavailable(reason: "\(error)")
        }
    }

    private func startDisplayLoop(fps: Int) {
        renderer?.setVSyncLimit(fps)
        // O loop de frames é o draw(in:) do MTKView; aqui ligamos o tick do
        // runtime à taxa do CADisplayLink do MTKView via Timer de alta resolução
        // que apenas prepara o próximo GfxFrame.
        let interval = 1.0 / Double(max(fps, 30))
        displayTimer = Timer.scheduledTimer(withTimeInterval: interval, repeats: true) {
            [weak self] _ in
            Task { @MainActor in self?.step() }
        }
    }

    private func step() {
        guard !isPaused else { return }
        guard let model = AppModel.shared else { return }
        let frame = model.runtime.tick(now: CACurrentMediaTime(), input: router.state)
        if let frame {
            renderer?.execute(frame)
        }
        framesPresented = model.runtime.framesPresented
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
