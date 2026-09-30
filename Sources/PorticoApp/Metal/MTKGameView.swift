import SwiftUI
import MetalKit
#if !PORTICO_XCODE_MONOLITHIC
import PorticoCore
#endif
/// Embalagem UIKit do MTKView para SwiftUI com diagnóstico físico iPhone 13.
struct MTKGameView: UIViewRepresentable {
    let renderScale: Double
    let onReady: (MetalGameRenderer) -> Void

    func makeCoordinator() -> Coordinator {
        Coordinator()
    }

    func makeUIView(context: Context) -> MTKView {
        NSLog("[WINOS-GFX-INIT] MTKGameView makeUIView renderScale=%.2f", renderScale)
        let view = MTKView()
        if let device = MTLCreateSystemDefaultDevice() {
            NSLog("[WINOS-GFX-METAL] MTLDevice created: %@ name=%@", "\(device)", device.name)
            view.device = device
        } else {
            NSLog("[WINOS-GFX-METAL] ERROR MTLCreateSystemDefaultDevice nil")
        }
        view.colorPixelFormat = .bgra8Unorm
        view.framebufferOnly = false
        view.enableSetNeedsDisplay = false
        view.isPaused = false
        view.preferredFramesPerSecond = 60
        // Retina handling
        view.contentScaleFactor = UIScreen.main.scale
        NSLog("[WINOS-GFX-SURFACE] contentScaleFactor=%.2f bounds=%.0fx%.0f", view.contentScaleFactor, view.bounds.width, view.bounds.height)
        NSLog("[WINOS-GFX-SURFACE] drawableSize=%.0fx%.0f", view.drawableSize.width, view.drawableSize.height)

        let renderer = MetalGameRenderer(view: view)
        renderer.resize(targetSize: CGSize(width: 640, height: 360),
                        renderScale: renderScale)
        view.delegate = renderer
        context.coordinator.renderer = renderer
        NSLog("[WINOS-GFX-INIT] Renderer attached, calling onReady")
        onReady(renderer)
        return view
    }

    func updateUIView(_ uiView: MTKView, context: Context) {
        let scale = uiView.contentScaleFactor
        let bounds = uiView.bounds
        NSLog("[WINOS-GFX-SURFACE] updateUIView scale=%.2f bounds=%.0fx%.0f drawable=%.0fx%.0f", scale, bounds.width, bounds.height, uiView.drawableSize.width, uiView.drawableSize.height)
        context.coordinator.renderer?.resize(
            targetSize: CGSize(width: bounds.width * scale, height: bounds.height * scale),
            renderScale: renderScale)
    }

    final class Coordinator {
        var renderer: MetalGameRenderer?
    }
}
