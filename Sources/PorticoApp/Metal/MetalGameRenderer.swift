import Foundation
import Metal
import MetalKit
import PorticoCore

/// Backend gráfico Metal: executa o stream de comandos do runtime em um
/// framebuffer interno (resolução do jogo × qualidade) e apresenta escalado.
/// Sem alocações por frame: buffers e texturas reutilizados.
public final class MetalGameRenderer: NSObject, GraphicsBackend, MTKViewDelegate {
    public let backendID = "metal"
    public var isAvailable: Bool { device != nil }

    private let device: MTLDevice?
    private var queue: MTLCommandQueue?
    private var gamePipeline: MTLRenderPipelineState?
    private var blitPipeline: MTLRenderPipelineState?
    private var linearSampler: MTLSamplerState?
    private var nearestSampler: MTLSamplerState?

    // Recursos reutilizados (sem churn por frame)
    private var vertexBuffer: MTLBuffer?
    private var uniformBuffer: MTLBuffer?
    private var internalTexture: MTLTexture?
    private var surfaceTexture: MTLTexture?          // ponte GfxFrame.surface (GDI)
    private let surfaceUpload = MetalFrameUpload()   // estágio BGRA8 reutilizado
    private let maxVertices = 65_536

    private var renderSize: CGSize = CGSize(width: 640, height: 360)
    private var viewSize: CGSize = CGSize(width: 1280, height: 720)
    private var currentClearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
    private var pendingFrame = GfxFrame()
    private var nearestFilter = false
    private weak var view: MTKView?

    public init(view: MTKView) {
        self.view = view
        self.device = view.device ?? MTLCreateSystemDefaultDevice()
        super.init()
        view.device = self.device
        setupPipelines()
    }

    private func setupPipelines() {
        NSLog("[WINOS-GFX-INIT] Iniciando setup Metal pipelines")
        guard let device else {
            NSLog("[WINOS-GFX-INIT] ERROR MTLDevice nil")
            return
        }
        NSLog("[WINOS-GFX-INIT] MTLDevice: %@ name=%@", "\(device)", device.name)
        NSLog("[WINOS-GFX-METAL] Device disponível, criando command queue")
        queue = device.makeCommandQueue()
        if queue == nil {
            NSLog("[WINOS-GFX-METAL] ERROR command queue nil")
        } else {
            NSLog("[WINOS-GFX-METAL] Command queue OK: %@", "\(queue!)")
        }
        vertexBuffer = device.makeBuffer(length: maxVertices * MemoryLayout<GfxVertex>.stride * 3,
                                         options: .storageModeShared)
        uniformBuffer = device.makeBuffer(length: 64, options: .storageModeShared)
        NSLog("[WINOS-GFX-INIT] Vertex buffer: %@ uniform: %@", vertexBuffer != nil ? "OK" : "FAIL", uniformBuffer != nil ? "OK" : "FAIL")

        guard let library = device.makeDefaultLibrary() else {
            NSLog("[WINOS-GFX-INIT] ERROR default library nil")
            return
        }
        NSLog("[WINOS-GFX-INIT] Default library OK")
        let desc = MTLRenderPipelineDescriptor()
        desc.colorAttachments[0].pixelFormat = .bgra8Unorm

        let vdesc = MTLVertexDescriptor()
        vdesc.attributes[0].format = .float2
        vdesc.attributes[0].offset = 0
        vdesc.attributes[0].bufferIndex = 0
        vdesc.attributes[1].format = .float3
        vdesc.attributes[1].offset = 8
        vdesc.attributes[1].bufferIndex = 0
        vdesc.layouts[0].stride = MemoryLayout<GfxVertex>.stride

        do {
            let vf = library.makeFunction(name: "game_vertex")
            let ff = library.makeFunction(name: "game_fragment")
            desc.vertexFunction = vf
            desc.fragmentFunction = ff
            desc.vertexDescriptor = vdesc
            gamePipeline = try device.makeRenderPipelineState(descriptor: desc)
            NSLog("[WINOS-GFX-INIT] Game pipeline OK")

            let bdesc = MTLRenderPipelineDescriptor()
            bdesc.colorAttachments[0].pixelFormat = .bgra8Unorm
            bdesc.vertexFunction = library.makeFunction(name: "blit_vertex")
            bdesc.fragmentFunction = library.makeFunction(name: "blit_fragment")
            blitPipeline = try device.makeRenderPipelineState(descriptor: bdesc)
            NSLog("[WINOS-GFX-INIT] Blit pipeline OK")
        } catch {
            NSLog("[WINOS-GFX-INIT] ERROR pipeline: %@", "\(error)")
            NSLog("Portico/Metal: falha de pipeline: %@", "\(error)")
        }

        let sdesc = MTLSamplerDescriptor()
        sdesc.minFilter = .linear
        sdesc.magFilter = .linear
        linearSampler = device.makeSamplerState(descriptor: sdesc)
        sdesc.minFilter = .nearest
        sdesc.magFilter = .nearest
        nearestSampler = device.makeSamplerState(descriptor: sdesc)
        NSLog("[WINOS-GFX-INIT] Samplers OK linear=%@ nearest=%@", linearSampler != nil ? "YES" : "NO", nearestSampler != nil ? "YES" : "NO")
    }

    // MARK: - GraphicsBackend

    public func initialize(targetSize: CGSize, renderScale: Double) throws {
        NSLog("[WINOS-GFX-INIT] initialize targetSize=%.0fx%.0f renderScale=%.2f", targetSize.width, targetSize.height, renderScale)
        viewSize = targetSize
        let scaler = ResolutionScaler(gameResolution: targetSize, renderScale: renderScale)
        renderSize = scaler.renderSize
        NSLog("[WINOS-GFX-SURFACE] renderSize=%.0fx%.0f viewSize=%.0fx%.0f scale=%.2f", renderSize.width, renderSize.height, viewSize.width, viewSize.height, renderScale)
        rebuildInternalTexture()
    }

    public func resize(targetSize: CGSize, renderScale: Double) {
        NSLog("[WINOS-GFX-SURFACE] resize targetSize=%.0fx%.0f renderScale=%.2f", targetSize.width, targetSize.height, renderScale)
        do {
            try initialize(targetSize: targetSize, renderScale: renderScale)
        } catch {
            NSLog("[WINOS-GFX-SURFACE] ERROR resize failed: %@", "\(error)")
            NSLog("Portico/Metal: resize failed: %@", "\(error)")
        }
    }

    public func setVSyncLimit(_ fps: Int) {
        NSLog("[WINOS-GFX-INIT] setVSyncLimit fps=%d", fps)
        view?.preferredFramesPerSecond = fps
    }

    public func beginFrame() {
        // NSLog("[WINOS-GFX-FRAME] beginFrame") // too verbose
    }

    public func execute(_ frame: GfxFrame) {
        if frame.surface != nil {
            NSLog("[WINOS-GFX-SURFACE] execute frame with surface %dx%d", frame.surface?.width ?? 0, frame.surface?.height ?? 0)
        }
        pendingFrame = frame
    }

    public func endFrameAndPresent() {
        // apresentação acontece em draw(in:)
    }

    public func shutdown() {
        NSLog("[WINOS-GFX-INIT] shutdown")
        internalTexture = nil
        surfaceTexture = nil
        pendingFrame = GfxFrame()
    }

    // MARK: - MTKViewDelegate

    public func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        viewSize = size
    }

    public func draw(in view: MTKView) {
        // Diagnóstico detalhado para iPhone 13 físico
        guard let queue else {
            NSLog("[WINOS-GFX-COMMAND] ERROR queue nil")
            return
        }
        guard let drawable = view.currentDrawable else {
            NSLog("[WINOS-GFX-DRAWABLE] ERROR drawable nil - view size %.0fx%.0f", view.drawableSize.width, view.drawableSize.height)
            return
        }
        guard let framePass = view.currentRenderPassDescriptor else {
            NSLog("[WINOS-GFX-DRAWABLE] ERROR renderPassDescriptor nil")
            return
        }
        guard let gamePipeline, let blitPipeline,
              let vertexBuffer, let uniformBuffer else {
            NSLog("[WINOS-GFX-METAL] ERROR pipeline/buffer nil gamePipeline=%@ blitPipeline=%@", gamePipeline != nil ? "OK" : "nil", blitPipeline != nil ? "OK" : "nil")
            return
        }

        let frame = pendingFrame
        pendingFrame = GfxFrame()

        // ---- Ponte GDI: GfxFrame.surface (XRGB8888) → MTLTexture (BGRA8) ----
        var surfaceTexUsed: MTLTexture? = nil
        if let surf = frame.surface, surf.width > 0, surf.height > 0 {
            NSLog("[WINOS-GFX-SURFACE] surface %dx%d pixels=%d", surf.width, surf.height, surf.pixels.count)
            if let stex = ensureSurfaceTexture(width: Int(surf.width), height: Int(surf.height)) {
                if surfaceUpload.stage(surf) {
                    let w = Int(surf.width), h = Int(surf.height)
                    let bytesPerRow = w * 4
                    NSLog("[WINOS-GFX-SURFACE] Upload texture %dx%d bytesPerRow=%d", w, h, bytesPerRow)
                    surfaceUpload.withBytes { ptr in
                        stex.replace(region: MTLRegionMake2D(0, 0, w, h),
                                     mipmapLevel: 0, withBytes: ptr, bytesPerRow: bytesPerRow)
                    }
                    surfaceTexUsed = stex
                    NSLog("[WINOS-GFX-SURFACE] frame submitted %dx%d", w, h)
                } else {
                    NSLog("[WINOS-GFX-SURFACE] ERROR stage failed")
                }
            } else {
                NSLog("[WINOS-GFX-SURFACE] ERROR ensureSurfaceTexture failed")
            }
        }
        let hasSurface = surfaceTexUsed != nil

        // Aplica comandos do runtime no framebuffer interno
        var drawCalls: [(count: Int, first: Int)] = []
        var wrote = false
        for cmd in frame.commands {
            switch cmd {
            case .clear(let c):
                currentClearColor = MTLClearColor(red: Double(c.r), green: Double(c.g),
                                                  blue: Double(c.b), alpha: Double(c.a))
                wrote = true
                NSLog("[WINOS-GFX-COMMAND] clear r=%.2f g=%.2f b=%.2f a=%.2f", c.r, c.g, c.b, c.a)
            case .drawTriangles(let count, let first):
                drawCalls.append((Int(count), Int(first)))
                wrote = true
                NSLog("[WINOS-GFX-COMMAND] drawTriangles count=%d first=%d", count, first)
            case .present(let w, let h):
                wrote = true
                NSLog("[WINOS-GFX-COMMAND] present %dx%d", w, h)
            case .setViewport(let r):
                NSLog("[WINOS-GFX-COMMAND] viewport x=%.0f y=%.0f w=%.0f h=%.0f", r.x, r.y, r.w, r.h)
            case .setScissor(let r):
                NSLog("[WINOS-GFX-COMMAND] scissor x=%.0f y=%.0f w=%.0f h=%.0f", r.x, r.y, r.w, r.h)
            case .setFilter(let nearest):
                nearestFilter = nearest
                NSLog("[WINOS-GFX-COMMAND] filter nearest=%@", nearest ? "YES" : "NO")
            }
        }

        guard let cmdBuffer = queue.makeCommandBuffer() else {
            NSLog("[WINOS-GFX-COMMAND] ERROR makeCommandBuffer nil")
            return
        }

        // 1) passe de jogo → textura interna
        if wrote, !hasSurface, let tex = internalTexture {
            let rp = MTLRenderPassDescriptor()
            rp.colorAttachments[0].texture = tex
            rp.colorAttachments[0].loadAction = .clear
            rp.colorAttachments[0].storeAction = .store
            rp.colorAttachments[0].clearColor = currentClearColor
            if let enc = cmdBuffer.makeRenderCommandEncoder(descriptor: rp) {
                if !frame.vertices.isEmpty {
                    let n = min(frame.vertices.count, maxVertices * 3)
                    frame.vertices.withUnsafeBytes { raw in
                        if let base = raw.baseAddress {
                            memcpy(vertexBuffer.contents(), base,
                                   n * MemoryLayout<GfxVertex>.stride)
                        }
                    }
                    var u: [Float] = [1.0 / Float(renderSize.width),
                                      1.0 / Float(renderSize.height), 0, 0]
                    u.withUnsafeBytes { raw in
                        memcpy(uniformBuffer.contents(), raw.baseAddress!, 16)
                    }
                    enc.setRenderPipelineState(gamePipeline)
                    enc.setVertexBuffer(vertexBuffer, offset: 0, index: 0)
                    enc.setVertexBuffer(uniformBuffer, offset: 0, index: 1)
                    for (count, first) in drawCalls {
                        enc.drawPrimitives(type: .triangle,
                                           vertexStart: first,
                                           vertexCount: min(count, frame.vertices.count - first))
                    }
                    NSLog("[WINOS-GFX-COMMAND] Encoded %d draw calls", drawCalls.count)
                }
                enc.endEncoding()
            } else {
                NSLog("[WINOS-GFX-COMMAND] ERROR makeRenderCommandEncoder nil (internal)")
            }
        }

        // 2) blit interno → drawable (upscale, letterbox) - considera Retina scale
        let drawableSize = view.drawableSize
        NSLog("[WINOS-GFX-DRAWABLE] drawableSize=%.0fx%.0f viewBounds=%.0fx%.0f", drawableSize.width, drawableSize.height, view.bounds.width, view.bounds.height)
        framePass.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
        framePass.colorAttachments[0].loadAction = .clear
        if let enc = cmdBuffer.makeRenderCommandEncoder(descriptor: framePass) {
            if let tex = surfaceTexUsed ?? internalTexture {
                NSLog("[WINOS-GFX-METAL] Blitting texture %dx%d -> drawable %.0fx%.0f", tex.width, tex.height, drawableSize.width, drawableSize.height)
                enc.setRenderPipelineState(blitPipeline)
                enc.setFragmentTexture(tex, index: 0)
                enc.setFragmentSamplerState(nearestFilter ? nearestSampler : linearSampler,
                                            index: 0)
                enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
                NSLog("[WINOS-GFX-PRESENT] Draw blit encoded")
            } else {
                NSLog("[WINOS-GFX-METAL] ERROR no texture to blit")
            }
            enc.endEncoding()
        } else {
            NSLog("[WINOS-GFX-PRESENT] ERROR makeRenderCommandEncoder nil (drawable)")
        }

        cmdBuffer.present(drawable)
        cmdBuffer.commit()
        if hasSurface {
            NSLog("[WINOS-GFX-PRESENT] frame presented hasSurface=YES")
        } else if wrote {
            NSLog("[WINOS-GFX-PRESENT] frame presented hasSurface=NO wrote=YES")
        }
        // NSLog("[WINOS-GFX-FRAME] frame committed") // verbose, only when needed
    }

    /// Textura compartilhada (upload via replaceRegion) no tamanho da superfície.
    private func ensureSurfaceTexture(width: Int, height: Int) -> MTLTexture? {
        guard let device, width > 0, height > 0 else {
            NSLog("[WINOS-GFX-SURFACE] ERROR ensureSurfaceTexture invalid device=%@ w=%d h=%d", device != nil ? "OK" : "nil", width, height)
            return nil
        }
        if let t = surfaceTexture, t.width == width, t.height == height {
            return t
        }
        NSLog("[WINOS-GFX-SURFACE] Creating surface texture %dx%d pixelFormat=bgra8Unorm storage=shared", width, height)
        let desc = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm,
            width: width,
            height: height,
            mipmapped: false)
        desc.usage = .shaderRead
        desc.storageMode = .shared
        surfaceTexture = device.makeTexture(descriptor: desc)
        if surfaceTexture == nil {
            NSLog("[WINOS-GFX-SURFACE] ERROR makeTexture failed %dx%d", width, height)
        } else {
            NSLog("[WINOS-GFX-SURFACE] Texture created OK %dx%d", width, height)
        }
        return surfaceTexture
    }

    private func rebuildInternalTexture() {
        guard let device else {
            NSLog("[WINOS-GFX-SURFACE] ERROR rebuildInternalTexture device nil")
            return
        }
        let desc = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm,
            width: max(Int(renderSize.width), 64),
            height: max(Int(renderSize.height), 64),
            mipmapped: false)
        desc.usage = [.renderTarget, .shaderRead]
        desc.storageMode = .private
        internalTexture = device.makeTexture(descriptor: desc)
    }
}
