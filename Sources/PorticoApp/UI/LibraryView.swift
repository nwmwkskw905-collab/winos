import SwiftUI
import PorticoCore

/// Tela principal: Biblioteca + Adicionar jogo + Configurações.
struct LibraryView: View {
    @EnvironmentObject var model: AppModel
    @State private var showingImport = false
    @State private var showingSettings = false
    @State private var gameToDelete: GameProfile?
    @State private var deleteFiles = false

    private let columns = [GridItem(.adaptive(minimum: 150), spacing: 16)]

    var body: some View {
        ScrollView {
            if model.games.isEmpty {
                emptyState
            } else {
                LazyVGrid(columns: columns, spacing: 16) {
                    ForEach(model.games) { game in
                        NavigationLink {
                            GameDetailView(game: game)
                        } label: {
                            GameCard(game: game)
                        }
                        .buttonStyle(.plain)
                        .contextMenu {
                            Button(role: .destructive) {
                                gameToDelete = game
                            } label: {
                                Label("Remover jogo", systemImage: "trash")
                            }
                        }
                    }
                }
                .padding()
            }
        }
        .navigationTitle("Biblioteca")
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Button {
                    showingImport = true
                } label: {
                    Label("Adicionar jogo", systemImage: "plus")
                }
            }
            ToolbarItem(placement: .topBarLeading) {
                NavigationLink {
                    GlobalSettingsView()
                } label: {
                    Label("Configurações", systemImage: "gearshape")
                }
            }
        }
        .sheet(isPresented: $showingImport) {
            ImportFlowView()
        }
        .sheet(isPresented: $showingSettings) {
            GlobalSettingsView()
        }
        .confirmationDialog(
            "Remover \"\(gameToDelete?.nome ?? "")\"?",
            isPresented: Binding(get: { gameToDelete != nil },
                                 set: { if !$0 { gameToDelete = nil } })
        ) {
            Button("Remover (manter arquivos)", role: .destructive) {
                if let g = gameToDelete { model.removeGame(g, deleteFiles: false) }
                gameToDelete = nil
            }
            Button("Remover e apagar arquivos", role: .destructive) {
                if let g = gameToDelete { model.removeGame(g, deleteFiles: true) }
                gameToDelete = nil
            }
            Button("Cancelar", role: .cancel) { gameToDelete = nil }
        }
    }

    private var emptyState: some View {
        VStack(spacing: 12) {
            Image(systemName: "gamecontroller")
                .font(.system(size: 56))
                .foregroundStyle(.secondary)
            Text("Sua biblioteca está vazia")
                .font(.title3.bold())
            Text("Importe jogos compatíveis (ZIP ou pasta) pelo botão +.\n"
                 + "O \"Portico Self-Test\" é criado automaticamente para validar o runtime.")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            Button {
                showingImport = true
            } label: {
                Label("Adicionar jogo", systemImage: "plus.circle.fill")
                    .font(.headline)
            }
            .buttonStyle(.borderedProminent)
        }
        .padding(40)
        .frame(maxWidth: .infinity)
    }
}

/// Card de jogo: capa gerada, nome e tipo.
struct GameCard: View {
    let game: GameProfile

    var body: some View {
        VStack(alignment: .leading, spacing: 8) {
            ZStack {
                RoundedRectangle(cornerRadius: 14)
                    .fill(coverGradient)
                    .frame(height: 110)
                Image(systemName: iconName)
                    .font(.system(size: 36))
                    .foregroundStyle(.white.opacity(0.9))
            }
            Text(game.nome)
                .font(.subheadline.bold())
                .lineLimit(2)
            HStack(spacing: 4) {
                Text(game.tipo == .windowsPE ? "Windows · \(game.arquiteturaExe)"
                     : "PXP nativo")
                    .font(.caption2)
                    .foregroundStyle(.secondary)
                Spacer()
                Text(game.tipo == .windowsPE ? "sem execução" : "pronto")
                    .font(.caption2.bold())
                    .foregroundStyle(game.tipo == .windowsPE ? .orange : .green)
                if game.ultimaExecucao != nil {
                    Image(systemName: "clock")
                        .font(.caption2)
                        .foregroundStyle(.secondary)
                }
            }
        }
        .padding(10)
        .background(.thinMaterial, in: RoundedRectangle(cornerRadius: 18))
    }

    private var iconName: String {
        switch game.tipo {
        case .windowsPE: return "pc"
        case .pxpNative, .selfTest: return "cpu"
        }
    }

    private var coverGradient: LinearGradient {
        let hues: [Double] = [0.58, 0.08, 0.33, 0.75, 0.45, 0.15]
        let h = hues[abs(game.coverSeed) % hues.count]
        return LinearGradient(
            colors: [Color(hue: h, saturation: 0.65, brightness: 0.75),
                     Color(hue: (h + 0.08).truncatingRemainder(dividingBy: 1),
                           saturation: 0.7, brightness: 0.45)],
            startPoint: .topLeading, endPoint: .bottomTrailing)
    }
}
