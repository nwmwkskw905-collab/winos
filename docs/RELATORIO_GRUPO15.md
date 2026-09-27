# RELATÓRIO GRUPO 15 — DESCOBERTA DO PRÓXIMO BLOQUEADOR REAL POR EXECUÇÃO

Data: 2026-09-24 · PorticoRuntime (C11) · PorticoCore (Swift 6.1.2) · validação por PEs de controle (hello_*.exe)

## Escopo (resumo)

Objetivo do grupo: **descobrir o próximo bloqueador real por EXECUÇÃO** (a execução determina; não escolher API por antecipação) usando um novo PE natural/pequeno/determinístico/observável que combine recursos **já existentes**; implementar **SOMENTE** o primeiro bloqueador com evidência registrada **antes** da implementação; terminar em **exit 42**; relatar o próximo bloqueador descoberto. NÃO implementar por antecipação: glGetFloatv, glGetDoublev, glFogf, glTexEnvf, glShadeModel, glMultMatrixf, glLoadMatrixf, glDepthFunc, glDepthMask, SSE packed extra, D3D9, Vulkan, áudio, threads, sincronização (a não ser que a execução real mostre ser o PRIMEIRO bloqueador).

---

## Os 15 itens

### 1. Resultado do build/teste C antes e depois

| | Antes (FASE 1) | Depois (FASE 6) |
|---|---|---|
| `make c-test` (gcc -Wall -Wextra -Werror=implicit-function-declaration) | **2866 verificações, 0 falhas** | **2897 verificações, 0 falhas** |

Delta: **+31 verificações** (test_gl8.c), todas as 2866 anteriores preservadas (nunca removidas/enfraquecidas).

### 2. Resultado dos testes Swift antes e depois

| | Antes | Depois |
|---|---|---|
| `swift test` (raiz do repositório, toolchain 6.1.2) | **63 testes, 0 falhas** | **63 testes, 0 falhas** (GLBridgeTests 2/2 com fixtures hello_gl/hello_gl2) |

Nenhum componente Swift foi modificado neste grupo (só quando a camada iOS/Metal precisar — não precisou).

### 3. Warnings

`gcc -Wall -Wextra` (build completo C11 + testes): **0 → 0**.

### 4. gcc -fanalyzer

`gcc -std=c11 -fanalyzer -Wall -Wextra` (src + testes): **0 issues → 0 issues**.

### 5. **PRIMEIRO bloqueador real** (descoberto por execução, não por escolha prévia)

```
prepare falhou: EXECUTION STOPPED

Reason:
Unsupported Win32 API

Module:
OPENGL32.dll

Function:
glDisableClientState

Address:
(—)

Architecture:
x86-64
```

`glDisableClientState` = o par canônico de `glEnableClientState` (já certificado), exigido pelo ciclo normal de vertex arrays do objeto C do novo PE (enable → draw → **disable** → re-layout). Não havia sido implementada em nenhum grupo anterior (os PEs anteriores nunca desligaram arranjos).

### 6. **Evidência ANTES da implementação**

Execução real de `Tests/PorticoRuntimeTests/data/hello_gl8.exe` em 2026-09-24, **antes de qualquer alteração de runtime**:

- **API**: módulo `OPENGL32.dll`; função `glDisableClientState`; endereço `(—)` (parada na resolução de imports); estado do loader: `pr_peproc_prepare` resolvendo a tabela de imports do PE; chamada: carga do `hello_gl8.exe` (a chamada em si ocorreria no ciclo enable→draw→disable do lote C).
- A evidência acima (item 5, com o texto literal do STOP) foi registrada nesta sessão ANTES de escrever `pr_gl_disable_client_state`, o wrapper ou a entrada de catálogo.
- **Segunda descoberta da mesma execução** (registrada sem implementar — "uma lacuna por vez"): após o 1º fix, bisect por sondas de `glGetError` codificadas no exit code (debug build `hello_gl8_dbg`, rc=66 → fase 2 → PEB rc=190 → k=15) isolou `glDrawElements` como fonte de `GL_INVALID_ENUM` com `GL_UNSIGNED_INT` — ver item 14.

### 7. Tipo do bloqueador

**API** (import Win32 não resolvido — não foi nem instrução nem bug).

### 8. Implementação realizada

Somente o primeiro bloqueador (menor implementação correta):

- `pr_gl_disable_client_state(pr_gl_state*, unsigned array)` em `pr_gl.c` — espelho real do lado `pr_gl_enable_client_state`: limpa `enabled` de `varr`/`carr`/`tarr`/`narr` conforme o enum (0x8074/0x8076/0x8078/0x8075); enum desconhecido → `GL_INVALID_ENUM` (0x500); sem contexto → no-op silencioso (idêntico ao enable). Nenhum comportamento do lado enable foi alterado.
- Wrapper `f_glDisableClientState` em `pr_win32.c` (1 arg; `n < 1` → `PR_ERR_INVALID`, como o par).
- Entrada de catálogo: `IMPL_NOTE("opengl32.dll", "glDisableClientState", f_glDisableClientState, 4, "par real do enable (G15)")`.
- Nenhum stub; nenhum sucesso falso; nenhuma API além desta.

### 9. Arquivos modificados (arquivo + linha + causa)

| Arquivo | O quê | Causa |
|---|---|---|
| `Sources/PorticoRuntime/src/pr_gl.c` | +`pr_gl_disable_client_state` (após o enable, ~:830) | 1º bloqueador (item 5) |
| `Sources/PorticoRuntime/include/portico/pr_gl.h` | +declaração (:51) | idem |
| `Sources/PorticoRuntime/src/pr_win32.c` | +`f_glDisableClientState` + entrada de catálogo OPENGL32 | idem |
| `realpe/hello_gl8.c` | **NOVO** — PE do grupo | FASE 2/5 |
| `Tests/PorticoRuntimeTests/data/hello_gl8.exe` | **NOVO** — PE compilado (MinGW -O2 -lgdi32 -lopengl32) | idem |
| `Tests/PorticoRuntimeTests/test_gl8.c` | **NOVO** — 31 verificações | FASE 4/5 |
| `Tests/PorticoRuntimeTests/test_main.c` | +`void test_gl8(void)` + `RUN(test_gl8)` | registro do teste |

Ajustes de fechamento do PE (FASE 5, documentados no cabeçalho do `hello_gl8.c`): índices `GL_UNSIGNED_SHORT` (subtipo certificado do lineage hello_gl3, ver item 14); overlay do HUD em `glOrtho` canônico (com frustum, vértices em z=0 seriam recortados pelo near — o PE nunca foi executado nessa forma); assert do overlay com tolerância de arredondamento f2b/bary ±1 (o blend certificado do G11 produz 127..128; a fórmula `src·sf + dst·df` foi auditada no código antes de ajustar o assert — o engine certificado é a fonte de verdade, não a minha suposição).

### 10. Testes adicionados

`Tests/PorticoRuntimeTests/test_gl8.c` (31 verificações novas):

1. **Uso observável do disable**: com `GL_COLOR_ARRAY` ligado o lote pinta com a cor do ARRANJO (amarelo 0xFFFF00 via glReadPixels); após `glDisableClientState(GL_COLOR_ARRAY)` o MESMO lote pinta com a cor ATUAL (verde 0x00FF00) — se o disable fosse no-op, o pixel continuaria amarelo.
2. Sem `GL_VERTEX_ARRAY`: draw → nada desenhado + `GL_INVALID_OPERATION` (0x502) honesto.
3. `glDisableClientState(0x9999)` → `GL_INVALID_ENUM` (0x500).
4. **Gap registrado travado**: `glDrawElements` com `GL_UNSIGNED_INT` → 0x500 + framebuffer intacto (nada desenhado); controle `GL_UNSIGNED_SHORT` desenha verde + `glGetError` = 0.
5. **PE `hello_gl8.exe`**: executa até o fim, **rc == 42**, superfície 320×240, 6 asserts de pixel derivados da geometria (ver item 12).

### 11. Novo PE

`realpe/hello_gl8.c` → `Tests/PorticoRuntimeTests/data/hello_gl8.exe` (PE de controle real, MinGW, janela 320×240). **Frame 3D natural determinístico** (sem API adicionada artificialmente — só recursos já certificados):

- Consultas: `glGetString(GL_VENDOR/GL_RENDERER/GL_VERSION)`, `glGetIntegerv(GL_VIEWPORT)` (0,0,320,240), `glGetError`.
- Cena: `glClearColor`+`glClearDepth`+`glClear(COLOR|DEPTH)`; `glFrustum(-1.6,1.6,-1.2,1.2,1,20)`; `glViewport(0,0,320,240)`; `GL_DEPTH_TEST`+`GL_LIGHTING`+`GL_LIGHT0`+`glMaterialfv(GL_SHININESS)`.
- **A (imediato, perto z=-2)**: triângulo `glBegin/glVertex3f/glNormal3f/glEnd` vermelho com fator difuso real → (255,0,0); desenhado **1º**.
- **B (arrays, longe z=-4)**: quadrado texturizado (textura procedural 2×1 branco|azul via `glTexImage2D`, `glBindTexture`, UVs) com `glVertexPointer`/`glNormalPointer`/`glTexCoordPointer` + `glDrawElements(GL_TRIANGLES,6,UNSIGNED_SHORT)` → (0,0,255) em u=2/3; desenhado **2º** — **como B é MAIS LONGE e mesmo assim não sobrescreve A na sobreposição x_gl 160..200, o assert prova o z-buffer**.
- **C (color array + `glDrawArrays`, z=-3)**: ciclo canônico de arrays: enable VERT+NORM+TEXCOORD → draw B → **`glDisableClientState`** nos três → enable VERT+COLOR → re-layout → draw C amarelo (255,255,0). **Uso observável do disable**: a textura permanece ativa; com o TEXCOORD_ARRAY desligado, C usa o uv corrente (0,0) = texel BRANCO (amarelo×branco = amarelo); se o disable fosse no-op, o array antigo amostraria o texel AZUL (amarelo×azul = preto) e o assert falharia.
- **Overlay**: `glOrtho` HUD em pixels + `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)` + `glColor4f` amarelo α=0.5 sobre preto → (128,128,0) com tolerância ±1 de arredondamento.
- `glReadPixels` por assert + `SwapBuffers`; saída 20..28 por falha específica; **exit 42** no caminho correto.

### 12. Resultado do novo PE

**`hello_gl8.exe` → exit 42** (`exited=1 rc=42 log=131`). Asserts internos 22–28 (vp, glGetString, A vermelho, **sobreposição = z-buffer provado**, B azul texturizado, C amarelo com `glDisableClientState` provado, overlay blend, fundo intacto, glGetError limpo) + 6 asserts de superfície em `test_gl8_pe` (0x00FF0000/0x00FF0000/0x000000FF/0x00FFFF00/0x00000000/0x007F7F00|0x00808000).

### 13. Resultado de TODOS os PEs anteriores

**12 PEs anteriores: todos 42, logs byte-idênticos aos da FASE 1:**

| PE | rc | log |
|---|---|---|
| hello_real.exe | 42 | 28 |
| hello_user.exe | 42 | 43 |
| hello_app.exe | 42 | 180 |
| hello_gdi.exe | 42 | 70 |
| hello_gl.exe | 42 | 72 |
| hello_gl2.exe | 42 | 75 |
| hello_gl3.exe | 42 | 98 |
| hello_gl4.exe | 42 | 118 |
| hello_gl5.exe | 42 | 76 |
| hello_gl6.exe | 42 | 81 |
| hello_gl7.exe | 42 | 194 |
| hello_input.exe | 42 | 105 |

(+ o novo hello_gl8.exe = 42/131 → **13×42** no total.)

### 14. Próximo bloqueador descoberto (registrado, NÃO implementado)

**`glDrawElements` com índices `GL_UNSIGNED_INT` (32-bit) → `GL_INVALID_ENUM` (0x500) + nada desenhado.**

- **Evidência da descoberta** (execução do debug build de hello_gl8, mesma execução do item 5, ANTES de qualquer implementação deste gap): sondas de `glGetError` codificadas no exit code → `rc=66` (fase 2 = lote B) → bisect por chamada → `rc=190` (PEB k=15 = logo após `glDrawElements`); chamadas 1–14 da fase limpas.
- **Código**: `pr_gl.c` — `pr_gl_draw_elements`: `if (type != GL_UNSIGNED_SHORT) { set_error(g, GL_INVALID_ENUM); return; }` (o subtipo certificado é `GL_UNSIGNED_SHORT`, exatamente como `hello_gl3`). O erro é **honesto** (sem crash, sem sucesso falso) e está **travado em teste** (test_gl8.c: 0x500 + framebuffer intacto).
- Decisão: conforme FASE 5 ("uma lacuna por vez"), **não implementado** neste grupo; o PE foi alinhado ao subtipo certificado do lineage (GL_UNSIGNED_SHORT). Implementar `GL_UNSIGNED_INT` (com checagens de range/resolve 4 bytes) é o próximo passo natural quando um PE real exigir.

Demais lacunas decididas (não exigidas por execução — a execução determina, não a vontade prévia): `glGetFloatv`, `glGetDoublev`, `glFogf`, `glTexEnvf`, `glShadeModel`, `glMultMatrixf`, `glLoadMatrixf`, `glDepthFunc`, `glDepthMask` continuam fora do runtime; a âncora de honestidade de `glGetFloatv`/`glGetDoublev`/`glFogf` segue viva nos testes (PR_ERR_RANGE, retorno 0). Depth real permanece o certificado (`GL_DEPTH_TEST`→`depth_test`, `glClear` COLOR|DEPTH, comparação `GL_LESS`).

### 15. Confirmação: nenhuma API funcional anterior foi reimplementada

Confirmado. Foram respeitadas intocadas todas as APIs funcionais já certificadas (glGetString, glGetIntegerv, glGetError, glScalef, glDrawElements, glTranslatef, glRotatef, glBlendFunc, glColor4f, glLightfv, glMaterialfv, glNormal3f, glNormalPointer, glTexCoord2f e todas as demais do catálogo), o fix de `raster_list` permanece intacto, e as 2866 verificações anteriores passam sem alteração (apenas ADIÇÃO de 31). Os relatórios históricos (GRUPO 8–14) não foram alterados. Builders v0–v13 preservados. Nenhum componente proprietário; nenhuma afirmação de compatibilidade Windows/OpenGL geral nem de GTA V/MX Bikes; nenhum hack específico para o PE dar 42 (o PE falha com exits 20–28 em cada cenário de quebra; o gap do item 14 permanece registrado e testado como erro honesto).

---

## Fases executadas

| Fase | Status | Evidência |
|---|---|---|
| 1. Registra estado atual | ✓ | `make c-test` 2866/0; 12 PEs × 42 (logs idênticos); Swift 63/0; auditoria do catálogo OPENGL32 (45 APIs) ANTES do novo PE (glClearDepth existia; glDisableClientState ausente — entre outros) |
| 2. Cria/atualiza PE real | ✓ | `hello_gl8.c`/`.exe` — natural, pequeno (247 linhas), determinístico, observável; só APIs já existentes |
| 3. Descobre o próximo bloqueador | ✓ | STOP `OPENGL32.dll \| glDisableClientState` (itens 5–6) ANTES da implementação; 2º item registrado por bisect (item 14) |
| 4. Implementa SOMENTE o primeiro bloqueador | ✓ | `pr_gl_disable_client_state` + wrapper + catálogo (item 8) |
| 5. Testa o resultado com uso observável | ✓ | exit 42 + asserts de pixel (itens 10–12); disable observável no lote C; 2º bloqueador registrado sem implementar cadeia |
| 6. Regressão completa | ✓ | c-test 2897/0; 13 PEs × 42 (logs dos 12 anteriores idênticos); Swift 63/0; -Wall -Wextra 0; analyzer 0 |
| 7. Se não houver bloqueador | n/a | havia bloqueador real (item 5) |
| 8. Qualidade | ✓ | ver item 15 |
| 9. Relatório 15 itens | ✓ | este documento (`docs/RELATORIO_GRUPO15.md`) |

## Limitações conhecidas do runtime

- Conjunto OpenGL 1.1 parcial e explícito: 46 APIs certificadas no catálogo OPENGL32 (45 + `glDisableClientState`).
- `glDrawElements` aceita apenas `GL_TRIANGLES` + `GL_UNSIGNED_SHORT` (o gap `GL_UNSIGNED_INT` está registrado — item 14).
- Vertex arrays: float, stride ≥ 12, endianness LE.
- Profundidade: comparação `GL_LESS` (sem `glDepthFunc`/`glDepthMask` — não exigidos por execução).
- Sem GPU Windows real — backend SoftRaster/Metal, compatibilidade limitada aos recursos certificados e validados por PEs de controle.
