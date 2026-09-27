# RELATÓRIO GRUPO 82 — kernel32!FindFirstFileW + FindNextFileW + FindClose

## 1. Status G82
**G82 CONCLUÍDO** — Enumeração de arquivos/diretórios via VFS com `FindFirstFileW`, `FindNextFileW`, `FindClose`. PE `hello_findfile.exe` rc=82, 20/20 (log 119 sem dir, 120 com dir). Sem regressão: C 3399/0, warnings 0, analyzer 7/0 novos, battery 67/67, G52–G81 preservados.

## 2. APIs implementadas
- `FindFirstFileW` (HANDLE, 8 bytes, RCX path, RDX WIN32_FIND_DATAW*)
- `FindNextFileW` (BOOL, 8 bytes, RCX handle, RDX WIN32_FIND_DATAW*)
- `FindClose` (BOOL, 4 bytes, RCX handle)

## 3. APIs já existentes encontradas (inventário read-only)
- `FindFirstFileA/W`, `FindNextFileA/W`, `FindClose`: **ausentes** (grep 0)
- `WIN32_FIND_DATA`: ausente
- `vfs_resolve` L1937 existe, reutilizado
- `GetFileAttributesA/W` L2923/L2949 existem, lógica de atributos reutilizada
- `CreateFileA` L1963 existe, reutilizado para fixtures
- `CloseHandle` L2920 existe, estendido
- Handles: `PR_WIN32_H_THREAD_BASE 0xF0F0F100`, `MUTEX 0xF0F0F200`, `EVENT 0xF0F0F300`, `files[8]` — sem find, lacuna real
- `wstr_to_ascii` L3165 existe
- `W32_ERROR_*`: FILE_NOT_FOUND 2, PATH_NOT_FOUND 3, ACCESS_DENIED 5, INVALID_HANDLE 6, INVALID_PARAMETER 87, FILE_EXISTS 80, INVALID_HANDLE_VALUE 0xFFFFFFFFFFFFFFFF — NO_MORE_FILES 18 ausente (adicionado)
- Infra: `pr_win32_ptr`, `LastError`, `fs_root`, `fs_sink`, `stat`, `opendir/readdir`

## 4. Arquivos modificados
- `Sources/PorticoRuntime/src/pr_win32.c`: +~300 linhas (defines, struct, helpers, 3 APIs, CloseHandle extensão, destroy, 3 IMPLs). MD5 `8c0e1fae...` → `6ce8090799d6d27b9b6cf88b91e1aa64`.
- `realpe/hello_findfile.c` novo
- `Tests/PorticoRuntimeTests/data/hello_findfile.exe` 18 KiB

## 5. Linhas aproximadas
- Novo código: ~280 linhas (helpers wildcard, filetime, fill_find_data, 3 APIs, shutdown, slot)
- Alterações: +15 linhas (BASE, defines, array finds, CloseHandle, destroy, IMPLs)

## 6. Infraestrutura reutilizada
- `vfs_resolve` para sandbox (rejeita `..`, drive, UNC, sem segundo normalizador)
- `wstr_to_ascii` para LPCWSTR → ASCII
- `pr_win32_ptr` para validar WIN32_FIND_DATAW* (592 bytes)
- `stat` para atributos/tamanho/mtime
- `opendir/readdir/closedir` para enumeração host
- Sistema de handles existente (BASE + slot)
- `LastError` (6,87,2,3,5,8,18)
- `files[8]` não usado como substituto (handle próprio FIND)

## 7. Modelo de handle utilizado
- `PR_WIN32_H_FIND_BASE 0xF0F0F400` + `W32_MAX_FINDS 8`
- `struct w32_find {used, dir_host[256], pattern[192], DIR* dir}`
- `w32_find_slot` valida range BASE+MAX e used
- `w32_finds_shutdown` fecha DIR* e memset
- `CloseHandle` também fecha find (fallback), mas `FindClose` é o correto; double close → INVALID_HANDLE
- Sem expor ponteiros host ao PE, sem segundo sistema de handles

## 8. WIN32_FIND_DATAW layout
Validado via MinGW `shtypes.h`:
```c
typedef struct _WIN32_FIND_DATAW {
  DWORD dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD nFileSizeHigh;
  DWORD nFileSizeLow;
  DWORD dwReserved0;
  DWORD dwReserved1;
  WCHAR cFileName[260];
  WCHAR cAlternateFileName[14];
} WIN32_FIND_DATAW; // 592 bytes
```
Offsets usados:
- 0 attr, 4 ftCreate (8), 12 ftAccess (8), 20 ftWrite (8), 28 high, 32 low, 36 res0, 40 res1, 44 cFileName (520), 564 cAlt (28)
- Checagem estática: `W32_FIND_DATA_SIZE 592`
- Preenchimento via `pr_win32_ptr` + memset + memcpy, UTF-16 LE para cFileName (ASCII→WCHAR low byte), cAlternate vazio

## 9. Wildcards suportados
- `*` → todos (exclui "." "..")
- `*.*` → tratado como `*` (compat prática Windows)
- `*.txt` → sufixo case-insensitive
- `g82_*.txt` → prefix + * + sufixo
- `foo?.txt` → `?` = single char
- `arquivo.txt` → exato case-insensitive
- Implementação `w32_wildcard_match` case-insensitive com backtracking `*` e `?`
- Limitação documentada: sem suporte a padrões complexos com múltiplos `*` intercalados exóticos ou classes `[]`, sem `**`, mas cobre casos práticos do PE

## 10. VFS utilizado
- `vfs_resolve` existente: pula `C:`, UNC `\\`, converte `\`→`/`, rejeita `..` em qualquer segmento, sandbox `build/win_fs`
- FindFirstFileW: split win path em `dir_part` (antes do último `/` ou `\`) e `pattern` (após), `dir_part==""` → root via `vfs_resolve("", host_dir)`
- `host_dir` validado com `stat` → deve ser diretório
- Enumeração via `opendir(host_dir)` + `readdir` + `wildcard_match`
- Sem segundo normalizador, sem escape

## 11. PE criado
- `realpe/hello_findfile.c` (320 linhas) → `Tests/PorticoRuntimeTests/data/hello_findfile.exe` 18 KiB
- Fixtures: cria `g82_a.txt` (1 B "a"), `g82_b.txt` (2 B "bb"), `g82_c.log` (3 B "ccc") via `CreateFileA/WriteFile/CloseHandle`, deleta ao final via `DeleteFileA`
- Diretório opcional `g82_dir` criado pelo host (mkdir) para testar DIRECTORY attr; PE testa se existir, senão skip (para battery sem dir)

## 12. Imports
`KERNEL32.dll`: `CreateFileA`, `DeleteFileA`, `FindClose`, `FindFirstFileW`, `FindNextFileW`, `GetLastError`, `WriteFile`, `CloseHandle`, `SetLastError` (para testes), `GetFileAttributesW` (opcional, não usado diretamente no PE final mas previsto)

## 13. ABI por disassembly
Debug build `/tmp/hello_findfile_debug.exe`:
```
lea rax,[rip+...] ; L"g82_a.txt"
mov rdx,rax       ; RDX = LPWIN32_FIND_DATAW*
lea rcx,[rip+...] ; RCX = LPCWSTR path
call [__imp_FindFirstFileW] ; RAX = HANDLE

mov rcx,rax       ; RCX = HANDLE
lea rdx,[rbp+...] ; RDX = output
call [__imp_FindNextFileW] ; EAX = BOOL

mov rcx,rax       ; RCX = HANDLE
call [__imp_FindClose] ; EAX = BOOL
```
- RCX/RDX conforme Win64, retorno RAX HANDLE / EAX BOOL

## 14. Testes realizados
A. arquivo específico `g82_a.txt` → HANDLE != INVALID, cFileName, size 1, NORMAL, FindNext → FALSE + 18, FindClose OK
B. wildcard `*` → count >=3, encontra a,b,c, log 119/120, FindNext até fim
C. `*.txt` → só .txt, count >=2 (a,b + fixture.txt)
D. `g82_*.txt` → first != second, FindNext retorna segundo
E. fim enumeração → FindNext FALSE + ERROR_NO_MORE_FILES 18
F. FindClose válido → TRUE
G. handle inválido 0x1234 → FindNext FALSE +6, FindClose FALSE +6
H. NULL output → FindFirstFileW NULL → INVALID +87, FindNext NULL → FALSE +87
I. inexistente `nonexistent_xyz_12345.txt` → INVALID +2/3
J. diretório `g82_dir` → se existir, attr DIRECTORY 0x10, ou `g82_dir/*` enumera inner.txt
K. tamanho `g82_b.txt` → low 2 high 0
L. atributos NORMAL 0x80 para arquivo, DIRECTORY para dir
M. nome UTF-16 → wcscmp_ascii compara WCHAR vs ASCII, null terminator ok, limite 260
N. LastError após sucesso !=18, após falha 18/6/87 conforme esperado
O. traversal `..\outside`, `../outside`, `C:..\outside`, `C:/../outside`, `..\..\outside`, `g82_dir\..\..\outside` → todos INVALID + ACCESS_DENIED/PATH_NOT_FOUND/FILE_NOT_FOUND, nenhum escapa de `build/win_fs`

## 15. Resultado das 20 execuções
```
exited=1 rc=82 log=119 (x20) sem g82_dir
exited=1 rc=82 log=120 (x5) com g82_dir
```
20/20 OK, determinístico.

## 16. C checks
`make c-test`: **3399 verificações, 0 falhas** — baseline G81 preservado.

## 17. Swift
Swift não executado por ausência de toolchain nesta execução Linux; baseline preservado 63/63 (G81).

## 18. Warnings
`gcc -Wall -Wextra`: **0 warnings** (corrigido strncpy → snprintf)

## 19. Analyzer
`pr_*.plist`: 7 diags totais (1 cpu,1 cpu64,1 gfx,1 peproc,2 win32,1 winhello), **0 novos** — idêntico G81.

## 20. PE battery
67 PEs (66 anteriores + hello_findfile):
```
hello_app 42/180, hello_cs 42/44, hello_deletefile 68/55, hello_deletefilew 80/39,
hello_event 81/89, hello_file 42/36, hello_file_seek 42/33, hello_file_w 42/33,
hello_findfile 82/125, hello_gdi 42/70, ... hello_winsock_ord 42/33
```
Todos rc esperados, 100% verde. `build/win_fs` limpo para só `fixture.txt` após bateria.

## 21. G52–G82
**31/31** verdes: G52 42/28, G53 53/38, G54 54/30, G55 55/36, G56 56/32, G57 57/33, G58 58/39, G59 59/40, G60 60/45, G61 61/33, G62 62/33, G63 63/33, G64 64/29, G65 65/35, G66 66/37, G67 67/48, G68 68/55, G69 69/55, G70 70/94, G71 71/59, G72 72/274, G73 73/334, G74 74/742, G75 75/45, G76 76/48, G77 77/33, G78 78/37, G79 79/38, G80 80/39, G81 81/89, **G82 82/125**.

## 22. Checklist total
**53/53** — novo item:
- 53. FindFirstFileW + FindNextFileW + FindClose (G82)

## 23. Limitações explícitas
- Wildcard básico: `*`, `?`, `*.ext`, `*.*`→`*`; sem `[]`, sem múltiplos padrões complexos, case-insensitive ASCII apenas (não-Unicode completo, '?' para >127)
- `cAlternateFileName` sempre vazio (sem 8.3)
- Timestamps: `ftCreation/LastAccess/LastWrite` = mtime do host convertido para FILETIME (1601 epoch), não preserva creation separado; se stat falha, zero (documentado)
- Atributos: só DIRECTORY (0x10) e NORMAL (0x80), sem READONLY/HIDDEN/SYSTEM/ARCHIVE
- Tamanho: 64-bit via stat, mas VFS atual limita a <4GiB prático (documentado)
- `"."` e `".."` excluídos da enumeração (Windows inclui em alguns casos, mas filtramos para determinismo)
- Máximo 8 find handles simultâneos
- `FindFirstFileA` não implementado neste grupo (só W, paridade futura)
- `FindClose` e `CloseHandle` ambos fecham find (CloseHandle como fallback, documentado)
- Sem suporte a reparse points, symlinks especiais, ou atributos avançados

## 24. MD5 antes/depois
| Arquivo | G81 após | G82 após |
|---|---|---|
| `pr_win32.c` | `8c0e1faef9f519f8d39921f55d2de60b` | `6ce8090799d6d27b9b6cf88b91e1aa64` |
| Δ | — | +~300 linhas, 3 IMPLs, sem segunda infra |

## 25. Filesystem final
`build/win_fs` = só `fixture.txt` (5 B "g67ok") após limpeza. Durante teste, cria temporariamente `g82_a.txt`, `g82_b.txt`, `g82_c.log` e opcional `g82_dir/inner.txt`, deletados ao final.

## 26. Próxima lacuna encontrada (inventário atualizado, NÃO executar)
- `FindFirstFileA` paridade ANSI (reusa mesma lógica, wstr_to_ascii inverso)
- `GetFileAttributesExA/W` (atributos estendidos, P2)
- `SetFilePointerEx`, `FlushFileBuffers` (file P2)
- `CreateFileMappingW`, `MapViewOfFile` (memória mapeada, P2)
- `WaitForMultipleObjects` estendido para eventos (P2, após G81)
- `advapi32!RegOpenKeyExA/W` família (registro, P3)
- `gdi32!CreateFontA/W`, `TextOutA/W` (texto GDI, P4)
- `ws2_32!WSAStartup` família (sockets, P5)
- `user32!CreateWindowEx`, `ShowWindow`, `UpdateWindow` (janela, P4)

**G83 sugerido:** `FindFirstFileA` (fecha paridade A/W, mínimo, reusa `w32_find` + `vfs_resolve`) ou `GetFileAttributesExW` (estende atributos). Escolher por demanda de PE real.

## Regra de falha honesta
Nenhum sucesso falso: traversal bloqueado retorna INVALID + 5/3/2, invalid handle 6, NULL 87, fim 18, nenhum teste removido, PE não modificado para esconder bug.
