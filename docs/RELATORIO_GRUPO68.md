# RELATÓRIO GRUPO 68 — kernel32!DeleteFileA validada por PE x64 real

## 1. Status
**G68 CONCLUÍDO COM SUCESSO** — `DeleteFileA` validada por PE x64 real (`hello_deletefile.exe`, 14848 bytes) integrada ao **VFS existente** (`vfs_resolve`/`fs_root`; nenhum segundo filesystem, nenhuma segunda normalização): import **por nome** (hint **0126**, IAT `0x1400081a8`); ABI **RCX = lpFileName** e **BOOL via EAX** comprovadas por disassembly; sucesso → `TRUE` + arquivo removido (confirmado por `GetFileAttributesA` = INVALID); inexistente → `FALSE` + **2**; NULL → `FALSE` + **87**; diretório → `FALSE` + **5**; traversal `..` → **bloqueado pelo mecanismo existente** (`FALSE` + 5); separadores `/` e `\` idênticos; **20/20 rc=68** (log=55); C **3398/0**; Swift **63/63**; warnings 0; analyzer **7** (pré-existentes); battery 15/15; **G52–G67 16/16 verdes**; FS limpo após 20 runs (só fixtures pré-existentes permanecem).

## 2. API validada
`kernel32.dll!DeleteFileA(LPCSTR lpFileName)` → `BOOL` (`TRUE=1` / `FALSE=0`). `DeleteFileW` e demais (RemoveDirectory/MoveFile/CopyFile/FindFirstFile/FindNextFile/GetFileInformationByHandle) = **0 implementações** (fora do escopo).

## 3. Inventário read-only (§1 — antes de qualquer alteração)
Buscas por `DeleteFileA|f_DeleteFileA|DeleteFileW|f_DeleteFileW|DeleteFile|vfs_resolve|fs_root|pr_win32_set_fs_root|pr_win32_normalize|CreateFileA|CloseHandle|FILE_ATTRIBUTE|GetFileAttributesA|W32_ERROR_*` em Sources/Tests/realpe/tools + catálogo:
1. handler: **NÃO existe**; 2. TODO: **NÃO existe**; 3. registro no catálogo: **NENHUM**; 4. `stdcall_bytes`: N/A → criado **4** (1 arg × 4; padrão `GetFileAttributesA=4`); 5. VFS reutilizável: `vfs_resolve` (L1785), `W32_ERROR_FILE_NOT_FOUND=2` (L1764), `W32_ERROR_PATH_NOT_FOUND=3`, `W32_ERROR_ACCESS_DENIED=5`, `W32_ERROR_INVALID_PARAMETER=87` (L55), `sys/stat.h` (G67), `unistd.h` (`unlink`); 6. `CreateFileA` resolve caminhos = string guest byte a byte (`pr_win32_ptr` por posição) + `vfs_resolve` (L1822); 7. `CloseHandle` = `f_CloseHandle` (L2336): file slots → `fclose`, mutex/thread slots, etc.; 8. erros de filesystem = convenção `W32_ERROR_*` (sucesso preserva LastError; erro seta `last_error` + retorno benigno); 9. infra de remoção de arquivos: **NENHUMA** (0 refs a delete/unlink); 10. sandbox a preservar: `vfs_resolve` recusa `..` e fora-de-`fs_root` ("sandbox — NUNCA lê o iOS").

## 4. Handler
**Criado**: `f_DeleteFileA` (antes de `f_GetProcAddress`): `n<1`→`PR_ERR_INVALID`; `a[0]==0`/ponteiro inválido → `FALSE`+87; string guest byte a byte (padrão `f_CreateFileA`/`f_GetFileAttributesA`, sem `strlen` cru); `vfs_resolve` (**reusada**); `stat()`; diretório → `FALSE`+5 (**não** remove diretórios); `unlink(host)` → sucesso `TRUE` (LastError preservado) / falha `FALSE`+5; não-existe → `FALSE`+2 (convenção existente).

## 5. Catálogo
**1 registro novo** (0 TODO substituído): `IMPL("kernel32.dll", "DeleteFileA", f_DeleteFileA, 4)` após `GetFileAttributesA`. TODOs: 31 antes = 31 depois (**0 removidos**). Sem duplicação; `vfs_resolve` = **reusada** (5 refs: def + CreateFileA + CreateFileW + GetFileAttributesA + DeleteFileA) — nenhum helper paralelo (`delete_resolve` etc. **não criados**).

## 6. Infraestrutura VFS reutilizada
`vfs_resolve` (única normalização Windows→host: `\`→`/`, skip drive/UNC, **recusa `..`**, prefixa `fs_root/`); `fs_root="build/win_fs"` (fixado pelo harness — **não alterado**); `pr_win32_ptr` (validação de string guest); mapeamento `W32_ERROR_*` idêntico ao de `f_CreateFileA`/`f_GetFileAttributesA`. Isolamento guest/host preservado integralmente — o PE só conhece caminhos guest (nenhum `/mnt/...` ou `build/win_fs/...` no fonte nem no disassembly).

## 7. Fixture
Autocontida no PE (padrão G67, §5): `delete_g68.txt` e `delete_g68b.txt` criados via `CreateFileA(CREATE_ALWAYS)+WriteFile("g68ok")+CloseHandle` (mecanismos já validados). Após 20 execuções: **ambos removidos** (fixtures limpas); `build/win_fs` contém apenas `fixture.txt` pré-existente do G67 = "sem alteração inesperada do filesystem fora da fixture" ✓.

## 8. PE criado
`realpe/hello_deletefile.c` → `Tests/PorticoRuntimeTests/data/hello_deletefile.exe` (14848 B). Nenhum PE histórico alterado.

## 9. Compilação
```
x86_64-w64-mingw32-gcc -O2 -s realpe/hello_deletefile.c -o Tests/PorticoRuntimeTests/data/hello_deletefile.exe
```

## 10. Imports (mínimos, tudo já validado)
`KERNEL32.dll` por nome: `DeleteFileA` (hint **0126**, IAT `0x1400081a8`) + `SetLastError` (0554) + `GetLastError` (0283) + fixture `CreateFileA` (00d4)/`WriteFile` (0641)/`CloseHandle` (0094) + `GetFileAttributesA` (0262, G67 — confirmação de existência/remoção) + CRT (`msvcrt.dll`). **Nenhuma API nova**.

## 11. Execução inicial com runtime intacto (§11)
`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_deletefile.exe` → `prepare falhou — EXECUTION STOPPED — Unsupported Win32 API — Module: KERNEL32.dll — Function: DeleteFileA — Technical: API conhecida do módulo mas sem implementação` (Address: (—)). Depois da implementação: `exited=1 rc=68 steps=1 exec=531 — ExitProcess(msvcrt!exit)`.

## 12. Blocker encontrado
`Unsupported Win32 API | kernel32.dll | DeleteFileA` = lacuna real (API fora do catálogo) — único blocker; resolvido pela implementação mínima §11.

## 13. ABI/disassembly (§7 — evidência do PE real)
```
mov    $0xdead,%ecx                             ; SetLastError(0xDEAD)
mov    0x5a6a(%rip),%r15 # 0x1400081d8          ; r15 = IAT[SetLastError]
call   *%r15
mov    0x5a30(%rip),%rax # 0x1400081a8          ; rax = IAT[DeleteFileA]
mov    %rbx,%rcx                  ; *** RCX = lpFileName = &"delete_g68.txt" (guest) ***
call   *%rax                       ; *** chamada real ***
mov    %eax,%edx                   ; *** BOOL consumido via EAX ***
mov    $0xb,%eax                   ; código de falha 11
sub    $0x1,%edx                   ; *** comparação BOOL: retorno == TRUE(1)? ***
jne    <fail 11>
mov    0x5a2b(%rip),%rax # 0x1400081c0          ; rax = IAT[GetLastError]
call   *%rax
cmp    $0xdead,%eax                ; *** LastError DWORD preservado? ***
je     ok / mov $0xe,%eax          ; fail 14
mov    %rbx,%rcx
call   *%r13                       ; GetFileAttributesA (confirma remoção)
add    $0x1,%eax                   ; *** EAX+1==0? = INVALID_FILE_ATTRIBUTES ***
je     ok / mov $0xc,%eax          ; fail 12
```
Sem tratamento especial do retorno (EAX direto); comparações BOOL = `sub $0x1,%edx; jne` (TRUE) e `!= FALSE` (`test`/`jz` nos casos de erro); LastError = `cmp $0xdead,%eax`.

## 14. Caminho guest
`"delete_g68.txt"`, `"definitely_missing_g68.bin"`, `NULL`, `".\\delete_g68b.txt"`, `"."`, `"..\\g68_escape.txt"` — somente caminhos guest (§3 verificado no fonte e no disassembly).

## 15. Resolução VFS
guest string validada byte a byte → `vfs_resolve` → `build/win_fs/<norm>` (host, só no runtime) → `stat`/`unlink`. `".\\delete_g68b.txt"` → `./delete_g68b.txt` → mesmo arquivo que `"delete_g68b.txt"` ✓. `"..\\g68_escape.txt"` → **recusado por `vfs_resolve`** antes de qualquer operação.

## 16. Comportamento de sucesso
`DeleteFileA("delete_g68.txt")` → **`TRUE`** (contrato 11); `GetFileAttributesA` posterior → **`INVALID_FILE_ATTRIBUTES`** (contrato 12 = arquivo deixou de existir no VFS); LastError **`0xDEAD` preservado** (contrato 14; observado, não presumido). Segundo ciclo `"."\\` idêntico (contrato 16).

## 17. Comportamento de arquivo inexistente
`DeleteFileA("definitely_missing_g68.bin")` → **`FALSE`** (contrato 13) + LastError **`2`** = `W32_ERROR_FILE_NOT_FOUND` — **convenção já existente** do runtime (mesma de `f_CreateFileA`/`f_GetFileAttributesA`), não uma nova inventada.

## 18. Comportamento de NULL
`DeleteFileA(NULL)` → **`FALSE`** + LastError **`87`** (`W32_ERROR_INVALID_PARAMETER` = comportamento seguro dos grupos recentes G65–G67) — validado explicitamente (contrato 15); sem crash; processo continua.

## 19. LastError (§10 — contrato atual do Portico, sem equivalência Windows)
| Caminho | Observado |
|---|---|
| sucesso | **preservado** (`0xDEAD` intacto) |
| inexistente | **2** (convenção existente) |
| NULL/ponteiro inválido | **87** (padrão G65–G67) |
| diretório | **5** (`ACCESS_DENIED`) |
| traversal `..` | **5** (negado por `vfs_resolve`) |

## 20. Comportamento de diretório (§8)
`DeleteFileA(".")` → **`FALSE` + `5`** — o handler **não** trata diretório como arquivo (`S_ISDIR` → recusa) e **nenhuma** API de remoção de diretório foi implementada. Observado e documentado; referência Windows (só referência): `DeleteFile` em diretório falha com `ERROR_ACCESS_DENIED` — coerente, não declarado equivalente.

## 21. Comportamento de `..` (§9 — testado)
`DeleteFileA("..\\g68_escape.txt")` → **`FALSE` + `5`**: o mecanismo **existente** de `vfs_resolve` (bloqueio de `..`) impede escapar de `fs_root` — política preservada sem criar nova proteção (contrato 18 = traversal não bloqueado seria falha).

## 22. Correções realizadas (escopo mínimo)
1. `pr_win32.c` — `f_DeleteFileA` (novo, ~25 linhas; reusa `vfs_resolve` + `W32_ERROR_*` + padrão de string guest).
2. `pr_win32.c` — catálogo: **1 linha nova** `IMPL(..., 4)`.
Mais: fonte do PE. **Sem** DeleteFileW/RemoveDirectory/MoveFile/CopyFile/FindFirstFile/FindNextFile/GetFileInformationByHandle/conversão ANSI-Unicode; **sem** segunda normalização/segundo filesystem; **sem** harness/PEs históricos alterados; **sem** UI/gráficos tocados.

## 23. MD5 antes/depois
| Arquivo | antes | depois |
|---|---|---|
| `pr_win32.c` | `8b7486834f88e83fa543a27305b7b191` (= depois de G67) | `27152efdfbf69ee7c111b71c9d9ab037` |
| Combinado (todos .c/.h) | `ae9f9e8e09510b8b437d3ff429e8968f` (`build/g68_before.md5`) | `340e01b6ac2438f8751fc10f54a9fb2f` |

Mudanças = estritamente as 2 de §22 + novo `realpe/hello_deletefile.c`. Sem correções incidentais.

## 24. 20 execuções (§13)
**20/20** `exited=1 rc=68 log=55` (determinístico; `steps=1 exec=531` no diagnóstico): fixture criada/removida, inexistente rejeitado, NULL tratado, diretório recusado, traversal bloqueado; **sem crash, sem `Unsupported`, sem variação**; FS pós-20 = apenas `fixture.txt` pré-existente (fixtures G68 limpas).

## 25. C (§14)
**3398 verificações, 0 falhas** (baseline exato; nenhum CHECK adicionado — o PE cobre o caminho real, §32).

## 26. Swift (§14)
**Executed 63 tests, with 0 failures** ✓ (intocados).

## 27. Warnings (§14)
**0** (todos os `.c` com `-Wall -Wextra`).

## 28. Analyzer (§14)
**7/0** — 7 pré-existentes (pr_cpu.c:126, pr_cpu64.c:401, pr_gfx.c:98, pr_peproc.c:831, pr_win32.c:3611/3615, pr_hello.c:885); **0 novos**.

## 29. PE battery (§14)
**15/15** — logs 28/43/180/70/72/75/98/118/76/81/194/131/55/200/105 (rc=42/5). Sem regressão silenciosa; PEs históricos intactos.

## 30. Checkpoints (§15)
**G52–G67 = 16/16 verdes** (G52=52/31 … G64=64/29, G65=65/35, G66=66/37, G67=67/48) + **G68=68/55** ⇒ **G52–G68 = 17/17 verdes**. Checklist acumulada **39/39** (38 anteriores + **win32 delete file (G68 DeleteFileA)** ✓; nenhuma mudança de contagem anterior).

## 31. Limitações
1. Delete de arquivo **aberto** por handle não testado (POSIX `unlink` remove; Windows falharia com sharing violation) — semântica não equivalente, não demonstrada.
2. Atributos/readonly: arquivo somente-leitura não é um estado que o VFS represente; `unlink` direto.
3. Sucesso preserva LastError (contrato Portico); Windows não documenta LastError em sucesso de `DeleteFileA` — sem equivalência declarada.
4. `RemoveDirectoryA`/`MoveFileA`/`CopyFileA`/família W/`FindFirstFileA` = fora do escopo.
5. Traversal validado pelo PE; demais políticas de sandbox (`..` interno, drive/UNC) exercitadas só pelo mecanismo existente.

## 32. Cobertura efetivamente demonstrada
Só `DeleteFileA` por import real resolvido pelo loader + chamada real com ABI x64 documentada, sobre o VFS existente: existente → `TRUE` + remoção confirmada por `GetFileAttributesA` + LastError preservado; inexistente → `FALSE`+2; NULL → `FALSE`+87; diretório `"."` → `FALSE`+5; traversal `"..\\"` → bloqueado (`FALSE`+5) pela proteção existente; variante `"."\\` idêntica; 20/20 determinístico com FS limpo. Nada além disso.

## 33. Próximo candidato (somente indicação — NÃO executar; baseado no estado real)
Estado real dos TODOs kernel32 restantes: `ReadFileEx`, `SetFilePointer` (L4961), `CloseHandle` (duplicado), `TlsAlloc` (L4963), `MultiByteToWideChar`, `WideCharToMultiByte`. **G69 sugerido: `kernel32!SetFilePointer`** (preenche TODO real; complementa a família VFS já completa com Create/Read/Write/Size/Close/GetAttr/Delete; observação: o inventário de G69 deve primeiro verificar como `hello_file_seek` faz seek hoje, já que esse PE funciona sem essa API catalogada). Alternativa: `kernel32!TlsAlloc` (TODO real L4963; família TLS com `TlsGetValue` já validado em G55).
