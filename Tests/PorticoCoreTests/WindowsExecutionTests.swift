import XCTest
@testable import PorticoCore
import PorticoRuntime

/// Execução REAL de software Windows compatível (Fase 7):
/// GameLibrary → GameProfile → PE Loader → ExecutionBackend → CPU → Win32 →
/// processo → exit. Sem simulação: o que não funciona falha com diagnóstico.
final class WindowsExecutionTests: XCTestCase {

    // MARK: - Helpers

    private func helloData(variant: Int32) throws -> Data {
        var ptr: UnsafeMutableRawPointer?
        var len = 0
        let st = pr_winhello_build(variant, &ptr, &len)
        XCTAssertEqual(st, PR_OK)
        guard let ptr else {
            throw RuntimeFailure.backendUnavailable(reason: "pr_winhello_build")
        }
        let data = Data(bytes: ptr, count: len)
        free(ptr)
        return data
    }

    private func makeConfig(maxInsns: UInt32 = 10_000) -> EffectiveConfig {
        EffectiveConfig(resolution: Resolution(width: 640, height: 360),
                        fps: .cap(60), renderer: .metal,
                        audioEnabled: true, gameVolume: 1, masterVolume: 1,
                        quality: .high, showTouchControls: true,
                        physicalControllersEnabled: true,
                        environmentVariables: [:], debugLogging: false,
                        maxInstructionsPerFrame: maxInsns)
    }

    private func makeProfile(arch: String = "x86") -> GameProfile {
        GameProfile(nome: "WinHello", caminho: "Temp", executavel: "hello.exe",
                    arquiteturaExe: arch, tipo: .windowsPE)
    }

    private func writeExe(_ data: Data, in sb: AppSandbox) throws -> URL {
        let url = sb.tempDir.appendingPathComponent("hello.exe")
        try data.write(to: url)
        return url
    }

    // MARK: - O marco: PE → CPU → memória → imports → Win32 → processo → exit
    // MARK: - PE32+ REAL de toolchain externo (MinGW-w64 GCC 14)

    func testRealMinGWPEExecutesToExitCode42() throws {
        // binário REAL (data/hello_real.exe): CRT do MinGW + main() retornando 42
        let here = URL(fileURLWithPath: #filePath)
        let peURL = here.deletingLastPathComponent().deletingLastPathComponent()
            .appendingPathComponent("PorticoRuntimeTests/data/hello_real.exe")
        let data = try Data(contentsOf: peURL)
        XCTAssertGreaterThan(data.count, 4096)

        let (sb, root) = try CoreTestSupport.makeSandbox("winrealpe")
        defer { try? FileManager.default.removeItem(at: root) }
        let exe = try writeExe(data, in: sb)

        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(),
                                        config: makeConfig(maxInsns: 200_000),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        try backend.start(context: ctx)

        // CRT real: varias "frames" ate exit(42) (ou diagnostico honesto)
        var result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        for _ in 0..<500 where !(result.halted || result.state == .stopped) {
            result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        }
        XCTAssertEqual(result.state, .stopped)
        XCTAssertTrue(result.halted)
        XCTAssertEqual(backend.exitCode, 42)
        XCTAssertTrue(result.message.contains("PROCESS EXIT"))

        backend.stop()
        backend.shutdown()
    }


    func testHelloPEExecutesToExitCode42() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("winhello")
        defer { try? FileManager.default.removeItem(at: root) }
        let log = CoreTestSupport.makeLog()
        let exe = try writeExe(try helloData(variant: 0), in: sb)

        let backend = WindowsPEBackend()
        let verdict = backend.canExecute(.windowsPE(PEImage.mockForRefusal("x86")))
        XCTAssertTrue(verdict.canRun)
        XCTAssertTrue(verdict.reason.contains("execução real"))

        let ctx = RuntimeSessionContext(profile: makeProfile(), config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        try backend.start(context: ctx)
        XCTAssertEqual(backend.phase, .running)

        // roda até ExitProcess(42) — dois calls stdcall + encerramento
        var result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        XCTAssertEqual(result.state, .stopped)
        XCTAssertTrue(result.halted)
        XCTAssertEqual(backend.exitCode, 42)
        XCTAssertTrue(result.message.contains("PROCESS EXIT"))
        XCTAssertTrue(backend.lastDiagnostic.contains("ExitProcess"))
        XCTAssertTrue(backend.lastDiagnostic.contains("42"))

        // logs do processo reais (Win32 dispatch registrado)
        backend.drainLogs(into: log)
        backend.stop()
        XCTAssertEqual(backend.phase, .stopped)
        backend.shutdown()
        XCTAssertEqual(backend.phase, .idle)
    }

    func testLifecyclePauseResumeStop() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("winlife")
        defer { try? FileManager.default.removeItem(at: root) }
        // variante GDI tem mais instruções; budget minúsculo mantém vivo p/ pausar
        let exe = try writeExe(try helloData(variant: 1), in: sb)
        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(),
                                        config: makeConfig(maxInsns: 1),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        try backend.start(context: ctx)
        XCTAssertEqual(backend.phase, .running)

        let r1 = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        XCTAssertEqual(r1.state, .running)   // 1 instrução/frame: ainda rodando

        backend.pause()
        XCTAssertEqual(backend.phase, .paused)
        let r2 = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 16)
        XCTAssertEqual(r2.state, .running)   // pausado não executa
        XCTAssertFalse(r2.halted)

        try backend.resume()
        XCTAssertEqual(backend.phase, .running)
        backend.stop()
        XCTAssertEqual(backend.phase, .stopped)
        backend.shutdown()
    }

    // MARK: - Diagnósticos EXECUTION STOPPED (nunca parar em silêncio)

    func testUnsupportedAPIProducesExecutionStopped() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("winapi")
        defer { try? FileManager.default.removeItem(at: root) }
        var data = try helloData(variant: 0)
        // troca "GetTickCount64" (14 chars) por espécime FORA de escopo (14):
        // "CreateHardLinkA" não está no catálogo nem no roadmap desta etapa
        // (FASE 6 implementou LoadLibraryA — intenção do teste preservada:
        // diagnosticar Unsupported Win32 API).
        let name = Array("CreateHardLinkA".utf8)
        for (i, b) in name.enumerated() { data[0x262 + i] = b }
        data[0x262 + name.count] = 0
        let exe = try writeExe(data, in: sb)

        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(), config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        XCTAssertThrowsError(try backend.start(context: ctx)) { err in
            guard case RuntimeFailure.unsupported(let reason) = err else {
                return XCTFail("esperava .unsupported, veio \(err)")
            }
            XCTAssertTrue(reason.contains("EXECUTION STOPPED"))
            XCTAssertTrue(reason.contains("Unsupported Win32 API"))
            XCTAssertTrue(reason.contains("KERNEL32.dll"))
            XCTAssertTrue(reason.contains("CreateHardLinkA"))
        }
        XCTAssertEqual(backend.phase, .failed)
    }

    func testMissingDLLProducesExecutionStopped() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("windll")
        defer { try? FileManager.default.removeItem(at: root) }
        var data = try helloData(variant: 0)
        let dll = Array("FOO32.dll".utf8)
        for (i, b) in dll.enumerated() { data[0x240 + i] = b }
        data[0x240 + dll.count] = 0
        let exe = try writeExe(data, in: sb)

        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(), config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        XCTAssertThrowsError(try backend.start(context: ctx)) { err in
            guard case RuntimeFailure.unsupported(let reason) = err else {
                return XCTFail("esperava .unsupported, veio \(err)")
            }
            XCTAssertTrue(reason.contains("EXECUTION STOPPED"))
            XCTAssertTrue(reason.contains("Missing DLL"))
            XCTAssertTrue(reason.contains("FOO32.dll"))
        }
    }

    func testInvalidMemoryProducesExecutionStopped() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("winmem")
        defer { try? FileManager.default.removeItem(at: root) }
        var data = try helloData(variant: 0)
        // código: mov [0x00900000], 1  →  C7 05 imm32 imm32 (fora da imagem)
        let code: [UInt8] = [0xC7, 0x05, 0x00, 0x00, 0x90, 0x00, 0x01, 0x00, 0x00, 0x00, 0xF4]
        for (i, b) in code.enumerated() { data[0x400 + i] = b }
        let exe = try writeExe(data, in: sb)

        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(), config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        try backend.start(context: ctx)
        let result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        XCTAssertEqual(result.state, .failed)
        XCTAssertTrue(result.halted)
        XCTAssertNotNil(result.failure)
        XCTAssertTrue(backend.lastDiagnostic.contains("Invalid memory access"))
        XCTAssertTrue(backend.lastDiagnostic.contains("0x00900000"))
    }

    func testInvalidExecutableFails() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("winbad")
        defer { try? FileManager.default.removeItem(at: root) }
        let exe = try writeExe(Data([0x4D, 0x5A, 0x00, 0x00]), in: sb)
        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(), config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        XCTAssertThrowsError(try backend.start(context: ctx)) { err in
            guard case RuntimeFailure.payloadInvalid(let reason) = err else {
                return XCTFail("esperava .payloadInvalid, veio \(err)")
            }
            XCTAssertTrue(reason.contains("Invalid executable")
                          || reason.contains("PE inválido"))
        }
    }

    func testUnsupportedArchRefusedHonestly() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("winarm")
        defer { try? FileManager.default.removeItem(at: root) }
        let exe = try writeExe(try helloData(variant: 0), in: sb)
        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(arch: "arm"),
                                        config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        XCTAssertThrowsError(try backend.start(context: ctx)) { err in
            guard case RuntimeFailure.unsupported(let reason) = err else {
                return XCTFail("esperava .unsupported, veio \(err)")
            }
            XCTAssertTrue(reason.contains("NÃO está integrada"))
            XCTAssertTrue(reason.contains("Win32"))
        }
    }

    /// Fronteira honesta do subconjunto x64: opcode fora → EXECUTION STOPPED
    /// com opcode/RIP/endereço (nunca fingir suporte a instruções ausentes).
    func testX64SubsetFaultIsDiagnosed() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("win64sub")
        defer { try? FileManager.default.removeItem(at: root) }
        var data = try helloData(variant: 2)   // PE32+ x64
        data[0x400] = 0x0F                     // RDMSR (0F 32): opcode fora do
        data[0x401] = 0x32                     // subconjunto (0F 55 passou a ser SSE)
        let exe = try writeExe(data, in: sb)
        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(arch: "x64"),
                                        config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        try backend.start(context: ctx)        // x64 do subconjunto: aceito
        let result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        XCTAssertEqual(result.state, .failed)
        XCTAssertTrue(result.halted)
        XCTAssertTrue(backend.lastDiagnostic.contains("fora do subconjunto"))
        XCTAssertTrue(backend.lastDiagnostic.contains("opcode=0x0F"))
        XCTAssertTrue(backend.lastDiagnostic.contains("rip=0x"))
        backend.shutdown()
    }

    /// MARCO VISUAL: PE visual x64 → CPU → Win32 → GDI → Bitmap → BitBlt →
    /// GfxFrame.surface (padrão exato) → estágio de submissão Metal → exit 0.
    func testVisualPEPaintsSurfaceThroughFrame() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("winvisual")
        defer { try? FileManager.default.removeItem(at: root) }
        let log = CoreTestSupport.makeLog()
        let exe = try writeExe(try helloData(variant: 6), in: sb)
        let backend = WindowsPEBackend()
        let verdict = backend.canExecute(.windowsPE(PEImage.mockForRefusal("x64")))
        XCTAssertTrue(verdict.canRun)
        XCTAssertTrue(verdict.reason.contains("subconjunto"))
        let ctx = RuntimeSessionContext(profile: makeProfile(arch: "x64"),
                                        config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        try backend.start(context: ctx)
        let result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        XCTAssertEqual(result.state, .stopped)
        XCTAssertTrue(result.halted)
        XCTAssertEqual(backend.exitCode, 0)

        // GfxFrame.surface recebeu o padrão (XRGB8888 0x00RRGGBB)
        let frame = backend.consumeGraphicsFrame()
        let surf = try XCTUnwrap(frame.surface)
        XCTAssertEqual(surf.width, 320)
        XCTAssertEqual(surf.height, 240)
        // índice = y * 320 + x (stride 320 pixels)
        XCTAssertEqual(surf.pixels[10 * 320 + 10], 0x00FFFFFF)    // fundo (10,10)
        XCTAssertEqual(surf.pixels[40 * 320 + 40], 0x00FF0000)    // vermelho (40,40)
        XCTAssertEqual(surf.pixels[119 * 320 + 159], 0x00FF0000)  // vermelho (159,119)
        XCTAssertEqual(surf.pixels[40 * 320 + 160], 0x00FFFFFF)   // fora (160,40)
        XCTAssertEqual(surf.pixels[120 * 320 + 160], 0x000000FF)  // azul (160,120)
        XCTAssertEqual(surf.pixels[179 * 320 + 259], 0x000000FF)  // azul (259,179)
        XCTAssertEqual(surf.pixels[120 * 320 + 260], 0x00FFFFFF)  // fora (260,120)
        XCTAssertTrue(frame.commands.contains { if case .present = $0 { return true }
                                                return false })

        // conversão de formato + estágio de submissão Metal (ponte pronta)
        let upload = MetalFrameUpload()
        XCTAssertTrue(upload.stage(surf))
        XCTAssertEqual(upload.width, 320)
        XCTAssertEqual(upload.height, 240)
        XCTAssertEqual(upload.byteCount, 320 * 240 * 4)
        upload.withBytes { (b: UnsafePointer<UInt8>) -> Void in
            // branco → (255,255,255,255); vermelho XRGB → B=0,G=0,R=255,A=255
            XCTAssertEqual(b[(10 * 320 + 10) * 4], 255)
            XCTAssertEqual(b[(10 * 320 + 10) * 4 + 3], 255)
            XCTAssertEqual(b[(40 * 320 + 40) * 4], 0)
            XCTAssertEqual(b[(40 * 320 + 40) * 4 + 1], 0)
            XCTAssertEqual(b[(40 * 320 + 40) * 4 + 2], 255)
            XCTAssertEqual(b[(40 * 320 + 40) * 4 + 3], 255)
            XCTAssertEqual(b[(120 * 320 + 160) * 4], 255)  // B do azul (160,120)
            XCTAssertEqual(b[(120 * 320 + 160) * 4 + 2], 0)
        }
        // reuso do buffer entre frames (sem churn de alocação)
        let cap = upload.byteCapacity
        XCTAssertTrue(upload.stage(surf))
        XCTAssertEqual(upload.byteCapacity, cap)

        // execution log das etapas do marco
        backend.drainLogs(into: log)
        let snap = log.snapshot().map { $0.message }.joined(separator: "\n")
        XCTAssertTrue(snap.contains("[PE] loaded"))
        XCTAssertTrue(snap.contains("[CPU] execution started"))
        XCTAssertTrue(snap.contains("[WIN32] API"))
        XCTAssertTrue(snap.contains("[GDI] bitmap created"))
        XCTAssertTrue(snap.contains("[GDI] BitBlt"))
        XCTAssertTrue(snap.contains("[GDI] surface updated"))
        XCTAssertTrue(snap.contains("[PROCESS] exit code"))
        backend.stop()
        backend.shutdown()
    }
    // MARK: - FASE 5: PEs de teste (memória / GDI+frame / APIs)

    func testPE7MemoryAllocatesWritesAndExitsClean() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("winpe7")
        defer { try? FileManager.default.removeItem(at: root) }
        let log = CoreTestSupport.makeLog()
        let exe = try writeExe(try helloData(variant: 7), in: sb)
        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(arch: "x64"),
                                        config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: ["PORTICO_SESSION": "1"],
                                        environmentName: "padrão")
        try backend.start(context: ctx)
        let result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        XCTAssertEqual(result.state, .stopped)
        XCTAssertTrue(result.halted)
        XCTAssertEqual(backend.exitCode, 0)   // 4 checagens internas de memória
        XCTAssertTrue(result.message.contains("PROCESS EXIT"))
        backend.drainLogs(into: log)
        let snap = log.snapshot().map { $0.message }.joined(separator: "\n")
        XCTAssertTrue(snap.contains("[MEM] stack mapped"))
        XCTAssertTrue(snap.contains("[MEM] image mapped"))
        XCTAssertTrue(snap.contains("[WIN32] API kernel32.dll!VirtualAlloc"))
        XCTAssertTrue(snap.contains("[WIN32] API kernel32.dll!HeapAlloc"))
        XCTAssertTrue(snap.contains("[PROCESS] exit code 0 after"))
        backend.stop()
        backend.shutdown()
    }

    func testPE10CRTRoutinesAndStringOps() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("winpe10")
        defer { try? FileManager.default.removeItem(at: root) }
        let log = CoreTestSupport.makeLog()
        let exe = try writeExe(try helloData(variant: 10), in: sb)
        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(arch: "x64"),
                                        config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        try backend.start(context: ctx)
        let result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        XCTAssertEqual(result.state, .stopped)
        XCTAssertTrue(result.halted)
        // main() retorna o nº de FALHAS das checagens internas do CRT
        // (memcpy/strlen/memcmp/memset/SSE/checksum/MOVD-MOVQ) → 0 = todas ok
        XCTAssertEqual(backend.exitCode, 0)
        XCTAssertTrue(result.message.contains("PROCESS EXIT"))
        backend.drainLogs(into: log)
        let snap = log.snapshot().map { $0.message }.joined(separator: "\n")
        XCTAssertTrue(snap.contains("[WIN32] API kernel32.dll!ExitProcess"))
        XCTAssertTrue(snap.contains("[PROCESS] exit code 0 after"))
        backend.stop()
        backend.shutdown()
    }

    func testPE11VirtualFileSystemWritesInsideSandboxRoot() throws {
        // FASE 7: filesystem virtual — CreateFileA/WriteFile/CloseHandle do
        // convidado escrevem DENTRO da raiz do sandbox (path Windows → Portico
        // → sandbox). Fugas ("..") são negadas em pr_win32 (testado em C).
        let (sb, root) = try CoreTestSupport.makeSandbox("winvfs")
        defer { try? FileManager.default.removeItem(at: root) }
        let exe = try writeExe(try helloData(variant: 13), in: sb)
        let profile = GameProfile(nome: "WinVFS", caminho: root.path,
                                  executavel: "hello.exe",
                                  arquiteturaExe: "x64", tipo: .windowsPE)
        let ctx = RuntimeSessionContext(profile: profile, config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        let backend = WindowsPEBackend()
        try backend.start(context: ctx)
        let result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        XCTAssertEqual(result.state, .stopped)
        XCTAssertEqual(backend.exitCode, 0)
        let nota = root.appendingPathComponent("nota.txt")
        let content = try Data(contentsOf: nota)
        XCTAssertEqual(String(data: content, encoding: .utf8), "Portico VFS E2E")
        backend.shutdown()
    }

    func testPE8GDIStretchBitBltProducesFrame() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("winpe8")
        defer { try? FileManager.default.removeItem(at: root) }
        let log = CoreTestSupport.makeLog()
        let exe = try writeExe(try helloData(variant: 8), in: sb)
        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(arch: "x64"),
                                        config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        try backend.start(context: ctx)
        let result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        XCTAssertEqual(result.state, .stopped)
        XCTAssertTrue(result.halted)
        XCTAssertEqual(backend.exitCode, 0)

        let frame = backend.consumeGraphicsFrame()
        let surf = try XCTUnwrap(frame.surface)
        XCTAssertEqual(surf.width, 320)
        XCTAssertEqual(surf.height, 240)
        // retângulos escalados 2x pelo StretchBlt (índice = y * 320 + x)
        XCTAssertEqual(surf.pixels[10 * 320 + 10], 0x00FFFFFF)    // fundo
        XCTAssertEqual(surf.pixels[40 * 320 + 40], 0x00FF0000)    // vermelho 2x
        XCTAssertEqual(surf.pixels[119 * 320 + 159], 0x00FF0000)
        XCTAssertEqual(surf.pixels[40 * 320 + 160], 0x00FFFFFF)   // fora
        XCTAssertEqual(surf.pixels[120 * 320 + 160], 0x000000FF)  // azul 2x
        XCTAssertEqual(surf.pixels[179 * 320 + 259], 0x000000FF)
        // recorte BitBlt (280,0,40,30) do bitmap 160x120
        XCTAssertEqual(surf.pixels[5 * 320 + 285], 0x00FFFFFF)
        XCTAssertEqual(surf.pixels[25 * 320 + 300], 0x00FF0000)
        // acentos SetPixel (verde COLORREF → XRGB 0x0000FF00)
        XCTAssertEqual(surf.pixels[5 * 320 + 5], 0x0000FF00)
        XCTAssertEqual(surf.pixels[234 * 320 + 314], 0x0000FF00)

        // estágio de submissão [METAL] testável (sem GPU)
        let upload = MetalFrameUpload()
        var staged: [(Int, Int)] = []
        upload.onStaged = { staged.append(($0, $1)) }
        XCTAssertTrue(upload.stage(surf))
        XCTAssertEqual(staged.count, 1)
        XCTAssertEqual(staged[0].0, 320)
        XCTAssertEqual(staged[0].1, 240)

        backend.drainLogs(into: log)
        let snap = log.snapshot().map { $0.message }.joined(separator: "\n")
        XCTAssertTrue(snap.contains("[GDI] StretchBlt"))
        XCTAssertTrue(snap.contains("[GDI] BitBlt"))
        XCTAssertTrue(snap.contains("[GDI] surface updated"))
        XCTAssertTrue(snap.contains("[PROCESS] exit code 0 after"))
        backend.stop()
        backend.shutdown()
    }

    func testPE9Kernel32GDIAndEnvironment() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("winpe9")
        defer { try? FileManager.default.removeItem(at: root) }
        let log = CoreTestSupport.makeLog()
        let exe = try writeExe(try helloData(variant: 9), in: sb)
        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(arch: "x64"),
                                        config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: ["PORTICO_TEST": "ok"],
                                        environmentName: "padrão")
        try backend.start(context: ctx)
        let result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        XCTAssertEqual(result.state, .stopped)
        XCTAssertTrue(result.halted)
        // 6 checagens: módulo/proc/linha de comando/ambiente/systeminfo/msgbox
        // (qualquer falha interna sairia com exit != 0 — sucesso honesto)
        XCTAssertEqual(backend.exitCode, 0)

        let frame = backend.consumeGraphicsFrame()
        let surf = try XCTUnwrap(frame.surface)
        XCTAssertEqual(surf.pixels[200 * 320 + 200], 0x00FFFFFF)  // PatBlt branco
        XCTAssertEqual(surf.pixels[10 * 320 + 10], 0x00FF0000)    // SetPixel vermelho

        backend.drainLogs(into: log)
        let snap = log.snapshot().map { $0.message }.joined(separator: "\n")
        XCTAssertTrue(snap.contains("[WIN32] API kernel32.dll!GetModuleHandleA"))
        XCTAssertTrue(snap.contains("[WIN32] API kernel32.dll!GetProcAddress"))
        XCTAssertTrue(snap.contains("[WIN32] API kernel32.dll!GetCommandLineA"))
        XCTAssertTrue(snap.contains("[WIN32] API kernel32.dll!GetEnvironmentVariableA"))
        XCTAssertTrue(snap.contains("[WIN32] API kernel32.dll!GetSystemInfo"))
        XCTAssertTrue(snap.contains("[WIN32] API user32.dll!MessageBoxA"))
        XCTAssertTrue(snap.contains("sem UI"))
        XCTAssertTrue(snap.contains("[PROCESS] exit code 0 after"))
        backend.stop()
        backend.shutdown()
    }

    // MARK: - Gráficos: GDI (Windows Graphics) → superfície → Metal

    func testGDISurfaceReachesGraphicsFrame() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("wingdi")
        defer { try? FileManager.default.removeItem(at: root) }
        let exe = try writeExe(try helloData(variant: 1), in: sb)
        let backend = WindowsPEBackend()
        let ctx = RuntimeSessionContext(profile: makeProfile(), config: makeConfig(),
                                        executableURL: exe,
                                        environmentVariables: [:],
                                        environmentName: "padrão")
        try backend.start(context: ctx)
        let result = backend.stepFrame(input: InputState(), dt: 1.0 / 60.0, timeMs: 0)
        XCTAssertEqual(result.state, .stopped)   // ExitProcess(0)

        let frame = backend.consumeGraphicsFrame()
        let surf = try XCTUnwrap(frame.surface)
        XCTAssertEqual(surf.width, 320)
        XCTAssertEqual(surf.height, 240)
        // retângulo GDI vermelho (COLORREF 0x000000FF → XRGB 0x00FF0000)
        XCTAssertEqual(surf.pixels[20 * 320 + 20], 0x00FF0000)
        XCTAssertEqual(surf.pixels[0], 0x00000000)
        XCTAssertTrue(frame.commands.contains { if case .present = $0 { return true }
                                                return false })
    }

    // MARK: - PLAY da biblioteca → ExecutionBackend real → Runtime → Game → Exit

    func testRuntimeManagerRunsWindowsPEGame() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("winplay")
        defer { try? FileManager.default.removeItem(at: root) }
        let log = CoreTestSupport.makeLog()
        let lib = LibraryStore(sandbox: sb, log: log)
        try lib.load()
        let envs = EnvironmentManager(sandbox: sb, log: log)
        try envs.load()
        let cfg = ConfigurationManager(sandbox: sb, log: log)
        try cfg.load()
        let reg = BackendRegistry.standard(log: log)
        let rt = RuntimeManager(sandbox: sb, log: log, backendRegistry: reg)

        try writeExe(try helloData(variant: 0), in: sb)
        let profile = makeProfile()
        let eff = cfg.effective(for: profile)

        var finished = false
        rt.events.onFinished = { finished = true }

        // GameProfile → ExecutionBackend → execução real
        try rt.start(profile: profile, config: eff, environments: envs, clockNow: 0)
        XCTAssertEqual(rt.state, .running)

        // PLAY: frames reais até ExitProcess
        var ticks = 0
        while rt.state == .running && ticks < 10 {
            _ = rt.tick(now: Double(ticks + 1) * 0.05, input: InputState())
            ticks += 1
        }
        XCTAssertTrue(finished, "o processo deve encerrar (ExitProcess) e disparar onFinished")
        XCTAssertEqual(rt.state, .stopped)
        rt.endGame(clockNow: 1.0)
    }
}
