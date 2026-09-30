# PATCH — Correção fluxo WindowsPEBackend

## 1. RuntimeManager.swift — detectImage usa PE real

Antes:
```
private static func detectImage(kind: GameKind, arch: String, executableURL: URL) -> SoftwareImage {
    switch kind {
    case .windowsPE: return .windowsPE(PEImage.mockForRefusal(arch))
    case .pxpNative, .selfTest: return .pxpNative
    }
}
```

Depois:
- Tenta Data(contentsOf:) + PEInspector.looksLikePE + PEInspector.scan
- Se sucesso, cria PEImage real com machine, arch, isPE32Plus etc e retorna .windowsPE(peReal)
- Se falha, fallback para mockForRefusal com log

Motivo: seleção honesta deve usar PE real quando arquivo existe, não apenas string do profile.

---

## 2. ExecutionBackend.swift — PXPInterpreterBackend.start

Antes:
```
let image: SoftwareImage = (context.profile.tipo == .windowsPE)
    ? .windowsPE(PEImage.mockForRefusal(context.profile.arquiteturaExe))
    : .pxpNative
try load(executable: context.executableURL, image: image)
```

Depois:
- Se profile.tipo == windowsPE, detecta PE real e lança RuntimeFailure.unsupported com mensagem honesta incluindo arch real e machine, sem tentar executar
- Para PXP, usa .pxpNative real

Motivo: PXP backend não deve usar mock para tentar executar PE; deve recusar honestamente com diag real.

---

## 3. ExecutionBackend.swift — WindowsPEBackend.load

Antes:
- Usava image param apenas como hint, mas logava archHint = pe.arch (pode ser mock)
- Verificações MZ/PE, PELoader.loadImage, pr_peproc_create, mas lastDiagnostic não setado em todos os fails
- Propagava diag via peLoadFailed apenas em alguns casos

Depois:
- Novo método loadRealData(from:) que lê Data e seta lastDiagnostic em falha IO
- load() ignora image mock para dados reais: archHint apenas para log, mas dados reais via loadRealData
- Em cada falha (MZ, PE signature, PELoader, ARM64, pr_peproc_create nil, pr_peproc_create status != OK):
  - seta lastDiagnostic = diag detalhado com file name
  - loga com diag
  - lança RuntimeFailure com detail = diag (peLoadFailed, invalidMZ, invalidPE, unsupportedArch)
- Em sucesso: lastDiagnostic = "PE loaded OK: file size arch"

Motivo: remove dependência indevida de mock, garante que PE real é usado, diag real propagado para UI via lastDiagnostic e RuntimeFailure.detail.

---

## 4. ExecutionBackend.swift — WindowsPEBackend.initialize

Antes:
```
let diag = String(cString: pr_peproc_diagnostic(p))
lastDiagnostic = diag
if let firstUnresolved = cov.unresolved.first {
    throw missingImport(dll:..., symbol:...)
}
if diag.contains("not implemented") { throw unsupportedWin32API }
throw processInitFailed(reason: summary + diag)
```

Problema: se unresolved existe, lança missingImport com apenas dll!symbol, perdendo diag real de pr_peproc_prepare.

Depois:
```
let diag = String(cString: pr_peproc_diagnostic(p))
summary = "imports Win32: X resolvida, Y não resolvida, Z desconhecida | file=... | unresolved: dll!api, ..."
fullDiag = summary + "\n" + diag
lastDiagnostic = fullDiag
phase = .failed
if diag.lowercased.contains("not implemented"/"unimplemented"/"unsupported") {
    throw unsupportedWin32API(api: fullDiag)
}
throw processInitFailed(reason: fullDiag)
```

Motivo: garante que diag real de pr_peproc_prepare (que contém motivo exato: missing import, unsupported API, etc) é exibido na UI via processInitFailed.reason, que vai para userMessage e technicalDetail. UI mostra fullDiag, não só logs.

---

## 5. ExecutionBackend.swift — WindowsPEBackend.start

Antes:
```
let arch = profile.arquiteturaExe
if !known { throw unsupported }
try load(executable: url, image: .windowsPE(mockForRefusal(arch)))
```

Depois:
- Tenta detectar PE real via Data + PEInspector.scan
- Se real, usa realArch e realMachine para verificação ARM64
- Se não real, fallback para string check
- Cria imageForLoad: se tem PE real, cria SoftwareImage.windowsPE(peReal) com metadados reais; senão .unknown (força load a ler real e falhar com diag honesto)
- Chama load com imageForLoad (real, não mock)
- lastDiagnostic setado em cada falha

Motivo: remove mockForRefusal da execução real, usa PE real para carregamento e seleção, mantém honestidade.

---

## 6. WinOSGameHarness.swift — load

Antes:
```
try backend.load(executable: url, image: .windowsPE(mockForRefusal(arch)))
```

Depois:
- Detecta PE real via PEInspector, cria SoftwareImage real ou .unknown, chama load

Motivo: mesmo que acima, remove mock da execução real no harness de testes.

---

## 7. Preservação

- PXPInterpreterBackend: mantido, apenas start() agora recusa PE com mensagem honesta usando PE real quando possível, sem quebrar self-test (self-test é .selfTest tipo, usa .pxpNative)
- mockForRefusal ainda existe em extension PEImage, usado apenas para fallback quando arquivo não existe ou para mensagens de recusa (não execução)
- Logs preservados: [WINOS-RUNTIME] PE_LOADER_INIT, WIN32_INIT, etc, agora com file= e diag=
- Biblioteca, VFS, self-test preservados

---

## Código pronto — diff resumido

Ver arquivos modificados:
- Sources/PorticoCore/Runtime/RuntimeManager.swift
- Sources/PorticoCore/Runtime/ExecutionBackend.swift
- Sources/PorticoCore/GameRuntime/WinOSGameHarness.swift
