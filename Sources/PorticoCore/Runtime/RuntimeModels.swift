import Foundation

/// Estado da sessão de execução.
public enum RuntimeSessionState: Equatable, Sendable, Codable {
    case idle
    case preparing
    case running
    case paused
    case stopped
    case failed

    public var isActive: Bool {
        self == .running || self == .paused || self == .preparing
    }
}

/// Falhas de execução com dupla face: mensagem ao usuário + detalhe técnico p/ log.
public enum RuntimeFailure: Error, Equatable {
    case unsupported(reason: String)
    case backendUnavailable(reason: String)
    case payloadInvalid(reason: String)
    case guestFault(detail: String)
    case io(reason: String)

    public var userMessage: String {
        switch self {
        case .unsupported(let r):
            return "Este software ainda não pode ser executado neste dispositivo."
                + "\n\nMotivo: \(r)"
        case .backendUnavailable(let r):
            return "Nenhum motor de execução compatível está disponível.\n\nMotivo: \(r)"
        case .payloadInvalid(let r):
            return "O arquivo selecionado não pôde ser carregado.\n\nDetalhe: \(r)"
        case .guestFault:
            return "O software encontrou um erro durante a execução e foi encerrado."
                + "\n\nConsulte os logs para detalhes técnicos."
        case .io(let r):
            return "Erro de arquivos durante a execução.\n\nDetalhe: \(r)"
        }
    }

    public var technicalDetail: String {
        switch self {
        case .unsupported(let r): return "unsupported: \(r)"
        case .backendUnavailable(let r): return "backend unavailable: \(r)"
        case .payloadInvalid(let r): return "payload invalid: \(r)"
        case .guestFault(let d): return "guest fault: \(d)"
        case .io(let r): return "io: \(r)"
        }
    }
}

/// Software a executar.
public enum SoftwareImage: Equatable, Sendable {
    case windowsPE(PEImage)
    case pxpNative
    case unknown

    public var label: String {
        switch self {
        case .windowsPE(let pe): return "Windows PE (\(pe.arch))"
        case .pxpNative: return "Payload nativo PXP"
        case .unknown: return "Formato desconhecido"
        }
    }
}

/// Contexto completo de uma sessão.
public struct RuntimeSessionContext: Sendable {
    public let profile: GameProfile
    public let config: EffectiveConfig
    public let executableURL: URL
    public let environmentVariables: [String: String]
    public let environmentName: String

    public init(profile: GameProfile, config: EffectiveConfig,
                executableURL: URL,
                environmentVariables: [String: String],
                environmentName: String) {
        self.profile = profile
        self.config = config
        self.executableURL = executableURL
        self.environmentVariables = environmentVariables
        self.environmentName = environmentName
    }
}

/// Resultado de um frame de runtime.
public struct FrameResult: Equatable, Sendable {
    public var state: RuntimeSessionState
    public var framesPresented: UInt32
    public var halted: Bool
    public var message: String
    public var failure: RuntimeFailure?

    public init(state: RuntimeSessionState, framesPresented: UInt32 = 0,
                halted: Bool = false, message: String = "",
                failure: RuntimeFailure? = nil) {
        self.state = state
        self.framesPresented = framesPresented
        self.halted = halted
        self.message = message
        self.failure = failure
    }
}

/// Eventos emitidos pelo RuntimeManager para a UI.
public struct RuntimeEvents {
    public var onStateChange: ((RuntimeSessionState) -> Void)?
    public var onFailure: ((RuntimeFailure) -> Void)?
    public var onLog: ((LogEntry) -> Void)?
    public var onFPS: ((Double) -> Void)?
    public var onFinished: (() -> Void)?

    public init() {}
}

extension GameProfile {
    /// Status de compatibilidade honesto para exibição na biblioteca/detalhe.
    public var compatibilityLabel: String {
        switch tipo {
        case .windowsPE:
            return "NÃO SUP. — execução Win32 não integrada (análise/carga OK)"
        case .pxpNative, .selfTest:
            return "SUPPORTED — interpretador PXP (IA-32)"
        }
    }
}
