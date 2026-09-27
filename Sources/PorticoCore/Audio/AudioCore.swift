import Foundation

/// Estado do subsistema de áudio da sessão.
public enum AudioSessionState: Equatable, Sendable {
    case stopped
    case playing
    case paused
    case interrupted   // ex.: chamada telefônica, alarme
}

/// Parâmetros de mixagem aplicados pelo AudioBackend.
public struct AudioMixState: Equatable, Sendable {
    public var masterVolume: Double
    public var gameVolume: Double
    public var muted: Bool

    public init(masterVolume: Double = 0.8, gameVolume: Double = 0.8, muted: Bool = false) {
        self.masterVolume = min(max(masterVolume, 0), 1)
        self.gameVolume = min(max(gameVolume, 0), 1)
        self.muted = muted
    }

    public var effectiveGain: Float {
        muted ? 0 : Float(masterVolume * gameVolume)
    }
}

/// Backend de áudio (implementação iOS: AVAudioEngine).
/// Recebe PCM interleaved float estéreo vindo do runtime e reproduz.
public protocol AudioOutputBackend: AnyObject {
    var sampleRate: Double { get }
    var state: AudioSessionState { get }
    func initialize(sampleRate: Double, bufferFrames: Int) throws
    func start() throws
    func pause()
    func resume() throws
    func stop()
    func setMix(_ mix: AudioMixState)
    func enqueue(interleaved: [Float])
    func handleInterruption(began: Bool)
}

/// Lógica pura do ciclo de áudio (testável sem AVFoundation).
public final class AudioSessionController {
    public private(set) var state: AudioSessionState = .stopped
    public var mix: AudioMixState
    public private(set) var underruns: Int = 0
    public private(set) var framesPlayed: UInt64 = 0

    private weak var backend: AudioOutputBackend?
    public var onChange: ((AudioSessionState) -> Void)?

    public init(mix: AudioMixState = AudioMixState(), backend: AudioOutputBackend? = nil) {
        self.mix = mix
        self.backend = backend
    }

    public func attach(_ backend: AudioOutputBackend) {
        self.backend = backend
    }

    public func start() {
        guard state != .playing else { return }
        do {
            if state == .stopped {
                try backend?.initialize(sampleRate: 48000, bufferFrames: 512)
            }
            if state == .paused {
                try backend?.resume()
            } else {
                try backend?.start()
            }
            backend?.setMix(mix)
            transition(to: .playing)
        } catch {
            transition(to: .stopped)
        }
    }

    public func pause() {
        guard state == .playing else { return }
        backend?.pause()
        transition(to: .paused)
    }

    public func stop() {
        backend?.stop()
        transition(to: .stopped)
    }

    public func interruption(began: Bool) {
        backend?.handleInterruption(began: began)
        if began, state == .playing {
            transition(to: .interrupted)
        } else if !began, state == .interrupted {
            start()
        }
    }

    public func feed(interleaved: [Float]) {
        guard state == .playing else { return }
        if interleaved.isEmpty {
            underruns += 1
            return
        }
        framesPlayed += UInt64(interleaved.count / 2)
        backend?.enqueue(interleaved: interleaved)
    }

    public func updateMix(_ transform: (inout AudioMixState) -> Void) {
        transform(&mix)
        backend?.setMix(mix)
    }

    private func transition(to newState: AudioSessionState) {
        guard state != newState else { return }
        state = newState
        onChange?(newState)
    }
}
