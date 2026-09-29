import Foundation

/// Executável candidato encontrado em uma importação.
public struct ExecutableCandidate: Identifiable, Equatable, Sendable {
    public var id: String { relativePath }
    public let relativePath: String
    public let sizeBytes: Int64
    public let kind: GameKind
    public let pe: PEImage?
    public let score: Int   // relevância p/ escolha automática (maior = melhor)

    public var displayName: String {
        (relativePath as NSString).lastPathComponent
    }
}

public struct ImportScanResult: Equatable, Sendable {
    public let stagingDir: URL
    public let allFiles: [String]
    public let executables: [ExecutableCandidate]

    public var suggestedMain: ExecutableCandidate? {
        executables.max(by: { $0.score < $1.score })
    }
}

public enum ImportError: Error, Equatable, LocalizedError {
    case unsupportedFormat(String)
    case noExecutables
    case copyFailed(String)

    public var errorDescription: String? {
        switch self {
        case .unsupportedFormat(let n):
            return "Formato não suportado: \(n) (suportados: diretórios, ZIP)"
        case .noExecutables:
            return "Nenhum executável reconhecido na importação."
        case .copyFailed(let m):
            return "Falha ao copiar os arquivos: \(m)"
        }
    }
}

/// Fluxo oficial de importação:
/// 1) copia para o sandbox (via URL autorizado pelo document picker);
/// 2) extrai ZIPs; 3) analisa arquivos (PE/PXP); 4) o usuário escolhe o
/// executável principal; 5) cria o GameProfile; 6) adiciona à biblioteca.
public final class ImportService {
    public let sandbox: AppSandbox
    public let library: LibraryStore
    public let environments: EnvironmentManager
    public let log: LogCenter

    public init(sandbox: AppSandbox, library: LibraryStore,
                environments: EnvironmentManager, log: LogCenter) {
        self.sandbox = sandbox
        self.library = library
        self.environments = environments
        self.log = log
    }

    /// Copia um item (.exe, .7z, .zip ou diretório) para área de stage e analisa.
    public func scanImport(from externalURL: URL) async throws -> ImportScanResult {
        try sandbox.ensureDirectories()
        let staging = sandbox.importsDir
            .appendingPathComponent("stage-\(UUID().uuidString)", isDirectory: true)
        try FileManager.default.createDirectory(at: staging, withIntermediateDirectories: true)

        let lower = externalURL.lastPathComponent.lowercased()
        NSLog("[WINOS-IMPORT-START] scanImport: %@", externalURL.path)
        NSLog("[WINOS-IMPORT-TYPE] lower=%@ isDir check", lower)

        if lower.hasSuffix(".zip") {
            NSLog("[WINOS-IMPORT-TYPE] Detectado ZIP: %@", lower)
            let zipDest = staging.appendingPathComponent(externalURL.lastPathComponent)
            let imported = try sandbox.importFrom(external: externalURL,
                                       toRelative: relativeToRoot(zipDest))
            NSLog("[WINOS-IMPORT-COPY] ZIP copiado para: %@", imported.path)
            let reader = try ZipReader(url: zipDest)
            log.info("import", "ZIP com \(reader.entries.count) entrada(s)")
            NSLog("[WINOS-IMPORT-COPY] ZIP entries: %d", reader.entries.count)
            try reader.extract(to: staging, log: log)
            try? FileManager.default.removeItem(at: zipDest)
            NSLog("[WINOS-IMPORT-SUCCESS] ZIP extraído para staging: %@", staging.path)
        } else if lower.hasSuffix(".exe") {
            NSLog("[WINOS-IMPORT-TYPE] Detectado EXE: %@", lower)
            // Copia .exe direto para staging como executável principal
            let dest = staging.appendingPathComponent(externalURL.lastPathComponent)
            let imported = try sandbox.importFrom(external: externalURL,
                                       toRelative: relativeToRoot(dest))
            NSLog("[WINOS-IMPORT-COPY] EXE copiado: %@ -> %@", externalURL.path, imported.path)
        } else if lower.hasSuffix(".7z") {
            NSLog("[WINOS-IMPORT-TYPE] Detectado 7Z: %@ - copiando como está (extração futura)", lower)
            // Por enquanto, copia .7z para staging; se tiver lib de extração, extrairia aqui
            // Como não temos descompressão 7z nativa no runtime atual, copiamos e tentamos analisar como arquivo
            // Se contiver PE dentro, usuário precisará extrair externamente ou usaremos ZipReader se for ZIP disfarçado
            let dest = staging.appendingPathComponent(externalURL.lastPathComponent)
            let imported = try sandbox.importFrom(external: externalURL,
                                       toRelative: relativeToRoot(dest))
            NSLog("[WINOS-IMPORT-COPY] 7Z copiado: %@ (size check)", imported.path)
            // Tenta extrair como ZIP se for compatível, senão mantém como arquivo
            if let reader = try? ZipReader(url: imported) {
                NSLog("[WINOS-IMPORT-COPY] 7Z é na verdade ZIP compatível, extraindo %d entries", reader.entries.count)
                try reader.extract(to: staging, log: log)
                try? FileManager.default.removeItem(at: imported)
            } else {
                NSLog("[WINOS-IMPORT-TYPE] 7Z mantido como arquivo único, será tratado como executável se for PE ou requer extração externa")
                // Se o .7z em si for um PE (improvável), ainda será detectado; senão, usuário precisará extrair
            }
        } else {
            let isDir = (try? externalURL.resourceValues(forKeys: [.isDirectoryKey]))?.isDirectory ?? false
            NSLog("[WINOS-IMPORT-TYPE] isDirectory=%@ for %@", isDir ? "YES" : "NO", externalURL.path)
            if isDir {
                let dest = staging.appendingPathComponent("content")
                let imported = try sandbox.importFrom(external: externalURL,
                                           toRelative: relativeToRoot(dest))
                NSLog("[WINOS-IMPORT-COPY] Diretório copiado: %@ -> %@", externalURL.path, imported.path)
            } else {
                // Arquivo genérico (pode ser .exe sem extensão, ou outro)
                NSLog("[WINOS-IMPORT-TYPE] Arquivo genérico, copiando como está: %@", lower)
                let dest = staging.appendingPathComponent(externalURL.lastPathComponent)
                let imported = try sandbox.importFrom(external: externalURL,
                                           toRelative: relativeToRoot(dest))
                NSLog("[WINOS-IMPORT-COPY] Arquivo copiado: %@ -> %@", externalURL.path, imported.path)
                // Se for PE, será detectado na análise
            }
        }

        let result = try analyze(stagingDir: staging)
        if result.executables.isEmpty {
            log.warning("import", "nenhum executável encontrado na importação")
            NSLog("[WINOS-IMPORT-ERROR] Nenhum executável após análise de %@", staging.path)
        } else {
            NSLog("[WINOS-IMPORT-SUCCESS] Encontrados %d executáveis", result.executables.count)
        }
        return result
    }

    /// Analisa uma área de stage: lista arquivos e identifica executáveis (PE e PXP).
    public func analyze(stagingDir: URL) throws -> ImportScanResult {
        var allFiles: [String] = []
        var executables: [ExecutableCandidate] = []
        let rootPath = stagingDir.standardizedFileURL.path

        guard let en = FileManager.default.enumerator(
            at: stagingDir, includingPropertiesForKeys: [.fileSizeKey, .isRegularFileKey]) else {
            return ImportScanResult(stagingDir: stagingDir, allFiles: [], executables: [])
        }

        for case let fileURL as URL in en {
            let vals = try? fileURL.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey])
            guard vals?.isRegularFile == true else { continue }
            let rel = String(fileURL.standardizedFileURL.path.dropFirst(rootPath.count + 1))
            allFiles.append(rel)
            let size = Int64(vals?.fileSize ?? 0)

            if let data = try? Data(contentsOf: fileURL, options: .mappedIfSafe) {
                if PEInspector.looksLikePE(data), let pe = try? PEInspector.scan(data) {
                    var score = 10
                    if pe.isWindowsExecutable { score += 20 }
                    if pe.subsystem == 2 { score += 5 }     // GUI
                    if !pe.isDLL && pe.arch == "x86" { score += 8 }
                    let lower = rel.lowercased()
                    if lower.hasPrefix("game") || lower.contains("app") { score += 2 }
                    executables.append(ExecutableCandidate(
                        relativePath: rel, sizeBytes: size,
                        kind: .windowsPE, pe: pe, score: score))
                } else if data.count >= 4,
                          data.prefix(4).elementsEqual([0x50, 0x58, 0x50, 0x30]) { // "PXP0"
                    executables.append(ExecutableCandidate(
                        relativePath: rel, sizeBytes: size,
                        kind: .pxpNative, pe: nil, score: 30))
                }
            }
        }
        executables.sort { $0.score > $1.score }
        log.info("import", "análise: \(allFiles.count) arquivo(s), \(executables.count) executável(is)")
        return ImportScanResult(stagingDir: stagingDir,
                                allFiles: allFiles.sorted(),
                                executables: executables)
    }

    /// Finaliza a importação: move o stage para Games/<id>/, cria o perfil e
    /// registra na biblioteca.
    @discardableResult
    public func finalize(scan: ImportScanResult,
                         mainExecutable: ExecutableCandidate,
                         name: String) throws -> GameProfile {
        let gameID = UUID()
        let dirName = "game-\(gameID.uuidString.prefix(8)).\(sanitize(name))"
        let destRel = "Games/\(dirName)"
        let dest = try sandbox.resolveInside(destRel)
        try FileManager.default.moveItem(at: scan.stagingDir, to: dest)

        let size = (try? sandbox.directorySize(dest)) ?? mainExecutable.sizeBytes
        let env = try environments.ensureDefault()

        let profile = GameProfile(
            id: gameID,
            nome: name,
            caminho: destRel,
            executavel: mainExecutable.relativePath,
            arquiteturaExe: mainExecutable.pe?.arch ?? "n/a",
            tipo: mainExecutable.kind,
            tamanhoInstalacao: size
        )
        var p = profile
        p.ambiente = EnvironmentRef(id: env.id, name: env.nome)
        _ = try library.add(p)
        log.info("import", "importação concluída: \(name) → \(destRel)")
        return p
    }

    private func relativeToRoot(_ url: URL) -> String {
        let rootPath = sandbox.root.standardizedFileURL.path
        return String(url.standardizedFileURL.path.dropFirst(rootPath.count + 1))
    }

    private func sanitize(_ name: String) -> String {
        let allowed = CharacterSet.alphanumerics.union(CharacterSet(charactersIn: "-_"))
        let scalars = name.lowercased().unicodeScalars.map {
            allowed.contains($0) ? Character($0) : "-"
        }
        return String(scalars.prefix(24))
    }
}
