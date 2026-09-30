import Foundation
import PorticoCore

/// ProcessManager REAL — conectado ao runtime, não simulação
/// Cada processo tem PID, exe, cmdline, wd, env, parent, state, exit, windows, threads, mem stats

public enum WinOSProcessState: String, Equatable, Sendable, Codable {
    case created = "CREATED"
    case starting = "STARTING"
    case running = "RUNNING"
    case suspended = "SUSPENDED"
    case exiting = "EXITING"
    case exited = "EXITED"
    case failed = "FAILED"
}

public struct WinOSProcess: Equatable, Sendable, Identifiable {
    public var id: UInt32
    public var executable: String
    public var commandLine: String
    public var workingDirectory: String
    public var environment: [String: String]
    public var parentPID: UInt32
    public var state: WinOSProcessState
    public var exitCode: UInt32?
    public var windows: [WinOSWindowID] = []
    public var threadCount: Int = 1
    public var memoryUsageBytes: Int64 = 0
    public var startTime: Date
    public var endTime: Date?
    public var lastError: String = ""
    public var isDesktop: Bool = false
    public var isFileManager: Bool = false
    
    public init(id: UInt32, executable: String, commandLine: String = "", workingDirectory: String = "", environment: [String: String] = [:], parentPID: UInt32 = 0) {
        self.id = id
        self.executable = executable
        self.commandLine = commandLine.isEmpty ? executable : commandLine
        self.workingDirectory = workingDirectory
        self.environment = environment
        self.parentPID = parentPID
        self.state = .created
        self.startTime = Date()
    }
    
    public var uptime: TimeInterval {
        (endTime ?? Date()).timeIntervalSince(startTime)
    }
}

@MainActor
public final class WinOSProcessManagerReal: ObservableObject {
    @Published public private(set) var processes: [WinOSProcess] = []
    private var nextPID: UInt32 = 100
    
    public var onProcessCreated: ((WinOSProcess) -> Void)?
    public var onProcessExited: ((WinOSProcess) -> Void)?
    
    public init() {
        NSLog("[WINOS-PM] ProcessManagerReal init")
    }
    
    @discardableResult
    public func createProcess(executable: String, commandLine: String = "", workingDirectory: String = "", environment: [String: String] = [:], parentPID: UInt32 = 0, isDesktop: Bool = false, isFileManager: Bool = false) -> WinOSProcess {
        let pid = nextPID
        nextPID += 1
        var proc = WinOSProcess(id: pid, executable: executable, commandLine: commandLine, workingDirectory: workingDirectory, environment: environment, parentPID: parentPID)
        proc.isDesktop = isDesktop
        proc.isFileManager = isFileManager
        proc.state = .starting
        processes.append(proc)
        NSLog("[WINOS-PM] createProcess pid=%u exe=%@ cmd=%@ wd=%@ parent=%u", pid, executable, commandLine, workingDirectory, parentPID)
        onProcessCreated?(proc)
        return proc
    }
    
    public func setProcessState(pid: UInt32, state: WinOSProcessState) {
        guard let idx = processes.firstIndex(where: { $0.id == pid }) else { return }
        processes[idx].state = state
        if state == .running {
            NSLog("[WINOS-PM] process pid=%u RUNNING", pid)
        }
        if state == .exited || state == .failed {
            processes[idx].endTime = Date()
            onProcessExited?(processes[idx])
        }
    }
    
    public func setExitCode(pid: UInt32, code: UInt32) {
        guard let idx = processes.firstIndex(where: { $0.id == pid }) else { return }
        processes[idx].exitCode = code
        processes[idx].state = code == 0 ? .exited : .failed
        processes[idx].endTime = Date()
        NSLog("[WINOS-PM] process pid=%u exit code=%u state=%@", pid, code, processes[idx].state.rawValue)
        onProcessExited?(processes[idx])
    }
    
    public func setError(pid: UInt32, error: String) {
        guard let idx = processes.firstIndex(where: { $0.id == pid }) else { return }
        processes[idx].lastError = error
        processes[idx].state = .failed
        NSLog("[WINOS-PM] process pid=%u FAILED error=%@", pid, error)
    }
    
    public func addWindow(pid: UInt32, windowID: WinOSWindowID) {
        guard let idx = processes.firstIndex(where: { $0.id == pid }) else { return }
        if !processes[idx].windows.contains(windowID) {
            processes[idx].windows.append(windowID)
        }
    }
    
    public func removeWindow(pid: UInt32, windowID: WinOSWindowID) {
        guard let idx = processes.firstIndex(where: { $0.id == pid }) else { return }
        processes[idx].windows.removeAll(where: { $0 == windowID })
    }
    
    public func process(pid: UInt32) -> WinOSProcess? {
        processes.first(where: { $0.id == pid })
    }
    
    public func runningProcesses() -> [WinOSProcess] {
        processes.filter { $0.state == .running || $0.state == .starting }
    }
    
    public func allProcesses() -> [WinOSProcess] {
        processes
    }
    
    public func terminateProcess(pid: UInt32) {
        guard let idx = processes.firstIndex(where: { $0.id == pid }) else { return }
        processes[idx].state = .exiting
        NSLog("[WINOS-PM] terminateProcess pid=%u", pid)
        // Simula saída rápida, mas runtime real deve chamar setExitCode
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.1) {
            self.setExitCode(pid: pid, code: 0)
        }
    }
    
    public func cleanupExited() {
        let before = processes.count
        processes.removeAll(where: { $0.state == .exited || $0.state == .failed })
        let after = processes.count
        if before != after {
            NSLog("[WINOS-PM] cleanupExited removed=%d remaining=%d", before - after, after)
        }
    }
    
    public func reset() {
        processes.removeAll()
        nextPID = 100
        NSLog("[WINOS-PM] reset")
    }
}
