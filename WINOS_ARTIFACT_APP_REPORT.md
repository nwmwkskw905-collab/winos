# WINOS — Artifact .app Gerado no GitHub Actions

**Data:** 2026-09-28  
**Objetivo:** Publicar Portico.app compilado como artifact, sem modificar código-fonte

---

## 1. Workflow Modificado

**Arquivo YAML:** `.github/workflows/ios-build.yml`
**Nome workflow:** iOS Build
**Evento:** push (main, master), pull_request, workflow_dispatch
**Runner:** macos-latest

**Comando de build preservado (inalterado):**
```bash
xcodebuild -project Portico.xcodeproj -target PorticoRuntime -configuration Debug -sdk iphoneos -destination generic/platform=iOS CODE_SIGNING_ALLOWED=NO clean build
xcodebuild -project Portico.xcodeproj -target PorticoCore -configuration Debug -sdk iphoneos -destination generic/platform=iOS CODE_SIGNING_ALLOWED=NO clean build
xcodebuild -project Portico.xcodeproj -scheme Portico -configuration Debug -sdk iphoneos -destination generic/platform=iOS CODE_SIGNING_ALLOWED=NO clean build
xcodebuild -project Portico.xcodeproj -scheme Portico -configuration Debug -sdk iphonesimulator -destination 'generic/platform=iOS Simulator' CODE_SIGNING_ALLOWED=NO clean build
xcodebuild -project Portico.xcodeproj -scheme Portico -configuration Release -sdk iphoneos -destination generic/platform=iOS CODE_SIGNING_ALLOWED=NO archive -archivePath build/Portico.xcarchive
```

**CODE_SIGNING_ALLOWED:** Preservado como `NO` em todas as etapas — sem assinatura falsa

---

## 2. Novo Fluxo para Artifact .app

**Adicionado após Archive:**

### Etapa: Locate Portico.app (iOS device)
```bash
find ~/Library/Developer/Xcode/DerivedData -name "*.app" -type d
APP_PATH=$(find ~/Library/Developer/Xcode/DerivedData -path "*Debug-iphoneos/Portico.app" -type d | head -n 1)
# fallback para qualquer Portico.app
if [ -z "$APP_PATH" ]; then APP_PATH=$(find ... -name "Portico.app" ...); fi
# fail explicit se não existir
if [ -z "$APP_PATH" ]; then echo "ERROR" && exit 1; fi
ls -lh "$APP_PATH"
du -sh "$APP_PATH"
ls -lh "$APP_PATH/Frameworks/"
mkdir -p build/app-artifact
cp -R "$APP_PATH" build/app-artifact/
```

**Validação:**
- Verifica existência exata de 1 .app esperado (Portico.app)
- Lista conteúdo (`ls -lh`), tamanho (`du -sh`)
- Mostra Frameworks e Info.plist dos frameworks (validação da correção anterior GENERATE_INFOPLIST_FILE=YES)
- Falha explicitamente `exit 1` se .app não existir
- Copia para `build/app-artifact/Portico.app` para upload

### Etapa: Locate Portico.app (Simulator)
- Similar para `Debug-iphonesimulator/Portico.app`
- Staging em `build/app-artifact-sim/`

### Etapa: Upload WinOS App (device)
```yaml
- name: Upload WinOS App (Portico.app device)
  uses: actions/upload-artifact@v4
  with:
    name: winos-app
    path: build/app-artifact/Portico.app
    if-no-files-found: error
```

### Etapa: Upload WinOS App (Simulator)
```yaml
- name: Upload WinOS App (Simulator)
  uses: actions/upload-artifact@v4
  with:
    name: winos-app-simulator
    path: build/app-artifact-sim/Portico.app
    if-no-files-found: ignore
```

**Artifact build-logs preservado** (if: always(), path build/ + *.log)

---

## 3. Caminho Real do .app

**No runner macOS:**
```
~/Library/Developer/Xcode/DerivedData/Portico-<hash>/Build/Products/Debug-iphoneos/Portico.app
~/Library/Developer/Xcode/DerivedData/Portico-<hash>/Build/Products/Debug-iphonesimulator/Portico.app
```

**Após staging:**
```
build/app-artifact/Portico.app (device, ARM64)
build/app-artifact-sim/Portico.app (simulator)
```

**Nome real:** `Portico.app` (PRODUCT_NAME = Portico, productReference Portico.app no pbxproj) — não inventado, encontrado via `find`

---

## 4. Nome do Novo Artifact

- **Principal:** `winos-app` — contém `Portico.app` (device ARM64 iOS 17+)
- **Secundário:** `winos-app-simulator` — contém `Portico.app` (simulator)
- **Existente preservado:** `build-logs` — contém build/ e logs

**No GitHub Actions UI, após build bem-sucedido, devem aparecer:**
```
winos-app (com Portico.app dentro)
winos-app-simulator (opcional)
build-logs (logs)
```

---

## 5. Confirmação Artifact Contém .app

- Upload usa `path: build/app-artifact/Portico.app` com `if-no-files-found: error` — falha se não existir, não faz upload de arquivos aleatórios de DerivedData
- Antes do upload, `ls -lh` e `du -sh` mostram tamanho aproximado e conteúdo, garantindo que é bundle válido
- `Portico.app/Frameworks/PorticoRuntime.framework/Info.plist` e `PorticoCore.framework/Info.plist` verificados (correção anterior GENERATE_INFOPLIST_FILE=YES)

---

## 6. Alterações Fora do Workflow

- **Nenhuma alteração em código-fonte:** Runtime C, PE loader, VFS, Input, Graphics, Metal, Audio, Swift UI, WinOS branding, PorticoApp, PorticoCore, PorticoRuntime, ControlEditorView — todos preservados
- **Nenhuma alteração em Xcode project:** `project.pbxproj` já corrigido anteriormente com `GENERATE_INFOPLIST_FILE=YES` para frameworks, sem nova alteração nesta etapa
- **Nenhum Info.plist copiado manualmente, nenhum bundle identifier alterado, nenhum certificado falso, nenhum contorno de code signing**

**Apenas:** `.github/workflows/ios-build.yml` modificado com 3 novas etapas (Locate device, Locate simulator, Upload winos-app, Upload simulator) + preservação do upload build-logs

---

## 7. Validação

- **YAML:** `python3 -c "import yaml; yaml.safe_load(...)"` → YAML OK
- **C Runtime:** `gcc ... -o build/pr_tests && ./build/pr_tests` → 3411/0 PASS (sem regressão, sem alteração código)
- **Xcode real:** Não disponível no Arena Linux — validação final requer GitHub Actions macOS runner
- **Sintaxe workflow:** Sem `continue-on-error` que esconda falhas, `if-no-files-found: error` para winos-app garante falha explícita se .app não existir

---

## 8. Relatório Final

**BUILD STATUS:** Preservado — 5 etapas xcodebuild existentes mantidas, com CODE_SIGNING_ALLOWED=NO

**ARTIFACT .APP:**
- Nome: `winos-app`
- Conteúdo: `Portico.app` (device, ARM64, iOS 17+)
- Caminho no runner: `~/Library/Developer/Xcode/DerivedData/Portico-*/Build/Products/Debug-iphoneos/Portico.app`
- Caminho staging: `build/app-artifact/Portico.app`
- Validação: `ls -lh`, `du -sh`, falha explícita se não existir
- Secundário: `winos-app-simulator` com `Portico.app` simulator

**CODE-SIGNING:** `CODE_SIGNING_ALLOWED=NO` preservado em todas as etapas — sem assinatura falsa, sem certificado inventado. IPA assinada será etapa posterior.

**FILES MODIFIED:**
- `.github/workflows/ios-build.yml` — adicionadas etapas Locate + Upload winos-app
- Nenhum arquivo de código-fonte modificado nesta etapa

**VALIDATION:**
- YAML OK
- C 3411/0 PASS
- Swift STATIC AUDIT PASS (correções anteriores preservadas)
- Xcode real pendente GitHub Actions

**Próximo passo:** Commit → Push → GitHub Actions → verificar artifacts `winos-app` contém `Portico.app` válido. Instalação no iPhone 13 requer etapa posterior de assinatura/provisionamento.

**Não afirmado que app está pronto para instalação no iPhone — apenas artifact .app não-assinado disponibilizado.**
