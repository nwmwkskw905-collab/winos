import XCTest
@testable import PorticoCore
import PorticoRuntime

final class ImportAndGraphicsTests: XCTestCase {
    func testLogCenterLifecycle() {
        let log = LogCenter(maxEntries: 300, minLevel: .debug)
        var received = 0
        log.onEntry = { _ in received += 1 }
        log.debug("t", "d")
        log.info("t", "i")
        log.warning("t", "w")
        log.error("t", "e")
        XCTAssertEqual(received, 4)
        XCTAssertEqual(log.snapshot(minLevel: .warning).count, 2)
        XCTAssertEqual(log.snapshot(category: "t").count, 4)
        XCTAssertTrue(log.exportText(minLevel: .error).contains("e"))
        log.clear()
        XCTAssertTrue(log.snapshot().isEmpty)
    }

    func testFramePacerAndScaler() {
        var pacer = FramePacer(targetFPS: 60)
        var rendered = 0
        var t = 0.0
        for _ in 0..<120 {
            if pacer.shouldRender(now: t) { rendered += 1 }
            t += 1.0 / 120.0 // 120 Hz de relógio
        }
        // ~60 frames em 1 segundo
        XCTAssertEqual(rendered, 60, accuracy: 2)

        let scaler = ResolutionScaler(gameResolution: CGSize(width: 640, height: 360),
                                      renderScale: 0.5)
        XCTAssertEqual(scaler.renderSize.width, 320)
        XCTAssertEqual(scaler.renderSize.height, 180)
        // letterbox: 320×180 em 2000×500 limita pela altura (aspect 16:9)
        let vp = scaler.viewport(in: CGSize(width: 2000, height: 500))
        XCTAssertEqual(vp.height, 500, accuracy: 1)
        XCTAssertEqual(vp.width, 500.0 * 320.0 / 180.0, accuracy: 1)
    }

    func testInputRouterMerging() {
        let router = InputRouter()
        router.setTouchButton(.a, pressed: true)
        router.setPadButton(.b, pressed: true)
        router.setMoveStick(x: 0.5, y: -0.25)
        router.setPadMove(x: -1.0, y: 0)
        XCTAssertEqual(router.state.buttons, InputButton.a.rawValue | InputButton.b.rawValue)
        // vence a maior magnitude por eixo
        XCTAssertEqual(router.state.moveX, -1.0)
        XCTAssertEqual(router.state.moveY, -0.25)
        router.reset()
        XCTAssertEqual(router.state.buttons, 0)
    }

    func testInputActionToButton() {
        XCTAssertNil(InputButton(action: .lookLeft))
        XCTAssertEqual(InputButton(action: .buttonA), .a)
        XCTAssertEqual(InputButton(action: .start), .start)
    }

    func testZipImportScanAndFinalize() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("import")
        defer { try? FileManager.default.removeItem(at: root) }
        let log = CoreTestSupport.makeLog()
        let lib = LibraryStore(sandbox: sb, log: log)
        try lib.load()
        let envs = EnvironmentManager(sandbox: sb, log: log)
        try envs.load()
        let importer = ImportService(sandbox: sb, library: lib, environments: envs, log: log)

        // ZIP de teste com um PE mínimo embutido + arquivo comum
        var pe = Data(count: 0x400)
        pe[0] = 0x4D; pe[1] = 0x5A // "MZ"
        let efanew: UInt32 = 0x80
        pe.replaceSubrange(0x3C..<0x40, with: withUnsafeBytes(of: efanew.littleEndian) { Data($0) })
        // "PE\0\0"
        pe.replaceSubrange(0x80..<0x84, with: Data([0x50, 0x45, 0x00, 0x00]))
        let machine: UInt16 = 0x014C
        pe.replaceSubrange(0x84..<0x86, with: withUnsafeBytes(of: machine.littleEndian) { Data($0) })
        let nsec: UInt16 = 0
        pe.replaceSubrange(0x86..<0x88, with: withUnsafeBytes(of: nsec.littleEndian) { Data($0) })
        let optsize: UInt16 = 224
        pe.replaceSubrange(0x94..<0x96, with: withUnsafeBytes(of: optsize.littleEndian) { Data($0) })
        let magic: UInt16 = 0x10B
        pe.replaceSubrange(0x98..<0x9A, with: withUnsafeBytes(of: magic.littleEndian) { Data($0) })

        let zip = try TestZipBuilder.build(entries: [
            ("readme.txt", Data("ola portico".utf8), false),
            ("bin/game.exe", pe, false),
        ])
        let zipURL = root.appendingPathComponent("game.zip")
        try zip.write(to: zipURL)

        let scan = try importer.scanImport(from: zipURL)
        XCTAssertEqual(scan.allFiles.count, 2)
        XCTAssertEqual(scan.executables.count, 1)
        let exe = try XCTUnwrap(scan.executables.first)
        XCTAssertEqual(exe.kind, .windowsPE)
        XCTAssertEqual(exe.pe?.arch, "x86")
        XCTAssertNotNil(scan.suggestedMain)

        let profile = try importer.finalize(scan: scan, mainExecutable: exe, name: "Jogo ZIP")
        XCTAssertEqual(profile.nome, "Jogo ZIP")
        XCTAssertEqual(lib.games.count, 1)
        XCTAssertTrue(profile.caminho.hasPrefix("Games/"))

        // arquivos de fato no lugar
        let dir = try sb.resolveInside(profile.caminho)
        XCTAssertTrue(FileManager.default.fileExists(
            atPath: dir.appendingPathComponent("bin/game.exe").path))
    }

    func testPEInspectorOnRealHeader() throws {
        // Mesmo PE do builder C: valida o caminho Swift → C
        var pe = Data(count: 0x400)
        pe[0] = 0x4D; pe[1] = 0x5A
        let lfanew: UInt32 = 0x80
        pe.replaceSubrange(0x3C..<0x40, with: withUnsafeBytes(of: lfanew.littleEndian) { Data($0) })
        pe.replaceSubrange(0x80..<0x84, with: Data([0x50, 0x45, 0x00, 0x00]))
        let machine: UInt16 = 0x8664
        pe.replaceSubrange(0x84..<0x86, with: withUnsafeBytes(of: machine.littleEndian) { Data($0) })
        let nsec: UInt16 = 0
        pe.replaceSubrange(0x86..<0x88, with: withUnsafeBytes(of: nsec.littleEndian) { Data($0) })
        let optsize: UInt16 = 240
        pe.replaceSubrange(0x94..<0x96, with: withUnsafeBytes(of: optsize.littleEndian) { Data($0) })
        let magic: UInt16 = 0x20B
        pe.replaceSubrange(0x98..<0x9A, with: withUnsafeBytes(of: magic.littleEndian) { Data($0) })

        XCTAssertTrue(PEInspector.looksLikePE(pe))
        let info = try PEInspector.scan(pe)
        XCTAssertEqual(info.arch, "x86_64")
        XCTAssertTrue(info.isPE32Plus)

        XCTAssertThrowsError(try PEInspector.scan(Data("ZZ".utf8)))
    }

    func testAudioSessionController() {
        final class FakeBackend: AudioOutputBackend {
            var sampleRate: Double = 48000
            var state: AudioSessionState = .stopped
            var started = 0, paused = 0, stopped = 0
            var enqueued = 0
            func initialize(sampleRate: Double, bufferFrames: Int) throws {}
            func start() throws { started += 1; state = .playing }
            func pause() { paused += 1; state = .paused }
            func resume() throws { started += 1; state = .playing }
            func stop() { stopped += 1; state = .stopped }
            func setMix(_ mix: AudioMixState) {}
            func enqueue(interleaved: [Float]) { enqueued += 1 }
            func handleInterruption(began: Bool) {}
        }
        let backend = FakeBackend()
        let ctrl = AudioSessionController(backend: backend)
        ctrl.start()
        XCTAssertEqual(ctrl.state, .playing)
        ctrl.feed(interleaved: [0, 0, 0.5, -0.5])
        XCTAssertEqual(backend.enqueued, 1)
        ctrl.feed(interleaved: [])
        XCTAssertEqual(ctrl.underruns, 1)
        ctrl.pause()
        XCTAssertEqual(ctrl.state, .paused)
        ctrl.interruption(began: true)
        // pausada não vira interrupted; mas se tocasse:
        ctrl.start()
        ctrl.interruption(began: true)
        XCTAssertEqual(ctrl.state, .interrupted)
        ctrl.interruption(began: false)
        XCTAssertEqual(ctrl.state, .playing)
    }
}
