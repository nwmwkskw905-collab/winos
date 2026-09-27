import Foundation

/// Erros de biblioteca com mensagens compreensíveis ao usuário.
public enum LibraryError: Error, Equatable, LocalizedError {
    case notFound(String)
    case duplicateName(String)
    case persistence(String)

    public var errorDescription: String? {
        switch self {
        case .notFound(let n): return "Jogo não encontrado: \(n)"
        case .duplicateName(let n): return "Já existe um jogo chamado \"\(n)\""
        case .persistence(let m): return "Falha ao salvar a biblioteca: \(m)"
        }
    }
}

/// Biblioteca de jogos com persistência JSON (GameProfile por jogo).
public final class LibraryStore {
    public private(set) var games: [GameProfile] = []
    public let sandbox: AppSandbox
    public let log: LogCenter
    public var onChange: (() -> Void)?

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
        let fm = FileManager.default
        guard fm.fileExists(atPath: sandbox.libraryFile.path) else {
            games = []
            return
        }
        do {
            let data = try Data(contentsOf: sandbox.libraryFile)
            games = try decoder.decode([GameProfile].self, from: data)
            log.info("library", "biblioteca carregada: \(games.count) jogo(s)")
        } catch {
            log.error("library", "falha ao ler library.json: \(error)")
            throw LibraryError.persistence(String(describing: error))
        }
    }

    public func save() throws {
        do {
            let data = try encoder.encode(games)
            try data.write(to: sandbox.libraryFile, options: .atomic)
        } catch {
            log.error("library", "falha ao gravar library.json: \(error)")
            throw LibraryError.persistence(String(describing: error))
        }
    }

    // MARK: - CRUD

    public func game(id: UUID) -> GameProfile? {
        games.first(where: { $0.id == id })
    }

    public func game(named name: String) -> GameProfile? {
        games.first(where: { $0.nome.lowercased() == name.lowercased() })
    }

    @discardableResult
    public func add(_ profile: GameProfile) throws -> GameProfile {
        if let _ = game(named: profile.nome) {
            throw LibraryError.duplicateName(profile.nome)
        }
        var p = profile
        p.tamanhoInstalacao = (try? sandbox.directorySize(
            try sandbox.resolveInside(p.caminho))) ?? p.tamanhoInstalacao
        games.append(p)
        try save()
        log.info("library", "jogo adicionado: \(p.nome)")
        onChange?()
        return p
    }

    public func update(_ profile: GameProfile) throws {
        guard let idx = games.firstIndex(where: { $0.id == profile.id }) else {
            throw LibraryError.notFound(profile.nome)
        }
        games[idx] = profile
        try save()
        log.info("library", "jogo atualizado: \(profile.nome)")
        onChange?()
    }

    public func remove(id: UUID, deleteFiles: Bool = false) throws {
        guard let idx = games.firstIndex(where: { $0.id == id }) else {
            throw LibraryError.notFound(id.uuidString)
        }
        let p = games[idx]
        games.remove(at: idx)
        try save()
        if deleteFiles {
            let dir = try sandbox.resolveInside(p.caminho)
            try? sandbox.deleteItem(dir)
        }
        log.info("library", "jogo removido: \(p.nome)")
        onChange?()
    }

    public func markLaunched(id: UUID) {
        guard let idx = games.firstIndex(where: { $0.id == id }) else { return }
        games[idx].ultimaExecucao = Date()
        try? save()
        onChange?()
    }

    public var totalInstallBytes: Int64 {
        games.reduce(0) { $0 + $1.tamanhoInstalacao }
    }
}
