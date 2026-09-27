import Foundation

/// Estado de entrada unificado (espelho do `pr_input_state` do runtime C).
public struct InputState: Equatable, Sendable {
    public var buttons: UInt32
    public var moveX: Float
    public var moveY: Float
    public var lookX: Float
    public var lookY: Float
    public var triggerLT: Float
    public var triggerRT: Float

    public init(buttons: UInt32 = 0,
                moveX: Float = 0, moveY: Float = 0,
                lookX: Float = 0, lookY: Float = 0,
                triggerLT: Float = 0, triggerRT: Float = 0) {
        self.buttons = buttons
        self.moveX = moveX
        self.moveY = moveY
        self.lookX = lookX
        self.lookY = lookY
        self.triggerLT = triggerLT
        self.triggerRT = triggerRT
    }

    public static let empty = InputState()
}

/// Bits de botões (idênticos ao runtime C).
public enum InputButton: UInt32, CaseIterable, Sendable {
    case a = 1
    case b = 2
    case x = 4
    case y = 8
    case lb = 16
    case rb = 32
    case start = 64
    case select = 128
    case dup = 256
    case ddown = 512
    case dleft = 1024
    case dright = 2048

    public init?(action: InputAction) {
        switch action {
        case .buttonA: self = .a
        case .buttonB: self = .b
        case .buttonX: self = .x
        case .buttonY: self = .y
        case .bumperLeft: self = .lb
        case .bumperRight: self = .rb
        case .triggerLeft: self = .lb
        case .triggerRight: self = .rb
        case .start: self = .start
        case .select: self = .select
        case .moveUp: self = .dup
        case .moveDown: self = .ddown
        case .moveLeft: self = .dleft
        case .moveRight: self = .dright
        case .lookLeft, .lookRight, .lookUp, .lookDown:
            return nil // eixos — tratados em `InputRouter.applyAxis`
        }
    }
}

/// Binding de controle físico (GameController) para ação do jogo.
public struct PhysicalBinding: Equatable, Sendable, Codable {
    public var elementID: UUID
    public var action: InputAction
    public init(elementID: UUID, action: InputAction) {
        self.elementID = elementID
        self.action = action
    }
}

/// Agrega toques virtuais e gamepads físicos num único InputState.
/// Sem alocações no caminho quente (mutação in-place).
    public final class InputRouter {
    public private(set) var state = InputState()
    private var touchButtons: UInt32 = 0
    private var padButtons: UInt32 = 0
    private var touchMove: (Float, Float) = (0, 0)
    private var padMove: (Float, Float) = (0, 0)
    private var touchLook: (Float, Float) = (0, 0)
    private var padLook: (Float, Float) = (0, 0)
    private var touchLT: Float = 0
    private var padLT: Float = 0
    private var touchRT: Float = 0
    private var padRT: Float = 0

    public init() {}

    // MARK: - touch virtual

    public func setTouchButton(_ button: InputButton, pressed: Bool) {
        if pressed { touchButtons |= button.rawValue }
        else { touchButtons &= ~button.rawValue }
        recompute()
    }

    public func setTouchAction(_ action: InputAction, pressed: Bool) {
        guard let b = InputButton(action: action) else { return }
        setTouchButton(b, pressed: pressed)
    }

    public func setMoveStick(x: Float, y: Float) {
        touchMove = (clampAxis(x), clampAxis(y))
        recompute()
    }

    public func setLookStick(x: Float, y: Float) {
        touchLook = (clampAxis(x), clampAxis(y))
        recompute()
    }

    public func setTrigger(_ action: InputAction, value: Float) {
        let v = min(max(value, 0), 1)
        switch action {
        case .triggerLeft: touchLT = v
        case .triggerRight: touchRT = v
        default: break
        }
        recompute()
    }

    // MARK: - gamepad físico

    public func setPadButton(_ button: InputButton, pressed: Bool) {
        if pressed { padButtons |= button.rawValue }
        else { padButtons &= ~button.rawValue }
        recompute()
    }

    public func setPadMove(x: Float, y: Float) {
        padMove = (clampAxis(x), clampAxis(y))
        recompute()
    }

    public func setPadLook(x: Float, y: Float) {
        padLook = (clampAxis(x), clampAxis(y))
        recompute()
    }

    public func setPadTriggers(lt: Float, rt: Float) {
        padLT = min(max(lt, 0), 1)
        padRT = min(max(rt, 0), 1)
        recompute()
    }

    public func reset() {
        touchButtons = 0; padButtons = 0
        touchMove = (0, 0); padMove = (0, 0)
        touchLook = (0, 0); padLook = (0, 0)
        touchLT = 0; padLT = 0; touchRT = 0; padRT = 0
        recompute()
    }

    private func clampAxis(_ v: Float) -> Float {
        min(max(v, -1), 1)
    }

    private func recompute() {
        state.buttons = touchButtons | padButtons
        state.moveX = maxMagnitude(touchMove.0, padMove.0)
        state.moveY = maxMagnitude(touchMove.1, padMove.1)
        state.lookX = maxMagnitude(touchLook.0, padLook.0)
        state.lookY = maxMagnitude(touchLook.1, padLook.1)
        state.triggerLT = max(touchLT, padLT)
        state.triggerRT = max(touchRT, padRT)
    }

    private func maxMagnitude(_ a: Float, _ b: Float) -> Float {
        abs(a) >= abs(b) ? a : b
    }
}
