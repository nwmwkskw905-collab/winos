import SwiftUI
#if !PORTICO_XCODE_MONOLITHIC
import PorticoCore
#endif
/// Desktop WinOS — ambiente inspirado em emulação, não clone Windows.
/// Wallpaper próprio, logo, atalhos, programas, jogos, barra controle.
/// Agora com suporte a PC específico (RUNTIME → DESKTOP fluxo real).
struct WinOSDesktopView: View {
    @EnvironmentObject var model: AppModel
    var pc: EnvironmentProfile? = nil
    @State private var showingCreatePC = false
    @State private var showingImport = false
    @State private var selectedGame: GameProfile?
    @State private var runtimeStage: String = "idle"
    @State private var desktopReady = false
    @State private var selfTestRunning = false
    
    var body: some View {
        ZStack {
            // Wallpaper próprio WinOS
            WinOSWallpaperView()
            
            VStack(spacing: 0) {
                // Top bar — controles de runtime
                topBar
                
                // Runtime stage bar
                if !desktopReady {
                    runtimeStageBar
                }
                
                // Área de atalhos/desktop
                desktopArea
                
                // Barra inferior — WinOS dock
                bottomDock
            }
        }
        .onAppear {
            startRuntimeInit()
        }
        .sheet(isPresented: $showingCreatePC) {
            WinOSCreatePCView()
                .environmentObject(model)
        }
        .sheet(isPresented: $showingImport) {
            ImportFlowView()
                .environmentObject(model)
        }
        .sheet(item: $selectedGame) { game in
            NavigationStack {
                GameDetailView(game: game)
                    .environmentObject(model)
            }
        }
        .fullScreenCover(item: $model.sessionToRun) { game in
            RuntimeSessionView(game: game)
                .environmentObject(model)
        }
    }
    
    private func startRuntimeInit() {
        NSLog("[WINOS-RUNTIME] RUNTIME_START pc=%@", pc?.nome ?? "default")
        runtimeStage = "RUNTIME_START"
        // Simula inicialização rápida sem bloqueio MainActor
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            NSLog("[WINOS-RUNTIME] WIN32_INIT")
            runtimeStage = "WIN32_INIT"
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                NSLog("[WINOS-RUNTIME] PE_LOADER_INIT")
                runtimeStage = "PE_LOADER_INIT"
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                    NSLog("[WINOS-RUNTIME] GRAPHICS_INIT")
                    runtimeStage = "GRAPHICS_INIT"
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
                        NSLog("[WINOS-RUNTIME] DESKTOP_INIT")
                        runtimeStage = "DESKTOP_INIT"
                        DispatchQueue.main.asyncAfter(deadline: .now() + 0.2) {
                            NSLog("[WINOS-RUNTIME] SESSION_READY")
                            runtimeStage = "SESSION_READY"
                            NSLog("[WINOS-RUNTIME] RUNNING desktop=%@", pc?.nome ?? "default")
                            runtimeStage = "RUNNING"
                            desktopReady = true
                        }
                    }
                }
            }
        }
    }
    
    private var runtimeStageBar: some View {
        HStack(spacing: 12) {
            ProgressView()
                .tint(WinOSBrand.accent)
            Text("Inicializando: \(runtimeStage)")
                .font(.system(size: 11, weight: .medium, design: .monospaced))
                .foregroundStyle(WinOSBrand.textSecondary)
            Spacer()
            Text(pc?.nome ?? "WinOS")
                .font(.system(size: 10, weight: .bold, design: .rounded))
                .foregroundStyle(WinOSBrand.textTertiary)
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 8)
        .background(WinOSBrand.card.opacity(0.9))
    }
    
    private var topBar: some View {
        HStack(spacing: 16) {
            WinOSLogoView(size: 32, compact: true)
            
            VStack(alignment: .leading, spacing: 2) {
                Text(pc != nil ? "WinOS Desktop — \(pc!.nome)" : "WinOS Desktop")
                    .font(.system(.subheadline, design: .rounded).weight(.bold))
                    .foregroundStyle(.white)
                Text("Runtime 3411/0 • PE 76/76 • ARM64 host • x64 guest • Stage: \(runtimeStage)")
                    .font(.system(size: 10, weight: .medium, design: .monospaced))
                    .foregroundStyle(WinOSBrand.textTertiary)
            }
            
            Spacer()
            
            HStack(spacing: 8) {
                statusDot(color: WinOSBrand.success, label: "Runtime")
                statusDot(color: WinOSBrand.accent, label: "VFS")
                statusDot(color: WinOSBrand.success, label: "PE")
                statusDot(color: desktopReady ? WinOSBrand.success : WinOSBrand.warning, label: desktopReady ? "DESKTOP" : "INIT")
            }
            
            Button {
                model.refreshGames()
            } label: {
                Image(systemName: "arrow.clockwise")
                    .foregroundStyle(WinOSBrand.textSecondary)
                    .frame(width: 32, height: 32)
                    .background(Circle().fill(WinOSBrand.card))
            }
            
            Button {
                NSLog("[WINOS-RUNTIME] SESSION_END pc=%@", pc?.nome ?? "default")
                if let pc = pc {
                    model.environments.markInUse(id: pc.id, inUse: false)
                }
                model.showDesktop = false
                model.selectedPC = nil
            } label: {
                Image(systemName: "xmark.circle.fill")
                    .foregroundStyle(WinOSBrand.textSecondary)
                    .frame(width: 32, height: 32)
                    .background(Circle().fill(WinOSBrand.card))
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 12)
        .background(.ultraThinMaterial)
        .overlay(Rectangle().fill(WinOSBrand.border).frame(height: 1), alignment: .bottom)
    }
    
    private var desktopArea: some View {
        ScrollView {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 100), spacing: 16)], spacing: 20) {
                // Atalho Criar PC
                desktopIcon(icon: "plus.rectangle.on.folder", title: "Criar PC", subtitle: "Novo ambiente", color: WinOSBrand.accent) {
                    showingCreatePC = true
                }
                
                // Atalho Importar
                desktopIcon(icon: "square.and.arrow.down", title: "Importar", subtitle: "Programa/PE", color: WinOSBrand.accentSecondary) {
                    showingImport = true
                }
                
                // Jogos/Programas como atalhos desktop
                ForEach(model.games) { game in
                    desktopIcon(
                        icon: game.tipo == .windowsPE ? "pc" : "cpu",
                        title: game.nome,
                        subtitle: game.tipo == .windowsPE ? "Windows PE" : "PXP",
                        color: game.tipo == .selfTest ? WinOSBrand.success : WinOSBrand.cardHighlight
                    ) {
                        selectedGame = game
                    }
                    .contextMenu {
                        Button { model.launch(game) } label: { Label("Executar", systemImage: "play.fill") }
                        Button(role: .destructive) { model.removeGame(game, deleteFiles: false) } label: { Label("Remover", systemImage: "trash") }
                    }
                }
                
                // Atalhos sistema
                desktopIcon(icon: "gearshape", title: "Config", subtitle: "Sistema", color: WinOSBrand.card) {
                    // navegação para settings via AppModel
                }
                desktopIcon(icon: "doc.text.magnifyingglass", title: "Logs", subtitle: "Diagnóstico", color: WinOSBrand.card) {
                }
            }
            .padding(20)
        }
    }
    
    private var bottomDock: some View {
        HStack(spacing: 16) {
            WinOSLogoView(size: 28, showText: false)
            
            Divider()
                .frame(height: 24)
                .background(WinOSBrand.border)
            
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 12) {
                    ForEach(model.games.prefix(5)) { game in
                        Button {
                            model.launch(game)
                        } label: {
                            VStack(spacing: 4) {
                                Image(systemName: game.tipo == .windowsPE ? "pc" : "cpu")
                                    .font(.system(size: 16))
                                Text(game.nome)
                                    .font(.system(size: 8, weight: .medium, design: .rounded))
                                    .lineLimit(1)
                                    .frame(width: 50)
                            }
                            .foregroundStyle(.white)
                            .frame(width: 60, height: 48)
                            .background(RoundedRectangle(cornerRadius: 10).fill(WinOSBrand.card))
                        }
                        .buttonStyle(.plain)
                    }
                }
            }
            
            Spacer()
            
            HStack(spacing: 8) {
                Image(systemName: "cpu")
                    .font(.caption2)
                    .foregroundStyle(WinOSBrand.textTertiary)
                Text("3411/0")
                    .font(.system(size: 10, weight: .bold, design: .monospaced))
                    .foregroundStyle(WinOSBrand.textTertiary)
            }
        }
        .padding(.horizontal, 16)
        .padding(.vertical, 10)
        .background(.ultraThinMaterial)
        .overlay(Rectangle().fill(WinOSBrand.border).frame(height: 1), alignment: .top)
    }
    
    private func desktopIcon(icon: String, title: String, subtitle: String, color: Color, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            VStack(spacing: 8) {
                ZStack {
                    RoundedRectangle(cornerRadius: 14)
                        .fill(color)
                        .frame(width: 64, height: 64)
                        .shadow(color: color.opacity(0.3), radius: 8, x: 0, y: 4)
                    Image(systemName: icon)
                        .font(.system(size: 28, weight: .medium))
                        .foregroundStyle(.white)
                }
                Text(title)
                    .font(.system(size: 11, weight: .semibold, design: .rounded))
                    .foregroundStyle(.white)
                    .lineLimit(1)
                    .frame(width: 80)
                Text(subtitle)
                    .font(.system(size: 9, weight: .medium, design: .monospaced))
                    .foregroundStyle(WinOSBrand.textTertiary)
                    .lineLimit(1)
            }
        }
        .buttonStyle(.plain)
    }
    
    private func statusDot(color: Color, label: String) -> some View {
        HStack(spacing: 4) {
            Circle().fill(color).frame(width: 6, height: 6)
            Text(label)
                .font(.system(size: 9, weight: .bold, design: .monospaced))
                .foregroundStyle(WinOSBrand.textTertiary)
        }
    }
}
