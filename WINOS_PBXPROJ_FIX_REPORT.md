# WINOS — ADIÇÃO CIRÚRGICA DOS 9 ARQUIVOS AO XCODE PROJECT

Data: 2026-09-30
Baseline: `Portico.xcodeproj/project.pbxproj` restaurado do Git HEAD
Hash baseline informado: `b592b06a5ba1cf0f3e62e8b0f695ab6755527879`
Hash atual (após adição cirúrgica): `4643dc93ad8aa4fc251d0f59ba0135d9941b4343` (git blob hash)
Linhas baseline (sem 9 arquivos, com comentários corretos): 777
Linhas após adição: 813
Diferença: 36 linhas essenciais

## Objetivo

Adicionar somente os 9 novos arquivos do Desktop Runtime ao projeto Xcode, com diff PEQUENO, sem regenerar projeto, sem alterar UUIDs existentes, Build Settings, Frameworks ou Targets.

## Arquivos Adicionados (9)

1. `Sources/PorticoApp/UI/WinOSRealDesktopView.swift` — 31K
2. `Sources/PorticoCore/Desktop/WinOSCompositor.swift` — 9.6K
3. `Sources/PorticoCore/Desktop/WinOSDesktopShell.swift` — 12K
4. `Sources/PorticoCore/Desktop/WinOSDesktopTests.swift` — 15K
5. `Sources/PorticoCore/Desktop/WinOSFileManagerReal.swift` — 14K
6. `Sources/PorticoCore/Desktop/WinOSInputBridge.swift` — 8.0K
7. `Sources/PorticoCore/Desktop/WinOSProcessManagerReal.swift` — 5.8K
8. `Sources/PorticoCore/Desktop/WinOSRenderEngine.swift` — 13K
9. `Sources/PorticoCore/Desktop/WinOSWindowManager.swift` — 14K

Todos existem no disco — validado via `ls -lh`.

## Procedimento Cirúrgico

1. Lido `project.pbxproj` atual (baseline limpa 777 linhas, sem 9 arquivos)
2. Identificados grupos existentes:
   - UI: `5AC99B9EF7C04A2A5AC290C2 /* UI */` — existente
   - Desktop: `609065A52F4548824594DBDE /* Desktop */` — existente (continha apenas `WinOSDesktopView.swift` em HEAD)
   - Portico target: `641004946ADDF49A07272EF5 /* Portico */`
   - Sources Build Phase: `D8E2B34C6803A5DAEDA2CB7E /* Sources */`
   - PorticoCore group, Frameworks group, etc. — existentes
3. Gerados 18 UUIDs novos (9 FileReference + 9 BuildFile) sem colisão com existentes
4. Adicionados:
   - 9 PBXFileReference ao final da seção PBXFileReference
   - 9 PBXBuildFile ao final da seção PBXBuildFile
   - 1 referência em UI group
   - 8 referências em Desktop group (reutilizado, não criado outro)
   - 9 referências em Sources Build Phase
5. Nenhuma outra alteração: sem reordenação, sem alteração de comentários existentes, sem alteração de Build Settings, Frameworks, Targets, sem normalização de line endings

## UUIDs — PBXFileReference (9 novos)

Gerados novos, sem reutilizar existentes:

- `WinOSRealDesktopView.swift`: `6FEDF7B36449470FBDB52992`
- `WinOSCompositor.swift`: `E2C6316DC0534172B7D3B6F7`
- `WinOSDesktopShell.swift`: `DDCFAF8BABA047358D063F7A`
- `WinOSDesktopTests.swift`: `58A21F5AAAE34F7491D404EB`
- `WinOSFileManagerReal.swift`: `4E84E580D2AE4FA6ADCA290D`
- `WinOSInputBridge.swift`: `1D952329D22D43EB95EF5B95`
- `WinOSProcessManagerReal.swift`: `D3A21115A0894C808AF01578`
- `WinOSRenderEngine.swift`: `E8A49F50536642EBA9F3F34C`
- `WinOSWindowManager.swift`: `76D04AA5A0AD47E5BD34FBA8`

## UUIDs — PBXBuildFile (9 novos)

- `WinOSRealDesktopView.swift in Sources`: `4D9FF1771811435989554E43` → fileRef `6FEDF7B36449470FBDB52992`
- `WinOSCompositor.swift in Sources`: `DA85ADD5337C49EF97B6CB83` → fileRef `E2C6316DC0534172B7D3B6F7`
- `WinOSDesktopShell.swift in Sources`: `71B6A1D8BD7E4A4DBF23D43A` → fileRef `DDCFAF8BABA047358D063F7A`
- `WinOSDesktopTests.swift in Sources`: `2025E64AE35F4AB1A24FA53F` → fileRef `58A21F5AAAE34F7491D404EB`
- `WinOSFileManagerReal.swift in Sources`: `34C5245E089B4B6195C83CE3` → fileRef `4E84E580D2AE4FA6ADCA290D`
- `WinOSInputBridge.swift in Sources`: `431CAC85F4E64F8FB8A0283D` → fileRef `1D952329D22D43EB95EF5B95`
- `WinOSProcessManagerReal.swift in Sources`: `E7ACA2A0782241BEB814EAE9` → fileRef `D3A21115A0894C808AF01578`
- `WinOSRenderEngine.swift in Sources`: `C74B878E9F014E9A820B4FE8` → fileRef `E8A49F50536642EBA9F3F34C`
- `WinOSWindowManager.swift in Sources`: `E67C0F91B7AA42808991DF88` → fileRef `76D04AA5A0AD47E5BD34FBA8`

Total: 18 UUIDs novos, conforme esperado.

## Grupo de Cada Arquivo

- `WinOSRealDesktopView.swift`:
  - Grupo: `UI` (`5AC99B9EF7C04A2A5AC290C2`)
  - Path: `Sources/PorticoApp/UI/`
  - Child adicionado: `6FEDF7B36449470FBDB52992 /* WinOSRealDesktopView.swift */,`

- `WinOSCompositor.swift`:
  - Grupo: `Desktop` (`609065A52F4548824594DBDE`)
  - Path: `Sources/PorticoCore/Desktop/`
  - Child: `E2C6316DC0534172B7D3B6F7 /* WinOSCompositor.swift */,`

- `WinOSDesktopShell.swift`:
  - Grupo: `Desktop`
  - Child: `DDCFAF8BABA047358D063F7A /* WinOSDesktopShell.swift */,`

- `WinOSDesktopTests.swift`:
  - Grupo: `Desktop`
  - Child: `58A21F5AAAE34F7491D404EB /* WinOSDesktopTests.swift */,`

- `WinOSFileManagerReal.swift`:
  - Grupo: `Desktop`
  - Child: `4E84E580D2AE4FA6ADCA290D /* WinOSFileManagerReal.swift */,`

- `WinOSInputBridge.swift`:
  - Grupo: `Desktop`
  - Child: `1D952329D22D43EB95EF5B95 /* WinOSInputBridge.swift */,`

- `WinOSProcessManagerReal.swift`:
  - Grupo: `Desktop`
  - Child: `D3A21115A0894C808AF01578 /* WinOSProcessManagerReal.swift */,`

- `WinOSRenderEngine.swift`:
  - Grupo: `Desktop`
  - Child: `E8A49F50536642EBA9F3F34C /* WinOSRenderEngine.swift */,`

- `WinOSWindowManager.swift`:
  - Grupo: `Desktop`
  - Child: `76D04AA5A0AD47E5BD34FBA8 /* WinOSWindowManager.swift */,`

Grupo Desktop já existia em HEAD (com 1 arquivo `WinOSDesktopView.swift`), foi reutilizado — não criado outro grupo Desktop, conforme regra.

## Confirmação — Sources Build Phase (9 entradas)

Target Portico: `641004946ADDF49A07272EF5`
Build Phase: `D8E2B34C6803A5DAEDA2CB7E /* Sources */`

Entradas adicionadas:

```
4D9FF1771811435989554E43 /* WinOSRealDesktopView.swift in Sources */,
DA85ADD5337C49EF97B6CB83 /* WinOSCompositor.swift in Sources */,
71B6A1D8BD7E4A4DBF23D43A /* WinOSDesktopShell.swift in Sources */,
2025E64AE35F4AB1A24FA53F /* WinOSDesktopTests.swift in Sources */,
34C5245E089B4B6195C83CE3 /* WinOSFileManagerReal.swift in Sources */,
431CAC85F4E64F8FB8A0283D /* WinOSInputBridge.swift in Sources */,
E7ACA2A0782241BEB814EAE9 /* WinOSProcessManagerReal.swift in Sources */,
C74B878E9F014E9A820B4FE8 /* WinOSRenderEngine.swift in Sources */,
E67C0F91B7AA42808991DF88 /* WinOSWindowManager.swift in Sources */,
```

Validação:
```bash
grep -A300 "D8E2B34C6803A5DAEDA2CB7E /* Sources" project.pbxproj | grep "WinOSRealDesktopView\|WinOSCompositor\|WinOSDesktopShell\|WinOSDesktopTests\|WinOSFileManagerReal\|WinOSInputBridge\|WinOSProcessManagerReal\|WinOSRenderEngine\|WinOSWindowManager" | wc -l
→ 9
```

## Preservação — UUIDs Existentes

Verificação por amostragem:

- `AppModel.swift`: fileRef `2FAD037C3DD3AA2F20C33801`, buildFile `CD93DAC8104828492727025D` — idênticos HEAD vs fixed
- `PorticoApp.swift`: `396985C66686FE7C05565B71`, `BB4564DB7DE721BC304790BA` — preservados
- `LibraryView.swift`: `4AFD3194FF8D2DEBD2E1E815`, `FC7C0F1D484F4C54FC2E5422` — preservados
- `WinOSHomeView.swift`: `6175D38181FFC2685D08F431`, `34CC67A98920076B49BB34FF` — preservados
- `WinOSDesktopView.swift`: `64E7F3A779028549BD9FCFAC`, `BD26839E8C596F0433CBF65D` — preservados
- `ExecutionBackend.swift`: `482AA6348D19A4E059C2D888`, `436E2CD503D9ABC0D651ABDF` — preservados
- `RuntimeManager.swift`: `D23B609CEC9DE9137BCDBE67`, `B37FE3247C2D137503852F49` — preservados
- `pr_selftest.c`: `207E41BE1C791C814C035DC7`, `75B9030D91DD26222F5F4AEE` — preservados

Método:
```bash
diff -u clean_head.pbxproj fixed.pbxproj | grep -E "AppModel.swift|PorticoApp.swift|LibraryView.swift"
→ 0 linhas (nenhuma alteração em existentes)
```

Nenhuma referência antiga removida.
Nenhum arquivo existente recriado com novo UUID.

## Preservação — Build Settings

Verificados idênticos HEAD vs fixed:

- Project Debug `06125DCB1954C4F7EEE69894`: `IPHONEOS_DEPLOYMENT_TARGET = 17.0`, `SWIFT_VERSION = 5.0`, `GCC_C_LANGUAGE_STANDARD = gnu11` — preservados
- Project Release `AACF5BDB2BC481EB947517D5`: idem — preservado
- Target Debug `E07B8131BAB0D598AD460C2C`: `PRODUCT_BUNDLE_IDENTIFIER = io.portico.Portico`, `INFOPLIST_FILE = Sources/PorticoApp/Info.plist`, `HEADER_SEARCH_PATHS = $(SRCROOT)/Sources/PorticoRuntime/include`, `OTHER_CFLAGS = -DPR_ENABLE_ZLIB=1` — preservados
- Target Release `AB3F0C1C96C3F4D3D4AB9249`: idem — preservado

## Preservação — Frameworks

Grupo Frameworks `B687106895D06AA330DAFED5` children idênticos:

- `845175776DA9B77AB4CAFC16 /* Metal.framework */`
- `2FAB3EFC261338FCDA442B95 /* MetalKit.framework */`
- `B58392B0683CF37E493DE0B2 /* AVFoundation.framework */`
- `76E6407D70BFAF7376E20110 /* GameController.framework */`
- `92DB7A71D3B15A69A5A049CD /* libz.tbd */`

Frameworks Build Phase `804DB52E2955449D2F091C7E`: idêntico, sem alteração.

## Preservação — Targets

- `PBXNativeTarget` único: `641004946ADDF49A07272EF5 /* Portico */` — preservado
- `PBXProject` `4DC844ABCCFC0D132D087C1C`: `targets = (641004946ADDF49A07272EF5 /* Portico */,)`, `mainGroup`, `productRefGroup` — preservados
- Build Phases: `D8E2B34C6803A5DAEDA2CB7E /* Sources */`, `804DB52E2955449D2F091C7E /* Frameworks */` — preservados (apenas adição de 9 build files em Sources)

## Diff Estatístico

Baseline limpo (sem 9 arquivos, com comentários corretos, 1 target): 777 linhas
Após adição cirúrgica: 813 linhas
Diferença: 36 linhas essenciais

`diff -u` entre baseline e fixed:

```
78 linhas no formato unified
39 adições
3 remoções (linhas em branco)
```

Detalhamento:
- 9 PBXFileReference
- 9 PBXBuildFile
- 1 child em UI
- 8 children em Desktop
- 9 entradas em Sources Build Phase
- 3 linhas de contexto/formatação

**Critério de sucesso:** diff PEQUENO — PASS. Não contém `392 insertions 701 deletions` ou `400+ insertions 700+ deletions`. Se contivesse centenas de remoções, seria considerado FALHA e desfeito — não ocorreu.

## Validação Estrutural Final

1. [x] Os 9 arquivos existem no disco — PASS (todos 9 com tamanho >0)
2. [x] Existem exatamente 9 novos PBXFileReference — PASS (9/9)
3. [x] Existem exatamente 9 novos PBXBuildFile — PASS (9/9)
4. [x] Existem exatamente 9 novas entradas no PBXSourcesBuildPhase — PASS (9/9)
5. [x] WinOSRealDesktopView.swift está no grupo UI — PASS
6. [x] Os outros 8 arquivos estão no grupo Desktop — PASS (8/8)
7. [x] Nenhum UUID existente foi substituído — PASS (verificado amostragem)
8. [x] Nenhum Build Setting foi alterado — PASS
9. [x] Nenhum Framework foi alterado — PASS
10. [x] Nenhum Target foi alterado — PASS

## Arquivos Tocados

Permitido nesta tarefa:
- `Portico.xcodeproj/project.pbxproj` — adição cirúrgica dos 9 arquivos (única alteração)
- `WINOS_PBXPROJ_FIX_REPORT.md` — este relatório

Não tocados (conforme regra NÃO FAZER):
- `Sources/PorticoApp/AppModel.swift` — não tocado
- `Sources/PorticoApp/UI/LibraryView.swift` — não tocado
- `Sources/PorticoApp/UI/WinOSDiagnosticsView.swift` — não tocado
- `Sources/PorticoApp/UI/WinOSHomeView.swift` — não tocado
- `Sources/PorticoCore/Runtime/ExecutionBackend.swift` — não tocado
- `Sources/PorticoCore/Runtime/RuntimeManager.swift` — não tocado
- Arquivos C — não tocados
- Headers — não tocados
- Frameworks — não tocados
- Build Settings — não tocados
- `winos/` — não tocado
- Sem `.bak` criado

## Conclusão

Adição cirúrgica dos 9 arquivos concluída com sucesso, com diff pequeno (39 adições, 3 remoções), preservando integralmente UUIDs existentes, Build Settings, Frameworks e Targets. Validação exclusivamente estrutural — não afirmado build Xcode, IPA ou teste iPhone.

---
Relatório gerado em `WINOS_PBXPROJ_FIX_REPORT.md`
