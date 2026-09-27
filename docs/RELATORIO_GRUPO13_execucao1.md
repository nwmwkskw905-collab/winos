# RELATÓRIO — GRUPO 13 (descoberta real do próximo bloqueador: glGet* → glGetString + lacuna de CPU CVTSI2SS)

## 1. C checks antes/depois
**Antes: 2537** (G12) → **Depois: 2635 verificações, 0 falhas** (+98: `test_gl6.c` e bloco `CVTSI2SS` em `test_cpu64ext.c`).

## 2. Swift tests antes/depois
**63 → 63** (0 falhas). A cadeia SurfaceBridge→BGRA8→Metal não mudou — nenhum teste Swift artificial foi criado.

## 3. Warnings
**0** sob `-Wall -Wextra` (verificado arquivo a arquivo com `-fsyntax-only` sobre `src/*.c` + `Tests/*.c` + driver).

## 4. Analyzer findings
**0** (`gcc -fanalyzer` sobre `pr_gl.c`, `pr_win32.c`, `pr_peproc.c`, `pr_cpu64.c`).

## 5. Qual glGet* foi realmente atingido primeiro
**`glGetString`** — comprovado por execução real ANTES de qualquer implementação (parada honesta no `pr_peproc_prepare`, `hello_gl6.exe` sobre o runtime inalterado):
```
Reason:    Unsupported Win32 API
Module:    OPENGL32.dll
Function:  glGetString
```
Ordem de investigação do grupo (1. glGetString, 2. glGetIntegerv) respeitada: o PE de descoberta usa `glGetString` como primeira consulta de init (fluxo realista — `GL_VERSION` é a consulta clássica de boot de todo app GL) e NÃO importa `glGetIntegerv` (remover UMA lacuna real por vez; FASE 8).

## 6. Qual API foi implementada
**`glGetString`** (única API nova) — comportamento real:
- `GL_VENDOR` (0x1F00) → `"Portico"`
- `GL_RENDERER` (0x1F01) → `"Portico SoftRaster/Metal"`
- `GL_VERSION` (0x1F02) → `"1.1 Portico Subset"`
- `GL_EXTENSIONS` (0x1F03) → `""` (nenhuma extensão inventada — FASE 3)
- enum desconhecido → `NULL` + `GL_INVALID_ENUM` (0x500); dentro de `glBegin` → `NULL` + `GL_INVALID_OPERATION` (0x502) — ambos verificados.
- Strings estáveis em memória de convidado (cache por contexto via `heap_alloc_impl`, padrão `GetCommandLineA`) — o PE lê via CRT real (`strcmp`/`strlen`).

**2º bloqueador REAL, descoberto pela MESMA execução (não antecipado)**: instrução x64 **`CVTSI2SS`** (`F3 [REX.W] 0F 2A`) — o PE parou honesto no interpretador:
```
Technical: rip=0xFE2B0D opcode=0x0F bytes=F3 48 0F 2A C8 F3 0F 5E
```
(`cvtsi2ss xmm1, rax` disparado por `(float)strlen(...)`). Implementada em `pr_cpu64.c` com **semântica Intel SDM real**: int32/int64 sinalizado → float32, escrevendo só os bits 31:0 do xmm e **preservando 127:32** (testado byte a byte). `DIVSS`/`MULSS`/`ADDSS`/`SUBSS` já existiam (G8). Sem a instrução, o PE de validação não poderia comprovar a API — foi exigência real de execução, não lista por antecipação (FASE 8).

## 7. Arquivos modificados
| Arquivo | Mudança |
|---|---|
| `Sources/PorticoRuntime/include/portico/pr_gl.h` | decl `pr_gl_get_string` |
| `Sources/PorticoRuntime/src/pr_gl.c` | `pr_gl_get_string` (4 enums, strings estáveis, erros reais) |
| `Sources/PorticoRuntime/src/pr_win32.c` | `f_glGetString` + cache `glstr[4]` no ctx + catálogo |
| `Sources/PorticoRuntime/src/pr_cpu64.c` | handler `CVTSI2SS` (semântica Intel real) |
| `Tests/PorticoRuntimeTests/test_gl6.c` | **NOVO** (3 suites, 91 checks) |
| `Tests/PorticoRuntimeTests/test_cpu64ext.c` | + bloco `CVTSI2SS` (7 checks: valores, preservação 127:32) |
| `Tests/PorticoRuntimeTests/test_gl5.c` | âncora de honestidade G12→G13 (`glGetString` virou real → `glGetDoublev`; intenção "consultas ainda ausentes" preservada; `glGetIntegerv`/`glFogf` mantidos) |
| `Tests/PorticoRuntimeTests/test_main.c` | `void test_gl6(void); RUN(test_gl6);` |
| `realpe/hello_gl6.c` + `data/hello_gl6.exe` | **NOVOS** (PE de descoberta/validação) |

## 8. Novo PE criado
**`hello_gl6.exe`** (`realpe/hello_gl6.c`), compilado com:
```
x86_64-w64-mingw32-gcc -O2 -o Tests/PorticoRuntimeTests/data/hello_gl6.exe realpe/hello_gl6.c -lgdi32 -lopengl32
```
- Cria contexto GL; consulta os 4 enums; valida conteúdo (`strcmp` com as strings esperadas; `strlen(GL_RENDERER)==24`); exercita erros reais (0x9999→0x500; em `begin`→0x502); **usa o resultado de maneira observável**: a cor do quad é função das strings (`r = vendor=="Portico" ? 255:0`; `g = strlen(GL_RENDERER)=24`; `b = exts[0]==0 ? 0:255`) → pixel esperado **(255, 24, 0) = 0x00FF1800**; desenha (glOrtho + quad) e valida pixels objetivamente (2 internos = 0x00FF1800; 2 externos = 0) com margens de 20..40 px; SwapBuffers.
- Exit codes: 1..5 setup; 6 glGetError; 11 read; 12 SwapBuffers; 20..23 consultas NULL; 24..27 conteúdo; 28/29 enum inválido; 30/31 begin; 32..35 pixels; **42 = tudo correto**.

## 9. Resultado do novo PE
**`hello_gl6.exe` = 42** (81 chamadas de API registradas em log).

## 10. Resultado de todos os PEs anteriores
| PE | rc |
|---|---|
| hello_real / hello_user / hello_app | **42 / 42 / 42** |
| hello_input (com injeção determinística) | **42** |
| hello_gdi / hello_gl / hello_gl2 | **42 / 42 / 42** |
| hello_gl3 (md5 `c6013978cfb22cfb8312dec93aa090dc`) | **42** |
| hello_gl4 / hello_gl5 | **42 / 42** |
| **hello_gl6 (NOVO)** | **42** |

Bateria total: **11×42**. `make c-test` = **2635/0**; `swift test` = **63/0**.

## 11. Valores efetivamente retornados pelas consultas
| Enum | Valor | Verificado por |
|---|---|---|
| `GL_VENDOR` | `Portico` | PE (`strcmp`) + `test_gl6_unidade` |
| `GL_RENDERER` | `Portico SoftRaster/Metal` | PE (`strlen==24`) + unidade |
| `GL_VERSION` | `1.1 Portico Subset` | PE (`strncmp "1.1"`) + unidade |
| `GL_EXTENSIONS` | `""` (vazio) | PE (`strcmp ""`) + unidade |
| 0x9999 | `NULL` + erro 0x500 | PE + unidade |
| em `begin` | `NULL` + erro 0x502 | PE + unidade |
Internamente consistente: descreve o backend REAL (rasterizador por software do WinOS entregue ao Metal — **não há GPU Windows**), "1.1 Subset" marca que é o subconjunto implementado (sem afirmação de conformidade), e zero extensões inventadas. Ponteiros estáveis (duas consultas = mesmo endereço — testado).

## 12. Próximo bloqueador descoberto
**`glGetIntegerv`** (prioridade 2 da investigação — lacuna `glGet*` restante, confirmada ausente por auditoria e por teste honesto): consultas de estado (`GL_MAX_TEXTURE_SIZE`, `GL_MAX_LIGHTS`, `GL_MAX_MODELVIEW_STACK_DEPTH`, `GL_VIEWPORT`, …) que todo app 3D real usa no init. Valores futuros deverão corresponder às capacidades efetivas (ex.: texturas 64×64×3 do subconjunto; pilha MV 32/PROJ 4 reais), com `GL_INVALID_ENUM` honesto para pnames não representáveis (FASE 4 do próximo grupo). Também confirmado fora do subconjunto: SSE packed (`addps` continua em fault honesto com bytes — testado). Nada disso foi implementado agora (FASE 8).

## 13. Nenhuma API funcional anterior foi reimplementada
Confirmado: `glGetError`, `glScalef`, `glDrawElements`, `glTranslatef`, `glRotatef` e todas as demais do catálogo **não foram tocadas** (apenas consumidas pelos testes). `glGetIntegerv`/`glGetDoublev`/`glFogf` continuam com lookup ausente + `PR_ERR_RANGE` (verificado em `test_gl5_unidade`/`test_gl6_unidade`). A única alteração em teste existente foi a **troca de âncora** do G12 (`glGetString`→`glGetDoublev`), preservando a intenção (padrão dos grupos 10–12); nenhum teste foi removido ou enfraquecido.

---
*Correção registrada durante o grupo: o primeiro bloco de teste do `CVTSI2SS` usava ModRM `0xD9` (`xmm3`) por engano de aritmética binária — o runtime estava correto; o byte certo para `xmm1` é `0xC9` (depurado com repro mínimo `build/dbg_cvtsi.c`). Nenhum comportamento de runtime foi alterado para "passar no teste".*
*Sem afirmação de compatibilidade com jogos comerciais nem com OpenGL/D3D9 em geral. Não suportado continua em parada honesta/EXECUTION STOPPED.*
