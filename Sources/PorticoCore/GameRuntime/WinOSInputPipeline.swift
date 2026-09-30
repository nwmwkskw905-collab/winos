import Foundation
import PorticoCore
import GameController

/// FASE 9 — Input pipeline real (touch → mouse/keyboard/gamepad → Win32 messages)

public enum WinOSInputDevice: String, Sendable {
    case touch = "TOUCH"
    case mouse = "MOUSE"
    case keyboard = "KEYBOARD"
    case gamepad = "GAMEPAD"
    case virtual = "VIRTUAL"
}

public struct WinOSGameInputState: Sendable {
    public var mouseX: Int32 = 0
    public var mouseY: Int32 = 0
    public var mouseLeftDown: Bool = false
    public var mouseRightDown: Bool = false
    public var mouseMiddleDown: Bool = false
    public var mouseDeltaX: Int32 = 0
    public var mouseDeltaY: Int32 = 0
    public var keysDown: Set<UInt32> = []
    public var lastChar: String = ""
    public var gamepadConnected: Bool = false
    public var gamepadButtons: UInt32 = 0
    public var gamepadAxes: [Float] = [0,0,0,0] // moveX, moveY, lookX, lookY
    public var gamepadTriggers: (Float, Float) = (0,0) // LT, RT
    public var timestamp: Double = 0
    
    public init() {
        timestamp = ProcessInfo.processInfo.systemUptime
    }
}

@MainActor
public final class WinOSInputPipeline: ObservableObject {
    @Published public private(set) var currentState: WinOSGameInputState = WinOSGameInputState()
    @Published public private(set) var isGamepadConnected: Bool = false
    
    private var diagnostics: WinOSGameDiagnostics?
    private var inputBridge: WinOSInputBridge?
    private var touchInputAdapter: Any? // TouchInputAdapter from InputCore
    private var gameControllerAdapter: Any?
    
    private var lastMouseX: Int32 = 0
    private var lastMouseY: Int32 = 0
    private var eventCount: UInt64 = 0
    
    public var onInputState: ((WinOSGameInputState) -> Void)?
    public var onWin32Message: ((UInt32, UInt32, UInt32, UInt32) -> Void)? // hwnd, msg, wParam, lParam
    
    public init() {
        NSLog("[WINOS-INPUT-PIPELINE] InputPipeline init")
    }
    
    public func configure(diagnostics: WinOSGameDiagnostics?, inputBridge: WinOSInputBridge?) {
        self.diagnostics = diagnostics
        self.inputBridge = inputBridge
        NSLog("[WINOS-INPUT-PIPELINE] configure bridge=%@", inputBridge != nil ? "YES" : "NO")
        
        // GameController observation
        NotificationCenter.default.addObserver(self, selector: #selector(controllerDidConnect), name: .GCControllerDidConnect, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(controllerDidDisconnect), name: .GCControllerDidDisconnect, object: nil)
    }
    
    // MARK: - Touch → Mouse
    
    public func handleTouchBegan(x: Double, y: Double, windowID: UInt32) {
        eventCount += 1
        let lx = Int32(x)
        let ly = Int32(y)
        let deltaX = lx - lastMouseX
        let deltaY = ly - lastMouseY
        lastMouseX = lx
        lastMouseY = ly
        
        currentState.mouseX = lx
        currentState.mouseY = ly
        currentState.mouseLeftDown = true
        currentState.mouseDeltaX = deltaX
        currentState.mouseDeltaY = deltaY
        currentState.timestamp = ProcessInfo.processInfo.systemUptime
        
        inputBridge?.handleTouchBegan(x: x, y: y, windowID: windowID)
        diagnostics?.inputEvent(type: "TOUCH_BEGAN", x: lx, y: ly, keyCode: 0)
        onInputState?(currentState)
        onWin32Message?(windowID, 0x0201, 1, UInt32((UInt32(ly) << 16) | UInt32(lx & 0xFFFF))) // WM_LBUTTONDOWN
        
        if eventCount % 10 == 0 {
            NSLog("[WINOS-INPUT-PIPELINE] touchBegan x=%.0f y=%.0f hwnd=%u count=%llu", x, y, windowID, eventCount)
        }
    }
    
    public func handleTouchMoved(x: Double, y: Double, windowID: UInt32) {
        eventCount += 1
        let lx = Int32(x)
        let ly = Int32(y)
        let deltaX = lx - lastMouseX
        let deltaY = ly - lastMouseY
        lastMouseX = lx
        lastMouseY = ly
        
        currentState.mouseX = lx
        currentState.mouseY = ly
        currentState.mouseDeltaX = deltaX
        currentState.mouseDeltaY = deltaY
        currentState.timestamp = ProcessInfo.processInfo.systemUptime
        
        inputBridge?.handleTouchMoved(x: x, y: y, windowID: windowID)
        onInputState?(currentState)
        onWin32Message?(windowID, 0x0200, currentState.mouseLeftDown ? 1 : 0, UInt32((UInt32(ly) << 16) | UInt32(lx & 0xFFFF))) // WM_MOUSEMOVE
    }
    
    public func handleTouchEnded(x: Double, y: Double, windowID: UInt32) {
        eventCount += 1
        let lx = Int32(x)
        let ly = Int32(y)
        lastMouseX = lx
        lastMouseY = ly
        
        currentState.mouseX = lx
        currentState.mouseY = ly
        currentState.mouseLeftDown = false
        currentState.timestamp = ProcessInfo.processInfo.systemUptime
        
        inputBridge?.handleTouchEnded(x: x, y: y, windowID: windowID)
        diagnostics?.inputEvent(type: "TOUCH_ENDED", x: lx, y: ly, keyCode: 0)
        onInputState?(currentState)
        onWin32Message?(windowID, 0x0202, 0, UInt32((UInt32(ly) << 16) | UInt32(lx & 0xFFFF))) // WM_LBUTTONUP
        
        NSLog("[WINOS-INPUT-PIPELINE] touchEnded x=%.0f y=%.0f hwnd=%u", x, y, windowID)
    }
    
    // MARK: - Keyboard
    
    public func handleKeyDown(keyCode: UInt32, char: String, windowID: UInt32) {
        eventCount += 1
        currentState.keysDown.insert(keyCode)
        currentState.lastChar = char
        currentState.timestamp = ProcessInfo.processInfo.systemUptime
        
        inputBridge?.handleKeyDown(keyCode: keyCode, char: char, windowID: windowID)
        diagnostics?.inputEvent(type: "KEY_DOWN", x: 0, y: 0, keyCode: keyCode)
        onInputState?(currentState)
        onWin32Message?(windowID, 0x0100, keyCode, 0) // WM_KEYDOWN
        if !char.isEmpty {
            let charCode = UInt32(char.utf16.first ?? 0)
            onWin32Message?(windowID, 0x0102, charCode, 0) // WM_CHAR
        }
        NSLog("[WINOS-INPUT-PIPELINE] keyDown code=%u char=%@ hwnd=%u", keyCode, char, windowID)
    }
    
    public func handleKeyUp(keyCode: UInt32, windowID: UInt32) {
        eventCount += 1
        currentState.keysDown.remove(keyCode)
        currentState.timestamp = ProcessInfo.processInfo.systemUptime
        
        inputBridge?.handleKeyUp(keyCode: keyCode, windowID: windowID)
        diagnostics?.inputEvent(type: "KEY_UP", x: 0, y: 0, keyCode: keyCode)
        onInputState?(currentState)
        onWin32Message?(windowID, 0x0101, keyCode, 0) // WM_KEYUP
    }
    
    // MARK: - Gamepad
    
    @objc private func controllerDidConnect(notification: Notification) {
        isGamepadConnected = true
        currentState.gamepadConnected = true
        diagnostics?.log(.info, category: .input, code: "GAMEPAD_CONNECTED", message: "GAMEPAD_CONNECTED", detail: "controller connected")
        NSLog("[WINOS-INPUT-PIPELINE] gamepad connected")
    }
    
    @objc private func controllerDidDisconnect(notification: Notification) {
        isGamepadConnected = false
        currentState.gamepadConnected = false
        diagnostics?.log(.info, category: .input, code: "GAMEPAD_DISCONNECTED", message: "GAMEPAD_DISCONNECTED", detail: "controller disconnected")
        NSLog("[WINOS-INPUT-PIPELINE] gamepad disconnected")
    }
    
    public func updateGamepad(buttons: UInt32, axes: [Float], triggers: (Float, Float)) {
        currentState.gamepadButtons = buttons
        currentState.gamepadAxes = axes
        currentState.gamepadTriggers = triggers
        currentState.timestamp = ProcessInfo.processInfo.systemUptime
        onInputState?(currentState)
    }
    
    // MARK: - Conversion to InputState (for ExecutionBackend)
    
    public func toInputState() -> InputState {
        var state = InputState()
        state.buttons = currentState.gamepadButtons
        if currentState.mouseLeftDown {
            state.buttons |= 1 // Map left mouse to button 0
        }
        state.moveX = currentState.gamepadAxes.count > 0 ? currentState.gamepadAxes[0] : 0
        state.moveY = currentState.gamepadAxes.count > 1 ? currentState.gamepadAxes[1] : 0
        state.lookX = currentState.gamepadAxes.count > 2 ? currentState.gamepadAxes[2] : 0
        state.lookY = currentState.gamepadAxes.count > 3 ? currentState.gamepadAxes[3] : 0
        state.triggerLT = currentState.gamepadTriggers.0
        state.triggerRT = currentState.gamepadTriggers.1
        return state
    }
    
    public func reset() {
        currentState = WinOSGameInputState()
        lastMouseX = 0
        lastMouseY = 0
        eventCount = 0
        NSLog("[WINOS-INPUT-PIPELINE] reset")
    }
    
    deinit {
        NotificationCenter.default.removeObserver(self)
    }
}
