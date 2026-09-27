import Foundation
import PorticoRuntime

/// Fachada Swift da camada Win32 (dispatch real por subsistema).
/// APIs implementadas executam comportamento real; não implementadas retornam
/// `PR_ERR_UNSUPPORTED` com log — nunca fingem sucesso.
public final class Win32Environment {
    private var ctx: OpaquePointer?

    /// - Parameter cLog: log C opcional (pr_log) para registros da camada.
    public init(cLog: OpaquePointer? = nil) {
        ctx = pr_win32_create(cLog)
    }

    deinit {
        if let ctx { pr_win32_destroy(ctx) }
    }

    public var isValid: Bool { ctx != nil }
    public var lastError: UInt32 { ctx.map { pr_win32_last_error($0) } ?? 0 }
    public var isHalted: Bool { ctx.map { pr_win32_halted($0) != 0 } ?? false }
    public var exitCode: UInt32 { ctx.map { pr_win32_exit_code($0) } ?? 0 }
    public var implementedCalls: Int { ctx.map { Int(pr_win32_calls_implemented($0)) } ?? 0 }
    public var unsupportedCalls: Int { ctx.map { Int(pr_win32_calls_unsupported($0)) } ?? 0 }

    /// Região de memória do convidado (dados estáticos) validada para ponteiros.
    public func scratch() -> UnsafeMutablePointer<UInt8>? {
        guard let ctx else { return nil }
        var sz = 0
        return pr_win32_scratch(ctx, &sz)
    }

    /// Associa a imagem PE carregada como módulo principal do convidado.
    public func bindImage(_ image: PELoadedImage, base: UnsafeMutableRawPointer?, size: Int) -> Bool {
        guard let ctx, let base else { return false }
        return image.moduleName.withCString { name in
            pr_win32_bind_image(ctx, base, size, name) == PR_OK
        }
    }

    /// Dispatch real: `nil` retorno + status indica falha honesta.
    public func call(_ module: String, _ name: String, args: [UInt64] = [])
        -> (status: pr_status, ret: UInt64) {
        guard let ctx else { return (PR_ERR_STATE, 0) }
        var ret: UInt64 = 0
        let st: pr_status = module.withCString { m in
            name.withCString { n in
                args.withUnsafeBufferPointer { p in
                    pr_win32_call(ctx, m, n, p.baseAddress, p.count, &ret)
                }
            }
        }
        return (st, ret)
    }

    public func stdoutRead(max: Int = 4096) -> String {
        guard let ctx else { return "" }
        var buf = [CChar](repeating: 0, count: max)
        _ = buf.withUnsafeMutableBufferPointer { p -> Int in
            pr_win32_stdout_read(ctx, p.baseAddress, p.count)
        }
        return String(cString: buf)
    }
}

/// Resumo real por subsistema Win32 (do catálogo C).
public struct Win32ModuleSummary: Equatable, Sendable {
    public let name: String
    public let implemented: Int
    public let cataloged: Int

    public var level: SupportLevel {
        if implemented == 0 {
            return .notSupported(reason: "catálogo apenas — sem implementação real")
        }
        if implemented < cataloged {
            return .partial(reason: "\(implemented)/\(cataloged) APIs implementadas com comportamento real")
        }
        return .supported
    }
}

/// Cobertura de imports de um PE contra o catálogo Win32 disponível.
public struct Win32Coverage: Equatable, Sendable {
    public let resolved: [String]     // "kernel32!GetTickCount64"
    public let unresolved: [String]   // conhecidas mas não implementadas
    public let unknown: [String]      // fora do catálogo

    public var total: Int { resolved.count + unresolved.count + unknown.count }
    public var summary: String {
        "imports Win32: \(resolved.count) resolvida(s), \(unresolved.count) não suportada(s), "
        + "\(unknown.count) fora do catálogo (de \(total))"
    }
}

public enum Win32Catalog {
    public static var modules: [Win32ModuleSummary] {
        var ptr: UnsafePointer<pr_win32_module_info>?
        let n = pr_win32_modules(&ptr)
        guard let ptr else { return [] }
        return (0..<n).map { i in
            let m = ptr[i]
            let name = withUnsafeBytes(of: m.name) { _ -> String in
                // name é UnsafePointer<CChar> importado (campo ponteiro)
                String(cString: m.name)
            }
            return Win32ModuleSummary(name: name,
                                      implemented: Int(m.implemented),
                                      cataloged: Int(m.cataloged))
        }
    }

    public static func lookup(_ module: String, _ name: String) -> Bool {
        module.withCString { m in
            name.withCString { n in
                guard let e = pr_win32_lookup(m, n) else { return false }
                return e.pointee.fn != nil
            }
        }
    }

    /// Calcula a cobertura dos imports de um relatório PE.
    public static func coverage(for report: PEReport) -> Win32Coverage {
        var resolved: [String] = []
        var unresolved: [String] = []
        var unknown: [String] = []
        for dll in report.imports {
            for f in dll.functions {
                let label = "\(dll.dll)!\(f.displayName)"
                if f.byOrdinal {
                    // resolução por ordinal requer a tabela do módulo — pendente
                    unresolved.append(label)
                    continue
                }
                switch lookup(dll.dll, f.name) {
                case true: resolved.append(label)
                case false:
                    // distingue conhecida (catalogada) de desconhecida
                    let cataloged = modules.contains { m in
                        m.name.lowercased().hasPrefix(dll.dll.lowercased().replacingOccurrences(of: ".dll", with: ""))
                    }
                    if cataloged {
                        unresolved.append(label)
                    } else {
                        unknown.append(label)
                    }
                }
            }
        }
        return Win32Coverage(resolved: resolved, unresolved: unresolved, unknown: unknown)
    }
}
