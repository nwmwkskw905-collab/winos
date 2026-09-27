# RELATÓRIO GRUPO 80 — kernel32!DeleteFileW

## 1. Status
**G80 CONCLUÍDO** — Variante Unicode de `DeleteFileA`. PE rc=80 log=39, 20/20. Fecha lote de W variants de arquivo.

## 2. Inventário
- A existe L~2745, W NÃO.
- Catálogo só A.
- File W variant.

## 3. Implementação
`f_DeleteFileW` L~2790: `wstr_to_ascii` → `vfs_resolve` → `stat` → `unlink` → BOOL. Valida NULL → 87, inexistente → 2, dir → 5 (ACCESS_DENIED).

Catálogo: `IMPL DeleteFileW f_DeleteFileW 4`.

## 4. Arquivos
- `pr_win32.c`: +22 linhas +1 IMPL.

## 5. PE
`hello_deletefilew.c` → `hello_deletefilew.exe` (14 KiB). Contratos: 10 fixture, 11 DeleteFileW falhou, 12 attr após delete, 13 inexistente não falha, 14 LastError inexistente, 15 NULL, 80 OK. Fixture `delw_g80.txt`.

## 6. Imports
`KERNEL32`: `CreateFileA`, `WriteFile`, `CloseHandle`, `GetFileAttributesA`, `DeleteFileW`, `SetLastError`, `GetLastError`.

## 7. ABI
RCX LPCWSTR, retorno EAX BOOL.

## 8. Testes
- Delete W OK + attr INVALID após.
- Inexistente FALSE +2.
- NULL FALSE +87.

## 9. 20 execuções
20/20 rc=80 log=39, win_fs só fixture.txt.

## 10. C
3399/0.

## 11. Swift
63/63.

## 12. warnings
0.

## 13. analyzer
7/0 novos.

## 14. PE battery
15/15 28/43/180/70/72/75/98/118/76/81/194/131/55/200/105.

## 15. checkpoints G52–G80
**29/29** verdes: … G75 75/45, G76 76/48, G77 77/33, G78 78/37, G79 79/38, G80 80/39, preservando G74 74/742 e G73 73/334.

## 16. checklist
**51/51** — itens novos:
- 46. ReadFileEx (G75)
- 47. GetSystemMetrics (G76)
- 48. GetCurrentDirectoryW (G77)
- 49. GetModuleFileNameW (G78)
- 50. GetFileAttributesW (G79)
- 51. DeleteFileW (G80)

## 17. MD5
| Arquivo | G74 após | G80 após (final) |
|---|---|---|
| `pr_win32.c` | `893daf6ffaefeafa5e6464a691e6fd92` | `fe7acd0ed9ff5c07990abd4b39aaea3f` |
| Combinado | `5467d7acbb256bc5401061fff092edc1` | `b7e2664ef66f2f15a2021e04dc2efa0b` |

Mudança total: +~200 linhas (6 APIs + forward decl), sem segunda infra.

## 18. filesystem
`build/win_fs` = só `fixture.txt` (5 B "g67ok").

## 19. Limitações (G75–G80 acumuladas)
- ReadFileEx síncrono imediato, sem APC/SleepEx, hEvent ignorado, offset > LONG_MAX trunca.
- GetSystemMetrics valores fixos, 1 monitor, sem UIKit.
- W variants: conversão ASCII↔UTF-16 simples (<128), '?' para >127, sem CP1252 completo.
- GetModuleFileNameW só módulo principal.
- GetFileAttributesW/DeleteFileW atributos mínimos.
- Sem WriteFileEx, sem FindFirstFile, sem CreateEvent (próximos candidatos).

## 20. Compatibilidade efetivamente demonstrada
- ReadFileEx por import real com OVERLAPPED 32 B + callback via guest_call.
- GetSystemMetrics por import real com 60+ índices.
- GetCurrentDirectoryW/A paridade byte-a-byte.
- GetModuleFileNameW/A paridade + truncamento.
- GetFileAttributesW/A paridade + inexistente.
- DeleteFileW/A paridade + limpeza.
- Todos com ABI Win64 provada, 20/20, sem regressões.

## 21. Próxima lacuna (indicação — NÃO executar ainda)
Do inventário atualizado:
- `kernel32!CreateEventA/W`, `SetEvent`, `ResetEvent` (sync, P2, fundamental para muitos PEs, requer extensão de WaitForSingleObject).
- `kernel32!FindFirstFileA/W`, `FindNextFile`, `FindClose` (file enum, P2).
- `kernel32!GetFileAttributesEx`, `SetFilePointerEx`, `FlushFileBuffers`.
- `advapi32!RegOpenKeyExA` família (registro, P3).
- `gdi32!CreateFontA`, `TextOutA` (texto GDI, P4).
- `ws2_32!WSAStartup` família (sockets, P5).

**G81 sugerido: `kernel32!CreateEventA/W` + `SetEvent`/`ResetEvent`** (sync, fecha lacuna de eventos; WaitForSingleObject já existe para mutex/thread, pode ser estendido). Alternativa: `FindFirstFileW` (file enum). Escolher por demanda de PE real.

## 22. Motivo da escolha G81
Sync é P2, após file W variants, eventos são usados por muitos programas Windows para sinalização; implementação similar a mutex (pthread condvar), segura e isolável.

## 23. Progresso estimado
- Runtime: 53 → 59 APIs implementadas (+6).
- C: 3398→3399 checks, 0 falhas.
- Swift: 63/63.
- Battery: 15/15 preservada.
- Checklist: 45→51.
- Cadeia PE→Win32→callback→VFS→GUI metrics→Unicode file: **avançada**, mas ainda sem janela gráfica real com Metal + áudio + input completo + iOS device validation.
- Progresso honesto para "primeiro runtime capaz de jogos PC simples": ~55% (fundação file/sync/unicode/GUI metrics sólida, falta eventos, file enum, registry, GDI texto, e camada Metal/janela).

## 24. Regra contra falsos verdes
Nenhum sucesso falso: ReadFileEx falha honesta em NULL/inválido, GetSystemMetrics retorna 0 para índice desconhecido, W variants falham com LastError correto, nenhum teste removido (apenas atualizado para refletir IMPLEMENTED).

## 25-28. Evidências adicionais
- `build/hello_readfileex.dis` com ABI RCX/RDX/R8/R9/stack.
- `build/win_fs/fixture.txt` = "g67ok".
- Todos PEs novos em `Tests/PorticoRuntimeTests/data/` com 0 warnings MinGW.
- `tools/dbg_input` rebuilt com `-Wall -Wextra` 0 warnings.
