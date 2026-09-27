import SwiftUI
import PorticoCore

/// Controles virtuais touchscreen (posições/tamanhos do layout do jogo).
struct VirtualControlsView: View {
    let layout: ControlProfile
    let router: InputRouter
    var opacityScale: Double = 1.0

    var body: some View {
        GeometryReader { geo in
            ZStack(alignment: .topLeading) {
                ForEach(layout.elements) { el in
                    VirtualControlElement(element: el,
                                          router: router,
                                          canvasSize: geo.size,
                                          opacityScale: opacityScale)
                }
            }
        }
    }
}

struct VirtualControlElement: View {
    let element: ControlElement
    let router: InputRouter
    let canvasSize: CGSize
    var opacityScale: Double

    @State private var isPressed = false

    var body: some View {
        let w = element.frame.w * canvasSize.width
        let h = element.frame.h * canvasSize.height
        let x = element.frame.x * canvasSize.width
        let y = element.frame.y * canvasSize.height

        ZStack {
            switch element.kind {
            case .analogStick:
                AnalogStickView(router: router, element: element,
                                size: CGSize(width: w, height: h))
            default:
                ButtonShape(label: element.label,
                            pressed: isPressed,
                            opacity: element.opacity * opacityScale)
                    .gesture(pressGesture)
            }
        }
        .frame(width: w, height: h)
        .position(x: x + w / 2, y: y + h / 2)
    }

    private var pressGesture: some Gesture {
        DragGesture(minimumDistance: 0)
            .onChanged { _ in
                if !isPressed {
                    isPressed = true
                    router.setTouchAction(element.action, pressed: true)
                    if element.kind == .trigger {
                        router.setTrigger(element.action, value: 1)
                    }
                }
            }
            .onEnded { _ in
                isPressed = false
                router.setTouchAction(element.action, pressed: false)
                if element.kind == .trigger {
                    router.setTrigger(element.action, value: 0)
                }
            }
    }
}

struct ButtonShape: View {
    let label: String
    let pressed: Bool
    let opacity: Double

    var body: some View {
        ZStack {
            Circle()
                .fill(Color.white.opacity(pressed ? 0.45 : 0.18))
            Circle()
                .stroke(Color.white.opacity(opacity), lineWidth: 2)
            Text(label)
                .font(.headline)
                .foregroundStyle(.white.opacity(opacity))
        }
    }
}

/// Analógico virtual com arraste e retorno ao centro.
struct AnalogStickView: View {
    let router: InputRouter
    let element: ControlElement
    let size: CGSize

    @State private var knob = CGSize.zero

    var body: some View {
        ZStack {
            Circle()
                .fill(Color.white.opacity(0.12))
            Circle()
                .stroke(Color.white.opacity(element.opacity), lineWidth: 2)
            Circle()
                .fill(Color.white.opacity(element.opacity * 0.5))
                .frame(width: size.width * 0.4, height: size.height * 0.4)
                .offset(knob)
        }
        .frame(width: size.width, height: size.height)
        .gesture(
            DragGesture(minimumDistance: 0)
                .onChanged { v in
                    let radius = Double(min(size.width, size.height)) * 0.35
                    var dx = Double(v.translation.width)
                    var dy = Double(v.translation.height)
                    let mag = (dx * dx + dy * dy).squareRoot()
                    if mag > radius {
                        dx = dx / mag * radius
                        dy = dy / mag * radius
                    }
                    knob = CGSize(width: dx, height: dy)
                    router.setMoveStick(x: Float(dx / radius),
                                        y: Float(dy / radius))
                }
                .onEnded { _ in
                    knob = .zero
                    router.setMoveStick(x: 0, y: 0)
                }
        )
    }
}
