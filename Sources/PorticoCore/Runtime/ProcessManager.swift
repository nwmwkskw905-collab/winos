import Foundation

/// Processo gerenciado dentro do app.
///
/// LIMITAÇÃO REAL DO iOS: o sistema NÃO permite `fork`/`exec` de binários nem
/// sub-processos clássicos. Todo "processo" aqui é uma tarefa IN-PROCESS
/// (uma sessão de runtime com ciclo de vida próprio), com captura de saída
/// (logs), detecção de término/falha e encerramento forçado — a interface é a
/// mesma de um ProcessManager tradicional para que a UI não precise mudar se
/// um dia surgir outra forma de execução.
public enum ManagedProcessState: Equatable, Sendable {
    case created
    case running
    case finished(exitCode: Int32)
    case failed(reason: String)
    case terminated
}

public final class ManagedProcess {
    public let id: UUID
    public let label: String
    public private(set) var state: ManagedProcessState = .created
    public private(set) var startedAt: Date?
    public private(set) var endedAt: Date?

    private let onStop: () -> Void
    private let log: LogCenter

    public init(id: UUID = UUID(), label: String, log: LogCenter,
                onStop: @escaping () -> Void) {
        self.id = id
        self.label = label
        self.log = log
        self.onStop = onStop
    }

    public func markRunning() {
        state = .running
        startedAt = Date()
        log.info("process", "[\(label)] iniciado (tarefa in-process)")
    }

    public func markFinished(exitCode: Int32) {
        state = .finished(exitCode: exitCode)
        endedAt = Date()
        log.info("process", "[\(label)] concluído (exit=\(exitCode))")
    }

    public func markFailed(reason: String) {
        state = .failed(reason: reason)
        endedAt = Date()
        log.error("process", "[\(label)] falhou: \(reason)")
    }

    /// Encerramento forçado (termina o runtime associado).
    public func terminate() {
        guard state == .running || state == .created else { return }
        onStop()
        state = .terminated
        endedAt = Date()
        log.warning("process", "[\(label)] encerrado pelo usuário")
    }

    public var isAlive: Bool { state == .running }
}

/// Gerencia o conjunto de processos vivos (normalmente 1 por vez no iOS).
public final class ProcessManager {
    public private(set) var processes: [ManagedProcess] = []
    private let log: LogCenter

    public init(log: LogCenter) {
        self.log = log
    }

    @discardableResult
    public func spawn(label: String, onStop: @escaping () -> Void) -> ManagedProcess {
        let p = ManagedProcess(label: label, log: log, onStop: onStop)
        processes.append(p)
        return p
    }

    public func reap() {
        processes.removeAll(where: {
            if case .running = $0.state { return false }
            if case .created = $0.state { return false }
            return true
        })
    }

    public func terminateAll() {
        for p in processes { p.terminate() }
    }
}
