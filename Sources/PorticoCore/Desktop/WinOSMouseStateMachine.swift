import Foundation

/// INPUT STATE MACHINE determinística para mouse/touch
/// IDLE → PRESSED → CLICK / DOUBLE_CLICK / HELD → DRAGGING → DROP → IDLE

public enum WinOSInputState: String, Sendable {
    case idle = "IDLE"
    case pressed = "PRESSED"
    case held = "HELD"
    case dragging = "DRAGGING"
    case released = "RELEASED"
    case click = "CLICK"
    case doubleClick = "DOUBLE_CLICK"
    case rightClick = "RIGHT_CLICK"
    case drop = "DROP"
}

public enum WinOSGestureType: String, Sendable {
    case none = "NONE"
    case tap = "TAP"
    case doubleTap = "DOUBLE_TAP"
    case longPress = "LONG_PRESS"
    case drag = "DRAG"
    case rightClick = "RIGHT_CLICK"
    case wheel = "WHEEL"
    case hover = "HOVER"
}

public struct WinOSGestureEvent: Sendable {
    public var type: WinOSGestureType
    public var state: WinOSInputState
    public var x: Double // desktop coords
    public var y: Double
    public var startX: Double
    public var startY: Double
    public var deltaX: Double
    public var deltaY: Double
    public var duration: Double // segundos desde PRESSED
    public var distance: Double // distância desde start
    public var timestamp: Double
    public var windowID: WinOSWindowID?
    public var button: WinOSMouseButton
    
    public init(type: WinOSGestureType, state: WinOSInputState, x: Double, y: Double, startX: Double, startY: Double, deltaX: Double, deltaY: Double, duration: Double, distance: Double, timestamp: Double, windowID: WinOSWindowID?, button: WinOSMouseButton) {
        self.type = type
        self.state = state
        self.x = x
        self.y = y
        self.startX = startX
        self.startY = startY
        self.deltaX = deltaX
        self.deltaY = deltaY
        self.duration = duration
        self.distance = distance
        self.timestamp = timestamp
        self.windowID = windowID
        self.button = button
    }
    
    public var diagnosticsLog: String {
        "[WINOS-GESTURE] type=\(type.rawValue) state=\(state.rawValue) x=\(Int(x)) y=\(Int(y)) start=\(Int(startX)),\(Int(startY)) delta=\(Int(deltaX)),\(Int(deltaY)) duration=\(String(format: "%.2f", duration))s distance=\(String(format: "%.1f", distance)) win=\(windowID?.raw ?? 0) button=\(button.rawValue)"
    }
}

public struct WinOSMouseConfig: Sendable {
    public var doubleClickTime: Double = 0.3 // segundos
    public var doubleClickDistance: Double = 10.0 // pixels desktop
    public var longPressTime: Double = 0.5
    public var dragThreshold: Double = 5.0 // pixels desktop para iniciar drag
    public var rightClickLongPressTime: Double = 0.6
    public var wheelThreshold: Double = 20.0
    
    public init() {}
}

@MainActor
public final class WinOSMouseStateMachine: ObservableObject {
    @Published public private(set) var currentState: WinOSInputState = .idle
    @Published public private(set) var lastGesture: WinOSGestureEvent?
    
    private var config: WinOSMouseConfig
    private var startX: Double = 0
    private var startY: Double = 0
    private var currentX: Double = 0
    private var currentY: Double = 0
    private var startTime: Double = 0
    private var lastClickTime: Double = 0
    private var lastClickX: Double = 0
    private var lastClickY: Double = 0
    private var lastWindowID: WinOSWindowID?
    private var currentWindowID: WinOSWindowID?
    private var currentButton: WinOSMouseButton = .left
    
    private var longPressTimer: Timer?
    private var isLongPressFired: Bool = false
    
    public var onGesture: ((WinOSGestureEvent) -> Void)?
    public var onStateChanged: ((WinOSInputState, WinOSInputState) -> Void)?
    
    public init(config: WinOSMouseConfig = WinOSMouseConfig()) {
        self.config = config
        NSLog("[WINOS-GESTURE] StateMachine init doubleClickTime=%.1fs distance=%.0f longPress=%.1fs dragThreshold=%.0f",
              config.doubleClickTime, config.doubleClickDistance, config.longPressTime, config.dragThreshold)
    }
    
    public func updateConfig(_ newConfig: WinOSMouseConfig) {
        config = newConfig
    }
    
    // MARK: - Touch/Mouse events
    
    public func touchBegan(x: Double, y: Double, windowID: WinOSWindowID?, button: WinOSMouseButton = .left) {
        let now = ProcessInfo.processInfo.systemUptime
        let oldState = currentState
        
        startX = x
        startY = y
        currentX = x
        currentY = y
        startTime = now
        currentWindowID = windowID
        currentButton = button
        isLongPressFired = false
        
        currentState = .pressed
        
        // Agenda long press detection
        scheduleLongPressCheck()
        
        let event = WinOSGestureEvent(type: .none, state: currentState, x: x, y: y, startX: startX, startY: startY,
                                      deltaX: 0, deltaY: 0, duration: 0, distance: 0, timestamp: now,
                                      windowID: windowID, button: button)
        lastGesture = event
        
        NSLog("[WINOS-GESTURE] touchBegan %@ | oldState=%@ newState=%@", event.diagnosticsLog, oldState.rawValue, currentState.rawValue)
        onStateChanged?(oldState, currentState)
        onGesture?(event)
    }
    
    public func touchMoved(x: Double, y: Double, windowID: WinOSWindowID?) {
        let now = ProcessInfo.processInfo.systemUptime
        let oldState = currentState
        
        currentX = x
        currentY = y
        currentWindowID = windowID
        
        let deltaX = x - startX
        let deltaY = y - startY
        let distance = sqrt(deltaX*deltaX + deltaY*deltaY)
        let duration = now - startTime
        
        var gestureType: WinOSGestureType = .none
        var newState = currentState
        
        switch currentState {
        case .pressed, .held:
            if distance > config.dragThreshold {
                newState = .dragging
                gestureType = .drag
            } else if currentState == .pressed && duration > config.longPressTime && !isLongPressFired {
                // Ainda não é drag, mas já passou tempo de long press — vira HELD
                // Não muda para HELD automaticamente aqui, espera timer
                break
            }
        case .dragging:
            gestureType = .drag
        default:
            break
        }
        
        if newState != currentState {
            currentState = newState
            onStateChanged?(oldState, newState)
        }
        
        let event = WinOSGestureEvent(type: gestureType, state: currentState, x: x, y: y, startX: startX, startY: startY,
                                      deltaX: deltaX, deltaY: deltaY, duration: duration, distance: distance,
                                      timestamp: now, windowID: windowID, button: currentButton)
        lastGesture = event
        
        if gestureType == .drag {
            NSLog("[WINOS-GESTURE] touchMoved DRAGGING %@", event.diagnosticsLog)
        }
        onGesture?(event)
    }
    
    public func touchEnded(x: Double, y: Double, windowID: WinOSWindowID?) {
        let now = ProcessInfo.processInfo.systemUptime
        let oldState = currentState
        
        currentX = x
        currentY = y
        
        let deltaX = x - startX
        let deltaY = y - startY
        let distance = sqrt(deltaX*deltaX + deltaY*deltaY)
        let duration = now - startTime
        
        cancelLongPressCheck()
        
        var gestureType: WinOSGestureType = .none
        var newState: WinOSInputState = .released
        
        switch oldState {
        case .pressed:
            // Verifica double click
            let timeSinceLastClick = now - lastClickTime
            let distFromLastClick = sqrt(pow(x - lastClickX, 2) + pow(y - lastClickY, 2))
            
            if timeSinceLastClick < config.doubleClickTime && distFromLastClick < config.doubleClickDistance && lastWindowID == windowID {
                // DOUBLE CLICK
                gestureType = .doubleTap
                newState = .doubleClick
                NSLog("[WINOS-GESTURE] DOUBLE_CLICK detected timeSinceLast=%.2fs dist=%.1f", timeSinceLastClick, distFromLastClick)
            } else {
                // CLICK simples
                gestureType = .tap
                newState = .click
            }
            
            // Guarda para próximo double click check
            lastClickTime = now
            lastClickX = x
            lastClickY = y
            lastWindowID = windowID
            
        case .held:
            // Se estava HELD e solta rápido, pode ser right click (long press)
            if duration >= config.rightClickLongPressTime {
                gestureType = .rightClick
                newState = .rightClick
            } else {
                gestureType = .tap
                newState = .click
            }
            
        case .dragging:
            gestureType = .drag
            newState = .drop
            
        default:
            gestureType = .none
            newState = .idle
        }
        
        currentState = newState
        
        let event = WinOSGestureEvent(type: gestureType, state: newState, x: x, y: y, startX: startX, startY: startY,
                                      deltaX: deltaX, deltaY: deltaY, duration: duration, distance: distance,
                                      timestamp: now, windowID: windowID, button: currentButton)
        lastGesture = event
        
        NSLog("[WINOS-GESTURE] touchEnded %@ | oldState=%@ newState=%@", event.diagnosticsLog, oldState.rawValue, newState.rawValue)
        onStateChanged?(oldState, newState)
        onGesture?(event)
        
        // Volta para IDLE após pequeno delay (para permitir que UI processe CLICK/DOUBLE_CLICK)
        DispatchQueue.main.asyncAfter(deadline: .now() + 0.05) {
            if self.currentState == newState { // ainda no mesmo estado, não houve novo touch
                let prev = self.currentState
                self.currentState = .idle
                self.onStateChanged?(prev, .idle)
            }
        }
    }
    
    public func touchCancelled() {
        cancelLongPressCheck()
        let old = currentState
        currentState = .idle
        NSLog("[WINOS-GESTURE] touchCancelled oldState=%@", old.rawValue)
        onStateChanged?(old, .idle)
    }
    
    // MARK: - Long press handling
    
    private func scheduleLongPressCheck() {
        cancelLongPressCheck()
        longPressTimer = Timer.scheduledTimer(withTimeInterval: config.longPressTime, repeats: false) { [weak self] _ in
            Task { @MainActor in
                self?.handleLongPressTimer()
            }
        }
    }
    
    private func cancelLongPressCheck() {
        longPressTimer?.invalidate()
        longPressTimer = nil
    }
    
    private func handleLongPressTimer() {
        guard currentState == .pressed else { return }
        let now = ProcessInfo.processInfo.systemUptime
        let duration = now - startTime
        let deltaX = currentX - startX
        let deltaY = currentY - startY
        let distance = sqrt(deltaX*deltaX + deltaY*deltaY)
        
        // Só vira HELD se ainda não moveu muito (evita long press durante drag)
        if distance < config.dragThreshold {
            let old = currentState
            currentState = .held
            isLongPressFired = true
            
            let event = WinOSGestureEvent(type: .longPress, state: .held, x: currentX, y: currentY,
                                          startX: startX, startY: startY, deltaX: deltaX, deltaY: deltaY,
                                          duration: duration, distance: distance, timestamp: now,
                                          windowID: currentWindowID, button: currentButton)
            lastGesture = event
            
            NSLog("[WINOS-GESTURE] LONG_PRESS fired %@ | oldState=%@ newState=HELD", event.diagnosticsLog, old.rawValue)
            onStateChanged?(old, .held)
            onGesture?(event)
        }
    }
    
    public func reset() {
        cancelLongPressCheck()
        currentState = .idle
        startX = 0
        startY = 0
        currentX = 0
        currentY = 0
        startTime = 0
        lastClickTime = 0
        lastClickX = 0
        lastClickY = 0
        lastWindowID = nil
        currentWindowID = nil
        isLongPressFired = false
        NSLog("[WINOS-GESTURE] reset")
    }
}
