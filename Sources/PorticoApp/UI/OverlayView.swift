import SwiftUI
import PorticoCore

/// Overlay de execução: voltar, pausar/continuar, configurações, controles,
/// FPS, resolução, áudio, logs, encerrar jogo. Não interrompe o runtime
/// ao abrir (pausa é opcional).
struct OverlayView: View {
    @ObservedObject var session: SessionController
    let game: GameProfile
    @Binding var showOverlay: Bool
    let onEnd: () -> Void

    @State private var tab: Tab = .main
    @State private var showingLogs = false

    enum Tab { case main, audio, controls, info }

    var body: some View {
        VStack {
            Spacer()
            VStack(spacing: 14) {
                HStack {
                    Text(game.nome).font(.headline)
                    Spacer()
                    Button("Voltar") { showOverlay = false }
                }
                Divider()

                switch tab {
                case .main: mainTab
                case .audio: audioTab
                case .controls: controlsTab
                case .info: infoTab
                }

                Divider()
                HStack {
                    tabButton(.main, "jogos.circle", "Jogo")
                    tabButton(.audio, "speaker.wave.2", "Áudio")
                    tabButton(.controls, "gamecontroller", "Controles")
                    tabButton(.info, "info.circle", "Info")
                }
            }
            .padding(20)
            .background(.ultraThinMaterial, in: RoundedRectangle(cornerRadius: 22))
            .padding()
        }
    }

    private var mainTab: some View {
        VStack(spacing: 10) {
            HStack(spacing: 12) {
                Button {
                    session.togglePause()
                } label: {
                    Label(session.isPaused ? "Continuar" : "Pausar",
                          systemImage: session.isPaused ? "play.fill" : "pause.fill")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.borderedProminent)

                Button(role: .destructive) {
                    onEnd()
                } label: {
                    Label("Encerrar jogo", systemImage: "power")
                        .frame(maxWidth: .infinity)
                }
                .buttonStyle(.bordered)
            }
            Button {
                showingLogs = true
            } label: {
                Label("Logs da sessão", systemImage: "doc.text.magnifyingglass")
                    .frame(maxWidth: .infinity)
            }
            .sheet(isPresented: $showingLogs) {
                NavigationStack { LogsView() }
            }
        }
    }

    private var audioTab: some View {
        VStack(spacing: 10) {
            Toggle("Som", isOn: Binding(get: { !session.muted },
                                        set: { session.setMuted(!$0) }))
            Slider(value: Binding(get: { session.volume },
                                  set: { session.setVolume($0) }), in: 0...1) {
                Text("Volume \(Int(session.volume * 100))%")
            }
        }
    }

    private var controlsTab: some View {
        VStack(alignment: .leading, spacing: 8) {
            Toggle("Controles na tela", isOn: .constant(session.showTouchControls))
                .disabled(true)
            Text("Layout: \(game.controles.elements.count) elementos")
                .font(.caption)
                .foregroundStyle(.secondary)
            Text("Gamepads físicos são detectados automaticamente (Bluetooth/MFi). "
                 + "O layout por jogo pode ser ajustado em Configurações → Controles.")
                .font(.caption2)
                .foregroundStyle(.secondary)
        }
    }

    private var infoTab: some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack {
                metric("FPS", String(format: "%.0f", session.fps))
                metric("Frames", "\(session.framesPresented)")
            }
            HStack {
                metric("Resolução", session.resolutionText)
                metric("Estado", session.isPaused ? "pausado" : "executando")
            }
        }
    }

    private func metric(_ title: String, _ value: String) -> some View {
        VStack {
            Text(value).font(.title3.bold().monospacedDigit())
            Text(title).font(.caption2).foregroundStyle(.secondary)
        }
        .frame(maxWidth: .infinity)
        .padding(8)
        .background(Color.primary.opacity(0.06), in: RoundedRectangle(cornerRadius: 10))
    }

    private func tabButton(_ t: Tab, _ icon: String, _ label: String) -> some View {
        Button {
            tab = t
        } label: {
            VStack(spacing: 2) {
                Image(systemName: icon)
                Text(label).font(.caption2)
            }
            .frame(maxWidth: .infinity)
            .foregroundStyle(tab == t ? Color.accentColor : Color.secondary)
        }
    }
}
