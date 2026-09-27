# Relatório do Grupo 44 — Mutex + sincronização real entre threads guest (Validação Real por PE x64)

## STATUS

**CONCLUÍDO COM SUCESSO** — exclusão mútua **real** entre duas threads guest concorrentes (pthreads reais, CPUs guest independentes, G43) validada por PE real com **`CreateMutexA` + `WaitForSingleObject` + `ReleaseMutex` + `CloseHandle`**. `hello_mutex.exe` terminou com **rc=42** (contrato 10–20/42 completo) e verificação direta de exclusão mútua (`violation == 0`). 15/15 critérios de aceitação atendidos.

## BASELINE (estado final do G43, confirmado antes de tudo)

- C tests: `3392 verificações, 0 falhas` ✓ · Swift tests: `Executed 63 tests, with 0 failures` ✓ · warnings: 0 ✓ · analyzer: 0 ✓
- PE battery: `15/15` (28/43/180/70/72/75/98/118/76/81/194/131/55/200/105) ✓
- hello_thread = 42 ✓ · hello_thread_shared = 42 ✓ · hello_cs = 42 ✓ · hello_virt = 42 ✓ · hello_virt_protect = 42 ✓ · hello_virt_query = 42 ✓ · hello_qpc = 42 ✓ · hello_heap = 42 ✓ · hello_mbwc = 42 ✓ · hello_file = 42 ✓ · hello_file_w = 42 ✓ · hello_file_seek = 42 ✓ · hello_stdio = 5 ✓ · hello_gl12 = 42 ✓ · hello_sse = 7 ✓
- **MD5 no início: `852afae4b659a87e9d7f572ebc02ad0d`** (= estado final do G43) ✓

## PE criado

- `realpe/hello_mutex.c` → `Tests/PorticoRuntimeTests/data/hello_mutex.exe`
- Fluxo exato do §5–§11: `mutex = CreateMutexA(NULL, FALSE, NULL)` (NULL→10); `h1/h2 = CreateThread(NULL, 0, workerN, NULL, 0, NULL)` (→11/12); `Wait(h1/h2, INFINITE)` (→13/14); `worker1_entered==1` (→15); `worker2_entered==1` (→16); `shared_value ∈ {0x11111111, 0x22222222}` (→17); `violation != 0` (→17 — **colisão do próprio contrato**: §9 e §11 atribuem ambos os checks ao código 17; adotado 17 para os dois, sem inventar código extra); `CloseHandle(h1/h2/mutex)` (→18/19/20); `return 42`. Cada worker (§6/§7): `Wait(mutex, INFINITE)` (≠`WAIT_OBJECT_0`→`0x11`/`0x12`), check `inside`→`violation`, `inside=1`, escrita do valor compartilhado + `workerN_entered=1`, `inside=0`, `ReleaseMutex(mutex)`, `return 0x41`/`0x42` (retornos das workers **não conferidos** — `GetExitCodeThread` fora do escopo, §2). Verificação de exclusão mútua (§11): `volatile LONG inside, violation` — sobreposição de seção ⇒ `violation=1`.

## toolchain

`x86_64-w64-mingw32-gcc -O2 -s` (0 erros), **mesmo toolchain POSIX GCC 14** dos grupos anteriores (`~/.cache/mingwtc`).

## imports (objdump)

KERNEL32 = `CloseHandle, CreateMutexA, CreateThread, DeleteCriticalSection, EnterCriticalSection, GetLastError, InitializeCriticalSection, LeaveCriticalSection, ReleaseMutex, SetUnhandledExceptionFilter, VirtualProtect, VirtualQuery, WaitForSingleObject` + `Sleep`, `TlsGetValue` (startup mingw posix — desvio documentado no G42; não chamados neste PE). msvcrt = CRT padrão. **Nenhuma** TLS/evento/semaforo/`CreateEvent`/`CreateSemaphore`/`GetExitCodeThread`/`TerminateThread`/`CreateRemoteThread`/`CreateMutexW`/`OpenMutex`/`ReleaseSemaphore` importada ✓ (§2/§17 cumpridos).

## primeira execução (runtime COMPLETAMENTE INTOCADO)

| Item | Registro |
|---|---|
| rc | **nenhum** — parada **antes da execução** (`pr_peproc_prepare` falhou) |
| primeiro ponto de falha | **bind/carga de `KERNEL32.dll!CreateMutexA`** — `EXECUTION STOPPED / Unsupported Win32 API / KERNEL32.dll / CreateMutexA / Address: (—) / Technical: API conhecida do módulo mas sem implementação` |
| handle do mutex | n/d (não criado) |
| handles das threads | n/d |
| retornos dos Waits | n/d (nenhum chamado) |
| retornos dos ReleaseMutex | n/d (nenhum chamado) |
| `worker1_entered` / `worker2_entered` | n/d |
| `inside` / `violation` / `shared_value` | n/d |
| CloseHandle | n/d (nenhum chamado) |
| número de instruções | 0 (execução nunca começou) |
| log | diagnóstico acima (probe) |
| **MD5** | `852afae4b659a87e9d7f572ebc02ad0d` antes == depois ✓ |

## rc

**42** (pós-correção; contrato 10–20/42 — nenhum outro código emitido). `exited=1 rc=42 log=39`.

## primeiro blocker

**`CreateMutexA` sem implementação** — a carga do PE rejeita o import não implementado (parada honesta no `prepare`, nunca sucesso falso).

## causa raiz

1. `CreateMutexA` e `ReleaseMutex` **não existiam** no runtime (nem catálogo, nem handler — auditoria §14 confirmou via grep vazio);
2. `WaitForSingleObject`/`CloseHandle` (G42) tratavam apenas handles de **thread** (files/std no caso do CloseHandle) — **claramente incompletos para o fluxo testado** (handles de mutex);
3. **Gap arquitetural do G42**: CPUs de worker criadas **sem trap `INT 0x2E`** — só a thread principal despachava Win32; o fluxo do G44 exige `WaitForSingleObject`/`ReleaseMutex` **a partir das workers**. Sem esse plumbing nenhum alvo do grupo poderia funcionar dentro das workers.

## correção aplicada

**Somente o alvo do grupo** (a implementação coerente do fluxo testado), nada além:
- `f_CreateMutexA` (L1982): subconjunto honesto (`lpSecurityAttributes=NULL`, `lpName=NULL` — mutex nomeado = fora do escopo §17 → `NULL` + `ERROR_INVALID_PARAMETER`); `bInitialOwner=TRUE` ⇒ owned real pela thread chamadora (`pthread_mutex_lock`); handle `0xF0F0F200+i`; sem slot ⇒ `ERROR_NOT_ENOUGH_MEMORY` (8).
- `f_ReleaseMutex` (L2006): ownership por thread (`owner`/`rec`); não-proprietária ⇒ `FALSE` + `ERROR_NOT_OWNER` (288) — nunca sucesso falso; liberação real (`pthread_mutex_unlock`) na última saída da contagem recursiva.
- `f_WaitForSingleObject` (ramo mutex L2189): `INFINITE` ⇒ **bloqueio real entre pthreads host** (`pthread_mutex_lock`) → `WAIT_OBJECT_0`; `0`/finito ⇒ `trylock`/`WAIT_TIMEOUT` (não bloqueia — limitação documentada); recursivo por thread (semântica Windows). Ramo de thread (join) **preservado intacto**.
- `f_CloseHandle` (ramo mutex L2249): destruição real (`pthread_mutex_destroy`); double-close ⇒ `FALSE` + `ERROR_INVALID_HANDLE`; faixas de handle **disjuntas** (threads `0xF0F0F100+`, mutexes `0xF0F0F200+`) — sem confusão de tipos; ramo files/std/threads **preservado** (`test_vfs` verde).
- `w32_thr_apitrap` (L2029) registrado nas CPUs das workers (L2156): **despacho Win32 via `INT 0x2E` a partir das threads guest**, com a mesma maquinaria do `proc_api_trap64` (peproc): índice do stub = índice do catálogo (`pr_win32_catalog` + `pr_win32_call_entry`), ABI x64 (RCX/RDX/R8/R9 ou XMM0-3; 5º+ em `[rsp+0x28…]`), retorno em RAX; chamada não suportada ⇒ log honesto `Unsupported Win32 API | <dll> | <API>` + parada (RAX=0, **nunca sucesso falso**).
- `pr_win32_destroy` (L5051): `w32_mutexes_shutdown` (limpeza de mutexes no teardown).

**Não implementado (proibido/fora de escopo, §2/§17)**: TLS, eventos, semáforos, Critical Section nova, condition variables, `CreateEvent`, `CreateSemaphore`, `GetExitCodeThread`, `TerminateThread`, `CreateRemoteThread`, sincronização genérica adicional, scheduler avançado, mutex nomeado, `CreateMutexW`, `OpenMutex`, `ReleaseSemaphore`, abandono de mutex (`WAIT_ABANDONED`), timeout finito real. **Nenhum lock interno adicionado ao interpretador** (§1) — o bloqueio real é o `pthread_mutex` do objeto mutex do convidado.

## arquivos alterados

- `Sources/PorticoRuntime/src/pr_win32.c` (arquivo ÚNICO alterado):
  - L38–45: `PR_WIN32_H_MUTEX_BASE 0xF0F0F200`, `W32_ERROR_NOT_OWNER 288`, `W32_ERROR_NOT_ENOUGH_MEMORY 8`;
  - L163–171: `w32_mutex` (`used/owned/rec`, `pthread_mutex_t`, `owner`);
  - L227: campo `mutexes[8]` no `pr_win32_ctx`;
  - L1963–2027: `w32_mutex_slot` / `w32_mutexes_shutdown` / `f_CreateMutexA` / `f_ReleaseMutex`;
  - L2029–2123: `w32_thr_apitrap` (despacho Win32 nas workers);
  - L2156: `pr_cpu64_set_trap(cpu, w32_thr_apitrap, ctx)` em `f_CreateThread`;
  - L2189–2217: ramo de mutex em `f_WaitForSingleObject`;
  - L2249–2262: ramo de mutex em `f_CloseHandle`;
  - L4743–4744: `IMPL` de `CreateMutexA` (args=12) e `ReleaseMutex` (args=4);
  - L5051: `w32_mutexes_shutdown` em `pr_win32_destroy`.

## alterações no runtime

As descritas acima (escopo exato = alvo do grupo + plumbing de despacho das workers). MD5 antes `852afae4…` → depois `5b346959df1e035b51f672a3ab705df7`.

## alterações no harness

**Nenhuma** (`tools/` intocado; probe descartável `build/virt_probe.c` apenas para registro).

## handle do mutex / handles das threads

- `mutex = 0xF0F0F200` (slot 0 — primeiro livre; derivado da construção determinística do slot scan; rc≠10 prova não-NULL e rc≠20 prova `CloseHandle(mutex)` TRUE).
- `h1 = 0xF0F0F100`, `h2 = 0xF0F0F101` (idem; rc≠11/12).

## resultado dos Waits

| Chamada | Resultado | Instrumento |
|---|---|---|
| `WaitForSingleObject(mutex, INFINITE)` na worker 1 | `WAIT_OBJECT_0` (aquisição real) | worker prosseguiu à seção (rc≠15) |
| `WaitForSingleObject(mutex, INFINITE)` na worker 2 | `WAIT_OBJECT_0` (após **bloqueio real** — só adquiriu após o `Release` da outra) | worker prosseguiu à seção (rc≠16) |
| `WaitForSingleObject(h1, INFINITE)` na main | `WAIT_OBJECT_0` (join real) | rc≠13 |
| `WaitForSingleObject(h2, INFINITE)` na main | `WAIT_OBJECT_0` (join real) | rc≠14 |

**Evidência da exclusão mútua no log (39 eventos)**: `CreateMutexA → CreateThread → CreateThread → Wait → Wait → Wait → ReleaseMutex → ReleaseMutex → Wait → 3× CloseHandle → exit(42)` — a 2ª worker despachou `Wait(mutex)` **enquanto a 1ª detinha o mutex** (registrada antes de qualquer `Release`) e só prosseguiu após o `Release` da primeira. As duas `ReleaseMutex` **provam o despacho Win32 a partir das workers** (a main nunca chama `ReleaseMutex`). O check `inside`/`violation` é a verificação direta: **`violation == 0`** ⇒ nenhuma sobreposição de seção protegida.

## resultado dos ReleaseMutex

- `ReleaseMutex` da worker 1: **TRUE** (liberação real — sem ela a 2ª worker bloquearia INFINITE; provado pelo avanço dela e pelo rc=42).
- `ReleaseMutex` da worker 2: **TRUE** (idem).
- (Os BOOLs não são conferidos pelo PE — §6/§7 não determinam check; a prova é comportamental, acima.)

## worker1_entered / worker2_entered / violation / shared_value / inside

| Item | Valor | Instrumento |
|---|---|---|
| `worker1_entered` | 1 | rc ≠ 15 |
| `worker2_entered` | 1 | rc ≠ 16 |
| `violation` | **0** | rc ≠ 17 (check §11) |
| `shared_value` | 0x11111111 **ou** 0x22222222 (não-determinístico por projeto; ordem das workers livre, §12) | rc ≠ 17 (check §9) |
| `inside` | 0 ao final; 1 apenas dentro da seção protegida sob o mutex | `violation==0` prova ausência de sobreposição |

## CloseHandles

- `CloseHandle(h1)` = TRUE (rc≠18) · `CloseHandle(h2)` = TRUE (rc≠19) · `CloseHandle(mutex)` = TRUE (rc≠20). Série do log: 3× `CloseHandle`. Double-close = `FALSE` + `ERROR_INVALID_HANDLE` (semântica consistente com files/threads; `test_vfs` verde).

## número de execuções repetidas (§16)

- Pós-correção: **27 execuções** no total (1 oficial + 1 probe de registro + **25 consecutivas** de observação de corrida).
- **27/27 `rc=42 log=39`** — determinístico, sem falha intermitente, sem corrida manifestada (nenhum item de §16 a registrar).

## C tests / Swift tests / warnings / analyzer / PE battery

- C tests: `3392 verificações, 0 falhas` ✓ · Swift tests: `Executed 63 tests, with 0 failures` ✓
- warnings: 0 ✓ · analyzer: 0 ✓
- PE battery: `15/15` (28/43/180/70/72/75/98/118/76/81/194/131/55/200/105) ✓
- hello_thread = 42/30 ✓ · hello_thread_shared = 42/33 ✓ · hello_cs = 42/44 ✓ · hello_virt = 42/64 · hello_virt_protect = 42/31 · hello_virt_query = 42/33 · hello_qpc = 42/98-99 · hello_heap = 42/31 · hello_mbwc = 42/29 · hello_file = 42/36 · hello_file_w = 42/33 · hello_file_seek = 42/33 · hello_stdio = 5/155 · hello_gl12 = 42/116 · hello_sse = 7/209 ✓

## hello_mutex

`exited=1 rc=42 log=39` ✓ (27/27 execuções).

## MD5 antes/depois

- Antes: `852afae4b659a87e9d7f572ebc02ad0d`
- Depois: `5b346959df1e035b51f672a3ab705df7` (alteração = alvo do grupo + despacho nas workers)

## limitações

- Subconjunto: `lpMutexAttributes` e `lpName` devem ser NULL (mutex nomeado = fora do escopo, §17 → `NULL` + `ERROR_INVALID_PARAMETER` honesto); até 8 mutexes por processo (`ERROR_NOT_ENOUGH_MEMORY` se esgotar).
- `WaitForSingleObject` em mutex: `INFINITE` = bloqueio real; `0` = `trylock` (`WAIT_TIMEOUT` se ocupado); **timeout finito não bloqueia** (devolve `WAIT_TIMEOUT` imediato) — limitação mantida do G42, não testada aqui (§17).
- Recursão de mutex implementada (semântica Windows: mesma thread re-adquire, `ReleaseMutex` por aquisição) — mas **não testada** (§17 "recursive mutex avançado" fora do escopo do teste).
- `CloseHandle` de mutex ainda **owned** = força liberação e destrói (evita UB do pthread); **abandono de mutex (`WAIT_ABANDONED`) fora do escopo** — desvio documentado.
- `ReleaseMutex` de não-proprietário = `FALSE` + `ERROR_NOT_OWNER` (288) (comportamento real; não exercitado pelo PE — §6/§7 não mandam testar).
- Ordem entre as workers é livre (qualquer ordem válida, §12); `shared_value` final não-determinístico; os checks do PE são independentes da ordem.
- Retornos `0x41`/`0x42`/`0x11`/`0x12` das workers não são conferidos (`GetExitCodeThread` proibido, §2).
- `inside`/`violation` são `volatile LONG` (não áticos) — a proteção é o próprio mutex; o check de `inside` é propositalmente fora da proteção de um segundo lock para detectar sobreposição real (é exatamente o "canário" pedido no §11).
- `Sleep`/`TlsGetValue` no import table (startup mingw posix) — desvio documentado no G42; não chamados.
- Não se afirma compatibilidade geral com aplicações Windows.

## critérios de aceitação (§18)

| # | Critério | Status |
|---|---|---|
| 1 | `CreateMutexA` funcionar | ✓ (handle criado; rc≠10) |
| 2 | duas threads guest reais criadas | ✓ (2× `CreateThread`, pthreads + CPUs guest por thread) |
| 3 | ambas usam o mesmo mutex | ✓ (HANDLE global `0xF0F0F200`) |
| 4 | ambas adquirem o mutex | ✓ (2× `Wait(mutex)` → `WAIT_OBJECT_0` com bloqueio real entre elas) |
| 5 | ambas executam a seção protegida | ✓ (entered flags + escritas) |
| 6 | `violation == 0` | ✓ (rc≠17) |
| 7 | `worker1_entered == 1` | ✓ (rc≠15) |
| 8 | `worker2_entered == 1` | ✓ (rc≠16) |
| 9 | `shared_value` válido | ✓ (rc≠17) |
| 10 | ambos os Waits (threads) = `WAIT_OBJECT_0` | ✓ (rc≠13/14) |
| 11 | ambos os `ReleaseMutex` funcionam | ✓ (liberação real provada pelo avanço da 2ª worker; 2× no log) |
| 12 | dois handles de thread fechados | ✓ (rc≠18/19) |
| 13 | handle do mutex fechado | ✓ (rc≠20) |
| 14 | `rc=42` | ✓ |
| 15 | regressão verde | ✓ (3392 · 63 · 0 · 0 · 15/15 · todos os hello_*) |

**15/15 critérios atendidos.**

## declaração explícita

**`CreateMutexA`, `WaitForSingleObject` sobre mutex, `ReleaseMutex` e `CloseHandle` foram validados por PE real, com exclusão mútua real entre duas threads guest concorrentes** (`hello_mutex.exe`, **rc=42**, contrato 10–20/42 completo, 27/27 execuções estáveis): duas threads guest reais em pthreads host (CPUs guest independentes, infraestrutura G43) adquiriram o **mesmo mutex Win32** com bloqueio real entre elas, executaram a seção protegida **sem sobreposição** (`violation == 0`, verificado por canário `inside`/`violation`), liberaram com `ReleaseMutex` a partir das **próprias threads guest** (despacho `INT 0x2E` na worker) e os três handles (2 threads + 1 mutex) foram fechados com liberação real de recursos. Não foi implementada nenhuma outra API de sincronização.

**Não se afirma compatibilidade geral com aplicações Windows.**
