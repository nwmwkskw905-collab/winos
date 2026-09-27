# RELATÓRIO — GRUPO 8: PRIMEIRO SUBCONJUNTO 3D REAL DO WINOS

Base preservada: GRUPO 5+6+7 (CPU/PE/GDI/janelas/mensagens/input/timers/
TouchInputAdapter/GameControllerAdapter/SurfaceBridge/MetalGameRenderer).
Builders v0–v13 intactos; nenhum teste removido; nenhum caminho certificado
alterado. **GRUPO 8: CONCLUÍDO** — cadeia completa provada por teste 3D real,
determinístico e reproduzível.

| Verificação | Resultado |
|---|---|
| `make c-test` | **1728 verificações, 0 falhas** (GRUPO 7: 1649 → +79) |
| `swift test` | **62 tests, 0 failures** (GRUPO 7: 61 → +1) |
| `gcc -Wall -Wextra` (src+tests, arquivo a arquivo) | **0 warnings** |
| `gcc -fanalyzer` (src, arquivo a arquivo) | **0 findings** |
| Builders v0–v13 (exercitados pela suíte) | **✓** |
| hello_real / hello_user / hello_app / hello_gdi / hello_input | **5× exit 42** (preservados) |
| **hello_gl.exe (NOVO, 3D)** | **exit 42** |

---

## 1. Grupo 8 concluído ou não

**CONCLUÍDO.** Existe teste 3D real, determinístico e reproduzível que
demonstra o caminho completo: **PE real → API 3D → renderização real →
superfície/framebuffer → SurfaceBridge → Metal → frame apresentado**
(`test_gl_hello_pe` em C + `testGLFrameFlowsThroughBridgeToMetalUpload`
em Swift).

## 2. API escolhida: **OpenGL 1.1** (e por quê)

A escolha foi baseada no PE de controle e nas chamadas reais encontradas
(objdump de imports da FASE 1): o PE exige **15 funções planas da OPENGL32
+ SwapBuffers (GDI32)** — ABI `cdecl` simples, sem COM. A alternativa
Direct3D 9 exigiria objetos COM (`IDirect3D9`/`IDirect3DDevice9`) com vtables
de ~17 e ~119 slots, superfície de ABI muito maior que o "menor subconjunto
necessário". D3D9 e OpenGL **não** foram implementados simultaneamente.

## 3. PE de controle utilizado

**`realpe/hello_gl.c` → `Tests/PorticoRuntimeTests/data/hello_gl.exe`**
(x86_64 REAL). Janela 320×240 + `wglCreateContext/wglMakeCurrent` +
dois triângulos 3D em clip-space com cores distintas em profundidades
distintas (z=+0.5 vermelho atrás; z=−0.5 verde na frente) → **oclusão real
pelo z-buffer** → `glReadPixels` valida 3 pixels DENTRO do PE
(sobreposto=verde ⇒ frente venceu o teste de profundidade; só-trás=vermelho;
fora=preto) → `SwapBuffers` → **exit 42**. Validação objetiva por pixels
dentro do próprio PE **e** na superfície apresentada (testes C/Swift).

## 4. Como o PE foi compilado

`x86_64-w64-mingw32-gcc -O2 -o Tests/PorticoRuntimeTests/data/hello_gl.exe realpe/hello_gl.c -lgdi32 -lopengl32` (MinGW-w64 GCC 14).

## 5. APIs realmente implementadas (16 — exatamente as analisadas)

**opengl32.dll (15)** — `wglCreateContext`, `wglMakeCurrent`,
`wglDeleteContext`, `glClearColor`, `glClearDepth`, `glClear`, `glEnable`,
`glViewport`, `glBegin`, `glColor3f`, `glVertex3f`, `glEnd`, `glFinish`,
`glGetError`, `glReadPixels`.
**gdi32.dll (+1)** — `SwapBuffers`.

Comportamento REAL (módulo novo `pr_gl.c`, implementação própria da
especificação pública do GL 1.1 — sem código proprietário): framebuffer
XRGB 320×240 + z-buffer float; viewport (clip→janela GL); limpeza de
cor/profundidade; rasterização barycentrica de triângulos com regra
top-left, interpolação de cor (Gouraud), **teste de profundidade GL_LESS**;
`glReadPixels` (GL_RGBA/GL_UNSIGNED_BYTE) com origem inferior-esquerda;
máquina de erros GL real (sticky-first: `GL_INVALID_ENUM/VALUE/OPERATION`);
`SwapBuffers` apresenta o frame com flip vertical na superfície XRGB8888 do
caminho certificado.

**Extensão de ABI x64 (justificada pela análise dos imports):** argumentos
`float/double` chegam em XMM0–3 (ABI Microsoft x64). A captura do trap
(`pr_peproc`) lia só GPRs; acrescentou-se `pr_cpu64_xmm()` e a máscara
`arg_xmm` em `pr_win32_export` (campo aditivo) — `glClearColor` (0x0F),
`glClearDepth` (0x01), `glColor3f`/`glVertex3f` (0x07). Os wrappers `f_gl*`
reinterpretam os bits como float/double (idêntico nos testes).

## 6. APIs analisadas mas ainda NÃO implementadas (todas → honestidade)

- **Todo o restante do OpenGL 1.1+** (matrix stack `glMatrixMode/glLoadIdentity/
  glRotatef/glFrustum`, `glDrawArrays/glDrawElements`, texturas
  `glTexImage2D`, iluminação, blend/alpha, `GL_QUADS/LINES/POINTS`,
  `glGetString`, `glReadPixels` fora de (GL_RGBA, GL_UNSIGNED_BYTE),
  `wglGetProcAddress`, swap com vsync…): **fora do catálogo** → no PE real a
  chamada = **EXECUTION STOPPED** (stub de diagnóstico do loader); via
  `pr_win32_call` = `PR_ERR_RANGE`/`PR_ERR_UNSUPPORTED` — nunca sucesso
  falso (ancorado em `test_gl_unidade`).
- **Direct3D 9 / dxgi / DirectDraw / dinput**: intocados → EXECUTION
  STOPPED (ancorado: `pr_win32_lookup("d3d9.dll","Direct3DCreate9")==NULL`).
- Parâmetros inválidos NO subconjunto sinalizam erro GL real
  (`GL_LIGHTING→INVALID_ENUM`, `GL_QUADS→INVALID_ENUM`, `glBegin` aninhado
  →`INVALID_OPERATION`) — testado.

## 7. Fluxo completo do frame até Metal (FASE 5)

```
hello_gl.exe (PE real x86_64)
  → OPENGL32!glBegin/glColor3f/glVertex3f/glEnd   (ABI x64 c/ XMM capturado)
  → pr_gl.c: rasterização barycentrica + z-buffer GL_LESS (320×240)
  → GDI32!SwapBuffers → framebuffer GL (flip) → superfície XRGB8888
      (contrato: UInt32 LE 0x00RRGGBB, stride = width*4)
  → SurfaceBridge: GfxSurfaceBuffer.update(width:height:copyFrom:)
  → SurfacePixelCodec.xrgb8888ToBGRA8 (B,G,R,255)
  → MetalFrameUpload.stage (bytesPerRow = w*4; realloc só no resize;
    onStaged = "frame submitted" = apresentado)
  → MetalGameRenderer (.bgra8Unorm, replaceRegion, blit letterbox) —
    renderer ÚNICO; caminho GDI intacto e coexistente
```
Provado em `GLBridgeTests.testGLFrameFlowsThroughBridgeToMetalUpload`:
pixels do frame 3D conferidos como bytes BGRA8 prontos para `replaceRegion`
(verde na oclusão [0,255,0,255], vermelho [0,0,255,255], preto [0,0,0,255]).

## 8. Novos testes C adicionados

`Tests/PorticoRuntimeTests/test_gl.c` (**+79 verificações**):
- `test_gl_unidade`: setup wgl, clear, 2 triângulos com oclusão z-buffer,
  `glReadPixels` (3 pixels exatos), apresentação `SwapBuffers` + pixels da
  superfície (flip exato 239−y), erros GL reais (INVALID_ENUM/OPERATION,
  sticky-first), honestidade (`glMatrixMode/glDrawArrays/glTexImage2D`
  fora do catálogo → `PR_ERR_RANGE`; `d3d9` ausente).
- `test_gl_hello_pe`: executa o hello_gl.exe real (peproc) → exit 42 +
  frame apresentado validado pixel a pixel na superfície do caminho Metal.

## 9. Novos testes Swift adicionados

`Tests/PorticoCoreTests/GLBridgeTests.swift` (**+1 teste**):
`testGLFrameFlowsThroughBridgeToMetalUpload` — roda o PE 3D real via
`pr_peproc` e leva o frame apresentado pelo contrato SurfaceBridge até o
upload BGRA8 do Metal (`stage`/`onStaged`/`withBytes`) com asserts byte a
byte. Só foi adicionado teste Swift porque havia comportamento real da
camada iOS/Metal a validar (mandato).

## 10–15. Resultados e totais

| Item | Resultado |
|---|---|
| 10. `make c-test` | **1728 verificações, 0 falhas** |
| 11. `swift test` | **62 tests, 0 failures** |
| 12. `gcc -Wall -Wextra` | **0 warnings** |
| 13. `gcc -fanalyzer` | **0 findings** |
| 14. Total final de verificações C | **1728** |
| 15. Total final de testes Swift | **62** |

## 16. Próximo bloqueador técnico REAL

Para um PE/jogo 3D **real maior** que o PE de controle: o subconjunto GL
cobre triângulos imediatos em clip-space, mas qualquer PE 3D real tipicamente
exige **matrizes (glMatrixMode/glLoadIdentity/glRotatef/glFrustum),
vertex arrays (glDrawArrays/glDrawElements) e texturas (glTexImage2D +
sampler)** — e/ou a **ABI COM do Direct3D 9** (Direct3DCreate9→CreateDevice→
Clear/BeginScene/DrawPrimitiveUP/EndScene/Present). Além disso, apresentação
contínua com pacing (loop de frames/vsync) e entrada 3D simultânea ainda não
foram exercitados por PE real. **Próximo passo recomendado:** escolher um PE
real de controle maior (que use matriz+vértices em array OU D3D9 COM), analisar
os imports reais dele e crescer o menor subconjunto a partir daí — tudo o que
o novo PE não exigir permanece EXECUTION STOPPED honesto.

---

## Honestidade (FASE 7)

NÃO se declara compatibilidade geral com Direct3D 9 nem com OpenGL; NÃO se
declara compatibilidade com GTA V, MX Bikes ou qualquer jogo comercial; o
WinOS NÃO executa jogos 3D comerciais. O GRUPO 8 demonstra **somente** o
subconjunto comprovado pelo PE de controle (16 funções acima). Tudo o mais
permanece documentado como não implementado e resulta em EXECUTION STOPPED
honesto. Nenhum código proprietário do Windows, DirectX, Winlator ou
projetos fechados foi copiado — `pr_gl.c` é implementação própria a partir
da especificação pública do OpenGL 1.1.
