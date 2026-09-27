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
        guard let device else { return }
        queue = device.makeCommandQueue()
        vertexBuffer = device.makeBuffer(length: maxVertices * MemoryLayout<GfxVertex>.stride * 3,
                                         options: .storageModeShared)
        uniformBuffer = device.makeBuffer(length: 64, options: .storageModeShared)

        guard let library = device.makeDefaultLibrary() else { return }
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

            let bdesc = MTLRenderPipelineDescriptor()
            bdesc.colorAttachments[0].pixelFormat = .bgra8Unorm
            bdesc.vertexFunction = library.makeFunction(name: "blit_vertex")
            bdesc.fragmentFunction = library.makeFunction(name: "blit_fragment")
            blitPipeline = try device.makeRenderPipelineState(descriptor: bdesc)
        } catch {
            NSLog("Portico/Metal: falha de pipeline: %@", "\(error)")
        }

        let sdesc = MTLSamplerDescriptor()
        sdesc.minFilter = .linear
        sdesc.magFilter = .linear
        linearSampler = device.makeSamplerState(descriptor: sdesc)
        sdesc.minFilter = .nearest
        sdesc.magFilter = .nearest
        nearestSampler = device.makeSamplerState(descriptor: sdesc)
    }

    // MARK: - GraphicsBackend

    public func initialize(targetSize: CGSize, renderScale: Double) throws {
        viewSize = targetSize
        let scaler = ResolutionScaler(gameResolution: targetSize, renderScale: renderScale)
        renderSize = scaler.renderSize
        rebuildInternalTexture()
    }

    public func resize(targetSize: CGSize, renderScale: Double) {
        initialize(targetSize: targetSize, renderScale: renderScale)
    }

    public func setVSyncLimit(_ fps: Int) {
        view?.preferredFramesPerSecond = fps
    }

    public func beginFrame() {}

    public func execute(_ frame: GfxFrame) {
        pendingFrame = frame
    }

    public func endFrameAndPresent() {
        // apresentação acontece em draw(in:)
    }

    public func shutdown() {
        internalTexture = nil
        surfaceTexture = nil
        pendingFrame = GfxFrame()
    }

    // MARK: - MTKViewDelegate

    public func mtkView(_ view: MTKView, drawableSizeWillChange size: CGSize) {
        viewSize = size
    }

    public func draw(in view: MTKView) {
        guard let queue,
              let drawable = view.currentDrawable,
              let framePass = view.currentRenderPassDescriptor,
              let gamePipeline, let blitPipeline,
              let vertexBuffer, let uniformBuffer else { return }

        let frame = pendingFrame
        pendingFrame = GfxFrame()

        // ---- Ponte GDI: GfxFrame.surface (XRGB8888) → MTLTexture (BGRA8) ----
        // Upload via replaceRegion em textura .shared (API segura no iOS).
        var surfaceTexUsed: MTLTexture? = nil
        if let surf = frame.surface, surf.width > 0, surf.height > 0,
           let stex = ensureSurfaceTexture(width: Int(surf.width), height: Int(surf.height)),
           surfaceUpload.stage(surf) {
            let w = Int(surf.width), h = Int(surf.height)
            surfaceUpload.withBytes { ptr in
                stex.replace(region: MTLRegionMake2D(0, 0, w, h),
                             mipmapLevel: 0, withBytes: ptr, bytesPerRow: w * 4)
            }
            surfaceTexUsed = stex
            NSLog("[METAL] frame submitted %dx%d", w, h)
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
            case .drawTriangles(let count, let first):
                drawCalls.append((Int(count), Int(first)))
                wrote = true
            case .present:
                wrote = true
            default:
                break
            }
        }

        guard let cmdBuffer = queue.makeCommandBuffer() else { return }

        // 1) passe de jogo → textura interna (resolução do jogo); superfície
        //    GDI presente substitui a textura interna no blit (sem redesenhar)
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
                }
                enc.endEncoding()
            }
        }

        // 2) blit interno → drawable (upscale, letterbox)
        framePass.colorAttachments[0].clearColor = MTLClearColor(red: 0, green: 0, blue: 0, alpha: 1)
        framePass.colorAttachments[0].loadAction = .clear
        if let enc = cmdBuffer.makeRenderCommandEncoder(descriptor: framePass) {
            if let tex = surfaceTexUsed ?? internalTexture {
                enc.setRenderPipelineState(blitPipeline)
                enc.setFragmentTexture(tex, index: 0)
                enc.setFragmentSamplerState(nearestFilter ? nearestSampler : linearSampler,
                                            index: 0)
                enc.drawPrimitives(type: .triangle, vertexStart: 0, vertexCount: 3)
            }
            enc.endEncoding()
        }

        cmdBuffer.present(drawable)
        cmdBuffer.commit()
        if hasSurface {
            NSLog("[METAL] frame presented")
        }
    }

    /// Textura compartilhada (upload via replaceRegion) no tamanho da superfície.
    private func ensureSurfaceTexture(width: Int, height: Int) -> MTLTexture? {
        guard let device, width > 0, height > 0 else { return nil }
        if let t = surfaceTexture, t.width == width, t.height == height {
            return t
        }
        let desc = MTLTextureDescriptor.texture2DDescriptor(
            pixelFormat: .bgra8Unorm,
            width: width,
            height: height,
            mipmapped: false)
        desc.usage = .shaderRead
        desc.storageMode = .shared
        surfaceTexture = device.makeTexture(descriptor: desc)
        return surfaceTexture
    }

    private func rebuildInternalTexture() {
        guard let device else { return }
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
