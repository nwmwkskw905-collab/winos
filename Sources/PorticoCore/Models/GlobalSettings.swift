import Foundation

public enum ThemeChoice: String, CaseIterable, Sendable, Codable {
    case system, dark, light
}

public enum LanguageChoice: String, CaseIterable, Sendable, Codable {
    case system, ptBR = "pt-BR", enUS = "en-US"
}

public enum QualityChoice: String, CaseIterable, Sendable, Codable {
    case low, medium, high, ultra

    /// Escala de renderização interna (1.0 = resolução nativa do jogo).
    public var renderScale: Double {
        switch self {
        case .low: return 0.6
        case .medium: return 0.8
        case .high: return 1.0
        case .ultra: return 1.0
        }
    }

    /// Limite suave de FPS sugerido quando o perfil do jogo é ilimitado.
    public var suggestedFPS: Int {
        switch self {
        case .low: return 30
        case .medium: return 45
        case .high: return 60
        case .ultra: return 120
        }
    }
}

public enum LogLevelFilter: Int, CaseIterable, Sendable, Codable {
    case debug = 0, info = 1, warning = 2, error = 3
}

/// Configurações globais do aplicativo.
public struct GlobalSettings: Equatable, Sendable, Codable {
    public var tema: ThemeChoice
    public var idioma: LanguageChoice
    public var qualidade: QualityChoice

    public var masterVolume: Double
    public var audioSessionsEnabled: Bool

    public var showTouchControls: Bool
    public var physicalControllersEnabled: Bool

    public var storageCacheLimitMB: Int

    public var logLevelFilter: LogLevelFilter
    public var logMaxEntries: Int
    public var logAutoExportOnFailure: Bool

    public init(tema: ThemeChoice = .system,
                idioma: LanguageChoice = .system,
                qualidade: QualityChoice = .high,
                masterVolume: Double = 0.8,
                audioSessionsEnabled: Bool = true,
                showTouchControls: Bool = true,
                physicalControllersEnabled: Bool = true,
                storageCacheLimitMB: Int = 512,
                logLevelFilter: LogLevelFilter = .info,
                logMaxEntries: Int = 5000,
                logAutoExportOnFailure: Bool = true) {
        self.tema = tema
        self.idioma = idioma
        self.qualidade = qualidade
        self.masterVolume = min(max(masterVolume, 0), 1)
        self.audioSessionsEnabled = audioSessionsEnabled
        self.showTouchControls = showTouchControls
        self.physicalControllersEnabled = physicalControllersEnabled
        self.storageCacheLimitMB = min(max(storageCacheLimitMB, 64), 16384)
        self.logLevelFilter = logLevelFilter
        self.logMaxEntries = min(max(logMaxEntries, 200), 100_000)
        self.logAutoExportOnFailure = logAutoExportOnFailure
    }

    public static let `default` = GlobalSettings()
}
