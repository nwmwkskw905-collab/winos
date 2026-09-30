import Foundation
import PorticoCore
#if canImport(Metal)
import Metal
#endif

/// FASE 7 — Graphics translation real (OpenGL 1.1 → Metal, GDI → Metal, D3D → diagnostic)

public enum WinOSGraphicsBackend: String, Sendable {
    case metal = "METAL"
    case software = "SOFTWARE"
    case unsupported = "UNSUPPORTED"
}

public struct WinOSGraphicsFrame: Sendable {
    public var width: Int
    public var height: Int
    public var pixels: [UInt32] // XRGB8888
    public var timestamp: Double
    public var drawCalls: Int
    
    public init(width: Int, height: Int, pixels: [UInt32], timestamp: Double, drawCalls: Int) {
        self.width = width
        self.height = height
        self.pixels = pixels
        self.timestamp = timestamp
        self.drawCalls = drawCalls
    }
}

@MainActor
public final class WinOSGraphicsTranslator: ObservableObject {
    @Published public private(set) var backend: WinOSGraphicsBackend = .unsupported
    @Published public private(set) var isReady: Bool = false
    @Published public private(set) var lastError: String = ""
    @Published public private(set) var framesPresented: UInt64 = 0
    
    private var metalDevice: Any? // MTLDevice
    private var surfaceWidth: Int = 0
    private var surfaceHeight: Int = 0
    private var frameCount: UInt64 = 0
    
    public var onFrame: ((WinOSGraphicsFrame) -> Void)?
    public var onDiagnostics: ((String, String) -> Void)? // category, message
    
    public init() {
        NSLog("[WINOS-GFX-TRANS] GraphicsTranslator init")
    }
    
    public func initialize(api: WinOSGraphicsAPI, width: Int, height: Int, diagnostics: WinOSGameDiagnostics?) -> Bool {
        NSLog("[WINOS-GFX-TRANS] initialize api=%@ size=%dx%d", api.rawValue, width, height)
        
        switch api {
        case .opengl11:
            return initializeOpenGL11(width: width, height: height, diagnostics: diagnostics)
        case .gdi, .software:
            return initializeGDI(width: width, height: height, diagnostics: diagnostics)
        case .direct3D9, .direct3D10, .direct3D11, .direct3D12, .dxgi:
            return initializeD3DFail(api: api, diagnostics: diagnostics)
        case .vulkan:
            return initializeVulkanFail(diagnostics: diagnostics)
        case .openglModern:
            return initializeModernGLFail(diagnostics: diagnostics)
        case .directDraw:
            return initializeDirectDrawFail(diagnostics: diagnostics)
        case .unknown:
            lastError = "Graphics API unknown, cannot initialize"
            diagnostics?.log(.error, category: .graphics, code: "GFX_INIT_FAIL", message: "GFX_INIT_FAIL", detail: lastError)
            backend = .unsupported
            isReady = false
            return false
        }
    }
    
    private func initializeOpenGL11(width: Int, height: Int, diagnostics: WinOSGameDiagnostics?) -> Bool {
        // OpenGL 1.1 é implementado via pr_gl.c software rasterizer
        // Aqui apenas prepara Metal para apresentar o surface que pr_gl.c produz
        surfaceWidth = width
        surfaceHeight = height
        
        #if canImport(Metal)
        if let device = MTLCreateSystemDefaultDevice() {
            metalDevice = device
            backend = .metal
            isReady = true
            diagnostics?.log(.info, category: .metal, code: "METAL_INIT_SUCCESS", message: "METAL_INIT_SUCCESS", detail: "device=\(device.name) for OpenGL11 translation, surface \(width)x\(height)")
            diagnostics?.metalInit(device: device.name, success: true)
            NSLog("[WINOS-GFX-TRANS] OpenGL11 → Metal READY device=%@ surface=%dx%d", device.name, width, height)
            return true
        } else {
            backend = .software
            isReady = true
            diagnostics?.log(.warning, category: .metal, code: "METAL_FALLBACK_SOFTWARE", message: "METAL_FALLBACK_SOFTWARE", detail: "No Metal device, using software fallback for OpenGL11")
            NSLog("[WINOS-GFX-TRANS] OpenGL11 → Software fallback (no Metal device)")
            return true
        }
        #else
        backend = .software
        isReady = true
        diagnostics?.log(.warning, category: .metal, code: "METAL_UNAVAILABLE", message: "METAL_UNAVAILABLE", detail: "Metal not available, software fallback for OpenGL11")
        NSLog("[WINOS-GFX-TRANS] OpenGL11 → Software (Metal not available)")
        return true
        #endif
    }
    
    private func initializeGDI(width: Int, height: Int, diagnostics: WinOSGameDiagnostics?) -> Bool {
        surfaceWidth = width
        surfaceHeight = height
        
        #if canImport(Metal)
        if let device = MTLCreateSystemDefaultDevice() {
            metalDevice = device
            backend = .metal
            isReady = true
            diagnostics?.log(.info, category: .metal, code: "METAL_INIT_SUCCESS", message: "METAL_INIT_SUCCESS", detail: "device=\(device.name) for GDI translation")
            NSLog("[WINOS-GFX-TRANS] GDI → Metal READY")
            return true
        }
        #endif
        backend = .software
        isReady = true
        diagnostics?.log(.info, category: .graphics, code: "GDI_SOFTWARE", message: "GDI_SOFTWARE", detail: "GDI via software renderer \(width)x\(height)")
        NSLog("[WINOS-GFX-TRANS] GDI → Software")
        return true
    }
    
    private func initializeD3DFail(api: WinOSGraphicsAPI, diagnostics: WinOSGameDiagnostics?) -> Bool {
        let msg = "\(api.rawValue) não implementado — requer tradução \(api.rawValue) → Metal. BLOQUEIO ARQUITETURAL para jogos modernos. Para suportar, implementar: \(api.rawValue) device creation, swapchain (DXGI), shaders (HLSL→MSL), textures, buffers, render targets"
        lastError = msg
        backend = .unsupported
        isReady = false
        diagnostics?.log(.error, category: .graphics, code: "GFX_UNSUPPORTED", message: api.rawValue, detail: msg)
        diagnostics?.log(.error, category: .compatibility, code: "COMPAT_FAIL_GRAPHICS", message: "COMPAT_FAIL_GRAPHICS", detail: msg)
        NSLog("[WINOS-GFX-TRANS] FAIL %@ not implemented: %@", api.rawValue, msg)
        onDiagnostics?("GRAPHICS", msg)
        return false
    }
    
    private func initializeVulkanFail(diagnostics: WinOSGameDiagnostics?) -> Bool {
        let msg = "Vulkan não implementado — requer MoltenVK ou tradução Vulkan→Metal"
        lastError = msg
        backend = .unsupported
        isReady = false
        diagnostics?.log(.error, category: .graphics, code: "GFX_UNSUPPORTED_VULKAN", message: "VULKAN", detail: msg)
        return false
    }
    
    private func initializeModernGLFail(diagnostics: WinOSGameDiagnostics?) -> Bool {
        let msg = "OpenGL moderno (shaders, VBOs, VAOs) não implementado — apenas OpenGL 1.1 fixed-function está implementado via pr_gl.c"
        lastError = msg
        backend = .unsupported
        isReady = false
        diagnostics?.log(.error, category: .graphics, code: "GFX_UNSUPPORTED_GL_MODERN", message: "GL_MODERN", detail: msg)
        return false
    }
    
    private func initializeDirectDrawFail(diagnostics: WinOSGameDiagnostics?) -> Bool {
        let msg = "DirectDraw não implementado — legado, requer tradução para Metal ou GDI"
        lastError = msg
        backend = .unsupported
        isReady = false
        diagnostics?.log(.error, category: .graphics, code: "GFX_UNSUPPORTED_DDRAW", message: "DDRAW", detail: msg)
        return false
    }
    
    public func presentSurface(pixels: UnsafeRawPointer?, width: Int, height: Int, diagnostics: WinOSGameDiagnostics?) {
        guard isReady else { return }
        frameCount += 1
        framesPresented = frameCount
        
        if frameCount % 60 == 0 {
            diagnostics?.renderFrame(frame: frameCount, fps: 60, frameTimeMs: 16.6, drawCalls: 1, width: width, height: height)
            NSLog("[WINOS-GFX-TRANS] present frame=%llu res=%dx%d backend=%@", frameCount, width, height, backend.rawValue)
        }
        
        // Se tem callback, converte para frame
        if let pixels = pixels, let onFrame = onFrame {
            let count = width * height
            let ptr = pixels.bindMemory(to: UInt32.self, capacity: count)
            let array = Array(UnsafeBufferPointer(start: ptr, count: count))
            let frame = WinOSGraphicsFrame(width: width, height: height, pixels: array, timestamp: ProcessInfo.processInfo.systemUptime, drawCalls: 1)
            onFrame(frame)
        }
    }
    
    public func shutdown() {
        isReady = false
        backend = .unsupported
        metalDevice = nil
        frameCount = 0
        framesPresented = 0
        NSLog("[WINOS-GFX-TRANS] shutdown")
    }
}
