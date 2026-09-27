import Foundation

/// Nível real de suporte de uma capacidade. Sem otimismo falso:
/// tudo que não funciona de verdade é `.notSupported` com o motivo.
public enum SupportLevel: Equatable, Sendable, Codable {
    case supported
    case partial(reason: String)
    case notSupported(reason: String)

    public var isUsable: Bool {
        if case .supported = self { return true }
        if case .partial = self { return true }
        return false
    }

    public var label: String {
        switch self {
        case .supported: return "SUPPORTED"
        case .partial: return "PARTIALLY SUPPORTED"
        case .notSupported: return "NOT SUPPORTED"
        }
    }
}

/// Capacidades avaliadas em tempo real.
public enum FeatureID: String, CaseIterable, Sendable, Codable {
    case windowsPEExecution
    case pxpNativeExecution
    case jitTranslation
    case interpreterCPU
    case metalRendering
    case audioOutput
    case touchInput
    case physicalControllers
    case zipImport
    case directoryImport
    case fileExport
    case gamepadHotplug
}

public struct FeatureStatus: Identifiable, Equatable, Sendable, Codable {
    public var id: FeatureID { feature }
    public let feature: FeatureID
    public let level: SupportLevel
    public let detail: String

    public init(feature: FeatureID, level: SupportLevel, detail: String = "") {
        self.feature = feature
        self.level = level
        self.detail = detail
    }
}

public struct CapabilityReport: Equatable, Sendable, Codable {
    public let generatedAt: Date
    public let features: [FeatureStatus]
    public let jitAvailable: Bool
    public let execMemMappable: Bool
    public let writeXorExecute: Bool
    public let isIOS: Bool
    public let isSimulator: Bool
    public let totalRAM: UInt64
    public let cpuBrand: String
    public let environmentNote: String

    public func status(for feature: FeatureID) -> SupportLevel {
        features.first(where: { $0.feature == feature })?.level
            ?? .notSupported(reason: "não avaliado")
    }

    public func plainText() -> String {
        var out = "Portico — Relatório de Capacidades\n"
        out += "Gerado em: \(generatedAt)\n"
        out += "CPU: \(cpuBrand)  RAM: \(totalRAM / (1024 * 1024)) MiB\n"
        out += "JIT disponível: \(jitAvailable ? "sim" : "não")\n"
        out += "Memória executável mapeável: \(execMemMappable ? "sim" : "não")\n"
        out += "W^X: \(writeXorExecute ? "sim" : "não")\n"
        out += "Plataforma: \(isIOS ? (isSimulator ? "iOS Simulator" : "iOS device") : "dev host")\n"
        out += "Nota: \(environmentNote)\n\n"
        for f in features {
            out += "[\(f.level.label)] \(f.feature.rawValue)"
            if !f.detail.isEmpty { out += " — \(f.detail)" }
            out += "\n"
        }
        return out
    }
}
