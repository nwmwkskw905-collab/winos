# RELATÓRIO DA ETAPA — GRUPO 58

## Status

CONCLUÍDO

## API validada

kernel32!lstrlenA

## Inventário

1. **Localização do handler**: `f_lstrlenA`, `Sources/PorticoRuntime/src/pr_win32.c` L818.
2. **Registro no catálogo**: `IMPL("kernel32.dll", "lstrlenA", f_lstrlenA, 4)` (L4789) — `stdcall_bytes = 4` → **1 argumento** (convenção do catálogo).
3. **Número de argumentos**: 1 (`lpString`).
4. **Tipo de retorno interno**: `uint64_t` (comprimento `size_t` zero-extended); o PE interpreta os 32 bits inferiores como `int` assinado.
5. **Tratamento de `NULL`**: **caminho explícito** — `if (n < 1 || a[0] == 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }` → `lstrlenA(NULL)` retorna **0** e define `last_error = 87` (`W32_ERROR_INVALID_PARAMETER = 87u`, pr_win32.c L54).
6. **Algoritmo de medição**: varredura limitada byte a byte com validação de região — `while (pr_win32_ptr(ctx, a[0] + len, 1) && p[len]) len++;` — **para no primeiro NUL**; somente leitura (sem escrita); ponteiro inválido → `last_error = 87` + retorno 0.
7. **Testes unitários existentes**: `Tests/PorticoRuntimeTests/test_win32.c:75` (C) e `Tests/PorticoCoreTests/CompatLayerTests.swift:348` (Swift).
8. **PEs anteriores que importam `lstrlenA`**: **nenhum** (varredura por `objdump -p` sobre os 43 PEs: 0 ocorrências; menções em `hello_getcommandline*.c` são apenas comentários do contrato).
9. **Comparação com `f_lstrcpynA` (G52, L842)**: mesma convenção `f(ctx, args, nargs, st)`; argumentos crus em `args[]` (`args[0]` = ponteiro de 64 bits em RCX); retorno `uint64_t`; mesma validação `pr_win32_ptr`; mesmo código de erro `W32_ERROR_INVALID_PARAMETER`. `f_lstrcpynA` **não foi alterada**.
10. **Código compartilhado**: `f_GetModuleHandleA` (L2481) **chama `f_lstrlenA` internamente** (L2489) como verificação de string vazia/ponteiro inválido — preservado intacto.

## Implementação encontrada

```c
static uint64_t f_lstrlenA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1 || a[0] == 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    /* valida região com varredura limitada */
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t len = 0;
    while (pr_win32_ptr(ctx, a[0] + len, 1) && p[len]) len++;
    return len;
}
```

## Tratamento de NULL

O handler possui **caminho explícito** para `NULL` (`a[0] == 0`): retorna **0** e define `ctx->last_error = 87` (`ERROR_INVALID_PARAMETER`). Como o comportamento está claro no código existente, o §11 autoriza investigá-lo e validá-lo — feito nas assertions 16–17 e tratado como **observação do Portico**, não como afirmação de equivalência universal com Windows (em Windows real o comportamento de `lstrlenA(NULL)` não é garantido por contrato documentado).

## PE

`hello_lstrlen.exe` — 14.336 bytes. Origem: `realpe/hello_lstrlen.c` (criado neste grupo; nenhum PE anterior alterado). O PE usa apenas o retorno da API — **nenhum estado interno do Portico é acessado** (sem `pr_win32_ctx`, sem handlers diretos).

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_lstrlen.c -o Tests/PorticoRuntimeTests/data/hello_lstrlen.exe` (MinGW-w64 posix GCC 14).

## Imports

| DLL | Símbolo | Hint | Binding | IAT |
|---|---|---|---|---|
| `KERNEL32.dll` | **`lstrlenA`** | `066d` | **por nome** (`Ordinal <none>`) | `0x1400081c8` |
| `KERNEL32.dll` | `SetLastError` (auxiliar §14) | `0554` | por nome | `0x140008198` |
| `KERNEL32.dll` | `GetLastError` (auxiliar §14) | `0283` | por nome | `0x140008180` |
| `msvcrt.dll` | CRT de startup | — | por nome | — |

Auxiliares somente APIs já existentes e já validadas (G51–G57). Nenhuma dependência nova.

## Execução inicial

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_lstrlen.exe` com o **runtime completamente intacto**:

```
exited=1 rc=58 steps=1 exec=441
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 58
```

## Primeiro blocker

Nenhum.

## Diagnóstico de ABI

```asm
lea    0x25(%rsp),%rdi
mov    %rdi,%rcx                ; LPCSTR → RCX (ponteiro de 64 bits)
call   *%rsi                    ; lstrlenA() via IAT (rsi = IAT[0x1400081c8])
test   %eax,%eax                ; int de 32 bits em EAX (string vazia == 0)
...
lea    0x31(%rsp),%rcx
call   *%rsi
cmp    $0x3,%eax                ; "abc" == 3 (comparação de 32 bits)
jne    ...
lea    0x31(%rsp),%rcx          ; (string maior)
call   *%rsi
mov    %eax,%ebx                ; mov de 32 bits = int
cmp    $0xe,%eax                ; "PorticoRuntime" == 14
...
lea    0x29(%rsp),%rcx          ; buffer com NUL interno
call   *%rsi
cmp    $0x3,%eax                ; == 3 (para no primeiro NUL)
...
lea    0x40(%rsp),%rbp          ; buffer com canary
mov    %rbp,%rcx
call   *%rsi
cmp    $0x7,%eax                ; == 7
cmpb   $0xcc,0x0(%rbp,%rax,1)  ; canary verificado byte a byte
```

Cadeia demonstrada: **ponteiro de 64 bits → RCX → handler → int de 32 bits → EAX** (`test %eax,%eax`, `cmp $imm,%eax`, `mov %eax,%ebx`).

## Argumento LPCSTR

Chegou em **RCX** como ponteiro de 64 bits (`mov %rdi,%rcx` antes de cada `call`) apontando para a memória real do guest — o handler percorreu os bytes via `pr_win32_ptr` e parou nos NULs corretos.

## Retorno int

Declarado no PE como **`int`** em todas as variáveis (`int r`) e comparado como inteiro (`==`), **sem** conversão para `size_t`/`uint64_t` antes das assertions. No assembly: `test %eax,%eax`, `cmp $0x3,%eax`, `cmp $0xe,%eax`, `cmp $0x7,%eax`, `mov %eax,%ebx` — uso de **32 bits** do retorno. Não se tentou forçar valor negativo (artificial; sem representatividade — §13).

## Assertions

| # | Assertion | Esperado | Resultado |
|---|---|---|---|
| 10 | `lstrlenA("")` | `0` | ✓ |
| 11 | `lstrlenA("abc")` | `3` | ✓ |
| 12 | `lstrlenA("PorticoRuntime")` | `14` (esperado local conhecido; sem usar `lstrlenA`) | ✓ |
| 13 | `lstrlenA("abc\0xyz\0")` (NUL interno) | `3` (para no primeiro NUL) | ✓ |
| 14 | buffer `[Portico\0][canary×8]` | comprimento `7` + canaries `0xCC` intactos (sem escrita) | ✓ |
| 15 | `SetLastError(0xDEAD); lstrlenA("abc"); GetLastError()` | `0xDEAD` (intacto no sucesso) | ✓ |
| 16 | `lstrlenA(NULL)` (caminho explícito do handler) | `0` | ✓ |
| 17 | `SetLastError(0xDEAD); lstrlenA(NULL); GetLastError()` | `87` (mapeamento atual do Portico) | ✓ |

## LastError

- **Caminho de sucesso**: o handler **não toca** `ctx->last_error`; demonstrado `SetLastError(0xDEAD)` → `lstrlenA(...)` → `GetLastError() == 0xDEAD` (intacto).
- **Caminho de erro** (ponteiro `NULL`): o handler define `last_error = 87` (`W32_ERROR_INVALID_PARAMETER`); demonstrado nas assertions 16–17.
- Isso é **observação do Portico**, não afirmação de equivalência universal com Windows.

## Correção

**Nenhuma alteração no runtime.**
MD5: **`3ee3ce898084047420c02e7ef62179c9` antes == `3ee3ce898084047420c02e7ef62179c9` depois** (idêntico). `f_lstrcpynA` (G52) e o chamador interno `f_GetModuleHandleA` **intocados**. `lstrlenW`/`lstrcpyA`/`lstrcpyW`/`lstrcpynW`/`TlsAlloc`/`TlsSetValue` e demais APIs fora do escopo **não** implementadas (a `f_lstrcpyA` pré-existente em L829 já existia antes deste grupo e não foi tocada nem promovida).

## Resultado

**Todas as assertions passaram; exit code 58 obtido.** Falhas esperadas: 10–17 (nenhuma disparada).

## Repetições

**20/20 execuções com rc=58** (log=39 em todas) — sem crashes, sem variações, sem corrupção, sem estado residual. + 1 execução diagnóstico inicial = 21 execuções bem-sucedidas.

## Regressão

### C
`3392 verificações, 0 falhas` ✓

### Swift
`Executed 63 tests, with 0 failures` ✓

### Warnings
`gcc -Wall -Wextra`: **0** novos ✓

### Analyzer
`clang --analyze`: **7 pré-existentes / 0 novos** ✓ (receita idêntica; lista em `docs/RELATORIO_GRUPO49.md` §15)

### PE battery
`15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓

## Checklist acumulada

hello_stdhandle = 42 · hello_lstrcpyn = 52 · hello_getenv = 53 · hello_tickcount64 = 54 · hello_tlsgetvalue = 55 · hello_getcommandline = 56 · hello_getcommandlinew = 57 · **hello_lstrlen = 58** · hello_winsock_ord = 42 · hello_version_systeminfo = 42 · hello_wait_multiple = 42 · hello_thread_timeout = 42 · hello_mutex_timeout = 42 · hello_thread_shared = 42 · hello_thread = 42 · hello_mutex = 42 · hello_cs = 42 · hello_virt = 42 · hello_virt_protect = 42 · hello_virt_query = 42 · hello_qpc = 42 · hello_heap = 42 · hello_mbwc = 42 · hello_file = 42 · hello_file_w = 42 · hello_file_seek = 42 · hello_stdio = 5 · hello_gl12 = 42 · hello_sse = 7 — **29/29; nenhum teste anterior removido ou substituído; G51–G57 verdes** (hello_lstrcpyn = 52 confirmado preservado).

## Cobertura efetivamente demonstrada

Um programa x64 real **importou `lstrlenA` por nome**, o loader **resolveu o símbolo**, a chamada ocorreu **pela IAT**, o **`LPCSTR` chegou em RCX** como ponteiro de 64 bits para memória real do guest, o **handler real executou**, o **`int` de 32 bits chegou em EAX** e foi **interpretado como inteiro assinado**: string vazia = 0, `"abc"` = 3, `"PorticoRuntime"` = 14, NUL interno respeitado = 3, **canary intacto** (sem escrita), LastError preservado no sucesso e definido no erro (NULL), e o programa **continuou normalmente até o CRT** (exit code 58). Nenhum estado interno foi acessado pelo PE.

## Limitações

- O comportamento de `lstrlenA(NULL)` validado é o **caminho explícito do handler do Portico** (0 + error 87); em Windows real o comportamento de `NULL` não é garantido por contrato documentado — não se afirma equivalência universal.
- Strings de teste são ANSI de 1 byte por caractere; comprimentos que excedam `INT_MAX`/valores negativos de `int` **não** foram exercitados (artificiais; §13).
- A varredura do handler valida limites por `pr_win32_ptr` (1 byte por passo) — não se mediu desempenho nem se afirmou equivalência para strings fora da memória do guest.
- APIs fora do escopo (`lstrlenW`, `lstrcpyA`/`lstrcpyW`/`lstrcpynW` como exportações validadas, `TlsAlloc`, `TlsSetValue`, demais) permanecem **não validadas/não promovidas**.
- **Não se declara** compatibilidade geral com Windows, compatibilidade geral com Win32, nem que jogos comerciais funcionam — **GTA V, MX Bikes ou qualquer outro jogo NÃO são declarados compatíveis**. Somente o comportamento efetivamente demonstrado pelo PE é relatado.

## Próximo passo

`lstrcpyA` — irmão direto da família já inventariada (`f_lstrcpyA` pré-existente em L829; confirmar registro no catálogo e comportamento no inventário do próximo grupo); retorno `LPCSTR` = próprio ponteiro de destino. Alternativas subsequentes: `IsDebuggerPresent` (retorno BOOL), `GetCurrentProcessId`/`GetCurrentThreadId` (retorno DWORD), `lstrcmpA`. Nada disso foi implementado/validado no G58; **G59 não foi avançado**.
