import Foundation
#if canImport(UIKit)
import UIKit
#endif

/// ORIENTATION MANAGER — detecta mudança de orientação do iPhone e atualiza viewport/displayMetrics
/// Preserva posição relativa das janelas, evita distorção

@MainActor
public final class WinOSOrientationManager: ObservableObject {
    @Published public private(set) var currentOrientation: WinOSOrientation = .portrait
    @Published public private(set) var displayMetrics: WinOSDisplayMetrics
    
    private let displayManager: WinOSDisplayManager
    private var windowManager: WinOSWindowManager?
    private var lastDeviceWidth: Double = 390
    private var lastDeviceHeight: Double = 844
    
    public var onOrientationChanged: ((WinOSOrientation, WinOSDisplayMetrics) -> Void)?
    public var onWillRotate: ((WinOSOrientation) -> Void)?
    public var onDidRotate: ((WinOSOrientation) -> Void)?
    
    public init(displayManager: WinOSDisplayManager, windowManager: WinOSWindowManager? = nil) {
        self.displayManager = displayManager
        self.displayMetrics = displayManager.metrics
        self.windowManager = windowManager
        self.currentOrientation = displayManager.orientation
        
        setupObservers()
        NSLog("[WINOS-ORIENTATION] OrientationManager init orient=%@ metrics=%@", currentOrientation.rawValue, displayMetrics.description)
    }
    
    private func setupObservers() {
        #if canImport(UIKit)
        NotificationCenter.default.addObserver(self, selector: #selector(orientationDidChange), name: UIDevice.orientationDidChangeNotification, object: nil)
        NotificationCenter.default.addObserver(self, selector: #selector(willEnterForeground), name: UIApplication.willEnterForegroundNotification, object: nil)
        #endif
        
        displayManager.onOrientationChanged = { [weak self] orient, metrics in
            Task { @MainActor in
                self?.handleOrientationChanged(orient, metrics: metrics)
            }
        }
    }
    
    @objc private func orientationDidChange() {
        #if canImport(UIKit)
        let deviceOrient = UIDevice.current.orientation
        var newOrient: WinOSOrientation?
        
        switch deviceOrient {
        case .portrait:
            newOrient = .portrait
        case .landscapeLeft:
            newOrient = .landscapeLeft
        case .landscapeRight:
            newOrient = .landscapeRight
        case .portraitUpsideDown:
            newOrient = .portraitUpsideDown
        default:
            return // ignora faceUp, faceDown, unknown
        }
        
        if let orient = newOrient {
            let screenSize = UIScreen.main.bounds.size
            updateOrientation(orient, deviceWidth: Double(screenSize.width), deviceHeight: Double(screenSize.height))
        }
        #endif
    }
    
    @objc private func willEnterForeground() {
        // Revalida orientação ao voltar para foreground
        #if canImport(UIKit)
        let screenSize = UIScreen.main.bounds.size
        let isLandscape = screenSize.width > screenSize.height
        let orient: WinOSOrientation = isLandscape ? .landscapeLeft : .portrait
        updateOrientation(orient, deviceWidth: Double(screenSize.width), deviceHeight: Double(screenSize.height))
        #endif
    }
    
    public func updateOrientation(_ newOrientation: WinOSOrientation, deviceWidth: Double, deviceHeight: Double) {
        guard newOrientation != currentOrientation || deviceWidth != lastDeviceWidth || deviceHeight != lastDeviceHeight else { return }
        
        let oldOrientation = currentOrientation
        let oldMetrics = displayMetrics
        
        onWillRotate?(newOrientation)
        
        lastDeviceWidth = deviceWidth
        lastDeviceHeight = deviceHeight
        
        displayManager.updateOrientation(newOrientation, deviceWidth: deviceWidth, deviceHeight: deviceHeight)
        currentOrientation = newOrientation
        displayMetrics = displayManager.metrics
        
        // Preserva posição relativa das janelas
        preserveWindowPositions(oldMetrics: oldMetrics, newMetrics: displayMetrics)
        
        // Corrige janelas parcialmente fora da área útil
        ensureWindowsVisible()
        
        NSLog("[WINOS-ORIENTATION] orientation %@ -> %@ | device %.0fx%.0f | oldMetrics %@ | newMetrics %@",
              oldOrientation.rawValue, newOrientation.rawValue, deviceWidth, deviceHeight,
              oldMetrics.description, displayMetrics.description)
        
        onOrientationChanged?(newOrientation, displayMetrics)
        onDidRotate?(newOrientation)
    }
    
    public func updateDeviceScreen(width: Double, height: Double, scale: Double = 3.0) {
        displayManager.updateDeviceScreen(width: width, height: height, scale: scale)
        displayMetrics = displayManager.metrics
        currentOrientation = displayManager.orientation
        
        // Preserva janelas
        ensureWindowsVisible()
    }
    
    private func handleOrientationChanged(_ orientation: WinOSOrientation, metrics: WinOSDisplayMetrics) {
        let oldMetrics = displayMetrics
        let oldOrientation = currentOrientation

        currentOrientation = orientation
        displayMetrics = metrics

        preserveWindowPositions(oldMetrics: oldMetrics, newMetrics: metrics)
        ensureWindowsVisible()

        NSLog("[WINOS-ORIENTATION] display callback %@ -> %@ | metrics %@", oldOrientation.rawValue, orientation.rawValue, metrics.description)

        onOrientationChanged?(orientation, metrics)
        onDidRotate?(orientation)
    }

    private func preserveWindowPositions(oldMetrics: WinOSDisplayMetrics, newMetrics: WinOSDisplayMetrics) {
        guard let wm = windowManager else { return }
        
        // Preserva posição relativa: ex: janela em 50% da largura continua em 50%
        for win in wm.allWindows() {
            let relX = Double(win.x) / oldMetrics.desktopWidth
            let relY = Double(win.y) / oldMetrics.desktopHeight
            let newX = relX * newMetrics.desktopWidth
            let newY = relY * newMetrics.desktopHeight
            
            // Não move se já está bem posicionada, mas loga
            // A janela mantém tamanho, apenas posição relativa preservada
            // Na prática, mantemos posição lógica (não recalculamos), apenas garantimos visibilidade
            // Mas logamos para diagnóstico
            NSLog("[WINOS-ORIENTATION] preserve win=%u oldPos=%d,%d rel=%.2f,%.2f newPos=%.0f,%.0f (kept logical, only ensuring visible)",
                  win.id.raw, win.x, win.y, relX, relY, newX, newY)
        }
    }
    
    private func ensureWindowsVisible() {
        guard let wm = windowManager else { return }
        
        let desktopW = Int32(displayMetrics.desktopWidth)
        let desktopH = Int32(displayMetrics.desktopHeight)
        let minVisible: Int32 = 50 // pelo menos 50px visível
        
        for win in wm.allWindows() {
            var newX = win.x
            var newY = win.y
            var moved = false
            
            // Se janela totalmente fora à direita, traz para esquerda
            if win.x >= desktopW {
                newX = desktopW - minVisible
                moved = true
            }
            // Se totalmente fora à esquerda
            if win.x + win.width <= 0 {
                newX = 0
                moved = true
            }
            // Se totalmente fora embaixo
            if win.y >= desktopH {
                newY = desktopH - minVisible
                moved = true
            }
            // Se totalmente fora em cima (considera taskbar)
            if win.y + win.height <= 0 {
                newY = 0
                moved = true
            }
            
            // Clamp para garantir pelo menos parte visível
            if win.x + minVisible > desktopW {
                newX = desktopW - minVisible
                moved = true
            }
            if win.y + minVisible > desktopH {
                newY = desktopH - minVisible
                moved = true
            }
            
            if moved {
                wm.moveWindow(id: win.id, x: newX, y: newY)
                NSLog("[WINOS-ORIENTATION] ensureVisible moved win=%u from %d,%d to %d,%d", win.id.raw, win.x, win.y, newX, newY)
            }
        }
    }
    
    public func setWindowManager(_ wm: WinOSWindowManager) {
        windowManager = wm
    }
    
    deinit {
        NotificationCenter.default.removeObserver(self)
    }
}
