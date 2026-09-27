import Foundation
import PorticoRuntime

/// Gera o relatório agregado de diagnóstico (capacidades + armazenamento +
/// biblioteca + logs recentes) para exibição e exportação.
public enum DiagnosticsReport {
    public static func capabilities() -> CapabilityReport {
        var cap = pr_cap_info()
        pr_cap_probe(&cap)

        let jit = cap.jit_available != 0
        let execMem = cap.exec_mem_mappable != 0
        let wx = cap.write_xor_execute != 0
        let cpu = withUnsafeBytes(of: cap.cpu_brand) {
            String(cString: $0.bindMemory(to: CChar.self).baseAddress!)
        }
        let note = withUnsafeBytes(of: cap.note) {
            String(cString: $0.bindMemory(to: CChar.self).baseAddress!)
        }

        var features: [FeatureStatus] = []
        features.append(FeatureStatus(
            feature: .windowsPEExecution,
            level: .partial(reason: "execução real PE32 (IA-32 interpretado) com subset Win32; "
                                    + "PE64 e APIs ausentes recusados com diagnóstico"),
            detail: "PE → memória virtual → imports → Win32 → CPU → ExitProcess; sem JIT"))
        features.append(FeatureStatus(
            feature: .pxpNativeExecution,
            level: .supported,
            detail: "payloads IA-32 interpretados (Self-Test)"))
        features.append(FeatureStatus(
            feature: .jitTranslation,
            level: jit ? .supported
                     : .notSupported(reason: "JIT indisponível na plataforma (codesigning/W^X do iOS)"),
            detail: note))
        features.append(FeatureStatus(
            feature: .interpreterCPU,
            level: .partial(reason: "subconjunto IA-32 implementado e testado; extensão contínua"),
            detail: "MOV/ALU/shifts/pilha/branch/MUL/DIV/INT/HLT/MMIO"))
        features.append(FeatureStatus(
            feature: .metalRendering,
            level: .supported,
            detail: "pipeline de comandos + escala de resolução + pacing de FPS"))
        features.append(FeatureStatus(feature: .audioOutput, level: .supported,
            detail: "AVAudioEngine + ring de áudio do runtime"))
        features.append(FeatureStatus(feature: .touchInput, level: .supported,
            detail: "controles virtuais configuráveis por jogo"))
        features.append(FeatureStatus(feature: .physicalControllers, level: .supported,
            detail: "GameController framework (MFi/Bluetooth)"))
        features.append(FeatureStatus(feature: .zipImport, level: .supported,
            detail: "ZIP stored/deflate; ZIP64 não suportado"))
        features.append(FeatureStatus(feature: .directoryImport, level: .supported,
            detail: "via document picker (security-scoped)"))
        features.append(FeatureStatus(feature: .fileExport, level: .supported,
            detail: "logs exportáveis (ShareLink)"))
        features.append(FeatureStatus(feature: .gamepadHotplug, level: .partial(reason: "conexão por evento; perfis salvos por jogo")))

        return CapabilityReport(
            generatedAt: Date(),
            features: features,
            jitAvailable: jit,
            execMemMappable: execMem,
            writeXorExecute: wx,
            isIOS: cap.is_ios != 0,
            isSimulator: cap.is_simulator != 0,
            totalRAM: cap.total_ram,
            cpuBrand: cpu,
            environmentNote: note
        )
    }

    // MARK: - Itens de diagnóstico por subsistema (tela de diagnóstico)

    public struct DiagnosticItem: Equatable, Sendable {
        public let title: String
        public let level: SupportLevel
        public let explanation: String
    }

    /// Os 9 subsistemas com estado e explicação técnica honesta.
    public static func subsystemItems(storageNote: String = "sandbox do app") -> [DiagnosticItem] {
        var cap = pr_cap_info()
        pr_cap_probe(&cap)
        let jit = cap.jit_available != 0
        let kernel = Win32Catalog.modules.first(where: { $0.name.hasPrefix("kernel32") })
        let win32Impl = kernel?.implemented ?? 0
        let win32Cat = Win32Catalog.modules.reduce(0) { $0 + $1.cataloged }

        return [
            DiagnosticItem(
                title: "CPU backend",
                level: .partial(reason: "interpretador IA-32 (subconjunto) ativo; x86-64/FPU pendentes"),
                explanation: "pxp-interpreter: MOV/ALU/shifts/pilha/branch/MUL/DIV/INT/HLT/MMIO. "
                    + "Execução de código PE Windows exige extensão x86/x64 (pendente)."),
            DiagnosticItem(
                title: "Graphics backend",
                level: .supported,
                explanation: "Stream de comandos comum (clear/viewport/scissor/draw/present/filter) "
                    + "+ superfície por pixels (pr_surf) + fence de sincronização."),
            DiagnosticItem(
                title: "Audio backend",
                level: .supported,
                explanation: "Ring SPSC + síntese de tom no runtime; saída AVAudioEngine "
                    + "com volume, pausa e interrupções."),
            DiagnosticItem(
                title: "JIT status",
                level: jit ? .supported : .notSupported(reason: "JIT indisponível (codesigning/W^X do iOS)"),
                explanation: jit
                    ? "Memória executável disponível neste ambiente (dev-host); sonda real pr_cap_probe."
                    : "Sem JIT nesta plataforma — tradução de CPU é por INTERPRETADOR (nada de JIT fake). "
                        + "Arquitetura aceita backend alternativo quando o ambiente permitir."),
            DiagnosticItem(
                title: "PE loader",
                level: .supported,
                explanation: "Análise e carga reais: validação MZ/PE, arquitetura, PE32/PE32+, "
                    + "seções, entry point, imports por função, exports e diagnóstico. "
                    + "Nenhum código é executado nesta etapa."),
            DiagnosticItem(
                title: "Win32 compatibility",
                level: .partial(reason: "\(win32Impl) APIs implementadas de \(win32Cat) catalogadas"),
                explanation: "Dispatch por subsistema (kernel32/user32/advapi32/ws2_32/gdi32/ole32/shell32). "
                    + "\(win32Impl) APIs kernel32 com comportamento real (heap, console, relógio, "
                    + "módulos, handles, ExitProcess); demais catalogadas retornam UNSUPPORTED com log."),
            DiagnosticItem(
                title: "Metal",
                level: .supported,
                explanation: "MetalGameRenderer: framebuffer interno (renderScale) + blit "
                    + "linear/nearest, buffers reutilizados, sem alocações por frame. "
                    + "Requer device Apple (iPhone > iPad > simulador)."),
            DiagnosticItem(
                title: "Input",
                level: .supported,
                explanation: "Touch virtuais configuráveis por jogo + GameController (MFi/Bluetooth) "
                    + "com hotplug; estado via pr_input (botões/analógicos/gatilhos)."),
            DiagnosticItem(
                title: "Storage",
                level: .supported,
                explanation: "Sandbox-safe (Application Support/Portico): biblioteca JSON, "
                    + "prefixos de ambiente, importação por document picker (ZIP stored/deflate). "
                    + "ZIP64 não suportado. \(storageNote)"),
        ]
    }

    public static func fullReport(sandbox: AppSandbox,
                                  library: LibraryStore,
                                  environments: EnvironmentManager,
                                  log: LogCenter) -> String {
        var out = capabilities().plainText()
        out += "\n--- Armazenamento ---\n"
        out += "Raiz: \(sandbox.root.path)\n"
        let space = sandbox.availableSpaceBytes()
        out += "Espaço disponível: \(space >= 0 ? "\(space / (1024 * 1024)) MiB" : "indisponível")\n"
        out += "Biblioteca: \(library.games.count) jogo(s), \(library.totalInstallBytes / 1024) KiB\n"
        for g in library.games {
            out += "  - \(g.nome) [\(g.tipo.rawValue)/\(g.arquiteturaExe)] \(g.tamanhoInstalacao / 1024) KiB\n"
        }
        out += "Ambientes: \(environments.environments.count)\n"
        for e in environments.environments {
            out += "  - \(e.nome) (\(e.state.rawValue)) \(environments.sizeBytes(id: e.id) / 1024) KiB\n"
        }
        out += "\n--- Logs recentes (últimas 40) ---\n"
        let entries = log.snapshot().suffix(40)
        for e in entries {
            out += e.formatted + "\n"
        }
        return out
    }
}
