# RELATÓRIO — GRUPO 11 (quarto PE 3D real: iluminação + blending)

## 1. O grupo foi concluído?
**SIM.** Todos os critérios de conclusão cumpridos: hello_gl4.exe real (MinGW) executa a 42; o primeiro bloqueador foi registrado ANTES de qualquer implementação (item 5); só as APIs realmente alcançadas foram implementadas; comportamento real verificável (iluminação com fórmula completa, blend com fórmula real, normal por array); pixels derivados com margens ≥23 px validados (6 asserts no PE + suites de teste); hello_gl4 = 42 e os 8 PEs anteriores = 42 (bateria 9×42); c-test 2436/0; swift test 63/0; 0 warnings sob `-Wall -Wextra`; 0 findings em `-fanalyzer`. `hello_gl3.exe` permanece byte-identical (md5 `c6013978cfb22cfb8312dec93aa090dc`).

## 2. Estado inicial e final
- **Inicial (após G10 certificado)**: C 2101/0, Swift 63/0; OpenGL de função fixa com 38 funções reais (primitivas, arrays vertex/color/texcoord, texturas 2D RGB com MODULATE, matrix stack PROJECTION/MODELVIEW com glFrustum, viewport, z-buffer, glReadPixels, SwapBuffers GDI→SurfaceBridge); iluminação **ausente**; blending **ausente**; normais **ausentes**; `glColor4f` **ausente**.
- **Final**: C **2436/0** (+335 checks), Swift 63/0; catálogo opengl32 com **44 funções** (50 implementadas contando sinônimos); novidades: `glNormal3f`/`glNormalPointer` (array de normais), `glColor4f` (com alpha preservado), `glLightfv`/`glMaterialfv` (Blinn-Phong eye-space), `glBlendFunc` (4 fatores), `GL_LIGHTING`/`GL_LIGHT0..7`/`GL_BLEND`/`GL_NORMAL_ARRAY`; Gouraud com interpolação de cor por vértice; blend float pós-MODULATE com z-teste antes; PEs G8/G9/G10 byte-idênticos (caminhos novos só entram com lighting/blend ON).

## 3. Verificações C
`make c-test` → **2436 verificações, 0 falhas** (2101 base G10 + 335 de `test_gl4.c`). Nenhum teste existente removido ou enfraquecido; 4 âncoras de não-suporte foram trocadas por recursos ainda não suportados preservando a intenção (ver §9).

## 4. Testes Swift
`swift test` → **63 testes, 0 falhas** (GLBridgeTests). Nada na cadeia SurfaceBridge→BGRA8→Metal mudou (o blend ocorre no framebuffer XRGB8888 do runtime, antes do swap), portanto nenhum teste Swift artificial foi adicionado — conforme a regra.

## 5. Primeiro bloqueador (registrado ANTES de qualquer implementação G11)
Executado hello_gl4.exe no runtime do G11 (ainda sem nenhuma função nova):

```
peproc: Unsupported Win32 API — OPENGL32.dll!glBlendFunc @ (—)
```

parada honesta em `pr_peproc_prepare` (import não resolvido). Registrado em log e no avanço do trabalho **antes** de escrever qualquer implementação G11. Inventário objdump prévio (A/B/C/D): `glBlendFunc glColor4f glLightfv glMaterialfv glNormal3f glNormalPointer` eram as 6 funções ainda ausentes que o PE importa.

## 6. APIs importadas pelo hello_gl4.exe (objdump)
| DLL | n | detalhe |
|---|---|---|
| GDI32.dll | 1 | SwapBuffers |
| OPENGL32.dll | 30 | 24 já existentes (A) + **6 novas (B)**: `glBlendFunc glColor4f glLightfv glMaterialfv glNormal3f glNormalPointer` |
| USER32.dll | 6 | já existentes |
| KERNEL32.dll | 11 | já existentes |
| msvcrt.dll | 24 | já existentes (signal = stub com WARNING) |
| C (instâncias novas de módulos já usados) | 0 | — |
| D (DLLs nunca vistas) | 0 | — |

## 7. APIs já implementadas (A) — 24 em OPENGL32
glBegin/glEnd/glVertex3f/glColor3f/glClear/glClearColor/glEnable/glDisable/glViewport/glMatrixMode/glLoadIdentity/glFrustum/glFlush/glGetError/glReadPixels/glGenTextures/glBindTexture/glTexImage2D/glTexCoord2f/glDrawArrays/glEnableClientState/glDisableClientState/glVertexPointer/glColorPointer/glTexCoordPointer + WGL (wglCreateContext/wglMakeCurrent/wglDeleteContext/wglGetProcAddress) + SwapBuffers.

## 8. Novas APIs (B) — 6
`glNormal3f`, `glNormalPointer`, `glColor4f`, `glLightfv`, `glMaterialfv`, `glBlendFunc`.

## 9. APIs implementadas neste grupo
As 6 do item 8, com wrappers `f_gl*` + catálogo (`pr_win32.c`) e motor real (`pr_gl.c`):
- `pr_gl_normal3f` (IMPL_FX 0x07, begin-safe), `pr_gl_color4f` (IMPL_FX 0x0F, begin-safe),
- `pr_gl_normal_pointer` (GL_FLOAT; stride 0; GL_DOUBLE→0x500; stride inválido→0x501),
- `pr_gl_lightfv` (GL_LIGHT0..7; POSITION transformada pela MODELVIEW na chamada; DIRECTIONAL w=0; att 1/(c+l·d+q·d²); SPOT→0x500+log),
- `pr_gl_materialfv` (AMBIENT/DIFFUSE/SPECULAR/EMISSION/SHININESS [0,128]; GL_BACK→0x500+log; SHININESS fora da faixa→0x501),
- `pr_gl_blend_func` (GL_ZERO/GL_ONE/GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA; outros→0x500+log; dentro de begin→0x502),
- estados `GL_LIGHTING`, `GL_LIGHT0..7`, `GL_BLEND` em enable/disable (luz ≥8→0x500), `GL_NORMAL_ARRAY` em client state.

Âncoras de teste trocadas (intenção preservada, padrão dos grupos anteriores):
- `GL_LIGHTING` (virou real) → `GL_CULL_FACE` 0x0B44 como enable fora do subconjunto;
- `glLightfv` (virou real) → `glFogf` 0x0B60/0x0B62 como lookup ausente + call PR_ERR_RANGE (+ `glAlphaFunc`);
- `GL_NORMAL_ARRAY` (virou real) → `GL_EDGE_FLAG_ARRAY` 0x8079.

## 10. Ainda ausentes (parada honesta se alcançadas)
`glFogf`/`glFogfv`/`glAlphaFunc`/`glCullFace`/`glLightModeli`/`glTexEnvf`/`glTexImage3D`/`glMultiTexCoord2f`/`glDrawElements`/`glTranslatef`/`glRotatef`/`glScalef`/`glGet*`/display lists/`glShadeModel`/`glTexGen*`/GLSL/D3D9 — todos com lookup NULL e chamada direta → `PR_ERR_RANGE` (verificado em `test_gl4_honestidade`). Nenhuma API nova é no-op ou sucesso falso.

## 11. Comportamento real das novas
- **Iluminação (Gouraud Blinn-Phong eye-space)** por vértice: `emiss + mat.amb·globalAmb(0.2,0.2,0.2,1) + Σ_luzes [att·amb·mat.amb + max(N·L,0)·att·dif·mat.dif + (espelho) att·pow(N·H,shine)·esp·mat.esp]`; normal levada pela MODELVIEW 3×3 **sem** normalizar; posição da luz transformada pela MODELVIEW na chamada (GL real); direcional w=0 com att=1; posição (0,0,1,0) default = luz direcional frontal. Interpolação de cor por vértice (barycentric 1/w) — verificado com N=0/1 → 0/255 exatos e (0.6,0,0.8)→204, somando ambiente 0.2+0.25=0.45→115 e especular pow(1,32)·1·0.5=0.5→128.
- **Blend real**: `src·sf + dst·df` em float (dst = fb/255) aplicado pós-MODULATE, com z-teste **antes** (overlay atrás rejeitado → pixel intocado). `glColor3f` **não** zera o alpha (GL real): `glColor4f(α) + glColor3f` mantém α na mistura.
- **`glNormal3f` é begin-safe** (atributo de vértice como glNormal/glColor/glTexCoord/glVertex dentro de glBegin — correção de um bug encontrado pelo próprio PE); `glMaterial/glLight/glBlendFunc` dentro de begin → `GL_INVALID_OPERATION` (0x502).
- Defaults GL reais em `wglCreateContext`: material amb 0.2/dif 0.8, luz 0 difusa branca posicional com att constante 1, luzes 1..7 apagadas, alpha=1.

## 12. Detalhes do hello_gl4.c
- Projção `glFrustum(-1.2,1.2,-0.9,0.9,1,10)`, viewport 320×240, MODELVIEW transladada (0,0,-3.5).
- Cena: quad ESQ immediate (−0.9..−0.1, ±0.55) com N constante (0,0,1) → vermelho puro 255; quad DIR (0.1..0.9) via **glNormalPointer** + glDrawArrays com N=(0.6,0,0.8) → 204,0,0 (sem especular); overlay BLEND frente (azul, **α=0.25** exato binário) sobre o ESQ → (191,0,64) = 0xBF40; overlay BLEND trás (verde) sobre o DIR → rejeitado pelo z-teste (depths 0.8488 < 0.8547 < 0.8602) → (204,0,0).
- glReadPixels → `fwrite("saida.ppm")` via CRT escrito no VFS do runtime (nome de arquivo Windows do PE); os asserts validam os pixels lidos por glReadPixels na memória; SwapBuffers; **6 asserts de pixel** com margens ≥23 px (regiões 54×135, 40×40, 56×56), pontos derivados com teste ponto-em-triângulo + barycentric exato; errado → exit 20..25, glGetError malformado → 6..8, certo → **42**.
- Imports ligados com `-lgdi32 -lopengl32` (GDI/USER/KERNEL32/msvcrt reais).

## 13. Comando de compilação
```
x86_64-w64-mingw32-gcc -O2 -o Tests/PorticoRuntimeTests/data/hello_gl4.exe realpe/hello_gl4.c -lgdi32 -lopengl32
```

## 14. Imports por DLL
ver item 6 (A=24, B=6, C=0, D=0).

## 15. Fluxo PE → Metal
`hello_gl4.exe` → PE loader (`pr_peproc`) → x86-64 (SSE2/XMM) → catálogo Win32 (`pr_win32`, trap 0x2E) → OpenGL subconjunto (`pr_gl`: rasterização → **iluminação/blend no framebuffer**) → z-buffer → **XRGB8888 320×240** → `pr_gl_swap_buffers` (flip) → `pr_win32_surface` → `GfxSurfaceBuffer.update` → `SurfacePixelCodec.xrgb8888ToBGRA8` → `MetalFrameUpload.stage` → `MetalGameRenderer` (.bgra8Unorm/replaceRegion/blit letterbox). Pipeline único; nenhum renderer paralelo; janela Windows = superfície interna.

## 16. Pixels de validação
| região (gl) | esperado | por quê |
|---|---|---|
| retângulo ESQ | (255,0,0) | N=(0,0,1) difusa plena sem especular |
| retângulo DIR | (204,0,0) | f2b(0.8)=204, sem especular (N≠V) |
| 56×56 ESQ | (191,0,64) | blend α=0.25: 255·0.75=191.25→191; azul 255·0.25=63.75→64 |
| 40×40 DIR | (204,0,0) | overlay atrás rejeitado pelo z-teste |
| 30×30 topo | (0,0,0) | fora da cena |
| fundo 320×240 | (0,0,0) | limpo |
Asserts 20..25 no PE (rc=42). Suites `test_gl4_light_render`/`blend_render` revalidam 255/204/0, ambiente 0.45→115, especular 0.5→128, ONE/ZERO e ONE/ONE (255,0,255), preservação de alpha por glColor3f e z×blend.

## 17. Exit codes
| code | significado |
|---|---|
| 42 | sucesso (todos os asserts) |
| 6/7/8 | glGetError anormal após setup/quads/arrays |
| 11 | glReadPixels falhou |
| 20..25 | assert de pixel divergente (ESQ/DIR/FRENTE/TRÁS/TOPO/POSTERIOR) |
| 12 | SwapBuffers falhou |
| STOP | API/instrução não suportada → EXECUTION STOPPED com diagnóstico (nunca sucesso) |

## 18. c-test
`make c-test` → **2436 verificações, 0 falhas** (suítes: …, test_gl, test_gl2, test_gl3, **test_gl4 (335 checks: unidade de erros GL reais, render de iluminação, render de blend, PE real 42 + 6 asserts, honestidade das não suportadas)**, …).

## 19. swift test
`swift test` → **63 testes, 0 falhas**.

## 20. Warnings
**0 sob `-Wall -Wextra`** (barra do critério; build do driver + `src/*.c` + make c-test limpos). Avisos adicionais de `-Wconversion/-Wshadow` introduzidos em G11: **zero** (os triviais dos arquivos tocados foram corrigidos); residual pré-existente do G8 em `pr_cpu.c` (16× `-Wsign-conversion`, fora da barra, código validado intocado).

## 21. Analyzer
`gcc -fanalyzer` sobre `pr_gl.c`/`pr_win32.c`/`pr_peproc.c` → **0 findings**.

## 22. Limitações (12 categorias)
1. **Gráficas**: OpenGL = subconjunto de função fixa (44 funções; GL 1.0/1.1 parcial); sem shaders/GLSL, sem extensões, sem multitextura, sem display lists, sem MSAA. **D3D9 não implementado**. Metal: renderer próprio (não MoltenVK); buffer final XRGB8888→BGRA8 documentado.
2. **CPU/x64**: subconjunto x86-64 (SSE2 + AVX parcial); sem 32-bit, x87 completo ou AVX-512; instrução não suportada → EXECUTION STOPPED (opcode/RIP/bytes).
3. **DLLs**: só módulos do catálogo (kernel32/user32/gdi32/opengl32/gdi helpers/msvcrt/winsock parcial) + imagens `provide_dll`; sem carregamento arbitrário de DLLs desconhecidas.
4. **Win32**: subconjunto do catálogo; janela = superfície interna 320×240 (não nativa); sem COM, registry, SEH completo.
5. **Filesystem**: VFS sobre o sandbox iOS; sem acesso fora do sandbox.
6. **Threads**: guest single-thread sem preemptividade.
7. **Sincronização**: sem waitable objects completos/IPC; winsock limitado (htonl/htons/ntohl/ntohs certificados; demais ordinais documentados).
8. **Áudio**: pipeline básico do G7; sem DirectSound.
9. **Input**: injeção determinística (teclado/mouse/touch/game controller genérico); sem raw input/XInput.
10. **Memória**: heap/próprio `pr_vm`; sem memória compartilhada/DEP completo.
11. **Performance**: sem JIT (interpretador); PEs controlados pequenos — não é runtime para jogos comerciais.
12. **Shaders/3D**: apenas pipeline fixo; iluminação Blinn-Phong no subconjunto (sem SPOT/GL_BACK lighting); blend com 4 fatores (ZERO/ONE/SRC_ALPHA/ONE_MINUS_SRC_ALPHA); sem glDrawElements/glTranslate/glRotate/glGet*. **NÃO se afirma compatibilidade com GTA V, MX Bikes ou qualquer jogo comercial, nem compatibilidade geral com OpenGL ou D3D9.**

## 23. Próximo bloqueador técnico REAL
Previsto por análise do catálogo (a confirmação exige um 5º PE controlado — não executado neste grupo): **desenho indexado e transformação de modelo**. Um app 3D de médio porte usa nas primeiras centenas de chamadas `glDrawElements` + `glTranslatef`/`glRotatef`/`glScalef` (e consultas `glGetString`/`glGetIntegerv`), e todos esses estão **ausentes** do catálogo hoje — a primeira chamada pararia com `Unsupported Win32 API`. Em seguida viriam `glTexParameteri` real (GL_LINEAR/mipmaps) e `glGet*`. Este grupo não os implementou por antecipação, conforme a regra.
