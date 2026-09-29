# WINOS — Correção do Módulo C PorticoRuntime no Xcode

**Data:** 2026-09-28  
**Erro raiz:** `umbrella header 'PorticoRuntime.h' not found` em `Sources/PorticoRuntime/include/module.modulemap:2:21` → `could not build module 'PorticoRuntime'` → `unable to resolve module dependency: 'PorticoRuntime'` em 8 arquivos PorticoCore

---

## 1. Onde Estava o Problema Real

**Estrutura encontrada:**
```
Sources/PorticoRuntime/
├── include/
│   ├── PorticoRuntime.h (811 bytes, existe, capitalização correta)
│   ├── module.modulemap (114 bytes, framework module PorticoRuntime { umbrella header "PorticoRuntime.h" })
│   └── portico/ (20 headers públicos pr_*.h)
└── src/ (20 arquivos C)
```

**Problema:** Manual `module.modulemap` em `Sources/PorticoRuntime/include/` com `framework module PorticoRuntime { umbrella header "PorticoRuntime.h" }`

**Por que falha no Xcode:**
- Quando existe arquivo nomeado `module.modulemap` em diretório que está em `HEADER_SEARCH_PATHS` (`$(SRCROOT)/Sources/PorticoRuntime/include`), clang automaticamente o descobre durante dependency scanning
- Xcode com `DEFINES_MODULE=YES` gera seu próprio module.modulemap em DerivedData a partir dos headers públicos da fase Headers
- Ter manual `module.modulemap` no source causa conflito: clang tenta usar o manual durante scanning, mas o umbrella header "PorticoRuntime.h" não é encontrado no contexto de scanning porque Xcode ainda não copiou headers para framework bundle
- Erro `umbrella header 'PorticoRuntime.h' not found` ocorre durante `clang dependency scanning`, antes mesmo de compilar Swift

**Evidência:** Caminho do erro `/Users/runner/work/winos/winos/Sources/PorticoRuntime/include/module.modulemap:2:21` — é o arquivo source, não o gerado em DerivedData. Isso indica que clang está escaneando o manual, não o gerado.

**Histórico:** Conforme prompt mestre, já houve problema anterior com modulemap manual e projeto foi corrigido para não depender de modulemap manual. O arquivo manual foi reintroduzido, causando regressão.

---

## 2. PorticoRuntime.h Existia ou Não

**Existia:** Sim, `Sources/PorticoRuntime/include/PorticoRuntime.h` existia com 811 bytes, capitalização exata correta, com `#import <Foundation/Foundation.h>` e 20 includes `#include "portico/pr_*.h"`

**Caminho final:** `Sources/PorticoRuntime/include/PorticoRuntime.h` (inalterado, preservado)

**Conteúdo (umbrella header legítimo):**
```c
#ifndef PORTICO_RUNTIME_H
#define PORTICO_RUNTIME_H
#import <Foundation/Foundation.h>
#include "portico/pr_types.h"
#include "portico/pr_log.h"
#include "portico/pr_vm.h"
#include "portico/pr_cap.h"
#include "portico/pr_asm.h"
#include "portico/pr_cpu.h"
#include "portico/pr_cpu64.h"
#include "portico/pr_unwind.h"
#include "portico/pr_pe.h"
#include "portico/pr_peproc.h"
#include "portico/pr_win32.h"
#include "portico/pr_surf.h"
#include "portico/pr_gfx.h"
#include "portico/pr_gl.h"
#include "portico/pr_input.h"
#include "portico/pr_audio.h"
#include "portico/pr_host.h"
#include "portico/pr_zip.h"
#include "portico/pr_winhello.h"
#endif
```

Todos headers referenciados existem em `portico/` subdirectory.

---

## 3. Conteúdo/Estrutura Final do module.modulemap

**Antes:**
```
framework module PorticoRuntime {
    umbrella header "PorticoRuntime.h"
    export *
    module * { export * }
}
```
Localizado em `Sources/PorticoRuntime/include/module.modulemap` — referenciado no pbxproj como file, mas NÃO em Headers phase

**Depois:** Arquivo **REMOVIDO** completamente do filesystem e do `project.pbxproj`

**Justificativa:** Conforme regra do prompt mestre: "NÃO reintroduza module.modulemap nem MODULEMAP_FILE a menos que evidência concreta" e "Se encontrar qualquer modulemap manual reintroduzido, avalie e remova se estiver causando conflito". O manual estava causando conflito direto (erro de dependency scanning), então remoção é correção estrutural apropriada, não workaround.

**Após remoção:** Xcode com `DEFINES_MODULE=YES` gera automaticamente module.modulemap em DerivedData que referencia corretamente o umbrella header da fase Headers.

---

## 4. Headers Públicos Incluídos

**Umbrella header:** `PorticoRuntime.h` inclui 20 headers públicos:
- pr_types.h, pr_log.h, pr_vm.h, pr_cap.h, pr_asm.h, pr_cpu.h, pr_cpu64.h, pr_unwind.h, pr_pe.h, pr_peproc.h, pr_win32.h, pr_surf.h, pr_gfx.h, pr_gl.h, pr_input.h, pr_audio.h, pr_host.h, pr_zip.h, pr_winhello.h

**Headers phase no Xcode:** 20 arquivos em `PBXHeadersBuildPhase`:
- pr_asm.h, pr_audio.h, pr_cap.h, pr_cpu.h, pr_cpu64.h, pr_gfx.h, pr_gl.h, pr_host.h, pr_input.h, pr_log.h, pr_pe.h, pr_peproc.h, pr_surf.h, pr_types.h, pr_unwind.h, pr_vm.h, pr_win32.h, pr_winhello.h, pr_zip.h, PorticoRuntime.h

Todos marcados como públicos (default para Headers phase).

---

## 5. Alterações no Xcode Project

**Arquivo:** `Portico.xcodeproj/project.pbxproj`

**Removido:**
- File reference `D5CAFA3FA9D843DA8D3EAACD /* module.modulemap */` (linha 235)
- Child `D5CAFA3FA9D843DA8D3EAACD /* module.modulemap */` do grupo `include` (linha 515)

**Preservado:**
- `DEFINES_MODULE=YES` para todos targets (Portico, PorticoCore, PorticoRuntime)
- `CLANG_ENABLE_MODULES=YES`
- `HEADER_SEARCH_PATHS = $(SRCROOT)/Sources/PorticoRuntime/include` para todos targets
- `PRODUCT_MODULE_NAME` correto (PorticoRuntime, PorticoCore, Portico)
- Headers phase com 20 headers públicos
- Dependências: Portico → PorticoCore → PorticoRuntime
- Nenhum `MODULEMAP_FILE` setting (correto, deixa Xcode gerar)
- Nenhum caminho absoluto runner

**Verificação:** `grep -n module.modulemap project.pbxproj` → 0 ocorrências após correção — GOOD

---

## 6. Alterações no .gitignore

**Nenhuma alteração.** `.gitignore` atual:
```
build/
.build/
*.o
*.a
DerivedData/
xcuserdata/
*.xcuserstate
.DS_Store
__pycache__/
pr_*.plist
pr_*.o
analyzer*.err
build_setup.log
```

Não ignora `PorticoRuntime.h` nem `module.modulemap`. `PorticoRuntime.h` está versionado (quando git repo existe). `module.modulemap` removido não precisa ser ignorado.

---

## 7. Arquivos Modificados

- `Sources/PorticoRuntime/include/module.modulemap` — **DELETADO** (filesystem)
- `Portico.xcodeproj/project.pbxproj` — removidas 2 referências ao module.modulemap
- `Sources/PorticoRuntime/include/PorticoRuntime.h` — preservado, sem alteração
- Todos outros arquivos Swift/C preservados (correções anteriores de Resolution Hashable, ImportFlowView type-check/async, AVAudioEngineBackend, GameControllerBridge, MetalGameRenderer, ControlEditorView, RuntimeSessionView, WinOSHomeView, WinOSLoadingView, pthread Darwin fixes)

**Total:** 1 arquivo deletado, 1 arquivo pbxproj editado

---

## 8. Resultado dos Testes C

```
gcc -Wall -Wextra -Werror=implicit-function-declaration -std=c11 -O2 -g -DPR_ENABLE_ZLIB=1 -ISources/PorticoRuntime/include Sources/PorticoRuntime/src/*.c Tests/PorticoRuntimeTests/*.c -lz -lm -lpthread -o build/pr_tests
./build/pr_tests
→ 3411 verificações, 0 falhas
```

**Critério:** 3411 checks, 0 failures — PASS, sem redução de cobertura, sem remoção de testes, sem remoção de código C.

---

## 9. Resultado do Build Xcode ARM64

- **Ambiente Arena:** Linux, sem Xcode, clang não disponível, swiftc não disponível — **BUILD NÃO EXECUTADO** (UNAVAILABLE)
- **Validação estática:**
  - C: 3411/0 PASS
  - Swift: correções anteriores (Resolution Hashable, ImportFlowView, async/await, AVAudioEngineBackend UInt→Int, GCController.controllers(), MetalGameRenderer try, ControlEditorView ToolbarItem + Binding) — STATIC AUDIT PASS
  - Módulo: sem module.modulemap manual, sem referências, PorticoRuntime.h existe, Headers phase OK, DEFINES_MODULE=YES, HEADER_SEARCH_PATHS OK — STATIC AUDIT PASS
- **GitHub Actions esperado:** Após push, `xcodebuild -project Portico.xcodeproj -scheme Portico -sdk iphoneos -destination generic/platform=iOS CODE_SIGNING_ALLOWED=NO clean build` deve avançar além de `umbrella header 'PorticoRuntime.h' not found` e `could not build module 'PorticoRuntime'`
- **BUILD SUCCEEDED:** Não declarado sem Xcode real, conforme regra anti-falso sucesso

---

## 10. Quantidade de Erros/Warnings Restantes

- **Erros C:** 0
- **Warnings C:** 0 com `-Wall -Wextra -Werror=implicit-function-declaration`
- **Erros Swift (estático):** 0 dos 4 bloqueios anteriores + 0 dos 3 bloqueios + 0 do módulo (após remoção modulemap)
- **Warnings Swift:** Pendente Xcode real
- **Erros módulo:** `umbrella header not found` deve estar eliminado após remoção manual modulemap; próximo bloqueio real só aparece no próximo Run Workflow

---

## 11. Import PorticoRuntime Resolvido?

- **Antes:** `fatal error: could not build module 'PorticoRuntime'` → `unable to resolve module dependency: 'PorticoRuntime'` em 8 arquivos PorticoCore (DiagnosticsReport, PEInspector, PELoader, ZipReader, GameControllerAdapter, TouchInputAdapter, ExecutionBackend, Win32Layer)
- **Depois (esperado):** Com manual modulemap removido, Xcode gera módulo automaticamente a partir de PorticoRuntime.h público, `import PorticoRuntime` deve ser resolvido corretamente em PorticoCore
- **Validação estática:** PorticoRuntime.h existe, inclui todos headers públicos, Headers phase OK, DEFINES_MODULE=YES, HEADER_SEARCH_PATHS inclui include dir — cadeia válida:
```
PorticoRuntime.h (umbrella)
    ↓
headers públicos C (portico/pr_*.h)
    ↓
Xcode gera module.modulemap em DerivedData
    ↓
PorticoRuntime module
    ↓
PorticoCore (import PorticoRuntime)
    ↓
Swift
```
- **Confirmação real:** Requer GitHub Actions macOS runner

---

## 12. Próximo Bloqueio (caso ainda exista)

Após eliminar `umbrella header not found`, próximos possíveis bloqueios (apenas Xcode real pode confirmar):

- **Swift 6 concurrency:** Outros `async` sem `await`, MainActor isolation em Views que atualizam @State desde background
- **Picker Hashable:** `FPSLimit`, `RendererChoice`, `EnvironmentRef` já corrigidos com Hashable, mas outros enums usados em Picker podem precisar Hashable explícito
- **Metal/Audio/GameController:** APIs que mudaram de propriedade para método (como `GCController.controllers()` já corrigido) ou que exigem `try`
- **Linker:** Símbolos C não exportados, libz, etc.
- **Target membership:** Arquivos Swift não incluídos no target Portico

**Próximo passo:** Commit/push da remoção modulemap + correções Swift anteriores + executar GitHub Actions → ler logs completos → corrigir próximo erro real se surgir.

---

## 13. Checklist Conclusão

- [x] PorticoRuntime.h localizado — existe em Sources/PorticoRuntime/include/PorticoRuntime.h
- [x] Header versionado no Git (quando repo existe, .gitignore não ignora)
- [x] module.modulemap apontava para header correto mas causava conflito — removido
- [x] Sem problemas case-sensitive (PorticoRuntime.h capitalização exata)
- [x] Módulo PorticoRuntime deve ser construído pelo Clang/Xcode após remoção manual (geração automática)
- [x] import PorticoRuntime deve ser resolvido por PorticoCore após correção (esperado)
- [x] Arquivos Swift que dependem do módulo devem compilar (esperado, STATIC AUDIT PASS)
- [x] Runtime C continua funcional — 3411/0
- [x] Testes C continuam passando — sem redução cobertura
- [x] Sem mocks/stubs
- [x] Sem remoção de imports para esconder problema
- [x] Sem caminhos absolutos runner
- [x] GitHub Actions deve avançar além do erro de dependency scanning (esperado)
- [ ] BUILD SUCCEEDED — pendente Xcode real no runner

---

## 14. Preservação

- pthread Apple compatibility preservado (pr_darwin_*)
- POSIX/Darwin guards preservados
- PorticoRuntime C preservado (sem reescrita, sem remoção)
- PorticoCore preservado
- WinOS UI preservada (Home, CreatePC, Loading, Desktop, Overlay, RuntimeSession)
- Metal, Audio, GameController preservados
- Correções anteriores (Resolution Hashable, ImportFlowView type-check/async, AVAudioEngineBackend, GameControllerBridge, MetalGameRenderer, ControlEditorView, RuntimeSessionView, WinOSHomeView, WinOSLoadingView) preservadas
- Nenhuma funcionalidade removida para fazer build passar
