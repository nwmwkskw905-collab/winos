import Foundation

/// CURSOR VISUAL — renderizado pelo compositor, acima das janelas
/// Ordem: background → windows → overlays → taskbar → cursor

public struct WinOSCursorVisual: Sendable {
    public var size: Double // tamanho em desktop coords (ex: 20px lógico)
    public var hotspotX: Double // ponto de clique dentro do cursor (ex: 0,0 para arrow)
    public var hotspotY: Double
    public var color: UInt32 // XRGB8888
    
    public init(size: Double = 20, hotspotX: Double = 0, hotspotY: Double = 0, color: UInt32 = 0xFFFFFFFF) {
        self.size = size
        self.hotspotX = hotspotX
        self.hotspotY = hotspotY
        self.color = color
    }
}

@MainActor
public final class WinOSCursorRenderer: ObservableObject {
    @Published public private(set) var isVisible: Bool = true
    @Published public private(set) var lastRenderTime: Double = 0
    
    private var cursor: WinOSMouseCursor
    private var displayMetrics: WinOSDisplayMetrics
    private var compositor: WinOSCompositor?
    
    private var cursorSurfaceID: UInt32?
    private var lastCursorX: Double = -1
    private var lastCursorY: Double = -1
    
    // Cursor bitmaps simples (arrow) — XRGB8888
    private var arrowPixels: [UInt32] = []
    private var handPixels: [UInt32] = []
    private var textPixels: [UInt32] = []
    private var busyPixels: [UInt32] = []
    
    public init(displayMetrics: WinOSDisplayMetrics = WinOSDisplayMetrics(), compositor: WinOSCompositor? = nil) {
        self.cursor = WinOSMouseCursor(x: 100, y: 100, visible: true)
        self.displayMetrics = displayMetrics
        self.compositor = compositor
        generateCursorBitmaps()
        NSLog("[WINOS-CURSOR-RENDER] CursorRenderer init")
    }
    
    public func setCompositor(_ compositor: WinOSCompositor) {
        self.compositor = compositor
    }
    
    public func updateDisplayMetrics(_ metrics: WinOSDisplayMetrics) {
        displayMetrics = metrics
        // Recria surface se necessário (escala mudou)
        if let sid = cursorSurfaceID {
            compositor?.destroySurface(id: sid)
            cursorSurfaceID = nil
        }
    }
    
    public func updateCursor(_ newCursor: WinOSMouseCursor) {
        let moved = abs(newCursor.x - cursor.x) > 0.5 || abs(newCursor.y - cursor.y) > 0.5
        let visibilityChanged = newCursor.visible != cursor.visible
        let typeChanged = newCursor.type != cursor.type
        
        cursor = newCursor
        
        if moved || visibilityChanged || typeChanged {
            render()
        }
    }
    
    private func generateCursorBitmaps() {
        // Arrow simples 16x24
        arrowPixels = generateArrowBitmap()
        handPixels = generateHandBitmap()
        textPixels = generateTextBitmap()
        busyPixels = generateBusyBitmap()
    }
    
    private func generateArrowBitmap() -> [UInt32] {
        // Arrow 16x24, branco com borda preta
        let w = 16, h = 24
        var pixels = [UInt32](repeating: 0x00000000, count: w*h) // transparente = 0
        // Desenha seta simples
        for y in 0..<h {
            for x in 0..<w {
                let idx = y*w + x
                // Triângulo arrow
                if y < 16 && x < y+1 {
                    pixels[idx] = 0xFFFFFFFF // branco
                } else if y >= 4 && y < 12 && x < 6 {
                    pixels[idx] = 0xFFFFFFFF
                }
                // Borda preta (simplificado)
                if (y < 16 && x == y) || (y == 11 && x < 6) {
                    pixels[idx] = 0xFF000000
                }
            }
        }
        return pixels
    }
    
    private func generateHandBitmap() -> [UInt32] {
        let w = 16, h = 24
        var pixels = [UInt32](repeating: 0x00000000, count: w*h)
        // Mão simples
        for y in 4..<20 {
            for x in 2..<14 {
                pixels[y*w + x] = 0xFFFFFFFF
            }
        }
        return pixels
    }
    
    private func generateTextBitmap() -> [UInt32] {
        let w = 8, h = 20
        var pixels = [UInt32](repeating: 0x00000000, count: w*h)
        for y in 0..<h {
            pixels[y*w + w/2] = 0xFFFFFFFF
            if y == 0 || y == h-1 {
                for x in 0..<w {
                    pixels[y*w + x] = 0xFFFFFFFF
                }
            }
        }
        return pixels
    }
    
    private func generateBusyBitmap() -> [UInt32] {
        let w = 16, h = 16
        var pixels = [UInt32](repeating: 0x00000000, count: w*h)
        // Círculo busy
        for y in 0..<h {
            for x in 0..<w {
                let dx = Double(x - w/2)
                let dy = Double(y - h/2)
                let dist = sqrt(dx*dx + dy*dy)
                if dist > 4 && dist < 8 {
                    pixels[y*w + x] = 0xFFFFFFFF
                }
            }
        }
        return pixels
    }
    
    private func pixelsForType(_ type: WinOSCursorType) -> (pixels: [UInt32], w: Int, h: Int) {
        switch type {
        case .arrow, .resizeHorizontal, .resizeVertical, .resizeDiagonal, .resizeDiagonal2, .move, .notAllowed:
            return (arrowPixels, 16, 24)
        case .hand:
            return (handPixels, 16, 24)
        case .text:
            return (textPixels, 8, 20)
        case .busy:
            return (busyPixels, 16, 16)
        case .crosshair:
            return (arrowPixels, 16, 24)
        }
    }
    
    public func render() {
        guard cursor.visible else {
            if let sid = cursorSurfaceID {
                compositor?.destroySurface(id: sid)
                cursorSurfaceID = nil
            }
            isVisible = false
            return
        }
        
        isVisible = true
        let (pixels, w, h) = pixelsForType(cursor.type)
        
        // Calcula posição em desktop coords, mas surface do compositor usa desktop coords também
        // O compositor depois será escalado para viewport via displayMetrics
        let x = Int32(cursor.x)
        let y = Int32(cursor.y)
        
        // Se já tem surface, move, senão cria
        if let sid = cursorSurfaceID {
            compositor?.moveSurface(id: sid, x: x, y: y)
            compositor?.updateSurface(id: sid, pixels: pixels)
        } else {
            // Cria surface com zIndex alto para ficar acima de tudo (cursor = topo)
            let sid = compositor?.createSurface(width: Int32(w), height: Int32(h), x: x, y: y, zIndex: 9999) ?? 0
            cursorSurfaceID = sid
            if sid != 0 {
                compositor?.updateSurface(id: sid, pixels: pixels)
            }
        }
        
        lastCursorX = cursor.x
        lastCursorY = cursor.y
        lastRenderTime = CACurrentMediaTime()
        
        // Log apenas quando move muito ou a cada 60 frames para não spammar
        // NSLog("[WINOS-CURSOR-RENDER] render x=%d y=%d type=%@ z=9999", x, y, cursor.type.rawValue)
    }
    
    public func hide() {
        if let sid = cursorSurfaceID {
            compositor?.destroySurface(id: sid)
            cursorSurfaceID = nil
        }
        isVisible = false
    }
    
    public func show() {
        isVisible = true
        render()
    }
    
    public func reset() {
        hide()
        cursor = WinOSMouseCursor(x: displayMetrics.desktopWidth/2, y: displayMetrics.desktopHeight/2, visible: true)
        NSLog("[WINOS-CURSOR-RENDER] reset")
    }
}
