import SwiftUI
import PorticoCore

/// Tela inicial WinOS — identidade própria, inspirada em ambiente de emulação,
/// não clone Windows. Mostra logo WinOS, PCs criados, ações principais.
struct WinOSHomeView: View {
    @EnvironmentObject var model: AppModel
    @State private var showingImport = false
    @State private var showingCreatePC = false
    @State private var showingSettings = false
    @State private var showingCapabilities = false
    @State private var gameToDelete: GameProfile?
    @State private var selectedGame: GameProfile?
    
    private let columns = [GridItem(.adaptive(minimum: 160), spacing: 16)]
    
    var body: some View {
        ZStack {
            WinOSWallpaperView()
            
            ScrollView {
                VStack(alignment: .leading, spacing: 24) {
                    header
                    quickStats
                    pcsSection
                    librarySection
                    systemInfo
                }
                .padding(.horizontal, 20)
                .padding(.vertical, 16)
            }
        }
        .navigationBarHidden(true)
        .sheet(isPresented: $showingImport) {
            ImportFlowView()
                .environmentObject(model)
        }
        .sheet(isPresented: $showingCreatePC) {
            WinOSCreatePCView()
                .environmentObject(model)
        }
        .sheet(isPresented: $showingSettings) {
            NavigationStack {
                GlobalSettingsView()
                    .environmentObject(model)
            }
        }
        .sheet(isPresented: $showingCapabilities) {
            NavigationStack {
                CapabilitiesView()
                    .environmentObject(model)
            }
        }
        .sheet(item: $selectedGame) { game in
            NavigationStack {
                GameDetailView(game: game)
                    .environmentObject(model)
            }
        }
        .confirmationDialog(
            "Remover \"\(gameToDelete?.nome ?? "")\"?",
            isPresented: Binding(get: { gameToDelete != nil }, set: { if !$0 { gameToDelete = nil } })
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
        .fullScreenCover(item: $model.sessionToRun) { game in
            RuntimeSessionView(game: game)
                .environmentObject(model)
        }
    }
    
    private var header: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                WinOSLogoView(size: 44)
                Spacer()
                HStack(spacing: 12) {
                    Button {
                        showingCapabilities = true
                    } label: {
                        Image(systemName: "waveform.path.ecg")
                            .font(.system(size: 18, weight: .semibold))
                            .foregroundStyle(WinOSBrand.textSecondary)
                            .frame(width: 40, height: 40)
                            .background(Circle().fill(WinOSBrand.card))
                    }
                    Button {
                        showingSettings = true
                    } label: {
                        Image(systemName: "gearshape.fill")
                            .font(.system(size: 18, weight: .semibold))
                            .foregroundStyle(WinOSBrand.textSecondary)
                            .frame(width: 40, height: 40)
                            .background(Circle().fill(WinOSBrand.card))
                    }
                }
            }
            
            VStack(alignment: .leading, spacing: 6) {
                Text("Bem-vindo ao")
                    .font(.system(.subheadline, design: .rounded))
                    .foregroundStyle(WinOSBrand.textSecondary)
                Text("WinOS")
                    .font(.system(size: 36, weight: .black, design: .rounded))
                    .foregroundStyle(.white)
                Text("Runtime Windows x64 para iOS • ARM64 host • x64 guest")
                    .font(.system(size: 12, weight: .medium, design: .monospaced))
                    .foregroundStyle(WinOSBrand.textTertiary)
            }
        }
    }
    
    private var quickStats: some View {
        HStack(spacing: 12) {
            statCard(icon: "cpu", value: "\(model.games.count)", label: "Programas")
            statCard(icon: "memorychip", value: "3411", label: "C checks")
            statCard(icon: "checkmark.seal.fill", value: "76/76", label: "PE PASS")
        }
    }
    
    private func statCard(icon: String, value: String, label: String) -> some View {
        WinOSCard {
            VStack(spacing: 8) {
                Image(systemName: icon)
                    .font(.system(size: 18))
                    .foregroundStyle(WinOSBrand.accent)
                Text(value)
                    .font(.system(.headline, design: .rounded).weight(.bold))
                    .foregroundStyle(.white)
                Text(label)
                    .font(.system(size: 10, weight: .bold, design: .monospaced))
                    .foregroundStyle(WinOSBrand.textTertiary)
            }
            .frame(maxWidth: .infinity)
        }
    }
    
    private var pcsSection: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("Meus PCs")
                    .font(.system(.title3, design: .rounded).weight(.bold))
                    .foregroundStyle(.white)
                Spacer()
                WinOSStatusBadge(text: "\(model.environments.environments.count) ambientes", color: WinOSBrand.accent)
            }
            
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 12) {
                    // Botão Criar PC
                    Button {
                        showingCreatePC = true
                    } label: {
                        VStack(spacing: 12) {
                            ZStack {
                                RoundedRectangle(cornerRadius: 14)
                                    .fill(WinOSBrand.gradientPrimary)
                                    .frame(width: 56, height: 56)
                                Image(systemName: "plus")
                                    .font(.system(size: 24, weight: .bold))
                                    .foregroundStyle(.white)
                            }
                            Text("Criar PC")
                                .font(.system(.subheadline, design: .rounded).weight(.semibold))
                                .foregroundStyle(.white)
                            Text("Novo ambiente")
                                .font(.caption2)
                                .foregroundStyle(WinOSBrand.textTertiary)
                        }
                        .frame(width: 140, height: 120)
                        .background(RoundedRectangle(cornerRadius: 18).fill(WinOSBrand.card).overlay(RoundedRectangle(cornerRadius: 18).stroke(WinOSBrand.border, lineWidth: 1)))
                    }
                    .buttonStyle(.plain)
                    
                    ForEach(model.environments.environments) { env in
                        WinOSCard {
                            VStack(alignment: .leading, spacing: 8) {
                                HStack {
                                    Image(systemName: "desktopcomputer")
                                        .foregroundStyle(WinOSBrand.accent)
                                    Spacer()
                                    WinOSStatusBadge(text: env.arquitetura, color: WinOSBrand.success)
                                }
                                Text(env.nome)
                                    .font(.system(.subheadline, design: .rounded).weight(.semibold))
                                    .foregroundStyle(.white)
                                    .lineLimit(1)
                                Text("\(env.ramMB) MB • \(env.backend)")
                                    .font(.caption2)
                                    .foregroundStyle(WinOSBrand.textSecondary)
                            }
                            .frame(width: 140)
                        }
                    }
                }
                .padding(.vertical, 4)
            }
        }
    }
    
    private var librarySection: some View {
        VStack(alignment: .leading, spacing: 12) {
            HStack {
                Text("Biblioteca")
                    .font(.system(.title3, design: .rounded).weight(.bold))
                    .foregroundStyle(.white)
                Spacer()
                Button {
                    showingImport = true
                } label: {
                    Label("Importar", systemImage: "plus.circle.fill")
                        .font(.system(.subheadline, design: .rounded).weight(.semibold))
                        .foregroundStyle(WinOSBrand.accent)
                }
            }
            
            if model.games.isEmpty {
                WinOSCard {
                    VStack(spacing: 12) {
                        Image(systemName: "gamecontroller")
                            .font(.system(size: 40))
                            .foregroundStyle(WinOSBrand.textTertiary)
                        Text("Biblioteca vazia")
                            .font(.headline)
                            .foregroundStyle(.white)
                        Text("Importe programas Windows x64 ou use o Self-Test para validar o runtime")
                            .font(.footnote)
                            .foregroundStyle(WinOSBrand.textSecondary)
                            .multilineTextAlignment(.center)
                        WinOSPrimaryButton(title: "Importar programa", systemImage: "plus", action: { showingImport = true })
                            .padding(.top, 8)
                    }
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 12)
                }
            } else {
                LazyVGrid(columns: columns, spacing: 16) {
                    ForEach(model.games) { game in
                        WinOSGameCard(game: game)
                            .onTapGesture { selectedGame = game }
                            .contextMenu {
                                Button(role: .destructive) { gameToDelete = game } label: { Label("Remover", systemImage: "trash") }
                                Button { model.launch(game) } label: { Label("Executar", systemImage: "play.fill") }
                            }
                    }
                }
            }
        }
    }
    
    private var systemInfo: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Sistema")
                .font(.system(.title3, design: .rounded).weight(.bold))
                .foregroundStyle(.white)
            
            WinOSCard {
                VStack(spacing: 12) {
                    infoRow(icon: "cpu", label: "Runtime", value: "PorticoRuntime C11 + Swift")
                    infoRow(icon: "memorychip", label: "Guest", value: "x64 (interpreter)")
                    infoRow(icon: "iphone", label: "Host", value: "ARM64 iOS 17.0+")
                    infoRow(icon: "cube.box", label: "PE Battery", value: "76/76 PASS com harness")
                    infoRow(icon: "checkmark.shield", label: "C Checks", value: "3411/0")
                    infoRow(icon: "exclamationmark.triangle", label: "Limitações", value: "Metal/Audio/iPhone 13 UNVERIFIED sem hardware")
                }
            }
            
            HStack(spacing: 12) {
                WinOSPrimaryButton(title: "Diagnóstico", systemImage: "waveform.path.ecg", action: { showingCapabilities = true }, isProminent: false)
                WinOSPrimaryButton(title: "Logs", systemImage: "doc.text", action: { showingSettings = true }, isProminent: false)
            }
        }
    }
    
    private func infoRow(icon: String, label: String, value: String) -> some View {
        HStack(spacing: 12) {
            Image(systemName: icon)
                .font(.system(size: 14))
                .foregroundStyle(WinOSBrand.accent)
                .frame(width: 20)
            Text(label)
                .font(.system(.caption, design: .rounded).weight(.semibold))
                .foregroundStyle(WinOSBrand.textSecondary)
            Spacer()
            Text(value)
                .font(.system(.caption2, design: .monospaced))
                .foregroundStyle(.white)
                .multilineTextAlignment(.trailing)
        }
    }
}

struct WinOSGameCard: View {
    let game: GameProfile
    
    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            ZStack {
                RoundedRectangle(cornerRadius: 14)
                    .fill(coverGradient)
                    .frame(height: 96)
                Image(systemName: iconName)
                    .font(.system(size: 32, weight: .medium))
                    .foregroundStyle(.white.opacity(0.9))
            }
            .overlay(alignment: .topTrailing) {
                WinOSStatusBadge(text: badgeText, color: badgeColor)
                    .padding(8)
            }
            
            Text(game.nome)
                .font(.system(.subheadline, design: .rounded).weight(.semibold))
                .foregroundStyle(.white)
                .lineLimit(2)
                .frame(height: 36, alignment: .topLeading)
            
            HStack(spacing: 4) {
                Text(game.tipo == .windowsPE ? "Windows • \(game.arquiteturaExe)" : "PXP nativo")
                    .font(.system(size: 10, weight: .medium, design: .monospaced))
                    .foregroundStyle(WinOSBrand.textTertiary)
                Spacer()
                if game.ultimaExecucao != nil {
                    Image(systemName: "clock.fill")
                        .font(.caption2)
                        .foregroundStyle(WinOSBrand.textTertiary)
                }
            }
        }
        .padding(12)
        .background(
            RoundedRectangle(cornerRadius: 18)
                .fill(WinOSBrand.card)
                .overlay(RoundedRectangle(cornerRadius: 18).stroke(WinOSBrand.border, lineWidth: 1))
        )
    }
    
    private var iconName: String {
        switch game.tipo {
        case .windowsPE: return "pc"
        case .pxpNative, .selfTest: return "cpu"
        }
    }
    
    private var badgeText: String {
        switch game.tipo {
        case .windowsPE: return "PE"
        case .selfTest: return "TEST"
        case .pxpNative: return "PXP"
        }
    }
    
    private var badgeColor: Color {
        switch game.tipo {
        case .windowsPE: return WinOSBrand.accentSecondary
        case .selfTest: return WinOSBrand.success
        case .pxpNative: return WinOSBrand.accent
        }
    }
    
    private var coverGradient: LinearGradient {
        let hues: [Double] = [0.58, 0.08, 0.33, 0.75, 0.45, 0.15]
        let h = hues[abs(game.coverSeed) % hues.count]
        return LinearGradient(
            colors: [Color(hue: h, saturation: 0.65, brightness: 0.75),
                     Color(hue: (h + 0.08).truncatingRemainder(dividingBy: 1), saturation: 0.7, brightness: 0.45)],
            startPoint: .topLeading,
            endPoint: .bottomTrailing
        )
    }
}
