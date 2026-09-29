import SwiftUI

/// Identidade visual própria do WinOS — não é Windows, não é Winlator.
/// Conceito: PC + emulação + performance + iOS, moderno, limpo, tecnológico.
enum WinOSBrand {
    // Cores principais — paleta tecnológica escura com acento ciano/azul
    static let background = Color(red: 0.06, green: 0.07, blue: 0.10) // #0F1219
    static let backgroundSecondary = Color(red: 0.10, green: 0.12, blue: 0.16)
    static let card = Color(red: 0.14, green: 0.16, blue: 0.22)
    static let cardHighlight = Color(red: 0.18, green: 0.20, blue: 0.28)
    static let accent = Color(red: 0.22, green: 0.78, blue: 0.95) // ciano tecnológico
    static let accentSecondary = Color(red: 0.40, green: 0.55, blue: 1.0) // azul iOS
    static let accentGlow = Color(red: 0.22, green: 0.78, blue: 0.95).opacity(0.35)
    static let success = Color(red: 0.30, green: 0.85, blue: 0.55)
    static let warning = Color(red: 1.0, green: 0.75, blue: 0.20)
    static let danger = Color(red: 1.0, green: 0.35, blue: 0.35)
    static let textPrimary = Color.white
    static let textSecondary = Color.white.opacity(0.65)
    static let textTertiary = Color.white.opacity(0.40)
    static let border = Color.white.opacity(0.08)
    
    static let gradientPrimary = LinearGradient(
        colors: [accent, accentSecondary],
        startPoint: .topLeading,
        endPoint: .bottomTrailing
    )
    static let gradientCard = LinearGradient(
        colors: [card, backgroundSecondary],
        startPoint: .topLeading,
        endPoint: .bottomTrailing
    )
    static let gradientBackground = LinearGradient(
        colors: [background, Color(red: 0.08, green: 0.10, blue: 0.18)],
        startPoint: .top,
        endPoint: .bottom
    )
}

/// Logo WinOS — identidade própria, não usa logo Windows nem Winlator.
/// Geometria: W estilizado + chip + orbita.
struct WinOSLogoView: View {
    var size: CGFloat = 56
    var showText: Bool = true
    var compact: Bool = false
    
    var body: some View {
        HStack(spacing: compact ? 8 : 12) {
            ZStack {
                // Glow
                Circle()
                    .fill(WinOSBrand.accentGlow)
                    .frame(width: size * 1.4, height: size * 1.4)
                    .blur(radius: size * 0.25)
                
                // Fundo do ícone
                RoundedRectangle(cornerRadius: size * 0.28)
                    .fill(WinOSBrand.gradientPrimary)
                    .frame(width: size, height: size)
                    .shadow(color: WinOSBrand.accent.opacity(0.5), radius: size * 0.25, x: 0, y: size * 0.15)
                
                // Símbolo W + OS
                ZStack {
                    // W estilizado
                    Text("W")
                        .font(.system(size: size * 0.52, weight: .black, design: .rounded))
                        .foregroundStyle(.white)
                        .offset(x: -size * 0.02)
                    
                    // Chip dots decorativos
                    HStack(spacing: size * 0.08) {
                        Circle().fill(.white.opacity(0.9)).frame(width: size * 0.08, height: size * 0.08)
                        Circle().fill(.white.opacity(0.6)).frame(width: size * 0.06, height: size * 0.06)
                    }
                    .offset(x: size * 0.22, y: size * 0.18)
                }
                
                // Orbita
                Circle()
                    .stroke(.white.opacity(0.25), lineWidth: 1)
                    .frame(width: size * 1.15, height: size * 1.15)
            }
            .frame(width: size * 1.4, height: size * 1.4)
            
            if showText {
                VStack(alignment: .leading, spacing: compact ? 0 : 2) {
                    HStack(spacing: 0) {
                        Text("WIN")
                            .font(.system(size: compact ? 18 : 22, weight: .black, design: .rounded))
                            .foregroundStyle(.white)
                        Text("OS")
                            .font(.system(size: compact ? 18 : 22, weight: .light, design: .rounded))
                            .foregroundStyle(WinOSBrand.accent)
                    }
                    if !compact {
                        Text("PC EMULATION • iOS")
                            .font(.system(size: 8, weight: .bold, design: .monospaced))
                            .tracking(1.2)
                            .foregroundStyle(WinOSBrand.textTertiary)
                    }
                }
            }
        }
        .accessibilityLabel("WinOS")
    }
}

/// Wallpaper próprio WinOS — não é Windows, identidade tecnológica
struct WinOSWallpaperView: View {
    var body: some View {
        ZStack {
            WinOSBrand.gradientBackground
                .ignoresSafeArea()
            
            // Grid tecnológico sutil
            GeometryReader { geo in
                let cols = 12
                let rows = 20
                Path { path in
                    let w = geo.size.width
                    let h = geo.size.height
                    for i in 0...cols {
                        let x = w * CGFloat(i) / CGFloat(cols)
                        path.move(to: CGPoint(x: x, y: 0))
                        path.addLine(to: CGPoint(x: x, y: h))
                    }
                    for j in 0...rows {
                        let y = h * CGFloat(j) / CGFloat(rows)
                        path.move(to: CGPoint(x: 0, y: y))
                        path.addLine(to: CGPoint(x: w, y: y))
                    }
                }
                .stroke(Color.white.opacity(0.03), lineWidth: 0.5)
                
                // Orbs de luz
                Circle()
                    .fill(WinOSBrand.accent.opacity(0.12))
                    .frame(width: geo.size.width * 0.8, height: geo.size.width * 0.8)
                    .blur(radius: 60)
                    .offset(x: geo.size.width * 0.1, y: -geo.size.height * 0.15)
                
                Circle()
                    .fill(WinOSBrand.accentSecondary.opacity(0.10))
                    .frame(width: geo.size.width * 0.6, height: geo.size.width * 0.6)
                    .blur(radius: 50)
                    .offset(x: geo.size.width * 0.5, y: geo.size.height * 0.5)
            }
            
            // Logo watermark sutil
            VStack {
                Spacer()
                HStack {
                    Spacer()
                    WinOSLogoView(size: 120, showText: false)
                        .opacity(0.04)
                        .offset(x: 20, y: 20)
                }
            }
        }
    }
}

/// Card padrão WinOS
struct WinOSCard<Content: View>: View {
    let content: Content
    var highlight: Bool = false
    
    init(highlight: Bool = false, @ViewBuilder content: () -> Content) {
        self.highlight = highlight
        self.content = content()
    }
    
    var body: some View {
        content
            .padding(16)
            .background(
                RoundedRectangle(cornerRadius: 18)
                    .fill(highlight ? WinOSBrand.cardHighlight : WinOSBrand.card)
                    .overlay(
                        RoundedRectangle(cornerRadius: 18)
                            .stroke(WinOSBrand.border, lineWidth: 1)
                    )
                    .shadow(color: .black.opacity(0.3), radius: 12, x: 0, y: 6)
            )
    }
}

/// Botão primário WinOS
struct WinOSPrimaryButton: View {
    let title: String
    let systemImage: String
    let action: () -> Void
    var isProminent: Bool = true
    
    var body: some View {
        Button(action: action) {
            Label(title, systemImage: systemImage)
                .font(.system(.subheadline, design: .rounded).weight(.semibold))
                .foregroundStyle(.white)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 14)
                .background(
                    RoundedRectangle(cornerRadius: 14)
                        .fill(isProminent ? WinOSBrand.gradientPrimary : LinearGradient(colors: [WinOSBrand.cardHighlight], startPoint: .top, endPoint: .bottom))
                        .shadow(color: isProminent ? WinOSBrand.accent.opacity(0.4) : .clear, radius: 12, x: 0, y: 4)
                )
        }
        .buttonStyle(.plain)
    }
}

/// Botão secundário WinOS
struct WinOSSecondaryButton: View {
    let title: String
    let systemImage: String
    let action: () -> Void
    
    var body: some View {
        Button(action: action) {
            Label(title, systemImage: systemImage)
                .font(.system(.subheadline, design: .rounded).weight(.medium))
                .foregroundStyle(WinOSBrand.textSecondary)
                .frame(maxWidth: .infinity)
                .padding(.vertical, 12)
                .background(
                    RoundedRectangle(cornerRadius: 12)
                        .fill(WinOSBrand.card)
                        .overlay(RoundedRectangle(cornerRadius: 12).stroke(WinOSBrand.border, lineWidth: 1))
                )
        }
        .buttonStyle(.plain)
    }
}

/// Badge de status WinOS
struct WinOSStatusBadge: View {
    let text: String
    let color: Color
    
    var body: some View {
        Text(text.uppercased())
            .font(.system(size: 9, weight: .bold, design: .monospaced))
            .tracking(0.8)
            .foregroundStyle(color)
            .padding(.horizontal, 8)
            .padding(.vertical, 4)
            .background(
                Capsule()
                    .fill(color.opacity(0.15))
                    .overlay(Capsule().stroke(color.opacity(0.3), lineWidth: 1))
            )
    }
}
