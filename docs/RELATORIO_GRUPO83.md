# RELATÓRIO GRUPO 83 — kernel32!FindFirstFileA + FindNextFileA + FindClose (paridade ANSI)

## 1. Status G83
**G83 CONCLUÍDO** — Fecha paridade ANSI/W da enumeração de arquivos. Reutiliza 100% da infraestrutura G82 (`w32_find`, `PR_WIN32_H_FIND_BASE`, `W32_MAX_FINDS`, `vfs_resolve`, `opendir/readdir`, wildcard). PE `hello_findfilea.exe` rc=83, 20/20 log 159 (sem dir) / 186 (com dir). Sem regressão: C 3399/0, warnings 0, analyzer 7/0 novos, battery 68/68, G52–G82 preservados.

## 2. Inventário antes/depois (read-only obrigatório)

**Antes (G82 final):**
- `FindFirstFileA` — ausente (grep 0)
- `FindNextFileA` — ausente
- `FindClose` — existe L2574 (para W, mas funciona para A)
- `WIN32_FIND_DATAA` — ausente, só `WIN32_FIND_DATAW` 592 B
- `w32_find` L192 existe, `PR_WIN32_H_FIND_BASE 0xF0F0F400` L42, `W32_MAX_FINDS 8`, `w32_find_slot` L2194, `w32_finds_shutdown` L2200, `vfs_resolve` L1954, `wstr_to_ascii` L3448, `W32_FIND_DATAW_SIZE 592`, `W32_FIND_DATAA` não
- `FindFirstFileW` L2462, `FindNextFileW` L2629, `FindClose` L2689 existem e verdes
- Catálogo: só W (`FindFirstFileW`, `FindNextFileW`, `FindClose`), sem A
- `CloseHandle` L3147 já trata find
- `GetFileAttributesA/W`, `CreateFileA/W` mostram padrão: A lê guest char* via `pr_win32_ptr`, W usa `wstr_to_ascii`
- PE G82 `hello_findfile.exe` rc=82 verde

**Depois (G83):**
- `FindFirstFileA` L2629 adicionado, `FindNextFileA` L2740 adicionado, `FindClose` reutilizado (não duplicado)
- `WIN32_FIND_DATAA` layout validado 320 B, constantes `W32_FIND_DATAA_SIZE 320` + offsets
- `w32_fill_find_dataA` novo, `w32_fill_find_data` (W) preservado
- Catálogo +2 IMPLs (A), total 62 APIs (60→62)

**O que foi reutilizado de G82:**
- `w32_find` struct, `PR_WIN32_H_FIND_BASE`, `W32_MAX_FINDS`, `w32_find_slot`, `w32_finds_shutdown`, `vfs_resolve`, `opendir/readdir/closedir`, `w32_wildcard_match`, `w32_filetime_from_time_t`, `LastError`, `pr_win32_ptr`, sistema de handles, `CloseHandle` extensão, `FindClose`

**O que precisou ser novo:**
- Layout `WIN32_FIND_DATAA` (320 B) e offsets
- `w32_fill_find_dataA` (ANSI)
- `f_FindFirstFileA`, `f_FindNextFileA` (leitura ANSI guest, split dir/pattern idêntico ao W, reutiliza mesmo fluxo VFS + DIR*)
- IMPLs A

## 3. Arquivos modificados
- `Sources/PorticoRuntime/src/pr_win32.c`: +~200 linhas (constantes DATAA, fill DATAA, 2 APIs). MD5 `6ce8090799d6d27b9b6cf88b91e1aa64` → `4e08c334acf46174742d8f9b47993443`
- `realpe/hello_findfilea.c` novo (350 linhas)
- `Tests/PorticoRuntimeTests/data/hello_findfilea.exe` 18 KiB

## 4. APIs implementadas
- `FindFirstFileA` (HANDLE, 8 bytes, RCX LPCSTR, RDX LPWIN32_FIND_DATAA)
- `FindNextFileA` (BOOL, 8 bytes, RCX HANDLE, RDX LPWIN32_FIND_DATAA)
- `FindClose` já existente, validado para A e W (mesmo handle)

## 5. Layout WIN32_FIND_DATAA
Validado via MinGW headers `minwinbase.h` + `shtypes.h` + compilação `/tmp/size.o`:
- `sizeof(WIN32_FIND_DATAA) = 320` (0x140), `sizeof(WIN32_FIND_DATAW)=592` (0x250) — extraído de `.data` section `40 01 00 00` e `50 02 00 00`
- Layout A:
```c
DWORD dwFileAttributes; // 0
FILETIME ftCreationTime; // 4 (8)
FILETIME ftLastAccessTime; //12 (8)
FILETIME ftLastWriteTime; //20 (8)
DWORD nFileSizeHigh; //28
DWORD nFileSizeLow; //32
DWORD dwReserved0; //36
DWORD dwReserved1; //40
CHAR cFileName[260]; //44 (260)
CHAR cAlternateFileName[14]; //304 (14) + 2 padding =320
```
- Offsets: `DATAA_OFF_ATTR 0, FT_CREATE 4, FT_ACCESS 12, FT_WRITE 20, HIGH 28, LOW 32, RES0 36, RES1 40, CFILE 44, CALT 304`
- Comparação: W tem `cFileName` UTF-16 520 B em offset 44, `cAlt` 28 B em 564, total 592. A tem 260 B ANSI em 44, 14 B em 304, total 320. Diferença validada, sem casts perigosos.

## 6. Estratégia ANSI
- Runtime já usa ACP = ASCII com `?` para >127 (padrão de `wstr_to_ascii` e `CreateFileA` que lê byte a byte)
- `FindFirstFileA` lê guest LPCSTR via `pr_win32_ptr` loop (igual `CreateFileA` L1991): `while(p[i] && pr_win32_ptr(...))`
- Sem nova tabela CP1252, mantém convenção existente: ASCII <128 direto, >127 preservado como byte (não `?` aqui, mas `?` já usado em W→A). Documentado como limitação: sem Unicode completo, apenas ASCII prático
- Pattern matching reutiliza mesmo `w32_wildcard_match` case-insensitive ASCII (já usado para W, que também converteu para ASCII via `wstr_to_ascii`)
- Output ANSI: `w32_fill_find_dataA` escreve `char` direto, NUL terminator, sem overflow (max 259)

## 7. Estratégia de reutilização do w32_find
- Mesma struct `w32_find {used, dir_host[256], pattern[192], DIR* dir}`
- Mesmo BASE `0xF0F0F400`, mesmo `W32_MAX_FINDS 8`, mesmo `w32_find_slot`, mesmo `w32_finds_shutdown`
- `FindFirstFileA`: converte ANSI → win[192], split dir_part/pattern igual W (último `/\`), `vfs_resolve` para dir, `opendir`, loop `readdir` + `wildcard_match`, `stat`, `fill_dataA`, retorna `BASE+slot`
- `FindNextFileA`: mesmo que W, mas `fill_dataA`
- `FindClose`: único, funciona para handles criados por A ou W (testado cross: W handle fechado por FindClose, A handle fechado por FindClose)
- Sem `w32_find_a`, sem novo BASE, sem novo VFS, sem novo matcher, sem segunda enumeração host

## 8. ABI por disassembly
Debug build `/tmp/hello_findfilea_debug.exe`:
```
lea rcx,[rip+...] ; LPCSTR "g83_a.txt"
lea rdx,[rbp+...] ; LPWIN32_FIND_DATAA
call [__imp_FindFirstFileA] ; RAX HANDLE

mov rcx,rax ; HANDLE
lea rdx,[rbp+...]
call [__imp_FindNextFileA] ; EAX BOOL

mov rcx,rax
call [__imp_FindClose] ; EAX BOOL
```
- RCX/RDX Win64, RAX HANDLE / EAX BOOL — comprovado via `objdump -d -M intel`

## 9. PE criado
- `realpe/hello_findfilea.c` 350 linhas → `hello_findfilea.exe` 18 KiB
- Imports: `CreateFileA`, `DeleteFileA`, `FindClose`, `FindFirstFileA`, `FindNextFileA`, `FindFirstFileW` (para cross test), `WriteFile`, `CloseHandle`, `GetLastError`, `SetLastError`
- Fixtures: `g83_a.txt` (1 B), `g83_b.txt` (2 B), `g83_c.log` (3 B) criados via `CreateFileA`, deletados via `DeleteFileA` ao final
- Sem dependência de arquivos permanentes externos

## 10. Testes do PE
A. `FindFirstFileA("g83_a.txt")` → HANDLE válido, cFileName, size 1, NORMAL
B. `*` → encontra a,b,c, count>=3
C. `*.txt` → só .txt, count>=2
D. `g83_*.txt` → first!=second
E. `g83_?.txt` → count>=2 (? test)
F. fim → FALSE + 18
G. FindClose TRUE
H. handle inválido 0x1234 → FALSE+6, FindClose→6
I. NULL output → INVALID+87 / FALSE+87
J. NULL path → INVALID+87
K. inexistente → INVALID +2/3
L. traversal `..\outside`, `../outside`, `C:..\outside`, `C:/../outside`, `..\..\outside`, `g83_dir\..\..\outside` → INVALID, não escapa `build/win_fs`
M. tamanho high/low → 2/0 para b
N. filename ANSI → NUL terminator, strlen>0
- Extra: cross W handle fechado por mesmo FindClose, 8 handles simultâneos OK, 9º falha determinística

## 11. 20 execuções
```
exited=1 rc=83 log=159 x20 (sem g82_dir)
exited=1 rc=83 log=186 x20 com fixtures extras (battery)
```
Determinístico, sem flakiness.

## 12. C tests
`make c-test`: **3399 verificações, 0 falhas** — preservado.

## 13. Swift
Swift não executado por ausência de toolchain nesta execução Linux; baseline preservado 63/63 (G82). Registrado explicitamente.

## 14. Warnings
`gcc -Wall -Wextra`: **0 warnings**, 0 novos.

## 15. Analyzer
`pr_*.plist` 7 diags totais (1 cpu,1 cpu64,1 gfx,1 peproc,2 win32,1 winhello), **0 novos**.

## 16. PE battery
68 PEs (67 anteriores + hello_findfilea):
- `hello_findfile 82/125`, `hello_findfilea 83/186`, todos rc esperados, 100% verde
- `hello_event 81/89`, `hello_deletefilew 80/39`, etc. preservados

## 17. G52–G83
**32/32** verdes: G52 42/28, G53 53/38, G54 54/30, G55 55/36, G56 56/32, G57 57/33, G58 58/39, G59 59/40, G60 60/45, G61 61/33, G62 62/33, G63 63/33, G64 64/29, G65 65/35, G66 66/37, G67 67/48, G68 68/55, G69 69/55, G70 70/94, G71 71/59, G72 72/274, G73 73/334, G74 74/742, G75 75/45, G76 76/48, G77 77/33, G78 78/37, G79 79/38, G80 80/39, G81 81/89, G82 82/125, **G83 83/186**

## 18. Checklist total
**54/54** — novo item:
- 54. FindFirstFileA + FindNextFileA + FindClose paridade ANSI (G83)

## 19. Filesystem final
`build/win_fs` = só `fixture.txt` (5 B "g67ok") após limpeza. Durante teste, cria temporariamente `g83_a.txt`, `g83_b.txt`, `g83_c.log`, deletados. Sem leaks DIR* (shutdown fecha).

## 20. Limitações documentadas
- ANSI = ASCII prático, sem CP1252 completo, >127 preservado como byte, `?` para W→A conversão mas A→host direto
- Wildcard mesmo de G82: `* ? *.txt *.*→*`, sem `[]`
- `cAlternateFileName` vazio (sem 8.3)
- Timestamps = mtime host→FILETIME, sem creation separado
- Atributos só DIRECTORY/NORMAL
- Tamanho 64-bit via stat, prático <4GiB
- `.`/`..` excluídos
- Max 8 handles, 9º falha determinística NOT_ENOUGH_MEMORY
- `FindClose` único para A e W (documentado, não duplicado)
- Sem segundo filesystem/handles/VFS/matcher

## 21. MD5 antes/depois
| Arquivo | G82 após | G83 após |
|---|---|---|
| `pr_win32.c` | `6ce8090799d6d27b9b6cf88b91e1aa64` | `4e08c334acf46174742d8f9b47993443` |
| Δ | — | +~200 linhas, 2 IMPLs, 1 fill DATAA, sem duplicação |

## 22. Próximas lacunas encontradas (inventário final, NÃO executar)
- `GetFileAttributesExA/W` (atributos estendidos, P2)
- `SetFilePointerEx`, `FlushFileBuffers` (file P2)
- `CreateFileMappingA/W`, `MapViewOfFile` (mmap, P2)
- `WaitForMultipleObjects` estendido para eventos (P2, após G81)
- `advapi32!RegOpenKeyExA/W` família (registro, P3)
- `gdi32!CreateFontA/W`, `TextOutA/W` (texto GDI, P4)
- `ws2_32!WSAStartup` família (sockets, P5)
- `user32!CreateWindowEx`, `ShowWindow` (janela, P4)
- Outras: `GetFullPathNameA/W`, `GetLongPathName`, `RemoveDirectory`, `CreateDirectory`

**G84 sugerido:** `GetFileAttributesExW` (estende atributos, reusa `stat` + `vfs_resolve`) ou `SetFilePointerEx` (file pointer 64-bit). Escolher por demanda de PE real.

## Regra de falha honesta
Nenhum sucesso falso: traversal bloqueado, invalid handle 6, NULL 87, fim 18, 9º handle falha, sem crash host, nenhum teste removido, PE não modificado para esconder bug. GREEN validado, YELLOW limitações documentadas, RED nenhum.
