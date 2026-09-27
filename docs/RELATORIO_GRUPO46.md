# Relatório do Grupo 46 — WaitForSingleObject com timeout finito em THREAD (Validação Real por PE x64)

## STATUS

**CONCLUÍDO COM SUCESSO** — `WaitForSingleObject(hThread, timeout_finito)` com **bloqueio real limitado ao prazo** validado por PE real (`hello_thread_timeout.exe`, **rc=42**, contrato 10–15/42 completo). A limitação documentada no G45 para handles de thread foi **removida**. 23/23 execuções com rc=42 (≥20 consecutivas ✓); regressão completa verde; **nenhuma API fora do escopo implementada**.

## BASELINE OBRIGATÓRIA (estado final do G45, antes de qualquer alteração)

- C tests: `3392 verificações, 0 falhas` ✓ · Swift tests: `Executed 63 tests, with 0 failures` ✓ · warnings: 0 ✓ · analyzer: 0 ✓
- PE battery: `15/15` (28/43/180/70/72/75/98/118/76/81/194/131/55/200/105) ✓
- hello_thread = 42 ✓ · hello_thread_shared = 42 ✓ · hello_mutex = 42 ✓ · hello_mutex_timeout = 42 ✓ · hello_cs = 42 ✓ · hello_virt = 42 ✓ · hello_virt_protect = 42 ✓ · hello_virt_query = 42 ✓ · hello_qpc = 42 ✓ · hello_heap = 42 ✓ · hello_mbwc = 42 ✓ · hello_file = 42 ✓ · hello_file_w = 42 ✓ · hello_file_seek = 42 ✓ · hello_stdio = 5 ✓ · hello_gl12 = 42 ✓ · hello_sse = 7 ✓
- **MD5 antes da alteração: `603378487f3c8531e59b516ac76b8797`** (= estado final do G45) ✓

## PE criado / toolchain / imports

- `realpe/hello_thread_timeout.c` → `Tests/PorticoRuntimeTests/data/hello_thread_timeout.exe` — `x86_64-w64-mingw32-gcc -O2 -s` (0 erros; mesmo GCC 14 POSIX).
- Fluxo: `CreateThread` (→10); worker sinaliza `worker_started=1` e roda ~150 ms (`Sleep` **existente**); main garante o início (janela 2000 ms → 11); QPC; `Wait(hworker, 50)`; QPC; ≠`WAIT_TIMEOUT` (→12); tempo ∉ `[25,250]` ms (→13); `Wait(hworker, INFINITE)` ≠ `WAIT_OBJECT_0` (→14); `CloseHandle(hworker)` FALSE (→15); `return 42`.
- imports (objdump): KERNEL32 = `CloseHandle, CreateThread, DeleteCriticalSection, EnterCriticalSection, GetLastError, InitializeCriticalSection, LeaveCriticalSection, QueryPerformanceCounter, QueryPerformanceFrequency, SetUnhandledExceptionFilter, Sleep, TlsGetValue, VirtualProtect, VirtualQuery, WaitForSingleObject` + msvcrt (CRT). **Nada de novo**: sem eventos, semáforos, TLS, `CreateMutexW`, `OpenMutex`, mutex nomeado, `GetExitCodeThread`, novas APIs Win32 ✓.

## primeiro blocker encontrado

**`rc=13`** na primeira execução com o runtime **completamente intocado** (MD5 idêntico antes/depois):
- `Wait(hworker, 50)` retornou `WAIT_TIMEOUT` **imediatamente** (check 12 **passou** — o retorno era o esperado);
- **tempo decorrido ≈ 0 ms** (processo inteiro = **0,40 ms**) ⇒ **check 13 reprovou** (fora de `[25,250]` ms) — **primeiro ponto exato de falha**;
- registro completo: handle `hworker=0xF0F0F100` criado (rc≠10); worker iniciou (rc≠11) e segurava `Sleep(150)` no momento do exit; `Wait(INFINITE)`/`CloseHandle` = n/d (não alcançados); `exec=821`; log oficial = 46 eventos (`rc=13 log=46`); **tempo registrado mesmo reprovando: ≈ 0 ms** (exigido pelo contrato).

## causa raiz

O caminho de handles de **thread** de `f_WaitForSingleObject` (G42) tratava timeout finito como **poll** (`!t->done → WAIT_TIMEOUT` imediato) — exatamente a limitação que o G45 documentou e deixou para trás ("em handles de thread, o timeout finito continua não bloqueando — fora do alvo do G45").

## correção aplicada

**Somente o alvo** (`WaitForSingleObject` em handle de thread com timeout finito), no ramo de thread de `f_WaitForSingleObject`:
- `ms == 0` → **poll imediato** sem bloqueio (`!done → WAIT_TIMEOUT`; `done → join → WAIT_OBJECT_0`) — inalterado em semântica;
- `ms > 0` finito → **bloqueio real limitado ao prazo** via `pthread_timedjoin_np` com deadline `clock_gettime(CLOCK_REALTIME) + ms`: thread termina dentro do prazo ⇒ `WAIT_OBJECT_0`; prazo expira com a thread ainda executando ⇒ `WAIT_TIMEOUT` (0x102);
- `INFINITE` → **join real preservado** (comportamento validado desde o G42);
- thread já terminada ⇒ `WAIT_OBJECT_0` imediato (handle sinalizado, semântica Windows).
- **A lógica de mutex NÃO foi duplicada nem alterada**: o timeout reusa a infraestrutura de **join já existente** na variante com prazo (`pthread_timedjoin_np`, extensão GNU do glibc; declaração exposta com `#define _GNU_SOURCE` no topo do arquivo). Mutexes/G45 intocados.

Comportamento desejado × obtido: `ms==0` poll ✓ · `ms>0` + executando → bloqueio real limitado ✓ · termina dentro do prazo → `WAIT_OBJECT_0` ✓ · prazo expira → `WAIT_TIMEOUT` ✓ · `INFINITE` → join real preservado ✓.

## arquivos alterados

- `Sources/PorticoRuntime/src/pr_win32.c` (arquivo ÚNICO):
  - L1: `#define _GNU_SOURCE` (expõe `pthread_timedjoin_np`);
  - L2252–2268: ramo de thread de `f_WaitForSingleObject` (poll `ms==0` + `pthread_timedjoin_np` para timeout finito; `INFINITE` preservado).

## runtime MD5 antes/depois

- Antes: `603378487f3c8531e59b516ac76b8797`
- Depois: `393d10bbe8f398dda14e022ea40a557e` (alteração = somente o timeout finito em handle de thread)

## resultado do PE

`exited=1 rc=42` — contrato **10–15/42** completo, nenhum outro código emitido.

## tempo real medido pelo QPC

- **Pelo QPC do convidado (instrumento do PE)**: `Wait(hworker, 50)` decorrido ∈ `[25,250]` ms (check 13 aprovado; ~50 ms) — registrado mesmo quando reprova (1ª execução: ≈ 0 ms → rc 13).
- **Pelos timestamps do log (corroboração)**: `Wait(hworker, 50)` despachado em `t=540467` e o `QPC` seguinte em `t=540518` ⇒ **≈ 51 ms bloqueando**; `Wait(INFINITE)` despachado em `t=540518` e `CloseHandle` em `t=540618` ⇒ **≈ 100 ms** esperando a worker terminar (join real); `[PROCESS] exit code 42 after 150.84 ms` ⇒ a worker cumpriu seus `Sleep(150)`.

## número de execuções repetidas

**23 execuções** pós-correção (1 oficial + 22 consecutivas): **23/23 `rc=42`** ✓ (critério: ≥20 consecutivas). Sem crash, fault ou corrupção. **Nota honesta**: a contagem de eventos de log varia entre execuções (63–8517) porque o **spin de `QueryPerformanceCounter` do próprio PE** despacha (e loga) a cada iteração até a worker sinalizar — artefato do PE, não do runtime; o rc é determinístico.

## C tests / Swift tests / warnings / analyzer / PE battery

- C tests: `3392 verificações, 0 falhas` ✓ · Swift tests: `Executed 63 tests, with 0 failures` ✓
- warnings: 0 ✓ · analyzer: 0 ✓
- PE battery: `15/15` (28/43/180/70/72/75/98/118/76/81/194/131/55/200/105) ✓
- Checklist completo canônico: thread=42/30 · thread_shared=42/33 · mutex=42/39 · mutex_timeout=42/40 · **thread_timeout=42** · cs=42/44 · virt=42/64 · virt_protect=42/31 · virt_query=42/33 · qpc=42/96 · heap=42/31 · mbwc=42/29 · file=42/36 · file_w=42/33 · file_seek=42/33 · stdio=5/155 · gl12=42/116 · sse=7/209 ✓

## limitações restantes (registradas — critério 10)

- Deadline do `pthread_timedjoin_np` em `CLOCK_REALTIME` (mesma escolha do timeout de mutex do G45); um salto do relógio de sistema durante a espera poderia distorcer o prazo (não observado).
- `pthread_timedjoin_np` = extensão GNU do glibc (a variante com prazo do `pthread_join` já usado) — não-portável a pthreads sem extensões GNU; `#define _GNU_SOURCE` adicionado ao topo de `pr_win32.c`.
- Janela de espera do início da worker no PE = 2000 ms (check 11); sob host absurdamente lento a mais que isso, o PE reprova com 11 (não observado).
- Após um `WAIT_TIMEOUT`, a thread continua **joinable** — esperas seguintes (`INFINITE`, finitas ou poll) e `CloseHandle` seguem funcionando (validado no fluxo: `INFINITE` após o timeout ⇒ `WAIT_OBJECT_0`).
- Não testado/fora de escopo (não implementado): eventos, semáforos, TLS, `CreateMutexW`, `OpenMutex`, mutex nomeado, `GetExitCodeThread`, `WaitForMultipleObjects`, timeouts em outros tipos de handle, mudanças de scheduler/CPU/harness.
- Comportamento de mutex validado no G45 permanece **inalterado** (regressão `hello_mutex_timeout=42` ✓).

## ACEITAÇÃO (10 critérios)

| # | Critério | Status |
|---|---|---|
| 1 | `hello_thread_timeout.exe` retorna `42` | ✓ |
| 2 | `Wait(hworker, 50)` retorna `WAIT_TIMEOUT` | ✓ (check 12) |
| 3 | tempo medido em `[25,250]` ms | ✓ (check 13; ≈50 ms / 51 ms no log) |
| 4 | `Wait(hworker, INFINITE)` retorna `WAIT_OBJECT_0` | ✓ (check 14; join real) |
| 5 | `CloseHandle` funciona | ✓ (check 15) |
| 6 | sem crash/fault/corrupção | ✓ (23 execuções limpas) |
| 7 | regressão completa verde | ✓ (3392 · 63 · 0 · 0 · 15/15 · checklist) |
| 8 | ≥20 execuções consecutivas com `rc=42` | ✓ (23/23) |
| 9 | nenhuma API fora do escopo | ✓ (imports e implementação restritos ao alvo) |
| 10 | limitações restantes registradas | ✓ (seção acima) |

**10/10 critérios atendidos.**

## declaração explícita — o que FOI e o que NÃO foi validado

**FOI validado por PE real** (`hello_thread_timeout.exe`, rc=42, contrato 10–15/42): `WaitForSingleObject` com **timeout finito em handle de thread** — com a thread guest ainda executando (worker em pthread própria, `Sleep(150)`), `WaitForSingleObject(hworker, 50)` **bloqueou por ~50 ms reais** (QPC do convidado; timestamps do log = 51 ms; margem `[25,250]` ms) e retornou `WAIT_TIMEOUT`; com a thread terminando dentro do prazo, retorna `WAIT_OBJECT_0`; `WaitForSingleObject(hworker, INFINITE)` manteve o **join real** e retornou `WAIT_OBJECT_0`; `CloseHandle` fechou o handle com liberação real de recursos. A correção se limitou ao caminho de timeout finito em handle de thread (`pthread_timedjoin_np` — variante com prazo do join já existente; lógica de mutex não duplicada nem alterada).

**NÃO foi validado / NÃO está implementado**: eventos, semáforos, TLS, `CreateMutexW`, `OpenMutex`, mutex nomeado, `GetExitCodeThread`, `WaitForMultipleObjects`, `WAIT_ABANDONED`, timeouts finitos em outros tipos de handle, sincronização genérica adicional, mudanças de scheduler ou de modelo de CPU, compatibilidade com qualquer aplicação ou jogo comercial.

**Não se afirma compatibilidade geral com Windows nem com jogos comerciais.**
