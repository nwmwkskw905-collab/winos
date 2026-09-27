# Relatório do Grupo 47 — WaitForMultipleObjects com duas threads reais (Validação Real por PE x64)

## STATUS

**CONCLUÍDO COM SUCESSO** — `WaitForMultipleObjects(2, handles_de_thread, TRUE, INFINITE)` validado por PE real (`hello_wait_multiple.exe`, **rc=42**, contrato 10–16/42 completo): `WAIT_OBJECT_0` após a **conclusão real de duas threads guest em pthreads reais**. 23/23 execuções com rc=42 (≥20 consecutivas ✓); regressão completa verde.

**Decisão registrada (contradição de regras resolvida com o usuário)**: o PE observou um **gap de CPU** (`PUNPCKLQDQ`, SSE2, fora do subconjunto do interpretador) e o escopo do grupo proibia "mudanças no CPU", mas a aceitação exigia `rc=42` e a regra permanente proíbe alterar o PE para esconder falha do runtime. Consultado, o usuário escolheu a **opção A**: adicionar ao subconjunto do interpretador as instruções **observadas pelo PE**, fiéis ao Intel SDM, isoladamente (regra permanente: "só o subconjunto necessário"). Foi necessário **exatamente um** opcode.

## BASELINE OBRIGATÓRIA (estado final do G46, antes de qualquer alteração)

- C tests: `3392 verificações, 0 falhas` ✓ · Swift tests: `Executed 63 tests, with 0 failures` ✓ · warnings: 0 ✓ · analyzer: 0 ✓
- PE battery: `15/15` (28/43/180/70/72/75/98/118/76/81/194/131/55/200/105) ✓
- hello_thread = 42 ✓ · hello_thread_shared = 42 ✓ · hello_mutex = 42 ✓ · hello_mutex_timeout = 42 ✓ · hello_thread_timeout = 42 ✓ · hello_cs = 42 ✓ · hello_virt = 42 ✓ · hello_virt_protect = 42 ✓ · hello_virt_query = 42 ✓ · hello_qpc = 42 ✓ · hello_heap = 42 ✓ · hello_mbwc = 42 ✓ · hello_file = 42 ✓ · hello_file_w = 42 ✓ · hello_file_seek = 42 ✓ · hello_stdio = 5 ✓ · hello_gl12 = 42 ✓ · hello_sse = 7 ✓
- **MD5 antes: `393d10bbe8f398dda14e022ea40a557e`** (= estado final do G46) ✓

## PE criado / toolchain / imports

- `realpe/hello_wait_multiple.c` → `Tests/PorticoRuntimeTests/data/hello_wait_multiple.exe` — `x86_64-w64-mingw32-gcc -O2 -s` (0 erros; mesmo GCC 14 POSIX).
- Fluxo (contrato 10–16/42): `h1 = CreateThread(worker1)` (→10); `h2 = CreateThread(worker2)` (→11); cada worker executa tarefa simples independente (soma 1..1000 / 1..500 em `volatile LONG`) e marca `worker1_done`/`worker2_done` em memória compartilhada; `WaitForMultipleObjects(2, handles, TRUE, INFINITE) != WAIT_OBJECT_0` (→12); `worker1_done != 1` (→13); `worker2_done != 1` (→14); `CloseHandle(h1)` FALSE (→15); `CloseHandle(h2)` FALSE (→16); `return 42`. Sem `GetExitCodeThread` ✓.
- imports (objdump): KERNEL32 = `CloseHandle, CreateThread, DeleteCriticalSection, EnterCriticalSection, GetLastError, InitializeCriticalSection, LeaveCriticalSection, SetUnhandledExceptionFilter, Sleep, TlsGetValue, VirtualProtect, VirtualQuery, WaitForMultipleObjects` + msvcrt (CRT). **Nenhum** evento/semaforo/TLS/`WaitForMultipleObjectsEx`/`MsgWaitForMultipleObjects`/`GetExitCodeThread` ✓.

## primeiro blocker encontrado (1ª execução — runtime COMPLETAMENTE intocado)

| Item | Registro |
|---|---|
| símbolo/import | `KERNEL32.dll!WaitForMultipleObjects` (import de `hello_wait_multiple.exe`) |
| ponto de dispatch | **bind/carga no `pr_peproc_prepare` — ANTES da execução** (`Address: (—)`); catálogo sem entrada (grep vazio) |
| retorno | nenhum (parada antes de qualquer chamada) |
| código de erro | nenhum (não há `GetLastError` — `EXECUTION STOPPED` honesto) |
| arquivo e função envolvidos | `realpe/hello_wait_multiple.c!main` (chamada à API) → binder `pr_peproc.c`/`pr_win32.c` ("API conhecida do módulo mas sem implementação") |
| MD5 | `393d10bbe8f398dda14e022ea40a557e` antes == depois ✓ |

**Segundo blocker observado** (após implementar a API, mesmo PE): `EXECUTION STOPPED — instrução fora do subconjunto x64 (opcode 0F 6C) @ 0x00FF77FA, bytes 66 0F 6C C1 0F 11 44 24` = **`PUNPCKLQDQ xmm0, xmm1`** (SSE2) — o GCC -O2 usou XMM para montar o array `handles[2]` (parada honesta com opcode/RIP/bytes/motivo, conforme a regra permanente de CPU).

## causa raiz

1. `WaitForMultipleObjects` **não existia** no runtime (nem catálogo IMPL/TODO);
2. após implementá-la, o fluxo do PE esbarrou no **gap de CPU** `PUNPCKLQDQ` (66 0F 6C) — instrução SSE2 não incluída no subconjunto do interpretador (o restante do caminho — `movq`/`movsd`/`movups` store, `unpcklpd`, `punpckldq` — já era suportado pelo GRUPO 3 SSE2 e grupos anteriores; a varredura de disassembly e a execução confirmaram `punpcklqdq` como **única** lacuna do caminho).

## correção aplicada

1. **`WaitForMultipleObjects` — SOMENTE o subconjunto `WaitForMultipleObjects(2, thread_handles, TRUE, INFINITE)`** (`f_WaitForMultipleObjects` em `pr_win32.c`, imediatamente antes de `f_CloseHandle`; `IMPL` args=16):
   - `nCount==2`, `bWaitAll==TRUE`, `dwMilliseconds==INFINITE` — fora disso: `WAIT_FAILED` + `ERROR_INVALID_PARAMETER` honesto (nCount≠2, wait-any e timeout finito de múltiplos handles **não implementados**);
   - handles: somente handles de **thread** (`w32_thread_slot`); tipo conhecido porém fora do subconjunto (mutex/file/std) → `WAIT_FAILED` + `ERROR_INVALID_PARAMETER`; handle desconhecido → `ERROR_INVALID_HANDLE`; handle repetido → `ERROR_INVALID_PARAMETER`. **Caminhos existentes de thread/mutex/file de `Wait`/`CloseHandle` preservados intocados.**
   - Comportamento: **join real nas duas threads** (as duas precisam concluir = wait-all); thread já sinalizada (join anterior) = sem espera; retorno `WAIT_OBJECT_0` (0) — semântica wait-all de sucesso do Windows.
   - Nenhum timeout (finito) de múltiplos handles; nenhum `WaitForMultipleObjectsEx`/`MsgWaitForMultipleObjects`; nenhum `GetExitCodeThread`; nenhuma nova API; nenhuma mudança de scheduler.
2. **`PUNPCKLQDQ xmm, xmm/m128` (66 0F 6C, forma legada SSE)** em `pr_cpu64.c` (GRUPO 3 "SSE2 real", junto de `punpckldq`), fiel ao Intel SDM: `DEST[63:0] ← SRC1[63:0]` (preservado); `DEST[127:64] ← SRC2[63:0]`. ~10 linhas; nada mais no CPU. **Autorizada explicitamente pelo usuário (opção A)** devido à contradição escopo×aceitação descrita no STATUS. Nenhuma outra instrução foi observada como faltante.

## arquivos alterados

- `Sources/PorticoRuntime/src/pr_win32.c` — `f_WaitForMultipleObjects` + entrada `IMPL("kernel32.dll", "WaitForMultipleObjects", …, 16)` (único arquivo Win32 alterado).
- `Sources/PorticoRuntime/src/pr_cpu64.c` — caso `op2 == 0x6C && p66` (PUNPCKLQDQ) no GRUPO 3 (SSE2).

## runtime MD5 antes/depois

- Antes: `393d10bbe8f398dda14e022ea40a557e`
- Depois: `c9e8afa71214412be92bff3de0b35278`

## resultado do PE

`exited=1 rc=42 log=32` — contrato **10–16/42** completo, nenhum outro código emitido. Série do log: `CreateThread → CreateThread → WaitForMultipleObjects → CloseHandle → CloseHandle → exit(42)` / `[PROCESS] exit code 42`.

## handles utilizados

`h1 = 0xF0F0F100`, `h2 = 0xF0F0F101` (slot scan determinístico do mais baixo livre em `f_CreateThread`; rc≠10/11 prova criação não-NULL; rc≠15/16 prova `CloseHandle` TRUE nos dois).

## resultado do WaitForMultipleObjects

**`WAIT_OBJECT_0`** (rc≠12) — wait-all com **join real nas duas threads guest** (pthreads host reais, CPUs guest independentes por thread — infraestrutura G42–G43 preservada). Os checks `worker1_done == 1` e `worker2_done == 1` **após** o wait (rc≠13/14) provam que **ambas terminaram antes da aceitação** (cada worker marca o flag como última ação da tarefa; o join só retorna com a conclusão real).

## número de execuções repetidas

**23 execuções** (1 oficial + 22 consecutivas): **23/23 `rc=42 log=32`** — determinístico (log estável; este PE não tem spin de QPC). Sem crash, fault ou corrupção.

## C tests / Swift tests / warnings / analyzer / PE battery / regressões

- C tests: `3392 verificações, 0 falhas` ✓ · Swift tests: `Executed 63 tests, with 0 failures` ✓
- warnings: 0 ✓ · analyzer: 0 ✓
- PE battery: `15/15` (28/43/180/70/72/75/98/118/76/81/194/131/55/200/105) ✓
- Regressões exigidas: **hello_thread_timeout = 42** ✓ · **hello_mutex_timeout = 42** ✓
- Checklist completo: thread=42/30 · thread_shared=42/33 · mutex=42/39 · mutex_timeout=42/40 · thread_timeout=42/73 · **wait_multiple=42/32** · cs=42/44 · virt=42/64 · virt_protect=42/31 · virt_query=42/33 · qpc=42/98 · heap=42/31 · mbwc=42/29 · file=42/36 · file_w=42/33 · file_seek=42/33 · stdio=5/155 · gl12=42/116 · sse=7/209 ✓ (todos os PEs anteriores preservados)

## limitações restantes

- Subconjunto exato: `nCount == 2`, `bWaitAll == TRUE`, `dwMilliseconds == INFINITE`, handles de **thread** apenas. Fora disso → `WAIT_FAILED` + erro honesto (nunca sucesso falso): wait-any (`bWaitAll=FALSE`), nCount≠2, timeout finito de múltiplos handles, handles de mutex/file/evento **não suportados** nesta API.
- `WaitForSingleObject` de thread/mutex e seus timeouts (G45/G46) **preservados e não alterados** (regressões verdes acima).
- `PUNPCKLQDQ` (SSE2) adicionado ao subconjunto do interpretador x64 mediante autorização explícita (opção A) — única mudança de CPU; SDM-fiel; sem VEX/AVX; demais shuffles/arithmetics packed continuam fora do subconjunto com fault honesto.
- Comportamento wait-all apenas; sem `WAIT_ABANDONED`, sem `WaitForMultipleObjectsEx`, sem `MsgWaitForMultipleObjects`, sem `GetExitCodeThread`, sem novas APIs Win32, sem mudanças de scheduler.
- Não testado: handles duplicados no array (rejeitados honestamente), threads de outros processos (inexistentes por modelo).

## ACEITAÇÃO (16 critérios)

| # | Critério | Status |
|---|---|---|
| 1 | `hello_wait_multiple.exe` retorna `42` | ✓ |
| 2 | duas threads realmente criadas | ✓ (log: 2× `CreateThread`; handles `0xF0F0F100/101`) |
| 3 | duas threads em pthreads reais | ✓ (`pthread_create` por chamada; CPU guest por thread — G42/G43) |
| 4 | `WaitForMultipleObjects(2, …, TRUE, INFINITE)` = `WAIT_OBJECT_0` | ✓ (rc≠12) |
| 5 | dois workers realmente terminaram antes da aceitação | ✓ (join real no wait-all; rc≠13/14) |
| 6 | dois handles fechados | ✓ (rc≠15/16) |
| 7 | sem crash/fault/corrupção | ✓ (23 execuções limpas) |
| 8 | ≥20 execuções consecutivas com `42` | ✓ (23/23) |
| 9 | C tests verdes | ✓ (3392/3392) |
| 10 | Swift tests verdes | ✓ (63/63) |
| 11 | warnings = 0 | ✓ |
| 12 | analyzer = 0 | ✓ |
| 13 | PE battery = 15/15 | ✓ |
| 14 | `hello_thread_timeout` = 42 | ✓ |
| 15 | `hello_mutex_timeout` = 42 | ✓ |
| 16 | nenhuma API fora do escopo implementada | ✓ (imports e implementação restritos; WFMO = subconjunto exato) |

**16/16 critérios atendidos.**

## declaração explícita — o que FOI e o que NÃO foi validado

**FOI validado por PE real** (`hello_wait_multiple.exe`, rc=42, contrato 10–16/42, 23/23 execuções): **`WaitForMultipleObjects(2, handles_de_thread, TRUE, INFINITE)`** retornou **`WAIT_OBJECT_0`** após a **conclusão real de duas threads guest** (tarefas independentes em memória compartilhada, `worker1_done = worker2_done = 1` verificados após a espera, joins reais em pthreads host com CPUs guest independentes), e os dois handles foram fechados com `CloseHandle` válido. O subconjunto implementado é **exatamente** esse fluxo; combinações fora dele retornam `WAIT_FAILED` com erro honesto.

**NÃO foi validado / NÃO está implementado**: `WaitForMultipleObjects` com wait-any, nCount≠2, timeout finito, handles de mutex/file/evento; `WaitForMultipleObjectsEx`; `MsgWaitForMultipleObjects`; `GetExitCodeThread`; eventos, semáforos, TLS, mutexes adicionais, novas APIs Win32; mudanças de scheduler. **Uma** instrução SSE2 (`PUNPCKLQDQ`, 66 0F 6C) foi adicionada ao subconjunto do interpretador x64 (SDM-fiel) mediante **autorização explícita do usuário** (opção A) — gap de CPU observado pelo próprio PE; nenhuma outra mudança de CPU.

**Não se afirma compatibilidade geral com Windows nem com jogos comerciais.**
