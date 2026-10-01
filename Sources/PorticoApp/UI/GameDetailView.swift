import SwiftUI
#if !PORTICO_XCODE_MONOLITHIC
import PorticoCore
#endif
/// Tela do jogo: capa/ícone, nome, Jogar, configurações, informações, logs.
struct GameDetailView: View {
    @EnvironmentObject var model: AppModel
    @State var game: GameProfile
    @State private var showingEditor = false
    @State private var showingSettings = false
    @State private var showingLogs = false

    var body: some View {
        List {
            Section {
                HStack(spacing: 16) {
                    ZStack {
                        RoundedRectangle(cornerRadius: 14)
                            .fill(Color.accentColor.gradient)
                            .frame(width: 96, height: 96)
                        Image(systemName: game.tipo == .windowsPE ? "pc" : "cpu")
                            .font(.system(size: 36))
                            .foregroundStyle(.white)
                    }
                    VStack(alignment: .leading, spacing: 4) {
                        Text(game.nome).font(.title2.bold())
                        Text(game.tipo == .windowsPE
                             ? "Windows PE · \(game.arquiteturaExe)"
                             : "Payload nativo PXP")
                            .font(.caption)
                            .foregroundStyle(.secondary)
                        if game.tipo == .windowsPE {
                            Label("Win32 parcial disponível (WindowsPEBackend)",
                                  systemImage: "exclamationmark.triangle")
                                .font(.caption2)
                                .foregroundStyle(.orange)
                        }
                    }
                }
                Button {
                    model.launch(game)
                } label: {
                    Label("Jogar", systemImage: "play.fill")
                        .frame(maxWidth: .infinity)
                        .font(.headline)
                }
                .buttonStyle(.borderedProminent)
            }

            Section("Configuração") {
                LabeledContent("Resolução", value: game.resolucao.description)
                LabeledContent("FPS",
                    value: game.fps.value.map(String.init) ?? "ilimitado")
                LabeledContent("Renderer", value: game.renderer.displayName)
                LabeledContent("Áudio",
                    value: game.audio.enabled ? "ligado (\(Int(game.audio.volume * 100))%)" : "desligado")
                LabeledContent("Ambiente", value: game.ambiente.name)
                Button("Editar configurações…") { showingSettings = true }
            }

            Section("Informações") {
                LabeledContent("Executável", value: game.executavel)
                if !game.argumentos.isEmpty {
                    LabeledContent("Argumentos", value: game.argumentos)
                }
                LabeledContent("Arquitetura", value: game.arquiteturaExe)
                LabeledContent("Status de compatibilidade", value: game.compatibilityLabel)
                LabeledContent("Pasta", value: game.caminho)
                LabeledContent("Tamanho",
                    value: "\(game.tamanhoInstalacao / 1024) KiB")
                LabeledContent("Criado em",
                    value: game.dataCriacao.formatted(date: .abbreviated, time: .shortened))
                if let last = game.ultimaExecucao {
                    LabeledContent("Última execução",
                        value: last.formatted(date: .abbreviated, time: .shortened))
                }
                if !game.notas.isEmpty {
                    Text(game.notas)
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                }
                Button("Editar perfil…") { showingEditor = true }
            }

            Section("Diagnóstico") {
                Button {
                    showingLogs = true
                } label: {
                    Label("Logs", systemImage: "doc.text.magnifyingglass")
                }
            }
        }
        .navigationTitle(game.nome)
        .navigationBarTitleDisplayMode(.inline)
        .sheet(isPresented: $showingEditor) {
            GameEditorView(game: game) { updated in
                model.updateGame(updated)
                game = updated
            }
        }
        .sheet(isPresented: $showingSettings) {
            GameSettingsView(game: $game) { updated in
                model.updateGame(updated)
            }
        }
        .sheet(isPresented: $showingLogs) {
            NavigationStack { LogsView() }
        }
        .fullScreenCover(item: $model.sessionToRun, onDismiss: {}) { g in
            RuntimeSessionView(game: g)
        }
    }
}
