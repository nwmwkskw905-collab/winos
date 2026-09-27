import Foundation

/// Ações mapeáveis nos controles (virtuais ou físicos).
public enum InputAction: String, CaseIterable, Sendable, Codable {
    case moveLeft, moveRight, moveUp, moveDown
    case lookLeft, lookRight, lookUp, lookDown
    case buttonA, buttonB, buttonX, buttonY
    case bumperLeft, bumperRight
    case triggerLeft, triggerRight
    case start, select

    public var displayName: String {
        switch self {
        case .moveLeft: return "Mover ←"
        case .moveRight: return "Mover →"
        case .moveUp: return "Mover ↑"
        case .moveDown: return "Mover ↓"
        case .lookLeft: return "Olhar ←"
        case .lookRight: return "Olhar →"
        case .lookUp: return "Olhar ↑"
        case .lookDown: return "Olhar ↓"
        case .buttonA: return "Botão A"
        case .buttonB: return "Botão B"
        case .buttonX: return "Botão X"
        case .buttonY: return "Botão Y"
        case .bumperLeft: return "LB"
        case .bumperRight: return "RB"
        case .triggerLeft: return "LT"
        case .triggerRight: return "RT"
        case .start: return "Start"
        case .select: return "Select"
        }
    }
}

/// Tipo de elemento de controle na tela.
public enum ControlKind: String, CaseIterable, Sendable, Codable {
    case dPad
    case faceButton
    case analogStick
    case shoulder
    case trigger
    case startButton
    case selectButton

    public var displayName: String {
        switch self {
        case .dPad: return "D-pad"
        case .faceButton: return "Botão"
        case .analogStick: return "Analógico"
        case .shoulder: return "Shoulder"
        case .trigger: return "Gatilho"
        case .startButton: return "Start"
        case .selectButton: return "Select"
        }
    }
}

/// Retângulo normalizado (0...1) relativo à área de jogo.
public struct NormalizedRect: Equatable, Sendable, Codable {
    public var x: Double
    public var y: Double
    public var w: Double
    public var h: Double

    public init(x: Double, y: Double, w: Double, h: Double) {
        self.x = min(max(x, -0.5), 1.5)
        self.y = min(max(y, -0.5), 1.5)
        self.w = min(max(w, 0.02), 2)
        self.h = min(max(h, 0.02), 2)
    }
}

/// Elemento de controle configurável (posição, tamanho, transparência, função).
public struct ControlElement: Identifiable, Equatable, Sendable, Codable {
    public var id: UUID
    public var kind: ControlKind
    public var action: InputAction
    public var frame: NormalizedRect
    public var opacity: Double
    public var label: String
    public var colorHex: String

    public init(id: UUID = UUID(),
                kind: ControlKind,
                action: InputAction,
                frame: NormalizedRect,
                opacity: Double = 0.75,
                label: String = "",
                colorHex: String = "#FFFFFF") {
        self.id = id
        self.kind = kind
        self.action = action
        self.frame = frame
        self.opacity = min(max(opacity, 0.05), 1)
        self.label = label.isEmpty ? action.displayName : label
        self.colorHex = colorHex
    }
}

/// Layout completo de controles por jogo.
public struct ControlProfile: Equatable, Sendable, Codable {
    public var elements: [ControlElement]
    public var physicalControllersEnabled: Bool
    public var showTouchControls: Bool

    public init(elements: [ControlElement] = ControlProfile.defaultLayout(),
                physicalControllersEnabled: Bool = true,
                showTouchControls: Bool = true) {
        self.elements = elements
        self.physicalControllersEnabled = physicalControllersEnabled
        self.showTouchControls = showTouchControls
    }

    /// Layout padrão: analógico esquerdo, 4 botões, ombros e start.
    public static func defaultLayout() -> [ControlElement] {
        [
            ControlElement(kind: .analogStick, action: .moveLeft,
                           frame: NormalizedRect(x: 0.05, y: 0.62, w: 0.28, h: 0.32),
                           label: "Mover"),
            ControlElement(kind: .faceButton, action: .buttonA,
                           frame: NormalizedRect(x: 0.82, y: 0.72, w: 0.10, h: 0.16), label: "A"),
            ControlElement(kind: .faceButton, action: .buttonB,
                           frame: NormalizedRect(x: 0.90, y: 0.58, w: 0.10, h: 0.16), label: "B"),
            ControlElement(kind: .faceButton, action: .buttonX,
                           frame: NormalizedRect(x: 0.74, y: 0.58, w: 0.10, h: 0.16), label: "X"),
            ControlElement(kind: .faceButton, action: .buttonY,
                           frame: NormalizedRect(x: 0.82, y: 0.44, w: 0.10, h: 0.16), label: "Y"),
            ControlElement(kind: .shoulder, action: .bumperLeft,
                           frame: NormalizedRect(x: 0.05, y: 0.04, w: 0.14, h: 0.10), label: "LB"),
            ControlElement(kind: .shoulder, action: .bumperRight,
                           frame: NormalizedRect(x: 0.81, y: 0.04, w: 0.14, h: 0.10), label: "RB"),
            ControlElement(kind: .startButton, action: .start,
                           frame: NormalizedRect(x: 0.45, y: 0.05, w: 0.10, h: 0.09), label: "Start"),
        ]
    }
}
