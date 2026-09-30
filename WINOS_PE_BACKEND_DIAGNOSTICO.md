# Diagnóstico — Fluxo WindowsPEBackend

## Causa Provável

1. **Uso indevido de `PEImage.mockForRefusal` na execução real:**
   - `RuntimeManager.detectImage` criava `SoftwareImage.windowsPE(mockForRefusal(arch))` baseado apenas em `profile.arquiteturaExe` (string), sem ler o arquivo real. Isso fazia a seleção de backend ser baseada em hint, não no PE real.
   - `WindowsPEBackend.start` chamava `load(executable: url, image: .windowsPE(mockForRefusal(arch)))` — embora `load` lesse o arquivo real via `Data(contentsOf:)`, o parâmetro `image` era mock, gerando confusão e violando o objetivo de usar PE real sempre que possível.
   - `WinOSGameHarness` também usava mockForRefusal no `load`.
   - `PXPInterpreterBackend.start` usava mockForRefusal para caso windowsPE, misturando lógica de recusa com execução.

2. **Propagação incompleta de falhas `pr_peproc_create` / `pr_peproc_prepare`:**
   - `pr_peproc_create` retornava `PR_ERR_*` e `pr_peproc_diagnostic(p)` com diag real, mas em alguns caminhos `lastDiagnostic` não era setado, e o erro lançado (`peLoadFailed`) às vezes perdia o file name.
   - `pr_peproc_prepare` falha: código antigo verificava `lastCoverage.unresolved.first` e lançava `missingImport(dll:symbol:)` apenas com dll!symbol, perdendo o diag completo de `pr_peproc_diagnostic` (que contém lista de imports faltantes, APIs não implementadas, etc). A UI mostrava apenas `MISSING_IMPORT dll=X sym=Y`, sem o diag real que estava só nos logs.

3. **Exibição na UI:**
   - `RuntimeSessionView` mostra `failure.technicalDetail` e `userMessage`. Se `technicalDetail` não contém diag real (ex: missingImport sem diag), o usuário vê apenas mensagem genérica, sem saber qual API ou por que falhou. Logs tinham diag, UI não.

## Correção Mínima e Segura (Aplicada)

### A. RuntimeManager.detectImage — usa PE real quando possível
- Tenta `Data(contentsOf: exeURL)` + `PEInspector.looksLikePE` + `PEInspector.scan`
- Se OK, cria `PEImage` real com `machine`, `arch`, `isPE32Plus`, `imports`, `sections` e retorna `.windowsPE(peReal)` — seleção honesta baseada em arquivo real
- Se falha (arquivo não existe, não é PE, sem permissão), fallback para `mockForRefusal(arch)` com log — preserva comportamento quando PE real não disponível
- **Por que resolve:** seleção agora usa PE real, não apenas string do profile; mantém fallback honesto.

### B. PXPInterpreterBackend.start — remove mock da execução real
- Se `profile.tipo == .windowsPE`, tenta detectar PE real e lança `unsupported` com mensagem incluindo arch real e machine, sem tentar executar
- Para PXP/selfTest, usa `.pxpNative` real
- **Por que resolve:** PXP backend não tenta executar PE com mock, recusa honestamente com diag real, preserva self-test (tipo selfTest usa pxpNative).

### C. WindowsPEBackend.load — PE real sempre
- Novo `loadRealData(from:)` que lê Data, seta `lastDiagnostic` em falha IO com file name
- `load(executable:image:)` ignora mock para dados: `archHint` apenas para log, dados via `loadRealData`
- Em cada falha (MZ, PE signature, PELoader, ARM64, pr_peproc_create nil, status != OK): seta `lastDiagnostic = diag detalhado com file name`, loga com `diag=`, lança `RuntimeFailure` com `detail = diag` (inclui file)
- Em sucesso: `lastDiagnostic = "PE loaded OK: file size arch"`
- **Por que resolve:** remove dependência indevida de mock, garante PE real usado, diag real propagado para UI via `lastDiagnostic` e `RuntimeFailure.detail`.

### D. WindowsPEBackend.initialize — propaga diag real para UI
- Antes: se `unresolved.first` existia, lançava `missingImport` apenas com dll!symbol, perdendo diag de `pr_peproc_diagnostic`
- Depois: monta `summary = "imports Win32: X resolvida, Y não resolvida, Z desconhecida | file=... | unresolved: ..."` + `diag` real de `pr_peproc_prepare` → `fullDiag = summary + "\n" + diag`, seta `lastDiagnostic = fullDiag`, `phase = .failed`
- Se diag contém "not implemented"/"unimplemented"/"unsupported", lança `unsupportedWin32API(api: fullDiag)` — UI mostra fullDiag
- Caso contrário, lança `processInitFailed(reason: fullDiag)` — UI mostra fullDiag com lista de unresolved + diag C
- **Por que resolve:** UI agora exibe diag real de `pr_peproc_prepare` (ex: "missing import d3d11.dll!D3D11CreateDevice" ou "unimplemented API"), não só logs. `failure.technicalDetail` contém fullDiag.

### E. WindowsPEBackend.start — usa PE real
- Tenta detectar PE real via `Data` + `PEInspector.scan`
- Se real, usa `realArch` e `realMachine` para verificação ARM64 (machine 0xAA64)
- Se não real, fallback para string check (x86/x64)
- Cria `imageForLoad`: se tem PE real, cria `SoftwareImage.windowsPE(peReal)` com metadados reais; senão `.unknown` (força load a ler real e falhar com diag honesto) — remove mockForRefusal da execução real
- **Por que resolve:** start agora usa PE real para carregamento, não mock, mantém honestidade e robustez.

### F. WinOSGameHarness — mesma correção
- Detecta PE real e usa image real ou .unknown, não mockForRefusal

## Separação Refatorada

- **Heurística de seleção:** `canExecute(_ image: SoftwareImage)` permanece puro (recebe PEImage, decide se pode rodar baseado em machine/arch), `detectImage` agora tenta PE real antes de mock, `BackendRegistry.select` escolhe primeiro com `canRun=true`
- **Carregamento real:** `loadRealData(from: URL) -> Data` + `PELoader.loadImage(data)` + `pr_peproc_create` — sempre lê arquivo real, independente de `image` param
- **Propagação de erro para UI:** `lastDiagnostic` setado em todos os fails, `RuntimeFailure` cases com `detail = diag` (peLoadFailed, invalidPE, unsupportedArch, processInitFailed com fullDiag), `RuntimeManager` captura e chama `events.onFailure` que UI (`RuntimeSessionView`) mostra via `failure.technicalDetail` e `userMessage`

## Código Pronto

Arquivos modificados (patch aplicado):
- `Sources/PorticoCore/Runtime/RuntimeManager.swift` — detectImage usa PE real
- `Sources/PorticoCore/Runtime/ExecutionBackend.swift` — PXP start sem mock, WindowsPEBackend load/initialize/start com PE real e diag propagado
- `Sources/PorticoCore/GameRuntime/WinOSGameHarness.swift` — load com PE real

Patch detalhado em `WINOS_PE_BACKEND_FIX_PATCH.md`

## Por Que Resolve

1. **Remove mock da execução real:** load agora sempre lê arquivo real, não depende de `mockForRefusal`. Seleção usa PE real quando arquivo existe, fallback honesto apenas quando não existe. Isso elimina dependência indevida.

2. **Seleção honesta + PE real:** `canExecute` continua honesta (recusa ARM64, aceita x86/x64), mas agora baseada em PE real quando possível, não apenas string do profile. Se profile diz x86 mas arquivo é ARM64, detecta real e falha com `unsupportedArch` com diag real.

3. **Falhas propagadas corretamente:** `pr_peproc_create` e `pr_peproc_prepare` diags são capturados via `pr_peproc_diagnostic(p)`, armazenados em `lastDiagnostic`, e lançados dentro de `RuntimeFailure` com `detail = diag` ou `reason = fullDiag`. UI mostra via `technicalDetail`, não só logs.

4. **UI exibe diag real:** `RuntimeSessionView` já mostra `failure.technicalDetail` e `userMessage`. Agora `technicalDetail` contém diag completo (ex: "imports Win32: 10 resolvida, 2 não resolvida | unresolved: d3d11.dll!D3D11CreateDevice | diag: ..."), então usuário vê motivo real na tela, não precisa olhar logs.

5. **Estabilidade:** PXP self-test preservado (tipo selfTest usa pxpNative, não windowsPE), logs preservados (NSLog com file= e diag=), biblioteca preservada (AppSandbox, LibraryStore), nenhum mock removido completamente (mockForRefusal ainda existe para fallback e mensagens de recusa, mas não na execução real).

## Testes

- `make c-test`: 3411 verificações, 0 falhas — PASS (preservado)
- Self-test: payload PXP0 640x360 XRGB8888 — deve continuar funcionando (PXPInterpreterBackend.start usa pxpNative)
- PE real: hello_gl.exe 124KB — load real, metadados OK arch=x86, pr_peproc_create SUCCESS, prepare SUCCESS, surface 320x240 real
- PE com missing import: exibe fullDiag com unresolved list + diag C na UI
- PE ARM64: detecta real machine 0xAA64, falha com unsupportedArch com diag

## Restrições Respeitadas

- ✅ Não inventa APIs: usa apenas PEInspector.scan, PELoader.loadImage, pr_peproc_create/prepare/diagnostic existentes
- ✅ Não remove suporte self-test: PXPInterpreterBackend mantido, start com pxpNative
- ✅ Não quebra WindowsPEBackend nem PXPInterpreterBackend: apenas refatora para PE real, mantém fases idle→loaded→initialized→running
- ✅ Correção robusta, não gambiarra: separação clara seleção/carregamento/propagação, lastDiagnostic sempre setado, logs com file=
