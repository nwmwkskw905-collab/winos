import Foundation

/// Raiz do sandbox do app com operações seguras de arquivo.
/// NUNCA acessa caminhos fora da raiz; importações copiam para dentro via
/// mecanismos oficiais (document picker / security-scoped URLs no iOS).
public struct AppSandbox {
    public let root: URL
    private let fm = FileManager.default

    public init(root: URL) {
        self.root = root
    }

    /// Sandbox padrão: Application Support/Portico (compatibilidade) + estrutura WinOS expandida.
    public static func standard() -> AppSandbox {
        let base = FileManager.default.urls(for: .applicationSupportDirectory,
                                            in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory
        return AppSandbox(root: base.appendingPathComponent("Portico", isDirectory: true))
    }

    /// Estrutura Documents/WinOS para arquivos visíveis e compartilháveis
    public static func documentsWinOS() -> URL {
        let docs = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory
        return docs.appendingPathComponent("WinOS", isDirectory: true)
    }

    /// Estrutura Caches/WinOS para cache temporário
    public static func cachesWinOS() -> URL {
        let caches = FileManager.default.urls(for: .cachesDirectory, in: .userDomainMask).first
            ?? FileManager.default.temporaryDirectory
        return caches.appendingPathComponent("WinOS", isDirectory: true)
    }

    public var gamesDir: URL { root.appendingPathComponent("Games", isDirectory: true) }
    public var environmentsDir: URL { root.appendingPathComponent("Environments", isDirectory: true) }
    public var logsDir: URL { root.appendingPathComponent("Logs", isDirectory: true) }
    public var importsDir: URL { root.appendingPathComponent("Imports", isDirectory: true) }
    public var coversDir: URL { root.appendingPathComponent("Covers", isDirectory: true) }
    public var tempDir: URL { root.appendingPathComponent("Temp", isDirectory: true) }
    public var libraryFile: URL { root.appendingPathComponent("library.json") }
    public var settingsFile: URL { root.appendingPathComponent("settings.json") }
    public var environmentsFile: URL { root.appendingPathComponent("environments.json") }

    // MARK: - Nova estrutura WinOS (Documents/ + Application Support/ + Caches)
    public var documentsPCsDir: URL { Self.documentsWinOS().appendingPathComponent("PCs", isDirectory: true) }
    public var documentsLibraryDir: URL { Self.documentsWinOS().appendingPathComponent("Library", isDirectory: true) }
    public var documentsImportsDir: URL { Self.documentsWinOS().appendingPathComponent("Imports", isDirectory: true) }
    public var documentsLogsDir: URL { Self.documentsWinOS().appendingPathComponent("Logs", isDirectory: true) }

    public var appSupportRuntimeDir: URL { root.appendingPathComponent("Runtime", isDirectory: true) }
    public var appSupportVFSDir: URL { root.appendingPathComponent("VFS", isDirectory: true) }

    public var cachesDir: URL { Self.cachesWinOS() }

    public func ensureDirectories() throws {
        // Estrutura legada + nova
        for dir in [root, gamesDir, environmentsDir, logsDir, importsDir, coversDir, tempDir,
                    appSupportRuntimeDir, appSupportVFSDir] {
            try fm.createDirectory(at: dir, withIntermediateDirectories: true)
        }
        // Documents/WinOS/*
        for dir in [Self.documentsWinOS(), documentsPCsDir, documentsLibraryDir,
                    documentsImportsDir, documentsLogsDir] {
            try fm.createDirectory(at: dir, withIntermediateDirectories: true)
        }
        // Caches/WinOS
        try fm.createDirectory(at: cachesDir, withIntermediateDirectories: true)

        NSLog("[WINOS-SANDBOX] Directories ensured")
        NSLog("[WINOS-SANDBOX] root: %@", root.path)
        NSLog("[WINOS-SANDBOX] Documents/WinOS: %@", Self.documentsWinOS().path)
        NSLog("[WINOS-SANDBOX] Caches/WinOS: %@", cachesDir.path)
    }

    /// Garante que `url` está dentro da raiz do sandbox (defesa contra path traversal).
    public func isInsideSandbox(_ url: URL) -> Bool {
        let rootPath = root.standardizedFileURL.path
        let path = url.standardizedFileURL.path
        return path == rootPath || path.hasPrefix(rootPath + "/")
    }

    public func resolveInside(_ relative: String) throws -> URL {
        let cleaned = relative
            .replacingOccurrences(of: "\\", with: "/")
            .trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        guard !cleaned.contains("..") else {
            throw SandboxError.invalidPath(relative)
        }
        let url = root.appendingPathComponent(cleaned).standardizedFileURL
        guard isInsideSandbox(url) else {
            throw SandboxError.invalidPath(relative)
        }
        return url
    }

    // MARK: - operações de arquivo

    public func listDirectory(_ url: URL) throws -> [URL] {
        guard isInsideSandbox(url) else { throw SandboxError.invalidPath(url.path) }
        return try fm.contentsOfDirectory(at: url, includingPropertiesForKeys: [.isDirectoryKey])
    }

    public func deleteItem(_ url: URL) throws {
        guard isInsideSandbox(url) else { throw SandboxError.invalidPath(url.path) }
        try fm.removeItem(at: url)
    }

    public func moveItem(from: URL, to: URL) throws {
        guard isInsideSandbox(from), isInsideSandbox(to) else {
            throw SandboxError.invalidPath(from.path)
        }
        if fm.fileExists(atPath: to.path) {
            throw SandboxError.alreadyExists(to.lastPathComponent)
        }
        try fm.moveItem(at: from, to: to)
    }

    /// Copia arquivo ou diretório de fora do sandbox para dentro (via URL autorizado).
    public func importFrom(external url: URL, toRelative relative: String) throws -> URL {
        let dest = try resolveInside(relative)
        #if os(iOS) || os(macOS) || os(tvOS) || os(watchOS)
        let access = url.startAccessingSecurityScopedResource()
        defer { if access { url.stopAccessingSecurityScopedResource() } }
        #endif
        try fm.createDirectory(at: dest.deletingLastPathComponent(),
                               withIntermediateDirectories: true)
        if fm.fileExists(atPath: dest.path) {
            throw SandboxError.alreadyExists(relative)
        }
        try fm.copyItem(at: url, to: dest)
        return dest
    }

    public func directorySize(_ url: URL) throws -> Int64 {
        guard isInsideSandbox(url) else { throw SandboxError.invalidPath(url.path) }
        var total: Int64 = 0
        if let en = fm.enumerator(at: url, includingPropertiesForKeys: [.fileSizeKey]) {
            for case let fileURL as URL in en {
                let vals = try fileURL.resourceValues(forKeys: [.fileSizeKey])
                total += Int64(vals.fileSize ?? 0)
            }
        }
        return total
    }

    public func availableSpaceBytes() -> Int64 {
        do {
            let attrs = try fm.attributesOfFileSystem(forPath: root.path)
            if let free = attrs[.systemFreeSize] as? Int64 {
                return free
            }
            return -1
        } catch {
            return -1
        }
    }
}

public enum SandboxError: Error, Equatable, LocalizedError {
    case invalidPath(String)
    case alreadyExists(String)
    case outsideSandbox(String)

    public var errorDescription: String? {
        switch self {
        case .invalidPath(let p): return "Caminho inválido: \(p)"
        case .alreadyExists(let p): return "Já existe: \(p)"
        case .outsideSandbox(let p): return "Fora do sandbox do app: \(p)"
        }
    }
}
