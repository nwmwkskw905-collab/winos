# RELATÓRIO — GRUPO 9: SEGUNDO PE 3D REAL E EXPANSÃO CONTROLADA DO OPENGL

Base preservada: GRUPO 5–8 (CPU/PE/GDI/janelas/mensagens/input/timers/
SurfaceBridge/MetalGameRenderer + GL 1.1 mínimo do GRUPO 8). Nenhum teste
removido; nenhum caminho certificado alterado; builders v0–v13 intactos;
hello_gl.exe do GRUPO 8 preservado e passando.

| Verificação | Resultado |
|---|---|
| `make c-test` | **1815 verificações, 0 falhas** (G8: 1728 → +87) |
| `swift test` | **63 tests, 0 failures** (G8: 62 → +1) |
| `gcc -Wall -Wextra` | **0 warnings** |
| `gcc -fanalyzer` | **0 findings** |
| hello_gl.exe (G8, regressão) | **exit 42** |
| **hello_gl2.exe (NOVO, G9)** | **exit 42** |
| hello_real / user / app / input | **4× exit 42** (preservados) |

---

## 1. GRUPO 9 concluído ou não

**CONCLUÍDO.** Existe um segundo PE 3D real, determinístico e reproduzível
(`hello_gl2.exe`) passando pelo runtime (CPU x64 → imports → OpenGL) e pelo
pipeline gráfico certificado (framebuffer → SurfaceBridge → BGRA8 → Metal).

## 2. Segundo PE escolhido/criado

NÃO havia segundo PE 3D no repositório (inspeção confirmada) — **criado**
`realpe/hello_gl2.c` → `hello_gl2.exe`, deliberadamente mais complexo que o
hello_gl.exe: **cubo 3D de 6 faces** (cor chapada por face) com
**MATRIZES** (`glMatrixMode/glLoadIdentity/glOrtho/glTranslatef/glRotatef` —
projeção ortográfica + modelview T·Ry(30°)·Rx(25°)) e **VERTEX ARRAYS**
(`glEnableClientState/glVertexPointer/glColorPointer/glDrawElements` —
24 vértices, 36 índices `GL_UNSIGNED_SHORT`), com z-buffer. Texturas
deliberadamente NÃO usadas (categoria fora do subconjunto, conforme mandato).

## 3. Como ele foi compilado

`x86_64-w64-mingw32-gcc -O2 -o Tests/PorticoRuntimeTests/data/hello_gl2.exe realpe/hello_gl2.c -lgdi32 -lopengl32` (MinGW-w64 GCC 14).

## 4. Imports reais encontrados (objdump + fluxo de execução)

**OPENGL32.dll — 22 funções**: glClear, glClearColor, glClearDepth, glEnable,
glFinish, glGetError, glReadPixels, glViewport, wglCreateContext,
wglDeleteContext, wglMakeCurrent (11 já do G8) + **glMatrixMode, glLoadIdentity,
glOrtho, glTranslatef, glRotatef, glEnableClientState, glVertexPointer,
glColorPointer, glDrawElements (9 novas)**. **GDI32.dll**: SwapBuffers (G8).
**USER32**: janelas (G6). Fluxo real confirmado ANTES da implementação: o
runtime parou honesto em `OPENGL32.dll!glColorPointer` (EXECUTION STOPPED no
prepare = primeira API nova alcançada pela execução real).

**Separação exigida:** (a) já implementadas no G8: 11 + SwapBuffers;
(b) novas necessárias: 9 (acima); (c) importadas mas não executadas: nenhuma
(todas as 22 aparecem no fluxo real do PE); (d) ainda não implementadas:
glTexImage2D, glDrawArrays, glPushMatrix/PopMatrix, glFrustum, texturas,
GL_QUADS/LINES/POINTS, D3D9 — todas → EXECUTION STOPPED.

## 5. APIs novas necessárias

As 9 do item 4 (matrizes: 5; vertex arrays: 4).

## 6. APIs novas implementadas (todas com comportamento real)

| API | Comportamento real |
|---|---|
| `glMatrixMode` | GL_MODELVIEW/GL_PROJECTION; enum inválido → GL_INVALID_ENUM |
| `glLoadIdentity` | identidade na matriz corrente |
| `glOrtho` | matriz ortográfica real (6 doubles: 4 em XMM0-3 + 2 na pilha — ABI x64); degenerada → GL_INVALID_VALUE |
| `glTranslatef` | translação (pós-multiplicação como o GL) |
| `glRotatef` | rotação de eixo genérico (sin/cos reais); eixo nulo = identidade (sem erro, como o GL) |
| `glEnableClientState` | GL_VERTEX_ARRAY/GL_COLOR_ARRAY; resto → INVALID_ENUM |
| `glVertexPointer` | formato mínimo do PE: size=3, GL_FLOAT, stride 0/12; outros → INVALID_VALUE/INVALID_ENUM |
| `glColorPointer` | idem |
| `glDrawElements` | GL_TRIANGLES + GL_UNSIGNED_SHORT (≤4096 índices); coleta vértices/cores (ponteiros de convidado validados no draw = semântica diferida real), transforma por P·Mv com divisão perspectiva e rasteriza pelo **rasterizador certificado do G8**; erros reais para modo/tipo/estado inválidos |

Operações de matriz dentro de `glBegin/glEnd` → GL_INVALID_OPERATION
(como o GL). Modo imediato do G8 (`glBegin/glVertex3f/...`) agora também
passa pelas matrizes — com estado padrão identidade, o resultado do G8 é
byte-idêntico (regressão certificada). `pr_win32_export.arg_xmm` (G8)
estendido: glOrtho=0x0F, glTranslatef=0x07, glRotatef=0x0F.

## 7. APIs ainda não implementadas (EXECUTION STOPPED honesto)

Matrix stack (`glPushMatrix/glPopMatrix`), `glFrustum`, `glDrawArrays`,
texturas (`glTexImage2D/glTexParameteri/glTexCoord*`), normais/iluminação,
`GL_QUADS/LINES/POINTS`, `glVertexPointer` com outros tipos/formatos,
índices `GL_UNSIGNED_BYTE/INT`, clipping de frustum (vértices fora do volume
não são aparados — limitação documentada), e **todo o Direct3D 9 / dxgi /
DirectDraw** (FASE 7: NÃO implementados neste grupo).

## 8. Fluxo completo do segundo PE até Metal

```
hello_gl2.exe (PE real x86_64)
  → OPENGL32!glMatrixMode/glLoadIdentity/glOrtho/glTranslatef/glRotatef
      (matrizes P e Mv = T·Ry·Rx; ABI x64: floats em XMM, doubles na pilha)
  → OPENGL32!glEnableClientState/glVertexPointer/glColorPointer/glDrawElements
      (24 vértices + 36 índices USHRT; transformação P·Mv + divisão perspectiva)
  → pr_gl.c: rasterização barycentrica certificada + z-buffer GL_LESS
  → GDI32!SwapBuffers → framebuffer (flip) → superfície XRGB8888
  → SurfaceBridge → SurfacePixelCodec.xrgb8888ToBGRA8
  → MetalFrameUpload (replaceRegion/blit do MetalGameRenderer ÚNICO)
  → frame apresentado
```

## 9. Validação de pixels (3 níveis, independente)

1. **Dentro do PE** (`glReadPixels`): frente=verde (162,108), esquerda=vermelho
   (96,130), topo=azul (150,175), fundo=preto (8,8) → exit 10–13 se falhar.
   Os valores esperados foram **DERIVADOS DA GEOMETRIA** (projeção analítica
   das faces; margens ≥21px às bordas; cores chapadas ⇒ interpolação exata)
   — não gravados do runtime; detecta implementação falsa.
2. **Superfície do WinOS** (`test_gl2_hello_pe`): mesmos pixels com flip 239−y.
3. **Bytes BGRA8 do caminho Metal** (`testSecondGLFrameFlows…` no Swift):
   [0,255,0,255], [0,0,255,255], [255,0,0,255], [0,0,0,255].

## 10. Novos testes C

`Tests/PorticoRuntimeTests/test_gl2.c` (**+87 verificações**):
`test_gl2_matrizes_arrays` (pipeline orto+translate com vertex arrays,
pixels, apresentação, erros reais: enum inválido, matriz dentro de begin,
ortho degenerada, client state/tipo/size/mode/type inválidos, eixo nulo;
âncoras de honestidade) e `test_gl2_hello_pe` (PE real + pixels da
superfície). Âncoras do `test_gl.c` atualizadas G8→G9 (padrão honesto:
`glMatrixMode` agora REAL ⇒ assertado como REAL; `glTexImage2D` vira a
âncora não implementada; `glDrawArrays/glPushMatrix` continuam fora).

## 11. Novos testes Swift

`GLBridgeTests.testSecondGLFrameFlowsThroughBridgeToMetalUpload` (**+1**):
frame do hello_gl2 pelo contrato SurfaceBridge→BGRA8→Metal com asserts byte
a byte (nível 3 da FASE 4). Nenhum outro comportamento novo de
SurfaceBridge/Metal existia a validar.

## 12–17. Resultados e totais

| Item | Resultado |
|---|---|
| 12. `make c-test` | **1815 verificações, 0 falhas** |
| 13. `swift test` | **63 tests, 0 failures** |
| 14. `gcc -Wall -Wextra` | **0 warnings** |
| 15. `gcc -fanalyzer` | **0 findings** |
| 16. Total final de verificações C | **1815** |
| 17. Total final de testes Swift | **63** |

## 18. Resultado do hello_gl.exe original

**exit 42** (regressão G8 preservada — `test_gl_hello_pe` intacto + bateria
de PEs; modo imediato agora passa por matrizes identidade = resultado
byte-idêntico).

## 19. Resultado do segundo PE

**exit 42** (hello_gl2.exe — roteiro completo: matrizes + vertex arrays +
z-buffer + SwapBuffers + 4 validações de pixels dentro do PE).

## 20. Próximo bloqueador técnico REAL

Evidência do experimento: o caminho OpenGL cresceu de 16 → **25 APIs reais**
cobrindo matrizes + vertex arrays com um segundo PE real, sem tocar em COM.
Para o próximo salto (PE 3D real maior), o que falta em ordem de impacto:
**matrix stack + glFrustum (projeção perspectiva)**, **glDrawArrays +
glVertexPointer com mais formatos**, **texturas (glTexImage2D + samplers)**,
normais/iluminação, clipping de frustum, e apresentação contínua com pacing.
**Decisão (FASE 7, agora com evidência): continuar expandindo OpenGL**
é o caminho de menor risco e progresso comprovado (2 PEs 3D reais já rodam);
iniciar o subconjunto D3D9 COM só se um PE de controle real exigir D3D9.

---

## Honestidade (FASE 8)

NÃO se declara compatibilidade geral com OpenGL nem com Direct3D; NÃO se
declara compatibilidade com GTA V, MX Bikes ou jogos comerciais; o WinOS
NÃO executa jogos 3D comerciais. O GRUPO 9 demonstra **somente** o
comportamento comprovado pelos dois PEs de controle. Tudo o que não está
implementado permanece EXECUTION STOPPED ou com o erro GL específico correto.
Nenhum código proprietário do Windows, DirectX, Winlator ou projetos fechados
foi copiado — a expansão do `pr_gl.c` continua implementação própria a partir
da especificação pública do OpenGL 1.1. Direct3D 9 NÃO foi implementado
(FASE 7).
