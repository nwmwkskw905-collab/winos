import SwiftUI
import PorticoCore

/// Gerenciamento de ambientes/prefixos independentes.
struct EnvironmentView: View {
    @EnvironmentObject var model: AppModel
    @State private var newName = ""

    var body: some View {
        List {
            Section {
                ForEach(model.environments.environments) { env in
                    VStack(alignment: .leading, spacing: 4) {
                        HStack {
                            Text(env.nome).font(.body.bold())
                            Spacer()
                            Text(stateLabel(env.state))
                                .font(.caption2)
                                .padding(.horizontal, 6)
                                .padding(.vertical, 2)
                                .background(stateColor(env.state).opacity(0.2),
                                            in: Capsule())
                        }
                        Text("runtime: \(env.runtimeVersion)")
                            .font(.caption.monospaced())
                            .foregroundStyle(.secondary)
                        Text("\(model.environments.sizeBytes(id: env.id) / 1024) KiB · "
                             + "criado em \(env.dataCriacao.formatted(date: .abbreviated, time: .omitted))")
                            .font(.caption2)
                            .foregroundStyle(.secondary)
                    }
                    .swipeActions {
                        if env.id != EnvironmentRef.defaultEnv.id {
                            Button(role: .destructive) {
                                try? model.environments.destroy(id: env.id)
                            } label: {
                                Label("Excluir", systemImage: "trash")
                            }
                        }
                    }
                }
            } footer: {
                Text("Cada ambiente é um prefixo isolado (variáveis, arquivos, "
                     + "estado) preparado para futuras camadas de compatibilidade.")
            }

            Section("Novo ambiente") {
                TextField("Nome", text: $newName)
                Button("Criar") {
                    let n = newName.trimmingCharacters(in: .whitespaces)
                    guard !n.isEmpty else { return }
                    _ = try? model.environments.create(name: n)
                    newName = ""
                }
            }
        }
        .navigationTitle("Ambientes")
    }

    private func stateLabel(_ s: EnvironmentState) -> String {
        switch s {
        case .empty: return "vazio"
        case .ready: return "pronto"
        case .inUse: return "em uso"
        case .broken: return "danificado"
        }
    }

    private func stateColor(_ s: EnvironmentState) -> Color {
        switch s {
        case .empty: return .gray
        case .ready: return .green
        case .inUse: return .blue
        case .broken: return .red
        }
    }
}
