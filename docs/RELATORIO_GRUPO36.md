# RELATÓRIO — GRUPO 36: validação real de QPC/QPF por PE x64

**Desfecho: CONCLUÍDO SEM BLOCKER — `hello_qpc.exe → rc=42` na primeira
execução, com runtime INTOCADO.**

> **Runtime permaneceu intocado; QPC/QPF foram validados por PE real na
> primeira execução.**

---

## 1. Baseline (FASE 1)

```text
C tests:     3392 / 0        Swift tests:   63 / 0
warnings:       0            analyzer:       0
PE battery: 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
hello_input=42 · hello_gl11=42 · hello_sse=7 · hello_gl12=42 · hello_stdio=5
hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_mbwc=42 · hello_heap=42
```

## 2. Auditoria QPC/QPF (FASE 2 — sem alterar código)

| Item | Confirmado no código |
|---|---|
| `f_QueryPerformanceFrequency` | `BOOL QueryPerformanceFrequency(LARGE_INTEGER*)` — out-param validado e escrito com **8 bytes** (`uint64_t` = `QuadPart` de `LARGE_INTEGER`) — **não é DWORD** ✓ |
| `f_QueryPerformanceCounter` | `BOOL QueryPerformanceCounter(LARGE_INTEGER*)` — idem **8 bytes** |
| Escrita no guest | `pr_win32_ptr(ctx, a[0], sizeof(uint64_t))` → `*out = …` na memória do convidado (região validada) |
| Frequência declarada | **1 000 000 000 Hz** (contador em nanossegundos) — coerente com a origem; travada pelo teste unitário (`== 1000000000ull`) |
| Origem do contador | `mono_ns()` = `clock_gettime(CLOCK_MONOTONIC)` → **monotônico por construção** |
| Ponteiro inválido | `pr_win32_ptr` falha → `INVALID_PARAMETER (87)` + retorno `FALSE` (0) com `*st = PR_OK` — erro Win32 honesto, sem crash |
| Retorno de sucesso | `1` = TRUE ✓ |

## 3. PE criado (FASE 3/4)

- **Fonte**: `realpe/hello_qpc.c` (45 linhas).
- **Binário**: `Tests/PorticoRuntimeTests/data/hello_qpc.exe`
  (`x86_64-w64-mingw32-gcc -O2 -s`, 0 warnings; entry `0x13F0`, CUI 5.2).
- **Imports**: `KERNEL32.dll` = **QueryPerformanceFrequency,
  QueryPerformanceCounter** (apenas as duas — verificado por `objdump -p`) +
  `msvcrt.dll` = só o CRT (inclui o `printf` de registro, formatador `ll` já
  implementado). Sem arquivos, GUI, OpenGL, sockets, threads ou outras APIs.
- **Tamanho do out-param**: `LARGE_INTEGER.QuadPart` = **8 bytes reais**.
- **Fluxo**: `QueryPerformanceFrequency(&f)` (10 = FALSE; 11 = `f != 1e9`) →
  `QueryPerformanceCounter(&t1)` (12 = FALSE) → `QueryPerformanceCounter(&t2)`
  (13 = FALSE) → `t2 >= t1` (14 = não-monotônico) → registro via `printf`
  (console capturada pelo runtime) → `return 42`. Não depende de valor absoluto
  do contador; `t2 == t1` seria aceito (resolução efetiva).
- **Contrato de retorno**: 42 = OK; 10..14 = primeira falha (etapa).

## 4. Primeira execução com runtime intocado (FASE 5)

| Métrica | Valor |
|---|---|
| `rc` | **42** (sucesso na primeira execução) |
| Instruções | **2919** (`dbg_diag`) |
| Eventos de log | **96** (`dbg_input`) |
| Primeira API que falhou | **nenhuma** (`ExitProcess(42)` via `msvcrt!exit` @0x00FF63CF) |
| Erro Win32 | nenhum |
| Frequência retornada | **f = 1 000 000 000** (contrato exato) |
| Contador 1 | **t1 = 756 144 165 691** |
| Contador 2 | **t2 = 756 144 166 429** |
| Diferença `t2 - t1` | **738 ns** (monotônico ✓) |

Valores obtidos do console do convidado capturado no log estruturado do runtime
(`console(stdout): qpc f=… t1=… t2=… d=…`), extraídos por um probe de harness
(`build/qpc_probe.c` = cópia do `dbg_log` com despejo do anel `pr_log` — nada
no runtime foi alterado).

## 5. Blocker encontrado

**Nenhum.** O fluxo completo passou na primeira execução.

## 6. Correção realizada

**"Runtime permaneceu intocado; QPC/QPF foram validados por PE real na primeira
execução."** Nenhum arquivo do runtime foi alterado; nenhuma correção artificial
foi criada.

## 7. Testes (FASE 7)

Sem alteração no runtime → **nenhum teste unitário redundante foi criado** (os
existentes — freq == 1e9, counter > 0 — permanecem válidos). O teste PE é a
validação principal deste grupo.

## 8. Regressão (FASE 8)

```text
make c-test            → 3392 verificações, 0 falhas
swift test             → Executed 63 tests, with 0 failures
gcc -Wall -Wextra      → 0 warnings
gcc -fanalyzer (build/analyzer36b.err) → 0 diagnósticos
PE battery (15 PEs)    → 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
hello_input=42 · hello_gl11=42 · hello_sse=7 · hello_gl12=42 · hello_stdio=5
hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_mbwc=42
hello_heap=42 · hello_qpc=42 (NOVO)
```

## 9. Resultado final

Critérios de aceitação (FASE 9): `QueryPerformanceFrequency` chamada pelo PE ✓ ·
retornou TRUE ✓ · escreveu o `LARGE_INTEGER` de 8 bytes corretamente ✓ ·
frequência válida (1 000 000 000) ✓ · `QueryPerformanceCounter` chamada 2× ✓ ·
ambas TRUE ✓ · valores realmente escritos no guest ✓ (lidos pelo PE) ·
`t2 >= t1` ✓ · `rc=42` ✓ · sem corrupção de memória ✓ · regressão verde ✓.

Não há declaração de compatibilidade geral com aplicações Windows nem com jogos
comerciais.

## 10. Estado de QPC/QPF

**VALIDADO POR PE REAL**
