import Foundation

/// FASE 6 — Graphics detection real
/// Detecta qual API gráfica o jogo usa via imports PE

public enum WinOSGraphicsAPI: String, Sendable, CaseIterable {
    case unknown = "UNKNOWN"
    case gdi = "GDI"
    case opengl11 = "OpenGL11"
    case openglModern = "OpenGLModern"
    case directDraw = "DirectDraw"
    case direct3D9 = "Direct3D9"
    case direct3D10 = "Direct3D10"
    case direct3D11 = "Direct3D11"
    case direct3D12 = "Direct3D12"
    case dxgi = "DXGI"
    case vulkan = "Vulkan"
    case software = "Software"
    
    public var isSupported: Bool {
        switch self {
        case .gdi, .opengl11, .software:
            return true // Implementado via pr_gl.c + pr_win32.c GDI
        case .unknown:
            return false
        default:
            return false // D3D/Vulkan não implementados, detectado honestamente
        }
    }
    
    public var priority: Int {
        switch self {
        case .direct3D12: return 100
        case .direct3D11: return 90
        case .direct3D10: return 80
        case .direct3D9: return 70
        case .vulkan: return 60
        case .dxgi: return 50
        case .openglModern: return 40
        case .opengl11: return 30
        case .directDraw: return 20
        case .gdi: return 10
        case .software: return 5
        case .unknown: return 0
        }
    }
}

public struct WinOSGraphicsDetectionResult: Sendable {
    public var primaryAPI: WinOSGraphicsAPI
    public var allAPIs: [WinOSGraphicsAPI]
    public var evidence: [String: [String]] // dll -> [funcs]
    public var confidence: Double // 0-100
    public var isSupported: Bool
    public var reason: String
    public var dlls: [String]
    public var functions: [String]
    
    public init(primaryAPI: WinOSGraphicsAPI, allAPIs: [WinOSGraphicsAPI], evidence: [String: [String]], confidence: Double, isSupported: Bool, reason: String, dlls: [String], functions: [String]) {
        self.primaryAPI = primaryAPI
        self.allAPIs = allAPIs
        self.evidence = evidence
        self.confidence = confidence
        self.isSupported = isSupported
        self.reason = reason
        self.dlls = dlls
        self.functions = functions
    }
}

public final class WinOSGraphicsDetector: Sendable {
    
    public init() {
        NSLog("[WINOS-GRAPHICS] GraphicsDetector init")
    }
    
    /// Detecta API gráfica via PEReport (imports)
    public func detect(report: PEReport) -> WinOSGraphicsDetectionResult {
        var evidence: [String: [String]] = [:]
        var apis: Set<WinOSGraphicsAPI> = []
        var allDLLs: [String] = []
        var allFuncs: [String] = []
        
        for imp in report.imports {
            let dll = imp.dll.lowercased()
            let funcs = imp.functions.map { $0.displayName.lowercased() }
            allDLLs.append(imp.dll)
            allFuncs.append(contentsOf: imp.functions.map { $0.displayName })
            
            // Acumula evidência
            evidence[imp.dll] = imp.functions.map { $0.displayName }
            
            // D3D12
            if dll.contains("d3d12") {
                apis.insert(.direct3D12)
            }
            // D3D11
            if dll.contains("d3d11") {
                apis.insert(.direct3D11)
            }
            // D3D10
            if dll.contains("d3d10") {
                apis.insert(.direct3D10)
            }
            // D3D9
            if dll.contains("d3d9") {
                apis.insert(.direct3D9)
            }
            // DXGI
            if dll.contains("dxgi") {
                apis.insert(.dxgi)
            }
            // DirectDraw
            if dll.contains("ddraw") {
                apis.insert(.directDraw)
            }
            // Vulkan
            if dll.contains("vulkan") {
                apis.insert(.vulkan)
            }
            // OpenGL
            if dll.contains("opengl32") {
                // Checa se usa funções modernas vs 1.1
                let modernFuncs = ["glCreateShader", "glCreateProgram", "glGenBuffers", "glGenVertexArrays", "glUseProgram"]
                let hasModern = funcs.contains { f in modernFuncs.contains { mf in f.contains(mf.lowercased()) } }
                if hasModern {
                    apis.insert(.openglModern)
                } else {
                    apis.insert(.opengl11)
                }
            }
            // GDI
            if dll.contains("gdi32") {
                apis.insert(.gdi)
            }
        }
        
        // Heurística: se tem CreateWindow + BitBlt mas sem D3D/OpenGL, assume GDI/Software
        if apis.isEmpty {
            apis.insert(.unknown)
        }
        
        // Determina primária por prioridade
        let sorted = apis.sorted { $0.priority > $1.priority }
        let primary = sorted.first ?? .unknown
        
        // Confidence
        let confidence: Double
        if primary == .unknown {
            confidence = 10
        } else if evidence.count >= 2 {
            confidence = 90
        } else {
            confidence = 70
        }
        
        let supported = primary.isSupported
        let reason: String
        if supported {
            reason = "API \(primary.rawValue) suportada via \(primary == .opengl11 ? "pr_gl.c software rasterizer + Metal" : "GDI software")"
        } else {
            reason = "API \(primary.rawValue) NÃO implementada — jogo requer \(primary.rawValue) que não está disponível no runtime. Detectado via DLLs: \(allDLLs.joined(separator: ","))"
        }
        
        NSLog("[WINOS-GRAPHICS] detect primary=%@ all=%@ confidence=%.0f%% supported=%@ reason=%@",
              primary.rawValue, sorted.map { $0.rawValue }.joined(separator: ","), confidence, supported ? "YES" : "NO", reason)
        
        return WinOSGraphicsDetectionResult(
            primaryAPI: primary,
            allAPIs: sorted,
            evidence: evidence,
            confidence: confidence,
            isSupported: supported,
            reason: reason,
            dlls: Array(Set(allDLLs)),
            functions: Array(Set(allFuncs))
        )
    }
    
    /// Detecta via Win32 coverage (fallback se não tem PEReport)
    public func detectFromCoverage(coverage: Win32Coverage) -> WinOSGraphicsDetectionResult {
        var apis: Set<WinOSGraphicsAPI> = []
        var dlls: [String] = []
        var funcs: [String] = []
        
        let allSymbols = coverage.resolved + coverage.unresolved + coverage.unknown
        
        for sym in allSymbols {
            let lower = sym.lowercased()
            if lower.contains("d3d12") { apis.insert(.direct3D12) }
            if lower.contains("d3d11") { apis.insert(.direct3D11) }
            if lower.contains("d3d10") { apis.insert(.direct3D10) }
            if lower.contains("d3d9") { apis.insert(.direct3D9) }
            if lower.contains("dxgi") { apis.insert(.dxgi) }
            if lower.contains("opengl32") || lower.contains("wgl") || lower.contains("glbegin") { apis.insert(.opengl11) }
            if lower.contains("gdi32") || lower.contains("bitblt") { apis.insert(.gdi) }
            if lower.contains("vulkan") { apis.insert(.vulkan) }
            dlls.append(sym)
            funcs.append(sym)
        }
        
        if apis.isEmpty { apis.insert(.unknown) }
        let sorted = apis.sorted { $0.priority > $1.priority }
        let primary = sorted.first ?? .unknown
        
        return WinOSGraphicsDetectionResult(
            primaryAPI: primary,
            allAPIs: sorted,
            evidence: ["coverage": allSymbols],
            confidence: 50,
            isSupported: primary.isSupported,
            reason: primary.isSupported ? "Suportada via coverage" : "Não suportada: \(primary.rawValue)",
            dlls: dlls,
            functions: funcs
        )
    }
}
