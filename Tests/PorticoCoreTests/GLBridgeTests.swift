import XCTest
import Foundation
@testable import PorticoCore
import PorticoRuntime

/// FASE 5 do GRUPO 8: o frame produzido pelo PE 3D REAL (hello_gl.exe —
/// OpenGL 1.1 por software) chega ao pipeline já certificado:
/// 3D guest → superfície XRGB8888 → SurfaceBridge → BGRA8 → MetalFrameUpload
/// (contrato `replaceRegion`/blit do MetalGameRenderer) → "frame presented".
final class GLBridgeTests: XCTestCase {

    /// GRUPO 9: frame do SEGUNDO PE 3D (hello_gl2 — matrizes + vertex
    /// arrays) pelo mesmo contrato SurfaceBridge→BGRA8→Metal.
    func testSecondGLFrameFlowsThroughBridgeToMetalUpload() throws {
        let base = "Tests/PorticoRuntimeTests/data/"
        let imgData = try Data(contentsOf: URL(fileURLWithPath: base + "hello_gl2.exe"))
        let dllData = try Data(contentsOf: URL(fileURLWithPath: base + "hello_dll.dll"))
        XCTAssertGreaterThan(imgData.count, 1024)

        let log = pr_log_create(1024)
        var p: OpaquePointer?
        let created: pr_status = imgData.withUnsafeBytes { raw in
            pr_peproc_create(raw.baseAddress, raw.count, log, &p)
        }
        XCTAssertEqual(created, PR_OK)
        let proc = try XCTUnwrap(p)
        defer { pr_peproc_destroy(proc) }
        let provided: pr_status = dllData.withUnsafeBytes { raw in
            pr_peproc_provide_dll(proc, "hello_dll.dll", raw.baseAddress,
                                  raw.count)
        }
        XCTAssertEqual(provided, PR_OK)
        XCTAssertEqual(pr_peproc_prepare(proc), PR_OK)
        var exec: UInt64 = 0
        var i = 0
        while i < 5000 && pr_peproc_exited(proc) == 0 {
            if pr_peproc_step(proc, 10000, &exec) != PR_OK { break }
            i += 1
        }
        XCTAssertEqual(pr_peproc_exited(proc), 1)
        XCTAssertEqual(pr_peproc_exit_code(proc), 42)

        let w32 = try XCTUnwrap(pr_peproc_win32(proc))
        let sp = try XCTUnwrap(pr_win32_surface(w32))
        let px = try XCTUnwrap(pr_surf_pixels(sp))
        let surf = GfxSurfaceBuffer()
        XCTAssertTrue(surf.update(width: 320, height: 240, copyFrom: px))
        let up = MetalFrameUpload()
        var staged = false
        up.onStaged = { _, _ in staged = true }
        XCTAssertTrue(up.stage(surf))
        XCTAssertTrue(staged)
        // pixels derivados da geometria (faces do cubo girado):
        up.withBytes { b in
            func px4(_ x: Int, _ y: Int) -> [UInt8] {
                let o = (y * 320 + x) * 4
                return [b[o], b[o + 1], b[o + 2], b[o + 3]]
            }
            XCTAssertEqual(px4(162, 131), [0, 255, 0, 255])   // frente: verde
            XCTAssertEqual(px4(96, 109), [0, 0, 255, 255])    // esq.: vermelho
            XCTAssertEqual(px4(150, 64), [255, 0, 0, 255])    // topo: azul
            XCTAssertEqual(px4(8, 231), [0, 0, 0, 255])       // fundo: preto
        }
    }

    func testGLFrameFlowsThroughBridgeToMetalUpload() throws {
        let base = "Tests/PorticoRuntimeTests/data/"
        let imgData = try Data(contentsOf: URL(fileURLWithPath: base + "hello_gl.exe"))
        let dllData = try Data(contentsOf: URL(fileURLWithPath: base + "hello_dll.dll"))
        XCTAssertGreaterThan(imgData.count, 1024)

        let log = pr_log_create(1024)
        var p: OpaquePointer?
        let created: pr_status = imgData.withUnsafeBytes { raw in
            pr_peproc_create(raw.baseAddress, raw.count, log, &p)
        }
        XCTAssertEqual(created, PR_OK)
        let proc = try XCTUnwrap(p)
        defer { pr_peproc_destroy(proc) }

        let provided: pr_status = dllData.withUnsafeBytes { raw in
            pr_peproc_provide_dll(proc, "hello_dll.dll", raw.baseAddress,
                                  raw.count)
        }
        XCTAssertEqual(provided, PR_OK)
        XCTAssertEqual(pr_peproc_prepare(proc), PR_OK)

        // execução real do PE 3D (determinística; sem hardware)
        var exec: UInt64 = 0
        var i = 0
        while i < 5000 && pr_peproc_exited(proc) == 0 {
            if pr_peproc_step(proc, 10000, &exec) != PR_OK { break }
            i += 1
        }
        XCTAssertEqual(pr_peproc_exited(proc), 1)
        XCTAssertEqual(pr_peproc_exit_code(proc), 42)

        // frame APRESENTADO (SwapBuffers) → superfície XRGB8888 do caminho Metal
        let w32 = try XCTUnwrap(pr_peproc_win32(proc))
        let sp = try XCTUnwrap(pr_win32_surface(w32))
        XCTAssertEqual(pr_surf_width(sp), 320)
        XCTAssertEqual(pr_surf_height(sp), 240)
        let px = try XCTUnwrap(pr_surf_pixels(sp))

        // SurfaceBridge (contrato já certificado — sem alterar o caminho GDI)
        let surf = GfxSurfaceBuffer()
        XCTAssertTrue(surf.update(width: 320, height: 240, copyFrom: px))
        let up = MetalFrameUpload()
        var stagedW = 0, stagedH = 0
        up.onStaged = { w, h in stagedW = w; stagedH = h }   // "frame submitted"
        XCTAssertTrue(up.stage(surf))
        XCTAssertEqual(stagedW, 320)
        XCTAssertEqual(stagedH, 240)

        // BGRA8 de entrada do MetalGameRenderer (replaceRegion) — pixels do
        // triângulo 3D com z-buffer: oclusão (verde), trás (vermelho), clear
        up.withBytes { b in
            func px(_ x: Int, _ y: Int) -> [UInt8] {
                let o = (y * 320 + x) * 4
                return [b[o], b[o + 1], b[o + 2], b[o + 3]]
            }
            // XRGB→BGRA: verde 0x0000FF00 → B=0,G=255,R=0,A=255
            XCTAssertEqual(px(160, 119), [0, 255, 0, 255])   // frente venceu
            // vermelho 0x00FF0000 → B=0,G=0,R=255,A=255
            XCTAssertEqual(px(160, 199), [0, 0, 255, 255])   // só trás
            // clear preto → 0,0,0,255
            XCTAssertEqual(px(20, 219), [0, 0, 0, 255])
        }
    }
}
