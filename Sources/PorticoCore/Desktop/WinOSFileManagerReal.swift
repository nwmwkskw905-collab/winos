import Foundation

/// FileManager REAL — usa VFS/Sandbox REAL, não lista fictícia
/// Mapeia Windows path → VFS → AppSandbox → iOS filesystem

public struct WinOSFileItem: Equatable, Sendable, Identifiable {
    public var id: String { path }
    public var name: String
    public var path: String // relativo ao sandbox root, ex: "Environments/env-XXX/drive_c/Games"
    public var windowsPath: String // ex: "C:\Games"
    public var isDirectory: Bool
    public var size: Int64
    public var modifiedDate: Date
    public var ext: String
    public var isExecutable: Bool
    
    public var displaySize: String {
        if isDirectory { return "--" }
        if size < 1024 { return "\(size) B" }
        if size < 1024*1024 { return String(format: "%.1f KB", Double(size)/1024) }
        if size < 1024*1024*1024 { return String(format: "%.1f MB", Double(size)/1024/1024) }
        return String(format: "%.2f GB", Double(size)/1024/1024/1024)
    }
}

public enum WinOSFileOperation: String, Sendable {
    case navigate, createFolder, rename, delete, copy, move, open
}

@MainActor
public final class WinOSFileManagerReal: ObservableObject {
    @Published public private(set) var currentWindowsPath: String = "C:\\"
    @Published public private(set) var currentItems: [WinOSFileItem] = []
    @Published public private(set) var history: [String] = ["C:\\"]
    @Published public private(set) var historyIndex: Int = 0
    @Published public private(set) var selectedItems: Set<String> = []
    @Published public private(set) var lastError: String = ""
    @Published public private(set) var isLoading: Bool = false
    
    private let sandbox: AppSandbox
    private let log: LogCenter
    
    // Mapeamento Windows → Sandbox
    // C:\ → Environments/env-XXX/drive_c
    // C:\Windows → Environments/env-XXX/drive_c/Windows
    // C:\Program Files → Environments/env-XXX/drive_c/Program Files
    // C:\Games → Games (global) ou Environments/.../drive_c/Games
    private var pcRoot: String = "Environments" // será setado para env específico
    private var driveCRoot: String = ""
    
    public init(sandbox: AppSandbox, log: LogCenter, pcPath: String = "") {
        self.sandbox = sandbox
        self.log = log
        self.pcRoot = pcPath.isEmpty ? "Environments" : pcPath
        self.driveCRoot = pcRoot + "/drive_c"
        NSLog("[WINOS-FM] FileManagerReal init pcRoot=%@ driveC=%@", pcRoot, driveCRoot)
    }
    
    public func setPCRoot(_ pcPath: String) {
        self.pcRoot = pcPath
        self.driveCRoot = pcPath + "/drive_c"
        NSLog("[WINOS-FM] setPCRoot pc=%@ driveC=%@", pcRoot, driveCRoot)
        // Garante estrutura C: inicial
        ensureDriveCStructure()
        navigateToWindowsPath("C:\\")
    }
    
    private func ensureDriveCStructure() {
        let fm = FileManager.default
        let dirs = [
            driveCRoot,
            driveCRoot + "/Windows",
            driveCRoot + "/Program Files",
            driveCRoot + "/ProgramData",
            driveCRoot + "/Users",
            driveCRoot + "/Users/usuario",
            driveCRoot + "/Users/usuario/Documents",
            driveCRoot + "/Games",
            driveCRoot + "/Temp"
        ]
        for rel in dirs {
            if let url = try? sandbox.resolveInside(rel) {
                try? fm.createDirectory(at: url, withIntermediateDirectories: true)
            }
        }
        NSLog("[WINOS-FM] ensureDriveCStructure OK for %@", driveCRoot)
    }
    
    // MARK: - Path mapping real
    
    /// Windows path → Sandbox relative path
    public func windowsToSandboxPath(_ winPath: String) -> String {
        var p = winPath.trimmingCharacters(in: .whitespacesAndNewlines)
        // Normaliza
        p = p.replacingOccurrences(of: "/", with: "\\")
        // Remove drive
        if p.hasPrefix("C:\\") || p.hasPrefix("C:/") {
            p = String(p.dropFirst(3))
        } else if p == "C:" || p == "C:\\" || p == "C:/" || p.isEmpty {
            p = ""
        }
        // Remove leading \
        while p.hasPrefix("\\") { p = String(p.dropFirst()) }
        // Trata . e ..
        var components: [String] = []
        for comp in p.split(separator: "\\") {
            let c = String(comp)
            if c == "." || c.isEmpty { continue }
            if c == ".." {
                if !components.isEmpty { components.removeLast() }
                continue
            }
            components.append(c)
        }
        let joined = components.joined(separator: "/")
        if joined.isEmpty {
            return driveCRoot
        }
        return driveCRoot + "/" + joined
    }
    
    /// Sandbox relative → Windows path
    public func sandboxToWindowsPath(_ sandboxRel: String) -> String {
        var rel = sandboxRel
        if rel.hasPrefix(driveCRoot) {
            rel = String(rel.dropFirst(driveCRoot.count))
        }
        rel = rel.replacingOccurrences(of: "/", with: "\\")
        if rel.isEmpty { return "C:\\" }
        if !rel.hasPrefix("\\") { rel = "\\" + rel }
        return "C:" + rel
    }
    
    public func resolveWindowsPath(_ winPath: String) throws -> URL {
        let sandboxRel = windowsToSandboxPath(winPath)
        return try sandbox.resolveInside(sandboxRel)
    }
    
    // MARK: - Navigation real
    
    public func navigateToWindowsPath(_ winPath: String) {
        isLoading = true
        defer { isLoading = false }
        
        let normalized = normalizeWindowsPath(winPath)
        NSLog("[WINOS-FM] navigateToWindowsPath win=%@ normalized=%@ sandbox=%@", winPath, normalized, windowsToSandboxPath(normalized))
        
        do {
            let url = try resolveWindowsPath(normalized)
            let fm = FileManager.default
            var isDir: ObjCBool = false
            guard fm.fileExists(atPath: url.path, isDirectory: &isDir), isDir.boolValue else {
                lastError = "Diretório não encontrado: \(normalized)"
                NSLog("[WINOS-FM] navigate FAIL not found: %@ -> %@", normalized, url.path)
                return
            }
            // Lista real
            let contents = try fm.contentsOfDirectory(at: url, includingPropertiesForKeys: [.isDirectoryKey, .fileSizeKey, .contentModificationDateKey])
            var items: [WinOSFileItem] = []
            for fileURL in contents {
                let vals = try? fileURL.resourceValues(forKeys: [.isDirectoryKey, .fileSizeKey, .contentModificationDateKey])
                let isDir = vals?.isDirectory ?? false
                let size = Int64(vals?.fileSize ?? 0)
                let mod = vals?.contentModificationDate ?? Date()
                let name = fileURL.lastPathComponent
                let ext = (name as NSString).pathExtension.lowercased()
                let isExe = ext == "exe" || ext == "dll" || ext == "bat" || ext == "cmd"
                let sandboxRel = windowsToSandboxPath(normalized) + "/" + name
                let winP = sandboxToWindowsPath(sandboxRel)
                // Normaliza winP
                let item = WinOSFileItem(name: name, path: sandboxRel, windowsPath: winP, isDirectory: isDir, size: size, modifiedDate: mod, ext: ext, isExecutable: isExe)
                items.append(item)
            }
            items.sort { (a, b) in
                if a.isDirectory != b.isDirectory { return a.isDirectory && !b.isDirectory }
                return a.name.lowercased() < b.name.lowercased()
            }
            currentItems = items
            currentWindowsPath = normalized
            // History
            if history.isEmpty || history[historyIndex] != normalized {
                // Remove frente se navegou após voltar
                if historyIndex < history.count - 1 {
                    history = Array(history[0...historyIndex])
                }
                history.append(normalized)
                historyIndex = history.count - 1
            }
            lastError = ""
            NSLog("[WINOS-FM] navigate SUCCESS win=%@ items=%d", normalized, items.count)
        } catch {
            lastError = "Erro ao navegar: \(error)"
            NSLog("[WINOS-FM] navigate ERROR win=%@ error=%@", normalized, "\(error)")
        }
    }
    
    public func navigateBack() {
        guard historyIndex > 0 else { return }
        historyIndex -= 1
        let path = history[historyIndex]
        navigateToWindowsPath(path)
        NSLog("[WINOS-FM] navigateBack to %@", path)
    }
    
    public func navigateForward() {
        guard historyIndex < history.count - 1 else { return }
        historyIndex += 1
        let path = history[historyIndex]
        navigateToWindowsPath(path)
        NSLog("[WINOS-FM] navigateForward to %@", path)
    }
    
    public func navigateUp() {
        let parent = parentWindowsPath(currentWindowsPath)
        navigateToWindowsPath(parent)
    }
    
    private func normalizeWindowsPath(_ path: String) -> String {
        var p = path.trimmingCharacters(in: .whitespacesAndNewlines)
        if p.isEmpty { return "C:\\" }
        p = p.replacingOccurrences(of: "/", with: "\\")
        // Garante C:\ prefix
        if !p.uppercased().hasPrefix("C:") {
            if p.hasPrefix("\\") {
                p = "C:" + p
            } else {
                p = "C:\\" + p
            }
        }
        // Remove trailing \ exceto root
        if p.count > 3 && p.hasSuffix("\\") {
            p = String(p.dropLast())
        }
        // Uppercase drive
        if p.uppercased().hasPrefix("C:") {
            p = "C:" + String(p.dropFirst(2))
        }
        return p
    }
    
    private func parentWindowsPath(_ path: String) -> String {
        let norm = normalizeWindowsPath(path)
        if norm == "C:\\" { return "C:\\" }
        var comps = norm.split(separator: "\\").map { String($0) }
        if comps.first?.uppercased() == "C:" { comps.removeFirst() }
        if comps.isEmpty { return "C:\\" }
        comps.removeLast()
        if comps.isEmpty { return "C:\\" }
        return "C:\\" + comps.joined(separator: "\\")
    }
    
    // MARK: - Operations real (filesystem)
    
    public func createFolder(name: String) {
        guard !name.isEmpty else { return }
        let newWinPath = currentWindowsPath == "C:\\" ? "C:\\\(name)" : "\(currentWindowsPath)\\\(name)"
        let sandboxRel = windowsToSandboxPath(newWinPath)
        do {
            let url = try sandbox.resolveInside(sandboxRel)
            try FileManager.default.createDirectory(at: url, withIntermediateDirectories: true)
            NSLog("[WINOS-FM] createFolder SUCCESS %@", newWinPath)
            navigateToWindowsPath(currentWindowsPath) // refresh
        } catch {
            lastError = "Falha ao criar pasta: \(error)"
            NSLog("[WINOS-FM] createFolder FAIL %@ error=%@", newWinPath, "\(error)")
        }
    }
    
    public func deleteItem(_ item: WinOSFileItem) {
        do {
            let url = try sandbox.resolveInside(item.path)
            try FileManager.default.removeItem(at: url)
            NSLog("[WINOS-FM] deleteItem SUCCESS %@", item.windowsPath)
            navigateToWindowsPath(currentWindowsPath)
        } catch {
            lastError = "Falha ao excluir: \(error)"
            NSLog("[WINOS-FM] deleteItem FAIL %@ error=%@", item.windowsPath, "\(error)")
        }
    }
    
    public func renameItem(_ item: WinOSFileItem, newName: String) {
        guard !newName.isEmpty else { return }
        let parentWin = parentWindowsPath(item.windowsPath)
        let newWinPath = parentWin == "C:\\" ? "C:\\\(newName)" : "\(parentWin)\\\(newName)"
        let newSandboxRel = windowsToSandboxPath(newWinPath)
        do {
            let src = try sandbox.resolveInside(item.path)
            let dst = try sandbox.resolveInside(newSandboxRel)
            try FileManager.default.moveItem(at: src, to: dst)
            NSLog("[WINOS-FM] renameItem SUCCESS %@ -> %@", item.windowsPath, newWinPath)
            navigateToWindowsPath(currentWindowsPath)
        } catch {
            lastError = "Falha ao renomear: \(error)"
            NSLog("[WINOS-FM] renameItem FAIL error=%@", "\(error)")
        }
    }
    
    // MARK: - Selection
    
    public func selectItem(_ item: WinOSFileItem) {
        selectedItems.insert(item.path)
    }
    
    public func deselectItem(_ item: WinOSFileItem) {
        selectedItems.remove(item.path)
    }
    
    public func clearSelection() {
        selectedItems.removeAll()
    }
    
    // MARK: - Tests
    
    public func testPaths() -> [(String, Bool, String)] {
        let tests = [
            "C:\\",
            "C:\\Windows",
            "C:\\Games",
            "C:\\Users",
            "C:\\Program Files",
            "C:\\NonExistent12345",
            "C:\\Games\\Test\\game.exe"
        ]
        var results: [(String, Bool, String)] = []
        for winPath in tests {
            let sandboxRel = windowsToSandboxPath(winPath)
            do {
                let url = try sandbox.resolveInside(sandboxRel)
                let exists = FileManager.default.fileExists(atPath: url.path)
                results.append((winPath, exists, url.path))
            } catch {
                results.append((winPath, false, "INVALID: \(error)"))
            }
        }
        return results
    }
}
