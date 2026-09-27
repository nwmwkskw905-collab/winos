import XCTest
@testable import PorticoCore

/// GRUPO 6 — FASE 3/4: superfície BGRA8 → estágio de upload Metal.
///
/// Contrato C↔Swift↔Metal (SurfaceBridge.swift):
///   GfxFrame.surface = XRGB8888 (UInt32 LE 0x00RRGGBB, stride = width*4)
///   → SurfacePixelCodec → bytes BGRA8 (B,G,R,255) → MTLTexture .bgra8Unorm
///   via replaceRegion (bytesPerRow = width*4).
/// Os testes validam o pipeline inteiro até a fronteira da API Metal
/// (sem GPU: o renderer iOS consome exatamente estes bytes no device).
final class GraphicsBridgeTests: XCTestCase {

    /// GDI (XRGB8888) → BGRA8: codec byte a byte (incl. alfa opaco).
    func testXRGB8888ToBGRA8CodecIsByteExact() {
        // padrão de referência: R, G, B, "quase-preto" (0x00123456)
        var src: [UInt32] = [0x00FF0000, 0x0000FF00, 0x000000FF, 0x00123456]
        var dst = [UInt8](repeating: 0, count: 16)
        src.withUnsafeBufferPointer { s in
            dst.withUnsafeMutableBufferPointer { d in
                SurfacePixelCodec.xrgb8888ToBGRA8(s.baseAddress!, count: 4,
                                                  into: d.baseAddress!)
            }
        }
        XCTAssertEqual(Array(dst[0..<4]), [0x00, 0x00, 0xFF, 0xFF])   // R
        XCTAssertEqual(Array(dst[4..<8]), [0x00, 0xFF, 0x00, 0xFF])   // G
        XCTAssertEqual(Array(dst[8..<12]), [0xFF, 0x00, 0x00, 0xFF])  // B
        XCTAssertEqual(Array(dst[12..<16]), [0x56, 0x34, 0x12, 0xFF])
    }

    /// GDI → BGRA8: o padrão real do hello_gdi.exe chega íntegro ao estágio.
    func testGDISurfaceXRGB8888ReachesBGRA8Upload() {
        // pixels do PE gráfico real (fundo/retângulo/pixel) em XRGB8888
        var px: [UInt32] = [0x00141840, 0x00C82828,
                            0x0028B450, 0x00FFFF00]
        let surf = GfxSurfaceBuffer()
        px.withUnsafeBufferPointer { p in
            XCTAssertTrue(surf.update(width: 2, height: 2,
                                      copyFrom: p.baseAddress))
        }
        let up = MetalFrameUpload()
        XCTAssertTrue(up.stage(surf))
        up.withBytes { b in
            // fundo RGB(20,24,64) = XRGB 0x00141840 → BGRA 40,18,14,FF
            XCTAssertEqual(b[0], 0x40); XCTAssertEqual(b[1], 0x18)
            XCTAssertEqual(b[2], 0x14); XCTAssertEqual(b[3], 0xFF)
            // ret. vermelho RGB(200,40,40) = 0x00C82828 → 28,28,C8,FF
            XCTAssertEqual(b[4], 0x28); XCTAssertEqual(b[5], 0x28)
            XCTAssertEqual(b[6], 0xC8); XCTAssertEqual(b[7], 0xFF)
        }
    }

    /// BGRA8 → Metal: bytes exatamente no contrato do replaceRegion
    /// (.bgra8Unorm, bytesPerRow = width*4, contíguo, A=255).
    func testBGRA8BytesMatchMetalReplaceRegionContract() {
        var px = [UInt32](repeating: 0x00141840, count: 3 * 2)   // 3×2
        let surf = GfxSurfaceBuffer()
        px.withUnsafeBufferPointer { p in
            XCTAssertTrue(surf.update(width: 3, height: 2, copyFrom: p.baseAddress))
        }
        let up = MetalFrameUpload()
        XCTAssertTrue(up.stage(surf))
        XCTAssertEqual(up.width, 3)
        XCTAssertEqual(up.height, 2)
        // bytesPerRow = width*4 = 12; total = 12*height = 24
        XCTAssertEqual(up.byteCount, 24)
        up.withBytes { b in
            for row in 0..<2 {
                let o = row * 12
                XCTAssertEqual(b[o], 0x40); XCTAssertEqual(b[o + 1], 0x18)
                XCTAssertEqual(b[o + 2], 0x14); XCTAssertEqual(b[o + 3], 0xFF)
            }
        }
    }

    /// Múltiplos frames: buffer reutilizado sem churn (upload eficiente).
    func testMultipleFramesStageWithoutReallocation() {
        var px = [UInt32](repeating: 0x00000000, count: 320 * 240)
        let surf = GfxSurfaceBuffer()
        px.withUnsafeBufferPointer { p in
            XCTAssertTrue(surf.update(width: 320, height: 240,
                                      copyFrom: p.baseAddress))
        }
        let up = MetalFrameUpload()
        var staged = 0
        up.onStaged = { _, _ in staged += 1 }
        XCTAssertTrue(up.stage(surf))
        let cap = up.byteCapacity
        for i in 0..<127 {
            px[0] = UInt32(i)   // conteúdo muda por frame
            px.withUnsafeBufferPointer { p in
                XCTAssertTrue(surf.update(width: 320, height: 240,
                                          copyFrom: p.baseAddress))
            }
            XCTAssertTrue(up.stage(surf))
        }
        XCTAssertEqual(staged, 128)                 // 128 frames estagiados
        XCTAssertEqual(up.byteCapacity, cap)        // sem realocação
        XCTAssertEqual(up.byteCount, 320 * 240 * 4)
    }

    /// Resize: mudança de resolução realoca e mantém o contrato.
    func testResolutionChangeReallocatesUploadBuffer() {
        var small = [UInt32](repeating: 0x00141840, count: 320 * 240)
        var big = [UInt32](repeating: 0x00C82828, count: 640 * 480)
        let surf = GfxSurfaceBuffer()
        let up = MetalFrameUpload()
        small.withUnsafeBufferPointer { p in
            XCTAssertTrue(surf.update(width: 320, height: 240, copyFrom: p.baseAddress))
        }
        XCTAssertTrue(up.stage(surf))
        XCTAssertEqual(up.byteCount, 320 * 240 * 4)
        big.withUnsafeBufferPointer { p in
            XCTAssertTrue(surf.update(width: 640, height: 480, copyFrom: p.baseAddress))
        }
        XCTAssertTrue(up.stage(surf))
        XCTAssertEqual(up.width, 640)
        XCTAssertEqual(up.height, 480)
        XCTAssertEqual(up.byteCount, 640 * 480 * 4)
        up.withBytes { b in
            XCTAssertEqual(b[0], 0x28); XCTAssertEqual(b[2], 0xC8)  // novo padrão
        }
        // e volta: o buffer reutiliza a capacidade maior (sem encolher)
        small.withUnsafeBufferPointer { p in
            XCTAssertTrue(surf.update(width: 320, height: 240, copyFrom: p.baseAddress))
        }
        XCTAssertTrue(up.stage(surf))
        XCTAssertEqual(up.byteCount, 320 * 240 * 4)
        XCTAssertGreaterThanOrEqual(up.byteCapacity, 640 * 480 * 4)
    }

    /// Memória inválida: recusa honesta (nunca sucesso falso).
    func testInvalidSurfaceMemoryIsRejectedHonestly() {
        let surf = GfxSurfaceBuffer()   // nunca atualizada
        let up = MetalFrameUpload()
        XCTAssertFalse(up.stage(surf))  // dimensões zero
        var px = [UInt32](repeating: 0, count: 4)
        px.withUnsafeBufferPointer { p in
            XCTAssertFalse(surf.update(width: 0, height: 2, copyFrom: p.baseAddress))
            XCTAssertFalse(surf.update(width: 2, height: 0, copyFrom: p.baseAddress))
            XCTAssertFalse(surf.update(width: 2, height: 2, copyFrom: nil))
        }
    }

    /// O gancho de frame estagiado dispara com as dimensões certas
    /// (é o mesmo ponto em que o renderer iOS registra "[METAL] frame ...").
    func testOnStagedHookReportsFrameDimensions() {
        var px = [UInt32](repeating: 0, count: 8 * 4)
        let surf = GfxSurfaceBuffer()
        px.withUnsafeBufferPointer { p in
            XCTAssertTrue(surf.update(width: 8, height: 4, copyFrom: p.baseAddress))
        }
        let up = MetalFrameUpload()
        var seen: (Int, Int)?
        up.onStaged = { w, h in seen = (w, h) }
        XCTAssertTrue(up.stage(surf))
        XCTAssertEqual(seen?.0, 8)
        XCTAssertEqual(seen?.1, 4)
    }
}
