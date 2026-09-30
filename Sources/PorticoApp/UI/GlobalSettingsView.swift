import SwiftUI
#if !PORTICO_XCODE_MONOLITHIC
import PorticoCore
#endif
/// Configurações globais: tema, idioma, qualidade, áudio, controles,
/// armazenamento e logs.
struct GlobalSettingsView: View {
    @EnvironmentObject var model: AppModel
    @State private var settings = GlobalSettings.default
    @State private var showingCapabilities = false

    var body: some View {
        Form {
            Section("Aparência") {
                Picker("Tema", selection: $settings.tema) {
                    Text("Sistema").tag(ThemeChoice.system)
                    Text("Escuro").tag(ThemeChoice.dark)
                    Text("Claro").tag(ThemeChoice.light)
                }
                Picker("Idioma", selection: $settings.idioma) {
                    Text("Sistema").tag(LanguageChoice.system)
                    Text("Português (BR)").tag(LanguageChoice.ptBR)
                    Text("English (US)").tag(LanguageChoice.enUS)
                }
            }

            Section("Qualidade") {
                Picker("Qualidade gráfica", selection: $settings.qualidade) {
                    ForEach(QualityChoice.allCases, id: \.self) { q in
                        Text(q.rawValue.capitalized).tag(q)
                    }
                }
                Text("Escala de renderização: \(Int(settings.qualidade.renderScale * 100))% · "
                     + "FPS sugerido: \(settings.qualidade.suggestedFPS)")
                    .font(.caption)
                    .foregroundStyle(.secondary)
            }

            Section("Áudio") {
                Slider(value: $settings.masterVolume, in: 0...1) {
                    Text("Volume geral \(Int(settings.masterVolume * 100))%")
                }
                Toggle("Áudio em segundo plano (quando permitido)",
                       isOn: $settings.audioSessionsEnabled)
            }

            Section("Controles") {
                Toggle("Mostrar controles na tela", isOn: $settings.showTouchControls)
                Toggle("Controles físicos", isOn: $settings.physicalControllersEnabled)
            }

            Section("Armazenamento") {
                Stepper("Cache máximo: \(settings.storageCacheLimitMB) MiB",
                        value: $settings.storageCacheLimitMB, in: 64...8192, step: 64)
                LabeledContent("Disponível",
                    value: spaceText)
                LabeledContent("Biblioteca",
                    value: "\(model.library.totalInstallBytes / (1024 * 1024)) MiB")
            }

            Section("Logs") {
                Picker("Nível mínimo", selection: $settings.logLevelFilter) {
                    ForEach(LogLevelFilter.allCases, id: \.self) { f in
                        Text(label(for: f)).tag(f)
                    }
                }
                Stepper("Máximo de entradas: \(settings.logMaxEntries)",
                        value: $settings.logMaxEntries, in: 200...50000, step: 200)
                Toggle("Exportar log automaticamente em falhas",
                       isOn: $settings.logAutoExportOnFailure)
                NavigationLink("Ver logs…") { LogsView() }
            }

            Section("Diagnóstico") {
                Button("Relatório de capacidades…") { showingCapabilities = true }
            }

            Section {
                Button("Salvar configurações") {
                    save()
                }
                .frame(maxWidth: .infinity)
            }
        }
        .navigationTitle("Configurações")
        .onAppear { settings = model.config.global }
        .sheet(isPresented: $showingCapabilities) {
            NavigationStack { CapabilitiesView() }
        }
    }

    private var spaceText: String {
        let bytes = model.sandbox.availableSpaceBytes()
        return bytes >= 0 ? "\(bytes / (1024 * 1024)) MiB" : "indisponível"
    }

    private func label(for f: LogLevelFilter) -> String {
        switch f {
        case .debug: return "DEBUG (tudo)"
        case .info: return "INFO"
        case .warning: return "WARNING"
        case .error: return "ERROR"
        }
    }

    private func save() {
        do {
            try model.config.update { g in
                g = settings
            }
            model.log.minLevel = {
                switch settings.logLevelFilter {
                case .debug: return .debug
                case .info: return .info
                case .warning: return .warning
                case .error: return .error
                }
            }()
        } catch {
            model.present(title: "Falha ao salvar", message: "\(error)")
        }
    }
}
