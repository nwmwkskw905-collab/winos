import XCTest
@testable import PorticoCore

final class StoreTests: XCTestCase {
    func testLibraryCRUDAndPersistence() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("lib")
        defer { try? FileManager.default.removeItem(at: root) }
        let log = CoreTestSupport.makeLog()
        let lib = LibraryStore(sandbox: sb, log: log)
        try lib.load()
        XCTAssertTrue(lib.games.isEmpty)

        let p = GameProfile(nome: "Alpha", caminho: "Games/game-1.alpha",
                            executavel: "a.exe", tipo: .windowsPE)
        try lib.add(p)
        XCTAssertEqual(lib.games.count, 1)

        // duplicata por nome
        XCTAssertThrowsError(try lib.add(p)) { err in
            XCTAssertEqual(err as? LibraryError, .duplicateName("Alpha"))
        }

        // atualiza
        var p2 = lib.games[0]
        p2.notas = "nota"
        try lib.update(p2)
        XCTAssertEqual(lib.game(id: p.id)?.notas, "nota")

        // persistência: novo store lê o mesmo conteúdo
        let lib2 = LibraryStore(sandbox: sb, log: log)
        try lib2.load()
        XCTAssertEqual(lib2.games.count, 1)
        XCTAssertEqual(lib2.games[0].nome, "Alpha")

        // remoção
        try lib2.remove(id: p.id)
        XCTAssertTrue(lib2.games.isEmpty)
    }

    func testConfigurationMerge() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("cfg")
        defer { try? FileManager.default.removeItem(at: root) }
        let log = CoreTestSupport.makeLog()
        let cfg = ConfigurationManager(sandbox: sb, log: log)
        try cfg.load()
        XCTAssertEqual(cfg.global.qualidade, .high)

        try cfg.update { $0.qualidade = .low; $0.masterVolume = 0.5 }
        XCTAssertEqual(cfg.global.qualidade, .low)

        var p = GameProfile(nome: "X", caminho: "Games/x", executavel: "x.pxp",
                            resolucao: Resolution(width: 640, height: 360),
                            fps: .cap(25),
                            audio: AudioProfile(enabled: false, volume: 0.9),
                            tipo: .pxpNative)
        p.opcoes.environmentVariables = ["A": "1"]
        let eff = cfg.effective(for: p)
        XCTAssertEqual(eff.resolution.width, 640)
        XCTAssertEqual(cfg.effectiveFPS(for: p), 25)
        XCTAssertFalse(eff.audioEnabled)
        XCTAssertEqual(eff.environmentVariables["A"], "1")
        XCTAssertEqual(eff.masterVolume, 0.5)

        // fps ilimitado cai no sugerido pela qualidade
        p.fps = .unlimited
        XCTAssertEqual(cfg.effectiveFPS(for: p), QualityChoice.low.suggestedFPS)
    }

    func testSandboxPathSafety() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("safe")
        defer { try? FileManager.default.removeItem(at: root) }

        XCTAssertThrowsError(try sb.resolveInside("../etc/passwd"))
        XCTAssertThrowsError(try sb.resolveInside("Games/../../escape"))
        XCTAssertNoThrow(try sb.resolveInside("Games/ok"))
        XCTAssertFalse(sb.isInsideSandbox(root.deletingLastPathComponent()))
    }

    func testEnvironmentManager() throws {
        let (sb, root) = try CoreTestSupport.makeSandbox("env")
        defer { try? FileManager.default.removeItem(at: root) }
        let log = CoreTestSupport.makeLog()
        let envs = EnvironmentManager(sandbox: sb, log: log)
        try envs.load()

        let def = try envs.ensureDefault()
        XCTAssertEqual(def.id, EnvironmentRef.defaultEnv.id)
        XCTAssertEqual(def.state, .ready)

        // estrutura real do prefixo
        let dir = try sb.resolveInside(def.caminho)
        let driveC = dir.appendingPathComponent("drive_c")
        var isDir: ObjCBool = false
        XCTAssertTrue(FileManager.default.fileExists(atPath: driveC.path, isDirectory: &isDir))
        XCTAssertTrue(isDir.boolValue)

        let custom = try envs.create(name: "wine-like")
        XCTAssertEqual(envs.environments.count, 2)
        let merged = envs.mergedVariables(environmentID: custom.id,
                                          gameOverrides: ["X": "9"])
        XCTAssertEqual(merged["PORTICO_ENV"], "1")
        XCTAssertEqual(merged["X"], "9")

        envs.markInUse(id: custom.id, inUse: true)
        XCTAssertEqual(envs.environment(id: custom.id)?.state, .inUse)

        try envs.destroy(id: custom.id)
        XCTAssertNil(envs.environment(id: custom.id))

        // reload persiste o padrão
        let envs2 = EnvironmentManager(sandbox: sb, log: log)
        try envs2.load()
        XCTAssertNotNil(envs2.environment(id: EnvironmentRef.defaultEnv.id))
    }
}
