# RELATÓRIO FINAL — GRUPO 14 (descoberta real de bloqueador + glGetIntegerv)

Data: 2026-09-24. Metodologia: código/execução real como fonte de verdade;
descobrir o PRIMEIRO bloqueador por execução real; implementar SOMENTE o
necessário; evidência ANTES de cada implementação; valores = capacidades
REAIS do runtime (nunca inventados); sem re-implementar APIs funcionais.

hello_gl7.exe (MinGW `-O2 -lgdi32 -lopengl32`, 124.936 bytes) é o PE novo da
linha hello_gl6/hello_gl7: contexto GL -> glViewport -> 5 consultas
glGetIntegerv -> textura 1024x2 (glTexCoord2f imediato) -> luz GL_LIGHT7 ->
provas de pilha/limite -> SwapBuffers. Uso OBSERVÁVEL de cada valor: posições
de assert derivadas do viewport consultado; textura com maxt colunas + prova
de teto (maxt ok, maxt+1 -> GL_INVALID_VALUE); luz de indice (GL_MAX_LIGHTS-1)
com difusa azul -> pixel (0,0,255); push M-1/P-1 com glGetError==0 e limite
real provado (overflow/underflow). Certo = exit 42; errado = 20..39.

---

## 1. C antes / depois

| | antes (G13) | depois (G14) |
|---|---|---|
| verificações | 2638 | **2866** (+228 de `test_gl7.c`) |
| falhas | 0 | **0** |

`make c-test`: `2866 verificações, 0 falhas`.

## 2. Swift antes / depois

| | antes | depois |
|---|---|---|
| testes | 63 | **63** |
| falhas | 0 | **0** |

`Executed 63 tests, with 0 failures` (toolchain 6.1.2). Camada Swift intocada.

## 3. Warnings (-Wall -Wextra)

**0 antes e depois.** Verificação por arquivo (`-fsyntax-only -Wall -Wextra`
em cada `.c` do runtime, testes e drivers) = 0; o build de `make c-test`
(`-Wall -Wextra -Werror=implicit-function-declaration`) também limpo.

## 4. Analyzer findings (`gcc -fanalyzer`)

**0 antes e depois** sobre `Sources/PorticoRuntime/src/*.c` (fontes do G14
incluídas: `pr_gl.c`, `pr_win32.c`, `pr_cpu64.c`).

## 5. PRIMEIRO bloqueador real (descoberto por execução)

**`Unsupported Win32 API | OPENGL32.dll | glGetIntegerv`** — a carga de
`hello_gl7.exe` parou em `pr_peproc_prepare` com a API conhecida do módulo,
mas sem implementação no catálogo. glGetIntegerv era o alvo do grupo e estava
confirmado ausente desde a recertificação (FASE 1: 0 refs em
`pr_win32.c`/`pr_gl.c`/`pr_gl.h`).

## 6. Evidência da descoberta ANTES da implementação

Toda parada foi registrada ANTES de qualquer alteração; os bloqueadores
seguintes apareceram por execução real APENAS após o anterior sair do caminho:

**(a) glGetIntegerv (1º):**
```
prepare falhou: EXECUTION STOPPED
Reason: Unsupported Win32 API | Module: OPENGL32.dll | Function: glGetIntegerv
Address: (—) | Architecture: x86-64
Technical: API conhecida do módulo mas sem implementação
```
Estado do runtime: loader em `pr_peproc_prepare` resolvendo a tabela de
imports. Chamada que provocou a parada: carga de `hello_gl7.exe`. Sem
RIP/opcode (é import não resolvido, não instrução). Pnames pretendidos no
fonte do PE: GL_VIEWPORT/GL_MAX_TEXTURE_SIZE/GL_MAX_LIGHTS/
GL_MAX_MODELVIEW_STACK_DEPTH/GL_MAX_PROJECTION_STACK_DEPTH.

**(b) glTexCoord2f (2º, exposto pelo (a)):**
```
prepare falhou: EXECUTION STOPPED | Module: OPENGL32.dll | Function: glTexCoord2f
Technical: API conhecida do módulo mas sem implementação
```
Nunca existiu no runtime (G10 só implementou texcoords por ARRANJO
`glTexCoordPointer`; o caminho imediato não tinha UV corrente).

**(c) PCMPEQD (3º, exposto pela execução até a query):**
```
Technical: rip=0xFDF991 opcode=0x0F addr=0x00000000 bytes=66 0F 76 C0 B9 A2 0B 00
```
`66 0F 76 C0` = `pcmpeqd xmm0, xmm0` (SSE packed compare) — emitido pelo GCC
real em `-O2` para inicializar `GLint vp[4] = {-1,-1,-1,-1}`; seguia
`mov ecx, 0x0BA2` (GL_VIEWPORT) carregando a primeira consulta. Estado: CPU
em fault no interpretador (69-76 chamadas de API já executadas), execução do
`hello_gl7.exe`. Opcode 0x0F + secundário 0x76 não pertenciam ao subconjunto.

**(d) `raster_list` zerava UV (defeito real, achado pelo uso observável):**
com glTexCoord2f implementado, o PE rodou até o fim mas o assert da textura
falhou (exit 29); depuração por cores codificadas em exit codes mostrou
amostragem sempre na linha 0 (u,v = 0): `raster_list` (caminho imediato)
copiava o vértice e em seguida zerava `u,v` (`tmp[k].u = 0.0f; ...`),
resquício do G10. Registrado e corrigido antes do teste passar.

## 7. API implementada

**glGetIntegerv** (`OPENGL32.dll`) — o alvo do grupo:
- `pr_gl.h`: `void pr_gl_get_integer_v(pr_gl_state* s, unsigned name, int* out);`
- `pr_gl.c`: corpo real (switch dos pnames; erros GL reais)
- `pr_win32.c`: `f_glGetIntegerv` (resolve 16 bytes p/ GL_VIEWPORT, 4 p/
  resto; ponteiro nulo -> `PR_ERR_FAULT`; `n < 2` -> `PR_ERR_INVALID`) +
  entrada `IMPL("opengl32.dll", "glGetIntegerv", f_glGetIntegerv, 8)`.

Exigidos pela MESMA execução real (mesma metodologia, evidência (b)/(c)/(d)):
- **glTexCoord2f** — `pr_gl_tex_coord2f` (UV corrente `cur_uv`, default 0,0;
  **begin-safe** como glNormal3f/glColor3f em GL real; levado por
  `pr_gl_vertex3f`) + `f_glTexCoord2f` (2 floats XMM0-1) + catálogo.
- **PCMPEQD** (`66 0F 76 /r`) — semântica real em `pr_cpu64.c`: 4 comparações
  dword -> 0xFFFFFFFF/0x00000000 por dword; flags intocadas; exigido
  `p66` sem prep.
- **Correção de `raster_list`** — preserva `u,v` do vértice imediato e
  calcula `iw = 1/w_clip` real (antes: zeros hardcoded = caminho de textura
  imediata não funcional).

NÃO reimplementada nenhuma API já funcional (ver item 15).

## 8. pnames efetivamente implementados

**Somente os 5 efetivamente consultados pelo PE** (lista inteira de pnames NÃO
foi implementada; glGetFloatv/glGetDoublev continuam ausentes):

| pname | enum | consultado pelo PE |
|---|---|---|
| GL_VIEWPORT | 0x0BA2 | sim |
| GL_MAX_LIGHTS | 0x0D31 | sim |
| GL_MAX_TEXTURE_SIZE | 0x0D33 | sim |
| GL_MAX_MODELVIEW_STACK_DEPTH | 0x0D36 | sim |
| GL_MAX_PROJECTION_STACK_DEPTH | 0x0D38 | sim |

Qualquer outro pname -> comportamento GL honesto: **GL_INVALID_ENUM (0x0500)**
com a **saída INTOCADA**. Em `glBegin` -> **GL_INVALID_OPERATION (0x0502)**,
saída intacta. Sem contexto -> retorno sem escrita. Nunca lixo, nunca
fingindo suporte.

## 9. Valores retornados por pname (capacidades REAIS)

| pname | valor | evidência da capacidade real no código |
|---|---|---|
| GL_VIEWPORT | `(vp_x, vp_y, vp_w, vp_h)` reais; default **(0,0,320,240)** | `pr_gl.c:124` estado; `:223-224` default 320x240; `pr_gl_viewport` atualiza |
| GL_MAX_TEXTURE_SIZE | **1024** | `pr_gl_tex_image2d`: `width > 1024 || height > 1024 -> GL_INVALID_VALUE` (prova no teste: 1024 ok / 1025 -> 0x501) |
| GL_MAX_LIGHTS | **8** | `lights[8]` (`pr_gl.c:145`), GL_LIGHT0..GL_LIGHT7 |
| GL_MAX_MODELVIEW_STACK_DEPTH | **32** | `mv_stack[32][16]`; `mv_sp >= 32` -> GL_STACK_OVERFLOW (0x0503); pop vazio -> GL_STACK_UNDERFLOW (0x0504). Convenção: valor = nº de `glPushMatrix` acumuláveis (32 cabem; o 33o overflows) |
| GL_MAX_PROJECTION_STACK_DEPTH | **4** | `pj_stack[4][16]`; `pj_sp >= 4` -> 0x0503 (4 cabem; o 5o overflows) |

Os valores são PROVADOS como limites reais por teste (overflow/underflow/teto
de textura), não declarados: `test_gl7_unidade` empurra 32/4 vezes (ok),
33a/5a (0x503), pops extra (0x504), textura 1024 (ok) / 1025 (0x501).

## 10. Arquivos modificados / criados

| arquivo | mudança |
|---|---|
| `Sources/PorticoRuntime/include/portico/pr_gl.h` | decls `pr_gl_get_integer_v`, `pr_gl_tex_coord2f` |
| `Sources/PorticoRuntime/src/pr_gl.c` | `pr_gl_get_integer_v` (5 pnames + erros); `pr_gl_tex_coord2f` + `cur_uv`; `pr_gl_vertex3f` leva UV corrente; `raster_list` preserva UV + `iw=1/w` real |
| `Sources/PorticoRuntime/src/pr_win32.c` | `f_glGetIntegerv` + `f_glTexCoord2f` + catálogo (2 entradas novas) |
| `Sources/PorticoRuntime/src/pr_cpu64.c` | PCMPEQD real (`66 0F 76 /r`) no bloco SSE |
| `Tests/PorticoRuntimeTests/test_gl7.c` | **criado** — 228 checks (unidade + render + hello_pe) |
| `Tests/PorticoRuntimeTests/test_main.c` | `RUN(test_gl7)` |
| `Tests/PorticoRuntimeTests/test_gl5.c` | âncora migra: glGetIntegerv (agora real) -> glGetFloatv (ainda ausente) |
| `Tests/PorticoRuntimeTests/test_gl6.c` | idem |
| `realpe/hello_gl7.c` | **criado** — PE de descoberta/validação |
| `Tests/PorticoRuntimeTests/data/hello_gl7.exe` | **criado** (MinGW `-O2 -lgdi32 -lopengl32`) |
| `build/hello_gl7_dbg.c` (+ .exe) | descartável de depuração (sondas de cor) |

## 11. Novo PE criado

`hello_gl7.exe` (e fonte `realpe/hello_gl7.c`): 5 pnames com uso observável —
asserts de pixel derivados do viewport consultado; textura real de
`GL_MAX_TEXTURE_SIZE` colunas x 2 linhas desenhada por glTexCoord2f imediato
(azul em v=0.25, verde em v=0.75, margens 60px) + prova de teto (maxt+1 ->
0x501); luz de indice `GL_MAX_LIGHTS-1` (GL_LIGHT7) difusa azul -> centro
(0,0,255) e fora preto; provas das pilhas `GL_MAX_*_STACK_DEPTH`
(overflow/underflow reais). Exit 20..39 para cada falha; **42 = tudo certo**.

## 12. Resultado do novo PE

**`exited=1 rc=42 log=194`** — usa os 5 valores observavelmente (nada de
"chama e ignora"): os asserts de valor (20-25), pixels derivados de vp (28-31)
e as provas de limite (32-39) todos passam; textura e luz visíveis na
superfície final (centro L7 azul #000000FF, fora preto; 19.200 px acesos).

## 13. Resultado de TODOS os PEs anteriores

| PE | antes (G13) | depois (G14) |
|---|---|---|
| hello_real | 42 (log 28) | **42** (log 28) |
| hello_user | 42 (log 43) | **42** (log 43) |
| hello_app | 42 (log 180) | **42** (log 180) |
| hello_gdi | 42 (log 70) | **42** (log 70) |
| hello_gl | 42 (log 72) | **42** (log 72) |
| hello_gl2 | 42 (log 75) | **42** (log 75) |
| hello_gl3 | 42 (log 98) | **42** (log 98) |
| hello_gl4 | 42 (log 118) | **42** (log 118) |
| hello_gl5 | 42 (log 76) | **42** (log 76) |
| hello_gl6 | 42 (log 81) | **42** (log 81) |
| hello_input | 42 (log 105) | **42** (log 105) |

Bateria completa = **11 PEs anteriores a 42** (contagens de log idênticas) +
`hello_gl7` 42. `make c-test` 2866/0. Swift 63/0.

## 14. Próximo bloqueador real descoberto

**NENHUM descoberto por execução neste grupo**: `hello_gl7.exe` executou até o
fim (exit 42) — não há próxima parada real registrada. Os candidatos NÃO
executados (portanto NÃO implementados, FASE 6) seguem honestamente fora:
glGetDoublev, glFogf, glTexEnvf, glShadeModel, glMultMatrixf, glLoadMatrixf,
glDepthFunc, glDepthMask, demais aritmética packed/SSE, Direct3D/D3D9, Vulkan,
áudio, threads. glGetFloatv virou a nova âncora de honestidade (ausente) em
test_gl5/test_gl6/test_gl7. O próximo bloqueador será descoberto pelo próximo
PE real, com a mesma metodologia.

## 15. Confirmação: nenhuma API funcional reimplementada

Confirmado: **nenhuma** API já funcional foi tocada — `pr_gl_get_string`,
`pr_gl_get_error`, `pr_gl_scalef`, `pr_gl_draw_elements`, `pr_gl_translatef`,
`pr_gl_rotatef`, `pr_gl_blend_func`, `pr_gl_color3f`, `pr_gl_lightfv`,
`pr_gl_material_fv`, `pr_gl_normal3f`, `pr_gl_normal_pointer`, vertex arrays,
textura por ponteiro (G10), loader/relocations/imports e o pipeline único
(MetalGameRenderer/SurfaceBridge) permanecem intactos — a regressão byte a
byte dos 11 PEs anteriores (mesmos exits e contagens de log) confirma. As
mudanças em `pr_gl.c` são ADIÇÕES (2 APIs novas + campo `cur_uv`) e UMA
correção de defeito real (`raster_list` zerava `u,v` do caminho imediato —
caminho que nunca esteve funcional para textura imediata; o caminho por
arranjo de `test_gl3`/`hello_gl3` não foi alterado).

---

### FASE 6 — lista "não implementar" (em vigor, salvo execução real futura)

glGetDoublev, glFogf, glTexEnvf, glShadeModel, glMultMatrixf, glLoadMatrixf,
glDepthFunc, glDepthMask, addps/outras SSE packed (exceto PCMPEQD, exigido),
Direct3D/D3D9, Vulkan, áudio, threads, sincronização.

### Notas de honestidade

- glGetIntegerv implementado SOMENTE após o STOP registrado (item 6a);
  glTexCoord2f e PCMPEQD idem (6b/6c), pela cláusula "a menos que uma execução
  real demonstre ser o próximo bloqueador".
- Valores retornados = capacidades REAIS provadas por limite (item 9); nenhum
  valor genérico/inventado para fazer o PE avançar.
- Sem alegação de compatibilidade com GTA V, MX Bikes, jogos comerciais ou
  OpenGL/D3D9 completos. Componente isolado; sem código proprietário.
