import Foundation

public enum EnvironmentState: String, Sendable, Codable {
    case empty
    case ready
    case inUse
    case broken
}

/// Ambiente/prefixo independente para execução (estilo "prefix" de camadas
/// de compatibilidade). Estrutura real em disco gerenciada por EnvironmentManager.
public struct EnvironmentProfile: Codable, Identifiable, Equatable, Sendable {
    public var id: UUID
    public var nome: String
    public var dataCriacao: Date
    public var runtimeVersion: String
    public var variables: [String: String]
    public var state: EnvironmentState
    public var caminho: String   // relativo à raiz do sandbox

    enum CodingKeys: String, CodingKey {
        case id, nome, dataCriacao, runtimeVersion, variables, state, caminho
    }

    public init(id: UUID = UUID(),
                nome: String,
                runtimeVersion: String = "portico-runtime/1.0",
                variables: [String: String] = [:],
                state: EnvironmentState = .empty,
                caminho: String,
                dataCriacao: Date = Date()) {
        self.id = id
        self.nome = nome
        self.dataCriacao = dataCriacao
        self.runtimeVersion = runtimeVersion
        self.variables = variables
        self.state = state
        self.caminho = caminho
    }

    /// Variáveis padrão aplicadas em toda inicialização de sessão neste ambiente.
    public static func defaultVariables() -> [String: String] {
        [
            "PORTICO_ENV": "1",
            "WINEPREFIX_KIND": "portico", // reservado p/ futura camada classe-Wine
            "LANG": "en_US.UTF-8",
        ]
    }
}
