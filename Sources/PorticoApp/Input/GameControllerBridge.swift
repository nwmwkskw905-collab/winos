import Foundation
import GameController
#if !PORTICO_XCODE_MONOLITHIC
import PorticoCore
#endif
/// Integra controles físicos (GameController framework): MFi, Bluetooth,
/// controles de console pareados. Hotplug por notificação.
@MainActor
final class GameControllerBridge {
    private weak var router: InputRouter?
    private var connected: [GCController] = []
    public private(set) var statusText = "nenhum controle"

    func attach(router: InputRouter, enabled: Bool) {
        self.router = router
        guard enabled else { return }
        let center = NotificationCenter.default
        center.addObserver(self, selector: #selector(controllerConnected(_:)),
                           name: .GCControllerDidConnect, object: nil)
        center.addObserver(self, selector: #selector(controllerDisconnected(_:)),
                           name: .GCControllerDidDisconnect, object: nil)
        GCController.startWirelessControllerDiscovery()
        for c in GCController.controllers() {
            bind(c)
        }
        updateStatus()
    }

    func detach() {
        NotificationCenter.default.removeObserver(self)
        GCController.stopWirelessControllerDiscovery()
        for c in connected {
            c.extendedGamepad?.valueChangedHandler = nil
        }
        connected.removeAll()
        router?.reset()
    }

    @objc private func controllerConnected(_ note: Notification) {
        guard let c = note.object as? GCController else { return }
        bind(c)
        updateStatus()
    }

    @objc private func controllerDisconnected(_ note: Notification) {
        guard let c = note.object as? GCController else { return }
        connected.removeAll { $0 === c }
        updateStatus()
    }

    private func bind(_ controller: GCController) {
        guard let pad = controller.extendedGamepad else { return }
        connected.append(controller)
        pad.valueChangedHandler = { [weak self] _, element in
            Task { @MainActor in
                self?.handle(element: element, pad: pad)
            }
        }
    }

    private func handle(element: GCControllerElement, pad: GCExtendedGamepad) {
        guard let router else { return }
        // Botões faciais e ombros
        router.setPadButton(.a, pressed: pad.buttonA.isPressed)
        router.setPadButton(.b, pressed: pad.buttonB.isPressed)
        router.setPadButton(.x, pressed: pad.buttonX.isPressed)
        router.setPadButton(.y, pressed: pad.buttonY.isPressed)
        router.setPadButton(.lb, pressed: pad.leftShoulder.isPressed)
        router.setPadButton(.rb, pressed: pad.rightShoulder.isPressed)
        router.setPadButton(.start, pressed: pad.buttonMenu.isPressed)
        router.setPadButton(.select, pressed: pad.buttonOptions?.isPressed ?? false)
        // D-pad
        router.setPadButton(.dup, pressed: pad.dpad.up.isPressed)
        router.setPadButton(.ddown, pressed: pad.dpad.down.isPressed)
        router.setPadButton(.dleft, pressed: pad.dpad.left.isPressed)
        router.setPadButton(.dright, pressed: pad.dpad.right.isPressed)
        // Analógicos
        router.setPadMove(x: pad.leftThumbstick.xAxis.value,
                          y: -pad.leftThumbstick.yAxis.value)
        router.setPadLook(x: pad.rightThumbstick.xAxis.value,
                          y: -pad.rightThumbstick.yAxis.value)
        // Gatilhos
        router.setPadTriggers(lt: pad.leftTrigger.value,
                              rt: pad.rightTrigger.value)
    }

    private func updateStatus() {
        if connected.isEmpty {
            statusText = "nenhum controle"
        } else {
            let name = connected.first?.vendorName ?? "controle"
            statusText = "\(connected.count) controle(s) · \(name)"
        }
    }
}
