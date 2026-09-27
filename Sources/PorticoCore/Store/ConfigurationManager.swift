import Foundation

/// Configuração efetiva resolvida (globais + overrides do jogo).
public struct EffectiveConfig: Equatable, Sendable {
    public var resolution: Resolution
    public var fps: FPSLimit
    public var renderer: RendererChoice
    public var audioEnabled: Bool
    public var gameVolume: Double
    public var masterVolume: Double
    public var quality: QualityChoice
    public var showTouchControls: Bool
    public var physicalControllersEnabled: Bool
    public var environmentVariables: [String: String]
    public var debugLogging: Bool
    public var maxInstructionsPerFrame: UInt32
}

/// Valida e mescla configurações globais com as individuais de cada jogo.
public final class ConfigurationManager {
    public private(set) var global: GlobalSettings
    private let sandbox: AppSandbox
    public let log: LogCenter

    private let encoder: JSONEncoder = {
        let e = JSONEncoder()
        e.outputFormatting = [.prettyPrinted, .sortedKeys]
        e.dateEncodingStrategy = .iso8601
        return e
    }()
    private let decoder: JSONDecoder = {
        let d = JSONDecoder()
        d.dateDecodingStrategy = .iso8601
        return d
    }()

    public init(sandbox: AppSandbox, log: LogCenter) {
        self.sandbox = sandbox
        self.log = log
        self.global = .default
    }

    public func load() throws {
        try sandbox.ensureDirectories()
        guard FileManager.default.fileExists(atPath: sandbox.settingsFile.path) else {
            global = .default
            return
        }
        let data = try Data(contentsOf: sandbox.settingsFile)
        global = try decoder.decode(GlobalSettings.self, from: data)
        log.info("config", "configurações globais carregadas")
    }

    public func save() throws {
        let data = try encoder.encode(global)
        try data.write(to: sandbox.settingsFile, options: .atomic)
    }

    public func update(_ transform: (inout GlobalSettings) -> Void) throws {
        transform(&global)
        try save()
        log.info("config", "configurações globais atualizadas")
    }

    /// Configuração final para execução, com validação/clamps aplicados.
    public func effective(for profile: GameProfile) -> EffectiveConfig {
        EffectiveConfig(
            resolution: Resolution(width: profile.resolucao.width,
                                   height: profile.resolucao.height),
            fps: profile.fps,
            renderer: profile.renderer,
            audioEnabled: profile.audio.enabled,
            gameVolume: min(max(profile.audio.volume, 0), 1),
            masterVolume: min(max(global.masterVolume, 0), 1),
            quality: global.qualidade,
            showTouchControls: global.showTouchControls && profile.controles.showTouchControls,
            physicalControllersEnabled: global.physicalControllersEnabled
                && profile.controles.physicalControllersEnabled,
            environmentVariables: profile.opcoes.environmentVariables,
            debugLogging: profile.opcoes.debugLogging,
            maxInstructionsPerFrame: profile.opcoes.maxInstructionsPerFrame
        )
    }

    /// FPS efetivo considerando o perfil e a qualidade global.
    public func effectiveFPS(for profile: GameProfile) -> Int {
        if let v = profile.fps.value {
            return min(max(v, 15), 480)
        }
        return global.qualidade.suggestedFPS
    }
}
