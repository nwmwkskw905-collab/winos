import SwiftUI
import UniformTypeIdentifiers
#if !PORTICO_XCODE_MONOLITHIC
import PorticoCore
#endif
/// Importação via document picker oficial do iOS com suporte a .exe, .7z, .zip
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

    // UTTypes explícitos para iOS Files picker
    static var supportedTypes: [UTType] {
        var types: [UTType] = [.folder, .zip, .data, .item]
        // Tenta UTType específicos, fallback para .data
        if let exe = UTType("com.microsoft.windows-executable") ?? UTType(filenameExtension: "exe") {
            types.append(exe)
        }
        if let sevenZip = UTType("org.7-zip.7-zip-archive") ?? UTType(filenameExtension: "7z") {
            types.append(sevenZip)
        }
        if let zipArchive = UTType("public.zip-archive") ?? UTType(filenameExtension: "zip") {
            types.append(zipArchive)
        }
        // Adiciona extensões manualmente via UTType
        if let exeExt = UTType(filenameExtension: "exe") { types.append(exeExt) }
        if let sevenExt = UTType(filenameExtension: "7z") { types.append(sevenExt) }
        return Array(Set(types.map { $0.identifier }).compactMap { UTType($0) })
    }

    var body: some View {
        NavigationStack {
            Group {
                switch stage {
                case .pick:
                    pickStage
                case .analyze:
                    VStack(spacing: 16) {
                        ProgressView("Analisando arquivos…")
                        Text("[WINOS-IMPORT-START] Analisando...")
                            .font(.caption2.monospaced())
                            .foregroundStyle(.secondary)
                    }
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
            allowedContentTypes: Self.supportedTypes,
            allowsMultipleSelection: false
        ) { result in
            switch result {
            case .success(let urls):
                if let url = urls.first {
                    NSLog("[WINOS-IMPORT-START] File picker success: %@", url.path)
                    NSLog("[WINOS-IMPORT-URL] URL: %@ ext=%@", url.absoluteString, url.pathExtension)
                    Task { await analyze(url) }
                }
            case .failure(let err):
                NSLog("[WINOS-IMPORT-ERROR] File picker failure: %@", "\(err)")
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
            Text("Selecione .exe, .7z, .zip ou pasta. Suporta:\n• Windows .exe (PE)\n• 7-Zip .7z (requer extração)\n• ZIP .zip\nTudo é copiado para sandbox do app via security-scoped URL.")
                .font(.footnote)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal)
            VStack(spacing: 8) {
                Text("Tipos suportados:")
                    .font(.caption2.bold())
                    .foregroundStyle(.secondary)
                HStack(spacing: 8) {
                    Label(".exe", systemImage: "doc")
                    Label(".7z", systemImage: "doc.zipper")
                    Label(".zip", systemImage: "doc.zipper")
                    Label("Pasta", systemImage: "folder")
                }
                .font(.caption2)
                .foregroundStyle(.secondary)
            }
            Button("Escolher arquivo ou pasta…") {
                NSLog("[WINOS-IMPORT-START] Botão escolher arquivo pressionado")
                pickingFolder = true
            }
            .buttonStyle(.borderedProminent)
            Text("[WINOS-IMPORT] iOS Files picker com UTType public.data + exe + 7z + zip")
                .font(.system(size: 9, design: .monospaced))
                .foregroundStyle(.secondary.opacity(0.6))
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
                                let peType = pe.is64Bit ? "PE32+" : "PE32"
                                let peSub = pe.subsystem == 2 ? "GUI" : "console"
                                let peKind = pe.isDLL ? "DLL" : "executável"
                                let peInfo = "\(peType) · \(pe.arch) · \(peSub) · \(pe.sections.count) seções · \(pe.imports.count) DLLs importadas · \(peKind)"
                                Text(peInfo)
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

    @MainActor
    private func analyze(_ url: URL) async {
        stage = .analyze
        NSLog("[WINOS-IMPORT-START] Iniciando análise URL: %@", url.path)
        let ext = url.pathExtension.lowercased()
        NSLog("[WINOS-IMPORT-TYPE] Extensão: %@, lastComponent: %@", ext, url.lastPathComponent)
        do {
            // Captura importer fora do detached para evitar capturar MainActor model em background
            let importer = model.importer
            let result = try await Task.detached(priority: .userInitiated) {
                NSLog("[WINOS-IMPORT-URL] Task detached scanImport: %@", url.path)
                return try await importer.scanImport(from: url)
            }.value
            self.scan = result
            NSLog("[WINOS-IMPORT-SUCCESS] Análise OK: %d arquivos, %d executáveis", result.allFiles.count, result.executables.count)
            for exe in result.executables {
                NSLog("[WINOS-IMPORT-TYPE] Executável: %@ kind=%@ size=%lld", exe.relativePath, "\(exe.kind)", exe.sizeBytes)
            }
            if result.executables.isEmpty {
                NSLog("[WINOS-IMPORT-ERROR] Nenhum executável encontrado")
                fail(ImportError.noExecutables.localizedDescription)
            } else {
                stage = .chooseExe
            }
        } catch {
            NSLog("[WINOS-IMPORT-ERROR] Falha análise: %@", "\(error)")
            fail("\(error)")
        }
    }

    private func finalize() {
        guard let scan, let chosen else { return }
        NSLog("[WINOS-IMPORT-COPY] Finalizando importação: chosen=%@ name=%@", chosen.relativePath, gameName)
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
