import Foundation
import PorticoCore

/// FASE 3,4,5 — Game Compatibility Layer
/// Analisa PE + Win32 + Filesystem + Graphics para gerar relatório de compatibilidade honesto

public enum WinOSCompatLevel: String, Sendable {
    case perfect = "PERFECT" // 100% APIs implementadas, graphics suportada
    case playable = "PLAYABLE" // >90% implementadas, pode ter stubs não críticos
    case partial = "PARTIAL" // 70-90%, falta algumas APIs mas inicia
    case loads = "LOADS" // 50-70%, carrega mas não roda bem
    case unsupported = "UNSUPPORTED" // <50% ou graphics não suportada
    case unknown = "UNKNOWN"
}

public struct WinOSCompatReport: Sendable {
    public var executable: String
    public var fileSize: Int64
    public var arch: String
    public var machine: UInt16
    public var isPE32Plus: Bool
    public var sectionCount: Int
    public var importCount: Int
    public var totalAPIs: Int
    public var resolvedAPIs: Int
    public var unresolvedAPIs: [String]
    public var unknownAPIs: [String]
    public var missingDLLs: [String]
    public var implementedAPIs: [String]
    public var graphics: WinOSGraphicsDetectionResult
    public var compatLevel: WinOSCompatLevel
    public var compatScore: Double // 0-100
    public var canRun: Bool
    public var reason: String
    public var recommendations: [String]
    public var timestamp: Date
    
    public init(executable: String, fileSize: Int64, arch: String, machine: UInt16, isPE32Plus: Bool, sectionCount: Int, importCount: Int, totalAPIs: Int, resolvedAPIs: Int, unresolvedAPIs: [String], unknownAPIs: [String], missingDLLs: [String], implementedAPIs: [String], graphics: WinOSGraphicsDetectionResult, compatLevel: WinOSCompatLevel, compatScore: Double, canRun: Bool, reason: String, recommendations: [String], timestamp: Date = Date()) {
        self.executable = executable
        self.fileSize = fileSize
        self.arch = arch
        self.machine = machine
        self.isPE32Plus = isPE32Plus
        self.sectionCount = sectionCount
        self.importCount = importCount
        self.totalAPIs = totalAPIs
        self.resolvedAPIs = resolvedAPIs
        self.unresolvedAPIs = unresolvedAPIs
        self.unknownAPIs = unknownAPIs
        self.missingDLLs = missingDLLs
        self.implementedAPIs = implementedAPIs
        self.graphics = graphics
        self.compatLevel = compatLevel
        self.compatScore = compatScore
        self.canRun = canRun
        self.reason = reason
        self.recommendations = recommendations
        self.timestamp = timestamp
    }
}

public final class WinOSGameCompatibility: Sendable {
    private let graphicsDetector = WinOSGraphicsDetector()
    
    public init() {
        NSLog("[WINOS-COMPAT] GameCompatibility init")
    }
    
    public func analyze(executableURL: URL, report: PEReport, coverage: Win32Coverage) -> WinOSCompatReport {
        let fileSize = (try? FileManager.default.attributesOfItem(atPath: executableURL.path)[.size] as? Int64) ?? 0
        let arch = report.image.arch
        let machine = report.image.machine
        let isPE32Plus = report.image.isPE32Plus
        let sectionCount = report.image.sectionCount
        let importCount = report.imports.count
        
        // APIs
        let totalAPIs = coverage.resolved.count + coverage.unresolved.count + coverage.unknown.count
        let resolved = coverage.resolved.count
        let unresolved = coverage.unresolved
        let unknown = coverage.unknown
        
        // Missing DLLs — detecta DLLs que não estão no catálogo conhecido
        let knownDLLs = Set(["kernel32.dll", "user32.dll", "gdi32.dll", "opengl32.dll", "msvcrt.dll", "advapi32.dll", "ws2_32.dll", "ole32.dll", "shell32.dll", "winmm.dll", "version.dll", "comctl32.dll", "shlwapi.dll", "oleaut32.dll", "crypt32.dll", "bcrypt.dll", "d3d9.dll", "d3d11.dll", "d3d12.dll", "dxgi.dll", "xinput1_4.dll", "xinput9_1_0.dll", "dinput8.dll"])
        var missingDLLs: [String] = []
        for imp in report.imports {
            let dllLower = imp.dll.lowercased()
            if !knownDLLs.contains(dllLower) {
                // Se não está no known, mas também não está no catálogo Win32 como implementado, marca missing
                if !coverage.resolved.contains(where: { $0.lowercased().contains(dllLower) }) {
                    missingDLLs.append(imp.dll)
                }
            }
        }
        
        // Graphics detection
        let graphics = graphicsDetector.detect(report: report)
        
        // Score calculation — honesto, não inventado
        var score: Double = 0
        if totalAPIs > 0 {
            score = Double(resolved) / Double(totalAPIs) * 100.0
        } else {
            score = 100 // Sem imports = trivialmente compatível
        }
        
        // Penalidade se graphics não suportada
        if !graphics.isSupported {
            score *= 0.3 // Reduz drasticamente se graphics não suportada
        }
        
        // Penalidade se arquitetura não suportada
        if machine == 0xAA64 || arch.lowercased().contains("arm64") {
            score = 0
        }
        
        // Compat level
        let level: WinOSCompatLevel
        if score >= 95 && graphics.isSupported {
            level = .perfect
        } else if score >= 80 && graphics.isSupported {
            level = .playable
        } else if score >= 60 {
            level = .partial
        } else if score >= 40 {
            level = .loads
        } else if score == 0 && machine == 0xAA64 {
            level = .unsupported
        } else {
            level = .unsupported
        }
        
        let canRun = score >= 50 && graphics.isSupported && machine != 0xAA64
        
        var reason: String
        var recommendations: [String] = []
        
        if canRun {
            reason = "Jogo pode iniciar: \(resolved)/\(totalAPIs) APIs resolvidas (\(String(format: "%.1f", score))%), graphics \(graphics.primaryAPI.rawValue) suportada"
        } else {
            if !graphics.isSupported {
                reason = "Jogo NÃO pode rodar: graphics API \(graphics.primaryAPI.rawValue) não implementada. Requer implementação de \(graphics.primaryAPI.rawValue) → Metal translation"
                recommendations.append("Implementar \(graphics.primaryAPI.rawValue) → Metal translation (FASE 7)")
            } else if score < 50 {
                reason = "Jogo NÃO pode rodar: apenas \(resolved)/\(totalAPIs) APIs (\(String(format: "%.1f", score))%) implementadas, faltam \(unresolved.count) APIs críticas"
                recommendations.append("Implementar APIs faltantes: \(unresolved.prefix(5).joined(separator: ", "))")
            } else if machine == 0xAA64 {
                reason = "Jogo NÃO pode rodar: arquitetura ARM64 não suportada, apenas x86 e x64 parcial"
                recommendations.append("Converter EXE para x86 ou implementar ARM64 backend")
            } else {
                reason = "Compatibilidade desconhecida: score \(String(format: "%.1f", score))%"
            }
        }
        
        if !missingDLLs.isEmpty {
            recommendations.append("Resolver missing DLLs: \(missingDLLs.joined(separator: ", ")) — adicionar ao fs_root ou implementar stub")
        }
        if !unresolved.isEmpty {
            recommendations.append("Implementar \(unresolved.count) APIs não resolvidas")
        }
        
        NSLog("[WINOS-COMPAT] analyze exe=%@ arch=%@ machine=0x%x score=%.1f%% level=%@ canRun=%@ graphics=%@ reason=%@",
              executableURL.lastPathComponent, arch, machine, score, level.rawValue, canRun ? "YES" : "NO", graphics.primaryAPI.rawValue, reason)
        
        return WinOSCompatReport(
            executable: executableURL.lastPathComponent,
            fileSize: fileSize,
            arch: arch,
            machine: machine,
            isPE32Plus: isPE32Plus,
            sectionCount: Int(sectionCount),
            importCount: importCount,
            totalAPIs: totalAPIs,
            resolvedAPIs: resolved,
            unresolvedAPIs: unresolved,
            unknownAPIs: unknown,
            missingDLLs: missingDLLs,
            implementedAPIs: coverage.resolved,
            graphics: graphics,
            compatLevel: level,
            compatScore: score,
            canRun: canRun,
            reason: reason,
            recommendations: recommendations
        )
    }
    
    public func generateMarkdown(report: WinOSCompatReport) -> String {
        var md = ""
        md += "# WINOS GAME COMPATIBILITY REPORT\n\n"
        md += "Executable: \(report.executable)\n"
        md += "FileSize: \(report.fileSize) bytes\n"
        md += "Arch: \(report.arch) (machine 0x\(String(report.machine, radix: 16)) isPE32Plus=\(report.isPE32Plus))\n"
        md += "Sections: \(report.sectionCount), Imports: \(report.importCount)\n"
        md += "APIs: \(report.resolvedAPIs)/\(report.totalAPIs) resolved (\(String(format: "%.1f", report.compatScore))%)\n"
        md += "Compat Level: \(report.compatLevel.rawValue)\n"
        md += "CanRun: \(report.canRun ? "YES" : "NO")\n"
        md += "Reason: \(report.reason)\n\n"
        
        md += "## Graphics\n"
        md += "Primary: \(report.graphics.primaryAPI.rawValue) (supported=\(report.graphics.isSupported), confidence=\(String(format: "%.0f", report.graphics.confidence))%)\n"
        md += "All: \(report.graphics.allAPIs.map { $0.rawValue }.joined(separator: ", "))\n"
        md += "DLLs: \(report.graphics.dlls.joined(separator: ", "))\n"
        md += "Evidence: \(report.graphics.evidence)\n\n"
        
        md += "## Missing DLLs (\(report.missingDLLs.count))\n"
        for dll in report.missingDLLs {
            md += "- \(dll)\n"
        }
        md += "\n## Unresolved APIs (\(report.unresolvedAPIs.count))\n"
        for api in report.unresolvedAPIs.prefix(20) {
            md += "- \(api)\n"
        }
        if report.unresolvedAPIs.count > 20 {
            md += "- ... and \(report.unresolvedAPIs.count - 20) more\n"
        }
        md += "\n## Unknown APIs (\(report.unknownAPIs.count))\n"
        for api in report.unknownAPIs.prefix(20) {
            md += "- \(api)\n"
        }
        md += "\n## Recommendations\n"
        for rec in report.recommendations {
            md += "- \(rec)\n"
        }
        md += "\nTimestamp: \(report.timestamp)\n"
        return md
    }
}
