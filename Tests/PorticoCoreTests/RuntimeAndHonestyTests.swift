import XCTest
@testable import PorticoCore
import PorticoRuntime

final class RuntimeAndHonestyTests: XCTestCase {
    /// HONESTIDADE: o que não executa de verdade é recusado com o motivo.
    /// Arquiteturas sem backend (ex.: ARM) seguem recusadas — x64 agora roda o
    /// subconjunto mínimo (ver WindowsExecutionTests).
    func testWindowsBackendRefusesHonestly() throws {
        let log = CoreTestSupport.makeLog()
        let reg = BackendRegistry.standard(log: log)
        let peBackend = reg.backend(id: "windows-pe")!
        let image = SoftwareImage.windowsPE(PEImage.mockForRefusal("arm"))
        let verdict = peBackend.canExecute(image)
        XCTAssertFalse(verdict.canRun)
        XCTAssertTrue(verdict.reason.contains("Win32"))
        XCTAssertTrue(verdict.reason.contains("NÃO está integrada"))

        // start lança unsupported com a mensagem completa
        let (sb, root) = try CoreTestSupport.makeSandbox("honest")
        defer { try? FileManager.default.removeItem(at: root) }
        let ctx = RuntimeSessionContext(
            profile: GameProfile(nome: "W", caminho: "Games/w", executavel: "w.exe",
                                 arquiteturaExe: "arm", tipo: .windowsPE),
            config: EffectiveConfig(resolution: Resolution(width: 640, height: 360),
                                    fps: .cap(60), renderer: .metal,
                                    audioEnabled: true, gameVolume: 1, masterVolume: 1,
                                    quality: .high, showTouchControls: true,
                                    physicalControllersEnabled: true,
                                    environmentVariables: [:], debugLogging: false,
                                    maxInstructionsPerFrame: 10000),
            executableURL: sb.root,
            environmentVariables: [:], environmentName: "padrão")
        XCTAssertThrowsError(try peBackend.start(context: ctx)) { err in
            guard case RuntimeFailure.unsupported = err else {
                return XCTFail("deve falhar com .unsupported, veio \(err)")
            }
        }
    }

    /// Capacidades: nunca prometer JIT no iOS; relatório coerente com a sonda.
    func testCapabilityReportCoherence() {
        let report = DiagnosticsReport.capabilities()
        // execução Windows PE é REAL e PARCIAL neste build (PE32 + subset Win32)
        XCTAssertEqual(report.status(for: .windowsPEExecution).isUsable, true)
        XCTAssertEqual(report.status(for: .pxpNativeExecution), .supported)
        if !report.jitAvailable {
            if case .notSupported = report.status(for: .jitTranslation) {
                // coerente
            } else {
                XCTFail("sem JIT, o recurso não pode estar suportado")
            }
        }
        XCTAssertTrue(report.plainText().contains("SUPPORTED"))
    }

    /// Tradução OpenGL: tabela de mapeamento real e tracker funcional.
    func testOpenGLTranslationMap() {
        XCTAssertEqual(OpenGLTranslationMap.map(token: 0x1800), .clear)
        XCTAssertEqual(OpenGLTranslationMap.map(token: 0x0BA2), .viewport)
        if case .unsupported = OpenGLTranslationMap.map(token: 0xDEAD) {
            // honesto
        } else {
            XCTFail("token desconhecido deve ser unsupported")
        }

        let tracker = TranslationStateTracker(apiName: "OpenGL ES-subset")
        let ops = OpenGLTranslationMap.translate(tokens: [0x1801, 0x0BA2, 0x1803], into: tracker)
        XCTAssertEqual(ops, [.clear, .viewport, .endPrimitive])
        XCTAssertEqual(tracker.pendingCommands.count, 3)
        XCTAssertTrue(OpenGLTranslationMap.GLToken.allCases.count >= 10)
    }

    /// ProcessManager: sem fork/exec (limitação iOS) mas com ciclo completo.
    func testProcessManagerLifecycle() {
        let log = CoreTestSupport.makeLog()
        let pm = ProcessManager(log: log)
        var stopped = false
        let p = pm.spawn(label: "demo") { stopped = true }
        p.markRunning()
        XCTAssertTrue(p.isAlive)
        p.terminate()
        XCTAssertTrue(stopped)
        XCTAssertEqual(p.state, .terminated)
        pm.reap()
        XCTAssertTrue(pm.processes.isEmpty)
    }

    /// Pipeline completo via Swift: Self-Test PXP → comandos → áudio → logs → halt.
    func testPXPPipelineEndToEnd() throws {
        let log = CoreTestSupport.makeLog()
        let reg = BackendRegistry.standard(log: log)
        let backend = reg.backend(id: "pxp-interpreter")!

        // grava o payload do Self-Test (construído pelo runtime C)
        var payload: UnsafeMutableRawPointer?
        var plen = 0
        let st = pr_selftest_payload_build(&payload, &plen)
        XCTAssertEqual(st, PR_OK)
        let data = Data(bytes: payload!, count: plen)
        free(payload)

        let (sb, root) = try CoreTestSupport.makeSandbox("pxp")
        defer { try? FileManager.default.removeItem(at: root) }
        let exeURL = sb.tempDir.appendingPathComponent("selftest.pxp")
        try data.write(to: exeURL)

        var profile = GameProfile(nome: "Self-Test", caminho: "Temp",
                                  executavel: "selftest.pxp",
                                  fps: .cap(60),
                                  tipo: .selfTest)
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

        let verdict = backend.canExecute(.pxpNative)
        XCTAssertTrue(verdict.canRun)

        try backend.start(context: ctx)

        var input = InputState()
        var sawClear = false, sawDraw = false, sawPresent = false
        for frame in 0..<4 {
            let result = backend.stepFrame(input: input, dt: 1.0 / 60.0,
                                           timeMs: Double(frame) * 16.7)
            XCTAssertEqual(result.state, .running)
            let gfx = backend.consumeGraphicsFrame()
            XCTAssertGreaterThanOrEqual(gfx.commands.count, 3)
            XCTAssertEqual(gfx.vertices.count, 6)
            for cmd in gfx.commands {
                switch cmd {
                case .clear: sawClear = true
                case .drawTriangles(let n, _): if n == 6 { sawDraw = true }
                case .present: sawPresent = true
                default: break
                }
            }
        }
        XCTAssertTrue(sawClear && sawDraw && sawPresent)

        // áudio do tom do payload
        let pcm = backend.pullAudio(maxFrames: 4096)
        XCTAssertFalse(pcm.isEmpty)

        // logs drenados
        backend.drainLogs(into: log)
        XCTAssertTrue(log.snapshot().contains(where: { $0.message.contains("iniciado") }))
        XCTAssertTrue(log.snapshot().contains(where: { $0.category == "pxp" }))

        // START encerra normalmente
        input.buttons = InputButton.start.rawValue
        let final = backend.stepFrame(input: input, dt: 1.0 / 60.0, timeMs: 200)
        XCTAssertTrue(final.halted)
        XCTAssertEqual(final.state, .stopped)
        backend.drainLogs(into: log)
        XCTAssertTrue(log.snapshot().contains(where: { $0.message.contains("encerrando") }))
        backend.stop()
    }

    /// RuntimeManager recusa PEs Windows em arquitetura sem backend (ex.: ARM)
    /// com erro compreensível e segue honesto.
    /// (PE32/x64-subconjunto são aceitos — ver WindowsExecutionTests.)
    func testRuntimeManagerRefusesWindowsPE() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("rt")
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

        let profile = GameProfile(nome: "WinGame", caminho: "Games/wg",
                                  executavel: "wg.exe",
                                  arquiteturaExe: "arm", tipo: .windowsPE)
        let eff = cfg.effective(for: profile)

        var failure: RuntimeFailure?
        rt.events.onFailure = { failure = $0 }
        XCTAssertThrowsError(try rt.start(profile: profile, config: eff,
                                          environments: envs, clockNow: 0))
        if case .unsupported(let reason) = failure {
            XCTAssertTrue(reason.contains("Win32"))
        } else {
            XCTFail("esperava .unsupported, veio \(String(describing: failure))")
        }
        XCTAssertEqual(rt.state, .failed)

        // mensagem ao usuário é compreensível
        let msg = RuntimeFailure.unsupported(reason: "teste").userMessage
        XCTAssertTrue(msg.contains("ainda não pode ser executado"))
    }
}
