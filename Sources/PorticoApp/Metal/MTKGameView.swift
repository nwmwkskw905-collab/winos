import SwiftUI
import MetalKit
import PorticoCore

/// Embalagem UIKit do MTKView para SwiftUI.
struct MTKGameView: UIViewRepresentable {
    let renderScale: Double
    let onReady: (MetalGameRenderer) -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    func makeUIView(context: Context) -> MTKView {
        let view = MTKView()
        view.device = MTLCreateSystemDefaultDevice()
        view.colorPixelFormat = .bgra8Unorm
        view.framebufferOnly = false
        view.enableSetNeedsDisplay = false
        view.isPaused = false
        view.preferredFramesPerSecond = 60
        let renderer = MetalGameRenderer(view: view)
        renderer.resize(targetSize: CGSize(width: 640, height: 360),
                        renderScale: renderScale)
        view.delegate = renderer
        context.coordinator.renderer = renderer
        onReady(renderer)
        return view
    }

    func updateUIView(_ uiView: MTKView, context: Context) {
        context.coordinator.renderer?.resize(
            targetSize: CGSize(width: 640, height: 360),
            renderScale: renderScale)
    }

    final class Coordinator {
        var renderer: MetalGameRenderer?
    }
}
