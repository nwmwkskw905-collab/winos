# RELATÓRIO GRUPO 67 — kernel32!GetFileAttributesA validada por PE x64 real

## 1. Status
**CONCLUÍDO COM SUCESSO** — `GetFileAttributesA` validada por PE x64 real (`hello_getfileattributes.exe`, 14848 bytes) conectada ao **filesystem virtual existente** (`vfs_resolve` + `fs_root="build/win_fs"`): import **por nome** resolvido pelo loader (hint **0262**, IAT `0x1400081a8`); ABI **RCX = lpFileName** e retorno **DWORD via EAX** comprovados por disassembly; arquivo guest existente → `0x80` (NORMAL); inexistente → `INVALID_FILE_ATTRIBUTES` + LastError **2** (mapeamento existente); NULL → INVALID + **87**; separadores `/` e `\` idênticos; diretório (`"."`) → `0x10` (DIRECTORY); **20/20 rc=67** (log=48); C **3398/0**; Swift **63/63**; warnings 0; analyzer **7** (pré-existentes); battery 15/15; checkpoints **G52–G66 15/15 verdes**.

## 2. API validada
`kernel32.dll!GetFileAttributesA(LPCSTR lpFileName)` → `DWORD` (atributos ou `INVALID_FILE_ATTRIBUTES = 0xFFFFFFFF`). `GetFileAttributesW` = **0 implementações** (fora do escopo; nenhuma conversão ANSI/Unicode iniciada).

## 3. Inventário (§1 — read-only, antes de alterar)
Buscas por `GetFileAttributesA|f_GetFileAttributesA|GetFileAttributesW|f_GetFileAttributesW|GetFileAttributes|FILE_ATTRIBUTE_|INVALID_FILE_ATTRIBUTES|fs_root|pr_win32_set_fs_root|pr_win32_normalize|virtual filesystem` em Sources/Tests/realpe/tools + catálogo kernel32:
1. handler: **NÃO existe**; 2. TODO: **NÃO existe** (API fora do catálogo); 3. registro: **NENHUM**; 4. `stdcall_bytes`: N/A → criado **4** (1 arg × 4); 5. constantes `FILE_ATTRIBUTE_*`/`INVALID_FILE_ATTRIBUTES`: **0 definições** (criado só o mínimo, §17); 6. FS virtual = `ctx->fs_root[256]` (L208) + **`vfs_resolve`** (L1784–1807) + `ctx->files[8]` (handles `W32_FILE_BASE|k`); 7. conversão guest→host = `vfs_resolve` (pula drive `C:`, `\`→`/`, **recusa `..`**, prefixa `fs_root/`); 8. APIs reutilizáveis: `f_CreateFileA` (L1810/28), `f_CreateFileW` (L1876), `f_WriteFile` (L726/20), `f_ReadFile` (L779/20), `f_GetFileSize` (L2376/8), `f_CloseHandle` (L2335/4); 9. helper de stat/metadata: **NENHUM** (usado host `stat()` interno); 10. fixtures = os próprios PEs criam via `CreateFileA(CREATE_ALWAYS)+WriteFile` (`hello_file.c` → `winos_file_test.txt` em `build/win_fs`).

## 4. Handler
**Criado**: `f_GetFileAttributesA` (antes de `f_GetProcAddress`): `n<1`→`PR_ERR_INVALID`; `a[0]==0` ou ponteiro inválido → `last_error=87`, `0xFFFFFFFF`; string guest lida **byte a byte com `pr_win32_ptr` por posição** (padrão idêntico ao de `f_CreateFileA`, sem `strlen` cru, NUL-bounded); `vfs_resolve` (reusada); host `stat()`; **mapeamento mínimo**: diretório→`0x10`, arquivo→`0x80`; não-existe → `W32_ERROR_FILE_NOT_FOUND=2` + `0xFFFFFFFF`; sem `fs_root` → `3`, traversal negado → `5` (mesmo mapeamento de `f_CreateFileA`).

## 5. Catálogo
**1 registro novo** (0 TODO substituído): `IMPL("kernel32.dll", "GetFileAttributesA", f_GetFileAttributesA, 4)` após `GetStartupInfoA`. TODOs: 31 antes = 31 depois (**0 removidos**). Sem duplicação (o par `IMPL`+`TODO` pré-existente de `CloseHandle` L4882/L4909 foi **intocado**). `vfs_resolve` = **reusada** (4 refs: def + CreateFileA/CreateFileW/GetFileAttributesA) — **nenhum helper duplicado**.

## 6. Infraestrutura de filesystem encontrada
- `pr_win32_set_fs_root` (L2492) + `pr_peproc_set_fs_root` (peproc L470); harness: `dbg_diag.c` L39 / `dbg_input.c` L63 = **`pr_peproc_set_fs_root(p, "build/win_fs")`** → raiz guest do VFS.
- `vfs_resolve(ctx, win, out, cap)`: **única** normalização Windows→host (reusada; §19 proibia segunda função) — aceita `\` e `/`, pula drive/UNC inicial, recusa qualquer segmento `..` (**proteção contra path traversal já existente**, §20 reutilizada), monta `fs_root/norm`.
- Guest só conhece caminhos guest (`"fixture.txt"`, `"."`); `fs_root` e o host path (`build/win_fs/fixture.txt`) existem **somente dentro do runtime** (§2/§18) — o PE nunca os vê nem importa POSIX.

## 7. Fixture utilizada (§7)
**Mecanismo já existente** (preferência 3): o próprio PE cria `fixture.txt` via `CreateFileA(CREATE_ALWAYS, GENERIC_WRITE)` + `WriteFile("g67ok")` + `CloseHandle` — exatamente o padrão validado de `hello_file.c` (G30). Sem alteração de harness; sem dependência de arquivo host arbitrário. Arquivos padrão do guest (`winos_file_test.txt`) existem, mas só após `hello_file` executar na mesma sessão — a fixture auto-criada torna cada execução **autocontida** (§24).

## 8. PE criado
`realpe/hello_getfileattributes.c` → `Tests/PorticoRuntimeTests/data/hello_getfileattributes.exe` (14848 B). Nenhum PE histórico alterado.

## 9. Compilação
```
x86_64-w64-mingw32-gcc -O2 -s realpe/hello_getfileattributes.c -o Tests/PorticoRuntimeTests/data/hello_getfileattributes.exe
```

## 10. Imports
`KERNEL32.dll` (tudo por nome, tudo já validado): `GetFileAttributesA` (hint **0262**, IAT `0x1400081a8`) + `CreateFileA` (00d4/`0x140008190`) + `WriteFile` (0641/`0x1400081f8`) + `CloseHandle` (0094/`0x140008188`) + `SetLastError` (0554/`0x1400081c8`) + `GetLastError` (0283/`0x1400081b0`) + CRT (`msvcrt.dll`). **Nenhuma API nova desnecessária** (as 3 extras = fixture §7).

## 11. Execução inicial (§21 — runtime intacto)
`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_getfileattributes.exe` → `prepare falhou — EXECUTION STOPPED — Unsupported Win32 API — Module: KERNEL32.dll — Function: GetFileAttributesA — Technical: API conhecida do módulo mas sem implementação` (Address: (—)). Depois da implementação: `exited=1 rc=67 steps=1 exec=450 — ExitProcess(msvcrt!exit)`.

## 12. Primeiro blocker
`Unsupported Win32 API | kernel32.dll | GetFileAttributesA` = lacuna real (API fora do catálogo) — único blocker; resolvido pela implementação mínima §19.

## 13. ABI/disassembly (evidência do PE real)
```
mov    $0xdead,%ecx                             ; SetLastError(0xDEAD)
mov    0x5a8c(%rip),%rdi # 0x1400081c8          ; rdi = IAT[SetLastError]
call   *%rdi
mov    0x5a63(%rip),%rbx # 0x1400081a8          ; rbx = IAT[GetFileAttributesA]
mov    %rsi,%rcx                  ; *** RCX = lpFileName = &"fixture.txt" (guest) ***
call   *%rbx                       ; *** chamada real ***
cmp    $0xffffffff,%eax            ; *** retorno DWORD via EAX == INVALID_FILE_ATTRIBUTES? ***
je     <fail 10>
cmp    $0x80,%eax                  ; *** EAX == FILE_ATTRIBUTE_NORMAL? ***
je     ok / mov $0xc,%eax          ; fail 12
mov    0x5a4c(%rip),%rbp # 0x1400081b0          ; rbp = IAT[GetLastError]
call   *%rbp
cmp    $0xdead,%eax                ; *** LastError DWORD preservado? ***
je     ok / mov $0xd,%eax          ; fail 13
mov    %rsi,%rcx
call   *%rbx                       ; 2ª chamada (determinismo)
```
Sucesso = `EAX != 0xFFFFFFFF` e `EAX == 0x80/0x10` (comparações de bits exatas); INVALID = `cmp $0xffffffff,%eax`; LastError = `cmp $0xdead,%eax`. Fixture: `call *0x5aaf(%rip) # 0x140008190` (CreateFileA: `mov %rsi,%rcx`, `movl $0x2` CREATE_ALWAYS, `movl $0x80` attr; `cmp $0xffffffffffffffff,%rax` = INVALID_HANDLE_VALUE) + WriteFile (`mov $0x5,%r8d`, `cmpl $0x5,0x4c(%rsp)`) + CloseHandle (`call *…# 0x140008188`).

## 14. Caminho guest
`"fixture.txt"`, `"definitely_missing_g67.bin"`, `NULL`, `"./fixture.txt"`, `".\\fixture.txt"`, `"."` — **somente caminhos guest**; nenhum `/mnt/...` ou path de máquina de desenvolvimento no PE (§2 verificado no fonte e no disassembly).

## 15. Resolução para filesystem
`guest "fixture.txt"` → string guest validada (`pr_win32_ptr` por byte) → `vfs_resolve` → `build/win_fs/fixture.txt` (host, só no runtime) → `stat()`. `"./fixture.txt"`/`".\fixture.txt"` → `build/win_fs/./fixture.txt` (POSIX `.` normaliza = mesmo arquivo). `"."` → `build/win_fs/.` = diretório.

## 16. Arquivo existente
`GetFileAttributesA("fixture.txt")` após criação: retorno `0x80` (**não**-INVALID, **sem** bit DIRECTORY) + LastError `0xDEAD` preservado (contratos 10/12/13) + 2ª chamada idêntica (contrato 17).

## 17. Atributos retornados
| Caso | Observado (contrato Portico) | Referência Windows (só referência) |
|---|---|---|
| arquivo regular | **`0x80`** = `FILE_ATTRIBUTE_NORMAL` (exato) | combinação de `FILE_ATTRIBUTE_*` (tipicamente NORMAL\|ARCHIVE) |
| diretório | **`0x10`** = `FILE_ATTRIBUTE_DIRECTORY` (exato) | `FILE_ATTRIBUTE_DIRECTORY` (+ outros bits possíveis) |
| erro | **`0xFFFFFFFF`** = `INVALID_FILE_ATTRIBUTES` | idem |

Menor mapeamento determinístico (§13): só os bits que o FS realmente conhece (regular vs diretório); **nenhuma flag inventada** (sem READONLY/HIDDEN/SYSTEM/ARCHIVE/etc.). Constantes usadas = as do `<windows.h>` do toolchain (valores 0x80/0x10/0xFFFFFFFF). Não declarado equivalente ao Windows.

## 18. Arquivo inexistente
`GetFileAttributesA("definitely_missing_g67.bin")` → **`0xFFFFFFFF`** (contrato 11 = sucesso indevido rejeitado) + LastError **2** observado (contrato 14).

## 19. LastError (§14)
| Caminho | Comportamento observado |
|---|---|
| sucesso (arquivo e diretório) | **preservado** (`0xDEAD` intacto) — não presumido; observado e testado (contrato 13) |
| arquivo inexistente | **`2`** = `W32_ERROR_FILE_NOT_FOUND` — **mapeamento já existente** de `f_CreateFileA` (L1851: `!exists`→2), reusado por coerência interna (não forçado para coincidir com Windows; coincide com `ERROR_FILE_NOT_FOUND` documentado) |
| NULL / ponteiro inválido | **`87`** = `ERROR_INVALID_PARAMETER` (padrão G65/G66) |
| traversal negado / sem fs_root | `5` (ACCESS_DENIED) / `3` (PATH_NOT_FOUND) — implementado, não exercitado pelo PE |

## 20. NULL (§15)
`GetFileAttributesA(NULL)` → `0xFFFFFFFF` + `last_error=87` + **sem crash** + processo continua até 67 (contrato 15) — seguro no dispatcher (soft error, padrão G65/G66).

## 21. Separadores/caminhos (§10/§11)
`"fixture.txt"` (nome simples) = `"./fixture.txt"` (relativo com `/`) = `".\\fixture.txt"` (relativo com `\`) → **todos produzem `0x80` idêntico** (contrato 16). Registrado: **`\` aceito; `/` aceito; ambos equivalentes** (conversão `\`→`/` no `vfs_resolve`). Caminhos absolutos (`C:\...`) apenas com o skip de drive existente (não testados = limitação §33).

## 22. Diretório (§12 — demonstrado)
`GetFileAttributesA(".")` → **`0x10` (`FILE_ATTRIBUTE_DIRECTORY` exato)** + LastError preservado — o diretório raiz do guest (`build/win_fs`) localizado via o próprio `vfs_resolve` (sem criar camada nova).

## 23. Segurança de path (§20)
Proteção contra `..`/path traversal **já existente** em `vfs_resolve` (recusa qualquer segmento `..`; comentário do código: "sandbox — NUNCA lê o iOS") — **reutilizada sem alteração**; fora do `fs_root` → negado (`5`). Nenhuma refatoração de segurança feita.

## 24. Correções realizadas (escopo mínimo)
1. `pr_win32.c` — `#include <sys/stat.h>` (metadata host interno; único include novo).
2. `pr_win32.c` — `f_GetFileAttributesA` (novo, ~25 linhas; reusa `vfs_resolve` + constantes `W32_ERROR_*` + padrão de string guest de `f_CreateFileA`).
3. `pr_win32.c` — catálogo: **1 linha nova** `IMPL(..., 4)`.
Mais: fonte do PE. **Sem** GetFileAttributesW/GetFileInformationByHandle/FindFirstFileA/FindNextFileA/GetFileSize/CreateFile novos/MultiByteToWideChar/WideCharToMultiByte; **sem** segunda normalização/segundo filesystem; **sem** harness alterado.

## 25. MD5
| Arquivo | antes | depois |
|---|---|---|
| `pr_win32.c` | `8090fb431a58da44f2a90e7531e2182b` (= depois de G66) | `8b7486834f88e83fa543a27305b7b191` |
| Combinado (todos .c/.h) | `e1f5e68157c852427c88761cc18e8237` (`build/g67_before.md5`) | `ae9f9e8e09510b8b437d3ff429e8968f` |

Mudanças = estritamente as 3 de §24 + novo `realpe/hello_getfileattributes.c`. (Nota: o combinado do fim do G66 foi `c6861da2…` porque incluía o artefato `build/si_layout.c` do probe — o `build/` não persiste entre turnos; `e1f5e681…` = mesma árvore de fontes pura.)

## 26. 20 execuções (§24)
**20/20** `exited=1 rc=67 log=48` (determinístico; log inclui linhas `[VFS] CreateFile` do fixture): fixture criado, existente reconhecido, inexistente rejeitado, LastError preservado nos dois caminhos, sem crash, sem `Unsupported`, sem variação.

## 27. C (§25)
**3398 verificações, 0 falhas** (baseline exato de G60; nenhum CHECK adicionado — o PE cobre o caminho real, §34).

## 28. Swift (§26)
**Executed 63 tests, with 0 failures** ✓ (intocados).

## 29. Warnings (§27)
**0** (todos os `.c` com `-Wall -Wextra`).

## 30. Analyzer (§28)
**7/0** (pré-existentes: pr_cpu.c:126, pr_cpu64.c:401, pr_gfx.c:98, pr_peproc.c:831, pr_win32.c:3611/3615, pr_hello.c:885; **0 novos**).

## 31. PE battery (§29)
**15/15** — logs 28/43/180/70/72/75/98/118/76/81/194/131/55/200/105 (rc=42/5). PEs históricos intactos (nenhum byte alterado).

## 32. Checkpoints (§30)
**G52–G66 = 15/15 verdes**: G52=52/31, G53=53/38, G54=54/30, G55=55/36, G56=56/32, G57=57/33, G58=58/39, G59=59/40, G60=60/45, G61=61/33, G62=62/33, G63=63/33, G64=64/29, G65=65/35, G66=66/37 (+G67=67/48 = 16/16). Checklist acumulada **38/38** (37 anteriores + **win32 file attributes (G67 GetFileAttributesA)** ✓).

## 33. Limitações
1. Atributos = só regular (`0x80`) / diretório (`0x10`) — sem READONLY/HIDDEN/SYSTEM/ARCHIVE/TEMPORARY/etc. (FS interno não possui esses metadados; não fabricados).
2. Caminhos absolutos/UNC: apenas o skip de drive/UNC do `vfs_resolve` (não exercitado pelo PE); sem curto/longo, sem `\\?\`.
3. Traversal (`..`) negado pelo mecanismo existente (não exercitado no PE — comportamento de segurança validado por código, não por PE).
4. LastError de erro = `2` por coerência com `f_CreateFileA`; sucesso preserva (≠ comportamentos possíveis do Windows em variações); não declarado equivalente.
5. `GetFileAttributesW`/`FindFirstFileA`/etc. fora do escopo; sem `GetFileInformationByHandle`.

## 34. Cobertura efetivamente demonstrada
Só `GetFileAttributesA` por import real resolvido pelo loader + chamada real com ABI x64 documentada, contra o FS virtual existente: arquivo guest existente (`fixture.txt` auto-criado pelo mecanismo validado) → `0x80` determinístico; inexistente (`definitely_missing_g67.bin`) → `0xFFFFFFFF`+2; NULL → `0xFFFFFFFF`+87; relativos `./` e `.\` idênticos; diretório `"."` → `0x10`; determinismo entre chamadas; caminho host nunca exposto ao PE. Nada além disso.

## 35. Próximo candidato (só indicação — não executar)
`kernel32!GetFileAttributesW` (família W — exige UTF-16, fora do escopo atual) ou `kernel32!DeleteFileA` (bytes, 1 argumento, mesma infra `vfs_resolve`). NÃO implementados aqui.
