import Foundation
import SwiftUI
import Combine
import PorticoCore
import PorticoRuntime

/// Fábrica do app: conecta sandbox, biblioteca, configurações, ambientes,
/// importação, backends de execução e o RuntimeManager.
@MainActor
final class AppModel: ObservableObject {
    /// Acesso global de conveniência para componentes não-SwiftUI (sessão, áudio).
    static var shared: AppModel!

    // Serviços centrais
    let sandbox: AppSandbox
    let log: LogCenter
    let library: LibraryStore
    let config: ConfigurationManager
    let environments: EnvironmentManager
    let importer: ImportService
    let backends: BackendRegistry
    let runtime: RuntimeManager

    // Estado de UI
    @Published var games: [GameProfile] = []
    @Published var showingAlert = false
    @Published var alertTitle = ""
    @Published var alertMessage = ""
    @Published var sessionToRun: GameProfile?
    @Published var logBadge = 0

    var colorScheme: ColorScheme? {
        switch config.global.tema {
        case .system: return nil
        case .dark: return .dark
        case .light: return .light
        }
    }

    init() {
        let sb = AppSandbox.standard()
        let lc = LogCenter()
        self.sandbox = sb
        self.log = lc
        self.library = LibraryStore(sandbox: sb, log: lc)
        self.config = ConfigurationManager(sandbox: sb, log: lc)
        self.environments = EnvironmentManager(sandbox: sb, log: lc)
        self.importer = ImportService(sandbox: sb, library: library,
                                      environments: environments, log: lc)
        self.backends = BackendRegistry.standard(log: lc)
        self.runtime = RuntimeManager(sandbox: sb, log: lc, backendRegistry: backends)
        AppModel.shared = self

        lc.onEntry = { [weak self] entry in
            Task { @MainActor in
                if entry.level >= .error { self?.logBadge += 1 }
            }
        }
    }

    func bootstrap() {
        do {
            try sandbox.ensureDirectories()
            try config.load()
            try environments.load()
            try library.load()
            try ensureSelfTestGame()
            refreshGames()
            log.info("app", "Portico iniciado")
        } catch {
            present(title: "Falha ao iniciar", message: "\(error)")
        }
    }

    func refreshGames() {
        games = library.games.sorted { $0.nome < $1.nome }
    }

    func present(title: String, message: String) {
        alertTitle = title
        alertMessage = message
        showingAlert = true
        log.error("app", "\(title): \(message)")
    }

    // MARK: - ações da biblioteca

    func removeGame(_ game: GameProfile, deleteFiles: Bool) {
        do {
            try library.remove(id: game.id, deleteFiles: deleteFiles)
            refreshGames()
        } catch {
            present(title: "Não foi possível remover", message: "\(error)")
        }
    }

    func updateGame(_ game: GameProfile) {
        do {
            try library.update(game)
            refreshGames()
        } catch {
            present(title: "Não foi possível salvar", message: "\(error)")
        }
    }

    /// Inicia a sessão de execução do jogo (chamado pelo botão Jogar).
    func launch(_ game: GameProfile) {
        library.markLaunched(id: game.id)
        refreshGames()
        sessionToRun = game
    }

    func effectiveConfig(for game: GameProfile) -> EffectiveConfig {
        config.effective(for: game)
    }

    // MARK: - Self-Test embutido (payload nativo PXP, honestamente rotulado)

    func ensureSelfTestGame() throws {
        guard library.game(named: "Portico Self-Test") == nil else { return }
        let rel = "Games/game-selftest.portico"
        let dir = try sandbox.resolveInside(rel)
        try FileManager.default.createDirectory(at: dir, withIntermediateDirectories: true)

        var payload: UnsafeMutableRawPointer?
        var plen = 0
        let st = pr_selftest_payload_build(&payload, &plen)
        guard st == PR_OK, let p = payload else {
            throw RuntimeFailure.payloadInvalid(reason: "construção do self-test")
        }
        let data = Data(bytes: p, count: plen)
        free(p)
        try data.write(to: dir.appendingPathComponent("selftest.pxp"), options: .atomic)

        var g = GameProfile(
            nome: "Portico Self-Test",
            caminho: rel,
            executavel: "selftest.pxp",
            resolucao: Resolution(width: 640, height: 360),
            fps: .cap(60),
            notas: "Payload nativo PXP (IA-32 interpretado) que exercita o pipeline completo. NÃO é um jogo Windows.",
            arquiteturaExe: "x86 (interpretado)",
            tipo: .selfTest,
            tamanhoInstalacao: Int64(plen)
        )
        g.ambiente = .defaultEnv
        _ = try library.add(g)
    }
}
