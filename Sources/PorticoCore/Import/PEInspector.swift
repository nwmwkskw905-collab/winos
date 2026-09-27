import Foundation
import PorticoRuntime

/// Metadados de uma imagem PE (Windows .exe/.dll) — lidos pelo parser C real.
public struct PEImage: Equatable, Sendable {
    public let isPE32Plus: Bool
    public let isDLL: Bool
    public let machine: UInt16
    public let subsystem: UInt16
    public let timestamp: UInt32
    public let imageBase: UInt32
    public let sizeOfImage: UInt32
    public let entryPointRVA: UInt32
    public let sectionCount: UInt32
    public let importCount: UInt32
    public let arch: String
    public let imports: [String]
    public let sections: [String]

    public var isWindowsExecutable: Bool { !isDLL }
    public var is64Bit: Bool { isPE32Plus }

    /// Veredito honesto de execução no iOS atual.
    public var executionVerdict: String {
        "PE \(arch): execução Windows requer camada Win32 (não integrada neste build)"
    }
}

public enum PEInspectError: Error, Equatable, LocalizedError {
    case notAPE
    case unreadable

    public var errorDescription: String? {
        switch self {
        case .notAPE: return "O arquivo não é um executável PE válido (MZ/PE)."
        case .unreadable: return "Não foi possível ler o arquivo."
        }
    }
}

public enum PEInspector {
    public static func looksLikePE(_ data: Data) -> Bool {
        data.withUnsafeBytes { raw in
            guard let base = raw.baseAddress else { return false }
            return pr_pe_looks_like(base, raw.count) != 0
        }
    }

    public static func scan(_ data: Data) throws -> PEImage {
        try data.withUnsafeBytes { raw -> PEImage in
            guard let base = raw.baseAddress else { throw PEInspectError.unreadable }
            var info = pr_pe_info()
            let st = pr_pe_scan(base, raw.count, &info)
            guard st == PR_OK, info.is_pe != 0 else { throw PEInspectError.notAPE }

            let arch = withUnsafeBytes(of: info.arch) { buf -> String in
                let p = buf.bindMemory(to: CChar.self).baseAddress!
                return String(cString: p)
            }

            var imports: [String] = []
            for i in 0..<Int(info.import_count) {
                var nameBuf = [CChar](repeating: 0, count: 128)
                let st2 = nameBuf.withUnsafeMutableBufferPointer { p -> pr_status in
                    pr_pe_import_name(base, raw.count, i, p.baseAddress, p.count)
                }
                if st2 == PR_OK {
                    imports.append(String(cString: nameBuf))
                }
            }

            var sections: [String] = []
            var secBuf = [pr_pe_section](repeating: pr_pe_section(), count: Int(info.section_count))
            var total = 0
            secBuf.withUnsafeMutableBufferPointer { p in
                _ = pr_pe_sections(base, raw.count, p.baseAddress, p.count, &total)
            }
            sections = secBuf.prefix(total).map { sec in
                withUnsafeBytes(of: sec.name) { buf -> String in
                    let p = buf.bindMemory(to: CChar.self).baseAddress!
                    return String(cString: p)
                }
            }

            return PEImage(
                isPE32Plus: info.is_pe32plus != 0,
                isDLL: info.is_dll != 0,
                machine: info.machine,
                subsystem: info.subsystem,
                timestamp: info.timestamp,
                imageBase: info.image_base,
                sizeOfImage: info.size_of_image,
                entryPointRVA: info.entry_point_rva,
                sectionCount: info.section_count,
                importCount: info.import_count,
                arch: arch,
                imports: imports,
                sections: sections
            )
        }
    }
}
