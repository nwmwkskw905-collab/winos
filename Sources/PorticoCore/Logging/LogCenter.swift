import Foundation

public enum LogLevel: Int, Comparable, CaseIterable, Sendable, Codable {
    case debug = 0
    case info = 1
    case warning = 2
    case error = 3

    public static func < (lhs: LogLevel, rhs: LogLevel) -> Bool {
        lhs.rawValue < rhs.rawValue
    }

    public var label: String {
        switch self {
        case .debug: return "DEBUG"
        case .info: return "INFO"
        case .warning: return "WARNING"
        case .error: return "ERROR"
        }
    }
}

public struct LogEntry: Identifiable, Equatable, Sendable, Codable {
    public var id: UInt64
    public var timestamp: Date
    public var level: LogLevel
    public var category: String
    public var message: String

    public init(id: UInt64, timestamp: Date = Date(),
                level: LogLevel, category: String, message: String) {
        self.id = id
        self.timestamp = timestamp
        self.level = level
        self.category = category
        self.message = message
    }

    public var formatted: String {
        let t = ISO8601DateFormatter().string(from: timestamp)
        return "[\(t)] [\(level.label)] [\(category)] \(message)"
    }
}

/// Sistema de logs por execução + log global do app.
/// Categorias livres ("app", "import", "runtime", "gfx", "audio", "input", "pxp"...).
public final class LogCenter {
    private let lock = NSLock()
    private var entries: [LogEntry] = []
    private var nextID: UInt64 = 1
    public var maxEntries: Int
    public var minLevel: LogLevel

    /// Callback de novas entradas (para UI observar).
    public var onEntry: ((LogEntry) -> Void)?

    public init(maxEntries: Int = 5000, minLevel: LogLevel = .debug) {
        self.maxEntries = max(100, maxEntries)
        self.minLevel = minLevel
    }

    public func log(_ level: LogLevel, _ category: String, _ message: String) {
        guard level >= minLevel else { return }
        let entry: LogEntry
        lock.lock()
        entry = LogEntry(id: nextID, level: level, category: category, message: message)
        nextID += 1
        entries.append(entry)
        if entries.count > maxEntries {
            entries.removeFirst(entries.count - maxEntries)
        }
        lock.unlock()
        onEntry?(entry)
    }

    public func debug(_ cat: String, _ msg: String) { log(.debug, cat, msg) }
    public func info(_ cat: String, _ msg: String) { log(.info, cat, msg) }
    public func warning(_ cat: String, _ msg: String) { log(.warning, cat, msg) }
    public func error(_ cat: String, _ msg: String) { log(.error, cat, msg) }

    public func snapshot(minLevel: LogLevel = .debug,
                         category: String? = nil) -> [LogEntry] {
        lock.lock()
        defer { lock.unlock() }
        return entries.filter {
            $0.level >= minLevel && (category == nil || $0.category == category)
        }
    }

    public func clear() {
        lock.lock()
        entries.removeAll()
        lock.unlock()
    }

    /// Exporta em texto (permitido: vai para a área de compartilhamento do app).
    public func exportText(minLevel: LogLevel = .error) -> String {
        snapshot(minLevel: minLevel).map(\.formatted).joined(separator: "\n")
    }

    /// Grava o export em um diretório do sandbox; retorna o arquivo criado.
    @discardableResult
    public func exportTo(directory: URL, prefix: String = "portico-log") throws -> URL {
        let fmt = DateFormatter()
        fmt.dateFormat = "yyyyMMdd-HHmmss"
        let name = "\(prefix)-\(fmt.string(from: Date())).txt"
        let url = directory.appendingPathComponent(name)
        try exportText(minLevel: .debug).data(using: .utf8)!.write(to: url, options: .atomic)
        return url
    }
}
