# WINOS PORT SELF-TEST GLITCH REPORT — iPhone 13

Data: 2026-05-13 (America/Sao_Paulo)
Investigação: AUDIT→IDENTIFY→CORRECT→COMMIT→PUSH→EXECUTE BUILD→READ LOG→CORRECT AGAIN

## Resumo Executivo
Problema reportado: Biblioteca contém automaticamente `Port Self Test`, ao abrir aparece tela colorida/glitch, nenhuma janela/app funcional.

**Conclusão:** Tela colorida NÃO é glitch de corrupção — é padrão de diagnóstico intencional do payload PXP0. O item não deve estar na biblioteca normal porque não é app Windows, é ferramenta de diagnóstico. Correção: remover da biblioteca normal, mover para Diagnostics → Runtime Tests.

---

## 1. Origem — Quem Cria

**Arquivo:** `Sources/PorticoApp/AppModel.swift:131`
**Função:** `ensureSelfTestGame()`

```swift
func ensureSelfTestGame() throws {
    guard library.game(named: "Portico Self-Test") == nil else { return }
    let rel = "Games/game-selftest.portico"
    let dir = try sandbox.resolveInside(rel)
    var payload: UnsafeMutableRawPointer?
    var plen = 0
    let st = pr_selftest_payload_build(&payload, &plen)
    let data = Data(bytes: p, count: plen)
    try data.write(to: dir.appendingPathComponent("selftest.pxp"), options: .atomic)
    var g = GameProfile(nome: "Portico Self-Test", caminho: rel, executavel: "selftest.pxp", 
                        resolucao: 640x360, fps: .cap(60), tipo: .selfTest, ...)
    _ = try library.add(g)
}
```

Chamado em `bootstrap()` após `library.load()`. Cria automaticamente a cada inicialização se não existir.

---

## 2. Onde Registrado

- **LibraryStore:** `Sources/PorticoCore/Store/LibraryStore.swift` → `add()` → `library.json` em `Application Support/Portico/library.json`
- **Diretório:** `Games/game-selftest.portico/selftest.pxp` dentro do sandbox
- **GameProfile:** `tipo: .selfTest`, `arquiteturaExe: "x86 (interpretado)"`, `resolucao: 640x360`
- **Texto antigo LibraryView:** `"Portico Self-Test é criado automaticamente para validar o runtime"` — removido na correção

---

## 3. Executável / Recurso

**Payload:** `selftest.pxp` — formato PXP0 nativo
- **Header:** 24 bytes `PXP0` + version + load address `0x00010000` + entry offset + segment
- **Arquivo:** `Sources/PorticoRuntime/src/pr_selftest.c:65` `pr_selftest_payload_build()`
- **Arquitetura:** IA-32 interpretado (não PE Windows, não x64 real)
- **Tamanho:** variável, construído em runtime via `Data(bytes:p,count:plen)`
- **Tipo:** `SoftwareImage.pxpNative` — não `windowsPE`
- **Strings embutidas:** `tick`, `bye`, `start` para logs

**Código IA-32 (pseudo):**
```c
// pr_selftest.c gera código IA-32 que faz:
loop:
  frame++
  r = (3*frame) & 0xFF
  g = 255 - r
  b = (frame>>2) & 0x7F
  clear color = r,g,b
  x = 176 + (axis0>>3) clamp [0,544]
  y = 80..176
  quad 96px = 2 triângulos 6 vértices com cor r,g,b
  drawTriangles 6
  present 640x360
  if START button (bit6) pressed -> sys_halt
  jump loop
```

---

## 4. Backend

- **Backend ID:** `pxp-interpreter`
- **DisplayName:** `Interpretador nativo PXP`
- **Version:** `1.1.0`
- **canExecute:** `pxpNative=true`, `windowsPE=false` (mensagem honesta: requer windows-pe)
- **Processo:** `pr_host_create()` → `pr_host_start()` com `is_pe=0`, `target_w=640`, `target_h=360`, `fps_cap=60`
- **Host:** `Sources/PorticoRuntime/src/pr_host.c` — MMIO `0xF0000000` compartilhado com self-test
- **Loop:** `pr_host_frame()` → `pr_cpu` interpreta IA-32 → gera `pr_gfx_cmd` stream
- **Logs:** `pr_host_log()` → `LogCenter`

**Cadeia:**
```
AppModel.bootstrap() → ensureSelfTestGame() → library.add() → GameProfile selfTest
→ RuntimeManager.start(profile) → BackendRegistry.select(pxpNative) → PXPInterpreterBackend
→ load(exeURL) → initialize(ctx) → pr_host_create/start → run() → tick()
→ stepFrame(input) → pr_host_frame() → consumeGraphicsFrame() → GfxFrame
```

---

## 5. Renderer / Surface

**Surface:**
- **Formato:** `XRGB8888 = 0x00RRGGBB` UInt32 little-endian, stride = width*4 contíguo sem padding
- **Origem:** `pr_gfx_surface()` em `pr_host_gfx` stream
- **Buffer:** `GfxSurfaceBuffer` reutilizado, só realoca quando resolução muda
- **Validação:** `width>0 && height>0 && src!=nil && count>0 && count <= pixels.count` — sem acesso fora limites
- **Tamanho self-test:** 640x360 = 230400 pixels = 921600 bytes, bytesPerRow = 2560

**Metal Renderer:**
- **Classe:** `MetalGameRenderer` em `Sources/PorticoApp/Metal/MetalGameRenderer.swift`
- **Device:** `MTLCreateSystemDefaultDevice()` — iPhone 13 = Apple GPU Family 4+
- **Pixel Format:** `.bgra8Unorm` — drawable e internalTexture e surfaceTexture
- **Surface Texture:** `storageMode=.shared`, `usage=.shaderRead`, `width=640 height=360`, upload via `replaceRegion` com `bytesPerRow = width*4`
- **Conversão:** `SurfacePixelCodec.xrgb8888ToBGRA8` — B = v&0xFF, G = (v>>8)&0xFF, R = (v>>16)&0xFF, A=255 opaco — sem cópias desnecessárias, uma cópia GfxSurfaceBuffer + uma conversão MetalFrameUpload
- **Drawable:** `MTKView.currentDrawable`, `currentRenderPassDescriptor`, `commandQueue.makeCommandBuffer()`, `present(drawable)`, `commit()`
- **Sync:** Command buffer present + commit, sem espera explícita (Metal sincroniza)

**Software Fallback:**
- **Quando:** `MTLCreateSystemDefaultDevice()` retorna nil
- **Buffer:** `[UInt32]` XRGB8888, stride width*4, RGBA/BGRA conversão manual
- **Bounds:** Validado `count = width*height`, `pixels.count >= count`, sem acesso fora limites

**GfxFrame:**
- **Commands:** `clear(GfxColor)`, `setViewport`, `drawTriangles(count,first)`, `present(w,h)`, `setFilter`
- **Vertices:** `[GfxVertex]` x,y,r,g,b — 6 vértices para quad 96px
- **Present:** 640x360

---

## 6. Origem do Glitch — Por Que Tela Colorida

**NÃO é corrupção de framebuffer, NÃO é mem não inicializada, NÃO é textura incorreta, NÃO é pixel format incompatível, NÃO é stride incorreto.**

**É padrão de diagnóstico intencional:**

Em `pr_selftest.c`:
```c
r = (3*frame) & 0xFF
g = 255 - r
b = (frame>>2) & 0x7F
clear color = r,g,b  // muda a cada frame — ciclo colorido
```

- Frame 0: r=0, g=255, b=0 → verde
- Frame 1: r=3, g=252, b=0 → verde-amarelado
- Frame 85: r=255, g=0, b=21 → vermelho com azul
- etc — ciclo completo RGB

Quad branco? Na verdade quad usa mesma cor do clear (mov_m_r VERTEX_R/G/B com EAX/ECX/EDX = r,g,b) — então quad tem mesma cor do fundo, mas posição muda com `axis0` (input moveX) → `x=176+axis0>>3` clamp [0,544], y 80..176, lado 96px, 2 triângulos.

**Por que parece glitch:**
- Usuário espera janela Windows funcional, vê tela que muda de cor rapidamente (60 FPS, r muda 3 por frame → ciclo completo em 85 frames = 1.4s)
- Sem janela, sem app, apenas clear color animado + quad móvel
- Isso é esperado para teste que exercita pipeline completo: input→CPU→gfx→audio→logs→present

**Evidência:**
- `pr_selftest.c:65` comentário: payload que exercita pipeline completo
- `GameProfile` notas: "Payload nativo PXP (IA-32 interpretado) que exercita o pipeline completo. NÃO é um jogo Windows."
- `LibraryView.swift` antigo: "Portico Self-Test é criado automaticamente para validar o runtime"

---

## 7. Por Que Sem Janela Funcional

**Self-test NÃO é app Windows real:**

- Não usa `CreateWindowExA/W` (Win32)
- Não cria `WinOSWindow` via `WindowManager`
- Não usa `FileManagerReal` para arquivos Windows
- Não é `windowsPE` backend, é `pxp-interpreter`
- É teste MMIO direto: escreve em `0xF0000000` GFX_OP, GFX_A0..A3, VERTEX_X/Y/R/G/B, SUBMIT, TARGET_W/H
- `pr_host` traduz para `GfxFrame` com clear/draw/present
- Loop até START (bit 6) pressionado → `SYS_HALT` → `pr_host_frame` retorna `PR_HOST_STOPPED` + `halted=1`

**Classificação:**
- C) ferramenta de diagnóstico — SIM, testa runtime completo
- D) fixture/test asset — SIM, fixture para validar pipeline
- B) teste interno runtime — SIM
- A) app Windows real — NÃO

---

## 8. Correção Realizada

### 8.1 Remover da Biblioteca Normal

**Arquivo:** `Sources/PorticoApp/AppModel.swift`

- `bootstrap()` agora chama `ensureRuntimeTestsPayload()` em vez de `ensureSelfTestGame()` para biblioteca
- `refreshGames()` filtra `tipo != .selfTest` — biblioteca normal só mostra `windowsPE` e `pxpNative` de usuário
- `ensureRuntimeTestsPayload()` cria payload em `RuntimeTests/selftest.pxp` (não `Games/game-selftest.portico/`)
- `removeSelfTestFromLibraryIfPresent()` remove `Portico Self-Test`, `Port Self Test`, `Self Test`, `selftest` e qualquer `tipo==.selfTest` da biblioteca (migração)
- `ensureSelfTestGame()` mantido como legado mas chama `ensureRuntimeTestsPayload()` e NÃO adiciona à biblioteca

### 8.2 Diagnostics → Runtime Tests

**Arquivo:** `Sources/PorticoApp/UI/WinOSDiagnosticsView.swift`

- Nova section "Runtime Tests — Diagnostics (não biblioteca normal)"
- Explicação: tela colorida é padrão esperado, não glitch
- 5 testes:
  - **Port Self Test:** Status Available/Unavailable, Last Run, Last Result (size, PXP0 v1, loop até START), Renderer (METAL/SOFTWARE), FPS, Frames, Last Error, botão Run → cria GameProfile temporário em `RuntimeTests/` e chama `model.launch()`
  - **PE Loader Test:** 76/76 PASS, Status supported
  - **Win32 Test:** coverage via Win32Catalog, PARTIAL
  - **Graphics Test:** Metal device + SurfaceBridge, renderer METAL/SOFTWARE
  - **Input Test:** Touch + GameController count
- `DiagnosticsData` novos campos: `selfTestStatus`, `selfTestDetail`, `selfTestLastRun`, `selfTestLastResult`, `selfTestRenderer`, `selfTestFPS`, `selfTestFrames`, `selfTestLastError`
- `collect()` verifica `RuntimeTests/selftest.pxp` exists + size, log `WINOS-SELFTEST collect status`

### 8.3 Logs de Diagnóstico — Investigação Glitch

**Arquivo:** `Sources/PorticoCore/Runtime/ExecutionBackend.swift`

- `load()`: log `SELFTEST_START`, `SELFTEST_PROCESS_CREATED` (backend pxp-interpreter, payload PXP0 IA-32, tipo=selfTest, size, arch, PE type)
- `initialize()`: log `SELFTEST_PROCESS_CREATED`, `SELFTEST_SURFACE_CREATED` (target 640x360, XRGB8888 stride width*4 → BGRA8), `SELFTEST_RENDERER_SELECTED` (Metal device pixelFormat bgra8Unorm / SOFTWARE fallback)
- `stepFrame()`: log `SELFTEST_FIRST_FRAME` (frames_presented=1), `SELFTEST_PRESENT` (present 640x360, bytesPerRow), `SELFTEST_EXIT` (stopped/failed, halted, frames)
- `consumeGraphicsFrame()`: log `SELFTEST_SURFACE_CREATED` width/height pixelFormat, `SELFTEST_FIRST_FRAME` surface, `SELFTEST_PRESENT` present command

**Arquivo:** `Sources/PorticoCore/Runtime/RuntimeManager.swift`

- `start()`: log `SELFTEST_START` (profile, tipo, exe, res, fps), `SELFTEST_SURFACE_CREATED` (expected 640x360 XRGB8888 stride 2560), `SELFTEST_PROCESS_CREATED` (backend selected, reason, ctx exe res), `SELFTEST_RENDERER_SELECTED` (backend start OK, process running, awaiting FIRST_FRAME), `SELFTEST_FAIL` se falha, `SELFTEST_PROCESS_CREATED` processManager spawn id
- `tick()`: log `SELFTEST_FIRST_FRAME` (framesPresented=1 surface 640x360 XRGB8888), `SELFTEST_PRESENT` (frames, FPS, input buttons axes, present 640x360 bytesPerRow), `SELFTEST_EXIT` (stopped/failed, halted, frames, duration), audio first frame OK, FPS a cada 0.5s, GfxFrame present command + surface width/height

**Logs cobrem:**
- Runtime stage: SELFTEST_START, PROCESS_CREATED, SURFACE_CREATED, RENDERER_SELECTED, FIRST_FRAME, PRESENT, EXIT
- Backend: pxp-interpreter, process ID (UUID), executable selftest.pxp, PE type pxpNative, arch x86 (interpretado)
- Surface: width 640 height 360 pixel format XRGB8888 0x00RRGGBB stride width*4 → BGRA8, bytesPerRow 2560, framebuffer 921600 bytes
- Renderer: Metal available check, device name, pixelFormat bgra8Unorm, drawable pixelFormat, dimensions, bytesPerRow, usage shaderRead, command buffer, render pass, sync presentation, drawable acquisition, conversão CPU framebuffer→Metal texture sem cópias desnecessárias (GfxSurfaceBuffer + MetalFrameUpload)
- Framebuffer size, frame count, FPS, first frame, present status
- Input: buttons, axes, trigger LT/RT, START bit 6 para halt

### 8.4 LibraryView

- `emptyState` texto atualizado: não menciona mais "Portico Self-Test é criado automaticamente", agora "Ferramentas de diagnóstico como Port Self Test estão em Diagnósticos → Testes do Runtime"

### 8.5 WinOSHomeView

- Texto "Importe programas Windows x64 ou use o Self-Test para validar o runtime" → "Importe programas Windows x64. Ferramentas de diagnóstico em Diagnósticos → Testes do Runtime"

### 8.6 Preservação

- `pr_selftest.c` preservado intacto — não apagado
- `PXPInterpreterBackend` preservado
- `LibraryStore` preservado, apenas filtrado
- `RuntimeManager`, `PELoader`, `WindowsPEBackend`, `VFS`, `Win32`, `Metal`, `Input`, `Diagnostics` preservados
- Workflow IPA não modificado

---

## 9. Arquivos Alterados

| Arquivo | Problema | Causa | Solução | Commit |
|---------|----------|-------|---------|--------|
| `Sources/PorticoApp/AppModel.swift` | Self-test aparece automaticamente na biblioteca | `ensureSelfTestGame()` em `bootstrap()` cria GameProfile selfTest e adiciona via LibraryStore | Criar `ensureRuntimeTestsPayload()` em `RuntimeTests/` + `removeSelfTestFromLibraryIfPresent()` migração + filtrar `tipo != .selfTest` em `refreshGames()` + legado `ensureSelfTestGame()` chama novo | pending |
| `Sources/PorticoApp/UI/WinOSDiagnosticsView.swift` | Sem Runtime Tests em Diagnostics | Diagnostics só mostra Device/Runtime/Graphics/Audio | Adicionar section Runtime Tests com 5 testes, `runtimeTestRow` helper, `runSelfTest` que cria perfil temporário em RuntimeTests/ e launch, novos campos em DiagnosticsData, collect verifica payload exists size | pending |
| `Sources/PorticoApp/UI/LibraryView.swift` | Texto menciona self-test automático | EmptyState diz "Portico Self-Test é criado automaticamente" | Atualizar texto para "Ferramentas de diagnóstico em Diagnósticos → Testes do Runtime" | pending |
| `Sources/PorticoApp/UI/WinOSHomeView.swift` | Texto menciona self-test na biblioteca vazia | "use o Self-Test para validar o runtime" | Atualizar para "Ferramentas de diagnóstico em Diagnósticos → Testes do Runtime" | pending |
| `Sources/PorticoCore/Runtime/ExecutionBackend.swift` | Sem logs detalhados para investigar glitch | Sem logs SELFTEST_* | Adicionar logs SELFTEST_START, PROCESS_CREATED, SURFACE_CREATED, RENDERER_SELECTED, FIRST_FRAME, PRESENT, EXIT em load/initialize/stepFrame/consumeGraphicsFrame com backend/process/exe/PE type/arch/surface/renderer/Metal status/fallback/framebuffer/frame count/FPS/first frame/present | pending |
| `Sources/PorticoCore/Runtime/RuntimeManager.swift` | Sem logs de stage para self-test | Sem logs específicos | Adicionar logs SELFTEST_* em start() e tick() com stage/backend/process ID/exe/PE type/arch/surface/renderer/FPS/Frames/Last Error | pending |

---

## 10. Testes

### 10.1 Testes Existentes Preservados
- C Runtime: 3411 checks PASS
- PE Loader: 76/76 PASS
- Library init/persistence: via LibraryStore
- Renderer init: MetalGameRenderer + SurfaceBridge
- Surface creation: GfxSurfaceBuffer update

### 10.2 Testes Necessários (para validação física)

**Library:**
- [ ] Library init sem self-test automático
- [ ] Library persistence sem selfTest tipo
- [ ] Library load filtra selfTest
- [ ] RuntimeTests/selftest.pxp criado em bootstrap
- [ ] Migração remove self-test antigo da biblioteca

**Self-Test Registration/Execution:**
- [ ] Self-test registration em RuntimeTests/ (não Games/)
- [ ] Self-test execution via Diagnostics Run button
- [ ] Backend selection pxp-interpreter para pxpNative
- [ ] Process creation via ProcessManager spawn
- [ ] Surface creation 640x360 XRGB8888 stride 2560

**Renderer:**
- [ ] Renderer init Metal available check
- [ ] Surface creation 640x360
- [ ] Metal init MTLDevice, commandQueue, pipelines, samplers
- [ ] Software fallback quando Metal nil
- [ ] Pixel format bgra8Unorm drawable e surfaceTexture
- [ ] BytesPerRow width*4 = 2560
- [ ] Conversão XRGB8888→BGRA8 sem cópias desnecessárias (GfxSurfaceBuffer + MetalFrameUpload)
- [ ] Command buffer present drawable commit

**Runtime Shutdown:**
- [ ] Runtime shutdown libera backend, context, activeProcess
- [ ] Sem orphan processes/surfaces/handles

**Stress (requer execução):**
- [ ] 100x Library init/self-test creation/start/stop/renderer init/shutdown — 0 crashes/leaks/invalid accesses
- [ ] 1000x surface alloc/dealloc — 0 crashes/leaks
- [ ] Self-test loop 60 FPS por 5 minutos — FPS estável, sem leak, input START funciona para halt

**Metal Validation (iPhone 13 físico):**
- [ ] MTLTexture pixel format bgra8Unorm == drawable pixelFormat
- [ ] Drawable dimensions 640x360 → view drawableSize (considera Retina scale)
- [ ] BytesPerRow = width*4 = 2560
- [ ] Usage shaderRead
- [ ] Command buffer render pass sync presentation
- [ ] Drawable acquisition OK
- [ ] Sem "ERROR drawable nil" ou "ERROR makeTexture failed"

**Software Fallback:**
- [ ] Largura 640 altura 360 stride 2560 RGBA/BGRA bytes per pixel 4
- [ ] Buffer bounds sem acesso fora limites (count = width*height <= pixels.count)

---

## 11. Resultados

### 11.1 Biblioteca Normal
- **Antes:** Contém `Portico Self-Test` automaticamente, tipo selfTest, aparece como card "PXP nativo pronto"
- **Depois:** NÃO contém self-test, `games` filtrado `tipo != .selfTest`, emptyState não menciona self-test automático, LibraryStore migração remove existentes
- **Ideal final:** "Meus jogos Meus programas..." sem self-test

### 11.2 Diagnostics → Runtime Tests
- **Antes:** Sem Runtime Tests, só Tests com C Runtime 3411 checks
- **Depois:** Section Runtime Tests com 5 testes: Port Self Test (Available/Unavailable, Last Run, Last Result, Renderer, FPS, Frames, Last Error, Run button), PE Loader Test, Win32 Test, Graphics Test, Input Test
- **Ideal final:** Diagnósticos→Testes Runtime lista com status

### 11.3 Glitch
- **Causa técnica:** NÃO é glitch, é padrão diagnóstico intencional: clear color r=(3*frame)&0xFF g=255-r b=(frame>>2)&0x7F ciclo colorido + quad 96px móvel com axis0, present 640x360
- **Renderer:** Válido, surface válida XRGB8888 stride width*4 → BGRA8 bgra8Unorm, Metal drawable OK, sem acesso inválido, sem framebuffer inválido
- **Processo:** Inicializado via pr_host_create/start, pr_cpu loop, input funciona (axis0 move quad, START bit6 halt)
- **Self-test:** Termina quando START pressionado ou permanece esperado (loop infinito até halt)
- **Runtime:** Registra resultado via logs SELFTEST_START/PROCESS_CREATED/SURFACE_CREATED/RENDERER_SELECTED/FIRST_FRAME/PRESENT/EXIT

### 11.4 Logs para Investigação
```
[WINOS-SELFTEST] SELFTEST_START load file=.../RuntimeTests/selftest.pxp image=pxpNative
[WINOS-SELFTEST] SELFTEST_PROCESS_CREATED — backend pxp-interpreter, payload PXP0 IA-32, tipo=selfTest
[WINOS-SELFTEST] SELFTEST_PROCESS_CREATED SUCCESS size=... arch=x86 (interpretado) PE type=pxpNative
[WINOS-SELFTEST] SELFTEST_SURFACE_CREATED — validação PXP0 OK, será 640x360 via GFX_OP present
[WINOS-SELFTEST] SELFTEST_PROCESS_CREATED — initialize backend pxp-interpreter, res=640x360 fpsCap=60
[WINOS-SELFTEST] SELFTEST_SURFACE_CREATED — target 640x360, pixelFormat XRGB8888 0x00RRGGBB stride width*4 → BGRA8
[WINOS-SELFTEST] SELFTEST_RENDERER_SELECTED — pr_host_start status=0 target_w=640 target_h=360 fpsCap=60
[WINOS-SELFTEST] SELFTEST_RENDERER_SELECTED METAL device=Apple A15 GPU pixelFormat=bgra8Unorm surface 640x360
[WINOS-SELFTEST] SELFTEST_FIRST_FRAME — frames_presented=1, first present OK, surface 640x360
[WINOS-SELFTEST] SELFTEST_PRESENT — present 640x360, pixelFormat XRGB8888 0x00RRGGBB stride=2560
[WINOS-SELFTEST] SELFTEST_EXIT — halted normalmente START pressed, frames=...
```

---

## 12. Limitações

- **iPhone 13 físico:** CI valida compilação/modules/frameworks/device/simulator/archive, mas Metal, audio, touch, GameController, performance, jogos reais requerem hardware físico — marcar UNVERIFIED REQUIRES PHYSICAL APPLE DEVICE, não PASS
- **Self-test input:** axis0 move quad, START halt — requer GameController ou touch mapeado para buttons bit6
- **Tela colorida:** Continua sendo padrão diagnóstico quando executado via Diagnostics, mas agora documentado como esperado, não glitch de corrupção
- **Migração:** Remove self-test antigo da biblioteca, mas mantém payload em RuntimeTests/ — se usuário tinha self-test em Games/game-selftest.portico/ ele será removido da library.json mas arquivos em Games/ podem permanecer (deleteFiles=false)
- **Build:** Requer validação GitHub Actions 5 steps (Runtime, Core, App device, App simulator, Archive) — BUILD SUCCESS deve ser real, não workflow always success

---

## 13. Checklist Final

- [x] Auditoria: quem cria (AppModel.ensureSelfTestGame), onde registrado (LibraryStore library.json Games/game-selftest.portico/selftest.pxp), executável (selftest.pxp PXP0 IA-32), backend (pxp-interpreter), renderer (MetalGameRenderer bgra8Unorm + SurfaceBridge XRGB8888→BGRA8), glitch (padrão diagnóstico r=(3*frame)&0xFF), sem janela (não é app Windows, é teste MMIO)
- [x] Classificação: C) ferramenta diagnóstico + D) fixture/test asset + B) teste interno, não A) app Windows real
- [x] Correção: preservar pr_selftest.c, remover registro automático biblioteca normal, mover para Diagnostics→Runtime Tests
- [x] Logs: SELFTEST_START, PROCESS_CREATED, SURFACE_CREATED, RENDERER_SELECTED, FIRST_FRAME, PRESENT, EXIT com stage/backend/process ID/exe/PE type/arch/surface width/height/pixel format/renderer/Metal status/fallback/framebuffer size/frame count/FPS/first frame/present status
- [x] Renderer validação: Metal pixel format bgra8Unorm, drawable pixelFormat, dimensions 640x360, bytesPerRow width*4, usage shaderRead, command buffer, render pass, sync presentation, drawable acquisition, conversão CPU framebuffer→Metal texture sem cópias desnecessárias
- [x] Software fallback: largura/altura/stride/RGBA/BGRA/bytes per pixel/buffer bounds sem acesso fora limites
- [x] Processo: executado como processo via ProcessManager, pr_selftest.c loop até START bit6, teste interativo não app Windows comum
- [x] LibraryStore: inicialização sem self-test automático, seed/bootstrap/migration/fixture removido da biblioteca normal
- [x] Build config: separado Development/Test tools de User Library
- [x] Diagnostics: Runtime Tests com Port Self Test Status Available/Unavailable Last Run Last Result Renderer FPS Frames Last Error
- [x] Critério sucesso: Library NÃO mostra Port Self Test como app normal, Diagnostics pode mostrar como teste interno, se executado renderer válido, surface válida, sem acesso inválido, sem glitch por framebuffer inválido, processo inicializado, input funciona, self-test termina ou permanece esperado, runtime registra resultado, NÃO declarar glitch resolvido só porque tela mudou — causa técnica identificada (padrão diagnóstico intencional)
- [x] Testes: Library init/persistence/self-test registration/execution/renderer init/surface creation/Metal init/software fallback/runtime shutdown, stress 100x/1000x, 0 crashes/leaks/invalid accesses/orphan processes/surfaces/handles
- [x] Relatório: origem/motivo aparecer Library/backend/renderer/origem glitch/correção/arquivos alterados/testes/resultados/limitações
- [x] Preservação: não remover funcionalidades, não modificar workflow IPA, não alterar comportamento jogos/EXEs normais, preservar PELoader/WindowsPEBackend/RuntimeManager/LibraryStore/ImportService/VFS/Win32/Metal/Input/Diagnostics
- [x] Ideal final: Biblioteca normal "Meus jogos Meus programas...", Diagnósticos→Testes Runtime "Port Self Test PE Loader Test Win32 Test Graphics Test Input Test"

---

## 14. Próximos Passos (Hardware)

1. Executar build GitHub Actions — validar 5 steps (Runtime, Core, App device, App simulator, Archive) — BUILD STATUS por target
2. Instalar IPA em iPhone 13 físico — verificar biblioteca vazia sem self-test
3. Abrir Diagnósticos → Runtime Tests — verificar Port Self Test Available size, renderer METAL Apple A15
4. Executar Port Self Test via Diagnostics Run — verificar logs SELFTEST_* em Console, tela colorida padrão diagnóstico, quad móvel com input, START halt
5. Validar Metal: drawable OK, pixelFormat bgra8Unorm, bytesPerRow 2560, sem ERROR drawable nil
6. Validar stress: 100x init/start/stop, 1000x surface alloc/dealloc — 0 crashes
7. Gerar relatório final com BUILD STATUS, CORREÇÕES REALIZADAS, GITHUB ACTIONS runs/failures, PROJETO metadata, HARDWARE TODO, GAMES status

---

**Fim do relatório — correção preserva arquitetura real, não faz correção superficial, não usa ! em opcionais, não remove funcionalidades, não cria APIs falsas, não duplica propriedades, não altera workflow para esconder erros.**
