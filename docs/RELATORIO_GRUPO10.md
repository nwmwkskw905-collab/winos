# RELATÓRIO — GRUPO 10: TERCEIRO PE 3D REAL + TEXTURAS + PERSPECTIVA

Data: 2026-09-24 · Runtime: Portico (x64 → WinOS) · Ambiente: Linux (build/testes)
Escopo do grupo: **hello_gl3.exe** (3º PE 3D real) com matrix stack real,
projeção perspectiva (glFrustum), glDrawArrays, textura real (2×2) amostrada
na rasterização. Pipeline único PE→…→MetalGameRenderer preservado.
**NÃO implementado D3D9. NÃO declarada compatibilidade geral com OpenGL nem
com jogos comerciais (GTA V, MX Bikes ou outros).**

---

## 1. O grupo está concluído?
**SIM.** hello_gl3.exe (PE x86_64 REAL, MinGW) executa no runtime real, usa as
10 APIs GL novas alcançadas na execução, rasteriza textura real, usa perspectiva
e matrix stack reais, valida pixels derivados da geometria/textura e retorna 42.
Regressão completa verde (itens 14–18).

## 2. Estado inicial → final dos testes
| Bateria | Antes (G9) | Depois (G10) |
|---|---|---|
| C (`make c-test`) | 2024 verificações / 0 falhas | **2101 verificações / 0 falhas** |
| Swift (`swift test`) | 63 / 0 falhas | **63 / 0 falhas** (inalterado) |
Nenhum teste removido ou enfraquecido. Teste Swift novo **NÃO** criado —
correto pela regra: SurfaceBridge→BGRA8→Metal **não mudou** (teste Swift
artificial seria proibido).

## 3. APIs GL encontradas no PE (inventário real ANTES de implementar)
`x86_64-w64-mingw32-objdump -p hello_gl3.exe` → OPENGL32.dll (29), GDI32.dll
(1: SwapBuffers), USER32.dll (6), KERNEL32.dll (11), msvcrt.dll (24).
OPENGL32: **glBindTexture, glClear, glClearColor, glClearDepth, glColorPointer,
glDisable, glDrawArrays, glDrawElements, glEnable, glEnableClientState, glFinish,
glFrustum, glGenTextures, glGetError, glLoadIdentity, glMatrixMode, glPopMatrix,
glPushMatrix, glReadPixels, glRotatef, glTexCoordPointer, glTexImage2D,
glTexParameteri, glTranslatef, glVertexPointer, glViewport, wglCreateContext,
wglDeleteContext, wglMakeCurrent**.

## 4. APIs já implementadas (Lista A — 19)
glClear, glClearColor, glClearDepth, glColorPointer, glDrawElements, glEnable,
glEnableClientState, glFinish, glGetError, glLoadIdentity, glMatrixMode,
glReadPixels, glRotatef, glTranslatef, glVertexPointer, glViewport,
wglCreateContext, wglDeleteContext, wglMakeCurrent (+ KERNEL32/msvcrt/USER32/GDI32
dos GRUPOs 4–8). Tudo preservado byte-idêntico.

## 5. APIs novas necessárias (Lista B — 10)
glBindTexture, glDisable, glDrawArrays, glFrustum, glGenTextures, glPopMatrix,
glPushMatrix, glTexCoordPointer, glTexImage2D, glTexParameteri.

## 6. APIs novas implementadas (10/10)
Todas do item 5, com comportamento real (item 8). **Parada honesta registrada
ANTES da implementação** (FASE 2): o runtime pré-G10 parou com
`Unsupported Win32 API — OPENGL32.dll!glBindTexture` (EXECUTION STOPPED no
loader = stub de diagnóstico) — confirmou o fluxo e o 1º bloqueador, sem
sucesso falso.

## 7. APIs ainda NÃO implementadas (fora do subconjunto — parada honesta)
glTexImage3D, glLightfv, glTexEnvf, glMultiTexCoord2f (âncoras de teste →
PR_ERR_RANGE/EXECUTION STOPPED), GL_LINEAR/GL_CLAMP (→ GL_INVALID_ENUM com
log), mipmap levels, glBlendFunc, glAlphaFunc, iluminação, display lists,
glTexCoord2f imediato, GL_QUADS/STRIP/ FAN (→ GL_INVALID_ENUM). Não são
chamadas pelos PEs 1–3 e **não foram implementadas por antecipação**.

## 8. Comportamento real de cada API nova
| API | Comportamento real |
|---|---|
| glPushMatrix/glPopMatrix | Pilhas REAIS: MODELVIEW 32 / PROJECTION 4 (profundidade GL). `GL_STACK_OVERFLOW 0x503` / `GL_STACK_UNDERFLOW 0x504`; dentro de glBegin → `GL_INVALID_OPERATION 0x502`. Salva/restaura a matriz corrente completa. |
| glFrustum | Matriz perspectiva real (col-major, pós-multiplicação): w'=-z; divisão perspectiva no pipeline. `GL_INVALID_VALUE 0x501` se n≤0 ou f≤0 ou l==r ou b==t (regra GL). 6 doubles via ABI x64 (XMM0-3 + 2 na pilha — `arg_xmm=0x3F`). glOrtho continua funcionando (G8/G9 byte-idênticos). |
| glGenTextures | Aloca nomes 1..63 por contexto (objeto 0 = default existe, GL real). n≤0 → 0x501. Escreve nomes em memória guest via `pr_win32_ptr`. |
| glBindTexture | GL_TEXTURE_2D; binding de nome não gerado cria o objeto (semântica GL 1.1). target≠TEXTURE_2D → 0x500; nome≥64 → 0x501 (limite documentado). |
| glTexParameteri | MIN/MAG_FILTER só GL_NEAREST (0x2601 → 0x500 + log); WRAP_S/WRAP_T só GL_REPEAT. |
| glTexImage2D | TEXTURE_2D level 0, GL_RGB/GL_UNSIGNED_BYTE, border 0 (outros → 0x500/0x501 reais); copia w×h×3 bytes do guest (resolve validado); pixels NULL → textura incompleta (desabilitada no draw, GL real). |
| glTexCoordPointer | size 2, GL_FLOAT, stride 0/8+ (fora → 0x501/0x500). |
| glDrawArrays | GL_TRIANGLES não-indexado com fetch de arrays do guest (posição+cor+UV); modo/parâmetros inválidos → erros GL reais. |
| glEnable/glDisable | Espelhados: + GL_TEXTURE_2D (DEPTH_TEST continua); outros caps → 0x500. |
| Amostragem | **Rasterização real**: UV com interpolação perspectiva-corrreta (1/w), GL_NEAREST + GL_REPEAT, MODULATE (cor_vert × texel). Textura incompleta = texturing disabled (GL real). |

## 9. Detalhes do hello_gl3 (o PE)
Janela 320×240. PROJECTION: `glPushMatrix; glLoadIdentity;
glFrustum(-0.2, 0.2, -0.15, 0.15, 0.5, 20)`. MODELVIEW: `T(0,0,-3)`.
**Quad texturado** 4v/6 idx USHRT (`glDrawElements`), half-size 0.75 em z=0,
textura 2×2 embutida `GL_RGB/GL_UNSIGNED_BYTE` (ordem: (0,0)=vermelho,
(1,0)=verde, (0,1)=azul, (1,1)=branco), vértices brancos (MODULATE), NEAREST.
**Triângulo laranja (255,128,0)** flat via `glPushMatrix; glTranslatef(0,0,0.5);
glRotatef(20,0,0,1); glDisable(GL_TEXTURE_2D); glDrawArrays(GL_TRIANGLES,0,3);
glPopMatrix` (MV=T(0,0,-2.5)·Rz(20°)). Z-buffer com oclusão real (tri 0.8205 <
quad 0.8547). `glReadPixels` valida 6 pixels e `SwapBuffers` entrega o frame;
exit 42 somente com todos corretos (errado → exit ≠ 42).
Fase 2 documentada: o runtime pré-impl parou honesto em `glBindTexture`.

## 10. Comando de compilação (MinGW, PE x86_64 REAL)
```
x86_64-w64-mingw32-gcc -O2 -o Tests/PorticoRuntimeTests/data/hello_gl3.exe \
    realpe/hello_gl3.c -lgdi32 -lopengl32
```

## 11. Imports reais do hello_gl3.exe
Ver item 3 (lista completa por DLL). OPENGL32 29 fns = 19 (A) + 10 (B);
KERNEL32 11, msvcrt 24, USER32 6, GDI32 1 — todos dos subconjuntos G4–G8.
Listas C (importadas não executadas) = 0; D (não implementadas) = 0.

## 12. Fluxo completo PE → Metal (pipeline ÚNICO, sem caminho paralelo)
PE → loader (relocs+imports+thunks 0x2E) → CPU x64 (XMM capturado para
glFrustum/glRotatef/glTranslatef) → catálogo Win32 (`opengl32.dll` f_ wrappers)
→ estado GL (pilhas matrizes, texturas) → projeção perspectiva (w'=-z,
divisão perspectiva) → vertex arrays (posição+cor+UV do guest) → amostragem de
textura (perspectiva correta, NEAREST/REPEAT/MODULATE) → rasterizador
barycentric top-left + z-buffer GL_LESS → framebuffer XRGB8888 320×240 →
`pr_gl_swap_buffers` (flip y_surf=239−y_gl) → superfície WinOS (`pr_win32_surface`)
→ GfxSurfaceBuffer → `SurfacePixelCodec.xrgb8888ToBGRA8` (inalterado) →
MetalFrameUpload → **MetalGameRenderer ÚNICO** (bgra8Unorm + blit letterbox).

## 13. Pixels de validação (derivados da geometria/derivação fechada; margens)
Derivação analítica (projeção completa) + verificação computacional de que cada
ponto está fora do triângulo com margem ≥24 px de qualquer borda/fronteira de
texel/aresta:

| glReadPixels (GL coords) | Esperado | Origem | Margem |
|---|---|---|---|
| (86, 90) | (255,0,0) | texel (0,0) vermelho | 26 px |
| (235, 45) | (0,255,0) | texel (1,0) verde | 24 px |
| (93, 181) | (0,0,255) | texel (0,1) azul | 33 px |
| (219, 179) | (255,255,255) | texel (1,1) branco | 41 px |
| (166, 102) | (255,128,0) | centroide do triângulo — prova de oclusão z-buffer (abaixo dele a textura seria verde u=0.53 v=0.41) | 51 px |
| (8, 8) | (0,0,0) | cor de limpeza | — |
Superfície WinOS (flip): mesmos pixels em (x, 239−y_gl) — verificados em
`test_gl3_hello_pe` (XRGB: 0x00FF0000, 0x0000FF00, 0x000000FF, 0x00FFFFFF,
0x00FF8000, 0x00000000). Comparação específica por valor derivado — **não** é
comparação genérica "não está preto".
Nota de engenharia (registrada na derivação): os centros de texel brutos
(110,70)/(210,70) caem DENTRO do triângulo projetado — os asserts foram
re-derivados com exclusão explícita do triângulo (2 pontos falhos por essa
razão até a correção; o runtime estava correto).

## 14. Testes C
`make c-test` → **2101 verificações, 0 falhas** (G9 2024 + 77 novos).
`test_gl3.c` novo (4 suítes): matrix stack (overflow 32/4, underflow, em
glBegin), glFrustum (erros + caminho), texturas (nomes, parâmetros, formatos,
erros GL reais), glTexCoordPointer/glDrawArrays (validação), render texturado
(MODULATE provado com tint (1,1,0.5); glDisable; push/pop restaura/desloca),
PE hello_gl3 (exit 42 + 6 pixels de superfície) e honestidade (glTexImage3D/
glLightfv/glTexEnvf/glMultiTexCoord2f → PR_ERR_RANGE; D3D9 ausente).
`RUN(test_gl3)` adicionado em test_main.c. Testes G8/G9 preservados: apenas as
**âncoras de honestidade** foram trocadas pelo padrão do projeto (API real que
virou suportada → nova API GL 1.1 real ainda não suportada: glTexImage3D/
glLightfv/glTexEnvf; GL_TEXTURE_COORD_ARRAY→GL_NORMAL_ARRAY) — a intenção do
teste (API ausente = falha honesta) permanece.

## 15. Testes Swift
`swift test` → **63 testes, 0 falhas**. Nenhum teste novo (correto: SurfaceBridge
e MetalGameRenderer inalterados — teste Swift artificial proibido).

## 16. Warnings
`gcc -Wall -Wextra` (todos os `Sources/PorticoRuntime/src/*.c`): **0 warnings**.

## 17. Analyzer
`gcc -fanalyzer` (pr_gl.c, pr_win32.c, pr_peproc.c): **0 findings**.

## 18. Exit codes dos PEs (bateria completa)
| PE | exit |
|---|---|
| hello_real.exe | 42 |
| hello_user.exe | 42 |
| hello_app.exe | 42 |
| hello_input.exe | 42 (com injeção de eventos — GetMessage bloqueia com fila vazia por design) |
| hello_gdi.exe | 42 |
| hello_gl.exe | 42 (GRUPO 8 — inalterado) |
| hello_gl2.exe | 42 (GRUPO 9 — inalterado) |
| **hello_gl3.exe** | **42 (GRUPO 10 — novo)** |
(hello_stdio.exe é fixture de stdout com exit 5 projetado — fora da bateria.)

## 19. Limitações (reais, sem mascarar)
- GL: subconjunto — textura 2D level 0 RGB888, GL_NEAREST+REPEAT, MODULATE;
  sem LINEAR/mipmaps/blend/luz/alpha/stencil/display lists/glTexCoord2f.
- glTexImage2D: só GL_RGB/GL_UNSIGNED_BYTE/border 0; glDrawArrays: só
  GL_TRIANGLES; glTexCoordPointer: size 2 GL_FLOAT.
- Nomes de textura limitados a 63 por contexto (erro 0x501 acima — documentado).
- Erro GL real + log para valores válidos do GL mas fora do subconjunto
  (ex.: GL_LINEAR) — padrão honesto já usado em G8/G9 (nunca sucesso falso).
- Testes G8/G9: o CAMINHO certificado (raster/matrizes/z) é byte-idêntico; as
  âncoras de honestidade migraram para APIs ainda não suportadas (preservando a
  intenção — exigido para as features novas existirem).
- **NÃO declarada compatibilidade geral com OpenGL, nem com GTA V, MX Bikes ou
  qualquer jogo comercial.** D3D9 continua inteiramente não implementado.

## 20. Próximo bloqueador técnico REAL
O próximo PE real mais complexo vai exigir **categorias ainda ausentes**:
provavelmente `glBlendFunc`/alpha (transparência), `glLightfv`/`glMaterialfv`
(iluminação) ou mipmap/GL_LINEAR — **não serão implementadas por antecipação**
(regra do projeto): o próximo grupo deve seguir FASE 1–2 (inventário de imports
do 4º PE + parada honesta) e implementar só o que ele alcançar. Além do GL, o
bloqueador real de escopo do projeto continua sendo a **superfície de sistema**
para aplicações maiores (mais DLLs do sistema além de msvcrt/kernel32/user32/
gdi32/winmm/opengl32, e cobertura de instruções x64 além do subconjunto atual),
sem qualquer declaração de compatibilidade com software comercial.
