import Foundation
import AVFoundation
import PorticoCore
import PorticoRuntime

/// AudioBackend iOS: AVAudioEngine com source node que consome o PCM do
/// runtime via ring buffer (sem bloquear a main thread). Volume, pausa,
/// retomada e interrupções (chamadas/alarmes) implementados.
final class AVAudioEngineBackend: AudioOutputBackend {
    private let engine = AVAudioEngine()
    private var sourceNode: AVAudioSourceNode?
    private var ring: OpaquePointer?
    private var mix = AudioMixState()
    private(set) var state: AudioSessionState = .stopped
    private(set) var sampleRate: Double = 48000
    private var bufferFrames: Int = 512

    init() {
        NotificationCenter.default.addObserver(
            self,
            selector: #selector(interruption(_:)),
            name: AVAudioSession.interruptionNotification,
            object: nil)
    }

    deinit {
        if let ring { pr_audio_ring_destroy(ring) }
    }

    func initialize(sampleRate: Double, bufferFrames: Int) throws {
        self.sampleRate = sampleRate
        self.bufferFrames = bufferFrames
        if ring == nil {
            ring = pr_audio_ring_create(UInt(max(bufferFrames * 8, 4096)))
        }
        let session = AVAudioSession.sharedInstance()
        try session.setCategory(.playback, mode: .default)
        try session.setPreferredSampleRate(sampleRate)
        try session.setActive(true)

        let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate, channels: 2)!
        let node = AVAudioSourceNode(format: format) { [weak self] _, _, frameCount, audioBufferList -> OSStatus in
            guard let self, let ring = self.ring else {
                return noErr
            }
            let abl = UnsafeMutableAudioBufferListPointer(audioBufferList)
            let n = Int(frameCount)
            // puxa interleaved estéreo do ring e deinterleava p/ o AudioBufferList
            var temp = [Float](repeating: 0, count: n * 2)
            let got = temp.withUnsafeMutableBufferPointer { p -> Int in
                pr_audio_ring_pull_wrapper(ring, p.baseAddress, n)
            }
            if got < n {
                for i in (got * 2)..<(n * 2) { temp[i] = 0 }
            }
            let gain = Float(self.mix.effectiveGain)
            for (i, buf) in abl.enumerated() {
                if let data = buf.mData?.assumingMemoryBound(to: Float.self) {
                    for f in 0..<n {
                        data[f] = temp[f * 2 + i] * gain
                    }
                }
            }
            return noErr
        }
        engine.attach(node)
        engine.connect(node, to: engine.mainMixerNode, format: format)
        sourceNode = node
    }

    func start() throws {
        if state == .stopped, sourceNode == nil {
            try initialize(sampleRate: sampleRate, bufferFrames: bufferFrames)
        }
        try AVAudioSession.sharedInstance().setActive(true)
        if !engine.isRunning {
            try engine.start()
        }
        state = .playing
    }

    func pause() {
        engine.pause()
        state = .paused
    }

    func resume() throws {
        try engine.start()
        state = .playing
    }

    func stop() {
        engine.stop()
        if let ring { pr_audio_ring_reset(ring) }
        state = .stopped
        try? AVAudioSession.sharedInstance().setActive(false,
            options: .notifyOthersOnDeactivation)
    }

    func setMix(_ mix: AudioMixState) {
        self.mix = mix
    }

    func enqueue(interleaved: [Float]) {
        guard let ring, !interleaved.isEmpty else { return }
        interleaved.withUnsafeBufferPointer { p in
            if let base = p.baseAddress {
                let frames = interleaved.count / 2
                _ = pr_audio_ring_push_wrapper(ring, base, frames)
            }
        }
    }

    func handleInterruption(began: Bool) {
        if began {
            engine.pause()
        } else {
            try? engine.start()
        }
    }

    @objc private func interruption(_ note: Notification) {
        guard let info = note.userInfo,
              let raw = info[AVAudioSessionInterruptionTypeKey] as? UInt,
              let type = AVAudioSession.InterruptionType(rawValue: raw) else { return }
        switch type {
        case .began:
            state = .interrupted
            handleInterruption(began: true)
        case .ended:
            handleInterruption(began: false)
            state = .playing
        @unknown default:
            break
        }
    }
}

// Wrappers C chamáveis a partir de closures @convention(c)/sem contexto
// (as funções do ring são C diretas; ponteiros opacos seguem o padrão do core).
@inline(__always)
func pr_audio_ring_pull_wrapper(_ ring: OpaquePointer?, _ out: UnsafeMutablePointer<Float>?, _ frames: Int) -> Int {
    guard let ring else { return 0 }
    return pr_audio_pull(ring, out, frames)
}

@inline(__always)
func pr_audio_ring_push_wrapper(_ ring: OpaquePointer?, _ input: UnsafePointer<Float>?, _ frames: Int) -> Int {
    guard let ring else { return 0 }
    return pr_audio_push(ring, input, frames)
}
