import Foundation

/// DESKTOP INPUT HANDLER — integra DisplayMetrics + MouseCursor + StateMachine + WindowManager + InputBridge
/// NÃO substitui InputCore, WinOSInputBridge, WindowManager — usa-os

@MainActor
public final class WinOSDesktopInputHandler: ObservableObject {
    @Published public private(set) var displayMetrics: WinOSDisplayMetrics
    @Published public private(set) var cursor: WinOSMouseCursor
    @Published public private(set) var inputState: WinOSInputState = .idle
    
    private let displayManager: WinOSDisplayManager
    private let cursorManager: WinOSMouseCursorManager
    private let stateMachine: WinOSMouseStateMachine
    private let windowManager: WinOSWindowManager
    private let inputBridge: WinOSInputBridge
    private let compositor: WinOSCompositor?
    
    private var cursorRenderer: WinOSCursorRenderer?
    
    // Drag state
    private var windowDragActive: Bool = false
    private var windowDragID: WinOSWindowID?
    private var windowDragOffsetX: Double = 0
    private var windowDragOffsetY: Double = 0
    
    // Config
    private var config: WinOSMouseConfig
    
    public var onWindowMessage: ((WinOSWindowID, UInt32, UInt32, UInt32) -> Void)? // hwnd, msg, wParam, lParam
    public var onWindowDrag: ((WinOSWindowID, Double, Double) -> Void)?
    
    public init(displayManager: WinOSDisplayManager,
                windowManager: WinOSWindowManager,
                inputBridge: WinOSInputBridge,
                compositor: WinOSCompositor? = nil,
                cursorManager: WinOSMouseCursorManager? = nil,
                config: WinOSMouseConfig = WinOSMouseConfig()) {
        self.displayManager = displayManager
        self.windowManager = windowManager
        self.inputBridge = inputBridge
        self.compositor = compositor
        self.config = config

        let initialMetrics = displayManager.metrics
        self.displayMetrics = initialMetrics
        
        // Managers
        self.cursorManager = cursorManager ?? WinOSMouseCursorManager(initialX: initialMetrics.desktopWidth/2, initialY: initialMetrics.desktopHeight/2, displayMetrics: initialMetrics)
        self.stateMachine = WinOSMouseStateMachine(config: config)
        self.cursor = self.cursorManager.cursor
        
        // Cursor renderer (acima das janelas)
        if let comp = compositor {
            self.cursorRenderer = WinOSCursorRenderer(displayMetrics: initialMetrics, compositor: comp)
        }
        
        setupCallbacks()
        
        NSLog("[WINOS-DESKTOP-INPUT] InputHandler init metrics=%@", displayMetrics.description)
    }
    
    private func setupCallbacks() {
        displayManager.onMetricsChanged = { [weak self] metrics in
            Task { @MainActor in
                self?.handleDisplayMetricsChanged(metrics)
            }
        }
        
        cursorManager.onCursorChanged = { [weak self] cursor in
            Task { @MainActor in
                self?.cursor = cursor
                self?.cursorRenderer?.updateCursor(cursor)
            }
        }
        
        stateMachine.onStateChanged = { [weak self] old, new in
            Task { @MainActor in
                self?.inputState = new
                NSLog("[WINOS-DESKTOP-INPUT] state %@ -> %@", old.rawValue, new.rawValue)
            }
        }
        
        stateMachine.onGesture = { [weak self] gesture in
            Task { @MainActor in
                self?.handleGesture(gesture)
            }
        }
    }
    
    // MARK: - Display metrics
    
    private func handleDisplayMetricsChanged(_ metrics: WinOSDisplayMetrics) {
        displayMetrics = metrics
        cursorManager.updateDisplayMetrics(metrics)
        cursorRenderer?.updateDisplayMetrics(metrics)
        inputBridge.setDesktopSize(logicalWidth: Int32(metrics.desktopWidth), logicalHeight: Int32(metrics.desktopHeight),
                                   physicalWidth: Int32(metrics.viewportWidth), physicalHeight: Int32(metrics.viewportHeight))
        NSLog("[WINOS-DESKTOP-INPUT] displayMetrics changed %@", metrics.diagnosticsLog)
    }
    
    // MARK: - Touch → Desktop → Mouse → Win32
    
    public func handleTouchBegan(screenX: Double, screenY: Double) {
        let desktop = displayMetrics.screenToDesktop(screenX: screenX, screenY: screenY)
        let win = windowManager.windowAt(pointX: Int32(desktop.x), pointY: Int32(desktop.y))
        let hwnd = win?.id
        
        // Atualiza cursor
        cursorManager.moveTo(desktopX: desktop.x, desktopY: desktop.y)
        cursorManager.setButtonState(.down, button: .left)
        
        // Hover
        updateHover(desktopX: desktop.x, desktopY: desktop.y)
        
        // State machine
        stateMachine.touchBegan(x: desktop.x, y: desktop.y, windowID: hwnd, button: .left)
        
        // Foco janela
        if let w = win {
            windowManager.focusWindow(id: w.id)
            
            // Verifica se é title bar para drag de janela
            if windowManager.titleBarHitTest(window: w, pointX: Int32(desktop.x), pointY: Int32(desktop.y)) &&
               !windowManager.closeButtonHitTest(window: w, pointX: Int32(desktop.x), pointY: Int32(desktop.y)) &&
               !windowManager.minimizeButtonHitTest(window: w, pointX: Int32(desktop.x), pointY: Int32(desktop.y)) &&
               !windowManager.maximizeButtonHitTest(window: w, pointX: Int32(desktop.x), pointY: Int32(desktop.y)) {
                // Inicia drag de janela
                windowDragActive = true
                windowDragID = w.id
                windowDragOffsetX = desktop.x - Double(w.x)
                windowDragOffsetY = desktop.y - Double(w.y)
                cursorManager.startDrag(windowID: w.id, offsetX: windowDragOffsetX, offsetY: windowDragOffsetY)
                NSLog("[WINOS-DESKTOP-INPUT] window drag start win=%u offset=%.0f,%.0f", w.id.raw, windowDragOffsetX, windowDragOffsetY)
            }
        }
        
        // Win32 message
        let lParam = UInt32((UInt32(desktop.y) << 16) | UInt32(Int(desktop.x) & 0xFFFF))
        inputBridge.handleTouchBegan(x: desktop.x, y: desktop.y, windowID: hwnd?.raw ?? 0)
        onWindowMessage?(hwnd ?? .invalid, 0x0201, 1, lParam) // WM_LBUTTONDOWN
        
        NSLog("[WINOS-DESKTOP-INPUT] touchBegan screen=%.0f,%.0f desktop=%.0f,%.0f win=%u %@", screenX, screenY, desktop.x, desktop.y, hwnd?.raw ?? 0, cursor.diagnosticsLog)
    }
    
    public func handleTouchMoved(screenX: Double, screenY: Double) {
        let desktop = displayMetrics.screenToDesktop(screenX: screenX, screenY: screenY)
        let win = windowManager.windowAt(pointX: Int32(desktop.x), pointY: Int32(desktop.y))
        
        cursorManager.moveTo(desktopX: desktop.x, desktopY: desktop.y)
        updateHover(desktopX: desktop.x, desktopY: desktop.y)
        stateMachine.touchMoved(x: desktop.x, y: desktop.y, windowID: win?.id)
        
        // Window drag
        if windowDragActive, let dragID = windowDragID {
            let newX = desktop.x - windowDragOffsetX
            let newY = desktop.y - windowDragOffsetY
            windowManager.moveWindow(id: dragID, x: Int32(newX), y: Int32(newY))
            onWindowDrag?(dragID, newX, newY)
            // NSLog("[WINOS-DESKTOP-INPUT] window drag move win=%u pos=%.0f,%.0f", dragID.raw, newX, newY)
        }
        
        let lParam = UInt32((UInt32(desktop.y) << 16) | UInt32(Int(desktop.x) & 0xFFFF))
        let wParam: UInt32 = cursor.buttonState == .down ? 1 : 0
        inputBridge.handleTouchMoved(x: desktop.x, y: desktop.y, windowID: win?.id.raw ?? 0)
        onWindowMessage?(win?.id ?? .invalid, 0x0200, wParam, lParam) // WM_MOUSEMOVE
    }
    
    public func handleTouchEnded(screenX: Double, screenY: Double) {
        let desktop = displayMetrics.screenToDesktop(screenX: screenX, screenY: screenY)
        let win = windowManager.windowAt(pointX: Int32(desktop.x), pointY: Int32(desktop.y))
        
        cursorManager.moveTo(desktopX: desktop.x, desktopY: desktop.y)
        cursorManager.setButtonState(.up, button: .none)
        stateMachine.touchEnded(x: desktop.x, y: desktop.y, windowID: win?.id)
        
        // End window drag
        if windowDragActive {
            windowDragActive = false
            cursorManager.endDrag()
            NSLog("[WINOS-DESKTOP-INPUT] window drag end win=%u", windowDragID?.raw ?? 0)
            windowDragID = nil
        }
        
        let lParam = UInt32((UInt32(desktop.y) << 16) | UInt32(Int(desktop.x) & 0xFFFF))
        inputBridge.handleTouchEnded(x: desktop.x, y: desktop.y, windowID: win?.id.raw ?? 0)
        onWindowMessage?(win?.id ?? .invalid, 0x0202, 0, lParam) // WM_LBUTTONUP
        
        // Verifica botões da janela no release (evita clique acidental durante drag)
        if !cursor.isDragging, let w = win {
            if windowManager.closeButtonHitTest(window: w, pointX: Int32(desktop.x), pointY: Int32(desktop.y)) {
                windowManager.destroyWindow(id: w.id)
                NSLog("[WINOS-DESKTOP-INPUT] close button clicked win=%u", w.id.raw)
            } else if windowManager.minimizeButtonHitTest(window: w, pointX: Int32(desktop.x), pointY: Int32(desktop.y)) {
                windowManager.minimizeWindow(id: w.id)
            } else if windowManager.maximizeButtonHitTest(window: w, pointX: Int32(desktop.x), pointY: Int32(desktop.y)) {
                if w.maximized {
                    windowManager.restoreWindow(id: w.id)
                } else {
                    windowManager.maximizeWindow(id: w.id, desktopWidth: Int32(displayMetrics.desktopWidth), desktopHeight: Int32(displayMetrics.desktopHeight))
                }
            }
        }
        
        NSLog("[WINOS-DESKTOP-INPUT] touchEnded screen=%.0f,%.0f desktop=%.0f,%.0f win=%u", screenX, screenY, desktop.x, desktop.y, win?.id.raw ?? 0)
    }
    
    public func handleTouchCancelled() {
        stateMachine.touchCancelled()
        cursorManager.setButtonState(.up, button: .none)
        if windowDragActive {
            windowDragActive = false
            cursorManager.endDrag()
            windowDragID = nil
        }
        NSLog("[WINOS-DESKTOP-INPUT] touchCancelled")
    }
    
    // MARK: - Hover
    
    private func updateHover(desktopX: Double, desktopY: Double) {
        let win = windowManager.windowAt(pointX: Int32(desktopX), pointY: Int32(desktopY))
        
        var control: String? = nil
        var cursorType: WinOSCursorType = .arrow
        
        if let w = win {
            if windowManager.closeButtonHitTest(window: w, pointX: Int32(desktopX), pointY: Int32(desktopY)) {
                control = "closeButton"
                cursorType = .hand
            } else if windowManager.titleBarHitTest(window: w, pointX: Int32(desktopX), pointY: Int32(desktopY)) {
                control = "titleBar"
                cursorType = .move
            } else if windowManager.minimizeButtonHitTest(window: w, pointX: Int32(desktopX), pointY: Int32(desktopY)) ||
                      windowManager.maximizeButtonHitTest(window: w, pointX: Int32(desktopX), pointY: Int32(desktopY)) {
                control = "windowButton"
                cursorType = .hand
            } else {
                control = "clientArea"
                cursorType = .arrow
            }
        } else {
            control = "desktop"
            cursorType = .arrow
        }
        
        cursorManager.setHoverTarget(windowID: win?.id, control: control)
        cursorManager.setType(cursorType)
    }
    
    // MARK: - Gesture handling
    
    private func handleGesture(_ gesture: WinOSGestureEvent) {
        NSLog("[WINOS-DESKTOP-INPUT] gesture %@", gesture.diagnosticsLog)
        
        switch gesture.type {
        case .tap:
            // Click já tratado em touchBegan/Ended via WM_LBUTTONDOWN/UP
            break
            
        case .doubleTap:
            // WM_LBUTTONDBLCLK
            let lParam = UInt32((UInt32(gesture.y) << 16) | UInt32(Int(gesture.x) & 0xFFFF))
            if let winID = gesture.windowID {
                onWindowMessage?(winID, 0x0203, 1, lParam) // WM_LBUTTONDBLCLK
                inputBridge.postMessage(hwnd: winID.raw, message: .wmLButtonDown, wParam: 1, lParam: lParam)
                NSLog("[WINOS-DESKTOP-INPUT] double click win=%u x=%.0f y=%.0f", winID.raw, gesture.x, gesture.y)
            }
            
        case .longPress, .rightClick:
            // Right click via long press
            let lParam = UInt32((UInt32(gesture.y) << 16) | UInt32(Int(gesture.x) & 0xFFFF))
            if let winID = gesture.windowID {
                onWindowMessage?(winID, 0x0204, 2, lParam) // WM_RBUTTONDOWN
                onWindowMessage?(winID, 0x0205, 0, lParam) // WM_RBUTTONUP
                inputBridge.postMessage(hwnd: winID.raw, message: .wmRButtonDown, wParam: 2, lParam: lParam)
                inputBridge.postMessage(hwnd: winID.raw, message: .wmRButtonUp, wParam: 0, lParam: lParam)
                NSLog("[WINOS-DESKTOP-INPUT] right click (long press) win=%u", winID.raw)
            }
            
        case .drag:
            // Drag já tratado via window drag, mas também pode ser file drag
            break
            
        case .wheel:
            // Mouse wheel — quando gesto vertical sobre área rolável
            break
            
        default:
            break
        }
    }
    
    // MARK: - Mouse wheel (swipe vertical sobre área rolável)
    
    public func handleWheel(screenX: Double, screenY: Double, deltaY: Double) {
        let desktop = displayMetrics.screenToDesktop(screenX: screenX, screenY: screenY)
        let win = windowManager.windowAt(pointX: Int32(desktop.x), pointY: Int32(desktop.y))
        
        // Só gera wheel se sobre janela e delta > threshold
        guard let w = win, abs(deltaY) > config.wheelThreshold else { return }
        
        let wParam = UInt32(Int32(deltaY * 120)) // Windows WHEEL_DELTA = 120
        let lParam = UInt32((UInt32(desktop.y) << 16) | UInt32(Int(desktop.x) & 0xFFFF))
        
        onWindowMessage?(w.id, 0x020A, wParam, lParam) // WM_MOUSEWHEEL
        NSLog("[WINOS-DESKTOP-INPUT] wheel win=%u delta=%.0f", w.id.raw, deltaY)
    }
    
    // MARK: - Diagnostics
    
    public var diagnosticsLog: String {
        let dm = displayMetrics.diagnosticsLog
        let cur = cursor.diagnosticsLog
        let state = "[WINOS-GESTURE] state=\(inputState.rawValue) lastGesture=\(stateMachine.lastGesture?.type.rawValue ?? "none")"
        return "\(dm)\n\(cur)\n\(state)"
    }
    
    public func reset() {
        cursorManager.reset()
        stateMachine.reset()
        windowDragActive = false
        windowDragID = nil
        inputState = .idle
        NSLog("[WINOS-DESKTOP-INPUT] reset")
    }
}
