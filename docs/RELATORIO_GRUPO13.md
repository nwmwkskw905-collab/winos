# RELATÓRIO — GRUPO 13 (definitivo): descoberta e implementação do próximo bloqueador real

> Re-certificação sob a spec de 14 itens. A passagem anterior foi arquivada em
> `docs/RELATORIO_GRUPO13_execucao1.md` (histórico preservado, não alterado).
> **Divergência CONTEXTO × código resolvida pela regra do grupo**: o código-fonte
> e a execução são a fonte de verdade. O CONTEXTO descreve o estado pós-G12
> ("glGetString ainda não existe"); a auditoria mostrou que `glGetString` já fora
> descoberto, registrado (evidência ANTES da implementação), implementado e
> validado na execução imediata deste mesmo Grupo 13. Esta passagem **auditou,
> re-executou, completou a cobertura de testes exigida e re-certificou** — sem
> reimplementar nada.

## 1. C checks antes/depois
**Antes (pós-G12): 2537** → **Depois: 2638 verificações, 0 falhas** (+101: `test_gl6.c` = 94 checks — incluindo +3 do caso "sem contexto" desta passagem — e bloco `CVTSI2SS` em `test_cpu64ext.c` = 7 checks). `make c-test` = 2638/0.

## 2. Swift tests antes/depois
**63 → 63** (0 falhas). A cadeia SurfaceBridge→BGRA8→Metal não mudou; nenhum teste Swift artificial foi criado.

## 3. Warnings
**0** sob `-Wall -Wextra` (verificado arquivo a arquivo sobre `src/*.c` + `Tests/*.c` + driver).

## 4. Analyzer findings
**0** (`gcc -fanalyzer` sobre `pr_gl.c`, `pr_win32.c`, `pr_peproc.c`, `pr_cpu64.c`).

## 5. O PRIMEIRO bloqueador real encontrado
**`OPENGL32.dll!glGetString`** — com evidência registrada ANTES de qualquer implementação (parada honesta no `pr_peproc_prepare`, `hello_gl6.exe` sobre o runtime inalterado):
```
Reason:    Unsupported Win32 API
Module:    OPENGL32.dll
Function:  glGetString
```
Um **segundo bloqueador real** apareceu na MESMA execução do PE de validação (após `glGetString` funcionar), também com parada honesta antes de implementado: a instrução x64 **`CVTSI2SS`** (`F3 [REX.W] 0F 2A`):
```
Technical: rip=0xFE2B0D opcode=0x0F bytes=F3 48 0F 2A C8 F3 0F 5E
```
(`cvtsi2ss xmm1, rax` disparado por `(float)strlen(...)`; `divss` ao lado já era suportado). Sem essa instrução o PE não comprova a API — exigência real de execução, não lista por antecipação.

## 6. Qual API foi implementada
**`glGetString`** (única API GL nova): 4 enums reais (item 7), strings estáveis em memória de convidado **válida enquanto o contexto existe** (cache `glstr[4]` no ctx, alocado no heap do processo — padrão `GetCommandLineA`; duas consultas do mesmo enum retornam o mesmo ponteiro, testado), erros GL reais (`0x500` enum inválido; `0x502` dentro de `glBegin` + `NULL`), leitura pelo PE via CRT real (`strcmp`/`strlen`/`strncmp`). Complementar (bloqueador #2): **`CVTSI2SS`** em `pr_cpu64.c` com semântica Intel SDM real (int32/int64 sinalizado → float32 escrevendo só os bits 31:0 e **preservando 127:32**, testado byte a byte). `glGetIntegerv` **não** foi implementada (não foi atingida — uma lacuna por vez).

## 7. Enums/pnames suportados
| API | Suportados | Não suportados (comportamento honesto) |
|---|---|---|
| `glGetString` | `GL_VENDOR` 0x1F00, `GL_RENDERER` 0x1F01, `GL_VERSION` 0x1F02, `GL_EXTENSIONS` 0x1F03 | qualquer outro enum → `NULL` + `GL_INVALID_ENUM` (0x500) |
| `glGetIntegerv` | **nenhum** (API ausente — lookup `PR_ERR_RANGE`, parada honesta no loader) | reserva do próximo grupo (item 13) |

## 8. Arquivos modificados
| Arquivo | Mudança |
|---|---|
| `Sources/PorticoRuntime/include/portico/pr_gl.h` | decl `pr_gl_get_string` |
| `Sources/PorticoRuntime/src/pr_gl.c` | `pr_gl_get_string` (4 enums, strings estáveis, erros reais) |
| `Sources/PorticoRuntime/src/pr_win32.c` | `f_glGetString` + cache `glstr[4]` no ctx + catálogo |
| `Sources/PorticoRuntime/src/pr_cpu64.c` | handler `CVTSI2SS` (bloqueador #2, semântica Intel real) |
| `Tests/PorticoRuntimeTests/test_gl6.c` | **NOVO** (94 checks: enums/valores/strings-ponteiro/estabilidade/erros/`glGetError`/sem-contexto/render observável/PE real) |
| `Tests/PorticoRuntimeTests/test_cpu64ext.c` | + bloco `CVTSI2SS` (7 checks, preservação 127:32) |
| `Tests/PorticoRuntimeTests/test_gl5.c` | âncora de honestidade G12→G13 (`glGetString`→`glGetDoublev`; intenção preservada; `glGetIntegerv`/`glFogf` mantidos) |
| `Tests/PorticoRuntimeTests/test_main.c` | `RUN(test_gl6)` |
| `realpe/hello_gl6.c` + `Tests/PorticoRuntimeTests/data/hello_gl6.exe` | **NOVOS** (PE de descoberta/validação) |
Nesta passagem de re-certificação: **apenas** `test_gl6.c` (+3 checks sem-contexto). Nenhum arquivo de runtime foi tocado.

## 9. Novo PE criado
**`hello_gl6.exe`** (`realpe/hello_gl6.c`) — inicialização 3D realista: contexto GL → consultas (`GL_VENDOR/GL_RENDERER/GL_VERSION/GL_EXTENSIONS`) → validação de conteúdo → erros reais → **uso observável do resultado** (cor do quad = função das strings) → operação gráfica (ortho + quad) → validação objetiva de pixels → `SwapBuffers`.
```
x86_64-w64-mingw32-gcc -O2 -o Tests/PorticoRuntimeTests/data/hello_gl6.exe realpe/hello_gl6.c -lgdi32 -lopengl32
```
Anti-fraude: `r = (GL_VENDOR=="Portico")?255:0`; `g = strlen(GL_RENDERER)=24`; `b = (GL_EXTENSIONS[0]==0)?0:255` → pixel **(255, 24, 0) = 0x00FF1800**. Qualquer string errada/curta/NULL muda o pixel ou aborta com exit próprio (20..35). Certo → **42**.

## 10. Resultado do novo PE
**`hello_gl6.exe` = 42** (81 chamadas de API em log). Repetido nesta passagem.

## 11. Resultado de todos os PEs anteriores
| PE | rc | | PE | rc |
|---|---|---|---|---|
| hello_real | **42** | | hello_gl3 | **42** |
| hello_user | **42** | | hello_gl4 | **42** |
| hello_app | **42** | | hello_gl5 | **42** |
| hello_input (injeção) | **42** | | **hello_gl6 (novo)** | **42** |
| hello_gdi | **42** | | hello_gl2 | **42** |
| hello_gl | **42** | | | |

Bateria: **11×42** (executada antes de qualquer alteração E depois — nenhuma regressão).

## 12. Valores retornados pelas consultas
| Enum | Valor | Verificado por |
|---|---|---|
| `GL_VENDOR` | `Portico` | PE (`strcmp`) + `test_gl6_unidade` |
| `GL_RENDERER` | `Portico SoftRaster/Metal` (24 bytes) | PE (`strlen==24`) + unidade |
| `GL_VERSION` | `1.1 Portico Subset` | PE (`strncmp "1.1"`) + unidade |
| `GL_EXTENSIONS` | `""` (vazio — nada inventado) | PE + unidade |
| 0x9999 | `NULL` + `GL_INVALID_ENUM` 0x500 | PE + unidade |
| dentro de `glBegin` | `NULL` + `GL_INVALID_OPERATION` 0x502 | PE + unidade |
| sem contexto GL | `NULL` (sem crash) | `test_gl6_unidade` (nova) |

Internamente consistente com o backend REAL: rasterizador por software do WinOS entregue ao Metal (**não há GPU Windows**); "1.1 Subset" = subconjunto implementado (sem afirmação de conformidade); zero extensões inventadas.

## 13. Próximo bloqueador real descoberto
**`glGetIntegerv`** — única lacuna `glGet*` restante (confirmada ausente por auditoria de catálogo e por teste honesto `PR_ERR_RANGE`). São as consultas de estado de init (`GL_MAX_TEXTURE_SIZE`, `GL_MAX_LIGHTS`, `GL_MAX_MODELVIEW_STACK_DEPTH`, `GL_MAX_PROJECTION_STACK_DEPTH`, `GL_VIEWPORT`, …) usadas por todo app 3D real. Regras já definidas para o próximo grupo: implementar **somente os pnames realmente consultados pelo PE**, com valores atrelados às capacidades efetivas (ex.: textura 64×64×3 do subconjunto; pilha MV 32 / PROJ 4 reais) e `GL_INVALID_ENUM` honesto para pname não representável. Também confirmado fora do subconjunto (fault honesto testado): SSE packed (`addps`). `glTexEnvf/glShadeModel/glMultMatrixf/glLoadMatrixf/glDepthFunc/glDepthMask` continuam fora (FASE 8 cumprida).

## 14. Nenhuma API funcional foi reimplementada
Confirmado por auditoria desta passagem: `glGetError`, `glScalef`, `glDrawElements`, `glTranslatef`, `glRotatef`, `glBlendFunc`, `glColor4f`, `glLightfv`, `glMaterialfv`, `glNormal3f`, `glNormalPointer` e demais do catálogo **não foram tocados em nenhuma das duas passagens** (apenas consumidos por testes). Nesta re-certificação o runtime permaneceu **byte-a-byte intocado** — só checks de teste foram adicionados. `glGetIntegerv`/`glGetDoublev`/`glFogf`/`glTexEnvf`/`glShadeModel`/`glMultMatrixf`/`glLoadMatrixf`/`glDepthFunc`/`glDepthMask` seguem com lookup ausente + parada honesta.

---
*Qualidade (FASE 9): 0 warnings `-Wall -Wextra`; 0 analyzer findings; testes determinísticos; sem código proprietário; sem no-op falso; não suportado = EXECUTION STOPPED/erro GL honesto. Sem afirmação de compatibilidade com jogos comerciais nem com OpenGL/D3D9 em geral.*
