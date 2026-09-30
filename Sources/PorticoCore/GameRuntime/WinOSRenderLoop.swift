import Foundation
import PorticoCore
#if canImport(Metal)
import Metal
import MetalKit
#endif

/// FASE 8 — Render loop real com métricas FPS, frameTime, present

public struct WinOSRenderStats: Sendable {
    public var fps: Double = 0
    public var frameTimeMs: Double = 0
    public var avgFrameTimeMs: Double = 0
    public var minFrameTimeMs: Double = Double.greatestFiniteMagnitude
    public var maxFrameTimeMs: Double = 0
    public var totalFrames: UInt64 = 0
    public var droppedFrames: UInt64 = 0
    public var drawCalls: Int = 0
    public var presentCount: UInt64 = 0
    public var backend: String = "UNKNOWN"
    public var resolution: String = "0x0"
    public var timestamp: Date = Date()
    
    public var description: String {
        "FPS: \(String(format: "%.1f", fps)) frameTime: \(String(format: "%.2f", frameTimeMs))ms avg: \(String(format: "%.2f", avgFrameTimeMs))ms total: \(totalFrames) dropped: \(droppedFrames) backend: \(backend) res: \(resolution)"
    }
}

@MainActor
public final class WinOSRenderLoop: ObservableObject {
    @Published public private(set) var stats: WinOSRenderStats = WinOSRenderStats()
    @Published public private(set) var isRunning: Bool = false
    
    private var displayLink: CADisplayLink?
    private var lastFrameTime: Double = 0
    private var frameTimes: [Double] = []
    private let maxFrameTimes = 60
    private var totalFrames: UInt64 = 0
    private var presentCount: UInt64 = 0
    private var droppedFrames: UInt64 = 0
    private var targetFPS: Int = 60
    private var width: Int = 0
    private var height: Int = 0
    private var backend: String = "UNKNOWN"
    
    private var diagnostics: WinOSGameDiagnostics?
    
    public var onFrame: (() -> Void)?
    public var onPresent: ((Int, Int) -> Void)?
    
    public init() {
        NSLog("[WINOS-RENDER-LOOP] RenderLoop init")
    }
    
    public func configure(width: Int, height: Int, targetFPS: Int, backend: String, diagnostics: WinOSGameDiagnostics?) {
        self.width = width
        self.height = height
        self.targetFPS = min(max(targetFPS, 15), 120)
        self.backend = backend
        self.diagnostics = diagnostics
        NSLog("[WINOS-RENDER-LOOP] configure res=%dx%d targetFPS=%d backend=%@", width, height, targetFPS, backend)
    }
    
    public func start() {
        guard !isRunning else { return }
        isRunning = true
        totalFrames = 0
        presentCount = 0
        droppedFrames = 0
        frameTimes.removeAll()
        lastFrameTime = ProcessInfo.processInfo.systemUptime
        
        displayLink = CADisplayLink(target: self, selector: #selector(displayLinkFired))
        displayLink?.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: Float(targetFPS), preferred: Float(targetFPS))
        displayLink?.add(to: .main, forMode: .common)
        
        diagnostics?.log(.info, category: .render, code: "RENDER_LOOP_START", message: "RENDER_LOOP_START", detail: "res=\(width)x\(height) targetFPS=\(targetFPS) backend=\(backend)")
        NSLog("[WINOS-RENDER-LOOP] start targetFPS=%d", targetFPS)
    }
    
    public func stop() {
        guard isRunning else { return }
        isRunning = false
        displayLink?.invalidate()
        displayLink = nil
        
        diagnostics?.log(.info, category: .render, code: "RENDER_LOOP_STOP", message: "RENDER_LOOP_STOP", detail: "totalFrames=\(totalFrames) present=\(presentCount) dropped=\(droppedFrames) avgFPS=\(String(format: "%.1f", stats.fps))")
        NSLog("[WINOS-RENDER-LOOP] stop total=%llu present=%llu dropped=%llu", totalFrames, presentCount, droppedFrames)
    }
    
    @objc private func displayLinkFired() {
        let now = ProcessInfo.processInfo.systemUptime
        let dt = now - lastFrameTime
        lastFrameTime = now
        
        totalFrames += 1
        
        // Frame time tracking
        let frameTimeMs = dt * 1000.0
        frameTimes.append(frameTimeMs)
        if frameTimes.count > maxFrameTimes {
            frameTimes.removeFirst()
        }
        
        let avg = frameTimes.isEmpty ? 0 : frameTimes.reduce(0, +) / Double(frameTimes.count)
        let min = frameTimes.min() ?? 0
        let max = frameTimes.max() ?? 0
        
        // Dropped frame detection: if frameTime > 1.5 * target
        let targetMs = 1000.0 / Double(targetFPS)
        if frameTimeMs > targetMs * 1.5 {
            droppedFrames += 1
        }
        
        let fps = dt > 0 ? 1.0 / dt : 0
        
        stats = WinOSRenderStats(
            fps: fps,
            frameTimeMs: frameTimeMs,
            avgFrameTimeMs: avg,
            minFrameTimeMs: min,
            maxFrameTimeMs: max,
            totalFrames: totalFrames,
            droppedFrames: droppedFrames,
            drawCalls: 1,
            presentCount: presentCount,
            backend: backend,
            resolution: "\(width)x\(height)",
            timestamp: Date()
        )
        
        // Callbacks
        onFrame?()
        
        // Present logic — here would be Metal present
        // For now, just count present when onPresent called externally
        if totalFrames % 60 == 0 {
            NSLog("[WINOS-RENDER-LOOP] stats %@", stats.description)
            diagnostics?.renderFrame(frame: totalFrames, fps: fps, frameTimeMs: frameTimeMs, drawCalls: 1, width: width, height: height)
        }
    }
    
    public func didPresent() {
        presentCount += 1
        stats.presentCount = presentCount
    }
    
    public func setTargetFPS(_ fps: Int) {
        targetFPS = min(max(fps, 15), 120)
        displayLink?.preferredFrameRateRange = CAFrameRateRange(minimum: 30, maximum: Float(targetFPS), preferred: Float(targetFPS))
        NSLog("[WINOS-RENDER-LOOP] setTargetFPS %d", targetFPS)
    }
    
    deinit {
        displayLink?.invalidate()
    }
}
