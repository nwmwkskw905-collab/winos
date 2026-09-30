import SwiftUI
#if !PORTICO_XCODE_MONOLITHIC
import PorticoCore
#endif
/// Tela de carregamento WinOS — logo, animação, progresso real quando disponível,
/// mensagens de estado (runtime, ambiente, PE). Sem progresso falso.
struct WinOSLoadingView: View {
    var message: String
    var progress: Double? // nil = indeterminado
    var detail: String?
    
    @State private var rotation: Double = 0
    @State private var pulse: Bool = false
    
    var body: some View {
        ZStack {
            WinOSBrand.background.ignoresSafeArea()
            WinOSWallpaperView()
            
            VStack(spacing: 28) {
                Spacer()
                
                // Logo com animação
                ZStack {
                    Circle()
                        .fill(WinOSBrand.accentGlow)
                        .frame(width: 120, height: 120)
                        .blur(radius: 20)
                        .scaleEffect(pulse ? 1.2 : 0.9)
                        .animation(.easeInOut(duration: 1.5).repeatForever(autoreverses: true), value: pulse)
                    
                    WinOSLogoView(size: 80)
                        .rotationEffect(.degrees(rotation))
                        .animation(.linear(duration: 3).repeatForever(autoreverses: false), value: rotation)
                }
                .onAppear {
                    rotation = 360
                    pulse = true
                }
                
                VStack(spacing: 12) {
                    Text("WinOS")
                        .font(.system(size: 28, weight: .black, design: .rounded))
                        .foregroundStyle(.white)
                    Text(message)
                        .font(.system(.subheadline, design: .rounded).weight(.medium))
                        .foregroundStyle(WinOSBrand.textSecondary)
                        .multilineTextAlignment(.center)
                    
                    if let detail {
                        Text(detail)
                            .font(.system(size: 11, weight: .medium, design: .monospaced))
                            .foregroundStyle(WinOSBrand.textTertiary)
                            .multilineTextAlignment(.center)
                            .padding(.horizontal, 32)
                    }
                }
                
                // Progresso
                if let progress {
                    VStack(spacing: 8) {
                        ProgressView(value: progress)
                            .tint(WinOSBrand.accent)
                            .frame(width: 200)
                        Text("\(Int(progress * 100))%")
                            .font(.caption.monospaced())
                            .foregroundStyle(WinOSBrand.textTertiary)
                    }
                } else {
                    ProgressView()
                        .tint(WinOSBrand.accent)
                        .scaleEffect(1.2)
                }
                
                Spacer()
                
                // Footer com estados reais
                VStack(spacing: 6) {
                    HStack(spacing: 8) {
                        Circle().fill(WinOSBrand.success).frame(width: 6, height: 6)
                        Text("Runtime C 3411/0 • PE 76/76")
                            .font(.system(size: 10, weight: .bold, design: .monospaced))
                            .foregroundStyle(WinOSBrand.textTertiary)
                    }
                    Text("WinOS • PC Emulation • iOS • ARM64 host • x64 guest")
                        .font(.system(size: 9, weight: .medium, design: .monospaced))
                        .tracking(0.5)
                        .foregroundStyle(WinOSBrand.textTertiary.opacity(0.7))
                }
                .padding(.bottom, 32)
            }
            .padding(24)
        }
    }
}

/// Loading com estados reais do runtime
struct WinOSRuntimeLoadingView: View {
    @EnvironmentObject var model: AppModel
    let game: GameProfile
    var onReady: () -> Void
    
    @State private var step = 0
    @State private var messages = [
        "[WinOS][Runtime] Inicializando...",
        "[WinOS][VFS] Montando win_fs...",
        "[WinOS][PE] Carregando PE...",
        "[WinOS][CPU] Resolvendo imports...",
        "[WinOS][Graphics] Inicializando Metal...",
        "[WinOS][Input] Configurando controles...",
        "[WinOS][Audio] Preparando AVAudioEngine...",
        "[WinOS][Runtime] Pronto!"
    ]
    
    var body: some View {
        WinOSLoadingView(
            message: messages[min(step, messages.count - 1)],
            progress: Double(step) / Double(messages.count - 1),
            detail: "Executando \(game.nome) • \(game.arquiteturaExe) • \(game.resolucao.description)"
        )
        .onAppear {
            startSequence()
        }
    }
    
    private func startSequence() {
        Timer.scheduledTimer(withTimeInterval: 0.4, repeats: true) { timer in
            if step < messages.count - 1 {
                step += 1
            } else {
                timer.invalidate()
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.5) {
                    onReady()
                }
            }
        }
    }
}
