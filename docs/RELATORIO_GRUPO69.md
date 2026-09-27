# RELATÓRIO GRUPO 69 — kernel32!SetFilePointer validada por PE x64 real (contrato completo)

## 1. Status
**G69 CONCLUÍDO COM SUCESSO** — descoberta arquitetural + validação completa. **`SetFilePointer` NÃO era lacuna**: `f_SetFilePointer` existe desde o G32 (catálogo `IMPL(..., 16)` L4940); o `TODO` L4961 é **duplicata morta** (a suposição do G68 estava errada — inventário completo do G69 a corrigiu). `hello_file_seek` (G32) importa e executa `SetFilePointer` = **evidência principal** (rc=42/33 revalidada). Novo PE `hello_setfilepointer.exe` (15360 bytes) cobre o **contrato completo**: FILE_BEGIN/CURRENT/END, negativos, efeito real via ReadFile (`'D'`), `lpDistanceToMoveHigh`, handle inválido (6), método inválido (87), abaixo-de-zero (5), LastError preservado em sucesso, limpeza da fixture. **Runtime 100% intocado** (MD5 `pr_win32.c` antes==depois). **20/20 rc=69** (log=55); C **3398/0**; Swift **63/63**; warnings 0; analyzer **7**; battery 15/15; **G52–G68 17/17 verdes**.

## 2. API validada
`kernel32.dll!SetFilePointer(HANDLE, LONG, PLONG, DWORD)` → `DWORD` (nova posição; `0xFFFFFFFF` + LastError = erro). `SetFilePointerEx` = **0 implementações** (fora do escopo).

## 3. Inventário read-only (§1 — antes de qualquer alteração)
Buscas completas por `SetFilePointer|f_SetFilePointer|SetFilePointerEx|seek|fseek|ftell|SEEK_*|CreateFileA|ReadFile|WriteFile|GetFileSize|CloseHandle|W32_FILE_BASE|ctx->files|FILE*|fseek(|ftell(|fseeko(|off_t` + catálogo kernel32 + TODOs:
1. handler: **`f_SetFilePointer` JÁ EXISTE** (L2396–2417); 2. TODO: **existe** `TODO("kernel32.dll","SetFilePointer","E/S de arquivos")` L4961 = **duplicata morta**; 3. registro: **`IMPL("kernel32.dll", "SetFilePointer", f_SetFilePointer, 16)` L4940** — 4 args × 4 = **16** já correto; 4. infra de seek: `fseek`/`ftell` host sobre `FILE*` (`ctx->files[8]`, handles `W32_FILE_BASE|k+1`); `W32_SEEK_BEGIN/CUR/END` (L1770–1772); `W32_INVALID_SET_FILE_PTR`; `W32_ERROR_INVALID_HANDLE=6`, `…_INVALID_PARAMETER=87`, `…_ACCESS_DENIED=5`; 5. `SetFilePointerEx` = 0; unidade `test_vfs.c` L73/89 já testa BEGIN/END via `pr_win32_call`; `test_peproc.c` L128 menciona a família.

## 4. Descoberta sobre `hello_file_seek` (§2/§6 — pergunta arquitetural)
**Por que funciona?** Porque **importa `SetFilePointer` por nome** (fonte declara: "Imports KERNEL32: CreateFileA, WriteFile, SetFilePointer, ReadFile, CloseHandle") e o loader resolve em `f_SetFilePointer` — **não** usa CRT/msvcrt para seek, **não** há adaptação indireta: é a API Win32 implementada. Fluxo G32: WriteFile(16)→SetFilePointer(8, FILE_BEGIN)→ReadFile(4)="CCCC". Reexecutado neste grupo: **rc=42 log=33** = evidência principal viva. **`SetFilePointer` nunca esteve ausente do caminho executado** — o TODO paralelo nunca é consultado (o lookup encontra o IMPL L4940 primeiro, como em `CloseHandle` L4909/L4962).

## 5. Infraestrutura interna de seek encontrada (§3/§15)
**Mecanismo único (reusado, não duplicado)**: o cursor é a **posição do stream do `FILE*` do host** em `ctx->files[k]` — o mesmo `FILE*` de `f_WriteFile` (`fwrite`+`fflush` L736–737) e `f_ReadFile` (`fread` L788): um único cursor por handle, sem estado de posição paralelo, sem segunda tabela/estrutura/FILE*. `fseek(f, dist, whence)` move; `ftell(f)` reporta.

## 6. Handler
**Existente, INTACTO** (`f_SetFilePointer` L2396–2417): valida `n`; handle via `w32_is_file_handle`/`w32_file_idx` (inválido → 6 + `0xFFFFFFFF`); `dist = (long)(int32_t)(uint32_t)a[1]` (**LONG assinado** via EDX); `lpDistanceToMoveHigh` não-NULL = **entrada** (DWORD alto do deslocamento, `dist |= hi<<32`); `dwMoveMethod` → `SEEK_SET/CUR/END` com **`default:` → 87** (já existia); `fseek` falha → **5** + `0xFFFFFFFF`; retorno `(uint32_t)ftell(f)` (DWORD = bits baixos da nova posição).

## 7. Catálogo
**INTOCADO**: `IMPL(..., 16)` L4940 (único vencedor do lookup) + `TODO` L4961 (duplicata pré-existente, mantida como está — mesma situação do par `CloseHandle`; **0 TODO removido**, **0 registro adicionado**, **sem duplicação nova**).

## 8. PE (§7 — novo, necessário)
`hello_file_seek` cobria só FILE_BEGIN+ReadFile; o contrato G69 exige CURRENT/END/negativos/inválidos/limpeza ⇒ **`realpe/hello_setfilepointer.c` → `Tests/PorticoRuntimeTests/data/hello_setfilepointer.exe` (15360 B)**. Nenhum PE histórico alterado.

## 9. Compilação
```
x86_64-w64-mingw32-gcc -O2 -s realpe/hello_setfilepointer.c -o Tests/PorticoRuntimeTests/data/hello_setfilepointer.exe
```

## 10. Imports (tudo já validado)
`KERNEL32.dll` por nome: `SetFilePointer` (hint **0544**, IAT `0x1400081f0`), `CreateFileA` (00d4), `WriteFile` (0641), `ReadFile` (04ac), `CloseHandle` (0094), `DeleteFileA` (0126), `GetFileAttributesA` (0262), `SetLastError` (0554), `GetLastError` (0283) + CRT. **Nenhuma API nova**.

## 11. Execução com runtime intacto (§11)
`./build/dbg_diag …/hello_setfilepointer.exe` → **`exited=1 rc=69 steps=1 exec=558`** — **PASSOU COMPLETO sem nenhuma alteração** (0 mudanças até este ponto): nenhum `Unsupported Win32 API`; todos os contratos (10–19) satisfeitos pelo handler existente.

## 12. Blocker
**NENHUM** — não há lacuna Win32 aqui. O "blocker" do G68 era o TODO morto (artefato de catálogo), não um gap de implementação. Descoberta registrada em §3/§4.

## 13. ABI/disassembly (§14 — evidência do PE real)
```
xor    %r8d,%r8d                 ; *** R8 = lpDistanceToMoveHigh = NULL ***
mov    $0x3,%edx                 ; *** EDX = lDistanceToMove = 3 (LONG) ***
mov    %rbx,%rcx                 ; *** RCX = hFile ***
call   *%rdi                     ; *** SetFilePointer (IAT 0x1400081f0) ***
cmp    $0x3,%eax                 ; *** retorno DWORD via EAX == 3? ***   (FILE_BEGIN)
mov    $0x2,%edx / mov $0x1,%r9d ; +2, FILE_CURRENT(1) → cmp $0x5,%edx (mov %eax,%edx)
mov    $0xfffffffe,%edx          ; *** LONG negativo −2 em EDX (32-bit) ***
mov    $0x1,%r9d                 ; FILE_CURRENT → cmp $0x3,%eax
xor    %edx,%edx / mov $0x2,%r9d ; 0, FILE_END(2) → cmp $0xa,%eax (posição 10)
mov    $0xfffffffc,%edx          ; −4 → cmp $0x6,%eax (posição 6)
```
Argumentos: RCX=hFile, EDX=LONG assinado (complemento de 2 em 32 bits), R8=lpHigh, R9D=method (0/1/2). Retorno consumido como **DWORD em EAX** (`cmp $imm,%eax` / `mov %eax,%edx; cmp`). Tratamento especial do retorno: nenhum (EAX direto).

## 14. FILE_BEGIN (§8)
`SetFilePointer(h, 0, NULL, FILE_BEGIN) == 0` e `SetFilePointer(h, 3, NULL, FILE_BEGIN) == 3` ✓ (contrato 11). Efeito não confiado em API interna — §17 prova o cursor.

## 15. FILE_CURRENT (§9)
`SetFilePointer(h, 2, NULL, FILE_CURRENT) == 5` (partindo de 3) ✓ (contrato 12). Deslocamento **negativo suportado**: `SetFilePointer(h, -2, NULL, FILE_CURRENT) == 3` ✓ (contrato 18).

## 16. FILE_END (§10)
`SetFilePointer(h, 0, NULL, FILE_END) == 10` (arquivo de 10 bytes) ✓ (contrato 13); relativo ao final suportado: `SetFilePointer(h, -4, NULL, FILE_END) == 6` ✓.

## 17. Efeito demonstrado por ReadFile (§11)
`WriteFile("ABCDEFGHIJ")` → `SetFilePointer(h, 3, NULL, FILE_BEGIN)` → `ReadFile(h, &b, 1, …)`: `got==1` e **`b == 'D'`** ✓ (contrato 14) = cadeia real `SetFilePointer → cursor (FILE* do host) → ReadFile` pelo runtime.

## 18. Handle inválido (§12)
`SetFilePointer(INVALID_HANDLE_VALUE, …)` → **`0xFFFFFFFF`** + LastError **`6`** (`W32_ERROR_INVALID_HANDLE` — código já empregado pelo runtime; nenhum novo inventado) ✓ (contrato 15).

## 19. lpDistanceToMoveHigh (§13/§5)
**NULL** (caso principal): exercitado em todas as chamadas acima ✓. **Não-NULL**: `LONG hi = 0; SetFilePointer(h, 0, &hi, FILE_BEGIN) == 0` aceito ✓ (contrato 19). **Contrato observado do Portico**: o ponteiro é tratado como **entrada** (DWORD alto do deslocamento, `dist |= hi<<32`); **não há escrita de saída** da parte alta da nova posição (diferença vs Windows, que sobrescreve `*lpHigh` com a parte alta da nova posição). Exercitado só com entrada 0 (seguro; ver §32.1). Detecção de erro com `lpHigh` usado segue §5: `0xFFFFFFFF` + LastError ≠ 0 (a semântica de erro está centralizada no LastError do runtime).

## 20. LastError (§16 — contrato observado do Portico)
| Caminho | Observado |
|---|---|
| sucesso | **preservado** (`0xDEAD` intacto) |
| handle inválido | **6** |
| método inválido | **87** |
| fseek falha (abaixo de zero) | **5** (`W32_ERROR_ACCESS_DENIED`) |

Sem equivalência Windows declarada (ex.: Windows usaria `ERROR_NEGATIVE_SEEK=131` para abaixo-de-zero = diferença documentada §32).

## 21. Método inválido (§17)
`SetFilePointer(h, 0, NULL, 99)` → `default:` do switch → **`0xFFFFFFFF` + 87** ✓ (contrato 17); sem crash, sem corrupção de cursor/arquivo, sem acesso fora do VFS.

## 22. Negative seek (§18)
Negativo válido: −2 de 5 → **3** ✓. Abaixo de zero (−7 de 5 → alvo −2): **`0xFFFFFFFF` + 5** (falha do `fseek` host mapeada pelo comportamento já existente) — falha testada e documentada; nenhuma outra API alterada para forçar comportamento.

## 23. Correções realizadas
**NENHUMA no runtime** (§3: reutilizar; nada faltava para o contrato testável). Zero handlers, zero catálogo, zero helpers, zero harness alterados. Adicionado somente o PE de validação. Observação (não alterada): o check `n < 3` do handler lê `a[3]` (4º arg) — latente e inócuo na prática (catálogo fixa `stdcall_bytes=16`; chamadas reais sempre trazem n=4), registrado em §32.

## 24. MD5 (§23)
| Artefato | antes | depois |
|---|---|---|
| `pr_win32.c` | `27152efdfbf69ee7c111b71c9d9ab037` | `27152efdfbf69ee7c111b71c9d9ab037` (**idêntico**) |
| Combinado (todos .c/.h) | `340e01b6ac2438f8751fc10f54a9fb2f` | `02818b338d9f811c520cec1fb6dc25c3` (mudou **só** pelo novo `realpe/hello_setfilepointer.c`) |

Nenhum PE histórico alterado; nenhum harness alterado; nenhuma API anterior quebrada (§31); nenhuma mudança não relacionada.

## 25. 20 execuções (§21)
**20/20** `exited=1 rc=69 log=55` (determinístico; `steps=1 exec=558` no diagnóstico); estabilidade total; **estado final do FS = limpo** (`seek_g69.txt` removido pela própria limpeza do PE via `DeleteFileA` validado; sem acúmulo entre execuções).

## 26. C (§20)
**3398 verificações, 0 falhas** (inclui os CHECKs pré-existentes de `SetFilePointer` em `test_vfs.c`; nenhum adicionado).

## 27. Swift (§20)
**Executed 63 tests, with 0 failures** ✓ (intocados).

## 28. Warnings (§20)
**0** (`-Wall -Wextra` em todos os `.c`).

## 29. Analyzer (§20)
**7/0** — pré-existentes (pr_cpu.c:126, pr_cpu64.c:401, pr_gfx.c:98, pr_peproc.c:831, pr_win32.c:3611/3615, pr_hello.c:885); **0 novos**.

## 30. PE battery (§20)
**15/15** — logs 28/43/180/70/72/75/98/118/76/81/194/131/55/200/105 (rc=42/5).

## 31. Checkpoints (§22)
**G52–G68 = 17/17 verdes** (G52=52/31 … G67=67/48, G68=68/55) + **G69=69/55** ⇒ **G52–G69 = 18/18 verdes**; evidência G32 `hello_file_seek` revalidada (42/33). Checklist acumulada **40/40** (39 anteriores + **win32 seek (G69 SetFilePointer)** ✓; contagens anteriores inalteradas).

## 32. Limitações (§24 — obrigatórias)
1. **`lpDistanceToMoveHigh`**: só como **entrada** (DWORD alto); **sem escrita de saída** da parte alta da nova posição (≠ Windows); caso não-NULL exercitado só com entrada 0 (sentinel não-zero provocaria deslocamento ~2³²<<32 = não-demostrável com segurança); **deslocamentos acima de 32 bits não demonstrados**.
2. **`SetFilePointerEx` não implementado** (fora do escopo).
3. Retorno > 4 GiB: `ftell` truncado a DWORD — posição real ≥ 2³² indistinguível; `0xFFFFFFFF` é posição válida possível (ambiguidade da própria API Win32; §5).
4. **Seek além do EOF não exercitado** (fseek host aceitaria; comportamento de leitura posterior não demonstrado).
5. Múltiplos handles para o mesmo arquivo, concorrência e sharing: **não demonstrados** (VFS não tem semântica de sharing).
6. Abaixo-de-zero = `0xFFFFFFFF`+**5** (contrato observado); Windows = `ERROR_NEGATIVE_SEEK` (131) — diferença `fseek` host vs semântica Win32 declarada.
7. Check latente `n < 3` vs 4 argumentos (§23) — inócuo com catálogo `16`, não alterado.
8. Nada disto = compatibilidade geral Windows/Winlar/GTA V/MX Bikes.

## 33. Cobertura efetivamente demonstrada
`SetFilePointer` (handler G32) com import real por nome + ABI RCX/EDX/R8/R9D documentada: FILE_BEGIN (0, 3), FILE_CURRENT (+2→5, −2→3), FILE_END (0→10, −4→6), efeito real no cursor provado por `ReadFile`→`'D'`, `lpHigh` NULL (dominante) e não-NULL (entrada 0), handle inválido (6), método inválido (87), abaixo-de-zero (5), LastError preservado em sucesso, fixture limpa. Nada além disso.

## 34. Próximo candidato (somente indicação — NÃO executar; baseado no estado real)
Estado real dos TODOs kernel32: `ReadFileEx`, `SetFilePointer`/`CloseHandle` (duplicatas mortas), **`TlsAlloc`** (L4963), `MultiByteToWideChar`, `WideCharToMultiByte`. **G70 sugerido: `kernel32!TlsAlloc`** (lacuna real catalogada; completa a família TLS com `TlsGetValue` já validado em G55; 0 argumentos → `DWORD`; `TlsSetValue`/`TlsFree` como continuação natural da família). Alternativa: `kernel32!ReadFileEx` (E/S assíncrona — complexidade maior).
