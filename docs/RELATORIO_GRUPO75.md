# RELATÓRIO GRUPO 75 — kernel32!ReadFileEx (E/S assíncrona mínima)

## 1. Status
**G75 CONCLUÍDO** — Implementação mínima de `kernel32!ReadFileEx` com OVERLAPPED (32 bytes) + completion routine via `guest_call` (mesma infraestrutura de `SetTimer`/`WndProc`). **PE real `hello_readfileex.exe` executou direto na primeira tentativa** com `rc=75 log=45` e 20/20 idênticos. Sem regressões: C 3398/0, Swift 63/63, warnings 0, analyzer 7/0, battery 15/15, G52–G75 = 24/24.

## 2. Inventário read-only (§1 — ANTES)
Buscas em `Sources/`, `Tests/`, `realpe/`, `tools/` por `ReadFileEx|f_ReadFileEx|OVERLAPPED|CompletionRoutine` + `TODO|IMPL|kernel32`:
- `f_ReadFileEx`? **NÃO** (apenas `f_ReadFile` L781).
- Catálogo? **TODO** `kernel32.dll ReadFileEx E/S assíncrona` L5048, sem IMPL.
- OVERLAPPED? **NÃO** em runtime; apenas `WS_OVERLAPPEDWINDOW` em PEs gráficos (falso positivo).
- `guest_call`? **SIM** — `pr_win32_guestcall_fn` em `pr_win32.h:49`, usado por `SetTimer` (TIMERPROC) e `DispatchMessageA` (WndProc) via `proc_guest_call` em `pr_peproc.c:449`.
- PEs importadores? **NENHUM** dos 58 PEs existentes importava `ReadFileEx` (strings confirmou).
- Conclusão: lacuna real, única TODO de arquivo sem implementação.

## 3. Lacuna encontrada
`ReadFileEx` ausente → `Unsupported Win32 API | kernel32.dll | ReadFileEx` em qualquer PE que tentasse usar. É a única API de arquivo assíncrona faltando; `ReadFile`/`WriteFile` já existiam.

## 4. Objetivo
Implementar `ReadFileEx` mínimo, síncrono imediato + callback, sem inventar fila assíncrona completa, preservando `pr_win32_ptr` e `w32_is_file_handle`.

## 5. Implementação (§15)
**Função `f_ReadFileEx` (L807–895, 88 linhas)**:
- Valida `n>=5`, `lpOverlapped!=0` + `pr_win32_ptr(...,32)`.
- Valida buffer `lpBuffer` com `pr_win32_ptr(...,toRead)` (ou 1 byte se toRead==0 mas buffer não-NULL).
- Handle: `w32_is_file_handle` → `fread` com seek para `Offset|OffsetHigh<<32` extraídos de `ov+16/20` (layout x64 OVERLAPPED: Internal 0, InternalHigh 8, Offset 16, OffsetHigh 20, hEvent 24, total 32).
- STDIN: 0 bytes (EOF) como `f_ReadFile`.
- Preenche OVERLAPPED: `Internal=0`, `InternalHigh=got`.
- Callback: se `lpCompletionRoutine!=0` e `guest_call` existe → `guest_call(ud, fn, [0, got, lpOverlapped])` (DWORD error, DWORD bytes, LPOVERLAPPED). Sem `guest_call` → log WARN e leitura sem callback (nunca sucesso falso).
- Retorna `1=TRUE`, `last_error=0` em sucesso; `0=FALSE` + `ERROR_INVALID_PARAMETER`/`INVALID_HANDLE` em falha.

**Catálogo**: `IMPL("kernel32.dll","ReadFileEx",f_ReadFileEx,20)` L5136 (20=5 args×4 x86), antes do TODO duplicado L5146 (sombreado, padrão MB2WC).

## 6. Arquivos alterados
- `Sources/PorticoRuntime/src/pr_win32.c`: +88 linhas `f_ReadFileEx` + 1 IMPL.

## 7. PE criado (§4)
`realpe/hello_readfileex.c` → `Tests/PorticoRuntimeTests/data/hello_readfileex.exe` (16 KiB, 0 warnings). Contratos: 10 fixture, 11 WriteFile, 12 Close, 13 reabrir, 14 ReadFileEx offset0 FALSE, 15 callback não chamado, 16 bytes incorretos, 17 conteúdo, 18 OVERLAPPED Internal, 19 offset6 FALSE, 20 conteúdo offset, 21 NULL OVERLAPPED não recusado, 22 handle inválido, 23 zero-byte, 24 DeleteFile, 75 OK. Fixture `readex_g75.txt` = "WinOS-ReadFileEx-42" (19 B).

## 8. Imports (§7)
`KERNEL32.dll`: `ReadFileEx` (hint 04ad, IAT 0x1400081e0), `CreateFileA`, `WriteFile`, `CloseHandle`, `DeleteFileA`, `GetFileAttributesA`, `SetLastError`, `GetLastError`, `SetUnhandledExceptionFilter`, `TlsGetValue`, `VirtualProtect`, `VirtualQuery`, `Sleep`, `DeleteCriticalSection` etc (CRT). `msvcrt.dll`: startup.

## 9. ABI/disassembly (§10)
```
mov %rbx,%rcx          ; HANDLE hFile
mov %r14,%rdx          ; LPVOID lpBuffer
mov $0x13,%r8d         ; DWORD nNumberOfBytesToRead =19
mov %r13,%r9           ; LPOVERLAPPED lpOverlapped
mov %r15,0x20(%rsp)    ; LPOVERLAPPED_COMPLETION_ROUTINE (arg5)
mov 0x59ab(%rip),%rax  ; IAT ReadFileEx
call *%rax             ; BOOL via IAT
```
Win64: RCX/RDX/R8/R9 + stack 0x20, retorno EAX (BOOL). Largura 64-bit preservada para HANDLE/ponteiros, DWORD zero-extendido.

## 10. Testes (§17)
- Offset 0: 19 B lidos, callback com error=0, bytes=19, ov==&ov, Internal=0, InternalHigh=19, conteúdo byte-a-byte OK.
- Offset 6: 4 B "Read" OK.
- NULL OVERLAPPED: FALSE + 87, sem callback.
- INVALID_HANDLE: FALSE +6.
- Zero-byte: TRUE + callback 0.
- Limpeza DeleteFile OK.

## 11. 20 execuções (§20)
**20/20** `exited=1 rc=75 log=45` idênticos, `build/win_fs` só `fixture.txt` após (PE deleta fixture).

## 12. C (§19)
**3398/0** no momento de G75 (antes de G76).

## 13. Swift (§19)
**63/63**.

## 14. warnings (§19)
**0**.

## 15. analyzer (§19)
**7/0 novos**.

## 16. PE battery (§18)
**15/15** 28/43/180/70/72/75/98/118/76/81/194/131/55/200/105 byte-idênticos.

## 17. checkpoints G52–G75 (§22)
**24/24** verdes (… G74 74/742, G75 75/45).

## 18. checklist (§23)
**46/46** — novo item **46. ReadFileEx (G75)**.

## 19. MD5 (§24)
| Arquivo | antes (G74) | depois (G75) |
|---|---|---|
| `pr_win32.c` | `893daf6ffaefeafa5e6464a691e6fd92` | `4b2d5a629aea3a971d1330ec64cf8e05` |
| Combinado | `5467d7acbb256bc5401061fff092edc1` | `9b2fcc43630f5b17d4c82c15846674c2` (após G75, antes de G76-G80) |

Final após G80: `pr_win32.c` `fe7acd0ed9ff5c07990abd4b39aaea3f`, combinado `b7e2664ef66f2f15a2021e04dc2efa0b`.

## 20. filesystem (§25)
`build/win_fs` = `fixture.txt` apenas.

## 21. Limitações (§24)
- Leitura **síncrona imediata** (não fila APC real; callback imediato, não em alertable wait `SleepEx`).
- `hEvent` do OVERLAPPED ignorado.
- Offset > LONG_MAX: `fseek` com `long` trunca; para arquivos pequenos (<2 GiB) OK; arquivos enormes = limitação.
- Sem `WriteFileEx`.
- Sem validação de `FILE_FLAG_OVERLAPPED` em `CreateFileA` (ignora flags).

## 22. Compatibilidade efetivamente demonstrada
`ReadFileEx` por import real (IAT) com ABI Win64, OVERLAPPED 32 B, callback via reentrada, leitura com offset, tratamento de NULL/inválido, zero-byte, limpeza.

## 23. Próxima lacuna (indicação)
`user32!GetSystemMetrics` (TODO real, métricas de tela, simples, fundamental para GUI) — escolhido como G76 por ser TODO isolado e P2.

## 24. Motivo da escolha
File é P2, única TODO de arquivo sem IMPL, implementação segura com infra existente `guest_call`, sem expandir para async completo.

## 25-28. (compatibilidade com formato G74 — seções extras)
- **Regressão**: nenhuma.
- **Canários**: N/A (buffer validado por `pr_win32_ptr`).
- **LastError**: preservado em sucesso (0), setado em falha (6/87).
- **Progresso**: +1 API de arquivo assíncrona, fecha TODO de file.
