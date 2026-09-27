import SwiftUI
import UniformTypeIdentifiers
import PorticoCore

/// Importação via document picker oficial do iOS (arquivos ZIP ou pastas).
/// Fluxo: escolher → analisar → escolher executável principal → nomear → salvar.
struct ImportFlowView: View {
    @EnvironmentObject var model: AppModel
    @Environment(\.dismiss) private var dismiss

    @State private var stage: Stage = .pick
    @State private var scan: ImportScanResult?
    @State private var chosen: ExecutableCandidate?
    @State private var gameName = ""
    @State private var errorMessage = ""
    @State private var pickingFolder = false

    enum Stage {
        case pick, analyze, chooseExe, name, failed
    }

    var body: some View {
        NavigationStack {
            Group {
                switch stage {
                case .pick:
                    pickStage
                case .analyze:
                    ProgressView("Analisando arquivos…")
                case .chooseExe:
                    exeStage
                case .name:
                    nameStage
                case .failed:
                    failedStage
                }
            }
            .navigationTitle("Adicionar jogo")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancelar") { dismiss() }
                }
            }
        }
        .fileImporter(
            isPresented: $pickingFolder,
            allowedContentTypes: [.folder, .zip],
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls):
                if let url = urls.first {
                    Task { await analyze(url) }
                }
            case .failure(let err):
                fail("Seleção cancelada ou inválida: \(err)")
            }
        }
    }

    private var pickStage: some View {
        VStack(spacing: 16) {
            Image(systemName: "square.and.arrow.down")
                .font(.system(size: 48))
                .foregroundStyle(.secondary)
            Text("Importar jogo ou aplicativo")
                .font(.headline)
            Text("Selecione um arquivo .zip ou uma pasta com os arquivos "
                 + "do software. Tudo é copiado para o sandbox do app — "
                 + "nada é acessado fora dele.")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal)
            Button("Escolher arquivo ou pasta…") {
                pickingFolder = true
            }
            .buttonStyle(.borderedProminent)
        }
        .padding()
    }

    private var exeStage: some View {
        List {
            Section {
                ForEach(scan?.executables ?? []) { exe in
                    Button {
                        chosen = exe
                        gameName = exe.displayName.replacingOccurrences(
                            of: (exe.displayName as NSString).pathExtension,
                            with: "").trimmingCharacters(in: CharacterSet(charactersIn: "."))
                        stage = .name
                    } label: {
                        VStack(alignment: .leading, spacing: 2) {
                            Text(exe.displayName).font(.body.bold())
                            Text(exe.relativePath)
                                .font(.caption)
                                .foregroundStyle(.secondary)
                            if let pe = exe.pe {
                                Text("PE\(pe.is64Bit ? "32+" : "32") · \(pe.arch) · "
                                     + "\(pe.subsystem == 2 ? "GUI" : "console") · "
                                     + "\(pe.sections.count) seções · "
                                     + "\(pe.imports.count) DLLs importadas · "
                                     + (pe.isDLL ? "DLL" : "executável"))
                                    .font(.caption2)
                                    .foregroundStyle(.secondary)
                                Text(pe.executionVerdict)
                                    .font(.caption2)
                                    .foregroundStyle(.orange)
                            } else {
                                Text("PXP nativo (IA-32 interpretado) — executável neste app")
                                    .font(.caption2)
                                    .foregroundStyle(.green)
                            }
                        }
                    }
                }
            } header: {
                Text("Escolha o executável principal")
            } footer: {
                Text("PEs Windows serão registrados, mas exigem a camada Win32 "
                     + "(ainda não integrada) para executar.")
            }

            if let scan, !scan.executables.isEmpty {
                Section("Sugerido") {
                    if let s = scan.suggestedMain {
                        Button("Usar sugerido: \(s.displayName)") {
                            chosen = s
                            gameName = s.displayName
                            stage = .name
                        }
                    }
                }
            }
        }
    }

    private var nameStage: some View {
        Form {
            Section("Nome do jogo") {
                TextField("Nome", text: $gameName)
            }
            if let chosen {
                Section("Resumo") {
                    LabeledContent("Executável", value: chosen.relativePath)
                    LabeledContent("Tipo", value: chosen.kind == .windowsPE
                        ? "Windows PE" : "Payload PXP")
                    LabeledContent("Arquivos importados",
                                   value: "\(scan?.allFiles.count ?? 0)")
                }
            }
            Section {
                Button("Adicionar à biblioteca") {
                    finalize()
                }
                .disabled(gameName.trimmingCharacters(in: .whitespaces).isEmpty)
            }
        }
    }

    private var failedStage: some View {
        VStack(spacing: 12) {
            Image(systemName: "exclamationmark.triangle")
                .font(.system(size: 40))
                .foregroundStyle(.orange)
            Text("Não foi possível importar")
                .font(.headline)
            Text(errorMessage)
                .font(.footnote)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
            Button("Tentar novamente") { stage = .pick }
                .buttonStyle(.bordered)
        }
        .padding()
    }

    private func analyze(_ url: URL) async {
        stage = .analyze
        do {
            let result = try await Task.detached(priority: .userInitiated) {
                try model.importer.scanImport(from: url)
            }.value
            self.scan = result
            if result.executables.isEmpty {
                fail(ImportError.noExecutables.localizedDescription)
            } else {
                stage = .chooseExe
            }
        } catch {
            fail("\(error)")
        }
    }

    private func finalize() {
        guard let scan, let chosen else { return }
        do {
            _ = try model.importer.finalize(
                scan: scan,
                mainExecutable: chosen,
                name: gameName.trimmingCharacters(in: .whitespaces))
            model.refreshGames()
            dismiss()
        } catch {
            fail("\(error)")
        }
    }

    private func fail(_ message: String) {
        errorMessage = message
        stage = .failed
    }
}
