# WINOS — Auditoria Pré-Build Xcode / GitHub Actions

**Data:** 2026-09-28  
**Objetivo:** Eliminar antecipadamente erros Swift/iOS antes do próximo Run Workflow  
**Runner observado:** macOS, Xcode 26.6, iPhoneOS SDK 26.5, arm64-apple-ios17.0, Swift 5, deployment iOS 17.0

---

## 1. Contexto Última Compilação

- Runner macOS funcionando, Xcode e SDK encontrados, ARM64 processada
- pthread Darwin já ultrapassado (pr_darwin_* implementados)
- Módulo PorticoRuntime já ultrapassado (umbrella header + modulemap)
- Compilação chegou aos arquivos Swift do target Portico
- Erros reais em `RuntimeSessionView.swift` e `WinOSHomeView.swift` + inconsistência `RuntimeEvents`

---

## 2. Problemas Encontrados

### 2.1 RuntimeSessionView.swift
**Arquivo:** `Sources/PorticoApp/UI/RuntimeSessionView.swift`
**Erros reportados:**
- `model.effectiveConfig(for: game)` → `model is AppModel?`
- `model.runtime.events.onFPS`, `onFailure`, `onAudioFrames`, `start`, `environments`, `config.effectiveFPS`, `resume`, `pause`
- `RuntimeEvents has no member 'onAudioFrames'`

**Causa raiz:**
- `AppModel.shared` é `static var shared: AppModel!` (IUO) → tratado como opcional em `SessionController`
- `RuntimeEvents` real (definido em `Sources/PorticoCore/Runtime/RuntimeModels.swift`) possui: `onStateChange`, `onFailure`, `onLog`, `onFPS`, `onFinished` — **NÃO possui `onAudioFrames`**
- `RuntimeManager` (em `RuntimeManager.swift`) possui `onAudioFrames: (([Float]) -> Void)?` diretamente, não em `events`
- `begin()` usava `let model = AppModel.shared` sem guard, podendo ser nil
- `togglePause()`, `end()` usavam `AppModel.shared` sem guard, risco de crash

**Arquivos afetados:**
- `RuntimeSessionView.swift`
- `RuntimeManager.swift` (definição correta)
- `RuntimeModels.swift` (definição correta de RuntimeEvents)

### 2.2 WinOSHomeView.swift
**Arquivo:** `Sources/PorticoApp/UI/WinOSHomeView.swift`
**Erros reportados:**
- `ForEach(model.environments.environments) { env in` → `cannot convert [EnvironmentProfile] to Binding<C>`
- `WinOSStatusBadge(text: env.arquitetura, ...)` → `Binding<Subject> cannot be converted to String`

**Causa raiz:**
- `EnvironmentProfile` (em `EnvironmentModels.swift`) possui: `id`, `nome`, `dataCriacao`, `runtimeVersion`, `variables`, `state`, `caminho` — **NÃO possui `arquitetura`, `ramMB`, `backend`**
- `PCEnvironment` (em `WinOSCreatePCView.swift`) possui `arquitetura`, `ramMB`, `backend` e mapeia para `EnvironmentProfile.variables` via `WINOS_ARCH`, `WINOS_RAM_MB`, `WINOS_BACKEND`
- `WinOSHomeView` tentava acessar propriedades inexistentes diretamente, causando inferência de Binding via dynamic member lookup e falha no ForEach
- ForEach correto é sobre `[EnvironmentProfile]` que é Identifiable, mas acesso a campos inexistentes quebrava compilação

**Arquivos afetados:**
- `WinOSHomeView.swift`
- `EnvironmentModels.swift` (definição real)
- `WinOSCreatePCView.swift` (definição PCEnvironment + extension EnvironmentManager)

### 2.3 WinOSLoadingView.swift
**Arquivo:** `Sources/PorticoApp/UI/WinOSLoadingView.swift`
**Problema:** Usa `GameProfile` em `WinOSRuntimeLoadingView` mas não importava `PorticoCore`
**Causa:** Import ausente, causaria `cannot find type GameProfile in scope`
**Correção necessária:** Adicionar `import PorticoCore`

### 2.4 AppModel Optional Handling
**Arquivo:** `Sources/PorticoApp/AppModel.swift` + `RuntimeSessionView.swift`
**Problema:** `shared` é IUO, usado como opcional em alguns lugares, não opcional em outros — inconsistência
**Categoria:** B — objeto obrigatório tratado como opcional em SessionController
**Solução arquitetural:** Usar `guard let model = AppModel.shared else { return }` consistentemente, e `AppModel.shared?` onde apropriado

### 2.5 C/C++ Apple Compatibility (já corrigido, verificado preservação)
**Arquivos:** `Sources/PorticoRuntime/src/pr_win32.c`, `pr_cap.c`, demais `src/*.c`
**Problemas anteriores:** `pthread_mutex_timedlock`, `pthread_timedjoin_np` undeclared no iOS SDK
**Status atual:** Corrigido com `pr_darwin_*` fallback, guards `__APPLE__`, `__has_include`, `_POSIX_C_SOURCE` + `_DARWIN_C_SOURCE`
**Verificação:** `grep` não encontrou outros `_np` ou Linux-only APIs; `gcc -D__APPLE__ -D__MACH__ -Wall -Wextra -Werror=implicit` compila 20 arquivos

### 2.6 ModuleMap
**Arquivo:** `Sources/PorticoRuntime/include/module.modulemap`
**Status:** Existe, referenciado no pbxproj como file, mas sem `MODULEMAP_FILE` build setting — Xcode gera módulo via `DEFINES_MODULE=YES` + umbrella header. Não causa conflito atualmente, mantido.

### 2.7 Workflow
**Arquivo:** `.github/workflows/ios-build.yml`
**Verificação:** 5 etapas (Runtime, Core, App device, App simulator, Archive), `CODE_SIGNING_ALLOWED=NO`, destinations `generic/platform=iOS` e `generic/platform=iOS Simulator`, sem `continue-on-error`, sem mascarar falhas (apenas archive tem `|| echo` para permitir falha de signing). Correto.

### 2.8 Scheme
**Arquivo:** `Portico.xcodeproj/xcshareddata/xcschemes/Portico.xcscheme`
**Verificação:** BuildAction inclui Portico.app (running), PorticoCore e PorticoRuntime (testing/analyzing), dependências implícitas corretas, LaunchAction Portico.app, buildConfiguration Debug/Release.

---

## 3. Dependências Verificadas

### AppModel
- Definição: `Sources/PorticoApp/AppModel.swift`, `@MainActor final class AppModel: ObservableObject`, `static var shared: AppModel!`
- Criação: `@StateObject private var model = AppModel()` em `PorticoApp.swift`, injetado via `.environmentObject(model)`
- Consumo: Todas Views usam `@EnvironmentObject var model: AppModel` (non-optional) — padrão correto já existente
- SessionController (não-View) usa `AppModel.shared` — precisa guard let

### RuntimeEvents
- Definição real: `Sources/PorticoCore/Runtime/RuntimeModels.swift`, struct com `onStateChange`, `onFailure`, `onLog`, `onFPS`, `onFinished`
- Produzido por: `RuntimeManager.swift` via `events.onFPS?`, `onFailure?`, etc.
- Consumido por: `RuntimeSessionView.swift` (corrigido para `onAudioFrames` direto no RuntimeManager)
- Tipo de cada callback: `((Double) -> Void)?`, `((RuntimeFailure) -> Void)?`, etc.
- Áudio: `RuntimeManager.onAudioFrames: (([Float]) -> Void)?` + `AVAudioEngineBackend.enqueue(interleaved:)`

### EnvironmentProfile
- Definição real: `EnvironmentModels.swift`, `id: UUID`, `nome: String`, `dataCriacao: Date`, `runtimeVersion: String`, `variables: [String: String]`, `state: EnvironmentState`, `caminho: String`
- Campos usados por WinOSHomeView: `nome` existe, `arquitetura`/`ramMB`/`backend` NÃO existem — devem vir de `variables["WINOS_..."]`
- EnvironmentManager: `environments: [EnvironmentProfile]`, `create(name:)`, `create(environment: PCEnvironment)` extension que persiste extras como variables

### GameProfile
- Definição: `GameModels.swift`, `nome`, `caminho`, `executavel`, `resolucao`, `fps`, `renderer`, `audio`, `controles`, `ambiente: EnvironmentRef`, `opcoes`, etc.
- Usado em: WinOSHomeView, WinOSCreatePCView, RuntimeSessionView, etc. — import PorticoCore necessário

### ForEach / Binding
- Todos ForEach auditados: 19 ocorrências
- Problema apenas em WinOSHomeView devido a campos inexistentes
- Padrão correto SwiftUI: `ForEach(collection, id: \.id) { element in }` quando Identifiable, ou `ForEach(collection) { element in }` se já Identifiable
- `$model` usado corretamente apenas para `Binding` de `showingAlert` e `sessionToRun` (fullScreenCover item)

### Optional Handling
- Categoria A (legítimo): `sessionToRun: GameProfile?`, `gameToDelete: GameProfile?`, `selectedGame: GameProfile?`, `AppModel.shared: AppModel!` (IUO para conveniência)
- Categoria B (obrigatório tratado como opcional): `SessionController` usando `AppModel.shared` sem guard — corrigido
- Categoria C (force unwrap seguro): `AppModel.shared = self` em init, `AppModel.shared!` implícito após bootstrap — justificável
- Categoria D (perigoso): nenhum novo adicionado

### Imports
- PorticoApp: `import SwiftUI` + `import PorticoCore` (e `PorticoRuntime` quando usa C API)
- PorticoCore: `import Foundation` + `import PorticoRuntime` quando usa C
- Verificado: WinOSLoadingView faltava `import PorticoCore` — corrigido
- Nenhum import AppKit/Cocoa encontrado (macOS-only)

### Targets
- Portico: Sources (27 Swift + Shaders.metal), Frameworks (PorticoCore, Metal, MetalKit, AVFoundation, GameController, libz), Embed Frameworks (PorticoCore, PorticoRuntime)
- PorticoCore: Sources (27 Swift), Frameworks (PorticoRuntime)
- PorticoRuntime: Headers (20), Sources (20 C), Frameworks (libz)
- PRODUCT_MODULE_NAME, DEFINES_MODULE, TARGETED_DEVICE_FAMILY, IPHONEOS_DEPLOYMENT_TARGET, SWIFT_VERSION corretos

---

## 4. Riscos Restantes

- **Xcode real:** Sem acesso a macOS runner no Arena, validação final depende de GitHub Actions. STATIC AUDIT PASS, não XCODE BUILD PASS.
- **SwiftUI inference:** ForEach com Identifiable pode ainda ter ambiguidade se EnvironmentProfile não for visível como Identifiable em algum arquivo — mitigado com `id: \.id` explícito.
- **AppModel.shared:** IUO pode causar crash se acessado antes de init — mitigado com guard let.
- **Áudio:** `onAudioFrames` agora correto, mas integração AVAudioEngine ainda UNVERIFIED sem hardware.
- **Metal:** MTKGameView, MetalGameRenderer, Shaders.metal não validados sem device.

---

## 5. Checklist Auditoria

- [x] RuntimeSessionView auditada — onAudioFrames corrigido, AppModel.shared guard
- [x] AppModel auditado — shared IUO, @StateObject/@EnvironmentObject padrão
- [x] RuntimeEvents auditado — 5 callbacks reais, onAudioFrames é do RuntimeManager
- [x] onAudioFrames investigado — existe em RuntimeManager, não em RuntimeEvents
- [x] EnvironmentProfile auditado — 7 campos reais, arquitetura/ram/backend em variables
- [x] ForEach auditado — 19 ocorrências, apenas WinOSHomeView com problema
- [x] Binding auditado — $model usado corretamente para alert e sessionToRun
- [x] GameProfile verificado — import PorticoCore necessário, adicionado em WinOSLoadingView
- [x] PorticoCore import verificado — todos arquivos que usam GameProfile/EnvironmentProfile importam
- [x] PorticoRuntime import verificado — AppModel e backends importam quando necessário
- [x] Optional handling auditado — categorias A/B/C/D classificadas
- [x] SwiftUI property wrappers auditados — @StateObject, @EnvironmentObject, @ObservedObject, @State, @Binding corretos
- [x] Imports auditados — nenhum AppKit, nenhum import faltante exceto WinOSLoadingView corrigido
- [x] Portico target auditado — Sources, Frameworks, Embed, Headers OK
- [x] PorticoCore target auditado — Sources, Frameworks OK
- [x] PorticoRuntime target auditado — Headers, Sources, Frameworks OK
- [x] Scheme auditado — BuildAction com 3 targets, dependências implícitas
- [x] Workflow auditado — 5 etapas, CODE_SIGNING_ALLOWED=NO, sem mascarar falhas
- [x] Modulemap auditado — existe mas sem MODULEMAP_FILE, não causa conflito
- [x] pthread Apple fixes preservados — pr_darwin_* com guards
- [x] POSIX Apple fixes preservados — _POSIX_C_SOURCE + _DARWIN_C_SOURCE
- [x] C/C++ Apple compatibility auditada — nenhum outro _np ou Linux-only
- [x] Referências quebradas procuradas — model.runtime, config, environments, launch, effectiveConfig verificados
- [x] APIs inexistentes procuradas — arquitetura, ramMB, backend em EnvironmentProfile não existem
- [x] Nenhuma funcionalidade removida
- [x] Nenhum erro mascarado
- [x] Nenhum force unwrap adicionado sem justificativa
- [x] Nenhuma API falsa criada
- [x] Relatório criado
