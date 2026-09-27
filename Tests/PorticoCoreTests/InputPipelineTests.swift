import XCTest
import Foundation
@testable import PorticoCore
import PorticoRuntime

/// FASE 8 + 4/5 do GRUPO 7: pipeline iOS → InputManager → mensagem Win32
/// observável, teclado→WM_CHAR, controle→pr_input_state e timers.
/// (As etapas WndProc→GDI→BGRA8→Metal são cobertas por hello_input.exe +
/// GraphicsBridgeTests; aqui validamos as pontes Swift↔C da entrada.)
final class InputPipelineTests: XCTestCase {

    // MARK: - helpers C (espelham test_gdiwin/test_input em Swift)

    private func writeU32(_ p: UnsafeMutablePointer<UInt8>, _ off: Int, _ v: UInt32) {
        var le = v.littleEndian
        withUnsafeBytes(of: &le) { raw in
            for i in 0..<4 { p[off + i] = raw[i] }
        }
    }

    private func writeU64(_ p: UnsafeMutablePointer<UInt8>, _ off: Int, _ v: UInt64) {
        var le = v.littleEndian
        withUnsafeBytes(of: &le) { raw in
            for i in 0..<8 { p[off + i] = raw[i] }
        }
    }

    private func writeStr(_ p: UnsafeMutablePointer<UInt8>, _ off: Int, _ s: String) {
        let b = Array(s.utf8)
        for i in 0..<b.count { p[off + i] = b[i] }
        p[off + b.count] = 0
    }

    private func readU32(_ p: UnsafeMutablePointer<UInt8>, _ off: Int) -> UInt32 {
        var v: UInt32 = 0
        withUnsafeMutableBytes(of: &v) { raw in
            for i in 0..<4 { raw[i] = p[off + i] }
        }
        return UInt32(littleEndian: v)
    }

    private func readU64(_ p: UnsafeMutablePointer<UInt8>, _ off: Int) -> UInt64 {
        var v: UInt64 = 0
        withUnsafeMutableBytes(of: &v) { raw in
            for i in 0..<8 { raw[i] = p[off + i] }
        }
        return UInt64(littleEndian: v)
    }

    private func clearBytes(_ p: UnsafeMutablePointer<UInt8>, _ n: Int) {
        for i in 0..<n { p[i] = 0 }
    }

    private func peek(_ ctx: OpaquePointer, _ msgp: UnsafeMutablePointer<UInt8>,
                      remove: Bool) -> (Bool, UInt32, UInt32, UInt64, UInt64) {
        clearBytes(msgp, 48)
        var args = [UInt64](repeating: 0, count: 8)
        args[0] = UInt64(UInt(bitPattern: msgp))
        args[4] = remove ? 1 : 0
        var ret: UInt64 = 0
        _ = pr_win32_call(ctx, "user32.dll", "PeekMessageA", args, 5, &ret)
        if ret == 0 { return (false, 0, 0, 0, 0) }
        return (true, readU32(msgp, 0), readU32(msgp, 8),
                readU64(msgp, 16), readU64(msgp, 24))
    }

    private func get(_ ctx: OpaquePointer, _ msgp: UnsafeMutablePointer<UInt8>)
        -> (UInt64, UInt32, UInt32, UInt64, UInt64) {
        clearBytes(msgp, 48)
        var args = [UInt64](repeating: 0, count: 8)
        args[0] = UInt64(UInt(bitPattern: msgp))
        var ret: UInt64 = 0
        _ = pr_win32_call(ctx, "user32.dll", "GetMessageA", args, 4, &ret)
        return (ret, readU32(msgp, 0), readU32(msgp, 8),
                readU64(msgp, 16), readU64(msgp, 24))
    }

    /// WndProc do hospedeiro: criação/pintura encerram com 0; resto com 1.
    private static let wndProc: pr_win32_guestcall_fn = { _, _, args, out in
        let msg = args![1]
        out?.pointee = (msg == 0x000F || msg == 0x0001) ? 0 : 1
        return 1
    }

    /// Cria janela de teste (320×240) com foco, espelhando test_gdiwin.
    private func makeWindow(_ ctx: OpaquePointer,
                            _ scratch: UnsafeMutablePointer<UInt8>) -> UInt32 {
        pr_win32_set_guestcall(ctx, Self.wndProc, nil)
        let wc = scratch            // WNDCLASSA 72B @0
        let cname = scratch + 80
        let title = scratch + 96
        clearBytes(wc, 72)
        writeStr(cname, 0, "SwInCls")
        writeStr(title, 0, "t")
        writeU64(wc, 8, 0x401000)   // wndproc fake (seam decide)
        writeU64(wc, 64, UInt64(UInt(bitPattern: cname)))

        var args = [UInt64](repeating: 0, count: 8)
        args[0] = UInt64(UInt(bitPattern: wc))
        var cls: UInt64 = 0
        XCTAssertEqual(pr_win32_call(ctx, "user32.dll", "RegisterClassA", args, 1, &cls), PR_OK)

        for i in 0..<8 { args[i] = 0 }
        args[1] = UInt64(UInt(bitPattern: cname))
        args[2] = UInt64(UInt(bitPattern: title))
        args[3] = 0x10CF0000
        args[6] = 320
        args[7] = 240
        var hwnd: UInt64 = 0
        XCTAssertEqual(pr_win32_call(ctx, "user32.dll", "CreateWindowExA", args, 12, &hwnd), PR_OK)
        XCTAssertNotEqual(hwnd, 0)
        return UInt32(truncatingIfNeeded: hwnd)
    }

    // MARK: - FASE 8 (etapas iOS→Win32) + FASE 4

    func testIOSTouchPipelineProducesWin32MouseMessages() throws {
        let ctx = try XCTUnwrap(pr_win32_create(nil))
        defer { pr_win32_destroy(ctx) }
        var sz = 0
        let scratch = try XCTUnwrap(pr_win32_scratch(ctx, &sz))
        XCTAssertGreaterThanOrEqual(sz, 256)
        let msgp = scratch + 128
        let hwnd = makeWindow(ctx, scratch)
        XCTAssertEqual(pr_win32_focus_hwnd(ctx), hwnd)

        let adapter = TouchInputAdapter()
        // UITouch began → WM_MOUSEMOVE + WM_LBUTTONDOWN (promoção p/ mouse)
        XCTAssertTrue(adapter.dispatch(TouchEvent(id: 1, phase: .began, x: 40, y: 50),
                                       to: ctx))
        var (ok, h, msg, wp, lp) = peek(ctx, msgp, remove: true)
        XCTAssertTrue(ok)
        XCTAssertEqual(h, hwnd)
        XCTAssertEqual(msg, 0x0200)                    // WM_MOUSEMOVE
        XCTAssertEqual(lp, (50 << 16) | 40)
        (ok, h, msg, wp, lp) = peek(ctx, msgp, remove: true)
        XCTAssertTrue(ok)
        XCTAssertEqual(msg, 0x0201)                    // WM_LBUTTONDOWN
        XCTAssertEqual(wp, 1)                          // MK_LBUTTON

        // 2º toque DURANTE o primário (multi-touch): rastreado, sem mouse
        XCTAssertTrue(adapter.dispatch(TouchEvent(id: 2, phase: .began, x: 70, y: 80),
                                       to: ctx))
        XCTAssertTrue(adapter.dispatch(TouchEvent(id: 2, phase: .moved, x: 71, y: 81),
                                       to: ctx))
        (ok, _, _, _, _) = peek(ctx, msgp, remove: true)
        XCTAssertFalse(ok)                             // fila vazia

        // moved → WM_MOUSEMOVE; ended → WM_LBUTTONUP
        XCTAssertTrue(adapter.dispatch(TouchEvent(id: 1, phase: .moved, x: 41, y: 51),
                                       to: ctx))
        (_, _, msg, _, lp) = peek(ctx, msgp, remove: true)
        XCTAssertEqual(msg, 0x0200)
        XCTAssertEqual(lp, (51 << 16) | 41)
        XCTAssertTrue(adapter.dispatch(TouchEvent(id: 1, phase: .ended, x: 41, y: 51),
                                       to: ctx))
        (_, _, msg, _, _) = peek(ctx, msgp, remove: true)
        XCTAssertEqual(msg, 0x0202)                    // WM_LBUTTONUP

        // sem toque ativo, o restante é promovido a primário (semântica
        // UIKit-like): ended do toque 2 → WM_LBUTTONUP
        XCTAssertTrue(adapter.dispatch(TouchEvent(id: 2, phase: .ended, x: 71, y: 81),
                                       to: ctx))
        (ok, _, msg, _, _) = peek(ctx, msgp, remove: true)
        XCTAssertTrue(ok)
        XCTAssertEqual(msg, 0x0202)
        (ok, _, _, _, _) = peek(ctx, msgp, remove: true)
        XCTAssertFalse(ok)
    }

    func testKeyboardToWMCharPipeline() throws {
        let ctx = try XCTUnwrap(pr_win32_create(nil))
        defer { pr_win32_destroy(ctx) }
        var sz = 0
        let scratch = try XCTUnwrap(pr_win32_scratch(ctx, &sz))
        let msgp = scratch + 128
        _ = makeWindow(ctx, scratch)

        XCTAssertEqual(pr_win32_input_key(ctx, UInt32(W32_VK_A), 1), PR_OK)
        var (ret, h, msg, wp, lp) = get(ctx, msgp)
        XCTAssertEqual(ret, 1)
        XCTAssertEqual(msg, 0x0100)                    // WM_KEYDOWN
        XCTAssertEqual(wp, UInt64(W32_VK_A))
        XCTAssertEqual(lp, 1)

        // TranslateMessage real → WM_CHAR 'a'
        var args = [UInt64](repeating: 0, count: 8)
        args[0] = UInt64(UInt(bitPattern: msgp))
        var tret: UInt64 = 0
        XCTAssertEqual(pr_win32_call(ctx, "user32.dll", "TranslateMessage", args, 1, &tret), PR_OK)
        XCTAssertEqual(tret, 1)
        (_, _, msg, wp, _) = get(ctx, msgp)
        XCTAssertEqual(msg, 0x0102)                    // WM_CHAR
        XCTAssertEqual(wp, UInt64(Character("a").asciiValue!))
    }

    func testGameControllerUpdatesPadState() throws {
        let ctx = try XCTUnwrap(pr_win32_create(nil))
        defer { pr_win32_destroy(ctx) }

        let adapter = GameControllerAdapter()
        let sample = GamePadSample(buttons: 1 | 256, moveX: 0.5, moveY: -0.25,
                                   camX: 0.1, camY: 0, triggerLT: 0.75, triggerRT: 0.2)
        XCTAssertTrue(adapter.apply(sample, to: ctx))
        let pad = try XCTUnwrap(pr_win32_input_pad(ctx))
        XCTAssertEqual(pad.pointee.buttons, 1 | 256)   // PR_BTN_A | PR_BTN_DUP
        XCTAssertEqual(pad.pointee.axes.0, 0.5)        // MOVE_X
        XCTAssertEqual(pad.pointee.axes.1, -0.25)      // MOVE_Y
        XCTAssertEqual(pad.pointee.trigger_lt, 0.75)
        XCTAssertEqual(pad.pointee.trigger_rt, 0.2)
    }

    func testTimerMessagesAdvanceDeterministically() throws {
        let ctx = try XCTUnwrap(pr_win32_create(nil))
        defer { pr_win32_destroy(ctx) }
        var sz = 0
        let scratch = try XCTUnwrap(pr_win32_scratch(ctx, &sz))
        let msgp = scratch + 128
        let hwnd = makeWindow(ctx, scratch)

        var args = [UInt64](repeating: 0, count: 8)
        args[0] = UInt64(hwnd)
        args[1] = 7
        args[2] = 10
        var ret: UInt64 = 0
        XCTAssertEqual(pr_win32_call(ctx, "user32.dll", "SetTimer", args, 4, &ret), PR_OK)
        XCTAssertEqual(ret, 7)

        XCTAssertEqual(pr_win32_advance_time(ctx, 5), PR_OK)
        var (ok, _, _, _, _) = peek(ctx, msgp, remove: true)
        XCTAssertFalse(ok)                             // cedo demais
        XCTAssertEqual(pr_win32_advance_time(ctx, 10), PR_OK)
        (ok, _, _, _, _) = peek(ctx, msgp, remove: true)
        XCTAssertTrue(ok)
        var msg = readU32(msgp, 8)
        var wp = readU64(msgp, 16)
        XCTAssertEqual(msg, 0x0113)                    // WM_TIMER
        XCTAssertEqual(wp, 7)

        // KillTimer encerra
        for i in 0..<8 { args[i] = 0 }
        args[0] = UInt64(hwnd)
        args[1] = 7
        XCTAssertEqual(pr_win32_call(ctx, "user32.dll", "KillTimer", args, 2, &ret), PR_OK)
        XCTAssertEqual(pr_win32_advance_time(ctx, 100), PR_OK)
        (ok, _, msg, wp, _) = peek(ctx, msgp, remove: true)
        XCTAssertFalse(ok)
    }
}
