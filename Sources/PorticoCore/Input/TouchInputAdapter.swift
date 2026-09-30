import Foundation
import PorticoRuntime
/// Evento de toque normalizado no formato interno do WinOS (UITouch →
/// camada de entrada host, SEPARADA da representação Win32).
public struct TouchEvent: Equatable, Sendable {
    public enum Phase: UInt32, Sendable {
        case began = 0      // PR_TOUCH_BEGAN
        case moved = 1      // PR_TOUCH_MOVED
        case ended = 2      // PR_TOUCH_ENDED
        case cancelled = 3  // PR_TOUCH_CANCELLED
    }
    public let id: UInt32
    public let phase: Phase
    public let x: Int32
    public let y: Int32
    public init(id: UInt32, phase: Phase, x: Int32, y: Int32) {
        self.id = id
        self.phase = phase
        self.x = x
        self.y = y
    }
}

/// Pipeline iOS → WinOS: `UITouch` (CoreGraphics) → `TouchEvent` →
/// `pr_win32_input_touch` → mensagens Win32 (`WM_MOUSEMOVE`/`WM_LBUTTONDOWN`/…).
/// O toque primário é promovido a mouse; toques adicionais são rastreados
/// (multi-touch) sem gerar mensagens de mouse. Sem inventar eventos: o que
/// não chega do host não é gerado.
public struct TouchInputAdapter: Sendable {
    public init() {}

    /// Entrega o evento à fila de entrada do WinOS. `true` = aceito.
    @discardableResult
    public func dispatch(_ event: TouchEvent, to win32: OpaquePointer) -> Bool {
        return pr_win32_input_touch(win32, event.id, event.phase.rawValue,
                                    event.x, event.y) == PR_OK
    }

    /// Lote de eventos (ex.: um frame de UITouch do iOS).
    @discardableResult
    public func dispatch<S: Sequence>(_ events: S, to win32: OpaquePointer) -> Bool
        where S.Element == TouchEvent {
        var ok = true
        for e in events {
            ok = dispatch(e, to: win32) && ok
        }
        return ok
    }
}
