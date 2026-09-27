# RELATÓRIO GRUPO 81 — kernel32!CreateEventA/W + SetEvent/ResetEvent + WaitForSingleObject (eventos)

## 1. Status
**G81 CONCLUÍDO** — Implementação real de eventos Win32 (manual-reset e auto-reset) com integração completa em `WaitForSingleObject`. PE `hello_event.exe` rc=81 log=89, 20/20. Sem regressão (C 3399/0, warnings 0, analyzer 7/0 novos).

## 2. Inventário (read-only antes de alterar)
- `pr_win32.c` L2351 `f_WaitForSingleObject`: só mutex (8 slots, BASE 0xF0F0F200) + thread (BASE 0xF0F0F100). Nenhum `w32_event`.
- `w32_mutex` struct L166-173 existe, `w32_thread` existe, `W32_MAX_MUTEXES 8`, `W32_MAX_THREADS 8`. Handle BASEs: THREAD 0xF0F0F100, MUTEX 0xF0F0F200. EVENT inexistente confirmado.
- `CloseHandle` L2615: só file + mutex + thread + std handles. Event não tratado.
- `pr_win32_destroy`: `w32_mutexes_shutdown` + `w32_threads_shutdown`, sem eventos.
- Catálogo L5318+: `CreateEventA/W`, `SetEvent`, `ResetEvent` ausentes (grep retorna 0).
- `LastError` infra existe (ERROR_INVALID_HANDLE 6, INVALID_PARAMETER 87, NOT_ENOUGH_MEMORY 8).
- `wstr_to_ascii` existe L2983 para W→A conversão de nome (reutilizado).
- Conclusão: lacuna real observada, sem TODO.

## 3. Implementação mínima (sem segunda infra)
`Sources/PorticoRuntime/src/pr_win32.c`:
- Novo BASE: `PR_WIN32_H_EVENT_BASE 0xF0F0F300` (após mutex, antes de colisões).
- `W32_MAX_EVENTS 8` + `struct w32_event {used, manual, signaled, pthread_mutex_t m, pthread_cond_t c}`.
- `ctx->events[8]` em `pr_win32_ctx`.
- Helpers:
  - `w32_event_slot(ctx,h)` — valida range BASE+MAX, retorna índice se used.
  - `w32_events_shutdown(ctx)` — destrói cond+mutex de cada slot usado.
- APIs:
  - `f_CreateEventA`: contrato `HANDLE CreateEventA(SECURITY_ATTRIBUTES*, BOOL manual, BOOL initial, LPCSTR name)` — RCX/RDX/R8/R9. Subconjunto: `lpSecurityAttributes==NULL` e `lpName==NULL` apenas; named → `NULL + ERROR_INVALID_PARAMETER 87` (limitação documentada, não finge namespace global). Aloca slot livre, `pthread_mutex_init` + `pthread_cond_init`, `manual = a[1]?1:0`, `signaled = a[2]?1:0`. Retorna handle ou NULL + `NOT_ENOUGH_MEMORY`.
  - `f_CreateEventW`: idem, reutiliza `wstr_to_ascii` para detectar nome não vazio → falha 87. `LPCWSTR` em RCX=attr, RDX=manual, R8=initial, R9=name (W).
  - `f_SetEvent`: `BOOL SetEvent(HANDLE)` — valida slot, `lock`, `signaled=1`, `manual? broadcast : signal`, `unlock`, retorna 1. Inválido → 0 + 6.
  - `f_ResetEvent`: `BOOL ResetEvent(HANDLE)` — `signaled=0`.
- `f_WaitForSingleObject` estendido (primeiro checa evento, antes de mutex/thread para preservar compatibilidade):
  - Se `signaled` → se auto-reset consome (`signaled=0`), retorna `WAIT_OBJECT_0 0`.
  - `ms==0` → poll imediato: `WAIT_TIMEOUT 0x102` se não sinalizado.
  - `ms==INFINITE 0xFFFFFFFF` → loop `pthread_cond_wait` até `signaled`, consome se auto.
  - Finito → `clock_gettime(CLOCK_REALTIME)` + `pthread_cond_timedwait`, consome se sinalizado, senão `WAIT_TIMEOUT`.
- `f_CloseHandle`: novo bloco para evento — `cond_destroy + mutex_destroy + memset`.
- `pr_win32_destroy`: adicionado `w32_events_shutdown`.
- Catálogo: `IMPL CreateEventA 16, CreateEventW 16, SetEvent 4, ResetEvent 4` — total runtime agora 57 APIs (53+4).
- Largura 64-bit preservada: handles são 64-bit, timeout DWORD 32-bit via cast, sem truncamento de ponteiro.
- Reutiliza infra existente: `pr_win32_ptr`, `wstr_to_ascii`, `LastError`, pthread.

## 4. Arquivos alterados
- `pr_win32.c`: +~130 linhas (defines, struct, 2 helpers, 4 APIs, Wait extensão, CloseHandle extensão, destroy, 4 IMPLs). MD5: `fe7acd0e...` → `8c0e1faef9f519f8d39921f55d2de60b`.
- `realpe/hello_event.c` novo.
- `Tests/.../hello_event.exe` 16 KiB.

## 5. PE real x64 MinGW
`realpe/hello_event.c` (195 linhas):
- Contratos exit codes 10-31 falha, 81 OK.
- Cenários:
  A. CreateEventA manual-reset FALSE
  B. CreateEventA auto-reset FALSE
  D/E. initial TRUE → Wait 0 = WAIT_OBJECT_0; FALSE → WAIT_TIMEOUT
  C. CreateEventW NULL name
  Named: CreateEventA("MyEvent") → NULL + 87; CreateEventW(L"MyEventW") → NULL + 87
  M. SetEvent(0x1234) → FALSE + 6; ResetEvent(0x1234) → FALSE + 6
  F/G/H. SetEvent → Wait success
  I. Wait timeout 0
  J. manual-reset permanece sinalizado após Wait (2 Waits = OBJECT_0)
  K. auto-reset consome (2º Wait = TIMEOUT)
  Reset blocks wait
  L. CloseHandle todos, double close → FALSE +6, use-after-close Wait → WAIT_FAILED +6
  Concurrency: CreateThread waiter_thread (Wait INFINITE) + Sleep 50 + SetEvent + WaitForSingleObject thread 2000 → done
  auto-reset single waiter release
  LastError preservação (não enforçado estrito, documentado)
- Imports: `CreateEventA/W`, `SetEvent`, `ResetEvent`, `WaitForSingleObject`, `CloseHandle`, `CreateThread`, `Sleep`, `SetLastError`, `GetLastError`.

## 6. Imports / DLLs
`KERNEL32.dll`: `CloseHandle`, `CreateEventA`, `CreateEventW`, `GetLastError`, `ResetEvent`, `SetEvent`, `WaitForSingleObject`, `CreateThread`, `Sleep`, `SetLastError`.

## 7. ABI Win64 provada (disassembly)
Debug build `hello_event_debug.exe`:
```
mov r9d,0x0          ; lpName = NULL
mov r8d,0x0          ; bInitialState = FALSE
mov edx,0x1          ; bManualReset = TRUE
mov ecx,0x0          ; lpSecurityAttributes = NULL
call [__imp_CreateEventA]
...
mov edx,0x0          ; dwMilliseconds = 0
mov rcx,rax          ; hHandle = ev
call [__imp_WaitForSingleObject]
...
mov ecx,0x1234
call [__imp_SetEvent] ; invalid handle test
...
mov rcx,rax
call [__imp_SetEvent] ; valid
```
- RCX/RDX/R8/R9 usados conforme contrato Win64, retorno em EAX/RAX (HANDLE/BOOL/DWORD).
- Largura: HANDLE 64-bit, BOOL 32-bit, timeout DWORD.

## 8. Testes funcionais do PE
- Manual-reset permanece sinalizado: PASS
- Auto-reset consome: PASS
- Initial TRUE/FALSE: PASS
- CreateEventW NULL: PASS
- Named falha 87: PASS (limitação explícita)
- Invalid handle 6: PASS
- Set→Wait success, timeout, Reset blocks: PASS
- CloseHandle, double close, use-after-close: PASS
- Concurrency Wait+SetEvent cross-thread: PASS (pthread condvar real)
- Auto-reset single waiter: PASS

## 9. 20 execuções
```
exited=1 rc=81 log=89 (x20)
```
Determinístico, sem flakiness.

## 10. C tests
`make c-test`: **3399 verificações, 0 falhas** — baseline G80 preservado (3399/0). Nenhum teste removido.

## 11. Swift tests
63/63 esperado (ambiente Linux sem Swift toolchain nesta execução; baseline G80 63/63). CompatLayerTests não alterado.

## 12. warnings
`gcc -Wall -Wextra`: **0 warnings** em `pr_win32.c` + testes + `dbg_input`.

## 13. analyzer
`pr_win32.plist` 2 diags, total 7 diags em 20 plists (1 cpu,1 cpu64,1 gfx,1 peproc,2 win32,1 winhello) — **7/0 novos**, idêntico G80.

## 14. PE battery
66 PEs em `Tests/PorticoRuntimeTests/data/`:
```
hello_app 42/180, hello_cs 42/44, hello_deletefile 68/55, hello_deletefilew 80/39,
hello_event 81/89, hello_file 42/36, hello_file_seek 42/33, hello_file_w 42/33,
hello_gdi 42/70, hello_getcommandline 56/32, hello_getcommandlinew 57/33,
hello_getcurrentdirw 77/33, hello_getcurrentprocessid 61/33, hello_getcurrentthreadid 62/33,
hello_getenv 53/38, hello_getfileattributes 67/48, hello_getfileattrw 79/38,
hello_getmodfilenamew 78/37, hello_getmodulefilename 65/35, hello_getstartupinfo 66/37,
hello_gl 42/72 ... hello_gl12 42/116, hello_heap 42/31, hello_input 42/105 (com input),
hello_isdebuggerpresent 63/33, hello_lstrcmp 60/45, hello_lstrcpy 59/40,
hello_lstrcpyn 52/31, hello_lstrlen 58/39, hello_mbwc 42/29, hello_mutex 42/39,
hello_mutex_timeout 42/40, hello_puts 73/334, hello_qpc 42/96, hello_readfileex 75/45,
hello_real 42/28, hello_setfilepointer 69/55, hello_sse 7/209, hello_stdhandle 42/33,
hello_stdio 5/155, hello_sysmetrics 76/48, hello_terminateprocess 64/29,
hello_thread 42/30, hello_thread_shared 42/33, hello_thread_timeout 42/44,
hello_tickcount64 54/30, hello_tlsalloc 70/94, hello_tlsfree 72/274,
hello_tlsgetvalue 55/36, hello_tlssetvalue 71/59, hello_unicode_probe 74/742,
hello_user 42/43, hello_version_systeminfo 42/30, hello_virt 42/64,
hello_virt_protect 42/31, hello_virt_query 42/33, hello_wait_multiple 42/32,
hello_winsock_ord 42/33
```
Todos rc esperados, nenhum HANG. `build/win_fs` = só `fixture.txt` (5 B).

## 15. checkpoints G52–G81
**30/30** verdes: G52 42/28, G53 53/38, G54 54/30, G55 55/36, G56 56/32, G57 57/33, G58 58/39, G59 59/40, G60 60/45, G61 61/33, G62 62/33, G63 63/33, G64 64/29, G65 65/35, G66 66/37, G67 67/48, G68 68/55, G69 69/55, G70 70/94, G71 71/59, G72 72/274, G73 73/334, G74 74/742, G75 75/45, G76 76/48, G77 77/33, G78 78/37, G79 79/38, G80 80/39, **G81 81/89**.

## 16. checklist
**52/52** — novo item:
- 52. CreateEventA/W + SetEvent/ResetEvent + WaitForSingleObject eventos (G81)

## 17. MD5 / escopo
| Arquivo | G80 após | G81 após (final) |
|---|---|---|
| `pr_win32.c` | `fe7acd0ed9ff5c07990abd4b39aaea3f` | `8c0e1faef9f519f8d39921f55d2de60b` |
| Δ | — | +~130 linhas, 4 IMPLs, sem segunda infra |

## 18. filesystem
`build/win_fs` limpo após bateria, só `fixture.txt`.

## 19. Limitações explícitas (G81)
- Named events (`lpName != NULL`) fora do escopo → falha honesta `NULL + 87 ERROR_INVALID_PARAMETER`, documentado. Não finge namespace global entre processos.
- `lpSecurityAttributes` deve ser NULL (mesma limitação de mutex).
- Máximo 8 eventos simultâneos (`W32_MAX_EVENTS 8`) — `NOT_ENOUGH_MEMORY 8` se exceder.
- `WaitForSingleObject` para eventos usa `CLOCK_REALTIME` + `pthread_cond_timedwait`; não suporta `WAIT_ABANDONED` (só mutex/thread).
- Sem `PulseEvent`, `CreateEventEx`, `OpenEvent`, `WaitForMultipleObjects` com eventos (só threads, conforme G47).
- Concorrência real via pthread condvar, sem starvation handling especial (suficiente para PEs de teste).

## 20. Compatibilidade efetivamente demonstrada
- Eventos manual-reset permanecem sinalizados após Wait, auto-reset consomem.
- Initial state TRUE/FALSE respeitado.
- SetEvent acorda waiter bloqueado em outra thread (CreateThread + Wait INFINITE + SetEvent).
- Timeout 0 retorna WAIT_TIMEOUT, INFINITE bloqueia real até SetEvent, finito usa timedwait.
- Invalid handle, double close, use-after-close retornam FALSE/WAIT_FAILED + LastError 6.
- CreateEventW com nome vazio funciona, com nome falha 87.
- Todos com ABI Win64 provada, 20/20.

## 21. Próxima lacuna (indicação — NÃO executar ainda)
Do inventário atualizado:
- `kernel32!FindFirstFileA/W`, `FindNextFileA/W`, `FindClose` (file enum, P2, muitos PEs usam).
- `kernel32!GetFileAttributesExA/W`, `SetFilePointerEx`, `FlushFileBuffers`.
- `kernel32!CreateFileMappingA/W`, `MapViewOfFile`.
- `advapi32!RegOpenKeyExA/W` família (registro).
- `gdi32!CreateFontA/W`, `TextOutA/W` (texto GDI).
- `ws2_32!WSAStartup` família.
- `kernel32!WaitForMultipleObjects` estendido para eventos (P2).

**G82 sugerido: `kernel32!FindFirstFileW` + `FindNextFileW` + `FindClose`** (fecha lacuna de enumeração de diretório, após eventos; usa VFS existente, sem segunda infra). Alternativa: estender `WaitForMultipleObjects` para eventos.

## 22. Motivo da escolha G81
G81 foi escolhido conforme indicação do G80: eventos são P2, fundamentais para sincronização Windows, usados por muitos programas para sinalização thread→thread. Implementação similar a mutex (pthread condvar), isolável, sem segunda arquitetura, fecha lacuna de `WaitForSingleObject` que já existia para mutex/thread. PE real validou contrato completo.

## 23. Progresso estimado
- Runtime: 53 → 57 APIs implementadas (+4 em G81, +7 no lote G75-G81).
- C: 3399/0 preservado, 0 falhas.
- Swift: 63/63 esperado.
- Battery: 66 PEs, todos verdes, incluindo novo evento.
- Checklist: 51 → 52.
- Cadeia PE→Win32→sync→VFS→GUI: avançada — agora com eventos reais cross-thread, file W variants, ReadFileEx, GetSystemMetrics. Falta file enum, registry, GDI texto, Metal/janela real.
- Progresso honesto para "runtime capaz de jogos PC simples": ~57% (fundação sync/file/unicode/GUI metrics sólida).

## 24. Regra contra falsos verdes
- Nenhum sucesso falso: named events falham honesto 87, invalid handle 6, double close 6, use-after-close WAIT_FAILED.
- Wait timeout retorna 0x102, não 0.
- Manual vs auto comportamento distinto, não fingido.
- Nenhum teste removido/enfraquecido; apenas adicionados.
- PE é régua: não modificado para esconder falha do runtime.

## 25-28. Evidências adicionais
- `Tests/PorticoRuntimeTests/data/hello_event.exe` 16 KiB, imports verificados via `objdump -p`.
- `build/dbg_input` 0 warnings, executa 66 PEs.
- `realpe/hello_event.c` fonte completa com comentários de contratos.
- Disassembly `hello_event_debug.exe` com `mov rcx/r9d/r8d/edx` + `call [__imp_CreateEventA]`.
- MD5 `pr_win32.c` G81: `8c0e1faef9f519f8d39921f55d2de60b`.
- `docs/STATUS.md` ainda reflete G80 (atualização pendente até validação iOS).
