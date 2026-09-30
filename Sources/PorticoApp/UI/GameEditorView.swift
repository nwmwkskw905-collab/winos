import SwiftUI
#if !PORTICO_XCODE_MONOLITHIC
import PorticoCore
#endif
/// Edição dos dados do perfil do jogo.
struct GameEditorView: View {
    @Environment(\.dismiss) private var dismiss
    @State var game: GameProfile
    let onSave: (GameProfile) -> Void

    var body: some View {
        NavigationStack {
            Form {
                Section("Identificação") {
                    TextField("Nome", text: $game.nome)
                    TextField("Notas", text: $game.notas, axis: .vertical)
                        .lineLimit(3...6)
                }
                Section("Execução") {
                    TextField("Executável (relativo à pasta)", text: $game.executavel)
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)
                    TextField("Argumentos", text: $game.argumentos)
                        .autocorrectionDisabled()
                        .textInputAutocapitalization(.never)
                }
                Section(footer: Text("O tipo define o backend usado na execução. "
                    + "PEs Windows exigem a camada Win32 (não integrada).")) {
                    Picker("Tipo", selection: $game.tipo) {
                        Text("Windows PE").tag(GameKind.windowsPE)
                        Text("PXP nativo").tag(GameKind.pxpNative)
                    }
                    TextField("Arquitetura do executável", text: $game.arquiteturaExe)
                }
            }
            .navigationTitle("Editar jogo")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancelar") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Salvar") {
                        onSave(game)
                        dismiss()
                    }
                    .disabled(game.nome.trimmingCharacters(in: .whitespaces).isEmpty)
                }
            }
        }
    }
}
