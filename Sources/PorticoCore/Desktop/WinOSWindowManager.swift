import Foundation

/// WindowManager REAL — central, controla z-order, foco, lifecycle
/// Cada janela tem ID, PID, title, geometria, estado, surface
/// NÃO é simulação visual — é estado real que alimenta Win32 e render

public struct WinOSWindowID: Hashable, Equatable, Sendable, Codable {
    public let raw: UInt32
    public init(_ raw: UInt32) { self.raw = raw }
    public static let desktop = WinOSWindowID(0)
    public static let invalid = WinOSWindowID(UInt32.max)
}

public struct WinOSWindow: Equatable, Sendable, Identifiable {
    public var id: WinOSWindowID
    public var processID: UInt32
    public var title: String
    public var x: Int32
    public var y: Int32
    public var width: Int32
    public var height: Int32
    public var minWidth: Int32 = 200
    public var minHeight: Int32 = 100
    public var maxWidth: Int32 = 4096
    public var maxHeight: Int32 = 4096
    public var visible: Bool = true
    public var minimized: Bool = false
    public var maximized: Bool = false
    public var focused: Bool = false
    public var zIndex: Int32 = 0
    public var parent: WinOSWindowID? = nil
    public var children: [WinOSWindowID] = []
    public var isFileManager: Bool = false
    public var isTaskManager: Bool = false
    public var executablePath: String = ""
    public var lastPaintTime: Double = 0
    public var dirty: Bool = true
    
    // Geometria salva para restore
    public var restoreX: Int32 = 0
    public var restoreY: Int32 = 0
    public var restoreWidth: Int32 = 0
    public var restoreHeight: Int32 = 0
    
    public init(id: WinOSWindowID, processID: UInt32, title: String, x: Int32, y: Int32, width: Int32, height: Int32) {
        self.id = id
        self.processID = processID
        self.title = title
        self.x = x
        self.y = y
        self.width = width
        self.height = height
        self.restoreX = x
        self.restoreY = y
        self.restoreWidth = width
        self.restoreHeight = height
    }
    
    public var rect: (x: Int32, y: Int32, w: Int32, h: Int32) {
        (x, y, width, height)
    }
    
    public func contains(pointX: Int32, pointY: Int32) -> Bool {
        guard visible && !minimized else { return false }
        return pointX >= x && pointX < x + width && pointY >= y && pointY < y + height
    }
    
    public var clientRect: (x: Int32, y: Int32, w: Int32, h: Int32) {
        // Área cliente sem borda/título (simplificado: 2px borda, 28px título)
        let titleBar: Int32 = 28
        let border: Int32 = 2
        return (x + border, y + titleBar, max(0, width - border*2), max(0, height - titleBar - border))
    }
}

public enum WinOSWindowEvent: Equatable, Sendable {
    case created(WinOSWindow)
    case destroyed(WinOSWindowID)
    case moved(WinOSWindowID, x: Int32, y: Int32)
    case resized(WinOSWindowID, width: Int32, height: Int32)
    case focused(WinOSWindowID)
    case minimized(WinOSWindowID)
    case maximized(WinOSWindowID)
    case restored(WinOSWindowID)
    case shown(WinOSWindowID)
    case hidden(WinOSWindowID)
    case titleChanged(WinOSWindowID, String)
    case zOrderChanged
}

/// WindowManager central — thread-safe via @MainActor para UI, mas lógica pode ser chamada de background com locks
@MainActor
public final class WinOSWindowManager: ObservableObject {
    @Published public private(set) var windows: [WinOSWindow] = []
    @Published public private(set) var focusedWindowID: WinOSWindowID? = nil
    @Published public private(set) var zOrder: [WinOSWindowID] = [] // menor índice = atrás, maior = frente
    
    private var nextID: UInt32 = 1
    private var nextZ: Int32 = 1
    
    public var onEvent: ((WinOSWindowEvent) -> Void)?
    
    public init() {
        NSLog("[WINOS-WM] WindowManager init")
    }
    
    // MARK: - Lifecycle real
    
    @discardableResult
    public func createWindow(processID: UInt32, title: String, x: Int32, y: Int32, width: Int32, height: Int32, isFileManager: Bool = false, executablePath: String = "") -> WinOSWindow {
        let id = WinOSWindowID(nextID)
        nextID += 1
        let z = nextZ
        nextZ += 1
        
        var win = WinOSWindow(id: id, processID: processID, title: title, x: x, y: y, width: width, height: height)
        win.zIndex = z
        win.isFileManager = isFileManager
        win.executablePath = executablePath
        
        windows.append(win)
        zOrder.append(id)
        focusedWindowID = id
        // Atualiza foco
        for i in windows.indices {
            windows[i].focused = windows[i].id == id
        }
        
        NSLog("[WINOS-WM] createWindow id=%u pid=%u title=%@ x=%d y=%d w=%d h=%d z=%d", id.raw, processID, title, x, y, width, height, z)
        onEvent?(.created(win))
        onEvent?(.focused(id))
        onEvent?(.zOrderChanged)
        return win
    }
    
    public func destroyWindow(id: WinOSWindowID) {
        guard let idx = windows.firstIndex(where: { $0.id == id }) else {
            NSLog("[WINOS-WM] destroyWindow FAIL id=%u not found", id.raw)
            return
        }
        let win = windows[idx]
        windows.remove(at: idx)
        zOrder.removeAll(where: { $0 == id })
        if focusedWindowID == id {
            focusedWindowID = zOrder.last
            if let newFocus = focusedWindowID {
                if let fIdx = windows.firstIndex(where: { $0.id == newFocus }) {
                    for i in windows.indices { windows[i].focused = false }
                    windows[fIdx].focused = true
                }
            }
        }
        NSLog("[WINOS-WM] destroyWindow id=%u title=%@", id.raw, win.title)
        onEvent?(.destroyed(id))
        onEvent?(.zOrderChanged)
    }
    
    public func showWindow(id: WinOSWindowID) {
        guard let idx = windows.firstIndex(where: { $0.id == id }) else { return }
        windows[idx].visible = true
        windows[idx].dirty = true
        NSLog("[WINOS-WM] showWindow id=%u", id.raw)
        onEvent?(.shown(id))
    }
    
    public func hideWindow(id: WinOSWindowID) {
        guard let idx = windows.firstIndex(where: { $0.id == id }) else { return }
        windows[idx].visible = false
        NSLog("[WINOS-WM] hideWindow id=%u", id.raw)
        onEvent?(.hidden(id))
    }
    
    public func moveWindow(id: WinOSWindowID, x: Int32, y: Int32) {
        guard let idx = windows.firstIndex(where: { $0.id == id }) else { return }
        windows[idx].x = x
        windows[idx].y = y
        windows[idx].dirty = true
        NSLog("[WINOS-WM] moveWindow id=%u x=%d y=%d", id.raw, x, y)
        onEvent?(.moved(id, x: x, y: y))
    }
    
    public func resizeWindow(id: WinOSWindowID, width: Int32, height: Int32) {
        guard let idx = windows.firstIndex(where: { $0.id == id }) else { return }
        let w = max(windows[idx].minWidth, min(windows[idx].maxWidth, width))
        let h = max(windows[idx].minHeight, min(windows[idx].maxHeight, height))
        windows[idx].width = w
        windows[idx].height = h
        windows[idx].dirty = true
        NSLog("[WINOS-WM] resizeWindow id=%u w=%d h=%d", id.raw, w, h)
        onEvent?(.resized(id, width: w, height: h))
    }
    
    public func minimizeWindow(id: WinOSWindowID) {
        guard let idx = windows.firstIndex(where: { $0.id == id }) else { return }
        if !windows[idx].minimized {
            windows[idx].restoreX = windows[idx].x
            windows[idx].restoreY = windows[idx].y
            windows[idx].restoreWidth = windows[idx].width
            windows[idx].restoreHeight = windows[idx].height
        }
        windows[idx].minimized = true
        windows[idx].maximized = false
        windows[idx].focused = false
        zOrder.removeAll(where: { $0 == id })
        zOrder.insert(id, at: 0) // vai para trás
        focusedWindowID = zOrder.last(where: { wid in
            guard let w = windows.first(where: { $0.id == wid }) else { return false }
            return !w.minimized && w.visible
        })
        if let fid = focusedWindowID, let fIdx = windows.firstIndex(where: { $0.id == fid }) {
            for i in windows.indices { windows[i].focused = false }
            windows[fIdx].focused = true
        }
        NSLog("[WINOS-WM] minimizeWindow id=%u", id.raw)
        onEvent?(.minimized(id))
        onEvent?(.zOrderChanged)
    }
    
    public func maximizeWindow(id: WinOSWindowID, desktopWidth: Int32 = 1920, desktopHeight: Int32 = 1080) {
        guard let idx = windows.firstIndex(where: { $0.id == id }) else { return }
        if !windows[idx].maximized {
            windows[idx].restoreX = windows[idx].x
            windows[idx].restoreY = windows[idx].y
            windows[idx].restoreWidth = windows[idx].width
            windows[idx].restoreHeight = windows[idx].height
        }
        windows[idx].x = 0
        windows[idx].y = 0
        windows[idx].width = desktopWidth
        windows[idx].height = desktopHeight - 48 // taskbar
        windows[idx].maximized = true
        windows[idx].minimized = false
        windows[idx].dirty = true
        focusWindow(id: id)
        NSLog("[WINOS-WM] maximizeWindow id=%u w=%d h=%d", id.raw, desktopWidth, desktopHeight)
        onEvent?(.maximized(id))
    }
    
    public func restoreWindow(id: WinOSWindowID) {
        guard let idx = windows.firstIndex(where: { $0.id == id }) else { return }
        if windows[idx].minimized || windows[idx].maximized {
            windows[idx].x = windows[idx].restoreX
            windows[idx].y = windows[idx].restoreY
            windows[idx].width = windows[idx].restoreWidth
            windows[idx].height = windows[idx].restoreHeight
            windows[idx].minimized = false
            windows[idx].maximized = false
            windows[idx].dirty = true
            focusWindow(id: id)
            NSLog("[WINOS-WM] restoreWindow id=%u", id.raw)
            onEvent?(.restored(id))
        }
    }
    
    public func focusWindow(id: WinOSWindowID) {
        guard let idx = windows.firstIndex(where: { $0.id == id }) else { return }
        guard !windows[idx].minimized else {
            restoreWindow(id: id)
            return
        }
        for i in windows.indices { windows[i].focused = false }
        windows[idx].focused = true
        focusedWindowID = id
        // z-order: move para frente
        zOrder.removeAll(where: { $0 == id })
        zOrder.append(id)
        // Atualiza zIndex
        for (z, wid) in zOrder.enumerated() {
            if let wi = windows.firstIndex(where: { $0.id == wid }) {
                windows[wi].zIndex = Int32(z)
            }
        }
        NSLog("[WINOS-WM] focusWindow id=%u z=%d", id.raw, windows[idx].zIndex)
        onEvent?(.focused(id))
        onEvent?(.zOrderChanged)
    }
    
    public func setWindowTitle(id: WinOSWindowID, title: String) {
        guard let idx = windows.firstIndex(where: { $0.id == id }) else { return }
        windows[idx].title = title
        NSLog("[WINOS-WM] setWindowTitle id=%u title=%@", id.raw, title)
        onEvent?(.titleChanged(id, title))
    }
    
    // MARK: - Hit testing real
    
    public func windowAt(pointX: Int32, pointY: Int32) -> WinOSWindow? {
        // Maior z primeiro (frente)
        for wid in zOrder.reversed() {
            if let win = windows.first(where: { $0.id == wid }), win.contains(pointX: pointX, pointY: pointY) {
                return win
            }
        }
        return nil
    }
    
    public func titleBarHitTest(window: WinOSWindow, pointX: Int32, pointY: Int32) -> Bool {
        guard window.contains(pointX: pointX, pointY: pointY) else { return false }
        let titleBarHeight: Int32 = 28
        return pointY >= window.y && pointY < window.y + titleBarHeight
    }
    
    public func closeButtonHitTest(window: WinOSWindow, pointX: Int32, pointY: Int32) -> Bool {
        let btnSize: Int32 = 28
        let btnX = window.x + window.width - btnSize
        let btnY = window.y
        return pointX >= btnX && pointX < btnX + btnSize && pointY >= btnY && pointY < btnY + btnSize
    }
    
    public func minimizeButtonHitTest(window: WinOSWindow, pointX: Int32, pointY: Int32) -> Bool {
        let btnSize: Int32 = 28
        let btnX = window.x + window.width - btnSize*3
        let btnY = window.y
        return pointX >= btnX && pointX < btnX + btnSize && pointY >= btnY && pointY < btnY + btnSize
    }
    
    public func maximizeButtonHitTest(window: WinOSWindow, pointX: Int32, pointY: Int32) -> Bool {
        let btnSize: Int32 = 28
        let btnX = window.x + window.width - btnSize*2
        let btnY = window.y
        return pointX >= btnX && pointX < btnX + btnSize && pointY >= btnY && pointY < btnY + btnSize
    }
    
    // MARK: - State queries
    
    public func visibleWindows() -> [WinOSWindow] {
        windows.filter { $0.visible && !$0.minimized }.sorted { $0.zIndex < $1.zIndex }
    }
    
    public func allWindows() -> [WinOSWindow] {
        windows.sorted { $0.zIndex < $1.zIndex }
    }
    
    public func window(id: WinOSWindowID) -> WinOSWindow? {
        windows.first(where: { $0.id == id })
    }
    
    public func focusedWindow() -> WinOSWindow? {
        guard let fid = focusedWindowID else { return nil }
        return windows.first(where: { $0.id == fid })
    }
    
    // MARK: - Cleanup
    
    public func destroyAllWindowsForProcess(pid: UInt32) {
        let toDestroy = windows.filter { $0.processID == pid }.map { $0.id }
        for wid in toDestroy {
            destroyWindow(id: wid)
        }
        NSLog("[WINOS-WM] destroyAllWindowsForProcess pid=%u count=%d", pid, toDestroy.count)
    }
    
    public func reset() {
        windows.removeAll()
        zOrder.removeAll()
        focusedWindowID = nil
        nextID = 1
        nextZ = 1
        NSLog("[WINOS-WM] reset")
        onEvent?(.zOrderChanged)
    }
}
