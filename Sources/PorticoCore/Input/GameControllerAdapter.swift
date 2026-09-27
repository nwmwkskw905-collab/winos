import Foundation
import PorticoRuntime
#if canImport(GameController)
import GameController
#endif

/// Amostra normalizada de controle (sem perfis nem mapeamentos de jogos):
/// botões (máscara `PR_BTN_*`), analógicos [-1, 1] e gatilhos [0, 1].
public struct GamePadSample: Equatable, Sendable {
    public var buttons: UInt32
    public var moveX: Float
    public var moveY: Float
    public var camX: Float
    public var camY: Float
    public var triggerLT: Float
    public var triggerRT: Float

    public init(buttons: UInt32 = 0, moveX: Float = 0, moveY: Float = 0,
                camX: Float = 0, camY: Float = 0,
                triggerLT: Float = 0, triggerRT: Float = 0) {
        self.buttons = buttons
        self.moveX = moveX
        self.moveY = moveY
        self.camX = camX
        self.camY = camY
        self.triggerLT = triggerLT
        self.triggerRT = triggerRT
    }
}

/// Infraestrutura de controle para o `GameController.framework` (iOS/macOS):
/// snapshot do controle → `pr_win32_input_controller` → `pr_input_state`
/// consultável pelo convidado. APENAS infraestrutura — mapeamentos
/// específicos de jogos ficam para camadas superiores.
public struct GameControllerAdapter: Sendable {
    public init() {}

    /// Aplica a amostra ao estado de controle do WinOS.
    @discardableResult
    public func apply(_ sample: GamePadSample, to win32: OpaquePointer) -> Bool {
        let buttonBits: [UInt32] = [
            1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048  // PR_BTN_*
        ]
        var ok = true
        for bit in buttonBits {
            let value: Float = (sample.buttons & bit) != 0 ? 1 : 0
            ok = (pr_win32_input_controller(win32, 0 /* PR_CTRL_BUTTON */,
                                            bit, value) == PR_OK) && ok
        }
        ok = (pr_win32_input_controller(win32, 1 /* PR_CTRL_AXIS */,
                                        0, sample.moveX) == PR_OK) && ok
        ok = (pr_win32_input_controller(win32, 1, 1, sample.moveY) == PR_OK) && ok
        ok = (pr_win32_input_controller(win32, 1, 2, sample.camX) == PR_OK) && ok
        ok = (pr_win32_input_controller(win32, 1, 3, sample.camY) == PR_OK) && ok
        ok = (pr_win32_input_controller(win32, 2 /* PR_CTRL_TRIGGER */,
                                        0, sample.triggerLT) == PR_OK) && ok
        ok = (pr_win32_input_controller(win32, 2, 1, sample.triggerRT) == PR_OK) && ok
        return ok
    }

    #if canImport(GameController)
    /// Snapshot genérico de um controle estendido (botões/analógicos/gatilhos).
    public static func sample(from pad: GCExtendedGamepad) -> GamePadSample {
        var buttons: UInt32 = 0
        if pad.buttonA.isPressed { buttons |= 1 }
        if pad.buttonB.isPressed { buttons |= 2 }
        if pad.buttonX.isPressed { buttons |= 4 }
        if pad.buttonY.isPressed { buttons |= 8 }
        if pad.leftShoulder.isPressed { buttons |= 16 }
        if pad.rightShoulder.isPressed { buttons |= 32 }
        if pad.dpad.up.isPressed { buttons |= 256 }
        if pad.dpad.down.isPressed { buttons |= 512 }
        if pad.dpad.left.isPressed { buttons |= 1024 }
        if pad.dpad.right.isPressed { buttons |= 2048 }
        return GamePadSample(
            buttons: buttons,
            moveX: pad.leftThumbstick.xAxis.value,
            moveY: pad.leftThumbstick.yAxis.value,
            camX: pad.rightThumbstick.xAxis.value,
            camY: pad.rightThumbstick.yAxis.value,
            triggerLT: pad.leftTrigger.value,
            triggerRT: pad.rightTrigger.value)
    }
    #endif
}
