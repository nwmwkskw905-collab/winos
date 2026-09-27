import XCTest
@testable import PorticoCore

final class ModelTests: XCTestCase {
    func testGameProfileCodableRoundTrip() throws {
        let profile = GameProfile(
            id: UUID(uuidString: "11111111-2222-3333-4444-555555555555")!,
            nome: "Demo",
            caminho: "Games/game-1.demo",
            executavel: "bin/demo.exe",
            argumentos: "-windowed",
            resolucao: Resolution(width: 1280, height: 720),
            fps: .cap(30),
            renderer: .metal,
            audio: AudioProfile(enabled: true, volume: 0.5),
            controles: ControlProfile(),
            ambiente: .defaultEnv,
            opcoes: AdvancedOptions(environmentVariables: ["FOO": "BAR"], debugLogging: true),
            dataCriacao: Date(timeIntervalSince1970: 1_700_000_000),
            coverSeed: 42,
            arquiteturaExe: "x86",
            tipo: .windowsPE
        )
        let enc = JSONEncoder()
        enc.dateEncodingStrategy = .iso8601
        let data = try enc.encode(profile)
        let dec = JSONDecoder()
        dec.dateDecodingStrategy = .iso8601
        let back = try dec.decode(GameProfile.self, from: data)
        XCTAssertEqual(back, profile)

        // chaves JSON em português conforme o modelo de dados do app
        let json = String(data: data, encoding: .utf8)!
        XCTAssertTrue(json.contains("\"nome\""))
        XCTAssertTrue(json.contains("\"caminho\""))
        XCTAssertTrue(json.contains("\"executavel\""))
        XCTAssertTrue(json.contains("\"resolucao\""))
    }

    func testResolutionClamps() {
        let r = Resolution(width: 5, height: 99999)
        XCTAssertEqual(r.width, 160)
        XCTAssertEqual(r.height, 4320)
    }

    func testFPSLimitCodable() throws {
        let enc = JSONEncoder()
        XCTAssertEqual(try String(data: enc.encode(FPSLimit.cap(60)), encoding: .utf8), "60")
        XCTAssertEqual(try String(data: enc.encode(FPSLimit.unlimited), encoding: .utf8), "0")
        let dec = JSONDecoder()
        XCTAssertEqual(try dec.decode(FPSLimit.self, from: Data("0".utf8)), .unlimited)
        XCTAssertEqual(try dec.decode(FPSLimit.self, from: Data("144".utf8)), .cap(144))
    }

    func testRendererAvailabilityIsHonest() {
        XCTAssertEqual(RendererChoice.metal.availability, .supported)
        if case .notSupported = RendererChoice.direct3DTranslation.availability {
            // correto: D3D ainda não existe
        } else {
            XCTFail("D3D não pode ser reportado como suportado")
        }
    }

    func testControlLayoutDefaults() {
        let layout = ControlProfile.defaultLayout()
        XCTAssertGreaterThanOrEqual(layout.count, 6)
        XCTAssertTrue(layout.contains(where: { $0.kind == .analogStick }))
        XCTAssertTrue(layout.contains(where: { $0.action == .buttonA }))
        // frames dentro de faixa razoável
        for el in layout {
            XCTAssertGreaterThan(el.frame.w, 0)
            XCTAssertGreaterThan(el.opacity, 0)
        }
    }

    func testGlobalSettingsClamps() {
        let s = GlobalSettings(masterVolume: 5, storageCacheLimitMB: 1,
                               logMaxEntries: 1)
        XCTAssertEqual(s.masterVolume, 1)
        XCTAssertEqual(s.storageCacheLimitMB, 64)
        XCTAssertEqual(s.logMaxEntries, 200)
    }
}
