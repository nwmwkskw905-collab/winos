import Foundation

/// Gerencia ambientes/prefixos independentes (estilo Wine prefix, sem Wine ainda).
/// Cada ambiente tem identificador, configurações, arquivos, variáveis, estado
/// e versão do runtime. Estrutura real em disco dentro do sandbox.
public final class EnvironmentManager {
    public private(set) var environments: [EnvironmentProfile] = []
    public let sandbox: AppSandbox
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
    }

    // MARK: - persistência

    public func load() throws {
        try sandbox.ensureDirectories()
        guard FileManager.default.fileExists(atPath: sandbox.environmentsFile.path) else {
            environments = []
            _ = try ensureDefault()
            return
        }
        let data = try Data(contentsOf: sandbox.environmentsFile)
        environments = try decoder.decode([EnvironmentProfile].self, from: data)
        log.info("env", "ambientes carregados: \(environments.count)")
    }

    public func save() throws {
        let data = try encoder.encode(environments)
        try data.write(to: sandbox.environmentsFile, options: .atomic)
    }

    // MARK: - CRUD de ambientes

    public func environment(id: UUID) -> EnvironmentProfile? {
        environments.first(where: { $0.id == id })
    }

    /// Garante que o ambiente padrão existe (criado na primeira execução).
    @discardableResult
    public func ensureDefault() throws -> EnvironmentProfile {
        if let e = environment(id: EnvironmentRef.defaultEnv.id) {
            return e
        }
        return try create(name: EnvironmentRef.defaultEnv.name,
                          fixedID: EnvironmentRef.defaultEnv.id)
    }

    @discardableResult
    public func create(name: String, fixedID: UUID? = nil) throws -> EnvironmentProfile {
        NSLog("[WINOS-PC-CREATE] Iniciando criação: name=%@ fixedID=%@", name, fixedID?.uuidString ?? "nil")
        let id = fixedID ?? UUID()
        let relPath = "Environments/env-\(id.uuidString.prefix(8))"
        let root: URL
        do {
            root = try sandbox.resolveInside(relPath)
            NSLog("[WINOS-PC-CREATE] Path resolvido: %@ -> %@", relPath, root.path)
        } catch {
            NSLog("[WINOS-PC-CREATE] ERROR resolvendo path: %@", "\(error)")
            throw error
        }

        // estrutura de prefixo preparada para camada compatível futura (classe Wine)
        let fm = FileManager.default
        for sub in ["drive_c/users/usuario/Documents",
                    "drive_c/users/usuario/AppData",
                    "drive_c/windows/temp",
                    "logs",
                    "drive_c/Program Files",
                    "drive_c/Windows"] {
            do {
                try fm.createDirectory(at: root.appendingPathComponent(sub),
                                       withIntermediateDirectories: true)
            } catch {
                NSLog("[WINOS-PC-CREATE] ERROR criando subdir %@: %@", sub, "\(error)")
                throw error
            }
        }

        var env = EnvironmentProfile(
            id: id,
            nome: name,
            runtimeVersion: "portico-runtime/1.0",
            variables: EnvironmentProfile.defaultVariables(),
            state: .ready,
            caminho: relPath
        )
        // Validação
        guard !env.nome.trimmingCharacters(in: .whitespaces).isEmpty else {
            NSLog("[WINOS-PC-CREATE] ERROR nome vazio")
            throw NSError(domain: "WinOS", code: 1, userInfo: [NSLocalizedDescriptionKey: "Nome do PC não pode ser vazio"])
        }
        guard FileManager.default.fileExists(atPath: root.path) else {
            NSLog("[WINOS-PC-CREATE] ERROR diretório não criado: %@", root.path)
            throw NSError(domain: "WinOS", code: 2, userInfo: [NSLocalizedDescriptionKey: "Falha ao criar diretório do PC"])
        }

        environments.append(env)
        do {
            try save()
            NSLog("[WINOS-PC-PERSIST] Ambiente persistido: %@ id=%@ path=%@", name, id.uuidString, relPath)
        } catch {
            NSLog("[WINOS-PC-PERSIST] ERROR persistindo: %@", "\(error)")
            throw error
        }
        do {
            try writeMarker(&env)
            NSLog("[WINOS-PC-PERSIST] Marker escrito: %@", root.appendingPathComponent("environment.json").path)
        } catch {
            NSLog("[WINOS-PC-PERSIST] ERROR marker: %@", "\(error)")
            throw error
        }
        log.info("env", "ambiente criado: \(name) em \(relPath)")
        NSLog("[WINOS-PC-CREATE] SUCCESS id=%@ name=%@ path=%@", id.uuidString, name, relPath)
        return env
    }

    public func update(_ env: EnvironmentProfile) throws {
        guard let idx = environments.firstIndex(where: { $0.id == env.id }) else { return }
        environments[idx] = env
        try save()
    }

    public func destroy(id: UUID, deleteFiles: Bool = true) throws {
        guard let idx = environments.firstIndex(where: { $0.id == id }) else { return }
        let env = environments[idx]
        environments.remove(at: idx)
        try save()
        if deleteFiles {
            let root = try sandbox.resolveInside(env.caminho)
            try? sandbox.deleteItem(root)
        }
        log.info("env", "ambiente destruído: \(env.nome)")
    }

    /// Variáveis finais de um ambiente (perfil + overrides do jogo).
    public func mergedVariables(environmentID: UUID,
                                gameOverrides: [String: String]) -> [String: String] {
        var vars = environment(id: environmentID)?.variables
            ?? EnvironmentProfile.defaultVariables()
        for (k, v) in gameOverrides { vars[k] = v }
        return vars
    }

    public func markInUse(id: UUID, inUse: Bool) {
        guard let idx = environments.firstIndex(where: { $0.id == id }) else { return }
        environments[idx].state = inUse ? .inUse : .ready
        try? save()
    }

    /// Tamanho em disco do ambiente.
    public func sizeBytes(id: UUID) -> Int64 {
        guard let env = environment(id: id),
              let root = try? sandbox.resolveInside(env.caminho) else { return 0 }
        return (try? sandbox.directorySize(root)) ?? 0
    }

    private func writeMarker(_ env: inout EnvironmentProfile) throws {
        let root = try sandbox.resolveInside(env.caminho)
        let marker = """
        {
          "id": "\(env.id.uuidString)",
          "runtimeVersion": "\(env.runtimeVersion)",
          "dataCriacao": "\(ISO8601DateFormatter().string(from: env.dataCriacao))"
        }
        """
        try marker.data(using: .utf8)!
            .write(to: root.appendingPathComponent("environment.json"), options: .atomic)
    }
}
