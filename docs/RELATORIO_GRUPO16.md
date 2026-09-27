# RELATÓRIO GRUPO 16 — `glDrawElements` + `GL_UNSIGNED_INT` (32-bit)

Data: 2026-09-24 · PorticoRuntime (C11) · PorticoCore (Swift 6.1.2) · escopo exclusivo: suporte real a índices de 32 bits em `glDrawElements`

---

## Os 17 itens

### 1. C checks antes/depois

| | Antes (baseline G16, = fechamento G15) | Depois (FASE 7) |
|---|---|---|
| `make c-test` | **2897 verificações, 0 falhas** | **2961 verificações, 0 falhas** |

Delta: **+64 verificações** (test_gl9.c + atualização autorizada do bloco de tipos de índice em test_gl8.c). As 2897 anteriores passam intactas.

### 2. Swift antes/depois

| | Antes | Depois |
|---|---|---|
| `swift test` (raiz do repositório, toolchain 6.1.2) | **63 testes, 0 falhas** | **63 testes, 0 falhas** |

Nenhum componente Swift foi modificado.

### 3. Warnings

`gcc -Wall -Wextra` (src + testes): **0 → 0**.

### 4. gcc -fanalyzer

`gcc -std=c11 -fanalyzer -Wall -Wextra` (src + testes): **0 issues → 0 issues**.

### 5. Baseline dos 13 PEs (FASE 1, antes de qualquer alteração)

**13×42 com logs byte-idênticos ao histórico certificado:**

| PE | rc | log | | PE | rc | log |
|---|---|---|---|---|---|---|
| hello_real | 42 | 28 | | hello_gl5 | 42 | 76 |
| hello_user | 42 | 43 | | hello_gl6 | 42 | 81 |
| hello_app | 42 | 180 | | hello_gl7 | 42 | 194 |
| hello_gdi | 42 | 70 | | hello_gl8 | 42 | 131 |
| hello_gl | 42 | 72 | | hello_input | 42 | 105 |
| hello_gl2 | 42 | 75 | | hello_gl3 | 42 | 98 |
| hello_gl4 | 42 | 118 | | | | |

Nota de honestidade: o driver de bateria `dbg_input.c` morava em `build/` e foi apagado por uma limpeza do `make c-test` durante o grupo. Foi **reconstruído e recalibrado** até reproduzir os logs históricos byte a byte (a calibração exigiu descobrir que o driver original (a) registrava `hello_dll.dll` via `pr_peproc_provide_dll` em todo run — sem isso `hello_user`/`hello_app` falhavam — e (b) injetava a sequência tecla 'A' down/up + mouse move + LDOWN + LUP + WHEEL + `pr_win32_advance_time(10)` para `hello_input`). A reconstrução está documentada no cabeçalho de `tools/dbg_input.c` (cópia de segurança fora de `build/`).

### 6. Confirmação do comportamento ANTERIOR de `GL_UNSIGNED_INT`

Antes da implementação: **`GL_INVALID_ENUM` (0x500) + framebuffer intacto** — comportamento travado em teste desde o G15 (`test_gl8_engine`, bloco 5) e confirmado no baseline da FASE 1 (os 2897 checks incluíam esse teste passando). O caminho `GL_UNSIGNED_SHORT` funcionava e continua funcionando.

### 7. Implementação realizada

Somente `glDrawElements(..., GL_UNSIGNED_INT, ...)` — a menor implementação correta dentro da arquitetura existente (`Sources/PorticoRuntime/src/pr_gl.c`):

- **Validação de tipo**: `GL_UNSIGNED_SHORT` (certificado, intocado) e `GL_UNSIGNED_INT` (novo) aceitos; **todo o resto continua `GL_INVALID_ENUM` honesto**.
- **Índices 32-bit little-endian vindos da memória do convidado**: cada índice ocupa **exatamente 4 bytes**; leitura montada byte a byte (sem aliasing/UB de alinhamento — o ponteiro do convidado pode não estar alinhado a 4).
- **Respeito ao pointer do PE**: `idx_addr` é usado exatamente como passado; o bloco inteiro de `count × 4` bytes é validado pelo `resolve` (memória do convidado) **antes de qualquer leitura**; ponteiro inválido/curto → `GL_INVALID_VALUE` + nada desenhado (mecanismo de erro já existente — nenhum código novo inventado).
- **Sem overflow**: (a) `count × 4` (count ≤ 4096) não transborda; (b) a leitura de cada índice é limitada ao bloco validado; (c) nova guarda `g16_idx_addr_ok` — só no caminho INT — recusa **antes de qualquer leitura** se `base + vi × stride + len` transbordaria `uint64` em qualquer arranjo ativo (vertex/cor/normal/uv); sem a guarda, uma base próxima de 2^64 envolveria o endereço para um offset baixo e leria memória errada.
- **Sem acesso fora do guest**: o `g10_fetch_vertex` certificado resolve e valida **cada elemento** (12 B vértice/cor/normal, 8 B uv) na memória do convidado; falha → `GL_INVALID_VALUE`.
- **Equivalência de comportamento**: caminho SHORT byte a byte idêntico (`((const uint16_t*)idx)[i+k]`); o pipeline após a leitura do índice (fetch → `xform_ndc` → `raster_tri`/`raster_list` com Gouraud/depth/blend) é o mesmo reusado sem alteração. Para `GL_UNSIGNED_INT`, muda **somente a leitura do tipo de índice**.

### 8. Arquivos modificados (arquivo + o quê + causa)

| Arquivo | O quê | Causa |
|---|---|---|
| `Sources/PorticoRuntime/src/pr_gl.c` | `#define GL_UNSIGNED_INT 0x1405u`; helpers `g16_arr_ok`/`g16_idx_addr_ok` (anti-overflow, só caminho INT); `pr_gl_draw_elements`: aceita INT, `isz = 4`, leitura LE 32-bit sem aliasing | o bloqueador do grupo |
| `Tests/PorticoRuntimeTests/test_gl9.c` | **NOVO** — 18 casos (64 verificações) | FASE 4 |
| `realpe/hello_gl9.c` | **NOVO** — PE real | FASE 5 |
| `Tests/PorticoRuntimeTests/data/hello_gl9.exe` | **NOVO** — PE compilado (MinGW -O2) | FASE 5 |
| `Tests/PorticoRuntimeTests/test_gl8.c` | bloco 5 atualizado p/ o novo comportamento (autorizado pela FASE 7) + teste explícito de tipo não suportado | FASE 7 |
| `Tests/PorticoRuntimeTests/test_main.c` | `void test_gl9(void)` + `RUN(test_gl9)` | registro |
| `tools/dbg_input.c` | cópia de segurança do driver de bateria reconstruído | infra de teste (item 5) |

Não foram tocados: caminho `GL_UNSIGNED_SHORT` (semântica preservada), `g10_fetch_vertex`, pointers, `raster_list`, `glDrawArrays`, framebuffer/depth pipeline, catálogo de APIs, PEs históricos, testes históricos (exceto a atualização autorizada do item 7 da FASE 7), relatórios G8–G15, builders v0–v13.

### 9. Testes novos/modificados

**Novos — `test_gl9.c` (18 casos, 64 verificações)** — harness com `resolve` controlado (offsets num buffer do teste = memória do convidado) chamando o mesmo `pr_gl_draw_elements` certificado:

1. índice simples 0,1,2 — pinta amarelo (cor do arranjo), erro 0
2. índice não sequencial {8,6,7} — pinta
3. índice repetido {9,9,9} — aceito, degenerado (nada desenhado), pipeline íntegro depois
4. múltiplos triângulos (count=6, {0..5}) — ambos pintam
5. **índice 32-bit > 65535** ({66000,66001,66002}) — ver item 15
6. combinação com vertex array (base de todos)
7. combinação com normal array — fetch resolve normal, pintura correta
8. combinação com color array — array amarelo; disable → cor atual verde (observável)
9. combinação com texture coordinate array — textura 2×1 procedural; amostra real (pixel azul onde u≈0.66; se o uv fosse ignorado seria branco)
10. GL_TRIANGLES — modo válido explícito
11. índice além da memória do convidado (250000) — 0x501 + nada
12. ponteiro de índice inválido (fora da memória; e intervalo parcialmente válido) — 0x501 + nada
13. memória insuficiente para todos os índices (count=6 em bloco curto) — 0x501 + nada
14. overflow de cálculo de endereço — (a) índice 0xFFFFFFFF → 0x501; (b) base 0xFFFFFFFFFFFFF000 + índice 500 → 0x501 pela guarda, ANTES de ler
15. glGetError limpo em caso válido
16. GL_INVALID_OPERATION quando o estado exigir (sem vertex array; dentro de glBegin)
17. preservação do caminho GL_UNSIGNED_SHORT (controle pinta + erro 0)
18. tipos não suportados (GL_UNSIGNED_BYTE, GL_BYTE, GL_FLOAT, enum desconhecido) — 0x500 honesto + nada desenhado

**Modificado — `test_gl8.c` (autorizado pela FASE 7, "somente se a implementação estiver realmente certificada"):** o bloco que travava `GL_UNSIGNED_INT → 0x500` foi atualizado para o novo comportamento certificado (INT desenha + erro 0, idêntico ao controle SHORT) e ganhou um caso explícito de tipo não suportado (`GL_UNSIGNED_BYTE → 0x500` + nada desenhado). A intenção original do teste — honestidade dos tipos de índice — foi preservada.

Mais a seção PE de `test_gl9` (hello_gl9 a 42 + 4 asserts de superfície).

### 10. Casos de segurança de memória testados

| Caso | Situação | Resultado observado |
|---|---|---|
| 11 | índice 250000 além de toda a memória do convidado | `GL_INVALID_VALUE` + framebuffer intacto |
| 12a | `idx_addr` fora da memória | `GL_INVALID_VALUE` + nada desenhado |
| 12b | `idx_addr` com apenas 4 dos 12 bytes do 1º índice válidos | `GL_INVALID_VALUE` (bloco validado antes de ler) |
| 13 | count=6 (24 bytes) num bloco com 8 bytes válidos | `GL_INVALID_VALUE` + nada desenhado |
| 14a | índice 0xFFFFFFFF num array normal | `GL_INVALID_VALUE` (resolve falha; multiplicação em uint64 sem overflow) |
| 14b | base 0xFFFFFFFFFFFFF000 + índices 500..502 | `GL_INVALID_VALUE` pela guarda `g16_idx_addr_ok` **antes** de qualquer leitura (sem ela: `base+vi×stride` envolveria p/ offset baixo e leria memória errada) |
| geral | cada elemento (vértice/cor/normal/uv) resolvido e validado individualmente (12/8 bytes) | falha → `GL_INVALID_VALUE` determinístico, nunca crash |

Nenhum teste aceita "apenas não crashou": todos afirmam erro exato + pixels derivados.

### 11. Novo PE

`realpe/hello_gl9.c` → `Tests/PorticoRuntimeTests/data/hello_gl9.exe` (MinGW-w64, janela 320×240). Cria contexto (`GetDC`/`wglCreateContext`/`wglMakeCurrent`), configura viewport + matrizes (ortho 1:1), aloca **vertex buffer grande no heap do convidado (66003 vértices float3 ≈ 792 KB via malloc)**, configura `glVertexPointer`, desenha:

1. **Controle**: `glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, {0,1,2})` — triângulo verde (caminho certificado preservado no PE)
2. **Principal**: `glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, {66000, 66001, 66002})` — triângulo amarelo no centro, com **3 índices > 65535**

Usa `glGetError` após cada draw, valida pixels com `glReadPixels` (4 asserts), `SwapBuffers`, `free`; exits 20–29 por falha específica; **exit 42** no caminho correto. Resultado visual observável: verde no canto superior esquerdo (controle), amarelo no centro (índices 32-bit reais).

### 12. Resultado do novo PE

**`hello_gl9.exe` → exit 42** (`exited=1 rc=42 log=55`). Asserts internos do PE (glGetError ×2 + 4 pixels) + 4 asserts de superfície em `test_gl9` (0x0000FF00 canto controle, 0x00FFFF00 centro, 0x00000000 no alvo do truncamento, 0x00000000 fundo).

### 13. Resultado de todos os PEs anteriores

**13 PEs anteriores: todos 42, logs byte-idênticos aos do baseline** (28/43/180/70/72/75/98/118/76/81/194/131/105 — tabela do item 5). Total com o novo: **14×42**.

### 14. Valor máximo de índice realmente testado

**66002** (nos dois caminhos de teste: `test_gl9` caso 5 e o PE `hello_gl9` — o maior índice do triângulo grande). Além disso, o caso de erro 14a exercita **0xFFFFFFFF** (o maior valor uint32 possível) com resultado determinístico (`GL_INVALID_VALUE`, nada desenhado).

### 15. Confirmação de que NÃO houve truncamento uint32 → uint16

**Confirmado por prova positiva e negativa.** Os índices `{66000, 66001, 66002}` (que em uint16 seriam `{464, 465, 466}`) referenciam os vértices REAIS `verts[66000..66002]`:

- **Positiva**: o triângulo amarelo do centro é pintado (teste 5 + PE exit 27 verificam o pixel central) — só existe se os bytes altos do índice (0x0001…) chegarem ao rasterizador.
- **Negativa**: `verts[464..466]` formam um triângulo proposital noutra posição (canto inferior esquerdo; azul no teste unitário). Se houvesse truncamento, ele apareceria lá e o centro ficaria preto. Os asserts dedicados garantem **PRETO no alvo do truncamento** (teste 5: pixels (15,30) e (20,32) = 0x000000; PE: exit 28 no pixel (30,35)) — qualquer truncamento uint32→uint16 falha imediatamente.
- A leitura é montada byte a byte em `uint32_t` (`e[0] | e[1]<<8 | e[2]<<16 | e[3]<<24`) — sem conversão para 16 bits em nenhum ponto.

### 16. Próximo bloqueador real descoberto

**Nenhum bloqueador adicional foi descoberto nas execuções deste grupo** — o `hello_gl9.exe` completou 42 e os 18 casos completaram sem esbarrar em nova lacuna (a regra final do grupo veda ampliar o escopo, e a execução não forçou nada além). Registram-se as lacunas **conhecidas e remanescentes** (não bloqueadoras desta execução, nenhuma exigida por teste real):

- tipos de índice `GL_UNSIGNED_BYTE`/`GL_BYTE`/`GL_FLOAT` continuam `GL_INVALID_ENUM` honesto (comportamento travado em teste 18);
- `glGetFloatv`, `glGetDoublev`, `glFogf`, `glTexEnvf`, `glShadeModel`, `glMultMatrixf`, `glLoadMatrixf`, `glDepthFunc`, `glDepthMask` continuam fora do runtime (não reimplementadas por antecipação; âncoras de honestidade vivas nos testes);
- `GL9_MAX_IDX = 4096` limita `count` por draw (`GL_INVALID_VALUE` acima — caso não exercitado acima do limite neste grupo).

### 17. Confirmação: nenhuma API funcional anterior foi reimplementada

Confirmado. O caminho `GL_UNSIGNED_SHORT` permanece **byte a byte o mesmo** (só a leitura do índice foi parametrizada por tipo); `g10_fetch_vertex`, `glVertexPointer`/`glColorPointer`/`glNormalPointer`/`glTexCoordPointer`, `raster_list`, `glDrawArrays`, o pipeline de framebuffer/depth e todas as demais APIs certificadas ficaram intactos; as 2897 verificações anteriores passam sem alteração (apenas ADIÇÃO de 64); os 13 PEs anteriores retornam 42 com logs idênticos; nenhum stub/no-op foi criado; nenhum hack específico para `hello_gl9`; nenhum código proprietário; nenhuma afirmação de compatibilidade com GTA V, MX Bikes ou jogos comerciais.

---

## Fases executadas

| Fase | Status | Evidência |
|---|---|---|
| 1. Baseline | ✓ | `make c-test` 2897/0; 13 PEs × 42 byte-idênticos (driver reconstruído/recalibrado — item 5); comportamento anterior `GL_UNSIGNED_INT → 0x500` confirmado em teste |
| 2. Auditoria | ✓ | `pr_gl_draw_elements` + `g10_fetch_vertex` (já `uint32_t` + resolve por elemento + aritmética uint64) + 4 pointers + `raster_list` + `glDrawArrays` + pipeline — só a leitura do índice precisava mudar |
| 3. Implementação | ✓ | item 7 (INT real; SHORT preservado; outros tipos honestos) |
| 4. Validações | ✓ | 18 casos — item 9 (todos afirmam pixels/erros; o índice 32-bit altera o vértice REAL do rasterizador — item 15) |
| 5. PE real | ✓ | `hello_gl9.exe` com índices > 65535 — itens 11–12 |
| 6. Segurança do acesso | ✓ | item 10 (validação de 4 bytes por índice, resolve por elemento, anti-overflow, erro coerente existente, determinístico) |
| 7. Regressão | ✓ | `make c-test` 2961/0; 14 PEs × 42; Swift 63/0; -Wall -Wextra 0; analyzer 0; teste antigo atualizado como autorizado + caso explícito de tipo não suportado |
| 8. Não implementar | ✓ | nada além do tipo de índice em `glDrawElements` foi tocado (item 8) |
| 9. Qualidade | ✓ | item 17 |
| 10. Relatório | ✓ | este documento (`docs/RELATORIO_GRUPO16.md`) |

## Limitações conhecidas do runtime

- Conjunto OpenGL 1.1 parcial e explícito: 46 APIs certificadas no catálogo OPENGL32 (nenhuma API nova neste grupo — só o subtipo de índice de `glDrawElements` foi estendido).
- `glDrawElements`: modos aceitos `GL_TRIANGLES`; tipos de índice aceitos `GL_UNSIGNED_SHORT` (16-bit LE) e `GL_UNSIGNED_INT` (32-bit LE).
- Vertex arrays: float, stride ≥ 12 (≥ 8 em uv), validação por acesso resolvido na memória do convidado.
- Profundidade: comparação `GL_LESS`; matriz por tipo com limites certificados (32/4).
- Sem GPU Windows real — backend SoftRaster/Metal, compatibilidade limitada aos recursos certificados e validados por PEs de controle.
