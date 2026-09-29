import Foundation

/// Resolução interna do jogo (largura × altura).
public struct Resolution: Equatable, Hashable, Sendable, Codable {
    public var width: Int
    public var height: Int

    public init(width: Int, height: Int) {
        self.width = max(160, min(width, 7680))
        self.height = max(120, min(height, 4320))
    }

    public static let auto = Resolution(width: 1280, height: 720)
    public static let presets: [Resolution] = [
        Resolution(width: 640, height: 360),
        Resolution(width: 960, height: 540),
        Resolution(width: 1280, height: 720),
        Resolution(width: 1600, height: 900),
        Resolution(width: 1920, height: 1080),
    ]

    public var description: String { "\(width)×\(height)" }
}

/// Limite de FPS (pacing do frame loop).
public enum FPSLimit: Equatable, Sendable, Codable {
    case unlimited
    case cap(Int)

    public var value: Int? {
        if case .cap(let v) = self { return v }
        return nil
    }

    public init(from decoder: Decoder) throws {
        let c = try decoder.singleValueContainer()
        let v = try c.decode(Int.self)
        self = v <= 0 ? .unlimited : .cap(min(v, 480))
    }

    public func encode(to encoder: Encoder) throws {
        var c = encoder.singleValueContainer()
        try c.encode(value ?? 0)
    }
}

/// Backend gráfico escolhido para o jogo.
public enum RendererChoice: String, CaseIterable, Sendable, Codable {
    case metal = "metal"
    case openGLTranslation = "opengl_translation"
    case direct3DTranslation = "d3d_translation"

    public var displayName: String {
        switch self {
        case .metal: return "Metal (nativo)"
        case .openGLTranslation: return "OpenGL → Metal (tradução)"
        case .direct3DTranslation: return "Direct3D → Metal (tradução)"
        }
    }

    /// Honestidade: apenas o caminho Metal está operacional hoje.
    public var availability: SupportLevel {
        switch self {
        case .metal:
            return .supported
        case .openGLTranslation:
            return .partial(reason: "interface de tradução pronta; implementação de comandos em subconjunto (ver CompatibilityLayer)")
        case .direct3DTranslation:
            return .notSupported(reason: "frontend D3D ainda não implementado; ABI de tradução definida em GraphicsTranslation")
        }
    }
}

/// Configuração de áudio por jogo.
public struct AudioProfile: Equatable, Sendable, Codable {
    public var enabled: Bool
    public var volume: Double          // 0...1
    public var sampleRate: Int        // Hz
    public var bufferFrames: Int      // frames por callback

    public init(enabled: Bool = true, volume: Double = 0.8,
                sampleRate: Int = 48000, bufferFrames: Int = 512) {
        self.enabled = enabled
        self.volume = min(max(volume, 0), 1)
        self.sampleRate = [44100, 48000].contains(sampleRate) ? sampleRate : 48000
        self.bufferFrames = min(max(bufferFrames, 128), 4096)
    }
}

/// Tipo de software importado.
public enum GameKind: String, Sendable, Codable {
    case windowsPE = "windows_pe"
    case pxpNative = "pxp_native"
    case selfTest = "self_test"
}

/// Referência a um ambiente/prefixo (EnvironmentManager).
public struct EnvironmentRef: Equatable, Hashable, Sendable, Codable {
    public var id: UUID
    public var name: String

    public init(id: UUID, name: String) {
        self.id = id
        self.name = name
    }

    public static let defaultEnv = EnvironmentRef(id: UUID(uuidString: "00000000-0000-0000-0000-000000000001")!,
                                                  name: "padrão")
}

/// Opções avançadas por jogo.
public struct AdvancedOptions: Equatable, Sendable, Codable {
    public var environmentVariables: [String: String]
    public var debugLogging: Bool
    public var maxInstructionsPerFrame: UInt32
    public var safeMode: Bool

    public init(environmentVariables: [String: String] = [:],
                debugLogging: Bool = false,
                maxInstructionsPerFrame: UInt32 = 2_000_000,
                safeMode: Bool = false) {
        self.environmentVariables = environmentVariables
        self.debugLogging = debugLogging
        self.maxInstructionsPerFrame = min(max(maxInstructionsPerFrame, 10_000), 50_000_000)
        self.safeMode = safeMode
    }
}

/// Perfil completo de um jogo (persistido).
public struct GameProfile: Codable, Identifiable, Equatable, Sendable {
    public var id: UUID
    public var nome: String
    public var caminho: String        // diretório de instalação (relativo à raiz do sandbox)
    public var executavel: String     // executável principal (relativo a `caminho`)
    public var argumentos: String
    public var resolucao: Resolution
    public var fps: FPSLimit
    public var renderer: RendererChoice
    public var audio: AudioProfile
    public var controles: ControlProfile
    public var ambiente: EnvironmentRef
    public var opcoes: AdvancedOptions

    public var dataCriacao: Date
    public var ultimaExecucao: Date?
    public var notas: String
    public var coverSeed: Int
    public var arquiteturaExe: String   // "x86", "x86_64", ...
    public var tipo: GameKind
    public var tamanhoInstalacao: Int64

    enum CodingKeys: String, CodingKey {
        case id, nome, caminho, executavel, argumentos, resolucao, fps, renderer
        case audio, controles, ambiente, opcoes
        case dataCriacao, ultimaExecucao, notas, coverSeed, arquiteturaExe, tipo
        case tamanhoInstalacao
    }

    public init(id: UUID = UUID(),
                nome: String,
                caminho: String,
                executavel: String,
                argumentos: String = "",
                resolucao: Resolution = .auto,
                fps: FPSLimit = .cap(60),
                renderer: RendererChoice = .metal,
                audio: AudioProfile = AudioProfile(),
                controles: ControlProfile = ControlProfile(),
                ambiente: EnvironmentRef = .defaultEnv,
                opcoes: AdvancedOptions = AdvancedOptions(),
                dataCriacao: Date = Date(),
                ultimaExecucao: Date? = nil,
                notas: String = "",
                coverSeed: Int = Int.random(in: 0..<100000),
                arquiteturaExe: String = "x86",
                tipo: GameKind = .windowsPE,
                tamanhoInstalacao: Int64 = 0) {
        self.id = id
        self.nome = nome
        self.caminho = caminho
        self.executavel = executavel
        self.argumentos = argumentos
        self.resolucao = resolucao
        self.fps = fps
        self.renderer = renderer
        self.audio = audio
        self.controles = controles
        self.ambiente = ambiente
        self.opcoes = opcoes
        self.dataCriacao = dataCriacao
        self.ultimaExecucao = ultimaExecucao
        self.notas = notas
        self.coverSeed = coverSeed
        self.arquiteturaExe = arquiteturaExe
        self.tipo = tipo
        self.tamanhoInstalacao = tamanhoInstalacao
    }
}
