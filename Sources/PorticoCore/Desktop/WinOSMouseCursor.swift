import Foundation

/// CURSOR DE MOUSE REAL — estrutura central, não duplica InputCore
/// x,y em coordenadas desktop virtual (lógico), não screen

public enum WinOSCursorType: String, Sendable, CaseIterable {
    case arrow = "ARROW"
    case hand = "HAND"
    case text = "TEXT"
    case resizeHorizontal = "RESIZE_H"
    case resizeVertical = "RESIZE_V"
    case resizeDiagonal = "RESIZE_D"
    case resizeDiagonal2 = "RESIZE_D2"
    case busy = "BUSY"
    case crosshair = "CROSSHAIR"
    case move = "MOVE"
    case notAllowed = "NOT_ALLOWED"
}

public enum WinOSMouseButton: String, Sendable {
    case none = "NONE"
    case left = "LEFT"
    case right = "RIGHT"
    case middle = "MIDDLE"
}

public enum WinOSMouseButtonState: String, Sendable {
    case up = "UP"
    case down = "DOWN"
}

public struct WinOSMouseCursor: Sendable, Equatable {
    // Posição em desktop virtual (lógico, ex: 1280x720)
    public var x: Double
    public var y: Double
    
    // Estado
    public var visible: Bool
    public var type: WinOSCursorType
    public var buttonState: WinOSMouseButtonState
    public var button: WinOSMouseButton
    
    // Hover/drag
    public var hoverTarget: WinOSWindowID?
    public var hoverControl: String? // ex: "closeButton", "titleBar", "file", "desktop", "taskbar"
    public var dragState: WinOSDragState
    public var isDragging: Bool
    
    // Para window drag
    public var dragWindowID: WinOSWindowID?
    public var dragOffsetX: Double
    public var dragOffsetY: Double
    public var dragStartX: Double
    public var dragStartY: Double
    
    public init(x: Double = 0, y: Double = 0, visible: Bool = true, type: WinOSCursorType = .arrow,
                buttonState: WinOSMouseButtonState = .up, button: WinOSMouseButton = .none,
                hoverTarget: WinOSWindowID? = nil, hoverControl: String? = nil,
                dragState: WinOSDragState = .none, isDragging: Bool = false,
                dragWindowID: WinOSWindowID? = nil, dragOffsetX: Double = 0, dragOffsetY: Double = 0,
                dragStartX: Double = 0, dragStartY: Double = 0) {
        self.x = x
        self.y = y
        self.visible = visible
        self.type = type
        self.buttonState = buttonState
        self.button = button
        self.hoverTarget = hoverTarget
        self.hoverControl = hoverControl
        self.dragState = dragState
        self.isDragging = isDragging
        self.dragWindowID = dragWindowID
        self.dragOffsetX = dragOffsetX
        self.dragOffsetY = dragOffsetY
        self.dragStartX = dragStartX
        self.dragStartY = dragStartY
    }
    
    public var intX: Int32 { Int32(x) }
    public var intY: Int32 { Int32(y) }
    
    public var diagnosticsLog: String {
        "[WINOS-MOUSE] x=\(Int(x)) y=\(Int(y)) visible=\(visible) type=\(type.rawValue) button=\(button.rawValue) state=\(buttonState.rawValue) target=\(hoverTarget?.raw ?? 0) control=\(hoverControl ?? "none") dragState=\(dragState.rawValue) dragging=\(isDragging) dragWin=\(dragWindowID?.raw ?? 0)"
    }
}

public enum WinOSDragState: String, Sendable {
    case none = "NONE"
    case pending = "PENDING" // DOWN mas ainda não moveu > threshold
    case dragging = "DRAGGING"
    case dropping = "DROPPING"
}

public enum WinOSHoverTarget: String, Sendable {
    case none = "NONE"
    case desktop = "DESKTOP"
    case window = "WINDOW"
    case titleBar = "TITLE_BAR"
    case closeButton = "CLOSE_BUTTON"
    case minimizeButton = "MINIMIZE_BUTTON"
    case maximizeButton = "MAXIMIZE_BUTTON"
    case clientArea = "CLIENT_AREA"
    case taskbar = "TASKBAR"
    case file = "FILE"
    case icon = "ICON"
    case resizeBorder = "RESIZE_BORDER"
}

@MainActor
public final class WinOSMouseCursorManager: ObservableObject {
    @Published public private(set) var cursor: WinOSMouseCursor
    
    private var displayMetrics: WinOSDisplayMetrics
    private var lastHoverTarget: WinOSWindowID?
    
    public var onCursorChanged: ((WinOSMouseCursor) -> Void)?
    public var onHoverChanged: ((WinOSWindowID?, String?) -> Void)?
    
    public init(initialX: Double = 100, initialY: Double = 100, displayMetrics: WinOSDisplayMetrics = WinOSDisplayMetrics()) {
        self.cursor = WinOSMouseCursor(x: initialX, y: initialY, visible: true)
        self.displayMetrics = displayMetrics
        NSLog("[WINOS-MOUSE] CursorManager init x=%.0f y=%.0f %@", initialX, initialY, cursor.diagnosticsLog)
    }
    
    public func updateDisplayMetrics(_ metrics: WinOSDisplayMetrics) {
        // Preserva posição relativa quando viewport muda (rotação)
        let oldMetrics = displayMetrics
        displayMetrics = metrics
        
        // Se desktop size mudou, preserva posição relativa (ex: 50% da tela continua 50%)
        if oldMetrics.desktopWidth != metrics.desktopWidth || oldMetrics.desktopHeight != metrics.desktopHeight {
            let relX = cursor.x / oldMetrics.desktopWidth
            let relY = cursor.y / oldMetrics.desktopHeight
            let newX = relX * metrics.desktopWidth
            let newY = relY * metrics.desktopHeight
            cursor.x = newX
            cursor.y = newY
            NSLog("[WINOS-MOUSE] displayMetrics changed, preserved relative pos %.2f,%.2f -> %.0f,%.0f", relX, relY, newX, newY)
        }
        
        clampToDesktop()
        notify()
    }
    
    public func moveTo(desktopX: Double, desktopY: Double) {
        cursor.x = desktopX
        cursor.y = desktopY
        clampToDesktop()
        NSLog("[WINOS-MOUSE] moveTo desktop=%.0f,%.0f %@", desktopX, desktopY, cursor.diagnosticsLog)
        notify()
    }
    
    public func moveToScreen(screenX: Double, screenY: Double) {
        let d = displayMetrics.screenToDesktop(screenX: screenX, screenY: screenY)
        moveTo(desktopX: d.x, desktopY: d.y)
    }
    
    public func setVisible(_ visible: Bool) {
        cursor.visible = visible
        notify()
    }
    
    public func setType(_ type: WinOSCursorType) {
        guard cursor.type != type else { return }
        cursor.type = type
        NSLog("[WINOS-MOUSE] setType %@ %@", type.rawValue, cursor.diagnosticsLog)
        notify()
    }
    
    public func setButtonState(_ state: WinOSMouseButtonState, button: WinOSMouseButton) {
        cursor.buttonState = state
        cursor.button = button
        notify()
    }
    
    public func setHoverTarget(windowID: WinOSWindowID?, control: String?) {
        let changed = cursor.hoverTarget != windowID || cursor.hoverControl != control
        cursor.hoverTarget = windowID
        cursor.hoverControl = control
        
        if changed {
            NSLog("[WINOS-MOUSE] hover target=%u control=%@ %@", windowID?.raw ?? 0, control ?? "none", cursor.diagnosticsLog)
            onHoverChanged?(windowID, control)
        }
        notify()
    }
    
    public func startDrag(windowID: WinOSWindowID?, offsetX: Double, offsetY: Double) {
        cursor.dragState = .dragging
        cursor.isDragging = true
        cursor.dragWindowID = windowID
        cursor.dragOffsetX = offsetX
        cursor.dragOffsetY = offsetY
        cursor.dragStartX = cursor.x
        cursor.dragStartY = cursor.y
        NSLog("[WINOS-MOUSE] startDrag win=%u offset=%.0f,%.0f start=%.0f,%.0f", windowID?.raw ?? 0, offsetX, offsetY, cursor.x, cursor.y)
        notify()
    }
    
    public func updateDrag(desktopX: Double, desktopY: Double) {
        cursor.x = desktopX
        cursor.y = desktopY
        clampToDesktop()
        notify()
    }
    
    public func endDrag() {
        cursor.dragState = .none
        cursor.isDragging = false
        cursor.dragWindowID = nil
        NSLog("[WINOS-MOUSE] endDrag %@", cursor.diagnosticsLog)
        notify()
    }
    
    public func setDragState(_ state: WinOSDragState) {
        cursor.dragState = state
        notify()
    }
    
    private func clampToDesktop() {
        let clamped = displayMetrics.clampToDesktop(desktopX: cursor.x, desktopY: cursor.y)
        cursor.x = clamped.x
        cursor.y = clamped.y
    }
    
    private func notify() {
        onCursorChanged?(cursor)
    }
    
    public func reset() {
        cursor = WinOSMouseCursor(x: displayMetrics.desktopWidth/2, y: displayMetrics.desktopHeight/2, visible: true)
        NSLog("[WINOS-MOUSE] reset")
        notify()
    }
}
