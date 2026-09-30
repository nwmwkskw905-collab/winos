import Foundation
import PorticoCore

/// FASE 11 — Diagnostics real para game runtime
/// Categorias: INFO, WARNING, UNIMPLEMENTED, MISSING_DLL, GRAPHICS, AUDIO, INPUT, PROCESS, FILESYSTEM, PE_LOADER

public enum WinOSDiagCategory: String, Sendable, CaseIterable {
    case info = "INFO"
    case warning = "WARNING"
    case unimplemented = "UNIMPLEMENTED"
    case missingDLL = "MISSING_DLL"
    case missingAPI = "MISSING_API"
    case graphics = "GRAPHICS"
    case audio = "AUDIO"
    case input = "INPUT"
    case process = "PROCESS"
    case filesystem = "FILESYSTEM"
    case peLoader = "PE_LOADER"
    case win32 = "WIN32"
    case metal = "METAL"
    case render = "RENDER"
    case compatibility = "COMPAT"
    case game = "GAME"
}

public enum WinOSDiagLevel: String, Sendable {
    case debug = "DEBUG"
    case info = "INFO"
    case warning = "WARNING"
    case error = "ERROR"
    case critical = "CRITICAL"
}

public struct WinOSDiagEntry: Sendable, Identifiable {
    public let id: UUID
    public let timestamp: Date
    public let level: WinOSDiagLevel
    public let category: WinOSDiagCategory
    public let code: String
    public let message: String
    public let detail: String
    public let file: String
    public let line: Int
    
    public init(level: WinOSDiagLevel, category: WinOSDiagCategory, code: String, message: String, detail: String = "", file: String = #file, line: Int = #line) {
        self.id = UUID()
        self.timestamp = Date()
        self.level = level
        self.category = category
        self.code = code
        self.message = message
        self.detail = detail
        self.file = file
        self.line = line
    }
    
    public var formatted: String {
        let date = ISO8601DateFormatter().string(from: timestamp)
        return "[WINOS-\(category.rawValue)] [\(level.rawValue)] [\(code)] \(message) \(detail) @\(file):\(line) time=\(date)"
    }
}

@MainActor
public final class WinOSGameDiagnostics: ObservableObject {
    @Published public private(set) var entries: [WinOSDiagEntry] = []
    @Published public private(set) var missingDLLs: Set<String> = []
    @Published public private(set) var missingAPIs: Set<String> = []
    @Published public private(set) var unimplementedAPIs: Set<String> = []
    @Published public private(set) var graphicsAPIs: Set<String> = []
    
    private let maxEntries = 2048
    private var startTime: Date = Date()
    
    public init() {
        NSLog("[WINOS-DIAG] WinOSGameDiagnostics init")
    }
    
    public func reset() {
        entries.removeAll()
        missingDLLs.removeAll()
        missingAPIs.removeAll()
        unimplementedAPIs.removeAll()
        graphicsAPIs.removeAll()
        startTime = Date()
        NSLog("[WINOS-DIAG] reset")
    }
    
    // MARK: - Core logging
    
    public func log(_ level: WinOSDiagLevel, category: WinOSDiagCategory, code: String, message: String, detail: String = "", file: String = #file, line: Int = #line) {
        let entry = WinOSDiagEntry(level: level, category: category, code: code, message: message, detail: detail, file: file, line: line)
        if entries.count >= maxEntries {
            entries.removeFirst()
        }
        entries.append(entry)
        
        // Mirror to NSLog for real device debugging
        NSLog("[WINOS-DIAG] [\(category.rawValue)] [\(level.rawValue)] [\(code)] \(message) \(detail)")
        
        // Track sets
        switch category {
        case .missingDLL:
            missingDLLs.insert(message)
        case .missingAPI:
            missingAPIs.insert(message)
        case .unimplemented:
            unimplementedAPIs.insert(message)
        case .graphics:
            graphicsAPIs.insert(message)
        default:
            break
        }
    }
    
    // MARK: - Convenience
    
    public func gameStart(executable: String, size: Int64, arch: String) {
        log(.info, category: .game, code: "GAME_START", message: "GAME START", detail: "exe=\(executable) size=\(size) arch=\(arch) time=\(Date())")
    }
    
    public func peLoaderInit(file: String, size: Int) {
        log(.info, category: .peLoader, code: "PE_LOADER_INIT", message: "PE_LOADER_INIT", detail: "file=\(file) size=\(size)")
    }
    
    public func peLoaderSuccess(file: String, machine: UInt16, arch: String, isPE32Plus: Bool, sections: Int, imports: Int) {
        log(.info, category: .peLoader, code: "PE_LOADER_SUCCESS", message: "PE_LOADER_SUCCESS", detail: "file=\(file) machine=0x\(String(machine, radix: 16)) arch=\(arch) isPE32Plus=\(isPE32Plus) sections=\(sections) imports=\(imports)")
    }
    
    public func peLoaderFail(file: String, reason: String) {
        log(.error, category: .peLoader, code: "PE_LOADER_FAIL", message: "PE_LOADER_FAIL", detail: "file=\(file) reason=\(reason)")
    }
    
    public func dllLoad(dll: String, status: String, funcCount: Int = 0) {
        log(.info, category: .info, code: "DLL_LOAD", message: "DLL_LOAD", detail: "dll=\(dll) status=\(status) funcCount=\(funcCount)")
        if status.contains("MISSING") {
            log(.warning, category: .missingDLL, code: "MISSING_DLL", message: dll, detail: "DLL not found in fs_root")
        }
    }
    
    public func apiCall(dll: String, api: String, status: String, args: String = "") {
        let full = "\(dll)!\(api)"
        if status == "IMPLEMENTED" {
            log(.debug, category: .win32, code: "API_CALL", message: "API_CALL", detail: "dll=\(dll) api=\(api) status=\(status) args=\(args)")
        } else if status == "UNIMPLEMENTED" {
            log(.warning, category: .unimplemented, code: "UNIMPLEMENTED_API", message: full, detail: "API not implemented, will cause EXECUTION STOPPED")
        } else if status == "MISSING" {
            log(.error, category: .missingAPI, code: "MISSING_API", message: full, detail: "API not in catalog")
        }
    }
    
    public func graphicsDetected(api: String, dlls: [String], funcs: [String]) {
        log(.info, category: .graphics, code: "GRAPHICS_DETECTED", message: api, detail: "dlls=\(dlls.joined(separator: ",")) funcs=\(funcs.prefix(5).joined(separator: ","))")
    }
    
    public func metalInit(device: String, success: Bool, reason: String = "") {
        if success {
            log(.info, category: .metal, code: "METAL_INIT_SUCCESS", message: "METAL_INIT_SUCCESS", detail: "device=\(device)")
        } else {
            log(.error, category: .metal, code: "METAL_INIT_FAIL", message: "METAL_INIT_FAIL", detail: "reason=\(reason)")
        }
    }
    
    public func renderFrame(frame: UInt64, fps: Double, frameTimeMs: Double, drawCalls: Int, width: Int, height: Int) {
        if frame % 60 == 0 {
            log(.info, category: .render, code: "RENDER_FRAME", message: "RENDER_FRAME", detail: "frame=\(frame) fps=\(String(format: "%.1f", fps)) frameTimeMs=\(String(format: "%.2f", frameTimeMs)) drawCalls=\(drawCalls) res=\(width)x\(height)")
        }
    }
    
    public func inputEvent(type: String, x: Int32, y: Int32, keyCode: UInt32) {
        log(.debug, category: .input, code: "INPUT_EVENT", message: "INPUT_EVENT", detail: "type=\(type) x=\(x) y=\(y) keyCode=\(keyCode)")
    }
    
    public func audioInit(sampleRate: Double, channels: Int, success: Bool) {
        if success {
            log(.info, category: .audio, code: "AUDIO_INIT_SUCCESS", message: "AUDIO_INIT_SUCCESS", detail: "sampleRate=\(sampleRate) channels=\(channels)")
        } else {
            log(.warning, category: .audio, code: "AUDIO_INIT_FAIL", message: "AUDIO_INIT_FAIL", detail: "sampleRate=\(sampleRate)")
        }
    }
    
    public func processCreated(pid: UInt32, exe: String, cmdline: String) {
        log(.info, category: .process, code: "PROCESS_CREATED", message: "PROCESS_CREATED", detail: "pid=\(pid) exe=\(exe) cmdline=\(cmdline)")
    }
    
    public func processExit(pid: UInt32, code: UInt32, uptime: Double) {
        log(.info, category: .process, code: "PROCESS_EXIT", message: "PROCESS_EXIT", detail: "pid=\(pid) exitCode=\(code) uptime=\(String(format: "%.2f", uptime))s")
    }
    
    public func filesystemAccess(path: String, hostPath: String, status: String) {
        log(.debug, category: .filesystem, code: "FS_ACCESS", message: "FS_ACCESS", detail: "winPath=\(path) hostPath=\(hostPath) status=\(status)")
    }
    
    public func compatibilityReport(score: Double, level: String, missing: [String], unimplemented: [String]) {
        log(.info, category: .compatibility, code: "COMPAT_REPORT", message: "COMPAT_REPORT", detail: "score=\(String(format: "%.1f", score))% level=\(level) missing=\(missing.count) unimplemented=\(unimplemented.count)")
    }
    
    // MARK: - Report generation
    
    public func generateReport() -> String {
        var report = ""
        report += "# WINOS GAME RUNTIME — DIAGNOSTICS REPORT\n"
        report += "Generated: \(Date())\n"
        report += "Uptime: \(String(format: "%.2f", Date().timeIntervalSince(startTime)))s\n"
        report += "Total entries: \(entries.count)\n"
        report += "Missing DLLs: \(missingDLLs.count) — \(missingDLLs.sorted().joined(separator: ", "))\n"
        report += "Missing APIs: \(missingAPIs.count) — \(missingAPIs.sorted().prefix(10).joined(separator: ", "))\n"
        report += "Unimplemented: \(unimplementedAPIs.count) — \(unimplementedAPIs.sorted().prefix(10).joined(separator: ", "))\n"
        report += "Graphics APIs: \(graphicsAPIs.sorted().joined(separator: ", "))\n"
        report += "\n## Entries\n"
        for e in entries {
            report += e.formatted + "\n"
        }
        return report
    }
    
    public func summary() -> String {
        "Diagnostics: \(entries.count) entries, \(missingDLLs.count) missing DLLs, \(missingAPIs.count) missing APIs, \(unimplementedAPIs.count) unimplemented, \(graphicsAPIs.count) graphics APIs"
    }
}
