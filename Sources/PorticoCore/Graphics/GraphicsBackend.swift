import Foundation

// MARK: - Comandos gráficos (ABI comum com o runtime C)

public struct GfxColor: Equatable, Sendable {
    public var r: Float, g: Float, b: Float, a: Float
    public init(r: Float, g: Float, b: Float, a: Float) {
        self.r = r; self.g = g; self.b = b; self.a = a
    }
}

public struct GfxRect: Equatable, Sendable {
    public var x: Float, y: Float, w: Float, h: Float
    public init(x: Float, y: Float, w: Float, h: Float) {
        self.x = x; self.y = y; self.w = w; self.h = h
    }
}

public struct GfxVertex: Equatable, Sendable {
    public var x: Float, y: Float, r: Float, g: Float, b: Float
    public init(x: Float, y: Float, r: Float, g: Float, b: Float) {
        self.x = x; self.y = y; self.r = r; self.g = g; self.b = b
    }
}

public enum GfxCommand: Equatable, Sendable {
    case clear(GfxColor)
    case setViewport(GfxRect)
    case setScissor(GfxRect)
    case drawTriangles(vertexCount: UInt32, firstVertex: UInt32)
    case present(width: UInt32, height: UInt32)
    case setFilter(nearest: Bool)
}

/// Um frame completo produzido pelo backend de execução.
public final class GfxSurfaceBuffer: @unchecked Sendable {
    public private(set) var width: UInt32 = 0
    public private(set) var height: UInt32 = 0
    public private(set) var pixels: [UInt32] = []

    public init() {}

    /// Atualiza os pixels (buffer reutilizado — só realoca quando a resolução muda).
    @discardableResult
    public func update(width: UInt32, height: UInt32,
                       copyFrom src: UnsafePointer<UInt32>?) -> Bool {
        guard width > 0, height > 0, let src else { return false }
        let count = Int(width &* height)
        guard count > 0 else { return false }
        if pixels.count < count {
            pixels = [UInt32](repeating: 0, count: count)
        }
        self.width = width
        self.height = height
        pixels.withUnsafeMutableBufferPointer { dst in
            dst.baseAddress!.update(from: src, count: count)
        }
        return true
    }
}

public struct GfxFrame: Sendable {
    public var commands: [GfxCommand]
    public var vertices: [GfxVertex]
    /// Textura do frame (caminho de apresentação por pixels); nil = só vértices.
    public var surface: GfxSurfaceBuffer?
    public init(commands: [GfxCommand] = [], vertices: [GfxVertex] = [],
                surface: GfxSurfaceBuffer? = nil) {
        self.commands = commands
        self.vertices = vertices
        self.surface = surface
    }
}

/// Backend gráfico (implementação iOS: MetalBackend). A abstração cobre
/// renderer/texture/buffer/shader/pipeline/framebuffer/command queue/sync.
public protocol GraphicsBackend: AnyObject {
    var backendID: String { get }
    var isAvailable: Bool { get }
    func initialize(targetSize: CGSize, renderScale: Double) throws
    func resize(targetSize: CGSize, renderScale: Double)
    func beginFrame()
    func execute(_ frame: GfxFrame)
    func endFrameAndPresent()
    func setVSyncLimit(_ fps: Int)
    func shutdown()
}

// MARK: - Pacing e escala

/// Limitador de FPS com clock monotônico (sem alocações por frame).
public struct FramePacer {
    public let targetFPS: Int
    private var nextDeadline: Double = 0
    public private(set) var presentedFrames: UInt64 = 0

    public init(targetFPS: Int) {
        self.targetFPS = max(1, min(targetFPS, 480))
    }

    public var frameInterval: Double { 1.0 / Double(targetFPS) }

    /// Retorna true quando já é hora de renderizar mais um frame.
    public mutating func shouldRender(now: Double) -> Bool {
        if nextDeadline == 0 {
            nextDeadline = now + frameInterval
            presentedFrames += 1
            return true
        }
        if now + 1e-9 >= nextDeadline {
            nextDeadline += frameInterval
            if nextDeadline < now {
                // atrasou mais que um intervalo: reancora
                nextDeadline = now + frameInterval
            }
            presentedFrames += 1
            return true
        }
        return false
    }

    public mutating func reset() {
        nextDeadline = 0
        presentedFrames = 0
    }
}

/// Calcula o tamanho do framebuffer interno (resolução do jogo × qualidade).
public struct ResolutionScaler {
    public var gameResolution: CGSize
    public var renderScale: Double

    public init(gameResolution: CGSize, renderScale: Double = 1.0) {
        self.gameResolution = gameResolution
        self.renderScale = min(max(renderScale, 0.25), 2.0)
    }

    public var renderSize: CGSize {
        CGSize(width: max(64, (gameResolution.width * renderScale).rounded()),
               height: max(64, (gameResolution.height * renderScale).rounded()))
    }

    /// Viewport final na tela (letterbox preservando aspect ratio).
    public func viewport(in viewSize: CGSize) -> CGRect {
        let rw = renderSize.width, rh = renderSize.height
        guard rw > 0, rh > 0, viewSize.width > 0, viewSize.height > 0 else {
            return CGRect(origin: .zero, size: viewSize)
        }
        let scale = min(viewSize.width / rw, viewSize.height / rh)
        let w = rw * scale, h = rh * scale
        return CGRect(x: (viewSize.width - w) / 2, y: (viewSize.height - h) / 2,
                      width: w, height: h)
    }
}
