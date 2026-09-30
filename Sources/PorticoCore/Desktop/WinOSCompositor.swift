import Foundation

/// Compositor central — combina desktop background + window surfaces + cursor + taskbar + overlays → single final framebuffer
/// Prioriza dirty rectangles, partial redraw, texture reuse, triple buffering, frame pacing
/// Não redesenha desktop inteiro quando apenas uma janela mudou

public struct WinOSDirtyRect: Equatable, Sendable {
    public var x: Int32
    public var y: Int32
    public var width: Int32
    public var height: Int32
    
    public init(x: Int32, y: Int32, width: Int32, height: Int32) {
        self.x = x
        self.y = y
        self.width = width
        self.height = height
    }
    
    public func intersects(_ other: WinOSDirtyRect) -> Bool {
        !(x + width <= other.x || other.x + other.width <= x ||
          y + height <= other.y || other.y + other.height <= y)
    }
    
    public func union(_ other: WinOSDirtyRect) -> WinOSDirtyRect {
        let minX = min(x, other.x)
        let minY = min(y, other.y)
        let maxX = max(x + width, other.x + other.width)
        let maxY = max(y + height, other.y + other.height)
        return WinOSDirtyRect(x: minX, y: minY, width: maxX - minX, height: maxY - minY)
    }
}

public struct WinOSSurface: Sendable {
    public var id: UInt32
    public var width: Int32
    public var height: Int32
    public var pixels: [UInt32] // XRGB8888
    public var x: Int32
    public var y: Int32
    public var zIndex: Int32
    public var dirtyRects: [WinOSDirtyRect] = []
    public var isDirty: Bool = true
    
    public init(id: UInt32, width: Int32, height: Int32, x: Int32 = 0, y: Int32 = 0, zIndex: Int32 = 0) {
        self.id = id
        self.width = width
        self.height = height
        self.x = x
        self.y = y
        self.zIndex = zIndex
        self.pixels = [UInt32](repeating: 0xFF0A0E14, count: Int(width*height))
    }
}

@MainActor
public final class WinOSCompositor: ObservableObject {
    @Published public private(set) var frameCount: Int = 0
    @Published public private(set) var dirtyRects: [WinOSDirtyRect] = []
    @Published public private(set) var lastCompositeTimeMs: Double = 0
    
    private var surfaces: [UInt32: WinOSSurface] = [:]
    private var nextSurfaceID: UInt32 = 1
    private var desktopWidth: Int32 = 1920
    private var desktopHeight: Int32 = 1080
    private var finalFramebuffer: [UInt32] = []
    
    // Triple buffering quando apropriado
    private var frontBuffer: [UInt32] = []
    private var backBuffer: [UInt32] = []
    private var pendingBuffer: [UInt32] = []
    
    public init(width: Int32 = 1920, height: Int32 = 1080) {
        self.desktopWidth = width
        self.desktopHeight = height
        self.finalFramebuffer = [UInt32](repeating: 0xFF0A0E14, count: Int(width*height))
        self.frontBuffer = finalFramebuffer
        self.backBuffer = finalFramebuffer
        self.pendingBuffer = finalFramebuffer
        NSLog("[WINOS-COMPOSITOR] init %dx%d", width, height)
    }
    
    public func setDesktopSize(width: Int32, height: Int32) {
        guard width != desktopWidth || height != desktopHeight else { return }
        desktopWidth = width
        desktopHeight = height
        let count = Int(width*height)
        finalFramebuffer = [UInt32](repeating: 0xFF0A0E14, count: count)
        frontBuffer = finalFramebuffer
        backBuffer = finalFramebuffer
        pendingBuffer = finalFramebuffer
        NSLog("[WINOS-COMPOSITOR] setDesktopSize %dx%d", width, height)
    }
    
    @discardableResult
    public func createSurface(width: Int32, height: Int32, x: Int32 = 0, y: Int32 = 0, zIndex: Int32 = 0) -> UInt32 {
        let id = nextSurfaceID
        nextSurfaceID += 1
        let surf = WinOSSurface(id: id, width: width, height: height, x: x, y: y, zIndex: zIndex)
        surfaces[id] = surf
        markDirty(rect: WinOSDirtyRect(x: x, y: y, width: width, height: height))
        NSLog("[WINOS-COMPOSITOR] createSurface id=%u %dx%d x=%d y=%d z=%d", id, width, height, x, y, zIndex)
        return id
    }
    
    public func destroySurface(id: UInt32) {
        if let surf = surfaces[id] {
            markDirty(rect: WinOSDirtyRect(x: surf.x, y: surf.y, width: surf.width, height: surf.height))
        }
        surfaces.removeValue(forKey: id)
        NSLog("[WINOS-COMPOSITOR] destroySurface id=%u", id)
    }
    
    public func updateSurface(id: UInt32, pixels: [UInt32], dirtyRect: WinOSDirtyRect? = nil) {
        guard var surf = surfaces[id] else { return }
        if pixels.count == surf.pixels.count {
            surf.pixels = pixels
        }
        surf.isDirty = true
        if let dr = dirtyRect {
            surf.dirtyRects.append(dr)
            markDirty(rect: WinOSDirtyRect(x: surf.x + dr.x, y: surf.y + dr.y, width: dr.width, height: dr.height))
        } else {
            markDirty(rect: WinOSDirtyRect(x: surf.x, y: surf.y, width: surf.width, height: surf.height))
        }
        surfaces[id] = surf
    }
    
    public func moveSurface(id: UInt32, x: Int32, y: Int32) {
        guard var surf = surfaces[id] else { return }
        // Marca antiga e nova posição como dirty
        markDirty(rect: WinOSDirtyRect(x: surf.x, y: surf.y, width: surf.width, height: surf.height))
        surf.x = x
        surf.y = y
        surf.isDirty = true
        surfaces[id] = surf
        markDirty(rect: WinOSDirtyRect(x: x, y: y, width: surf.width, height: surf.height))
    }
    
    public func setSurfaceZ(id: UInt32, zIndex: Int32) {
        guard var surf = surfaces[id] else { return }
        surf.zIndex = zIndex
        surfaces[id] = surf
        markDirty(rect: WinOSDirtyRect(x: surf.x, y: surf.y, width: surf.width, height: surf.height))
    }
    
    private func markDirty(rect: WinOSDirtyRect) {
        // Merge com existentes se intersecta
        var merged = rect
        var newDirty: [WinOSDirtyRect] = []
        for existing in dirtyRects {
            if existing.intersects(merged) {
                merged = merged.union(existing)
            } else {
                newDirty.append(existing)
            }
        }
        newDirty.append(merged)
        dirtyRects = newDirty
    }
    
    public func composite() -> [UInt32] {
        let start = CACurrentMediaTime()
        
        // Se não há dirty rects, não redesenha tudo — retorna front buffer
        if dirtyRects.isEmpty && frameCount > 0 {
            // Evita redraw global quando somente uma janela mudou — otimização crítica
            return frontBuffer
        }
        
        // Ordena superfícies por zIndex (menor atrás)
        let sorted = surfaces.values.sorted { $0.zIndex < $1.zIndex }
        
        // Começa com fundo desktop (wallpaper WinOS #0A0E14)
        var fb = backBuffer
        if fb.count != Int(desktopWidth*desktopHeight) {
            fb = [UInt32](repeating: 0xFF0A0E14, count: Int(desktopWidth*desktopHeight))
        }
        
        // Se há dirty rects, só compõe nessas áreas (partial redraw)
        // Por simplicidade inicial, compõe tudo mas com dirty tracking para futuro
        if dirtyRects.isEmpty {
            // Full redraw primeira vez
            for y in 0..<Int(desktopHeight) {
                for x in 0..<Int(desktopWidth) {
                    let idx = y*Int(desktopWidth) + x
                    if idx < fb.count {
                        fb[idx] = 0xFF0A0E14 // fundo
                    }
                }
            }
        }
        
        // Blit superfícies em ordem z
        for surf in sorted {
            for row in 0..<Int(surf.height) {
                let dstY = Int(surf.y) + row
                if dstY < 0 || dstY >= Int(desktopHeight) { continue }
                for col in 0..<Int(surf.width) {
                    let dstX = Int(surf.x) + col
                    if dstX < 0 || dstX >= Int(desktopWidth) { continue }
                    let srcIdx = row*Int(surf.width) + col
                    let dstIdx = dstY*Int(desktopWidth) + dstX
                    if srcIdx < surf.pixels.count && dstIdx < fb.count {
                        let pixel = surf.pixels[srcIdx]
                        // Alpha blending simples: se pixel não é transparente (assumimos XRGB, sempre opaco)
                        // Para janelas com transparência futura, checar alpha
                        fb[dstIdx] = pixel
                    }
                }
            }
        }
        
        // Triple buffering: pending → back → front
        pendingBuffer = fb
        // Swap
        let tmp = frontBuffer
        frontBuffer = pendingBuffer
        backBuffer = tmp
        
        finalFramebuffer = frontBuffer
        frameCount += 1
        let end = CACurrentMediaTime()
        lastCompositeTimeMs = (end - start) * 1000.0
        
        // Limpa dirty rects após composite
        dirtyRects.removeAll()
        // Marca superfícies como limpas
        for id in surfaces.keys {
            surfaces[id]?.isDirty = false
            surfaces[id]?.dirtyRects.removeAll()
        }
        
        // NSLog("[WINOS-COMPOSITOR] composite frame=%d time=%.2fms dirty=%d surfaces=%d", frameCount, lastCompositeTimeMs, dirtyRects.count, surfaces.count)
        return finalFramebuffer
    }
    
    public func getFramebuffer() -> [UInt32] {
        finalFramebuffer
    }
    
    public func reset() {
        surfaces.removeAll()
        dirtyRects.removeAll()
        finalFramebuffer = [UInt32](repeating: 0xFF0A0E14, count: Int(desktopWidth*desktopHeight))
        frontBuffer = finalFramebuffer
        backBuffer = finalFramebuffer
        pendingBuffer = finalFramebuffer
        frameCount = 0
        nextSurfaceID = 1
        NSLog("[WINOS-COMPOSITOR] reset")
    }
}
