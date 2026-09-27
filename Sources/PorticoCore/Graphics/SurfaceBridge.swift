import Foundation

/// Ponte GfxFrame.surface → textura Metal.
///
/// FORMATO DOCUMENTADO do `GfxFrame.surface` (contrato C↔Swift↔Metal):
///   - pixels: `UInt32` little-endian **XRGB8888 = 0x00RRGGBB**
///     (em memória LE os bytes são B,G,R,0 — igual ao layout BGRA8 do Metal
///     exceto o alfa, que aqui é 0 e é forçado para 255 no upload);
///   - stride: `width * 4` bytes por linha (contíguo, sem padding);
///   - origem: superfície GDI virtual do processo (`pr_surf`, 320×240 por ora).
/// Os dados vivem em `GfxSurfaceBuffer` (buffer reutilizado); a GDI escreve
/// direto no buffer do processo e o `update` faz UMA cópia por frame — sem
/// duplicação adicional até a textura.
public enum SurfacePixelFormat {
    public static let description =
        "XRGB8888 (UInt32 LE 0x00RRGGBB, stride = width*4, sem padding)"
}

public enum SurfacePixelCodec {
    /// Converte `count` pixels XRGB8888 (0x00RRGGBB) em bytes BGRA8 (B,G,R,255)
    /// para `MTLTexture` com pixelFormat `.bgra8Unorm` (upload via replaceRegion).
    public static func xrgb8888ToBGRA8(_ src: UnsafePointer<UInt32>, count: Int,
                                       into dst: UnsafeMutablePointer<UInt8>) {
        var i = 0
        while i < count {
            let v = src[i]
            let o = i &* 4
            dst[o]         = UInt8(v & 0xFF)           // B
            dst[o &+ 1]    = UInt8((v >> 8) & 0xFF)    // G
            dst[o &+ 2]    = UInt8((v >> 16) & 0xFF)   // R
            dst[o &+ 3]    = 255                       // A (opaco)
            i &+= 1
        }
    }
}

/// Estágio de upload p/ Metal: buffer de bytes BGRA8 reutilizado entre frames
/// (só realoca quando a resolução muda). O renderer iOS chama `stage` e faz
/// `MTLTexture.replaceRegion` (API permitida e segura no iOS, textura .shared).
public final class MetalFrameUpload: @unchecked Sendable {
    public private(set) var width = 0
    public private(set) var height = 0
    private var bytes: [UInt8] = []
    /// Gancho testável de frame estagiado (o renderer registra "[METAL]
    /// frame submitted"; os testes verificam sem GPU). Chamado após stage().
    public var onStaged: ((_ width: Int, _ height: Int) -> Void)?

    public init() {}

    public var byteCount: Int { width &* height &* 4 }
    /// Capacidade atual do buffer (para testar reuso sem churn).
    public var byteCapacity: Int { bytes.count }

    /// Estagia os pixels do surface em BGRA8. `true` se pronto para upload.
    @discardableResult
    public func stage(_ surface: GfxSurfaceBuffer) -> Bool {
        guard surface.width > 0, surface.height > 0 else { return false }
        let count = Int(surface.width &* surface.height)
        guard count > 0, count <= surface.pixels.count else { return false }
        if bytes.count < count &* 4 {
            bytes = [UInt8](repeating: 0, count: count &* 4)
        }
        width = Int(surface.width)
        height = Int(surface.height)
        surface.pixels.withUnsafeBufferPointer { src in
            bytes.withUnsafeMutableBufferPointer { dst in
                SurfacePixelCodec.xrgb8888ToBGRA8(src.baseAddress!, count: count,
                                                  into: dst.baseAddress!)
            }
        }
        onStaged?(width, height)
        return true
    }

    /// Acesso aos bytes estagiados (para memcpy/replaceRegion).
    public func withBytes<R>(_ body: (UnsafePointer<UInt8>) -> R) -> R {
        bytes.withUnsafeBufferPointer { body($0.baseAddress!) }
    }
}
