import Foundation
import PorticoRuntime
/// Detalhe completo de uma imagem PE — construído pelo loader C real:
/// imports por função, exports e texto de diagnóstico. NADA é executado.
public struct PEImportFunction: Equatable, Sendable {
    public let name: String        // vazio se byOrdinal
    public let ordinal: UInt16
    public let hint: UInt16
    public let byOrdinal: Bool

    public var displayName: String {
        byOrdinal ? "#\(ordinal)" : name
    }
}

public struct PEImportDLL: Equatable, Sendable {
    public let dll: String
    public let functions: [PEImportFunction]
}

public struct PEExportEntry: Equatable, Sendable {
    public let name: String
    public let ordinal: UInt16
    public let rva: UInt32
}

/// Relatório de análise PE (leitura segura; sem execução de código).
public struct PEReport: Equatable, Sendable {
    public let image: PEImage
    public let imports: [PEImportDLL]
    public let exports: [PEExportEntry]
    public let diagnostics: String   // texto legível (pr_pe_diagnose)

    public var totalImportFunctions: Int {
        imports.reduce(0) { $0 + $1.functions.count }
    }
}

public enum PELoadError: Error, Equatable, LocalizedError {
    case invalidImage
    case mappingFailed(String)

    public var errorDescription: String? {
        switch self {
        case .invalidImage:
            return "A imagem PE é inválida ou corrompida (análise falhou)."
        case .mappingFailed(let d):
            return "Falha ao carregar a imagem em memória: \(d)"
        }
    }
}

/// Loader/inspetor PE (etapa de análise e carga — sem execução).
public enum PELoader {
    /// Análise completa: scan + imports por função + exports + diagnóstico.
    public static func inspect(_ data: Data) throws -> PEReport {
        let image = try PEInspector.scan(data)
        var imports: [PEImportDLL] = []
        var exports: [PEExportEntry] = []

        return try data.withUnsafeBytes { (raw: UnsafeRawBufferPointer) -> PEReport in
            guard let base = raw.baseAddress else { throw PELoadError.invalidImage }
            let count = raw.count

            for i in 0..<Int(image.importCount) {
                var dllBuf = [CChar](repeating: 0, count: 64)
                guard dllBuf.withUnsafeMutableBufferPointer({ p -> Bool in
                    pr_pe_import_name(base, count, i, p.baseAddress, p.count) == PR_OK
                }) else { continue }
                let dll = String(cString: dllBuf)

                var funcs: [PEImportFunction] = []
                let nf = pr_pe_import_func_count(base, count, i)
                for f in 0..<nf {
                    var fn = pr_pe_import_func()
                    guard pr_pe_import_func_at(base, count, i, f, &fn) == PR_OK else { continue }
                    let fname = withUnsafeBytes(of: fn.name) { buf -> String in
                        String(cString: buf.bindMemory(to: CChar.self).baseAddress!)
                    }
                    funcs.append(PEImportFunction(
                        name: fname, ordinal: fn.ordinal,
                        hint: fn.hint, byOrdinal: fn.by_ordinal != 0))
                }
                imports.append(PEImportDLL(dll: dll, functions: funcs))
            }

            var expBuf = [pr_pe_export](repeating: pr_pe_export(), count: 256)
            var total = 0
            let copied = expBuf.withUnsafeMutableBufferPointer { p -> Int in
                pr_pe_exports(base, count, p.baseAddress, p.count, &total)
            }
            for i in 0..<copied {
                let e = expBuf[i]
                let ename = withUnsafeBytes(of: e.name) { buf -> String in
                    String(cString: buf.bindMemory(to: CChar.self).baseAddress!)
                }
                exports.append(PEExportEntry(
                    name: ename, ordinal: e.ordinal, rva: e.rva))
            }

            var diag = [CChar](repeating: 0, count: 8192)
            _ = diag.withUnsafeMutableBufferPointer { p -> Int in
                pr_pe_diagnose(base, count, p.baseAddress, p.count)
            }
            let diagnostics = String(cString: diag)
            return PEReport(image: image, imports: imports,
                            exports: exports, diagnostics: diagnostics)
        }
    }

    /// Carrega a imagem mapeada em memória (headers + seções nos RVAs) para
    /// inspeção/ligação futura. NÃO executa código.
    public static func loadImage(_ data: Data, moduleName: String) throws -> PELoadedImage {
        try PELoadedImage(data: data, moduleName: moduleName)
    }
}

/// Imagem PE mapeada em memória (proprietária do buffer C `pr_pe_loaded`).
public final class PELoadedImage {
    private let raw: UnsafeMutablePointer<pr_pe_loaded>
    public let report: PEReport

    public var imageSize: Int { Int(raw.pointee.image_size) }
    public var entryRVA: UInt32 { raw.pointee.entry_rva }
    public var moduleName: String {
        withUnsafeBytes(of: raw.pointee.module_name) {
            String(cString: $0.bindMemory(to: CChar.self).baseAddress!)
        }
    }
    /// Ponteiro do entry point na imagem mapeada (inspeção; não executar).
    public var entryPointer: UnsafeMutableRawPointer? {
        pr_pe_loaded_entry(raw)
    }

    public init(data: Data, moduleName: String) throws {
        self.report = try PELoader.inspect(data)
        self.raw = .allocate(capacity: 1)
        raw.initialize(to: pr_pe_loaded())
        let st: pr_status = data.withUnsafeBytes { r in
            moduleName.withCString { name in
                pr_pe_load(r.baseAddress, r.count, raw, name)
            }
        }
        guard st == PR_OK else {
            raw.deallocate()
            switch st {
            case PR_ERR_NOMEM: throw PELoadError.mappingFailed("memória insuficiente")
            case PR_ERR_RANGE: throw PELoadError.mappingFailed("tamanho de imagem inválido")
            default: throw PELoadError.mappingFailed(pr_status_str(st).map { String(cString: $0) } ?? "?")
            }
        }
    }

    deinit {
        pr_pe_loaded_free(raw)
        raw.deallocate()
    }

    /// String ASCII apontada por RVA dentro da imagem mapeada.
    public func string(atRVA rva: UInt32) -> String? {
        var buf = [CChar](repeating: 0, count: 256)
        let st = buf.withUnsafeMutableBufferPointer { p -> pr_status in
            pr_pe_loaded_string(raw, rva, p.baseAddress, p.count)
        }
        guard st == PR_OK else { return nil }
        return String(cString: buf)
    }

    /// Ponteiro validado dentro da imagem mapeada.
    public func pointer(atRVA rva: UInt32, length: Int) -> UnsafeMutableRawPointer? {
        pr_pe_loaded_ptr(raw, rva, length)
    }
}
