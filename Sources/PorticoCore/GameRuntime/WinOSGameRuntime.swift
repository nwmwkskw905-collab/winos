import Foundation
import PorticoCore

/// FASE 13-18 — Game Runtime central: gerencia primeiro jogo real, teste completo, autocorreção loop

public struct WinOSGameRuntimeConfig: Sendable {
    public var executableURL: URL
    public var fsRoot: String
    public var resolution: (Int, Int)
    public var targetFPS: Int
    public var enableAudio: Bool
    public var enableInput: Bool
    public var maxFrames: UInt64 // 0 = infinito
    public var timeoutSeconds: Double
    
    public init(executableURL: URL, fsRoot: String, resolution: (Int, Int) = (1280, 720), targetFPS: Int = 60, enableAudio: Bool = true, enableInput: Bool = true, maxFrames: UInt64 = 0, timeoutSeconds: Double = 30) {
        self.executableURL = executableURL
        self.fsRoot = fsRoot
        self.resolution = resolution
        self.targetFPS = targetFPS
        self.enableAudio = enableAudio
        self.enableInput = enableInput
        self.maxFrames = maxFrames
        self.timeoutSeconds = timeoutSeconds
    }
}

public enum WinOSGameResult: Sendable {
    case success(frames: UInt64, compatScore: Double, report: String)
    case failed(reason: String, compatReport: WinOSCompatReport?, diagnostics: String)
    case timeout(frames: UInt64, report: String)
    case unsupported(reason: String, compatReport: WinOSCompatReport)
}

@MainActor
public final class WinOSGameRuntime: ObservableObject {
    @Published public private(set) var phase: WinOSGamePhase = .idle
    @Published public private(set) var diagnostics: WinOSGameDiagnostics
    @Published public private(set) var lastResult: WinOSGameResult?
    @Published public private(set) var currentCompat: WinOSCompatReport?
    
    private var harness: WinOSGameHarness?
    private var logCenter: LogCenter
    private var config: WinOSGameRuntimeConfig?
    
    public var onFrame: ((GfxFrame) -> Void)?
    public var onLog: ((String) -> Void)?
    
    public init(logCenter: LogCenter = LogCenter()) {
        self.logCenter = logCenter
        self.diagnostics = WinOSGameDiagnostics()
        NSLog("[WINOS-GAME-RUNTIME] GameRuntime init")
    }
    
    // MARK: - Primeiro jogo real (FASE 13)
    
    public func runFirstRealGame(config: WinOSGameRuntimeConfig) async -> WinOSGameResult {
        self.config = config
        phase = .loading
        diagnostics = WinOSGameDiagnostics()
        
        let harness = WinOSGameHarness(logCenter: logCenter)
        self.harness = harness
        harness.onFrame = { [weak self] frame in
            self?.onFrame?(frame)
        }
        
        // Build profile
        let profile = GameProfile(
            id: UUID(),
            nome: config.executableURL.deletingPathExtension().lastPathComponent,
            caminho: config.fsRoot,
            executavel: config.executableURL.lastPathComponent,
            argumentos: "",
            resolucao: Resolution(
                width: config.resolution.0,
                height: config.resolution.1
            ),
            fps: .cap(config.targetFPS),
            renderer: .metal,
            audio: AudioProfile(
                enabled: config.enableAudio,
                volume: 0.8
            ),
            controles: ControlProfile(
                physicalControllersEnabled: config.enableInput,
                showTouchControls: config.enableInput
            ),
            ambiente: .defaultEnv,
            opcoes: AdvancedOptions(
                environmentVariables: [:],
                debugLogging: true,
                maxInstructionsPerFrame: 200_000
            ),
            arquiteturaExe: "x86",
            tipo: .windowsPE
        )
        
        let runtimeConfig = EffectiveConfig(
            resolution: profile.resolucao,
            fps: profile.fps,
            renderer: profile.renderer,
            audioEnabled: profile.audio.enabled,
            gameVolume: profile.audio.volume,
            masterVolume: 0.8,
            quality: .high,
            showTouchControls: profile.controles.showTouchControls,
            physicalControllersEnabled: profile.controles.physicalControllersEnabled,
            environmentVariables: profile.opcoes.environmentVariables,
            debugLogging: profile.opcoes.debugLogging,
            maxInstructionsPerFrame: profile.opcoes.maxInstructionsPerFrame
        )
        
        // Load + Analyze
        let compat: WinOSCompatReport
        do {
            compat = try harness.load(executableURL: config.executableURL, profile: profile, config: runtimeConfig, fsRoot: config.fsRoot)
            currentCompat = compat
            diagnostics = harness.diagnostics
            
            NSLog("[WINOS-GAME-RUNTIME] compatibility score=%.1f%% level=%@ canRun=%@ gfx=%@",
                  compat.compatScore, compat.compatLevel.rawValue, compat.canRun ? "YES" : "NO", compat.graphics.primaryAPI.rawValue)
            
            if !compat.canRun {
                phase = .failed
                let result = WinOSGameResult.unsupported(reason: compat.reason, compatReport: compat)
                lastResult = result
                return result
            }
        } catch {
            phase = .failed
            let reason = "Load failed: \(error)"
            diagnostics.log(.error, category: .game, code: "LOAD_FAIL", message: "LOAD_FAIL", detail: reason)
            let result = WinOSGameResult.failed(reason: reason, compatReport: nil, diagnostics: harness.generateFinalReport())
            lastResult = result
            return result
        }
        
        // Initialize
        do {
            try harness.initialize()
        } catch {
            phase = .failed
            let reason = "Initialize failed: \(error)"
            diagnostics.log(.error, category: .game, code: "INIT_FAIL", message: "INIT_FAIL", detail: reason)
            let result = WinOSGameResult.failed(reason: reason, compatReport: compat, diagnostics: harness.generateFinalReport())
            lastResult = result
            return result
        }
        
        // Run
        do {
            try harness.run()
            phase = .running
        } catch {
            phase = .failed
            let reason = "Run failed: \(error)"
            diagnostics.log(.error, category: .game, code: "RUN_FAIL", message: "RUN_FAIL", detail: reason)
            let result = WinOSGameResult.failed(reason: reason, compatReport: compat, diagnostics: harness.generateFinalReport())
            lastResult = result
            return result
        }
        
        // Frame loop — teste completo (FASE 14)
        let startTime = Date()
        var frames: UInt64 = 0
        var lastTime = CACurrentMediaTime()
        
        while phase == .running {
            let now = CACurrentMediaTime()
            let dt = Float(now - lastTime)
            lastTime = now
            let timeMs = now * 1000.0
            
            let result = harness.stepFrame(dt: dt, timeMs: timeMs)
            frames = harness.framesPresented
            
            // Check max frames
            if config.maxFrames > 0 && frames >= config.maxFrames {
                NSLog("[WINOS-GAME-RUNTIME] maxFrames reached %llu", frames)
                break
            }
            
            // Check timeout
            if Date().timeIntervalSince(startTime) > config.timeoutSeconds {
                phase = .stopped
                let report = harness.generateFinalReport()
                let timeoutResult = WinOSGameResult.timeout(frames: frames, report: report)
                lastResult = timeoutResult
                harness.stop()
                NSLog("[WINOS-GAME-RUNTIME] timeout after %.1fs frames=%llu", config.timeoutSeconds, frames)
                return timeoutResult
            }
            
            // Check backend state
            if result.state == .stopped {
                phase = .stopped
                let report = harness.generateFinalReport()
                let successResult = WinOSGameResult.success(frames: frames, compatScore: compat.compatScore, report: report)
                lastResult = successResult
                NSLog("[WINOS-GAME-RUNTIME] SUCCESS frames=%llu score=%.1f%%", frames, compat.compatScore)
                return successResult
            } else if result.state == .failed {
                phase = .failed
                let report = harness.generateFinalReport()
                let failResult = WinOSGameResult.failed(reason: result.message, compatReport: compat, diagnostics: report)
                lastResult = failResult
                NSLog("[WINOS-GAME-RUNTIME] FAILED reason=%@ frames=%llu", result.message, frames)
                return failResult
            }
            
            // Yield to main loop — 60 FPS pacing
            try? await Task.sleep(nanoseconds: 16_000_000) // ~60 FPS
        }
        
        // If loop ended without explicit success/fail, treat as success with frames
        phase = .stopped
        let report = harness.generateFinalReport()
        let result = WinOSGameResult.success(frames: frames, compatScore: compat.compatScore, report: report)
        lastResult = result
        return result
    }
    
    public func stop() {
        harness?.stop()
        phase = .stopped
        NSLog("[WINOS-GAME-RUNTIME] stop")
    }
    
    public func shutdown() {
        harness?.shutdown()
        harness = nil
        phase = .idle
        NSLog("[WINOS-GAME-RUNTIME] shutdown")
    }
    
    // MARK: - Autocorreção loop (FASE 15)
    
    public struct AutoFixResult: Sendable {
        public var attempt: Int
        public var result: WinOSGameResult
        public var fixesApplied: [String]
        public var shouldRetry: Bool
    }
    
    public func runWithAutoFix(config: WinOSGameRuntimeConfig, maxAttempts: Int = 3) async -> [AutoFixResult] {
        var results: [AutoFixResult] = []
        var currentConfig = config
        
        for attempt in 1...maxAttempts {
            NSLog("[WINOS-GAME-RUNTIME] AutoFix attempt %d/%d", attempt, maxAttempts)
            diagnostics.log(.info, category: .game, code: "AUTOFIX_ATTEMPT", message: "AUTOFIX_ATTEMPT", detail: "attempt=\(attempt)/\(maxAttempts) exe=\(config.executableURL.lastPathComponent)")
            
            let result = await runFirstRealGame(config: currentConfig)
            var fixes: [String] = []
            var shouldRetry = false
            
            switch result {
            case .success:
                NSLog("[WINOS-GAME-RUNTIME] AutoFix attempt %d SUCCESS", attempt)
                results.append(AutoFixResult(attempt: attempt, result: result, fixesApplied: fixes, shouldRetry: false))
                return results // Success, stop loop
                
            case .failed(let reason, let compat, _):
                NSLog("[WINOS-GAME-RUNTIME] AutoFix attempt %d FAILED reason=%@", attempt, reason)
                
                // Analyze failure and propose fixes
                if reason.contains("MISSING_DLL") || reason.contains("dll") {
                    fixes.append("Missing DLL detected — would need to add DLL to fs_root")
                    shouldRetry = false // Cannot auto-fix missing DLL without file
                } else if reason.contains("UNIMPLEMENTED") || reason.contains("not implemented") {
                    fixes.append("Unimplemented API — would need to implement API in pr_win32.c")
                    shouldRetry = false
                } else if reason.contains("GRAPHICS") || reason.contains("D3D") {
                    fixes.append("Graphics API not supported — need to implement \(compat?.graphics.primaryAPI.rawValue ?? "D3D") → Metal")
                    shouldRetry = false
                } else if reason.contains("timeout") || reason.contains("budget") {
                    // Could retry with larger budget
                    currentConfig = WinOSGameRuntimeConfig(
                        executableURL: currentConfig.executableURL,
                        fsRoot: currentConfig.fsRoot,
                        resolution: currentConfig.resolution,
                        targetFPS: max(30, currentConfig.targetFPS - 10),
                        enableAudio: currentConfig.enableAudio,
                        enableInput: currentConfig.enableInput,
                        maxFrames: currentConfig.maxFrames,
                        timeoutSeconds: currentConfig.timeoutSeconds * 1.5
                    )
                    fixes.append("Increased timeout to \(currentConfig.timeoutSeconds)s, reduced FPS to \(currentConfig.targetFPS)")
                    shouldRetry = true
                }
                
                results.append(AutoFixResult(attempt: attempt, result: result, fixesApplied: fixes, shouldRetry: shouldRetry))
                
                if !shouldRetry {
                    break
                }
                
            case .unsupported(let reason, _):
                NSLog("[WINOS-GAME-RUNTIME] AutoFix attempt %d UNSUPPORTED reason=%@", attempt, reason)
                fixes.append("Unsupported: \(reason) — cannot auto-fix, requires new implementation")
                results.append(AutoFixResult(attempt: attempt, result: result, fixesApplied: fixes, shouldRetry: false))
                break
                
            case .timeout(let frames, _):
                NSLog("[WINOS-GAME-RUNTIME] AutoFix attempt %d TIMEOUT frames=%llu", attempt, frames)
                // Timeout could be success if frames > 0 — game is running but didn't exit
                if frames > 10 {
                    fixes.append("Timeout but \(frames) frames presented — game is running, treat as success")
                    results.append(AutoFixResult(attempt: attempt, result: result, fixesApplied: fixes, shouldRetry: false))
                    return results
                } else {
                    currentConfig = WinOSGameRuntimeConfig(
                        executableURL: currentConfig.executableURL,
                        fsRoot: currentConfig.fsRoot,
                        resolution: currentConfig.resolution,
                        targetFPS: currentConfig.targetFPS,
                        enableAudio: currentConfig.enableAudio,
                        enableInput: currentConfig.enableInput,
                        maxFrames: currentConfig.maxFrames,
                        timeoutSeconds: currentConfig.timeoutSeconds * 2
                    )
                    fixes.append("Timeout with only \(frames) frames — increased timeout to \(currentConfig.timeoutSeconds)s")
                    shouldRetry = attempt < maxAttempts
                    results.append(AutoFixResult(attempt: attempt, result: result, fixesApplied: fixes, shouldRetry: shouldRetry))
                    if !shouldRetry { break }
                }
            }
        }
        
        return results
    }
}
