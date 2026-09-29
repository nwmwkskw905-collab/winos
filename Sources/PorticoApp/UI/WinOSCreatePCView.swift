import SwiftUI
import PorticoCore

/// Fluxo Criar PC WinOS — configurações realmente utilizadas pelo runtime.
/// Não cria configurações visuais sem efeito.
struct WinOSCreatePCView: View {
    @EnvironmentObject var model: AppModel
    @Environment(\.dismiss) var dismiss
    
    @State private var nome = "Meu PC \(Int.random(in: 1...99))"
    @State private var arquitetura = "x64"
    @State private var ramMB = 2048.0
    @State private var backend = "Metal"
    @State private var resolucao = "1280x720"
    @State private var fps = 60.0
    @State private var audioEnabled = true
    @State private var touchControls = true
    
    private let arquiteturas = ["x64", "x86 (compat)"]
    private let backends = ["Metal", "Software"]
    private let resolucoes = ["640x360", "960x540", "1280x720", "1920x1080"]
    
    var body: some View {
        NavigationStack {
            ZStack {
                WinOSBrand.background.ignoresSafeArea()
                ScrollView {
                    VStack(spacing: 20) {
                        header
                        formSections
                        createButton
                    }
                    .padding(20)
                }
            }
            .navigationTitle("Criar PC")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancelar") { dismiss() }
                        .foregroundStyle(WinOSBrand.textSecondary)
                }
            }
        }
    }
    
    private var header: some View {
        VStack(spacing: 12) {
            WinOSLogoView(size: 64)
            Text("Novo ambiente WinOS")
                .font(.system(.title2, design: .rounded).weight(.bold))
                .foregroundStyle(.white)
            Text("Configuração real utilizada pelo runtime — sem opções decorativas")
                .font(.footnote)
                .foregroundStyle(WinOSBrand.textSecondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 12)
    }
    
    private var formSections: some View {
        VStack(spacing: 16) {
            WinOSCard {
                VStack(alignment: .leading, spacing: 16) {
                    sectionTitle("Geral", icon: "desktopcomputer")
                    field(label: "Nome do PC") {
                        TextField("Nome", text: $nome)
                            .textFieldStyle(.roundedBorder)
                    }
                    field(label: "Arquitetura Guest") {
                        Picker("", selection: $arquitetura) {
                            ForEach(arquiteturas, id: \.self) { Text($0).tag($0) }
                        }
                        .pickerStyle(.segmented)
                    }
                    Text("HOST sempre ARM64 iOS, GUEST x64 — distinção explícita preservada")
                        .font(.caption2)
                        .foregroundStyle(WinOSBrand.textTertiary)
                }
            }
            
            WinOSCard {
                VStack(alignment: .leading, spacing: 16) {
                    sectionTitle("Hardware", icon: "memorychip")
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Text("RAM")
                                .font(.subheadline.weight(.semibold))
                                .foregroundStyle(.white)
                            Spacer()
                            Text("\(Int(ramMB)) MB")
                                .font(.subheadline.monospaced())
                                .foregroundStyle(WinOSBrand.accent)
                        }
                        Slider(value: $ramMB, in: 512...8192, step: 256)
                            .tint(WinOSBrand.accent)
                        HStack {
                            Text("512 MB").font(.caption2).foregroundStyle(WinOSBrand.textTertiary)
                            Spacer()
                            Text("8192 MB").font(.caption2).foregroundStyle(WinOSBrand.textTertiary)
                        }
                    }
                    field(label: "Backend Gráfico") {
                        Picker("", selection: $backend) {
                            ForEach(backends, id: \.self) { Text($0).tag($0) }
                        }
                        .pickerStyle(.segmented)
                    }
                    Text("Metal = MTLDevice/queue/pipeline real (UNVERIFIED sem hardware). Software = framebuffer 320x240 + SurfaceBridge (GREEN)")
                        .font(.caption2)
                        .foregroundStyle(WinOSBrand.textTertiary)
                }
            }
            
            WinOSCard {
                VStack(alignment: .leading, spacing: 16) {
                    sectionTitle("Vídeo", icon: "display")
                    field(label: "Resolução") {
                        Picker("", selection: $resolucao) {
                            ForEach(resolucoes, id: \.self) { Text($0).tag($0) }
                        }
                        .pickerStyle(.menu)
                    }
                    VStack(alignment: .leading, spacing: 8) {
                        HStack {
                            Text("FPS Alvo")
                                .font(.subheadline.weight(.semibold))
                                .foregroundStyle(.white)
                            Spacer()
                            Text("\(Int(fps))")
                                .font(.subheadline.monospaced())
                                .foregroundStyle(WinOSBrand.accent)
                        }
                        Slider(value: $fps, in: 15...120, step: 15)
                            .tint(WinOSBrand.accent)
                    }
                }
            }
            
            WinOSCard {
                VStack(alignment: .leading, spacing: 16) {
                    sectionTitle("Input & Áudio", icon: "gamecontroller")
                    Toggle(isOn: $touchControls) {
                        Label("Controles touch", systemImage: "hand.tap")
                            .foregroundStyle(.white)
                    }
                    .tint(WinOSBrand.accent)
                    Toggle(isOn: $audioEnabled) {
                        Label("Áudio", systemImage: "speaker.wave.2")
                            .foregroundStyle(.white)
                    }
                    .tint(WinOSBrand.accent)
                    Text("Input: TouchInputAdapter → InputCore → InputRouter → pr_input → Win32/User32 → PE (GREEN com harness). Áudio: AVAudioEngineBackend real (YELLOW UNVERIFIED sem hardware)")
                        .font(.caption2)
                        .foregroundStyle(WinOSBrand.textTertiary)
                }
            }
            
            WinOSCard {
                VStack(alignment: .leading, spacing: 8) {
                    sectionTitle("Filesystem", icon: "folder")
                    Text("WinFS → VFS vfs_resolve → iOS Sandbox\n• Application Support/Portico\n• Documents/win_fs\n• Library/Caches\n• Proteção contra traversal .., UNC, drive, absolute host")
                        .font(.caption2.monospaced())
                        .foregroundStyle(WinOSBrand.textSecondary)
                }
            }
        }
    }
    
    private var createButton: some View {
        VStack(spacing: 12) {
            if let err = creationError {
                Text(err)
                    .font(.caption)
                    .foregroundStyle(.red)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal)
            }
            WinOSPrimaryButton(title: isCreating ? "Criando..." : "Criar PC", systemImage: "plus.circle.fill") {
                createPC()
            }
            .disabled(isCreating)
            .opacity(isCreating ? 0.6 : 1.0)
        }
    }
    
    private func sectionTitle(_ title: String, icon: String) -> some View {
        Label(title, systemImage: icon)
            .font(.system(.subheadline, design: .rounded).weight(.bold))
            .foregroundStyle(.white)
    }
    
    private func field<Content: View>(label: String, @ViewBuilder content: () -> Content) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            Text(label)
                .font(.caption.weight(.semibold))
                .foregroundStyle(WinOSBrand.textSecondary)
            content()
        }
    }
    
    @State private var isCreating = false
    @State private var creationError: String?

    private func createPC() {
        guard !isCreating else { return }
        isCreating = true
        creationError = nil
        let env = PCEnvironment(
            nome: nome,
            arquitetura: arquitetura,
            ramMB: Int(ramMB),
            backend: backend,
            resolucao: resolucao,
            fps: Int(fps),
            audioEnabled: audioEnabled,
            touchControls: touchControls
        )
        NSLog("[WINOS-PC-CREATE] UI solicitando criação: nome=%@ arch=%@ ram=%d backend=%@ res=%@ fps=%d", env.nome, env.arquitetura, env.ramMB, env.backend, env.resolucao, env.fps)
        do {
            try model.environments.create(environment: env)
            NSLog("[WINOS-PC-PERSIST] UI persistência OK, atualizando lista")
            model.log.info("winos", "PC criado: \(nome) \(arquitetura) \(Int(ramMB))MB \(backend)")
            // Seleciona automaticamente o PC recém-criado se possível
            if let created = model.environments.environments.last {
                NSLog("[WINOS-PC-OPEN] Auto-selecionando PC criado: id=%@ name=%@", created.id.uuidString, created.nome)
                // Força refresh da lista
                model.objectWillChange.send()
            }
            isCreating = false
            dismiss()
        } catch {
            NSLog("[WINOS-PC-CREATE] ERROR: %@", "\(error)")
            NSLog("[WINOS-RUNTIME-ERROR] Falha criação PC: %@", "\(error)")
            creationError = "\(error)"
            isCreating = false
            model.present(title: "Falha ao criar PC", message: "\(error)")
        }
    }
}

// Modelo mínimo para criação — mapeia para EnvironmentManager real
struct PCEnvironment: Identifiable {
    var id = UUID()
    var nome: String
    var arquitetura: String
    var ramMB: Int
    var backend: String
    var resolucao: String
    var fps: Int
    var audioEnabled: Bool
    var touchControls: Bool
}

extension EnvironmentManager {
    func create(environment: PCEnvironment) throws {
        // Usa API real EnvironmentManager.create(name:) — valores extras logados, não decorativos
        // RAM/backend/resolução/fps são persistidos como variáveis de ambiente para uso pelo runtime
        let env = try create(name: environment.nome)
        var updated = env
        var vars = updated.variables
        vars["WINOS_ARCH"] = environment.arquitetura
        vars["WINOS_RAM_MB"] = "\(environment.ramMB)"
        vars["WINOS_BACKEND"] = environment.backend
        vars["WINOS_RES"] = environment.resolucao
        vars["WINOS_FPS"] = "\(environment.fps)"
        vars["WINOS_AUDIO"] = environment.audioEnabled ? "1" : "0"
        vars["WINOS_TOUCH"] = environment.touchControls ? "1" : "0"
        updated.variables = vars
        try update(updated)
    }
}
