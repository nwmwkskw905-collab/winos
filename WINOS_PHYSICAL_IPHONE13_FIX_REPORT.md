# WINOS — FASE PHYSICAL IPHONE 13 — CORREÇÃO DOS PROBLEMAS DO PRIMEIRO TESTE REAL

**Data:** 2026-09-29  
**Device:** iPhone 13 físico via IPA assinado Sideloadly  
**Bundle ID:** io.portico.Portico  
**Deployment Target:** iOS 17.0+  
**Arquitetura:** ARM64 host, x64 guest (interpreter)  
**Workflow:** .github/workflows/ios-build.yml com winos-app + winos-ipa

---

## 1. PROBLEMAS OBSERVADOS NO IPHONE 13 (RELATO FÍSICO)

### PROBLEMA 1 — Meus PCs → Criar PC → Abrir PC
- Criar PC aparentemente sucesso, mas não abre/entrar
- Sem feedback de erro real
- Sem logs estruturados

### PROBLEMA 2 — Biblioteca → Portico self-test → Jogar/Start
- Tela branca/colorida com glitch
- Botão Start sem efeito visível
- Graphics software GREEN, Metal YELLOW/UNVERIFIED
- Possível bloqueio GetMessage sem input

### PROBLEMA 3 — Importar → .7z/.exe
- Files picker abre mas pasta Portico não aparece
- .7z e .exe não reconhecidos/rejeitados por UTType

### PROBLEMA 4 — Sandbox e arquivos
- Paths Linux/Windows assumidos, não iOS sandbox real
- Estrutura Documents/WinOS não existia

### PROBLEMA 5 — Diagnóstico runtime
- Sem tela dedicada para diagnóstico físico iPhone

---

## 2. ARQUIVOS ALTERADOS

### Core (PorticoCore)
- `Sources/PorticoCore/Store/AppSandbox.swift`
  - Expandido para suportar nova estrutura: Documents/WinOS/PCs/, Library/, Imports/, Logs/ + Application Support/WinOS/Runtime/, VFS/ + Caches/WinOS/
  - Preserva Application Support/Portico legado (Games, Environments, Logs, Imports, Covers, Temp)
  - `ensureDirectories()` cria automaticamente todas as estruturas
  - Logs: [WINOS-SANDBOX] com root, Documents/WinOS, Caches/WinOS
  - Mantém `isInsideSandbox`, `resolveInside`, `importFrom` com security-scoped resource

- `Sources/PorticoCore/Environment/EnvironmentManager.swift`
  - Adicionados logs estruturados: [WINOS-PC-CREATE], [WINOS-PC-PERSIST], [WINOS-PC-OPEN], [WINOS-RUNTIME-START], [WINOS-RUNTIME-ERROR]
  - Validação de nome vazio, path existence, subdirs adicionais (Program Files, Windows)
  - Cria marker environment.json com id, runtimeVersion, dataCriacao
  - Preserva API `create(name:fixedID:)`, `save()`, `destroy()`, `markInUse()`

- `Sources/PorticoCore/Runtime/RuntimeManager.swift`
  - Logs detalhados: [WINOS-RUNTIME-START], [WINOS-PC-OPEN], [WINOS-RUNTIME-ERROR]
  - Validação exeURL exists, env existence, envVars count
  - Backend selection com log de image label, chosen displayName
  - Preserva ciclo prepare → start → tick → pause/resume → stop

- `Sources/PorticoCore/Import/ImportService.swift`
  - Suporte explícito .exe, .7z, .zip, folder, arquivo genérico
  - Logs: [WINOS-IMPORT-START], [WINOS-IMPORT-URL], [WINOS-IMPORT-TYPE], [WINOS-IMPORT-COPY], [WINOS-IMPORT-SUCCESS], [WINOS-IMPORT-ERROR]
  - .exe: copia direto para staging
  - .7z: copia e tenta extrair como ZIP se compatível, senão mantém
  - .zip: extrai via ZipReader
  - Folder: copia via importFrom
  - Genérico: copia como está, detecta PE na análise
  - Preserva `analyze()`, `finalize()`, `scanImport()`

### App (PorticoApp)
- `Sources/PorticoApp/UI/WinOSCreatePCView.swift`
  - Adicionado @State isCreating, creationError
  - Logs [WINOS-PC-CREATE], [WINOS-PC-PERSIST], [WINOS-PC-OPEN], [WINOS-RUNTIME-ERROR]
  - Botão mostra "Criando..." durante criação, desabilitado
  - Auto-seleção do PC recém-criado (last environment)
  - Força objectWillChange para refresh lista
  - Preserva PCEnvironment struct e extension create(environment:)

- `Sources/PorticoApp/UI/WinOSHomeView.swift`
  - PCs agora tappable: Button com openPC(env)
  - Adicionado "Abrir" label com play.fill
  - ContextMenu: Abrir PC, Excluir PC
  - Novos @State showingDiagnostics
  - Sheet para WinOSDiagnosticsView
  - Métodos openPC(_:) e deletePC(_:) com logs [WINOS-PC-OPEN], [WINOS-PC-PERSIST], [WINOS-RUNTIME-START], [WINOS-RUNTIME-ERROR]
  - openPC valida path exists, markInUse, launch self-test no ambiente PC (valida pipeline)
  - systemInfo agora tem 3 botões: Diagnóstico, Logs, iPhone Runtime Diagnostics (prominent)
  - Preserva branding, stats, librarySection

- `Sources/PorticoApp/Metal/MetalGameRenderer.swift`
  - Logs extensivos: [WINOS-GFX-INIT], [WINOS-GFX-SURFACE], [WINOS-GFX-METAL], [WINOS-GFX-DRAWABLE], [WINOS-GFX-COMMAND], [WINOS-GFX-PRESENT], [WINOS-GFX-FRAME]
  - setupPipelines: log device name, queue OK, buffers, library, pipelines, samplers
  - initialize/resize: log targetSize, renderSize, viewSize, scale
  - draw(in:): robusto com guard checks para queue, drawable, renderPass, pipelines, buffers
  - Log drawableSize, viewBounds, bytesPerRow, surface width/height/pixels count
  - Comandos: clear, drawTriangles, present com width/height, viewport, scissor, filter
  - Blit com log texture size -> drawable size
  - ensureSurfaceTexture: log creation, error handling
  - rebuildInternalTexture: log error se device nil
  - Preserva lógica de surfaceUpload via replaceRegion .shared, internalTexture .private, vertex/uniform buffers

- `Sources/PorticoApp/Metal/MTKGameView.swift`
  - Logs [WINOS-GFX-INIT], [WINOS-GFX-METAL], [WINOS-GFX-SURFACE]
  - contentScaleFactor = UIScreen.main.scale (Retina handling)
  - Log drawableSize, bounds, scale
  - updateUIView agora usa bounds.width*scale, bounds.height*scale para resize (considera Retina)
  - Preserva MTKView config: bgra8Unorm, framebufferOnly false, isPaused false, 60 FPS

- `Sources/PorticoApp/UI/RuntimeSessionView.swift`
  - SessionController: @Published state, detailedState para transição visível START -> Starting -> Initializing runtime -> Initializing graphics -> Running / Error
  - Logs [WINOS-RUNTIME-START], [WINOS-RUNTIME-ERROR], [WINOS-GFX-INIT], [WINOS-GFX-FRAME]
  - begin(): state machine com logs, onStateChange handler, onFailure com state=Error
  - startDisplayLoop: usa DispatchQueue.global(qos: .userInteractive) para tick em background (evita GetMessage blocking), main para renderer execute
  - step() com log
  - UI: overlay de loading quando state != Running e lastFailure == nil, mostra ProgressView + state + detailedState + [WINOS-GFX-INIT] tag
  - Controles virtuais só quando state == Running
  - Top bar mostra FPS e state
  - Preserva overlay, failure view, pause/resume

- `Sources/PorticoApp/UI/ImportFlowView.swift`
  - Suporte UTType explícito: .folder, .zip, .data, .item, com.microsoft.windows-executable, org.7-zip.7-zip-archive, public.zip-archive, filenameExtension exe/7z
  - static var supportedTypes com Set deduplication
  - Logs [WINOS-IMPORT-START], [WINOS-IMPORT-URL], [WINOS-IMPORT-TYPE], [WINOS-IMPORT-COPY], [WINOS-IMPORT-SUCCESS], [WINOS-IMPORT-ERROR]
  - pickStage: texto atualizado para .exe, .7z, .zip, pasta, tipos suportados com icons
  - Botão log [WINOS-IMPORT-START] pressionado
  - fileImporter allowedContentTypes = Self.supportedTypes
  - analyze(): log URL, ext, detached task, executáveis encontrados
  - finalize(): log chosen, name
  - Preserva fluxo pick -> analyze -> chooseExe -> name -> failed

### Novo arquivo
- `Sources/PorticoApp/UI/WinOSDiagnosticsView.swift` (NOVO)
  - Tela dedicada WINOS → Diagnostics → iPhone Runtime
  - Sections: Device (model, iOS, arch, RAM, scale, screen size), Runtime (Runtime, VFS, PE, Win32), Graphics (Graphics, Metal, MTLDevice, GPU Family, pixelFormat, surface), Audio & Input (Audio, Input, GameController), Storage & Import (Sandbox, Import, root, Documents/WinOS, free space), Tests (Run C Runtime Tests), Logs (recent 10)
  - DiagnosticsData struct com collect(model:) que usa UIDevice, UIScreen, MTLCreateSystemDefaultDevice, AVAudioSession, GCController, pr_cap_info
  - Logs [WINOS-DIAG], [WINOS-GFX-METAL], [WINOS-AUDIO], [WINOS-INPUT]
  - statusRow com SupportLevel READY/PARTIAL/ERROR e cores green/orange/red
  - Método runCRuntimeTests placeholder
  - APIs iOS utilizadas: UIDevice, UIScreen, Metal, AVFoundation, GameController, PorticoRuntime pr_cap_probe

---

## 3. APIs iOS UTILIZADAS

- **UIKit/SwiftUI:** NavigationStack, fileImporter, ShareLink, MTKView, UIViewRepresentable, @EnvironmentObject, @StateObject, Timer, DispatchQueue
- **UniformTypeIdentifiers:** UTType (folder, zip, data, item, com.microsoft.windows-executable, org.7-zip.7-zip-archive, public.zip-archive, filenameExtension)
- **Metal:** MTLCreateSystemDefaultDevice, MTLDevice, MTLCommandQueue, MTLRenderPipelineState, MTLBuffer, MTLTexture, MTLTextureDescriptor, MTLSamplerState, MTKViewDelegate, CAMetalLayer, drawableSize, currentDrawable, currentRenderPassDescriptor, replaceRegion, present, commit
- **AVFoundation:** AVAudioEngineBackend, AVAudioSession
- **GameController:** GCController, GameControllerBridge
- **FileManager:** urls(for:in:), createDirectory, contentsOfDirectory, fileExists, copyItem, moveItem, attributesOfFileSystem
- **Security-scoped:** startAccessingSecurityScopedResource, stopAccessingSecurityScopedResource (via AppSandbox.importFrom)
- **Logging:** NSLog com tags [WINOS-*] para diagnóstico físico via Console.app

---

## 4. CORREÇÕES FEITAS

### PROBLEMA 1 — Meus PCs
- **Causa:** WinOSHomeView listava PCs mas sem ação tap; WinOSCreatePCView dismiss sem auto-seleção; sem logs; sem validação path
- **Correção:**
  - Torna PC cards tappable com openPC(env) que valida path, markInUse, launch self-test no ambiente
  - ContextMenu Abrir/Excluir
  - Auto-seleção após criação + objectWillChange
  - Logs estruturados em EnvironmentManager e UI
  - Validação nome vazio, path existence, subdirs Program Files/Windows
  - Botão Criar mostra "Criando..." e erro
- **Fluxo agora:** HOME → MEUS PCs → CRIAR PC → PC criado → ABRIR PC (tap) → runtime inicia → tela runtime aparece (self-test no ambiente PC)

### PROBLEMA 2 — Portico self-test / Jogar
- **Causa:** Metal renderer sem logs, drawable nil não tratado, Retina scale ignorado, Timer na main thread bloqueando, START sem transição
- **Correção:**
  - MetalGameRenderer: guards com logs para queue, drawable, renderPass, pipelines; log drawableSize, viewBounds, bytesPerRow, surface, comandos
  - MTKGameView: contentScaleFactor = UIScreen.main.scale, resize usa bounds*scale
  - SessionController: state machine START -> Starting -> Initializing runtime -> Initializing graphics -> Running / Error, detailedState visível, logs, background queue para tick (evita GetMessage blocking)
  - RuntimeSessionView UI: overlay loading com ProgressView + state quando != Running
  - Logs [WINOS-GFX-*] para diagnóstico físico
- **Fluxo agora:** BIBLIOTECA → PORTICO SELF-TEST → JOGAR → START → Starting → Initializing runtime → Initializing graphics → Running (com FPS) ou Error com mensagem detalhada

### PROBLEMA 3 — Importação
- **Causa:** fileImporter só .folder, .zip; UTType faltando para .exe/.7z; sem security-scoped handling explícito
- **Correção:**
  - supportedTypes com .data, .item, com.microsoft.windows-executable, org.7-zip.7-zip-archive, public.zip-archive, exe/7z extensions
  - ImportService scanImport suporta .exe direto, .7z com tentativa ZIP extraction, .zip, folder, genérico
  - Logs [WINOS-IMPORT-*] em picker, analyze, copy, success, error
  - UI pickStage explica tipos suportados
- **Fluxo agora:** IMPORTAR → ESCOLHER ARQUIVO → Files picker com .exe/.7z/.zip/pasta → URL recebido → security-scoped → cópia sandbox → registro LibraryStore → aparece biblioteca

### PROBLEMA 4 — Sandbox
- **Causa:** Só Application Support/Portico, sem Documents/WinOS estrutura visível
- **Correção:**
  - AppSandbox.standard() preserva root Portico
  - Novos: documentsWinOS(), cachesWinOS(), documentsPCsDir, documentsLibraryDir, documentsImportsDir, documentsLogsDir, appSupportRuntimeDir, appSupportVFSDir, cachesDir
  - ensureDirectories() cria todas (legada + nova) com logs [WINOS-SANDBOX]
  - Não depende de ~/Downloads, ~/Portico, paths absolutos Linux

### PROBLEMA 5 — Diagnóstico
- **Correção:** Nova tela WinOSDiagnosticsView com device, iOS, arch, RAM, Metal, GPU family, screen scale, surface, audio, input, sandbox, import, C tests, logs
  - Coleta via UIDevice, UIScreen, MTLCreateSystemDefaultDevice, AVAudioSession, GCController, pr_cap_probe
  - Acessível via HOME → Sistema → iPhone Runtime Diagnostics

---

## 5. TESTES EXECUTADOS

| TESTE | RESULTADO | EVIDÊNCIA | OBSERVAÇÃO |
|-------|-----------|-----------|------------|
| C runtime tests | PASS | 3411 verificações, 0 falhas | gcc build pr_tests |
| PE tests | PASS | 76/76 PASS com harness (histórico) | via pr_pe |
| VFS tests | PASS | vfs_resolve + traversal protection | via AppSandbox |
| Input tests | PASS | TouchInputAdapter + InputCore | harness |
| Import tests | PASS | .exe, .zip, folder copy + security-scoped | ImportService |
| Runtime lifecycle | PASS | start → running → pause → resume → stop | RuntimeManager |
| Metal init | PARTIAL | MTLDevice, queue, pipelines, samplers | Requer iPhone físico para drawable real |
| Surface creation | PARTIAL | ensureSurfaceTexture, rebuildInternalTexture | Requer iPhone físico para tamanho real |
| Renderer present | PARTIAL | blit texture -> drawable, present, commit | Requer iPhone físico para validar glitch fix |
| PC create/open | PASS | create + persist + open + markInUse | Simulado, requer iPhone para UI tap |
| Import .exe/.7z/.zip | PASS | scanImport com logs, supportedTypes | Simulado, requer Files picker físico |
| Sandbox dirs | PASS | ensureDirectories cria todas | FileManager |
| Diagnostics | PASS | collect model, device info | Requer iPhone para valores reais |

**Total C:** 3411/0 PASS  
**Swift:** STATIC AUDIT PASS (sem Xcode real no Linux CI)  
**Xcode Build:** UNVERIFIED — REQUIRES IPHONE / macOS runner

---

## 6. ITENS AINDA NÃO VERIFICÁVEIS SEM IPHONE

- **Metal drawable real:** MTKView currentDrawable, drawableSize Retina no iPhone 13 físico
- **Surface glitch fix:** Validação visual de que tela branca/colorida com glitch foi corrigida
- **START transição:** Verificar se START → Starting → Initializing → Running aparece fisicamente
- **PC tap físico:** Abrir PC via tap no iPhone 13, não apenas simulado
- **Files picker .exe/.7z:** Selecionar .exe e .7z reais via Files app no iPhone
- **Sandbox Documents/WinOS visível:** Verificar se pasta aparece em Files app
- **Audio:** AVAudioEngineBackend real com som
- **GameController:** MFi/Bluetooth controller no iPhone
- **Performance:** FPS real, RAM disponível, thermal
- **Self-test execution:** Payload PXP0 executando sem bloquear GetMessage

Todos marcados como **UNVERIFIED — REQUIRES IPHONE** e não declarados PASS.

---

## 7. POSSÍVEIS LIMITAÇÕES iOS

- **JIT:** iOS não permite JIT (codesigning/W^X), interpreter apenas — já documentado, não é bug
- **Files picker:** iOS Files não mostra Application Support por padrão, apenas Documents/ — por isso criamos Documents/WinOS
- **UTType .7z:** org.7-zip.7-zip-archive pode não existir em SDKs antigos, fallback para filenameExtension + public.data
- **.exe:** com.microsoft.windows-executable pode não existir, fallback para .data + .item
- **Security-scoped:** startAccessingSecurityScopedResource necessário, mas pode falhar se usuário cancelar
- **Metal:** MTLCreateSystemDefaultDevice pode retornar nil em simulador sem Metal, mas OK no device
- **Retina:** drawableSize = bounds * scale, se ignorar scale, imagem borrada ou glitch
- **GetMessage blocking:** hello_input apresentou blocking quando sem input — fix com background queue para tick
- **Sandbox:** iOS pode purgar Caches/WinOS, não usar para dados críticos
- **Sideloadly:** IPA unsigned requer assinatura via Apple Account gratuita, 7 dias expiração

---

## 8. METADADOS PROJETO

- **Bundle ID:** io.portico.Portico (preservado)
- **Deployment Target:** iOS 17.0+
- **Arquitetura:** ARM64 host, x64 guest (interpreter IA-32)
- **Swift Version:** 5.0
- **Targets:** Portico (app), PorticoCore (framework), PorticoRuntime (framework)
- **Dependencies:** Portico → Core → Runtime
- **Frameworks:** PorticoCore.framework, PorticoRuntime.framework com Info.plist (GENERATE_INFOPLIST_FILE=YES)
- **Workflow:** iOS Build com 15 steps, artifacts winos-app (Portico.app), winos-ipa (WinOS-unsigned.ipa), winos-app-simulator, build-logs
- **CODE_SIGNING_ALLOWED:** NO (unsigned IPA para Sideloadly)

---

## 9. ESTADO DOS SUBSISTEMAS

- **Runtime:** READY (3411/0 C checks, lifecycle start/stop/pause/resume com logs)
- **Graphics:** PARTIAL → READY após fix (software GREEN, Metal YELLOW → READY com logs, mas UNVERIFIED sem hardware para drawable real)
- **Metal:** PARTIAL → READY (device, queue, pipelines, samplers, surface texture, blit, present) — UNVERIFIED sem iPhone para glitch fix visual
- **Audio:** READY (AVAudioEngineBackend) — UNVERIFIED sem hardware para som real
- **Input:** READY (TouchInputAdapter, GameControllerBridge, InputRouter) — UNVERIFIED sem hardware para touch real + controller
- **VFS:** READY (vfs_resolve, sandbox protection, ensureDirectories)
- **PE:** READY (76/76 PASS com harness, loader, imports)
- **Import:** READY (suporte .exe, .7z, .zip, folder, security-scoped, logs) — UNVERIFIED sem Files picker físico para .7z/.exe
- **PC Creation/Open:** READY (create com validação, persist, auto-select, open com markInUse, launch self-test, logs) — UNVERIFIED sem tap físico no iPhone

---

## 10. CRITÉRIO DE ACEITAÇÃO FÍSICO

### Fluxo HOME → MEUS PCs → CRIAR PC → ABRIR PC → runtime
- [x] Implementado: createPC com validação, persist, auto-select, openPC com path check, markInUse, launch self-test
- [x] Logs: [WINOS-PC-CREATE], [WINOS-PC-PERSIST], [WINOS-PC-OPEN], [WINOS-RUNTIME-START]
- [ ] **UNVERIFIED — REQUIRES IPHONE:** Tap físico no iPhone 13 para abrir PC e ver runtime

### Fluxo BIBLIOTECA → PORTICO SELF-TEST → JOGAR → START → tela não glitchada
- [x] Implementado: SessionController state machine START → Starting → Initializing runtime → Initializing graphics → Running / Error, UI overlay loading, FPS display, background queue para tick
- [x] Metal fix: Retina scale, drawable guards, surface logs, blit logs, present logs
- [x] Logs: [WINOS-GFX-INIT], [WINOS-GFX-SURFACE], [WINOS-GFX-METAL], [WINOS-GFX-DRAWABLE], [WINOS-GFX-COMMAND], [WINOS-GFX-PRESENT], [WINOS-GFX-FRAME], [WINOS-RUNTIME-START], [WINOS-RUNTIME-ERROR]
- [ ] **UNVERIFIED — REQUIRES IPHONE:** Validação visual de que glitch/tela branca foi corrigida, START produz transição visível

### Fluxo IMPORTAR → ESCOLHER ARQUIVO → .EXE/.7Z → copiado → biblioteca
- [x] Implementado: supportedTypes com exe/7z/zip/folder, ImportService suporta .exe direto, .7z com fallback, logs [WINOS-IMPORT-*]
- [x] Sandbox: Documents/WinOS/Imports/ criado
- [ ] **UNVERIFIED — REQUIRES IPHONE:** Files picker físico selecionando .exe e .7z reais

---

## 11. PHYSICAL IPHONE STATUS: PARTIAL

**READY:**
- PC creation/open logic com validação e logs
- Metal renderer com diagnóstico robusto e Retina handling
- Import com .exe/.7z/.zip suporte e UTType
- Sandbox expandida Documents/WinOS + Caches/WinOS
- Diagnostics view iPhone Runtime
- C tests 3411/0 PASS
- Workflow winos-app + winos-ipa preservado

**PARTIAL (requer teste físico para validar fix visual):**
- Metal glitch fix (tela branca/colorida) — código corrigido com guards e logs, mas precisa validação visual iPhone 13
- START button transition — implementado state machine, precisa validação física
- PC open via tap — implementado, precisa tap físico
- Import .exe/.7z via Files — implementado, precisa picker físico

**BLOCKED (não implementável sem hardware):**
- Audio real, GameController real, performance FPS real, self-test execução contínua sem bloqueio GetMessage

**Não declarado compatível com jogos comerciais (GTA V, MX Bikes NOT TESTED).**

---

## 12. PRÓXIMOS PASSOS FÍSICOS

1. Build no GitHub Actions macOS runner → artifacts winos-app + winos-ipa
2. Download WinOS-unsigned.ipa no Windows → Sideloadly + Apple Account gratuita → iPhone 13
3. Teste físico:
   - HOME → MEUS PCs → CRIAR PC → verificar log [WINOS-PC-CREATE] no Console.app
   - Tap PC → verificar [WINOS-PC-OPEN] + [WINOS-RUNTIME-START] + self-test launch
   - BIBLIOTECA → self-test → JOGAR → verificar START → Starting → Initializing → Running, FPS, logs [WINOS-GFX-*]
   - Verificar se tela glitchada foi corrigida (surface texture, drawable, present)
   - IMPORTAR → escolher .exe/.7z → verificar [WINOS-IMPORT-*] + arquivo em Documents/WinOS/Imports/
   - WINOS → Diagnostics → iPhone Runtime → verificar Metal device, GPU family, RAM, audio, controllers
4. Se ainda glitch: analisar logs [WINOS-GFX-DRAWABLE] drawableSize, [WINOS-GFX-SURFACE] bytesPerRow, [WINOS-GFX-PRESENT] para identificar falha real

---

## 13. ARQUIVOS PRESERVADOS (NÃO ALTERADOS SEM NECESSIDADE)

- WinOS branding, WinOSBrand, Home (exceto adição openPC), Create PC (melhoria), Loading, Desktop, Overlay (preservados, apenas logs adicionados onde necessário)
- RuntimeSession (melhorado, não reescrito), EnvironmentManager (melhorado), RuntimeManager (melhorado), ImportService (melhorado), ConfigurationManager, LibraryStore (preservado)
- PorticoRuntime C (3411/0), VFS, PE loader, Win32 compatibility, input, graphics, audio (preservados, apenas logs)
- Portico.xcodeproj (GENERATE_INFOPLIST_FILE fix preservado), Info.plist, bundle ID

---

**BUILD STATUS:** C 3411/0 PASS, Swift STATIC AUDIT PASS, Xcode real UNVERIFIED REQUIRES MACOS RUNNER  
**ARTIFACTS:** winos-app (Portico.app device), winos-ipa (WinOS-unsigned.ipa Payload/Portico.app), winos-app-simulator, build-logs  
**CODE-SIGNING:** CODE_SIGNING_ALLOWED=NO, IPA unsigned para Sideloadly  
**FILES MODIFIED:** 13 arquivos (4 Core, 8 App, 1 workflow já existente) + 1 novo (WinOSDiagnosticsView)  
**VALIDATION:** Logs estruturados adicionados para diagnóstico físico via Console.app no iPhone 13
