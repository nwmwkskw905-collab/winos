import Foundation
import PorticoCore
import AVFoundation

/// FASE 10 — Audio pipeline real (waveOut, DirectSound, XAudio2 → AVAudioEngine)

public enum WinOSAudioAPI: String, Sendable {
    case none = "NONE"
    case waveOut = "WAVEOUT"
    case directSound = "DIRECTSOUND"
    case xaudio2 = "XAUDIO2"
    case wasapi = "WASAPI"
    case openAL = "OPENAL"
}

public struct WinOSAudioStats: Sendable {
    public var sampleRate: Double = 44100
    public var channels: Int = 2
    public var framesPulled: UInt64 = 0
    public var underruns: UInt64 = 0
    public var api: String = "NONE"
    public var isPlaying: Bool = false
}

@MainActor
public final class WinOSAudioPipeline: ObservableObject {
    @Published public private(set) var stats: WinOSAudioStats = WinOSAudioStats()
    @Published public private(set) var isReady: Bool = false
    @Published public private(set) var lastError: String = ""
    
    private var audioEngine: AVAudioEngine?
    private var playerNode: AVAudioPlayerNode?
    private var audioFormat: AVAudioFormat?
    private var sampleRate: Double = 44100
    private var diagnostics: WinOSGameDiagnostics?
    private var detectedAPI: WinOSAudioAPI = .none
    
    private var framesPulled: UInt64 = 0
    private var underruns: UInt64 = 0
    
    public var onAudioFrames: (([Float]) -> Void)?
    
    public init() {
        NSLog("[WINOS-AUDIO-PIPELINE] AudioPipeline init")
    }
    
    public func configure(diagnostics: WinOSGameDiagnostics?) {
        self.diagnostics = diagnostics
    }
    
    public func detectAudioAPI(report: PEReport) -> WinOSAudioAPI {
        var api: WinOSAudioAPI = .none
        
        for imp in report.imports {
            let dll = imp.dll.lowercased()
            let funcs = imp.functions.map { $0.displayName.lowercased() }
            
            if dll.contains("winmm") {
                if funcs.contains(where: { $0.contains("waveout") }) {
                    api = .waveOut
                    break
                }
            }
            if dll.contains("dsound") {
                api = .directSound
                break
            }
            if dll.contains("xaudio2") {
                api = .xaudio2
                break
            }
            if dll.contains("openal") {
                api = .openAL
                break
            }
        }
        
        detectedAPI = api
        NSLog("[WINOS-AUDIO-PIPELINE] detect API=%@ dlls=%@", api.rawValue, report.imports.map { $0.dll }.joined(separator: ","))
        diagnostics?.log(.info, category: .audio, code: "AUDIO_DETECTED", message: "AUDIO_DETECTED", detail: "api=\(api.rawValue)")
        return api
    }
    
    public func initialize(sampleRate: Double = 44100, channels: Int = 2) -> Bool {
        self.sampleRate = sampleRate
        
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default, options: [.mixWithOthers])
            try session.setActive(true)
            
            let actualSampleRate = session.sampleRate
            NSLog("[WINOS-AUDIO-PIPELINE] AVAudioSession sampleRate=%.0f requested=%.0f", actualSampleRate, sampleRate)
            
            let engine = AVAudioEngine()
            let player = AVAudioPlayerNode()
            engine.attach(player)
            
            guard let format = AVAudioFormat(standardFormatWithSampleRate: actualSampleRate, channels: AVAudioChannelCount(channels)) else {
                lastError = "Failed to create AVAudioFormat"
                diagnostics?.log(.error, category: .audio, code: "AUDIO_FORMAT_FAIL", message: "AUDIO_FORMAT_FAIL", detail: lastError)
                return false
            }
            
            engine.connect(player, to: engine.mainMixerNode, format: format)
            
            try engine.start()
            player.play()
            
            self.audioEngine = engine
            self.playerNode = player
            self.audioFormat = format
            self.sampleRate = actualSampleRate
            self.isReady = true
            
            stats.sampleRate = actualSampleRate
            stats.channels = channels
            stats.api = detectedAPI.rawValue
            stats.isPlaying = true
            
            diagnostics?.audioInit(sampleRate: actualSampleRate, channels: channels, success: true)
            diagnostics?.log(.info, category: .audio, code: "AUDIO_INIT_SUCCESS", message: "AUDIO_INIT_SUCCESS", detail: "sampleRate=\(actualSampleRate) channels=\(channels) api=\(detectedAPI.rawValue)")
            NSLog("[WINOS-AUDIO-PIPELINE] initialize SUCCESS sampleRate=%.0f channels=%d api=%@", actualSampleRate, channels, detectedAPI.rawValue)
            return true
            
        } catch {
            lastError = "\(error)"
            diagnostics?.log(.error, category: .audio, code: "AUDIO_INIT_FAIL", message: "AUDIO_INIT_FAIL", detail: lastError)
            diagnostics?.audioInit(sampleRate: sampleRate, channels: channels, success: false)
            NSLog("[WINOS-AUDIO-PIPELINE] initialize FAIL error=%@", lastError)
            return false
        }
    }
    
    public func pullAudio(maxFrames: Int) -> [Float] {
        guard isReady else { return [] }
        
        // Pull from backend if available
        if let callback = onAudioFrames {
            // Backend will provide frames via callback
            // For now, return empty and let backend push
            return []
        }
        
        // Fallback: silence
        return [Float](repeating: 0, count: maxFrames * stats.channels)
    }
    
    public func pushAudioFrames(_ frames: [Float]) {
        guard isReady, let player = playerNode, let format = audioFormat else {
            underruns += 1
            return
        }
        
        let frameCount = frames.count / stats.channels
        guard frameCount > 0 else { return }
        
        guard let buffer = AVAudioPCMBuffer(pcmFormat: format, frameCapacity: AVAudioFrameCount(frameCount)) else {
            underruns += 1
            return
        }
        
        buffer.frameLength = AVAudioFrameCount(frameCount)
        
        // Copy float frames to buffer
        if let channelData = buffer.floatChannelData {
            for ch in 0..<stats.channels {
                for i in 0..<frameCount {
                    let srcIdx = i * stats.channels + ch
                    if srcIdx < frames.count {
                        channelData[ch][i] = frames[srcIdx]
                    }
                }
            }
        }
        
        player.scheduleBuffer(buffer, completionHandler: nil)
        framesPulled += UInt64(frameCount)
        stats.framesPulled = framesPulled
        stats.underruns = underruns
        
        if framesPulled % 44100 == 0 {
            NSLog("[WINOS-AUDIO-PIPELINE] pushed frames=%llu underruns=%llu", framesPulled, underruns)
        }
    }
    
    public func handleWaveOutOpen() {
        diagnostics?.log(.info, category: .audio, code: "WAVEOUT_OPEN", message: "WAVEOUT_OPEN", detail: "waveOutOpen called, api=\(detectedAPI.rawValue)")
        NSLog("[WINOS-AUDIO-PIPELINE] waveOutOpen")
    }
    
    public func handleWaveOutWrite(data: Data) {
        // Convert PCM data to float and push
        // Assume 16-bit stereo 44.1k for now
        let sampleCount = data.count / 2
        var floats: [Float] = []
        floats.reserveCapacity(sampleCount)
        
        data.withUnsafeBytes { raw in
            let ptr = raw.bindMemory(to: Int16.self)
            for i in 0..<ptr.count {
                floats.append(Float(ptr[i]) / 32768.0)
            }
        }
        
        pushAudioFrames(floats)
        diagnostics?.log(.debug, category: .audio, code: "WAVEOUT_WRITE", message: "WAVEOUT_WRITE", detail: "bytes=\(data.count) samples=\(sampleCount)")
    }
    
    public func shutdown() {
        playerNode?.stop()
        audioEngine?.stop()
        audioEngine = nil
        playerNode = nil
        audioFormat = nil
        isReady = false
        stats.isPlaying = false
        NSLog("[WINOS-AUDIO-PIPELINE] shutdown frames=%llu underruns=%llu", framesPulled, underruns)
        diagnostics?.log(.info, category: .audio, code: "AUDIO_SHUTDOWN", message: "AUDIO_SHUTDOWN", detail: "frames=\(framesPulled) underruns=\(underruns)")
    }
    
    deinit {
        // Cannot call @MainActor shutdown from deinit, just log
        NSLog("[WINOS-AUDIO-PIPELINE] deinit")
    }
}
