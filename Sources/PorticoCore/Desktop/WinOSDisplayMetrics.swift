import Foundation

/// FASE DESKTOP RESPONSIVO — Display Metrics centralizado
/// Separa DEVICE SCREEN → WINOS VIEWPORT → VIRTUAL DESKTOP → WINDOWS
/// Preserva aspect ratio, calcula escala dinâmica, offset, conversão coordenadas

public enum WinOSOrientation: String, Sendable, CaseIterable {
    case portrait = "PORTRAIT"
    case landscapeLeft = "LANDSCAPE_LEFT"
    case landscapeRight = "LANDSCAPE_RIGHT"
    case portraitUpsideDown = "PORTRAIT_UPSIDE_DOWN"
    
    public var isPortrait: Bool {
        self == .portrait || self == .portraitUpsideDown
    }
    
    public var isLandscape: Bool {
        !isPortrait
    }
}

public struct WinOSDisplayMetrics: Sendable, Equatable {
    // Device screen (physical iPhone)
    public var deviceScreenWidth: Double
    public var deviceScreenHeight: Double
    public var deviceScale: Double // UIScreen.main.scale
    
    // Viewport disponível para WinOS (pode excluir safe area, mas por simplicidade = screen)
    public var viewportWidth: Double
    public var viewportHeight: Double
    public var viewportX: Double = 0
    public var viewportY: Double = 0
    
    // Desktop virtual lógico (ex: 1280x720, 1920x1080)
    public var desktopWidth: Double
    public var desktopHeight: Double
    
    // Calculados dinamicamente — preservam aspect ratio
    public var scale: Double // min(viewportW/desktopW, viewportH/desktopH)
    public var displayWidth: Double // desktopW * scale
    public var displayHeight: Double // desktopH * scale
    public var offsetX: Double // (viewportW - displayW)/2
    public var offsetY: Double // (viewportH - displayH)/2
    
    public var orientation: WinOSOrientation
    
    public init(deviceScreenWidth: Double = 390, deviceScreenHeight: Double = 844, deviceScale: Double = 3.0,
                viewportWidth: Double? = nil, viewportHeight: Double? = nil,
                desktopWidth: Double = 1280, desktopHeight: Double = 720,
                orientation: WinOSOrientation = .portrait) {
        self.deviceScreenWidth = deviceScreenWidth
        self.deviceScreenHeight = deviceScreenHeight
        self.deviceScale = deviceScale
        self.viewportWidth = viewportWidth ?? deviceScreenWidth
        self.viewportHeight = viewportHeight ?? deviceScreenHeight
        self.desktopWidth = desktopWidth
        self.desktopHeight = desktopHeight
        self.orientation = orientation
        
        // Calcula escala preservando aspect ratio
        let scaleX = self.viewportWidth / self.desktopWidth
        let scaleY = self.viewportHeight / self.desktopHeight
        self.scale = min(scaleX, scaleY)
        self.displayWidth = self.desktopWidth * self.scale
        self.displayHeight = self.desktopHeight * self.scale
        self.offsetX = (self.viewportWidth - self.displayWidth) / 2.0 + self.viewportX
        self.offsetY = (self.viewportHeight - self.displayHeight) / 2.0 + self.viewportY
        
        NSLog("[WINOS-DISPLAY] init device=%.0fx%.0f viewport=%.0fx%.0f desktop=%.0fx%.0f scale=%.3f display=%.0fx%.0f offset=%.0f,%.0f orient=%@",
              deviceScreenWidth, deviceScreenHeight, self.viewportWidth, self.viewportHeight,
              desktopWidth, desktopHeight, self.scale, self.displayWidth, self.displayHeight,
              self.offsetX, self.offsetY, orientation.rawValue)
    }
    
    public func recalculated() -> WinOSDisplayMetrics {
        var m = self
        let scaleX = m.viewportWidth / m.desktopWidth
        let scaleY = m.viewportHeight / m.desktopHeight
        m.scale = min(scaleX, scaleY)
        m.displayWidth = m.desktopWidth * m.scale
        m.displayHeight = m.desktopHeight * m.scale
        m.offsetX = (m.viewportWidth - m.displayWidth) / 2.0 + m.viewportX
        m.offsetY = (m.viewportHeight - m.displayHeight) / 2.0 + m.viewportY
        return m
    }
    
    public func withOrientation(_ newOrientation: WinOSOrientation, deviceWidth: Double, deviceHeight: Double) -> WinOSDisplayMetrics {
        var m = self
        m.orientation = newOrientation
        m.deviceScreenWidth = deviceWidth
        m.deviceScreenHeight = deviceHeight
        m.viewportWidth = deviceWidth
        m.viewportHeight = deviceHeight
        return m.recalculated()
    }
    
    public func withViewport(width: Double, height: Double, x: Double = 0, y: Double = 0) -> WinOSDisplayMetrics {
        var m = self
        m.viewportWidth = width
        m.viewportHeight = height
        m.viewportX = x
        m.viewportY = y
        return m.recalculated()
    }
    
    public func withDesktopSize(width: Double, height: Double) -> WinOSDisplayMetrics {
        var m = self
        m.desktopWidth = width
        m.desktopHeight = height
        return m.recalculated()
    }
    
    // MARK: - Conversão centralizada (obrigatório para evitar bugs na rotação)
    
    /// screen → desktop (iPhone touch → desktop lógico)
    public func screenToDesktop(screenX: Double, screenY: Double) -> (x: Double, y: Double) {
        let dx = (screenX - offsetX) / scale
        let dy = (screenY - offsetY) / scale
        return (dx, dy)
    }
    
    /// desktop → screen (janela posição → screen)
    public func desktopToScreen(desktopX: Double, desktopY: Double) -> (x: Double, y: Double) {
        let sx = desktopX * scale + offsetX
        let sy = desktopY * scale + offsetY
        return (sx, sy)
    }
    
    /// screenToDesktop Int32 (para Win32)
    public func screenToDesktopInt32(screenX: Double, screenY: Double) -> (x: Int32, y: Int32) {
        let d = screenToDesktop(screenX: screenX, screenY: screenY)
        return (Int32(d.x), Int32(d.y))
    }
    
    /// Verifica se ponto screen está dentro da área do desktop exibido
    public func isInsideDesktopDisplay(screenX: Double, screenY: Double) -> Bool {
        screenX >= offsetX && screenX < offsetX + displayWidth &&
        screenY >= offsetY && screenY < offsetY + displayHeight
    }
    
    /// Clamp desktop coords para dentro do desktop virtual
    public func clampToDesktop(desktopX: Double, desktopY: Double) -> (x: Double, y: Double) {
        let cx = min(max(desktopX, 0), desktopWidth)
        let cy = min(max(desktopY, 0), desktopHeight)
        return (cx, cy)
    }
    
    public var description: String {
        "orientation=\(orientation.rawValue) viewport=\(Int(viewportWidth))x\(Int(viewportHeight)) desktop=\(Int(desktopWidth))x\(Int(desktopHeight)) scale=\(String(format: "%.3f", scale)) display=\(Int(displayWidth))x\(Int(displayHeight)) offset=\(Int(offsetX)),\(Int(offsetY))"
    }
    
    public var diagnosticsLog: String {
        "[WINOS-DISPLAY] orientation=\(orientation.rawValue) viewport=\(Int(viewportWidth))x\(Int(viewportHeight)) desktop=\(Int(desktopWidth))x\(Int(desktopHeight)) scale=\(String(format: "%.3f", scale)) display=\(Int(displayWidth))x\(Int(displayHeight)) offset=\(Int(offsetX)),\(Int(offsetY)) device=\(Int(deviceScreenWidth))x\(Int(deviceScreenHeight))@\(deviceScale)x"
    }
}

@MainActor
public final class WinOSDisplayManager: ObservableObject {
    @Published public private(set) var metrics: WinOSDisplayMetrics
    @Published public private(set) var orientation: WinOSOrientation = .portrait
    
    public var onMetricsChanged: ((WinOSDisplayMetrics) -> Void)?
    public var onOrientationChanged: ((WinOSOrientation, WinOSDisplayMetrics) -> Void)?
    
    public init(initialMetrics: WinOSDisplayMetrics = WinOSDisplayMetrics()) {
        self.metrics = initialMetrics
        self.orientation = initialMetrics.orientation
        NSLog("[WINOS-DISPLAY] DisplayManager init %@", metrics.diagnosticsLog)
    }
    
    public func updateDeviceScreen(width: Double, height: Double, scale: Double = 3.0) {
        let newOrient: WinOSOrientation = width > height ? .landscapeLeft : .portrait
        var newMetrics = metrics.withOrientation(newOrient, deviceWidth: width, deviceHeight: height)
        newMetrics.deviceScale = scale
        metrics = newMetrics
        orientation = newOrient
        
        NSLog("[WINOS-DISPLAY] updateDeviceScreen %@", metrics.diagnosticsLog)
        onMetricsChanged?(metrics)
        onOrientationChanged?(newOrient, metrics)
    }
    
    public func updateOrientation(_ newOrientation: WinOSOrientation, deviceWidth: Double, deviceHeight: Double) {
        guard newOrientation != orientation else { return }
        
        let oldMetrics = metrics
        let newMetrics = metrics.withOrientation(newOrientation, deviceWidth: deviceWidth, deviceHeight: deviceHeight)
        metrics = newMetrics
        orientation = newOrientation
        
        NSLog("[WINOS-DISPLAY] orientation changed %@ -> %@ | old=%@ new=%@",
              oldMetrics.orientation.rawValue, newOrientation.rawValue,
              oldMetrics.description, newMetrics.description)
        
        onMetricsChanged?(metrics)
        onOrientationChanged?(newOrientation, metrics)
    }
    
    public func updateViewport(width: Double, height: Double, x: Double = 0, y: Double = 0) {
        metrics = metrics.withViewport(width: width, height: height, x: x, y: y)
        NSLog("[WINOS-DISPLAY] updateViewport %@", metrics.diagnosticsLog)
        onMetricsChanged?(metrics)
    }
    
    public func updateDesktopSize(width: Double, height: Double) {
        metrics = metrics.withDesktopSize(width: width, height: height)
        NSLog("[WINOS-DISPLAY] updateDesktopSize %@", metrics.diagnosticsLog)
        onMetricsChanged?(metrics)
    }
    
    public func screenToDesktop(screenX: Double, screenY: Double) -> (x: Double, y: Double) {
        metrics.screenToDesktop(screenX: screenX, screenY: screenY)
    }
    
    public func desktopToScreen(desktopX: Double, desktopY: Double) -> (x: Double, y: Double) {
        metrics.desktopToScreen(desktopX: desktopX, desktopY: desktopY)
    }
}
