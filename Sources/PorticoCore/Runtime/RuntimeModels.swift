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
/// Códigos técnicos reais exigidos pela fase crítica.
public enum RuntimeFailure: Error, Equatable {
    case unsupported(reason: String)
    case backendUnavailable(reason: String)
    case payloadInvalid(reason: String)
    case guestFault(detail: String)
    case io(reason: String)
    // Códigos técnicos específicos PE/Win32/Storage
    case invalidMZ(path: String)
    case invalidPE(path: String, detail: String)
    case unsupportedArch(path: String, arch: String)
    case missingImport(dll: String, symbol: String)
    case unsupportedWin32API(api: String)
    case vfsNotFound(path: String)
    case storageAccessDenied(path: String, underlying: String)
    case peLoadFailed(path: String, code: Int32, detail: String)
    case relocationFailed(path: String)
    case entrypointFailed(path: String, entry: UInt32)
    case processInitFailed(reason: String)

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
        case .invalidMZ(let path):
            return "WINOS RUNTIME ERROR\nStage: PE_LOADER_INIT\nError: INVALID_MZ\nFile: \(path)\n\nAssinatura MZ não encontrada. Arquivo não é executável Windows válido."
        case .invalidPE(let path, let detail):
            return "WINOS RUNTIME ERROR\nStage: PE_LOADER_INIT\nError: INVALID_PE\nFile: \(path)\nDetail: \(detail)\n\nCabeçalho PE inválido."
        case .unsupportedArch(let path, let arch):
            return "WINOS RUNTIME ERROR\nStage: PE_LOADER_INIT\nError: UNSUPPORTED_ARCH\nFile: \(path)\nArch: \(arch)\n\nArquitetura não suportada. Suportado: x86, x64 mínimo."
        case .missingImport(let dll, let symbol):
            return "WINOS RUNTIME ERROR\nStage: WIN32_INIT\nError: MISSING_IMPORT\nDLL: \(dll)\nSymbol: \(symbol)\n\nImport não encontrado no catálogo Win32."
        case .unsupportedWin32API(let api):
            return "WINOS RUNTIME ERROR\nStage: WIN32_INIT\nError: UNSUPPORTED_WIN32_API\nAPI: \(api)\n\nAPI Win32 ainda não implementada."
        case .vfsNotFound(let path):
            return "WINOS RUNTIME ERROR\nStage: VFS\nError: VFS_NOT_FOUND\nPath: \(path)\n\nArquivo não encontrado no VFS."
        case .storageAccessDenied(let path, let underlying):
            return "WINOS RUNTIME ERROR\nStage: STORAGE\nError: STORAGE_ACCESS_DENIED\nPath: \(path)\nDetail: \(underlying)\n\nAcesso negado ao storage sandbox."
        case .peLoadFailed(let path, let code, let detail):
            return "WINOS RUNTIME ERROR\nStage: PE_LOADER_INIT\nError: PE_LOAD_FAILED\nFile: \(path)\nCode: \(code)\nDetail: \(detail)"
        case .relocationFailed(let path):
            return "WINOS RUNTIME ERROR\nStage: PE_LOADER_INIT\nError: RELOCATION_FAILED\nFile: \(path)\n\nFalha ao aplicar relocations."
        case .entrypointFailed(let path, let entry):
            return "WINOS RUNTIME ERROR\nStage: PROCESS_INIT\nError: ENTRYPOINT_FAILED\nFile: \(path)\nEntry: 0x\(String(entry, radix: 16))\n\nFalha ao iniciar entrypoint."
        case .processInitFailed(let reason):
            return "WINOS RUNTIME ERROR\nStage: PROCESS_INIT\nError: PROCESS_INIT_FAILED\nReason: \(reason)"
        }
    }

    public var technicalDetail: String {
        switch self {
        case .unsupported(let r): return "unsupported: \(r)"
        case .backendUnavailable(let r): return "backend unavailable: \(r)"
        case .payloadInvalid(let r): return "payload invalid: \(r)"
        case .guestFault(let d): return "guest fault: \(d)"
        case .io(let r): return "io: \(r)"
        case .invalidMZ(let p): return "INVALID_MZ path=\(p)"
        case .invalidPE(let p, let d): return "INVALID_PE path=\(p) detail=\(d)"
        case .unsupportedArch(let p, let a): return "UNSUPPORTED_ARCH path=\(p) arch=\(a)"
        case .missingImport(let dll, let sym): return "MISSING_IMPORT dll=\(dll) sym=\(sym)"
        case .unsupportedWin32API(let api): return "UNSUPPORTED_WIN32_API api=\(api)"
        case .vfsNotFound(let p): return "VFS_NOT_FOUND path=\(p)"
        case .storageAccessDenied(let p, let u): return "STORAGE_ACCESS_DENIED path=\(p) underlying=\(u)"
        case .peLoadFailed(let p, let c, let d): return "PE_LOAD_FAILED path=\(p) code=\(c) detail=\(d)"
        case .relocationFailed(let p): return "RELOCATION_FAILED path=\(p)"
        case .entrypointFailed(let p, let e): return "ENTRYPOINT_FAILED path=\(p) entry=0x\(String(e, radix:16))"
        case .processInitFailed(let r): return "PROCESS_INIT_FAILED reason=\(r)"
        }
    }
    
    public var stage: String {
        switch self {
        case .invalidMZ, .invalidPE, .unsupportedArch, .peLoadFailed, .relocationFailed: return "PE_LOADER_INIT"
        case .missingImport, .unsupportedWin32API: return "WIN32_INIT"
        case .vfsNotFound: return "VFS"
        case .storageAccessDenied: return "STORAGE"
        case .entrypointFailed, .processInitFailed: return "PROCESS_INIT"
        case .payloadInvalid: return "PE_LOADER_INIT"
        case .unsupported, .backendUnavailable: return "RUNTIME_START"
        case .io: return "STORAGE"
        case .guestFault: return "RUNNING"
        }
    }
    
    public var code: String {
        switch self {
        case .invalidMZ: return "INVALID_MZ"
        case .invalidPE: return "INVALID_PE"
        case .unsupportedArch: return "UNSUPPORTED_ARCH"
        case .missingImport: return "MISSING_IMPORT"
        case .unsupportedWin32API: return "UNSUPPORTED_WIN32_API"
        case .vfsNotFound: return "VFS_NOT_FOUND"
        case .storageAccessDenied: return "STORAGE_ACCESS_DENIED"
        case .peLoadFailed: return "PE_LOAD_FAILED"
        case .relocationFailed: return "RELOCATION_FAILED"
        case .entrypointFailed: return "ENTRYPOINT_FAILED"
        case .processInitFailed: return "PROCESS_INIT_FAILED"
        case .unsupported: return "UNSUPPORTED"
        case .backendUnavailable: return "BACKEND_UNAVAILABLE"
        case .payloadInvalid: return "PAYLOAD_INVALID"
        case .guestFault: return "GUEST_FAULT"
        case .io: return "IO_ERROR"
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
            // Verifica arquitetura real se disponível
            if arquiteturaExe.lowercased().contains("x64") || arquiteturaExe.lowercased().contains("amd64") {
                return "PARCIAL — PE x64 (Win32 mínimo, em validação)"
            } else if arquiteturaExe.lowercased().contains("x86") || arquiteturaExe.lowercased().contains("i386") {
                return "PARCIAL — PE x86 (Win32 parcial, teste requerido)"
            } else {
                return "ANÁLISE OK — PE detectado, execução em validação (Win32 parcial)"
            }
        case .pxpNative, .selfTest:
            return "SUPPORTED — interpretador PXP (IA-32)"
        }
    }
    
    /// Detalhe técnico para debug
    public var compatibilityDetail: String {
        switch tipo {
        case .windowsPE:
            return "PE: \(arquiteturaExe) | Win32: parcial | Backend: windows-pe | Validação física pendente"
        case .pxpNative, .selfTest:
            return "PXP0 IA-32 interpretado | Backend: pxp | 3411 testes"
        }
    }
}
