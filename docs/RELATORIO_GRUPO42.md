# Relatório do Grupo 42 — CreateThread + WaitForSingleObject + CloseHandle (Validação Real por PE x64)

## 1. Base e objeto

- Base: G41.
- **Alvo SÚMULA**: `CreateThread`, execução de **thread guest real**, `WaitForSingleObject`, `CloseHandle`.
- Modelo: **concorrência real via pthread** (worker em paralelo com a thread principal; `WaitForSingleObject(INFINITE)` = join real).
- Fora de escopo (NÃO implementados): TLS, mutexes, eventos, semáforos, APCs, outras APIs de sincronização, `GetExitCodeThread`, `CreateRemoteThread`, `_beginthread`.
- Declaração emitida ao final.

## 2. Preparação (baseline, toolchain, harness, imports)

- Baseline prévia (runtime intocado): c-test `3392 verificações, 0 falhas`; swift `Executed 63 tests, with 0 failures`; warnings 0; analyzer 0; battery `15/15` (real/user/app/gl2..gl10 = 42 · gdi = 42 · input = 42); hello_virt/heap/mbwc/file/file_w/file_seek/gl12 = 42, hello_file_seek (log) = 33, hello_stdio = 5 (comportamento correto), hello_sse = 7 (comportamento correto), hello_qpc = 42. MD5 = `957920b285c21590dbc10c3c06283242` ✓.
- Toolchain: mingw posix já em uso pelo repositório (`~/.cache/mingwtc`, GCC 14 posix). Sem reclamação `-Wall`/analyzer.
- Harness: **sem alterações** (probes são artefatos descartáveis de `build/`; `tools/dbg_log.c` intocado).
- Auditoria: `CloseHandle` já tinha implementação parcial real (files/std, `f_CloseHandle`) — **preservada e estendida**, nunca substituída. `CreateThread`/`WaitForSingleObject` eram TODO puro.
- Import table do `hello_thread.exe` (objdump): KERNEL32 = `CloseHandle, CreateThread, DeleteCriticalSection, EnterCriticalSection, GetLastError, InitializeCriticalSection, LeaveCriticalSection, SetUnhandledExceptionFilter, VirtualProtect, VirtualQuery, WaitForSingleObject` + **`Sleep`, `TlsGetValue`**; msvcrt = CRT padrão (24 itens: `___lc_codepage_func`, `___mb_cur_max_func`, `__getmainargs`, `__initenv`, `__lconv_init`, `__set_app_type`, `__setusermatherr`, `_cexit`, `_commode`, `_configthreadlocale`, `_dowildcard`, `_fmode`, `_initterm`, `_lock`, `_onexit`, `_set_invalid_parameter_handler`, `_setmode`, `__dllonexit`, `_initterm_e`, `_c_exit`, `_exit`, `_XcptFilter`, `exit`, `signal`).
  - `CreateRemoteThread`, `_beginthread`, `TlsAlloc`, `TlsSetValue`, `GetExitCodeThread` **não importados** ✓.
  - `Sleep`/`TlsGetValue` são **do startup mingw posix (winpthreads)** — desvio do §3 ("mesmo no import table do startup"), documentado: o runtime só os despacharia em caminhos TLS/FS que este PE não exercita (todos os `hello_*.exe` do repo já usam este mesmo runtime startup e não os chamam; apenas `hello_tls` quebra em `TlsGetValue`). Adotar toolchain win32-threads mudaria o runtime CRT dos PEs já validados.

## 3. Alterações (arquivo, linha e causa)

- `Sources/PorticoRuntime/src/pr_win32.c`:
  - L37: handle `0xF0F0F100+i` p/ threads (sem colisão com files/std/heap).
  - L155–165: `w32_thread` (used/done/failed/joined, tid, exit_code, stack, `pr_cpu64*`, ctx).
  - L211: campo `threads[8]` + `thread_sentinel` no `pr_win32_ctx`.
  - L1890–2068: guarda de VM da thread (`w32_thr_guard` = `pr_vm_check`), slot/limpeza/shutdown, trampolim `w32_thread_main` (roda o `pr_cpu64` da worker; retorno natural = HLT ⇒ exit_code = RAX; fault ⇒ log honesto `thread guest parada: …`), `f_CreateThread` (L1946) e `f_WaitForSingleObject` (L2034).
  - L2070–2077 (`f_CloseHandle`): **extensão** com ramo de handle de thread (join + destroy do CPU + unmap da stack + slot liberado → duplo close = `ERROR_INVALID_HANDLE`, igual à semântica de arquivo). Files/std **preservados** (`test_vfs` verde).
  - L4549–4550: IMPL de `CreateThread` (args=24) e `WaitForSingleObject` (args=8). TODOs das duas APIs removidos (eram falsos após a implementação); TODO de `CloseHandle` mantido ("handles genéricos" como processo/GDI continuam não fecháveis via CloseHandle).
  - L4856: `pr_win32_destroy` conclui threads pendentes (join honesto) antes de liberar o ctx — evita teardown com worker ativo.
  - Causa: só as APIs do alvo, com comportamento real (sem stub de sucesso).
- `Sources/PorticoRuntime/src/pr_vm.c` L~120 + `include/portico/pr_vm.h`: **somente leitura** `pr_vm_backing(vm, &space)` (4 linhas) — permite criar o 2º contexto `pr_cpu64` sobre a mesma memória física emprestada (sem getter existente; sem mudança de comportamento).

Modelo de thread (concorrência real): `pthread_create` roda `w32_thread_main`; este executa um **segundo `pr_cpu64`** sobre a MESMA memória do processo (`pr_cpu64_create_on(mem, space)`), com pilha própria `VirtualAlloc` 64 KiB (`w32thrd`), **página sentinela própria** com byte `HLT` (`w32thsnt`, R|X) como retorno natural da função, frame MS x64 idêntico ao `pr_peproc_call` (`[rsp]=sentinela`, `rsp%16==8`, `RCX=lpParameter`, `RIP=lpStartAddress`, RFLAGS=0x202). INT dentro da worker = fault honesto `int-without-trap` (a worker não chama Win32). Sem locks no interpretador: a memória do processo é compartilhada sem sincronização interna (benigno neste PE: escrita única de `volatile LONG`).

## 4. Fluxo verificado (exatamente o PE)

`hello_thread.exe`: `volatile LONG result = 0` → `h = CreateThread(NULL, 0, worker, &result, 0, NULL)` (NULL ⇒ 10) → `WaitForSingleObject(h, INFINITE)` (≠ WAIT_OBJECT_0 ⇒ 11) → `result == 0x12345678` (≠ ⇒ 12) → `CloseHandle(h)` (FALSE ⇒ 13) → `exit(42)`. `worker(LPVOID param)`: `*(volatile LONG*)param = 0x12345678; return 0x42;` (retorno NÃO conferido — sem `GetExitCodeThread`).

## 5. Primeira execução — runtime COMPLETAMENTE INTOCADO

| Item | Registro |
|---|---|
| MD5 runtime (antes == depois) | `957920b285c21590dbc10c3c06283242` ✓ idêntico |
| rc oficial | **nenhum** — `EXECUTION STOPPED` |
| Ponto exato da falha | `Unsupported Win32 API — kernel32.dll!CreateThread @ 0x00FF76DD` (dispatch da 1ª chamada de `main`) |
| CreateThread | **não retornou** — parada no stub `INT 0x2E` |
| HANDLE | n/d (não criado) |
| WaitForSingleObject | **não chamado** |
| `result` | n/d (worker nunca iniciou) |
| CloseHandle | **não chamado** |
| nº de instruções | contador do probe = 0 na parada (primeiro dispatch); pós-implementação = 332 na thread principal |
| nº de eventos/log | log ≥ 13 eventos até a parada (tail registrado; `_onexit` duplo do CRT antes de `CreateThread`) |
| LastError aplicável | n/a (parada antes de qualquer retorno de API) |
| Memória compartilhada usada | 0 KiB nova (nenhuma) |

Blocker corrigido: **somente o alvo ausente** (as 3 APIs do grupo, única correção possível; nada além foi implementado).

## 6. Validação real (runtime implementado)

- `./build/dbg_input data/hello_thread.exe noinput` → `exited=1 rc=42 log=30` ✓.
- Série observada no log: `CreateThread → WaitForSingleObject → CloseHandle → exit(42) — processo sinalizado como encerrado` / `[PROCESS] exit code 42` ✓.
- PROVA da thread guest real: o check 12 (`result == 0x12345678` após o Wait) e o rc 42 implicam que a **worker executou de fato** numa pthread própria (concorrência real: `exec=332` só na thread principal), escreveu na memória compartilhada do processo e retornou (`HLT` de retorno natural), e o `Wait` só devolveu `WAIT_OBJECT_0` após a conclusão real (join).
- Tabela do contrato (§6):

| Código | Significado | Observado |
|---|---|---|
| 10 | `CreateThread` retornou NULL | não — HANDLE `0xF0F0F100` ok |
| 11 | `WaitForSingleObject` ≠ `WAIT_OBJECT_0` | não — devolveu 0 após join real |
| 12 | `result` ≠ `0x12345678` após o Wait | não — memória compartilhada escrita pela worker |
| 13 | `CloseHandle` retornou FALSE | não — TRUE, recursos liberados |
| **42** | fluxo completo | **✓ observado** |

## 7. Contrato PE (esperado × observado)

- Contrato: `10/11/12/13/42`; sem outros códigos. Observado: **42** ✓.

## 8. Regressão completa (pós-implementação)

- c-test: `3392 verificações, 0 falhas` ✓.
- swift: `Executed 63 tests, with 0 failures` ✓.
- Warnings `-Wall -Wextra`: 0 ✓. Analyzer: 0 ✓.
- Battery `15/15`: real/user/app/gl2..gl10 = 42 (logs 28/43/180/75/98/118/76/81/194/131/55/200), gdi = 42 (70), input = 42 (105) ✓.
- hello_virt = 42/64 · hello_virt_protect = 42/31 · hello_virt_query = 42/33 · hello_qpc = 42/98 · hello_heap = 42/31 · hello_mbwc = 42/29 · hello_file = 42/36 · hello_file_w = 42/33 · hello_file_seek = 42/33 · hello_stdio = 5/155 (correto) · hello_gl12 = 42/116 · hello_sse = 7/209 (correto) · hello_cs = 42/44 · **hello_thread = 42/30** ✓.

## 9. Limitações e honestidade

- Subconjunto implementado: `lpSecurityAttributes` deve ser NULL; `dwCreationFlags` deve ser 0 (sem `CREATE_SUSPENDED`/`STACK_SIZE_PARAM_IS_A_RESERVATION`); `lpThreadId` é `LPDWORD` (4 bytes) — demais parâmetros com valores fora do subconjunto ⇒ `NULL` + `ERROR_INVALID_PARAMETER` (real).
- `WaitForSingleObject`: só handles de **thread**; `INFINITE` = join real; `0` = poll imediato (`WAIT_TIMEOUT` se não concluiu); **timeout finito NÃO bloqueia** (`WAIT_TIMEOUT` se não concluiu) — limitação documentada. Outros tipos de handle ⇒ `WAIT_FAILED` + `ERROR_INVALID_HANDLE` (não fingimos sincronização genérica).
- `CloseHandle` de handle de thread: conclui a thread (join) e libera CPU/pilha/slot — **não é o detach assíncrono do Windows** (desvio documentado; no fluxo validado o Wait já concluiu a thread, então o fechamento não bloqueia). Duplo fechamento = `FALSE` + `ERROR_INVALID_HANDLE` (consistente com files; `test_vfs` verde).
- Até 8 threads por processo (slots); orçamento de execução por thread (200 M instruções) → fault honesto `budget esgotado` se estourar.
- `GetExitCodeThread` não existe: o exit code (RAX) é registrado internamente mas não exposto — como pedido, o retorno `0x42` da worker **não é conferido** pelo PE.
- `Sleep`/`TlsGetValue` no import table (startup mingw posix) = desvio do §3 documentado (seção 2); não são chamados neste PE.
- Concorrência real sem locks no interpretador: o PE validado usa escrita única `volatile LONG` (benigno); padrões de corrida do convidado são responsabilidade do convidado (como no Windows).
- Não se afirma compatibilidade geral com aplicações Windows.

## 10. Declaração

**CreateThread, thread guest real, WaitForSingleObject e CloseHandle foram validados por PE real** (`hello_thread.exe`, rc=42, contrato 10/11/12/13/42): `CreateThread` criou uma thread guest real em pthread paralela (CPU64 própria sobre a memória do processo, stack/sentinela próprias, frame MS x64); a worker escreveu `result = 0x12345678` e retornou; `WaitForSingleObject(h, INFINITE)` só devolveu `WAIT_OBJECT_0` após a conclusão real (join); `CloseHandle(h)` fechou o handle de thread com liberação real de recursos (preservando files/std do `test_vfs`). Não se afirma compatibilidade geral com aplicações Windows.

**Não se afirma compatibilidade geral com aplicações Windows.**
