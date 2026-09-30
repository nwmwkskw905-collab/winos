import SwiftUI
#if !PORTICO_XCODE_MONOLITHIC
import PorticoCore
#endif
/// Configurações individuais do jogo: gráficos, áudio, controles, ambiente, avançado.
struct GameSettingsView: View {
    @Environment(\.dismiss) private var dismiss
    @Binding var game: GameProfile
    let onSave: (GameProfile) -> Void
    @EnvironmentObject var model: AppModel
    @State private var showingControlEditor = false

    var body: some View {
        NavigationStack {
            Form {
                Section("Gráficos") {
                    Picker("Resolução", selection: $game.resolucao) {
                        ForEach(Resolution.presets, id: \.description) { r in
                            Text(r.description).tag(r)
                        }
                    }
                    Stepper("FPS: \(game.fps.value.map(String.init) ?? "ilimitado")",
                            value: fpsBinding, in: 0...240)
                    Picker("Renderer", selection: $game.renderer) {
                        ForEach(RendererChoice.allCases, id: \.self) { r in
                            Text(r.displayName).tag(r)
                        }
                    }
                    if game.renderer != .metal {
                        Text(availabilityText(for: game.renderer))
                            .font(.caption)
                            .foregroundStyle(.orange)
                    }
                }

                Section("Áudio") {
                    Toggle("Áudio ligado", isOn: $game.audio.enabled)
                    Slider(value: $game.audio.volume, in: 0...1) {
                        Text("Volume \(Int(game.audio.volume * 100))%")
                    }
                    Picker("Taxa de amostragem", selection: $game.audio.sampleRate) {
                        Text("48 kHz").tag(48000)
                        Text("44,1 kHz").tag(44100)
                    }
                    Stepper("Buffer: \(game.audio.bufferFrames) frames",
                            value: $game.audio.bufferFrames, in: 128...4096, step: 128)
                }

                Section("Controles") {
                    Toggle("Controles na tela", isOn: $game.controles.showTouchControls)
                    Toggle("Controles físicos (Bluetooth/MFi)",
                           isOn: $game.controles.physicalControllersEnabled)
                    Button("Editar controles…") { showingControlEditor = true }
                }

                Section("Ambiente") {
                    Picker("Ambiente/Prefixo", selection: $game.ambiente) {
                        ForEach(model.environments.environments) { env in
                            Text(env.nome).tag(EnvironmentRef(id: env.id, name: env.nome))
                        }
                    }
                    NavigationLink("Gerenciar ambientes…") {
                        EnvironmentView()
                    }
                }

                Section("Avançado") {
                    Toggle("Log detalhado (DEBUG)", isOn: $game.opcoes.debugLogging)
                    Toggle("Modo seguro", isOn: $game.opcoes.safeMode)
                    Stepper("Orçamento CPU/frame: \(game.opcoes.maxInstructionsPerFrame / 1000)k instruções",
                            value: cpuBudgetBinding,
                            in: 10...25000, step: 10)
                    NavigationLink("Variáveis de ambiente (\(game.opcoes.environmentVariables.count))") {
                        EnvVarsEditor(vars: $game.opcoes.environmentVariables)
                    }
                }
            }
            .navigationTitle("Configurações")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .confirmationAction) {
                    Button("Salvar") {
                        onSave(game)
                        dismiss()
                    }
                }
            }
            .sheet(isPresented: $showingControlEditor) {
                ControlEditorView(profile: $game.controles)
            }
        }
    }

    private var fpsBinding: Binding<Int> {
        Binding(
            get: { game.fps.value ?? 0 },
            set: { game.fps = $0 == 0 ? .unlimited : .cap($0) }
        )
    }

    private var cpuBudgetBinding: Binding<Int> {
        Binding(
            get: { Int(game.opcoes.maxInstructionsPerFrame / 1000) },
            set: { game.opcoes.maxInstructionsPerFrame = UInt32($0 * 1000) }
        )
    }

    private func availabilityText(for renderer: RendererChoice) -> String {
        switch renderer.availability {
        case .supported: return ""
        case .partial(let r): return "Parcial: \(r)"
        case .notSupported(let r): return "Indisponível: \(r)"
        }
    }
}

/// Editor simples de variáveis de ambiente do jogo.
struct EnvVarsEditor: View {
    @Binding var vars: [String: String]
    @State private var newKey = ""
    @State private var newValue = ""

    var body: some View {
        List {
            ForEach(vars.keys.sorted(), id: \.self) { key in
                VStack(alignment: .leading) {
                    Text(key).font(.body.monospaced())
                    Text(vars[key] ?? "")
                        .font(.caption.monospaced())
                        .foregroundStyle(.secondary)
                }
            }
            .onDelete { idx in
                for i in idx {
                    let k = vars.keys.sorted()[i]
                    vars.removeValue(forKey: k)
                }
            }
            Section("Nova variável") {
                TextField("Nome", text: $newKey)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                TextField("Valor", text: $newValue)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                Button("Adicionar") {
                    let k = newKey.trimmingCharacters(in: .whitespaces)
                    guard !k.isEmpty else { return }
                    vars[k] = newValue
                    newKey = ""
                    newValue = ""
                }
            }
        }
        .navigationTitle("Variáveis")
    }
}
