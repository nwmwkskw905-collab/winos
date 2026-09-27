# Relatório do Grupo 43 — Comunicação entre duas threads guest (Validação Real por PE x64)

## STATUS

**CONCLUÍDO COM SUCESSO** — a infraestrutura de concorrência existente (G42) foi validada com DUAS threads guest reais em pthreads concorrentes, CPUs guest independentes e memória guest compartilhada. **Primeira execução = rc 42 (fluxo completo aprovado)**; **nenhum blocker**; **nenhuma correção aplicada**; **runtime intocado do início ao fim** (MD5 idêntico antes/depois). Nenhuma API nova foi implementada.

## BASELINE (estado final do G42 confirmado antes de tudo)

- C tests: `3392 verificações, 0 falhas` (3392/3392) ✓
- Swift tests: `Executed 63 tests, with 0 failures` (63/63) ✓
- warnings (`-Wall -Wextra`): 0 ✓ · analyzer: 0 ✓
- PE battery: `15/15` (real/user/app/gl2..gl10 = 42 · gdi = 42 · input = 42) ✓
- hello_thread = 42 ✓ · hello_cs = 42 ✓ · hello_virt = 42 ✓ · hello_virt_protect = 42 ✓ · hello_virt_query = 42 ✓ · hello_qpc = 42 ✓ · hello_heap = 42 ✓ · hello_mbwc = 42 ✓ · hello_file = 42 ✓ · hello_file_w = 42 ✓ · hello_file_seek = 42 ✓ · hello_stdio = 5 ✓ · hello_gl12 = 42 ✓ · hello_sse = 7 ✓
- **MD5 no início: `852afae4b659a87e9d7f572ebc02ad0d`** (= estado final do G42) ✓

## PE criado

- `realpe/hello_thread_shared.c` → `Tests/PorticoRuntimeTests/data/hello_thread_shared.exe`
- Compilado com `x86_64-w64-mingw32-gcc -O2 -s` (0 erros), **mesmo toolchain POSIX do G42** (GCC 14-posix).
- Fluxo exato do §4: workers `worker1` (`shared_value = 0x11111111; worker1_done = 1; return 0x41`) e `worker2` (`shared_value = 0x22222222; worker2_done = 1; return 0x42`); main cria `h1`/`h2` (`CreateThread(NULL, 0, workerN, NULL, 0, NULL)`), verifica `h1` (10), `h2` (11), `Wait(h1, INFINITE)` (12), `Wait(h2, INFINITE)` (13), `worker1_done == 1` (14), `worker2_done == 1` (15), `shared_value ∈ {0x11111111, 0x22222222}` (16), `CloseHandle(h1)` (17), `CloseHandle(h2)` (18), retorna 42. **Somente** os códigos 10–18 e 42.
- Variáveis compartilhadas: `volatile LONG worker1_done, worker2_done, shared_value` (globais na memória do processo).

## imports (objdump)

KERNEL32 = `CloseHandle, CreateThread, DeleteCriticalSection, EnterCriticalSection, GetLastError, InitializeCriticalSection, LeaveCriticalSection, SetUnhandledExceptionFilter, VirtualProtect, VirtualQuery, WaitForSingleObject` + `Sleep`, `TlsGetValue` (startup mingw posix/winpthreads — mesmo conjunto dos demais PEs do repo; não são chamados neste PE; desvio do §3 do G42 já documentado). msvcrt = CRT padrão. **Nenhuma** TLS/mutex/evento/semaforo/`GetExitCodeThread`/`TerminateThread`/`CreateRemoteThread` importada ✓ (§1/§10).

## primeira execução (runtime COMPLETAMENTE INTOCADO)

Série completa do log (33 eventos; probe descartável de `build/`):

```
[WIN32] API kernel32.dll!CreateThread
[WIN32] API kernel32.dll!CreateThread
[WIN32] API kernel32.dll!WaitForSingleObject
[WIN32] API kernel32.dll!WaitForSingleObject
[WIN32] API kernel32.dll!CloseHandle
[WIN32] API kernel32.dll!CloseHandle
[WIN32] API msvcrt.dll!exit
[WIN32] exit(42) — processo sinalizado como encerrado
[PROCESS] exit code 42 after 0.30 ms
```

- **rc: `42`** (`exited=1 rc=42 log=33`) — fluxo completo aprovado **na primeira execução**.
- Ponto exato da execução: `main` percorreu TODO o fluxo — 2× `CreateThread`, 2× `WaitForSingleObject`, checks 14–16, 2× `CloseHandle`, `exit(42)`.
- HANDLE de cada thread: `h1 = 0xF0F0F100`, `h2 = 0xF0F0F101` (derivado da construção determinística do slot scan em `f_CreateThread` — menor slot livre; rc≠10/11 prova ambos não-NULL; rc≠17/18 prova `CloseHandle` TRUE nos dois).
- Execução das duas workers: **provada** — `worker1_done = 1` e `worker2_done = 1` após os joins (rc≠14 e rc≠15); a ordem de programa escreve `shared_value` ANTES do flag de done, logo ambas escreveram na memória compartilhada.
- `worker1_done` = **1** (provado por rc≠14).
- `worker2_done` = **1** (provado por rc≠15).
- `shared_value` = **0x11111111 OU 0x22222222** (provado puro por rc≠16 — um mix rasgado como `0x11112222` retornaria 16; o vencedor é **não-determinístico por projeto** e ambos os valores são aceitos pelo contrato).
- Retorno dos dois `WaitForSingleObject`: **`WAIT_OBJECT_0`, `WAIT_OBJECT_0`** (provado por rc≠12 e rc≠13; joins reais).
- `CloseHandle`: **TRUE, TRUE** (provado por rc≠17 e rc≠18; série do log com 2× `CloseHandle`).
- Número de instruções: `exec = 379` (thread principal; contagens das workers são internas ao trampolim e não expostas — limitação do probe, não afeta o contrato).
- Log: 33 eventos (completo acima; `log=33`).
- **MD5: `852afae4b659a87e9d7f572ebc02ad0d` antes == depois** ✓ (nada foi alterado).

## rc

**42** (contrato 10/11/12/13/14/15/16/17/18/42 — nenhum outro código emitido).

## primeiro blocker

**Nenhum.** A infraestrutura de threads do G42 suportou o fluxo de duas threads na primeira execução.

## correção aplicada

**Nenhuma** (§1: não alterar o runtime antes da primeira execução; §8: só corrigir o primeiro blocker REAL se revelado — nada foi revelado).

## alterações no runtime

**Nenhuma.** MD5 `852afae4b659a87e9d7f572ebc02ad0d` idêntico antes/depois. Nenhuma API nova (§10 cumprido): sem TLS, mutex, evento, semáforo, `GetExitCodeThread`, `TerminateThread`, `CreateRemoteThread`, sincronização genérica ou scheduler avançado. Sem locks internos adicionados ao interpretador (§8).

## alterações no harness

**Nenhuma.** `tools/` intocado; apenas o probe descartável `build/virt_probe.c` (cópia de `tools/dbg_log.c` com dump de log/`exec`) para o registro da primeira execução.

## resultado das duas threads

- **Thread 1** (`worker1`): pthread host própria → CPU guest própria (`pr_cpu64` sobre o backing do processo) → escreveu `shared_value = 0x11111111`, marcou `worker1_done = 1`, retornou `0x41` (retorno não conferido — `GetExitCodeThread` fora do escopo, conforme §10).
- **Thread 2** (`worker2`): pthread host própria → CPU guest própria → escreveu `shared_value = 0x22222222`, marcou `worker2_done = 1`, retornou `0x42` (idem).
- Comunicação: **memória guest compartilhada** (globais `volatile LONG` no backing único do processo) — observada corretamente pela thread principal após os dois joins.

**Critério fundamental (§7) — paralelismo real, não serial na thread host:** evidência estrutural do runtime (não apenas do rc): `f_CreateThread` chama `pthread_create` **por chamada e retorna sem bloquear** (`pr_win32.c` L2021), iniciando `w32_thread_main` → `pr_cpu64_run(t->cpu, …)` com `t->cpu = pr_cpu64_create_on(mem, space)` **próprio** por thread; o log mostra as **duas `CreateThread` antes de qualquer `Wait`** (as duas pthreads existem concorrentemente com a main); os joins só ocorrem nos `WaitForSingleObject`. O esquema é exatamente `Thread host A → CPU guest A → worker1` **ao mesmo tempo que** `Thread host B → CPU guest B → worker2`, sobre a mesma memória guest. (Honestidade: o rc sozinho não distingue paralelismo de intercalação — a garantia vem da construção com `pthread_create` imediato por chamada e CPU guest por thread.)

## worker1_done / worker2_done / shared_value / Waits / CloseHandles

| Item | Valor registrado | Instrumento |
|---|---|---|
| `worker1_done` | 1 | rc ≠ 14 |
| `worker2_done` | 1 | rc ≠ 15 |
| `shared_value` | 0x11111111 **ou** 0x22222222 (puro; não-determinístico) | rc ≠ 16 (detectaria mix) |
| Wait 1 | `WAIT_OBJECT_0` | rc ≠ 12 (join real) |
| Wait 2 | `WAIT_OBJECT_0` | rc ≠ 13 (join real) |
| CloseHandle 1 | TRUE | rc ≠ 17 |
| CloseHandle 2 | TRUE | rc ≠ 18 |

## Observação de corrida (§8) — arquitetura atual, sem locks

Foram executadas **mais de 120 execuções** (incluindo **100 consecutivas** dedicadas): **todas `rc=42 log=33`**, sem crash, fault, corrupção ou mix de `shared_value`. Janelas concorrentes examinadas e registradas (nada se manifestou → nada foi corrigido):

1. **Bookkeeping de threads (`ctx->threads[]`)**: slots disjuntos; cada worker escreve apenas `done`/`exit_code` do próprio slot; sincronização com a main é o `pthread_join`. Estrutura: sem corrupção observada.
2. **Contexto Win32 (`pr_win32_ctx`)**: as workers **não emitem chamadas Win32** (um `INT` dentro da worker terminaria em parada honesta — não ocorreu); não há despacho concorrente sobre o `ctx`. Sem corrupção.
3. **VM (`pr_vm`)**: janela real leitor/escritor — a **2ª `CreateThread` aloca a stack do worker2 (`pr_vm_alloc`/`pr_vm_write`) enquanto a worker1 já roda** e valida cada acesso via `pr_vm_check` (guarda). Não manifestada: as mutações atingem páginas recém-alocadas que a worker1 não acessa e não há resize estrutural no caminho. **Janela arquitetural conhecida, registrada** (limitação) — se um PE futuro agravar (ex.: `VirtualFree` de páginas em uso concorrente), tratar como blocker real na ocasião.
4. **`pr_cpu64`**: uma CPU por thread (estados disjuntos); memória do processo compartilhada **sem locks** (modelo de concorrência real escolhido no G42). Escritas concorrentes em `shared_value` (32-bit) sem valor rasgado observado (0× rc=16 em 120+ execuções; o check 16 é o detector de mix por projeto).
5. **Crash/fault/corrupção: nenhum.**

## C tests / Swift tests / warnings / analyzer / PE battery

- C tests: `3392 verificações, 0 falhas` ✓
- Swift tests: `Executed 63 tests, with 0 failures` ✓
- warnings: 0 ✓ · analyzer: 0 ✓
- PE battery: `15/15` (28/43/180/70/72/75/98/118/76/81/194/131/55/200/105 — todos canônicos) ✓
- hello_thread = 42/30 ✓ · hello_cs = 42/44 ✓ · hello_virt = 42/64 · hello_virt_protect = 42/31 · hello_virt_query = 42/33 · hello_qpc = 42/98-99 · hello_heap = 42/31 · hello_mbwc = 42/29 · hello_file = 42/36 · hello_file_w = 42/33 · hello_file_seek = 42/33 · hello_stdio = 5/155 · hello_gl12 = 42/116 · hello_sse = 7/209 ✓

## hello_thread_shared

`exited=1 rc=42 log=33` ✓ (1ª execução e todas as seguintes).

## MD5 antes/depois

- Antes: `852afae4b659a87e9d7f572ebc02ad0d`
- Depois: `852afae4b659a87e9d7f572ebc02ad0d` — **idêntico** (runtime intocado).

## limitações

- `shared_value` final não é determinístico (por projeto); o contrato aceita exatamente um dos dois valores puros e o check 16 detecta escrita rasgada.
- Contagens de instrução das workers não expostas (internas ao trampolim); `exec=379` refere-se à thread principal.
- Sem locks internos no interpretador/VM (modelo escolhido): padrões de corrida do convidado são responsabilidade do convidado; janela VM leitor/escritor (item 3 acima) é arquitetural e está registrada.
- `Sleep`/`TlsGetValue` no import table (startup mingw posix) — desvio documentado no G42; não chamados neste PE.
- Nenhuma API nova implementada (§10). `GetExitCodeThread` inexistente — retornos `0x41`/`0x42` das workers **não são conferidos** (conforme §1).
- Não se afirma compatibilidade geral com aplicações Windows.

## critérios de aceitação (§11)

| # | Critério | Status |
|---|---|---|
| 1 | duas chamadas `CreateThread` executadas | ✓ (log: 2× `CreateThread`) |
| 2 | duas pthreads host reais criadas | ✓ (`pthread_create` por chamada, `f_CreateThread` L2021) |
| 3 | duas CPUs guest independentes executando | ✓ (`pr_cpu64_create_on` por thread, `w32_thread_main`) |
| 4 | `worker1` alterou `worker1_done` | ✓ (rc ≠ 14) |
| 5 | `worker2` alterou `worker2_done` | ✓ (rc ≠ 15) |
| 6 | ambas compartilham memória guest corretamente | ✓ (globais no backing único; checks 14–16) |
| 7 | ambos os `WaitForSingleObject` retornaram `WAIT_OBJECT_0` | ✓ (rc ≠ 12/13; joins reais) |
| 8 | ambos os handles fechados | ✓ (rc ≠ 17/18; log: 2× `CloseHandle`) |
| 9 | `shared_value` com um dos dois valores válidos | ✓ (rc ≠ 16; 0 mixes em 120+ execuções) |
| 10 | PE termina com `rc=42` | ✓ |
| 11 | regressão permanece verde | ✓ (3392 · 63 · 0 · 0 · 15/15 · todos os hello_*) |

**11/11 critérios atendidos.**

## declaração explícita

**Duas threads guest reais, em duas pthreads host concorrentes com CPUs guest independentes sobre a mesma memória do processo, comunicação por memória compartilhada, dois `WaitForSingleObject` (`WAIT_OBJECT_0` por join real) e dois `CloseHandle` foram validados por PE real** (`hello_thread_shared.exe`, **rc=42** na primeira execução com runtime intocado, contrato 10–18/42 completo, 120+ execuções estáveis): `worker1` e `worker2` executaram de fato, escreveram `worker1_done = 1` e `worker2_done = 1` na memória guest compartilhada, `shared_value` terminou com exatamente um dos dois valores válidos (0× mix em 120+ execuções), ambos os Waits devolveram `WAIT_OBJECT_0` após conclusão real e ambos os handles foram fechados com liberação real de recursos.

**Não se afirma compatibilidade geral com aplicações Windows.**
