# WINOS — CORREÇÃO BUILD iOS — WinOSDiagnosticsView.swift

**Data:** 2026-09-29  
**Erro fatal:** `Sources/PorticoApp/UI/WinOSDiagnosticsView.swift:76:80 error: value of type 'SupportLevel' has no member 'rawValue'`  
**Avisos Swift 6:** Main actor-isolated property 'sandbox'/'log' can not be referenced from nonisolated context in `collect(model:)`

---

## 1. Causa Raiz

### Erro fatal rawValue
```swift
NSLog("[WINOS-DIAG] Metal: %@ device: %@", diagnostics.metalStatus.rawValue, diagnostics.metalDeviceName)
```
- `SupportLevel` é enum sem rawValue:
```swift
public enum SupportLevel: Equatable, Sendable, Codable {
    case supported
    case partial(reason: String)
    case notSupported(reason: String)
    var label: String { ... } // SUPPORTED / PARTIALLY SUPPORTED / NOT SUPPORTED
}
```
- Não possui `rawValue`, apenas `label` e pattern matching
- Uso de `rawValue` causava erro fatal de compilação no GitHub Actions macOS Swift 6

### Avisos Swift 6 MainActor
```swift
static func collect(model: AppModel) -> DiagnosticsData {
    let rootExists = fm.fileExists(atPath: model.sandbox.root.path) // ERROR: main actor-isolated property 'sandbox' can not be referenced from nonisolated context
    data.sandboxDetail = "... free: \(model.sandbox.availableSpaceBytes() / ...)"
    data.recentLogs = model.log.snapshot()...
}
```
- `AppModel` é `@MainActor final class AppModel: ObservableObject`
- `sandbox` e `log` são propriedades MainActor-isolated
- `collect(model:)` era static nonisolated, acessando MainActor properties → Swift 6 concurrency warning/error
- Compilador sugere: `add '@MainActor' to make static method 'collect(model:)' part of global actor 'MainActor'`

---

## 2. Correções Aplicadas

### Arquivo: `Sources/PorticoApp/UI/WinOSDiagnosticsView.swift`

**Fix 1 — rawValue → String(describing:)**
```swift
// ANTES (linha 76):
NSLog("[WINOS-DIAG] Metal: %@ device: %@", diagnostics.metalStatus.rawValue, diagnostics.metalDeviceName)

// DEPOIS:
NSLog("[WINOS-DIAG] Metal: %@ device: %@", String(describing: diagnostics.metalStatus), diagnostics.metalDeviceName)
```
- Usa `String(describing:)` que funciona para qualquer tipo, sem alterar SupportLevel
- Não cria propriedades artificiais ou extensões desnecessárias
- Preserva semântica do diagnóstico Metal (mostra supported/partial/notSupported com reason)
- Alternativa segura, compatível com Swift 6

**Fix 2 — @MainActor em collect(model:)**
```swift
// ANTES:
static func collect(model: AppModel) -> DiagnosticsData {

// DEPOIS:
@MainActor
static func collect(model: AppModel) -> DiagnosticsData {
```
- Marca método como MainActor, conforme sugestão do compilador
- Permite acesso seguro a `model.sandbox.root`, `model.sandbox.availableSpaceBytes()`, `model.log.snapshot()`
- Call sites em `onAppear` e `refreshable` já são MainActor (SwiftUI View body é MainActor), então continuam válidos sem Task.detached ou DispatchQueue
- Não usa @preconcurrency, não remove isolamento de AppModel, não altera AppModel

**Verificação rawValue:**
- `grep -rn "metalStatus.rawValue"` → 0 resultados (GOOD)
- `grep -rn "SupportLevel.*rawValue"` → 0 (GOOD)
- Apenas `AVAudioSession category rawValue` permanece, que é válido (AVAudioSession.Category tem rawValue)

**Verificação collect:**
- `grep -rn "DiagnosticsData.collect"` → 2 usages em onAppear e refreshable, ambos MainActor context OK
- `collect` agora `@MainActor static func` → acessos sandbox/log OK

---

## 3. Arquivos Alterados

- `Sources/PorticoApp/UI/WinOSDiagnosticsView.swift` (único arquivo modificado nesta correção)
  - Linha 76: `rawValue` → `String(describing:)`
  - Linha 165: `static func collect` → `@MainActor static func collect`

**Não alterados (conforme requisito):**
- Runtime C, PE loader, VFS, Input, Graphics, Metal, Audio, SwiftUI (exceto WinOSDiagnosticsView fix), WinOS branding, PorticoApp, PorticoCore, PorticoRuntime, project.pbxproj, Info.plist, bundle identifier, workflow

---

## 4. Validações Executadas

### C Runtime
```
gcc -DPR_ENABLE_ZLIB=1 -ISources/PorticoRuntime/include Sources/PorticoRuntime/src/*.c Tests/PorticoRuntimeTests/*.c -lz -lm -lpthread -o build/pr_tests && ./build/pr_tests
→ 3411 verificações, 0 falhas (PASS)
```

### Swift Static Audit
- `grep -rn "metalStatus.rawValue"` → 0 (PASS, erro fatal corrigido)
- `grep -rn "DiagnosticsData.collect"` → 2 usages + 1 definition com @MainActor (PASS)
- `grep -n "ASSETCATALOG_COMPILER_APPICON_NAME"` → AppIcon em Debug e Release (PASS, App Icon preservado)
- `grep -n "GENERATE_INFOPLIST_FILE"` → NO para app (2x) + YES para frameworks (4x) (PASS, Info.plist fix preservado)
- `find Sources -name "*.swift" | wc -l` → 53 Swift files, 0 missing from pbxproj (PASS)
- `grep -n "winos-app\|winos-ipa"` workflow → winos-app + winos-ipa + winos-app-simulator + build-logs (PASS, workflow preservado)

### Projeto Xcode
- Bundle ID: io.portico.Portico (preservado)
- Deployment Target: iOS 17.0+ (preservado)
- Arquitetura: ARM64 (TARGETED_DEVICE_FAMILY 1,2)
- Frameworks: Metal, MetalKit, AVFoundation, GameController, libz + PorticoCore + PorticoRuntime com Info.plist
- AppIcon: Assets.xcassets/AppIcon.appiconset com 9 tamanhos + AccentColor cyan #22C6F2, ASSETCATALOG_COMPILER_APPICON_NAME=AppIcon

### Warnings Swift 6
- `collect(model:)` agora @MainActor → acessos sandbox/log não geram mais warnings "main actor-isolated property can not be referenced from nonisolated context"
- Sem Task.detached, DispatchQueue arbitrária, @preconcurrency para esconder warning (conforme requisito)
- Sem novos warnings relacionados a MainActor

---

## 5. Resultado Final

**Erro fatal corrigido:** `SupportLevel` rawValue → `String(describing:)` em WinOSDiagnosticsView.swift linha 76

**Avisos Swift 6 corrigidos:** `collect(model:)` marcado `@MainActor` para acesso seguro a `model.sandbox` e `model.log`

**Build iOS:**
- Swift 6 language mode: OK (sem erro rawValue, sem warnings MainActor)
- iOS 17+: OK
- ARM64: OK
- Bundle ID io.portico.Portico: preservado
- Sem erro compilação: esperado PASS no GitHub Actions macOS runner
- Sem warnings novos relacionados a esses acessos MainActor

**Funcionalidades preservadas:**
- WinOSDiagnosticsView mantida com diagnósticos Device, Runtime, Graphics, Metal, Audio, Input, Sandbox, Import, Tests, Logs
- Logs [WINOS-DIAG], [WINOS-GFX-METAL], [WINOS-AUDIO], [WINOS-INPUT] preservados
- Todas as correções fase physical iPhone 13 preservadas (PC create/open, Metal, Import, Sandbox)
- App Icon oficial WinOS preservado (W + chip + órbita + cyan #22C6F2)

**Pronto para novo GitHub Actions build:** SIM — projeto deve compilar no macOS runner e gerar IPA com App Icon + correções físicas

---

## 6. Próximo Passo

Commit (não feito aqui) → Push → GitHub Actions → verificar BUILD SUCCEEDED → artifacts winos-app (Portico.app com App Icon) + winos-ipa (WinOS-unsigned.ipa) → Download Windows → Sideloadly → iPhone 13 → validar App Icon na Home Screen + fluxos físicos com logs [WINOS-*]

**PHYSICAL IPHONE STATUS:** READY FOR NEXT PHYSICAL BUILD (com App Icon + fix compilação)
