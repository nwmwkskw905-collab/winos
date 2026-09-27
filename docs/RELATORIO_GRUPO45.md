# Relatório do Grupo 45 — Timeout real de WaitForSingleObject em mutex (Validação Real por PE x64)

## STATUS

**CONCLUÍDO COM SUCESSO** — `WaitForSingleObject(mutex, timeout_finito)` com **bloqueio real limitado no tempo** validado por PE real (`hello_mutex_timeout.exe`, **rc=42**, contrato 10–17/42 completo): `Wait(mutex, 50)` com o mutex ocupado bloqueou **50 ms reais** (medidos pelo QPC do convidado e pelos timestamps do log) e retornou `WAIT_TIMEOUT`; após a liberação pela worker, `Wait(mutex, INFINITE)` retornou `WAIT_OBJECT_0`. 26/26 execuções estáveis; regressão completa verde.

**Nota de processo**: o briefing do grupo foi truncado no meio do fluxo ("Fecha…"). O contrato de **códigos de diagnóstico (opção A: 10–17/42)** e a **margem de tempo ([25, 250] ms — opção A)** foram confirmados via `ask_user`; os campos do relatório seguem o padrão dos grupos recentes.

## BASELINE (estado final do G44, confirmado antes de tudo)

- C tests: `3392 verificações, 0 falhas` ✓ · Swift tests: `Executed 63 tests, with 0 failures` ✓ · warnings: 0 ✓ · analyzer: 0 ✓
- PE battery: `15/15` (28/43/180/70/72/75/98/118/76/81/194/131/55/200/105) ✓
- hello_thread = 42 ✓ · hello_thread_shared = 42 ✓ · hello_mutex = 42 ✓ · hello_cs = 42 ✓ · hello_virt = 42 ✓ · hello_virt_protect = 42 ✓ · hello_virt_query = 42 ✓ · hello_qpc = 42 ✓ · hello_heap = 42 ✓ · hello_mbwc = 42 ✓ · hello_file = 42 ✓ · hello_file_w = 42 ✓ · hello_file_seek = 42 ✓ · hello_stdio = 5 ✓ · hello_gl12 = 42 ✓ · hello_sse = 7 ✓
- **MD5 no início: `5b346959df1e035b51f672a3ab705df7`** (= estado final do G44) ✓

## PE criado

- `realpe/hello_mutex_timeout.c` → `Tests/PorticoRuntimeTests/data/hello_mutex_timeout.exe`
- Fluxo exato do briefing (§5–§8): `mutex = CreateMutexA(NULL, FALSE, NULL)` (→10); `hworker = CreateThread(NULL, 0, worker, NULL, 0, NULL)` (→11); aguardar `worker_started == 1` (spin); `QueryPerformanceFrequency` + timestamp `QueryPerformanceCounter`; `result = WaitForSingleObject(mutex, 50)`; timestamp; `result != WAIT_TIMEOUT` (→12); tempo decorrido fora de `[25, 250]` ms (→13); aguardar/confirmar `worker_released == 1` (spin); `WaitForSingleObject(mutex, INFINITE) != WAIT_OBJECT_0` (→14); `ReleaseMutex(mutex)` FALSE (→15); `CloseHandle(hworker)` FALSE (→16); `CloseHandle(mutex)` FALSE (→17); `return 42`.
- Worker (§6): `WaitForSingleObject(mutex, INFINITE)` (falha → `return 0x11`, não conferido); `worker_started = 1`; `Sleep(150)` (infraestrutura de Sleep **existente** — não implementada neste grupo); `ReleaseMutex(mutex)`; `worker_released = 1`; `return 0x41` (não conferido).

## toolchain

`x86_64-w64-mingw32-gcc -O2 -s` (0 erros), **mesmo GCC 14 POSIX** dos grupos anteriores.

## imports (objdump)

KERNEL32 = `CloseHandle, CreateMutexA, CreateThread, DeleteCriticalSection, EnterCriticalSection, GetLastError, InitializeCriticalSection, LeaveCriticalSection, QueryPerformanceCounter, QueryPerformanceFrequency, ReleaseMutex, SetUnhandledExceptionFilter, Sleep, TlsGetValue, VirtualProtect, VirtualQuery, WaitForSingleObject` + msvcrt (CRT padrão). **Nenhuma** TLS/evento/semaforo/`CreateMutexW`/`OpenMutex`/mutex nomeado/`GetExitCodeThread`/nova API de sincronização ✓ (§2 cumprido).

## primeira execução (runtime COMPLETAMENTE INTOCADO)

| Item | Registro |
|---|---|
| rc | **13** |
| primeiro ponto de falha | **check 13 (tempo decorrido)** — `Wait(mutex, 50)` retornou `WAIT_TIMEOUT` **imediatamente** (limitação documentada no G44: "timeout finito não bloqueia") |
| handle do mutex | `0xF0F0F200` (criado; rc≠10) |
| handle da worker | `0xF0F0F100` (criado; rc≠11) |
| Wait da worker (`INFINITE`) | `WAIT_OBJECT_0` (adquiriu; marcou `worker_started=1`; entrou em `Sleep(150)`) |
| `Wait(mutex, 50)` da main | `WAIT_TIMEOUT` (0x102) — **check 12 PASSOU**; mas instantâneo |
| tempo decorrido (check 13) | **≈ 0 ms** (QPC delta ≈ 0; processo inteiro = **0,46 ms**) — fora de [25, 250] ⇒ **rc 13** |
| `worker_released` | 0 no momento do `exit(13)` (a worker ainda dormia 150 ms; `ReleaseMutex` dela nunca chegou a rodar) |
| Wait `INFINITE` da main / `ReleaseMutex` / `CloseHandle`s | n/d (não alcançados) |
| número de instruções | `exec = 1571` (thread principal) |
| log | 35 eventos (série: `CreateMutexA → CreateThread → Wait(worker) → Sleep → QPF → QPC → Wait(50) → QPC → exit(13)`) |
| **MD5** | `5b346959df1e035b51f672a3ab705df7` antes == depois ✓ |

## rc (pós-correção)

**42** (contrato 10–17/42 — nenhum outro código emitido). `exited=1 rc=42 log=40`.

## primeiro blocker

**Timeout finito de `WaitForSingleObject` em mutex não bloqueia** — retorna `WAIT_TIMEOUT` imediatamente (sem esperar o prazo pedido); o PE detectou via o check 13 (`rc=13`, elapsed ≈ 0 ms).

## causa raiz

O caminho de timeout finito em `f_WaitForSingleObject` (ramo de mutex, G44) fazia um único `pthread_mutex_trylock` e devolvia `WAIT_TIMEOUT` logo em seguida — a **limitação que o próprio G44 documentou** ("poll (0) ou timeout finito: não bloqueia"). Não havia espera limitada no tempo.

## correção aplicada

**Somente o primeiro blocker = o único alvo do grupo** — caminho de timeout finito em mutex:
- `ms == 0`: poll imediato (`trylock` → `WAIT_OBJECT_0`/`WAIT_TIMEOUT`) — inalterado;
- `ms > 0` finito: **`pthread_mutex_timedlock` com deadline `clock_gettime(CLOCK_REALTIME) + ms`** = bloqueio **real** limitado no tempo entre pthreads host → sucesso `WAIT_OBJECT_0`, prazo esgotado `WAIT_TIMEOUT` (0x102). Mesma base de tempo de `Sleep` (`nanosleep` real) e `QueryPerformanceCounter` (`mono_ns()` real) — auditar: `f_Sleep` = `nanosleep` de parede real (L904); `f_QueryPerformanceCounter` = nanossegundos monotônicos reais (L341); `f_QueryPerformanceFrequency` = 10⁹ (L332).
- Nada mais foi alterado: caminho `INFINITE`, recursão, poll, ramos de thread/file/std, demais APIs — intocados.

## arquivos alterados

- `Sources/PorticoRuntime/src/pr_win32.c` (arquivo ÚNICO): L2216–2232 — substituição do trecho final do ramo de mutex de `f_WaitForSingleObject` (o antigo `trylock` único + `WAIT_TIMEOUT` imediato) pelo poll (`ms==0`) + `pthread_mutex_timedlock` com deadline real.

## alterações no runtime

Apenas a acima (escopo = timeout finito em mutex). MD5 `5b346959…` → `603378487f3c8531e59b516ac76b8797`.

## alterações no harness

**Nenhuma** (`tools/` intocado; probe descartável de `build/` apenas para registro).

## Resultado pós-correção — tempos REAIS provados

Série do log (40 eventos) com timestamps em ms — **o tempo decorrido é o próprio registro**:

```
[243405] QPC (t0) → WaitForSingleObject(mutex, 50)   ← main despacha com mutex ocupado
[243455] QPC (t1)                                      ← 50 ms DEPOIS = bloqueou o prazo
[243555] ReleaseMutex                                  ← worker soltou após Sleep(150) = 150 ms
[243555] WaitForSingleObject(INFINITE) → WAIT_OBJECT_0 ← re-aquisição imediata
[243555] ReleaseMutex → 2× CloseHandle → exit(42)
[PROCESS] exit code 42 after 150.60 ms
```

| Medida | Valor | Margem [25, 250] ms |
|---|---|---|
| `Wait(mutex, 50)` decorrido (QPC do convidado + log) | **50 ms** (t1−t0 = 243455−243405) | ✓ |
| Worker segurando o mutex (`Sleep(150)`) | **150 ms** (243555−243405) | — |
| Processo total | **150,60 ms** | — |

- `Wait(mutex, 50)` = `WAIT_TIMEOUT` ✓ (check 12) · tempo 50 ms ✓ (check 13) · `Wait(mutex, INFINITE)` = `WAIT_OBJECT_0` ✓ (14) · `ReleaseMutex` = TRUE ✓ (15) · `CloseHandle(hworker)` = TRUE ✓ (16) · `CloseHandle(mutex)` = TRUE ✓ (17) · **rc 42** ✓.
- Contrato confirmado (opção A): `10`=CreateMutexA · `11`=CreateThread · `12`=`Wait(50)`≠`WAIT_TIMEOUT` · `13`=tempo fora da margem · `14`=`Wait(INFINITE)`≠`WAIT_OBJECT_0` · `15`=`ReleaseMutex` FALSE · `16`=`CloseHandle(hworker)` FALSE · `17`=`CloseHandle(mutex)` FALSE · `42`=fluxo completo.

## número de execuções repetidas

**26 execuções** pós-correção (1 oficial + 1 probe + **25 consecutivas** de observação de corrida): **26/26 `rc=42 log=40`** — determinístico, nenhuma falha intermitente.

## C tests / Swift tests / warnings / analyzer / PE battery

- C tests: `3392 verificações, 0 falhas` ✓ · Swift tests: `Executed 63 tests, with 0 failures` ✓
- warnings: 0 ✓ · analyzer: 0 ✓
- PE battery: `15/15` (28/43/180/70/72/75/98/118/76/81/194/131/55/200/105) ✓
- hello_thread = 42/30 ✓ · hello_thread_shared = 42/33 ✓ · hello_mutex = 42/39 ✓ · **hello_mutex_timeout = 42/40** ✓ · hello_cs = 42/44 ✓ · hello_virt = 42/64 · hello_virt_protect = 42/31 · hello_virt_query = 42/33 · hello_qpc = 42/96 · hello_heap = 42/31 · hello_mbwc = 42/29 · hello_file = 42/36 · hello_file_w = 42/33 · hello_file_seek = 42/33 · hello_stdio = 5/155 · hello_gl12 = 42/116 · hello_sse = 7/209 ✓

## hello_mutex_timeout

`exited=1 rc=42 log=40` ✓ (26/26 execuções).

## MD5 antes/depois

- Antes: `5b346959df1e035b51f672a3ab705df7`
- Depois: `603378487f3c8531e59b516ac76b8797` (alteração = só o timeout finito em mutex)

## limitações

- Timeout finito validado **em mutex** (o único alvo, §2). Em **handles de thread**, o timeout finito continua **não bloqueando** (limitação documentada no G42 — fora do alvo deste grupo; `INFINITE` = join real continua válido).
- `pthread_mutex_timedlock` usa deadline em `CLOCK_REALTIME` (`pthread_mutex_clocklock` indisponível); o convidado mede com relógio monotônico (`mono_ns`) — para janelas curtas a diferença é irrelevante; um salto do relógio de sistema durante a espera poderia distorcer o prazo (não observado).
- Margem validada = `[25, 250]` ms (contrato confirmado) para `Wait(..., 50)`; o PE aceita qualquer ordem de liberação da worker e tolera scheduler do host dentro da margem.
- `Sleep(150)` da worker usa a infraestrutura **existente** (`f_Sleep` = `nanosleep` real) — não implementada neste grupo ✓. Não testado/adicionado: TLS, eventos, semáforos, condition variables, `CreateMutexW`, `OpenMutex`, mutex nomeado, `WAIT_ABANDONED`, `GetExitCodeThread`, novas APIs de sincronização (§2).
- Timeout finito para outros tipos de handle (eventos etc.) não existe — sem objeto, `WAIT_FAILED` + `ERROR_INVALID_HANDLE` (sem fingir sincronização genérica).
- Retornos `0x41`/`0x11` da worker não conferidos (`GetExitCodeThread` proibido).
- Não se afirma compatibilidade geral com aplicações Windows.

## Critérios de aceitação (derivados do objetivo/fluxo do briefing — a seção formal foi truncada)

| Critério | Status |
|---|---|
| `WaitForSingleObject(mutex, 50)` com mutex ocupado retorna `WAIT_TIMEOUT` | ✓ (check 12) |
| O tempo decorrido é compatível com ≈50 ms (margem [25, 250] ms) | ✓ (50 ms medidos por QPC e timestamps do log; check 13) |
| Após a worker liberar, `Wait(mutex, INFINITE)` retorna `WAIT_OBJECT_0` | ✓ (check 14) |
| `ReleaseMutex` funciona | ✓ (check 15) |
| Handles (`hworker`, `mutex`) fechados | ✓ (checks 16/17) |
| PE termina com `rc=42` | ✓ |
| Regressão permanece verde | ✓ (3392 · 63 · 0 · 0 · 15/15 · todos os hello_*) |
| Nenhuma outra API implementada | ✓ (§2) |

## declaração explícita

**`WaitForSingleObject` com timeout finito em mutex foi validado por PE real** (`hello_mutex_timeout.exe`, **rc=42**, contrato 10–17/42 completo, 26/26 execuções estáveis): com o mutex **realmente ocupado** por uma thread guest (worker em pthread própria segurando-o por 150 ms), `WaitForSingleObject(mutex, 50)` **bloqueou por 50 ms reais** (QPC do convidado e timestamps do log; margem [25, 250] ms) e retornou `WAIT_TIMEOUT`; após `ReleaseMutex` da worker, `WaitForSingleObject(mutex, INFINITE)` retornou `WAIT_OBJECT_0` com re-aquisição real, seguido de `ReleaseMutex` e fechamento válido dos dois handles. A correção se limitou ao caminho de timeout finito em mutex (`pthread_mutex_timedlock` com deadline real).

**Não se afirma compatibilidade geral com aplicações Windows.**
