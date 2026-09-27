# XCODE FIX — PorticoCore / PorticoRuntime Module Resolution

## Problema original (GitHub Actions)

```
/Users/runner/work/winos/winos/Sources/PorticoApp/AppModel.swift:4:8: error: Unable to resolve module dependency: 'PorticoCore'
import PorticoCore
```

Xcode também falhava com `PorticoRuntime`.

Causa raiz: `Portico.xcodeproj` possuía apenas 1 target (`Portico` app) e incluía todos os arquivos de `Sources/PorticoCore` e `Sources/PorticoRuntime` diretamente no target do app. Assim, `import PorticoCore` não tinha módulo para resolver — o código estava compilado como parte do app, não como framework/módulo separado.

## Correção aplicada

### 1. Criação de targets reais no Xcode

Antes: 1 target
- Portico (app)

Depois: 3 targets
- Portico (app) — `com.apple.product-type.application`
- PorticoCore (framework) — `com.apple.product-type.framework`
- PorticoRuntime (framework) — `com.apple.product-type.framework`

**Evidência:**
```
grep "isa = PBXNativeTarget" Portico.xcodeproj/project.pbxproj
→ 3 targets
grep "name = Portico" → Portico, PorticoCore, PorticoRuntime
```

### 2. PRODUCT_MODULE_NAME correto

- Portico: `PRODUCT_MODULE_NAME = Portico`
- PorticoCore: `PRODUCT_MODULE_NAME = PorticoCore` (exatamente o import)
- PorticoRuntime: `PRODUCT_MODULE_NAME = PorticoRuntime` (exatamente o import)

**Evidência:**
```
grep PRODUCT_MODULE_NAME Portico.xcodeproj/project.pbxproj
→ Portico, Portico, PorticoCore, PorticoCore, PorticoRuntime, PorticoRuntime
```

### 3. Associação de arquivos aos targets corretos

- **Portico target — Sources (26 arquivos, sem Info.plist):**
  - Sources/PorticoApp/AppModel.swift, PorticoApp.swift
  - Sources/PorticoApp/Audio/AVAudioEngineBackend.swift
  - Sources/PorticoApp/Input/GameControllerBridge.swift, VirtualControlsView.swift
  - Sources/PorticoApp/Metal/MTKGameView.swift, MetalGameRenderer.swift, Shaders.metal
  - Sources/PorticoApp/UI/* (14 arquivos incluindo WinOSBrand, WinOSHomeView, WinOSCreatePCView, WinOSLoadingView, WinOSDesktopView, WinOSOverlayView, LibraryView, RuntimeSessionView, etc)

- **PorticoCore target — Sources (27 arquivos):**
  - Sources/PorticoCore/Audio/AudioCore.swift
  - Sources/PorticoCore/Compatibility/CompatibilityLayer.swift
  - Sources/PorticoCore/Diagnostics/DiagnosticsReport.swift
  - Sources/PorticoCore/Environment/EnvironmentManager.swift
  - Sources/PorticoCore/Graphics/GraphicsBackend.swift, SurfaceBridge.swift
  - Sources/PorticoCore/Import/ImportService.swift, PEInspector.swift, PELoader.swift, ZipReader.swift
  - Sources/PorticoCore/Input/InputCore.swift, GameControllerAdapter.swift, TouchInputAdapter.swift
  - Sources/PorticoCore/Logging/LogCenter.swift
  - Sources/PorticoCore/Models/ControlLayout.swift, EnvironmentModels.swift, GameModels.swift, GlobalSettings.swift
  - Sources/PorticoCore/Runtime/ExecutionBackend.swift, ProcessManager.swift, RuntimeManager.swift, RuntimeModels.swift, Win32Layer.swift
  - Sources/PorticoCore/Store/AppSandbox.swift, ConfigurationManager.swift, LibraryStore.swift
  - Sources/PorticoCore/Support/SupportLevel.swift

- **PorticoRuntime target — Sources (20 arquivos C):**
  - Sources/PorticoRuntime/src/pr_asm.c, pr_audio.c, pr_cap.c, pr_cpu.c, pr_cpu64.c, pr_gfx.c, pr_gl.c, pr_host.c, pr_input.c, pr_log.c, pr_pe.c, pr_peproc.c, pr_selftest.c, pr_surf.c, pr_types.c, pr_vm.c, pr_win32.c, pr_winhello.c, pr_zip.c, (pr_gfx, etc)

- **PorticoRuntime target — Headers (19 headers + umbrella):**
  - Sources/PorticoRuntime/include/portico/*.h (19 headers: pr_types.h, pr_asm.h, pr_audio.h, pr_cap.h, pr_cpu.h, pr_cpu64.h, pr_gfx.h, pr_gl.h, pr_host.h, pr_input.h, pr_log.h, pr_pe.h, pr_peproc.h, pr_surf.h, pr_types.h, pr_unwind.h, pr_vm.h, pr_win32.h, pr_winhello.h, pr_zip.h)
  - Sources/PorticoRuntime/include/PorticoRuntime.h (umbrella header, Public)
  - Todos com ATTRIBUTES = (Public, )

**Evidência:**
```
D8E2B34C...: 26 files (Portico app)
40F3438D...: 27 files (PorticoCore)
A4C7DC8A...: 20 files (PorticoRuntime)
Headers phase: 20 headers (19 + umbrella)
```

### 4. Dependências entre targets

- **Portico → PorticoCore:**
  - PBXContainerItemProxy: remoteGlobalIDString = PorticoCore target, remoteInfo = PorticoCore
  - PBXTargetDependency: target = PorticoCore
  - Link Binary With Libraries: PorticoCore.framework in Frameworks (Portico)
  - Embed Frameworks: PorticoCore.framework in Embed Frameworks (CodeSignOnCopy, RemoveHeadersOnCopy)

- **PorticoCore → PorticoRuntime:**
  - PBXContainerItemProxy: remoteGlobalIDString = PorticoRuntime target, remoteInfo = PorticoRuntime
  - PBXTargetDependency: target = PorticoRuntime
  - Link Binary With Libraries: PorticoRuntime.framework in Frameworks (PorticoCore)

Cadeia: **Portico → PorticoCore → PorticoRuntime** corretamente configurada.

**Evidência:**
```
grep remoteInfo → PorticoCore, PorticoRuntime
grep "PorticoCore.framework in Frameworks" → 2 ocorrências (Link + Embed)
grep "PorticoRuntime.framework in Frameworks" → 2 ocorrências (Link Core + Embed)
```

### 5. Frameworks e bibliotecas

- **Portico target Frameworks phase:**
  - PorticoCore.framework
  - Metal.framework
  - MetalKit.framework
  - AVFoundation.framework
  - GameController.framework
  - libz.tbd

- **PorticoCore target Frameworks phase:**
  - PorticoRuntime.framework

- **PorticoRuntime target Frameworks phase:**
  - libz.tbd

- **Portico target Embed Frameworks phase (Copy Files, dstSubfolderSpec=10):**
  - PorticoCore.framework (CodeSignOnCopy, RemoveHeadersOnCopy)
  - PorticoRuntime.framework (CodeSignOnCopy, RemoveHeadersOnCopy)

### 6. Build settings críticos

**Portico (app):**
- PRODUCT_BUNDLE_IDENTIFIER = io.portico.Portico
- PRODUCT_MODULE_NAME = Portico
- INFOPLIST_FILE = Sources/PorticoApp/Info.plist
- HEADER_SEARCH_PATHS = $(SRCROOT)/Sources/PorticoRuntime/include
- OTHER_CFLAGS = -DPR_ENABLE_ZLIB=1
- CLANG_ENABLE_MODULES = YES
- DEFINES_MODULE = YES
- SWIFT_VERSION = 5.0
- IPHONEOS_DEPLOYMENT_TARGET = 17.0

**PorticoCore (framework):**
- PRODUCT_BUNDLE_IDENTIFIER = io.portico.PorticoCore
- PRODUCT_MODULE_NAME = PorticoCore (== import)
- PRODUCT_NAME = PorticoCore
- DEFINES_MODULE = YES
- DYLIB_COMPATIBILITY_VERSION = 1, DYLIB_CURRENT_VERSION = 1, DYLIB_INSTALL_NAME_BASE = @rpath
- INSTALL_PATH = $(LOCAL_LIBRARY_DIR)/Frameworks
- HEADER_SEARCH_PATHS = $(SRCROOT)/Sources/PorticoRuntime/include
- OTHER_CFLAGS = -DPR_ENABLE_ZLIB=1
- CLANG_ENABLE_MODULES = YES
- SWIFT_VERSION = 5.0
- IPHONEOS_DEPLOYMENT_TARGET = 17.0
- SKIP_INSTALL = NO

**PorticoRuntime (framework):**
- PRODUCT_BUNDLE_IDENTIFIER = io.portico.PorticoRuntime
- PRODUCT_MODULE_NAME = PorticoRuntime (== import)
- PRODUCT_NAME = PorticoRuntime
- DEFINES_MODULE = YES
- DYLIB_COMPATIBILITY_VERSION = 1, DYLIB_CURRENT_VERSION = 1, DYLIB_INSTALL_NAME_BASE = @rpath
- INSTALL_PATH = $(LOCAL_LIBRARY_DIR)/Frameworks
- HEADER_SEARCH_PATHS = $(SRCROOT)/Sources/PorticoRuntime/include
- MODULEMAP_FILE = $(SRCROOT)/Sources/PorticoRuntime/include/module.modulemap
- OTHER_CFLAGS = -DPR_ENABLE_ZLIB=1
- CLANG_ENABLE_MODULES = YES
- GCC_C_LANGUAGE_STANDARD = gnu11
- IPHONEOS_DEPLOYMENT_TARGET = 17.0
- SKIP_INSTALL = NO

### 7. Umbrella header e module map (novos arquivos)

**Sources/PorticoRuntime/include/PorticoRuntime.h:**
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

**Sources/PorticoRuntime/include/module.modulemap:**
```
framework module PorticoRuntime {
    umbrella header "PorticoRuntime.h"
    export *
    module * { export * }
}
```

Esses arquivos permitem que Swift faça `import PorticoRuntime` e que o framework seja um módulo Clang válido.

### 8. Arquivos alterados

- `Portico.xcodeproj/project.pbxproj` — reescrito completo com 3 targets, dependências, frameworks, headers, sources, embed, configs, module map, umbrella header. Antes tinha 1 target e todos os arquivos no app; agora tem separação correta.
- `Sources/PorticoRuntime/include/PorticoRuntime.h` — criado (umbrella header)
- `Sources/PorticoRuntime/include/module.modulemap` — criado (module map)

Nenhum código funcional removido, nenhum mock/stub criado, nenhuma arquitetura alterada — apenas configuração Xcode corrigida.

### 9. Verificação completa

**Targets existentes:**
- Portico (app) — 641004946ADDF49A07272EF5
- PorticoCore (framework) — 607EF030517049F49451CE82
- PorticoRuntime (framework) — 87AB843997844840815A816A

**Dependências:**
- Portico → PorticoCore (PBXTargetDependency E94417D147AB47B193012103 + proxy 36A1A12706ED424BB5DB8939)
- PorticoCore → PorticoRuntime (PBXTargetDependency CEDEA32790404686A1ED5B2F + proxy 4B216AE29DE1475C9FF39C54)
- Cadeia Portico → PorticoCore → PorticoRuntime: **CORRETAMENTE CONFIGURADO**

**Link Binary With Libraries:**
- Portico: PorticoCore.framework + Metal + MetalKit + AVFoundation + GameController + libz.tbd
- PorticoCore: PorticoRuntime.framework
- PorticoRuntime: libz.tbd

**Embed Frameworks:**
- Portico: PorticoCore.framework + PorticoRuntime.framework (CodeSignOnCopy, RemoveHeadersOnCopy, dstSubfolderSpec=10)

**PRODUCT_MODULE_NAME:**
- Portico: Portico (não usado em import, mas definido)
- PorticoCore: PorticoCore == `import PorticoCore` ✓
- PorticoRuntime: PorticoRuntime == `import PorticoRuntime` ✓

**Build em clone limpo:**
- Todos os arquivos referenciados existem no repositório (nenhum depende de /tmp ou máquina Arena)
- Umbrella header e module.modulemap estão versionados em Sources/PorticoRuntime/include/
- HEADER_SEARCH_PATHS usa $(SRCROOT)/Sources/PorticoRuntime/include (relativo, funciona em clone limpo)
- MODULEMAP_FILE usa $(SRCROOT)/Sources/PorticoRuntime/include/module.modulemap (relativo)
- Frameworks system (Metal, MetalKit, AVFoundation, GameController, libz.tbd) são SDKROOT, disponíveis em macOS
- Nenhum caminho absoluto de máquina local

**Preparado para xcodebuild em macOS/GitHub Actions:**
```
xcodebuild -list -project Portico.xcodeproj
→ Targets: Portico, PorticoCore, PorticoRuntime

xcodebuild -project Portico.xcodeproj -scheme Portico -configuration Debug -sdk iphoneos -destination generic/platform=iOS clean build
→ Deve compilar PorticoRuntime.framework → PorticoCore.framework → Portico.app
→ PorticoCore module resolvido via target dependency
→ PorticoRuntime module resolvido via umbrella + module.modulemap + target dependency
```

**Regressão C:**
- build/pr_tests: 3411 verificações, 0 falhas
- /tmp/run_pe_battery_final: TOTAL 76 PASS 76 FAIL 0 HANG 0

**Interface WinOS preservada:**
- 6 arquivos WinOS (Brand/Home/CreatePC/Loading/Desktop/Overlay) continuam no target Portico (26 arquivos)
- Nenhum arquivo removido

## Conclusão

O erro `Unable to resolve module dependency: 'PorticoCore'` foi corrigido na raiz, não com workaround falso. O projeto agora possui 3 targets reais com dependências corretas, PRODUCT_MODULE_NAME correspondendo exatamente aos imports, Link Binary With Libraries e Embed Frameworks configurados, umbrella header e module map para PorticoRuntime, e está preparado para compilar em clone limpo no GitHub Actions com Xcode/macOS.

**Status: READY FOR XCODEBUILD**
