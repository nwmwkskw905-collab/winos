import XCTest
import Foundation
import PorticoRuntime
@testable import PorticoCore

/// Construtor de PE32 em memória para testes do loader:
/// 2 DLLs importadas (KERNEL32: nome+ordinal; USER32: nome) e 2 exports.
enum TestPEBuilder {
    static func build() -> Data {
        var buf = [UInt8](repeating: 0, count: 0x600)
        func w16(_ off: Int, _ v: UInt16) {
            buf[off] = UInt8(v & 0xFF); buf[off + 1] = UInt8(v >> 8)
        }
        func w32(_ off: Int, _ v: UInt32) {
            buf[off] = UInt8(v & 0xFF); buf[off + 1] = UInt8((v >> 8) & 0xFF)
            buf[off + 2] = UInt8((v >> 16) & 0xFF); buf[off + 3] = UInt8(v >> 24)
        }
        func wstr(_ off: Int, _ s: String) {
            for (i, b) in s.utf8.enumerated() { buf[off + i] = b }
        }

        buf[0] = 0x4D; buf[1] = 0x5A        // MZ
        w32(0x3C, 0x80)
        let pe = 0x80
        wstr(pe, "PE")
        w16(pe + 4, 0x014C)                 // i386
        w16(pe + 6, 2)                      // 2 seções
        w16(pe + 20, 224)
        w16(pe + 22, 0x0102)
        let opt = pe + 24
        w16(opt, 0x10B)                     // PE32
        w32(opt + 16, 0x1000)               // entry
        w32(opt + 28, 0x00400000)           // image base
        w32(opt + 32, 0x1000)
        w32(opt + 36, 0x200)
        w32(opt + 56, 0x3000)               // size of image
        w32(opt + 60, 0x200)                // size of headers
        w16(opt + 68, 3)                    // console
        w32(opt + 92, 16)
        w32(opt + 96, 0x2000)               // exports rva
        w32(opt + 100, 0x80)
        w32(opt + 104, 0x1000)              // imports rva
        w32(opt + 108, 0x140)

        let sec = opt + 224
        wstr(sec, ".rdata")
        w32(sec + 8, 0x200); w32(sec + 12, 0x1000)
        w32(sec + 16, 0x200); w32(sec + 20, 0x200)
        wstr(sec + 40, ".edata")
        w32(sec + 48, 0x200); w32(sec + 52, 0x2000)
        w32(sec + 56, 0x200); w32(sec + 60, 0x400)

        // imports (raw 0x200 = RVA 0x1000)
        w32(0x200, 0x1040); w32(0x20C, 0x1060); w32(0x210, 0x1050)
        w32(0x214, 0x1070); w32(0x220, 0x1088); w32(0x224, 0x1080)
        w32(0x240, 0x10A0); w32(0x244, 0x80000005)
        w32(0x250, 0x10A0); w32(0x254, 0x80000005)
        wstr(0x260, "KERNEL32.dll")
        w32(0x270, 0x10C0)
        w32(0x280, 0x10C0)
        wstr(0x288, "USER32.dll")
        w16(0x2A0, 7); wstr(0x2A2, "GetTickCount64")
        w16(0x2C0, 3); wstr(0x2C2, "MessageBoxA")

        // exports (raw 0x400 = RVA 0x2000)
        w32(0x40C, 0x2040)
        w32(0x410, 1)                       // ordinal base
        w32(0x414, 2); w32(0x418, 2)
        w32(0x41C, 0x2050); w32(0x420, 0x2058); w32(0x424, 0x2060)
        wstr(0x440, "game.dll")
        w32(0x450, 0x2070); w32(0x454, 0x2080)
        w32(0x458, 0x2090); w32(0x45C, 0x2098)
        w16(0x460, 0); w16(0x462, 1)
        buf[0x470] = 0xC3; buf[0x480] = 0xC3
        wstr(0x490, "Alpha"); wstr(0x498, "Beta")
        return Data(buf)
    }
}

/// Ampliação do self-test: PE loader, GameProfile, importação, ExecutionBackend,
/// dispatch Win32, gráficos (superfície), áudio, input e shutdown.
/// Os testes existentes permanecem intactos.
final class CompatLayerTests: XCTestCase {

    // MARK: - PE loader

    func testPELoaderInspectAndMap() throws {
        let data = TestPEBuilder.build()

        let report = try PELoader.inspect(data)
        XCTAssertEqual(report.image.arch, "x86")
        XCTAssertFalse(report.image.isPE32Plus)
        XCTAssertFalse(report.image.isDLL)
        XCTAssertEqual(report.image.subsystem, 3)   // console
        XCTAssertEqual(report.image.entryPointRVA, 0x1000)
        XCTAssertEqual(report.image.sectionCount, 2)

        // imports por função
        XCTAssertEqual(report.imports.count, 2)
        XCTAssertEqual(report.imports[0].dll, "KERNEL32.dll")
        XCTAssertEqual(report.imports[0].functions.count, 2)
        XCTAssertEqual(report.imports[0].functions[0].name, "GetTickCount64")
        XCTAssertFalse(report.imports[0].functions[0].byOrdinal)
        XCTAssertTrue(report.imports[0].functions[1].byOrdinal)
        XCTAssertEqual(report.imports[0].functions[1].ordinal, 5)
        XCTAssertEqual(report.imports[1].dll, "USER32.dll")
        XCTAssertEqual(report.imports[1].functions[0].name, "MessageBoxA")
        XCTAssertEqual(report.totalImportFunctions, 3)

        // exports
        XCTAssertEqual(report.exports.count, 2)
        XCTAssertEqual(report.exports[0].name, "Alpha")
        XCTAssertEqual(report.exports[0].rva, 0x2070)
        XCTAssertEqual(report.exports[1].name, "Beta")

        // diagnóstico legível
        XCTAssertTrue(report.diagnostics.contains("imagem PE VÁLIDA"))
        XCTAssertTrue(report.diagnostics.contains("KERNEL32.dll"))
        XCTAssertTrue(report.diagnostics.contains("Alpha"))
        XCTAssertTrue(report.diagnostics.contains("nenhum código é executado"))

        // carga/mapeamento (sem execução)
        let img = try PELoader.loadImage(data, moduleName: "game.exe")
        XCTAssertEqual(img.imageSize, 0x3000)
        XCTAssertEqual(img.moduleName, "game.exe")
        XCTAssertNotNil(img.entryPointer)
        XCTAssertEqual(img.string(atRVA: 0x1060), "KERNEL32.dll")
        XCTAssertEqual(img.string(atRVA: 0x2090), "Alpha")
        XCTAssertNil(img.string(atRVA: 0x100000))
        XCTAssertNil(img.pointer(atRVA: 0x2FFF, length: 4))
        XCTAssertNotNil(img.pointer(atRVA: 0x2FFC, length: 4))

        // PE inválido é recusado
        XCTAssertThrowsError(try PELoader.inspect(Data([1, 2, 3, 4]))) { err in
            guard case PEInspectError.notAPE = err else {
                return XCTFail("esperava .notAPE, veio \(err)")
            }
        }
    }

    // MARK: - GameProfile

    func testGameProfileRoundTripPE() throws {
        var p = GameProfile(nome: "Meu Jogo", caminho: "Games/game-1.meu-jogo",
                            executavel: "bin/game.exe", arquiteturaExe: "x86",
                            tipo: .windowsPE, tamanhoInstalacao: 1024)
        p.argumentos = "-window"
        p.resolucao = Resolution(width: 800, height: 600)
        p.ambiente = EnvironmentRef(id: UUID(), name: "padrão")
        let json = try JSONEncoder().encode(p)
        let back = try JSONDecoder().decode(GameProfile.self, from: json)
        XCTAssertEqual(back, p)
        XCTAssertEqual(back.executavel, "bin/game.exe")
        XCTAssertEqual(back.arquiteturaExe, "x86")
        XCTAssertEqual(back.tipo, .windowsPE)
    }

    // MARK: - Game import

    func testGameImportChooseMainExecutable() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("import")
        defer { try? FileManager.default.removeItem(at: root) }
        let log = CoreTestSupport.makeLog()
        let lib = LibraryStore(sandbox: sb, log: log)
        try lib.load()
        let envs = EnvironmentManager(sandbox: sb, log: log)
        try envs.load()
        let service = ImportService(sandbox: sb, library: lib, environments: envs, log: log)

        // área de stage com PE + PXP + arquivo de dados
        let staging = sb.importsDir.appendingPathComponent("stage-test", isDirectory: true)
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: true)
        try TestPEBuilder.build().write(to: staging.appendingPathComponent("game.exe"))
        try Data("PXP0xxxx".utf8).write(to: staging.appendingPathComponent("tool.pxp"))
        try Data("dados".utf8).write(to: staging.appendingPathComponent("leia-me.txt"))

        let scan = try service.analyze(stagingDir: staging)
        XCTAssertEqual(scan.allFiles.count, 3)
        XCTAssertEqual(scan.executables.count, 2)   // PE + PXP

        // o usuário escolhe o PE principal
        let chosen = try XCTUnwrap(scan.executables.first(where: { $0.kind == .windowsPE }))
        XCTAssertEqual(chosen.pe?.arch, "x86")
        XCTAssertEqual(chosen.displayName, "game.exe")

        // sugerido existe (PXP tem score maior por padrão — decisão explícita do usuário vale)
        XCTAssertNotNil(scan.suggestedMain)

        let profile = try service.finalize(scan: scan, mainExecutable: chosen,
                                           name: "Meu Jogo")
        XCTAssertEqual(profile.executavel, "game.exe")
        XCTAssertEqual(profile.arquiteturaExe, "x86")
        XCTAssertEqual(profile.tipo, .windowsPE)
        XCTAssertEqual(lib.games.count, 1)
        XCTAssertEqual(lib.games[0].nome, "Meu Jogo")

        // conteúdo copiado para o sandbox
        let dest = try sb.resolveInside(profile.caminho)
        XCTAssertTrue(FileManager.default.fileExists(
            atPath: dest.appendingPathComponent("game.exe").path))
        XCTAssertTrue(FileManager.default.fileExists(
            atPath: dest.appendingPathComponent("leia-me.txt").path))
    }

    // MARK: - ExecutionBackend (ciclo de vida)

    func testExecutionBackendLifecyclePXP() throws {
        var payload: UnsafeMutableRawPointer?
        var plen = 0
        XCTAssertEqual(pr_selftest_payload_build(&payload, &plen), PR_OK)
        let data = Data(bytes: payload!, count: plen)
        free(payload)

        let (sb, root) = try CoreTestSupport.makeSandbox("lifecycle")
        defer { try? FileManager.default.removeItem(at: root) }
        let exeURL = sb.tempDir.appendingPathComponent("selftest.pxp")
        try data.write(to: exeURL)

        var profile = GameProfile(nome: "S", caminho: "Temp", executavel: "selftest.pxp",
                                  fps: .cap(60), tipo: .selfTest)
        profile.opcoes = AdvancedOptions()
        let config = EffectiveConfig(resolution: Resolution(width: 640, height: 360),
                                     fps: .cap(60), renderer: .metal,
                                     audioEnabled: true, gameVolume: 1, masterVolume: 1,
                                     quality: .high, showTouchControls: true,
                                     physicalControllersEnabled: true,
                                     environmentVariables: [:], debugLogging: false,
                                     maxInstructionsPerFrame: 2_000_000)
        let ctx = RuntimeSessionContext(profile: profile, config: config,
                                        executableURL: exeURL,
                                        environmentVariables: [:],
                                        environmentName: "padrão")

        let backend = PXPInterpreterBackend()
        XCTAssertEqual(backend.phase, .idle)

        try backend.load(executable: exeURL, image: .pxpNative)
        XCTAssertEqual(backend.phase, .loaded)
        try backend.initialize(context: ctx)
        XCTAssertEqual(backend.phase, .initialized)
        try backend.run()
        XCTAssertEqual(backend.phase, .running)

        backend.pause()
        XCTAssertEqual(backend.phase, .paused)
        try backend.resume()
        XCTAssertEqual(backend.phase, .running)

        // um frame real roda (não precisa terminar — E2E cobre o fluxo completo)
        let result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 16)
        XCTAssertEqual(result.state, .running)

        backend.stop()
        XCTAssertEqual(backend.phase, .stopped)
        backend.shutdown()
        XCTAssertEqual(backend.phase, .idle)

        // carga sem assinatura PXP é recusada
        try Data("ZZZZ".utf8).write(to: exeURL)
        XCTAssertThrowsError(try backend.load(executable: exeURL, image: .pxpNative)) { err in
            guard case RuntimeFailure.payloadInvalid = err else {
                return XCTFail("esperava .payloadInvalid, veio \(err)")
            }
        }
    }

    func testExecutionBackendLoadPEAndHonestRefusal() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("perefuse")
        defer { try? FileManager.default.removeItem(at: root) }
        let exeURL = sb.tempDir.appendingPathComponent("game.exe")
        try TestPEBuilder.build().write(to: exeURL)

        let backend = WindowsPEBackend()

        // LOAD é real: mapeia a imagem e produz o relatório
        try backend.load(executable: exeURL, image: .windowsPE(PEImage.mockForRefusal("x86")))
        XCTAssertEqual(backend.phase, .loaded)
        let img = try XCTUnwrap(backend.loadedImage)
        XCTAssertEqual(img.imageSize, 0x3000)
        XCTAssertEqual(img.report.imports.count, 2)

        // initialize recusa com motivo preciso (sem fingir execução)
        let profile = GameProfile(nome: "W", caminho: "Games/w", executavel: "game.exe",
                                  arquiteturaExe: "x86", tipo: .windowsPE)
        let config = EffectiveConfig(resolution: Resolution(width: 640, height: 360),
                                     fps: .cap(60), renderer: .metal,
                                     audioEnabled: true, gameVolume: 1, masterVolume: 1,
                                     quality: .high, showTouchControls: true,
                                     physicalControllersEnabled: true,
                                     environmentVariables: [:], debugLogging: false,
                                     maxInstructionsPerFrame: 10000)
        let ctx = RuntimeSessionContext(profile: profile, config: config,
                                        executableURL: exeURL,
                                        environmentVariables: [:], environmentName: "padrão")
        XCTAssertThrowsError(try backend.initialize(context: ctx)) { err in
            guard case RuntimeFailure.unsupported(let reason) = err else {
                return XCTFail("esperava .unsupported, veio \(err)")
            }
            XCTAssertTrue(reason.contains("Win32"))
            XCTAssertTrue(reason.contains("NÃO está integrada"))
            XCTAssertTrue(reason.contains("resolvida"))
        }
        let coverage = try XCTUnwrap(backend.lastCoverage)
        XCTAssertTrue(coverage.resolved.contains("KERNEL32.dll!GetTickCount64"))
        // MessageBoxA está IMPLEMENTADA neste build (sem UI) — cobertura honesta
        XCTAssertTrue(coverage.resolved.contains("USER32.dll!MessageBoxA"))
        XCTAssertTrue(coverage.unresolved.contains("KERNEL32.dll!#5")) // ordinal

        XCTAssertThrowsError(try backend.run())
        XCTAssertThrowsError(try backend.resume())
        backend.shutdown()
        XCTAssertEqual(backend.phase, .idle)
        XCTAssertNil(backend.loadedImage)
    }

    // MARK: - Win32 API dispatch

    func testWin32DispatchReal() throws {
        let env = Win32Environment()
        XCTAssertTrue(env.isValid)

        // relógio real
        let t1 = env.call("kernel32.dll", "GetTickCount64")
        XCTAssertEqual(t1.status, PR_OK)
        let t2 = env.call("kernel32", "GetTickCount64")
        XCTAssertEqual(t2.status, PR_OK)
        XCTAssertGreaterThanOrEqual(t2.ret, t1.ret)
        XCTAssertEqual(env.implementedCalls, 2)

        // last error real
        _ = env.call("kernel32.dll", "SetLastError", args: [777])
        XCTAssertEqual(env.call("kernel32.dll", "GetLastError").ret, 777)
        XCTAssertEqual(env.lastError, 777)

        // heap real via memória do convidado
        let heap = env.call("kernel32.dll", "GetProcessHeap").ret
        XCTAssertNotEqual(heap, 0)
        let alloc = env.call("kernel32.dll", "HeapAlloc", args: [heap, 0, 32])
        XCTAssertEqual(alloc.status, PR_OK)
        XCTAssertNotEqual(alloc.ret, 0)
        XCTAssertEqual(env.call("kernel32.dll", "HeapSize", args: [heap, 0, alloc.ret]).ret, 32)
        XCTAssertEqual(env.call("kernel32.dll", "HeapFree", args: [heap, 0, alloc.ret]).ret, 1)

        // string real no scratch
        let scratch = env.scratch()!
        for (i, b) in "abc".utf8.enumerated() { scratch[32 + i] = b }
        scratch[35] = 0
        XCTAssertEqual(env.call("kernel32.dll", "lstrlenA",
                                args: [UInt64(UInt(bitPattern: scratch + 32))]).ret, 3)

        // console real
        let hout = env.call("kernel32.dll", "GetStdHandle",
                            args: [UInt64(bitPattern: Int64(-11))]).ret
        XCTAssertNotEqual(hout, 0)
        for (i, b) in "oi".utf8.enumerated() { scratch[64 + i] = b }
        scratch[66] = 0
        let w = env.call("kernel32.dll", "WriteFile",
                         args: [hout, UInt64(UInt(bitPattern: scratch + 64)), 2,
                                UInt64(UInt(bitPattern: scratch + 128)), 1])
        XCTAssertEqual(w.status, PR_OK)
        XCTAssertEqual(w.ret, 1)
        XCTAssertEqual(env.stdoutRead(), "oi")

        // ExitProcess real
        _ = env.call("kernel32.dll", "ExitProcess", args: [7])
        XCTAssertTrue(env.isHalted)
        XCTAssertEqual(env.exitCode, 7)

        // MessageBoxA implementada DEGRADADA (sem UI): registra e retorna IDOK
        let mb = env.call("user32.dll", "MessageBoxA", args: [0, 0, 0, 0])
        XCTAssertEqual(mb.status, PR_OK)
        XCTAssertEqual(mb.ret, 1) // IDOK — documentado; não finge janela
        // realmente não suportada: falha honesta + contador
        let before = env.unsupportedCalls
        let sock = env.call("ws2_32.dll", "socket")
        XCTAssertEqual(sock.status, PR_ERR_UNSUPPORTED)
        XCTAssertEqual(sock.ret, 0)
        XCTAssertEqual(env.unsupportedCalls, before + 1)

        // desconhecida: PR_ERR_RANGE
        XCTAssertEqual(env.call("foo32.dll", "Bar").status, PR_ERR_RANGE)

        // catálogo por subsistema
        let mods = Win32Catalog.modules
        XCTAssertEqual(mods.count, 7)
        let kernel = mods.first(where: { $0.name == "kernel32.dll" })!
        XCTAssertGreaterThanOrEqual(kernel.implemented, 20)
        if case .partial = kernel.level {} else {
            XCTFail("kernel32 deve ser .partial")
        }
        let user = mods.first(where: { $0.name == "user32.dll" })!
        // MessageBoxA/W + janelas/mensagens/GDI do GRUPO 6 (contagem real)
        XCTAssertEqual(user.implemented, 33)  // G6: 20 + G7: 12 (entrada/timers) + G76: GetSystemMetrics
        XCTAssertTrue(Win32Catalog.lookup("KERNEL32", "heapalloc"))
        XCTAssertTrue(Win32Catalog.lookup("user32.dll", "MessageBoxA"))
        XCTAssertTrue(Win32Catalog.lookup("user32.dll", "PeekMessageA")) // GRUPO 6
        XCTAssertTrue(Win32Catalog.lookup("user32.dll", "GetSystemMetrics")) // G76
    }

    // MARK: - Graphics (superfície virtual + sincronização)

    func testGraphicsSurfacePath() throws {
        guard let surf = pr_surf_create(2, 2, PR_SURF_XRGB8888) else {
            return XCTFail("pr_surf_create")
        }
        defer { pr_surf_destroy(surf) }
        XCTAssertEqual(pr_surf_width(surf), 2)
        XCTAssertEqual(pr_surf_pitch(surf), 8)

        var color = pr_color(r: 1, g: 0, b: 0, a: 1)
        pr_surf_clear(surf, color)
        let px = pr_surf_pixels(surf)!
        XCTAssertEqual(px[0], 0xFF0000)

        color = pr_color(r: 0, g: 0, b: 1, a: 1)
        pr_surf_fill_rect(surf, pr_rect(x: 0, y: 0, w: 1, h: 1), color)
        XCTAssertEqual(px[0], 0x0000FF)
        XCTAssertEqual(px[1], 0xFF0000)

        guard let stream = pr_gfx_stream_create(8, 0) else {
            return XCTFail("pr_gfx_stream_create")
        }
        defer { pr_gfx_stream_destroy(stream) }
        XCTAssertEqual(pr_surf_present(surf, stream, 320, 180), PR_OK)

        // consumidor lê a textura do frame e alimenta o GfxSurfaceBuffer (Metal)
        var w: UInt32 = 0, h: UInt32 = 0
        let surfPx = pr_gfx_surface(stream, &w, &h)
        XCTAssertNotNil(surfPx)
        XCTAssertEqual(w, 2)
        let buffer = GfxSurfaceBuffer()
        XCTAssertTrue(buffer.update(width: w, height: h, copyFrom: surfPx))
        XCTAssertEqual(buffer.pixels[0], 0x0000FF)
        XCTAssertEqual(buffer.pixels[1], 0xFF0000)

        var frame = GfxFrame()
        frame.surface = buffer
        XCTAssertNotNil(frame.surface)

        // PRESENT presente no stream
        var cmd = pr_gfx_cmd()
        XCTAssertEqual(pr_gfx_pull(stream, &cmd, 1), 1)
        XCTAssertEqual(cmd.op, UInt32(PR_GFX_PRESENT.rawValue))

        // fence
        let fence = pr_sync_fence_create()!
        defer { pr_sync_fence_destroy(fence) }
        XCTAssertEqual(pr_sync_fence_reached(fence, 1), 0)
        pr_sync_fence_signal(fence)
        XCTAssertEqual(pr_sync_fence_reached(fence, 1), 1)
        XCTAssertEqual(pr_sync_fence_value(fence), 1)
    }

    // MARK: - Áudio

    func testAudioRing() throws {
        guard let ring = pr_audio_ring_create(64) else {
            return XCTFail("pr_audio_ring_create")
        }
        defer { pr_audio_ring_destroy(ring) }
        var pcm: [Float] = [0.5, -0.5, 0.25, -0.25]
        let pushed = pcm.withUnsafeBufferPointer { p -> Int in
            pr_audio_push(ring, p.baseAddress, 2)   // 2 frames estéreo
        }
        XCTAssertEqual(pushed, 2)
        var out = [Float](repeating: 0, count: 4)
        let got = out.withUnsafeMutableBufferPointer { p -> Int in
            pr_audio_pull(ring, p.baseAddress, 2)
        }
        XCTAssertEqual(got, 2)
        XCTAssertEqual(out[0], 0.5, accuracy: 0.0001)
        XCTAssertEqual(out[3], -0.25, accuracy: 0.0001)
    }

    // MARK: - Input

    func testInputRouter() throws {
        let router = InputRouter()
        XCTAssertEqual(router.state.buttons, 0)
        router.setPadButton(.a, pressed: true)
        router.setPadButton(.start, pressed: true)
        XCTAssertEqual(router.state.buttons, InputButton.a.rawValue | InputButton.start.rawValue)
        router.setPadButton(.a, pressed: false)
        XCTAssertEqual(router.state.buttons, InputButton.start.rawValue)
        router.setPadMove(x: 0.5, y: -0.25)
        XCTAssertEqual(router.state.moveX, 0.5, accuracy: 0.0001)
        XCTAssertEqual(router.state.moveY, -0.25, accuracy: 0.0001)
        router.reset()
        XCTAssertEqual(router.state.buttons, 0)
    }

    // MARK: - Shutdown

    func testShutdownCleansEverything() throws {
        // tudo sobe e desce sem vazar estado
        let env = Win32Environment()
        let backend = PXPInterpreterBackend()
        XCTAssertEqual(backend.phase, .idle)
        backend.shutdown()
        XCTAssertEqual(backend.phase, .idle)

        let pe = WindowsPEBackend()
        let (sb, root) = try CoreTestSupport.makeSandbox("shutdown")
        defer { try? FileManager.default.removeItem(at: root) }
        let exeURL = sb.tempDir.appendingPathComponent("g.exe")
        try TestPEBuilder.build().write(to: exeURL)
        try pe.load(executable: exeURL, image: .windowsPE(PEImage.mockForRefusal("x86")))
        XCTAssertEqual(pe.phase, .loaded)
        pe.stop()
        XCTAssertEqual(pe.phase, .stopped)
        pe.shutdown()
        XCTAssertEqual(pe.phase, .idle)
        XCTAssertNil(pe.loadedImage)

        // diagnóstico dos 9 subsistemas sempre responde
        let items = DiagnosticsReport.subsystemItems(storageNote: "teste")
        XCTAssertEqual(items.count, 9)
        let titles = items.map(\.title)
        XCTAssertTrue(titles.contains("PE loader"))
        XCTAssertTrue(titles.contains("Win32 compatibility"))
        XCTAssertTrue(titles.contains("JIT status"))
        for item in items {
            XCTAssertFalse(item.explanation.isEmpty, item.title)
        }
        _ = env.isHalted
    }
}
