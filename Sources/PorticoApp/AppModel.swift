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
    @Published var selectedPC: EnvironmentProfile?
    @Published var showDesktop = false
    @Published var showRealDesktop = false
    @Published var runtimeStage: String = "idle"
    @Published var lastRuntimeError: String = ""
    @Published var lastLoadedExecutable: String = ""

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
            // Self-test NÃO deve aparecer na biblioteca normal — é ferramenta de diagnóstico
            // Preserva capacidade de diagnóstico mas remove registro automático na biblioteca
            // Gera payload em RuntimeTests/ para uso em Diagnostics → Runtime Tests
            try ensureRuntimeTestsPayload()
            // Remove self-test antigo da biblioteca se existir (migração)
            try removeSelfTestFromLibraryIfPresent()
            refreshGames()
            log.info("app", "Portico iniciado — biblioteca normal sem self-test, runtime tests em Diagnostics")
        } catch {
            present(title: "Falha ao iniciar", message: "\(error)")
        }
    }

    func refreshGames() {
        // Filtra selfTest da biblioteca normal — só mostra windowsPE e pxpNative de usuário
        games = library.games.filter { $0.tipo != .selfTest }.sorted { $0.nome < $1.nome }
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
        NSLog("[WINOS-RUNTIME] CREATE_PC launch game=%@ exe=%@ path=%@", game.nome, game.executavel, game.caminho)
        NSLog("[WINOS-RUNTIME] SANDBOX_READY root=%@ gamePath=%@", sandbox.root.path, game.caminho)
        library.markLaunched(id: game.id)
        refreshGames()
        lastLoadedExecutable = game.executavel
        runtimeStage = "RUNTIME_START"
        NSLog("[WINOS-RUNTIME] ENV_READY game=%@", game.nome)
        sessionToRun = game
    }

    func effectiveConfig(for game: GameProfile) -> EffectiveConfig {
        config.effective(for: game)
    }

    // MARK: - Runtime Tests (diagnóstico, não biblioteca normal)

    /// Garante que payloads de teste de runtime existem em RuntimeTests/ (não Games/)
    /// Para uso em Diagnostics → Runtime Tests, sem poluir biblioteca do usuário
    func ensureRuntimeTestsPayload() throws {
        let rel = "RuntimeTests"
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
        
        let pxpURL = dir.appendingPathComponent("selftest.pxp")
        if !FileManager.default.fileExists(atPath: pxpURL.path) {
            try data.write(to: pxpURL, options: .atomic)
            NSLog("[WINOS-SELFTEST] Payload PXP gerado em RuntimeTests/selftest.pxp size=%d", plen)
        }
        
        // Também garante que diretório de testes existe para futuros PE Loader, Win32, Graphics, Input tests
        NSLog("[WINOS-SELFTEST] RuntimeTests payloads prontos em %@", dir.path)
    }
    
    /// Remove self-test antigo da biblioteca se existir (migração para Diagnostics)
    func removeSelfTestFromLibraryIfPresent() throws {
        if let old = library.game(named: "Portico Self-Test") {
            try library.remove(id: old.id, deleteFiles: false)
            NSLog("[WINOS-SELFTEST] Removido Portico Self-Test da biblioteca normal (movido para Diagnostics)")
        }
        // Também remove variações de nome
        for name in ["Port Self Test", "Portico Self Test", "Self Test", "selftest"] {
            if let g = library.game(named: name) {
                try? library.remove(id: g.id, deleteFiles: false)
                NSLog("[WINOS-SELFTEST] Removido '%@' da biblioteca normal", name)
            }
        }
        // Remove games com tipo selfTest que possam ter sido criados com outros nomes
        let selfTests = library.games.filter { $0.tipo == .selfTest }
        for st in selfTests {
            try? library.remove(id: st.id, deleteFiles: false)
            NSLog("[WINOS-SELFTEST] Removido selfTest tipo da biblioteca: %@", st.nome)
        }
    }

    // MARK: - Self-Test legado (preservado para compatibilidade, mas não usado na biblioteca normal)

    func ensureSelfTestGame() throws {
        // LEGADO: mantido para compatibilidade, mas agora chama ensureRuntimeTestsPayload
        // Não adiciona mais à biblioteca normal
        try ensureRuntimeTestsPayload()
    }
}
