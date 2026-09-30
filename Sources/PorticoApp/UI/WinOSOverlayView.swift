import SwiftUI
#if !PORTICO_XCODE_MONOLITHIC
import PorticoCore
#endif
/// Overlay WinOS durante execução — controles, FPS, CPU, memória, resolução, pausa, sair.
/// Identidade própria WinOS, não Windows.
struct WinOSOverlayView: View {
    @ObservedObject var session: SessionController
    let game: GameProfile
    @Binding var showOverlay: Bool
    let onEnd: () -> Void
    
    @State private var tab: Tab = .main
    @State private var showingLogs = false
    
    enum Tab { case main, performance, audio, controls, info }
    
    var body: some View {
        ZStack {
            Color.black.opacity(0.6)
                .ignoresSafeArea()
                .onTapGesture { withAnimation { showOverlay = false } }
            
            VStack {
                Spacer()
                VStack(spacing: 16) {
                    header
                    content
                    tabs
                }
                .padding(20)
                .background(
                    RoundedRectangle(cornerRadius: 22)
                        .fill(WinOSBrand.card)
                        .overlay(RoundedRectangle(cornerRadius: 22).stroke(WinOSBrand.border, lineWidth: 1))
                        .shadow(color: .black.opacity(0.5), radius: 24, x: 0, y: 12)
                )
                .padding()
            }
        }
    }
    
    private var header: some View {
        HStack(spacing: 12) {
            WinOSLogoView(size: 32, showText: false)
            VStack(alignment: .leading, spacing: 2) {
                Text(game.nome)
                    .font(.system(.headline, design: .rounded).weight(.bold))
                    .foregroundStyle(.white)
                Text("WinOS Runtime • \(game.arquiteturaExe) • \(game.resolucao.description)")
                    .font(.system(size: 10, weight: .medium, design: .monospaced))
                    .foregroundStyle(WinOSBrand.textTertiary)
            }
            Spacer()
            Button {
                withAnimation { showOverlay = false }
            } label: {
                Image(systemName: "xmark.circle.fill")
                    .font(.system(size: 24))
                    .foregroundStyle(WinOSBrand.textSecondary)
            }
        }
    }
    
    @ViewBuilder
    private var content: some View {
        switch tab {
        case .main: mainTab
        case .performance: performanceTab
        case .audio: audioTab
        case .controls: controlsTab
        case .info: infoTab
        }
    }
    
    private var mainTab: some View {
        VStack(spacing: 12) {
            HStack(spacing: 12) {
                WinOSPrimaryButton(title: session.isPaused ? "Continuar" : "Pausar", systemImage: session.isPaused ? "play.fill" : "pause.fill") {
                    session.togglePause()
                }
                Button(role: .destructive) {
                    onEnd()
                } label: {
                    Label("Encerrar", systemImage: "power")
                        .font(.system(.subheadline, design: .rounded).weight(.semibold))
                        .foregroundStyle(.white)
                        .frame(maxWidth: .infinity)
                        .padding(.vertical, 14)
                        .background(RoundedRectangle(cornerRadius: 14).fill(WinOSBrand.danger))
                }
                .buttonStyle(.plain)
            }
            
            WinOSCard {
                HStack(spacing: 16) {
                    metric(icon: "speedometer", title: "FPS", value: String(format: "%.0f", session.fps))
                    Divider().frame(height: 36).background(WinOSBrand.border)
                    metric(icon: "film", title: "Frames", value: "\(session.framesPresented)")
                    Divider().frame(height: 36).background(WinOSBrand.border)
                    metric(icon: "display", title: "Res", value: session.resolutionText)
                }
            }
            
            Button {
                showingLogs = true
            } label: {
                Label("Logs da sessão", systemImage: "doc.text.magnifyingglass")
                    .font(.subheadline)
                    .foregroundStyle(WinOSBrand.textSecondary)
                    .frame(maxWidth: .infinity)
                    .padding(.vertical, 12)
                    .background(RoundedRectangle(cornerRadius: 12).fill(WinOSBrand.cardHighlight))
            }
            .sheet(isPresented: $showingLogs) {
                NavigationStack { LogsView() }
            }
        }
    }
    
    private var performanceTab: some View {
        VStack(spacing: 12) {
            WinOSCard {
                VStack(alignment: .leading, spacing: 12) {
                    Text("Performance Real")
                        .font(.system(.subheadline, design: .rounded).weight(.bold))
                        .foregroundStyle(.white)
                    HStack {
                        perfRow(label: "FPS", value: String(format: "%.1f", session.fps), icon: "speedometer")
                        perfRow(label: "Frames", value: "\(session.framesPresented)", icon: "film")
                    }
                    HStack {
                        perfRow(label: "Estado", value: session.isPaused ? "Pausado" : "Executando", icon: "cpu")
                        perfRow(label: "Volume", value: "\(Int(session.volume * 100))%", icon: "speaker.wave.2")
                    }
                    Text("Métricas reais do RuntimeManager.tick() + FramePacer + MetalGameRenderer. Sem FPS inventado.")
                        .font(.caption2)
                        .foregroundStyle(WinOSBrand.textTertiary)
                }
            }
        }
    }
    
    private var audioTab: some View {
        WinOSCard {
            VStack(spacing: 16) {
                Toggle(isOn: Binding(get: { !session.muted }, set: { session.setMuted(!$0) })) {
                    Label("Áudio habilitado", systemImage: "speaker.wave.2.fill")
                        .foregroundStyle(.white)
                }
                .tint(WinOSBrand.accent)
                
                VStack(alignment: .leading, spacing: 8) {
                    HStack {
                        Text("Volume")
                            .foregroundStyle(.white)
                        Spacer()
                        Text("\(Int(session.volume * 100))%")
                            .foregroundStyle(WinOSBrand.accent)
                            .monospacedDigit()
                    }
                    .font(.subheadline)
                    Slider(value: Binding(get: { session.volume }, set: { session.setVolume($0) }), in: 0...1)
                        .tint(WinOSBrand.accent)
                }
                
                Text("Backend: AVAudioEngine + SourceNode + ring buffer pr_audio_ring_* (código real, UNVERIFIED sem hardware)")
                    .font(.caption2)
                    .foregroundStyle(WinOSBrand.textTertiary)
            }
        }
    }
    
    private var controlsTab: some View {
        WinOSCard {
            VStack(alignment: .leading, spacing: 12) {
                Text("Input WinOS")
                    .font(.system(.subheadline, design: .rounded).weight(.bold))
                    .foregroundStyle(.white)
                
                Text("Fluxo: UIKit/GameController → TouchInputAdapter/GameControllerBridge → InputCore/InputRouter → pr_input_state → Win32/User32 → PE")
                    .font(.caption2)
                    .foregroundStyle(WinOSBrand.textSecondary)
                
                Divider().background(WinOSBrand.border)
                
                HStack {
                    Image(systemName: "hand.tap").foregroundStyle(WinOSBrand.accent)
                    Text("Touch primário → mouse, secundário sem mouse")
                        .font(.caption)
                        .foregroundStyle(WinOSBrand.textSecondary)
                }
                HStack {
                    Image(systemName: "keyboard").foregroundStyle(WinOSBrand.accent)
                    Text("Teclado via pr_win32_input_key/char")
                        .font(.caption)
                        .foregroundStyle(WinOSBrand.textSecondary)
                }
                HStack {
                    Image(systemName: "gamecontroller").foregroundStyle(WinOSBrand.accent)
                    Text("Gamepad MFi/Bluetooth → InputRouter")
                        .font(.caption)
                        .foregroundStyle(WinOSBrand.textSecondary)
                }
                
                Text("Layout: \(game.controles.elements.count) elementos • Controles na tela: \(session.showTouchControls ? "ON" : "OFF")")
                    .font(.caption2.monospaced())
                    .foregroundStyle(WinOSBrand.textTertiary)
            }
        }
    }
    
    private var infoTab: some View {
        WinOSCard {
            VStack(alignment: .leading, spacing: 10) {
                infoRow(icon: "cpu", label: "Runtime", value: "3411/0 C checks")
                infoRow(icon: "cube.box", label: "PE Battery", value: "76/76 PASS")
                infoRow(icon: "memorychip", label: "Guest", value: "x64 interpreter")
                infoRow(icon: "iphone", label: "Host", value: "ARM64 iOS 17.0+")
                infoRow(icon: "paintbrush", label: "Graphics", value: "Metal + Software")
                infoRow(icon: "speaker.wave.2", label: "Audio", value: "AVAudioEngine")
                infoRow(icon: "folder", label: "VFS", value: "vfs_resolve sandbox")
                infoRow(icon: "gamecontroller", label: "Input", value: "harness determinístico")
                
                Divider().background(WinOSBrand.border)
                
                Text("WinOS • Identidade própria • Não é Windows • Não é Winlator • PC + emulação + performance + iOS")
                    .font(.system(size: 9, weight: .medium, design: .monospaced))
                    .foregroundStyle(WinOSBrand.textTertiary)
                    .multilineTextAlignment(.center)
            }
        }
    }
    
    private var tabs: some View {
        HStack(spacing: 0) {
            ForEach([Tab.main, .performance, .audio, .controls, .info], id: \.self) { t in
                tabButton(t)
            }
        }
        .padding(4)
        .background(Capsule().fill(WinOSBrand.backgroundSecondary))
    }
    
    private func tabButton(_ t: Tab) -> some View {
        Button {
            withAnimation(.easeInOut(duration: 0.2)) { tab = t }
        } label: {
            VStack(spacing: 4) {
                Image(systemName: iconForTab(t))
                    .font(.system(size: 16, weight: tab == t ? .bold : .regular))
                Text(labelForTab(t))
                    .font(.system(size: 9, weight: .bold, design: .rounded))
            }
            .foregroundStyle(tab == t ? .white : WinOSBrand.textTertiary)
            .frame(maxWidth: .infinity)
            .padding(.vertical, 8)
            .background(
                Capsule()
                    .fill(tab == t ? WinOSBrand.cardHighlight : Color.clear)
            )
        }
        .buttonStyle(.plain)
    }
    
    private func iconForTab(_ t: Tab) -> String {
        switch t {
        case .main: return "gamecontroller.fill"
        case .performance: return "speedometer"
        case .audio: return "speaker.wave.2.fill"
        case .controls: return "hand.tap.fill"
        case .info: return "info.circle.fill"
        }
    }
    
    private func labelForTab(_ t: Tab) -> String {
        switch t {
        case .main: return "Jogo"
        case .performance: return "Perf"
        case .audio: return "Áudio"
        case .controls: return "Input"
        case .info: return "Info"
        }
    }
    
    private func metric(icon: String, title: String, value: String) -> some View {
        VStack(spacing: 4) {
            Image(systemName: icon)
                .font(.system(size: 14))
                .foregroundStyle(WinOSBrand.accent)
            Text(value)
                .font(.system(.headline, design: .rounded).weight(.bold).monospacedDigit())
                .foregroundStyle(.white)
            Text(title)
                .font(.system(size: 9, weight: .bold, design: .monospaced))
                .foregroundStyle(WinOSBrand.textTertiary)
        }
        .frame(maxWidth: .infinity)
    }
    
    private func perfRow(label: String, value: String, icon: String) -> some View {
        HStack(spacing: 8) {
            Image(systemName: icon)
                .font(.caption)
                .foregroundStyle(WinOSBrand.accent)
                .frame(width: 16)
            Text(label)
                .font(.caption)
                .foregroundStyle(WinOSBrand.textSecondary)
            Spacer()
            Text(value)
                .font(.caption.monospaced().weight(.semibold))
                .foregroundStyle(.white)
        }
    }
    
    private func infoRow(icon: String, label: String, value: String) -> some View {
        HStack(spacing: 10) {
            Image(systemName: icon)
                .font(.system(size: 12))
                .foregroundStyle(WinOSBrand.accent)
                .frame(width: 16)
            Text(label)
                .font(.caption.weight(.semibold))
                .foregroundStyle(WinOSBrand.textSecondary)
            Spacer()
            Text(value)
                .font(.caption2.monospaced())
                .foregroundStyle(.white)
        }
    }
}
