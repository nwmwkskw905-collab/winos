import Foundation
import PorticoRuntime
public struct ZipEntry: Equatable, Sendable {
    public let name: String
    public let compressedSize: UInt64
    public let uncompressedSize: UInt64
    public let isDirectory: Bool
    public let isDeflate: Bool
}

public enum ZipError: Error, Equatable, LocalizedError {
    case invalidArchive
    case zip64Unsupported
    case entryFailed(String)
    case unsafePath(String)

    public var errorDescription: String? {
        switch self {
        case .invalidArchive: return "Arquivo ZIP inválido ou corrompido."
        case .zip64Unsupported: return "ZIP64 ainda não é suportado (limitação v1)."
        case .entryFailed(let n): return "Falha ao extrair: \(n)"
        case .unsafePath(let n): return "Entrada ignorada por caminho inseguro: \(n)"
        }
    }
}

/// Leitor ZIP real (central directory + inflate zlib) sobre o parser C.
/// Sanitiza path traversal na extração (bloqueia "..", absolutos, "C:").
public final class ZipReader {
    private var handle: OpaquePointer?

    public let entries: [ZipEntry]

    public init(url: URL) throws {
        var h: OpaquePointer?
        let st = url.path.withCString { pr_zip_open_file($0, &h) }
        try Self.check(st)
        self.handle = h
        self.entries = Self.readEntries(h)
    }

    deinit {
        if let h = handle { pr_zip_close(h) }
    }

    private static func check(_ st: pr_status) throws {
        switch st {
        case PR_OK: return
        case PR_ERR_UNSUPPORTED: throw ZipError.zip64Unsupported
        default: throw ZipError.invalidArchive
        }
    }

    private static func readEntries(_ h: OpaquePointer?) -> [ZipEntry] {
        guard let h else { return [] }
        let n = pr_zip_count(h)
        var out: [ZipEntry] = []
        for i in 0..<n {
            var e = pr_zip_entry()
            guard pr_zip_entry_info(h, i, &e) == PR_OK else { continue }
            let name = withUnsafeBytes(of: e.name) { buf -> String in
                let p = buf.bindMemory(to: CChar.self).baseAddress!
                return String(cString: p)
            }
            out.append(ZipEntry(
                name: name,
                compressedSize: e.comp_size,
                uncompressedSize: e.uncomp_size,
                isDirectory: e.is_dir != 0,
                isDeflate: e.comp_method == 8
            ))
        }
        return out
    }

    /// Extrai todas as entradas seguras para `directory` (dentro do sandbox).
    /// Retorna o nº de arquivos gravados.
    @discardableResult
    public func extract(to directory: URL, log: LogCenter? = nil) throws -> Int {
        guard let h = handle else { throw ZipError.invalidArchive }
        var files = 0
        let logger = log
        // hook C: ponteiro estável para o LogCenter via box
        let box = Unmanaged.passUnretained(LogHookBox(logger)).toOpaque()
        let st = directory.path.withCString { cdir -> pr_status in
            pr_zip_extract_to_dir(h, cdir, &files, { ud, msg in
                guard let ud, let msg else { return }
                let b = Unmanaged<LogHookBox>.fromOpaque(ud).takeUnretainedValue()
                b.log?.warning("zip", String(cString: msg))
            }, box)
        }
        try Self.check(st)
        log?.info("zip", "extraídos \(files) arquivo(s) para \(directory.lastPathComponent)")
        return files
    }
}

/// Box para atravessar o hook de log C.
final class LogHookBox {
    let log: LogCenter?
    init(_ log: LogCenter?) { self.log = log }
}
