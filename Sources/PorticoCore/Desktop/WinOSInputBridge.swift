import Foundation

/// InputBridge REAL — mapeia Touch → Desktop coords → WindowManager hit testing → Win32 message
/// Fluxo: input iOS → InputBridge → Win32 message queue → GetMessage/PeekMessage → TranslateMessage → DispatchMessage → Window proc → Window state → Render invalidation

public enum WinOSInputType: String, Sendable {
    case mouseMove, mouseDown, mouseUp, leftButton, rightButton, keyDown, keyUp, textInput, touch, gameController
}

public struct WinOSInputEvent: Sendable {
    public var type: WinOSInputType
    public var x: Int32
    public var y: Int32
    public var button: Int32 // 0=left, 1=right, 2=middle
    public var keyCode: UInt32
    public var char: String
    public var timestamp: Double
    
    public init(type: WinOSInputType, x: Int32 = 0, y: Int32 = 0, button: Int32 = 0, keyCode: UInt32 = 0, char: String = "", timestamp: Double = 0) {
        self.type = type
        self.x = x
        self.y = y
        self.button = button
        self.keyCode = keyCode
        self.char = char
        self.timestamp = timestamp == 0 ? CACurrentMediaTime() : timestamp
    }
}

public enum Win32Message: UInt32, Sendable {
    case wmMouseMove = 0x0200
    case wmLButtonDown = 0x0201
    case wmLButtonUp = 0x0202
    case wmRButtonDown = 0x0204
    case wmRButtonUp = 0x0205
    case wmMButtonDown = 0x0207
    case wmMButtonUp = 0x0208
    case wmKeyDown = 0x0100
    case wmKeyUp = 0x0101
    case wmChar = 0x0102
    case wmPaint = 0x000F
    case wmSize = 0x0005
    case wmClose = 0x0010
    case wmDestroy = 0x0002
    case wmSetFocus = 0x0007
    case wmKillFocus = 0x0008
    case wmShowWindow = 0x0018
    case wmMove = 0x0003
    case wmTimer = 0x0113
    case wmQuit = 0x0012
    case wmEraseBkgnd = 0x0014
}

public struct Win32MessageEntry: Sendable {
    public var hwnd: UInt32
    public var message: Win32Message
    public var wParam: UInt32
    public var lParam: UInt32
    public var time: UInt32
    public var x: Int32
    public var y: Int32
    
    public init(hwnd: UInt32, message: Win32Message, wParam: UInt32 = 0, lParam: UInt32 = 0, x: Int32 = 0, y: Int32 = 0) {
        self.hwnd = hwnd
        self.message = message
        self.wParam = wParam
        self.lParam = lParam
        self.x = x
        self.y = y
        self.time = UInt32(CACurrentMediaTime() * 1000)
    }
}

@MainActor
public final class WinOSInputBridge: ObservableObject {
    @Published public private(set) var lastMouseX: Int32 = 0
    @Published public private(set) var lastMouseY: Int32 = 0
    @Published public private(set) var isLeftDown: Bool = false
    @Published public private(set) var isRightDown: Bool = false
    
    private var messageQueue: [Win32MessageEntry] = []
    private let maxQueueSize = 1024
    
    // DPI / Scale
    private var logicalWidth: Int32 = 1920
    private var logicalHeight: Int32 = 1080
    private var physicalWidth: Int32 = 1920
    private var physicalHeight: Int32 = 1080
    private var scale: Double = 1.0
    
    public var onMessage: ((Win32MessageEntry) -> Void)?
    
    public init() {
        NSLog("[WINOS-INPUT-BRIDGE] init")
    }
    
    public func setDesktopSize(logicalWidth: Int32, logicalHeight: Int32, physicalWidth: Int32, physicalHeight: Int32) {
        self.logicalWidth = logicalWidth
        self.logicalHeight = logicalHeight
        self.physicalWidth = physicalWidth
        self.physicalHeight = physicalHeight
        self.scale = Double(physicalWidth) / Double(logicalWidth)
        NSLog("[WINOS-INPUT-BRIDGE] setDesktopSize logical=%dx%d physical=%dx%d scale=%.2f", logicalWidth, logicalHeight, physicalWidth, physicalHeight, scale)
    }
    
    // MARK: - Coordinate scaling
    
    public func physicalToLogical(physicalX: Double, physicalY: Double) -> (Int32, Int32) {
        let lx = Int32(physicalX / scale)
        let ly = Int32(physicalY / scale)
        return (lx, ly)
    }
    
    public func logicalToPhysical(logicalX: Int32, logicalY: Int32) -> (Double, Double) {
        let px = Double(logicalX) * scale
        let py = Double(logicalY) * scale
        return (px, py)
    }
    
    // MARK: - Input real
    
    public func handleTouchBegan(x: Double, y: Double, windowID: UInt32) {
        let (lx, ly) = physicalToLogical(physicalX: x, physicalY: y)
        lastMouseX = lx
        lastMouseY = ly
        isLeftDown = true
        
        // WM_LBUTTONDOWN
        let lParam = UInt32((UInt32(ly) << 16) | UInt32(lx & 0xFFFF))
        let msg = Win32MessageEntry(hwnd: windowID, message: .wmLButtonDown, wParam: 1, lParam: lParam, x: lx, y: ly)
        enqueueMessage(msg)
        NSLog("[WINOS-INPUT-BRIDGE] touchBegan physical=%.0f,%.0f logical=%d,%d hwnd=%u", x, y, lx, ly, windowID)
    }
    
    public func handleTouchMoved(x: Double, y: Double, windowID: UInt32) {
        let (lx, ly) = physicalToLogical(physicalX: x, physicalY: y)
        lastMouseX = lx
        lastMouseY = ly
        
        let lParam = UInt32((UInt32(ly) << 16) | UInt32(lx & 0xFFFF))
        let msg = Win32MessageEntry(hwnd: windowID, message: .wmMouseMove, wParam: isLeftDown ? 1 : 0, lParam: lParam, x: lx, y: ly)
        enqueueMessage(msg)
    }
    
    public func handleTouchEnded(x: Double, y: Double, windowID: UInt32) {
        let (lx, ly) = physicalToLogical(physicalX: x, physicalY: y)
        lastMouseX = lx
        lastMouseY = ly
        isLeftDown = false
        
        let lParam = UInt32((UInt32(ly) << 16) | UInt32(lx & 0xFFFF))
        let msg = Win32MessageEntry(hwnd: windowID, message: .wmLButtonUp, wParam: 0, lParam: lParam, x: lx, y: ly)
        enqueueMessage(msg)
        NSLog("[WINOS-INPUT-BRIDGE] touchEnded logical=%d,%d hwnd=%u", lx, ly, windowID)
    }
    
    public func handleKeyDown(keyCode: UInt32, char: String, windowID: UInt32) {
        let msgDown = Win32MessageEntry(hwnd: windowID, message: .wmKeyDown, wParam: keyCode, lParam: 0)
        enqueueMessage(msgDown)
        if !char.isEmpty {
            let charCode = UInt32(char.utf16.first ?? 0)
            let msgChar = Win32MessageEntry(hwnd: windowID, message: .wmChar, wParam: charCode, lParam: 0)
            enqueueMessage(msgChar)
        }
        NSLog("[WINOS-INPUT-BRIDGE] keyDown code=%u char=%@ hwnd=%u", keyCode, char, windowID)
    }
    
    public func handleKeyUp(keyCode: UInt32, windowID: UInt32) {
        let msg = Win32MessageEntry(hwnd: windowID, message: .wmKeyUp, wParam: keyCode, lParam: 0)
        enqueueMessage(msg)
        NSLog("[WINOS-INPUT-BRIDGE] keyUp code=%u hwnd=%u", keyCode, windowID)
    }
    
    // MARK: - Message queue real
    
    private func enqueueMessage(_ msg: Win32MessageEntry) {
        if messageQueue.count >= maxQueueSize {
            messageQueue.removeFirst()
        }
        messageQueue.append(msg)
        onMessage?(msg)
        // Também envia para pr_win32 via pr_win32_input_*
        // Aqui seria ponte para C: pr_win32_input_mouse, pr_win32_input_key, etc
    }
    
    public func peekMessage() -> Win32MessageEntry? {
        return messageQueue.first
    }
    
    public func getMessage() -> Win32MessageEntry? {
        guard !messageQueue.isEmpty else { return nil }
        return messageQueue.removeFirst()
    }
    
    public func postMessage(hwnd: UInt32, message: Win32Message, wParam: UInt32 = 0, lParam: UInt32 = 0) {
        let entry = Win32MessageEntry(hwnd: hwnd, message: message, wParam: wParam, lParam: lParam)
        enqueueMessage(entry)
        NSLog("[WINOS-INPUT-BRIDGE] postMessage hwnd=%u msg=%@ w=%u l=%u", hwnd, "\(message)", wParam, lParam)
    }
    
    public func clearQueue() {
        messageQueue.removeAll()
        NSLog("[WINOS-INPUT-BRIDGE] clearQueue")
    }
    
    public func queueCount() -> Int {
        messageQueue.count
    }
    
    // MARK: - Hit testing bridge
    
    public func hitTestDesktop(x: Double, y: Double, windowManager: WinOSWindowManager) -> WinOSWindow? {
        let (lx, ly) = physicalToLogical(physicalX: x, physicalY: y)
        return windowManager.windowAt(pointX: lx, pointY: ly)
    }
}
