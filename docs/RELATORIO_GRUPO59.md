# RELATÓRIO DA ETAPA — GRUPO 59

## Status

CONCLUÍDO

## API validada

kernel32!lstrcpyA

## Inventário

1. **Handler**: `f_lstrcpyA`, `Sources/PorticoRuntime/src/pr_win32.c` **L829–841**.
2. **Registro no catálogo**: `IMPL("kernel32.dll", "lstrcpyA", f_lstrcpyA, 8)` (**L4790**) — `stdcall_bytes = 8` → **2 argumentos**.
3. **Argumentos**: `args[0]` = `lpString1` (destino, RCX), `args[1]` = `lpString2` (origem, RDX).
4. **Tipo de retorno interno**: `uint64_t` com o **valor cru de `a[0]`** (o próprio ponteiro de destino).
5. **Tratamento de ponteiros inválidos**: `pr_win32_ptr` falha → `ctx->last_error = W32_ERROR_INVALID_PARAMETER (87)` + retorno 0 — caminhos explícitos `!src` (L833) e `!dst` (L838).
6. **Tratamento de `NULL`**: **caminhos explícitos e determinísticos** — `pr_win32_ptr` (L258) começa com `if (!ctx || addr == 0) return NULL;`, então `lpString2 == NULL` cai em `!src` e `lpString1 == NULL` cai em `!dst`: ambos retornam **0 (NULL)** com **`last_error = 87`**, sem qualquer escrita.
7. **Algoritmo de cópia**: varredura limitada da origem byte a byte (`while (pr_win32_ptr(ctx, a[1] + len, 1) && src[len]) len++;`) → valida destino para **`len + 1`** bytes → `memcpy(dst, src, len + 1)`. A cópia inclui o **NUL** e **não** inclui bytes após o primeiro NUL.
8. **Validação antes da escrita**: destino validado (`pr_win32_ptr(ctx, a[0], len + 1)`) **antes** do `memcpy`; origem validada byte a byte; somente leitura na origem.
9. **Uso de `pr_win32_ptr`**: sim (L832, L836, L837) — endereços guest traduzidos por `pr_vm_translate`; `addr == 0` → NULL.
10. **Testes unitários existentes**: `Tests/PorticoRuntimeTests/test_win32.c:81` (`pr_win32_call(..., "lstrcpyA", args, 2, ...)`).
11. **PEs que já importavam `lstrcpyA`**: **nenhum** (varredura `objdump -p` sobre os 43 PEs: 0 ocorrências).

## Implementação encontrada

```c
static uint64_t f_lstrcpyA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    const char* src = (const char*)pr_win32_ptr(ctx, a[1], 1);
    if (!src) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t len = 0;
    while (pr_win32_ptr(ctx, a[1] + len, 1) && src[len]) len++;
    char* dst = (char*)pr_win32_ptr(ctx, a[0], len + 1);
    if (!dst) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    memcpy(dst, src, len + 1);
    return a[0];
}
```

Como `lpString1`/`lpString2` são obtidos: `args[0]`/`args[1]` (registradores crus RCX/RDX). Memória validada por `pr_win32_ptr` (origem por byte; destino por `len+1`). Cópia por `memcpy` do bloco `len+1` (conteúdo + **NUL**; nada além do NUL). Valor retornado: **`a[0]` = `lpString1`**. `LastError`: alterado (87) apenas nos caminhos de erro; **não toca** `last_error` no sucesso.

## Relação com lstrlenA

`f_lstrlenA` (G58, L818): mesma convenção `f(ctx, args, nargs, st)`, retorno `uint64_t`, mesma varredura limitada com `pr_win32_ptr`, mesmo código de erro 87. Diferenças: 1 argumento (`IMPL …, 4`) vs. 2 (`IMPL …, 8`); `f_lstrcpyA` **não** chama `f_lstrlenA` (faz a própria varredura); `f_lstrlenA` possui guarda explícita precoce `a[0] == 0` — em `f_lstrcpyA` o `NULL` é alcançado deterministicamente via `pr_win32_ptr(0) → NULL` + `!src`/`!dst`.

## Relação com lstrcpynA

`f_lstrcpynA` (G52, L842): mesma convenção e família (`IMPL …, 12` = 3 argumentos). Diferenças: `lstrcpynA` tem capacidade máxima (`iCharCount`) e **sempre** escreve NUL (corta em `cap−1`); `lstrcpyA` copia `len+1` exatos e exige destino com espaço suficiente (`len+1` validado antes da escrita). `f_lstrcpynA` **não foi alterada** (G52 preservado, `hello_lstrcpyn.exe = 52` reexecutado e verde).

## Tratamento de NULL

Caminhos explícitos e determinísticos existem para os **dois** argumentos (`!src` e `!dst`): retorno **NULL (0)** + `last_error = 87`, sem escrita em memória. Validado no PE (assertions 17–18) e registrado como **comportamento observado do Portico**, não como contrato universal de Windows (em Windows real o comportamento de NULL não é garantido por contrato documentado).

## PE

`hello_lstrcpy.exe` — 14.848 bytes. Origem: `realpe/hello_lstrcpy.c` (criado neste grupo; nenhum PE anterior alterado). O PE usa somente a importação normal por DLL/IAT — **nenhum** acesso a `pr_win32_ctx`, handlers ou estruturas internas.

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_lstrcpy.c -o Tests/PorticoRuntimeTests/data/hello_lstrcpy.exe` (MinGW-w64 posix GCC 14).

## Imports

| DLL | Símbolo | Hint | Binding | IAT |
|---|---|---|---|---|
| `KERNEL32.dll` | **`lstrcpyA`** | `0667` | **por nome** (`Ordinal <none>`) | `0x1400081c8` |
| `KERNEL32.dll` | `SetLastError` (auxiliar §6.8) | `0554` | por nome | `0x140008198` |
| `KERNEL32.dll` | `GetLastError` (auxiliar §6.8) | `0283` | por nome | `0x140008180` |
| `msvcrt.dll` | CRT de startup | — | por nome | — |

Somente APIs já existentes e já validadas (G51–G58) como auxiliares. Nenhuma dependência nova.

## Execução inicial

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_lstrcpy.exe` com o **runtime completamente intacto**:

```
exited=1 rc=59 steps=1 exec=662
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 59
```

## Primeiro blocker

Nenhum.

## Diagnóstico de ABI

```asm
mov    %rsi,%rdx                ; lpString2 (src) → RDX
mov    %rbx,%rcx                ; lpString1 (dst) → RCX
mov    0x5b12(%rip),%rdi        ; rdi = IAT[0x1400081c8] (lstrcpyA)
call   *%rdi                    ; chamada pela IAT
mov    %rax,%rdx                ; retorno consumido como ponteiro (64 bits)
cmp    %rdx,%rbx                ; ret == dst (contra o ponteiro ORIGINAL em rbx)
...
lea    0x42(%rsp),%rdx          ; src (string maior) → RDX
call   *%rdi
cmp    %rax,%r12                ; ret == dst (original guardado em r12)
mov    $0xc,%eax                ; código de falha 12
...
mov    %r12,%rcx                ; destino → RCX (NUL interno / canary)
call   *%rdi
cmp    %rax,%r12                ; ret == dst
cmpb   $0xcc,(%r12,%rax,1)     ; canary verificado byte a byte
```

Em cada um dos 4 call sites: destino em **RCX**, origem em **RDX**, `call` pela IAT, retorno em **RAX** consumido como ponteiro e comparado por igualdade (`cmp %rax, <reg>`) contra o ponteiro de destino original — nunca contra uma cópia.

## Argumento lpString1

Chegou em **RCX** (`mov %rbx,%rcx` / `mov %r12,%rcx`) como ponteiro de 64 bits para buffers locais reais do PE na memória do guest; o handler escreveu nesses buffers (`pr_win32_ptr` traduziu o endereço guest) e o conteúdo foi lido de volta pelo PE.

## Argumento lpString2

Chegou em **RDX** (`mov %rsi,%rdx` / `lea …,%rdx`) como ponteiro de 64 bits para as strings de origem; o handler percorreu os bytes reais do guest até o primeiro NUL.

## Retorno LPCSTR

Entregue em **RAX** como o próprio valor de `lpString1` (`return a[0];`). Consumido pelo PE como ponteiro: `r == dst` verificado **contra o ponteiro original de destino** (registradores `rbx`/`r12` preservados) em todas as chamadas — inclusive nos buffers com canary (`r == (char*)buf`).

## Assertions

| # | Assertion | Esperado | Resultado |
|---|---|---|---|
| 1 (10) | cópia simples `"abc"` em destino vazio | conteúdo `"abc"` no destino | ✓ |
| 2 (11) | terminador | `dst[3] == '\0'` | ✓ |
| 3 (12) | `"PorticoRuntime"` (14 chars; comprimento conhecido localmente, sem `lstrlenA`) | conteúdo completo + NUL no destino | ✓ |
| 4 (10) | retorno | `r == dst` (ponteiro original, não cópia) | ✓ |
| 5 (13) | origem preservada | `src` byte a byte inalterada | ✓ |
| 6 (14) | `"abc\0xyz"` em destino pré-preenchido `0xEE` | destino = `"abc\0"` e bytes ≥ 4 **permanecem** `0xEE` (nada copiado após o NUL) | ✓ |
| 7 (15) | `[Portico\0][canary×8=0xCC]` (destino com espaço suficiente) | exatamente 8 bytes escritos; canaries intactos | ✓ |
| 8 (16) | LastError no sucesso | `SetLastError(0xDEAD)` → `lstrcpyA` → `GetLastError() == 0xDEAD` | ✓ |
| §8 (17) | `lstrcpyA(NULL, src)` | `NULL` + `GetLastError() == 87` (caminho `!dst`) | ✓ |
| §8 (18) | `lstrcpyA(dst, NULL)` | `NULL` + `GetLastError() == 87` (caminho `!src`) | ✓ |

## LastError

- **Caminho de sucesso**: `last_error` **não** é tocado — demonstrado `0xDEAD` intacto.
- **Caminhos de erro** (src ou dst inválido/NULL): `last_error = 87` (`W32_ERROR_INVALID_PARAMETER`) — demonstrado nos dois casos NULL.
- Observação do Portico; não se afirma equivalência universal com Windows.

## Correção

**Nenhuma alteração no runtime.**
MD5: **`3ee3ce898084047420c02e7ef62179c9` antes == `3ee3ce898084047420c02e7ef62179c9` depois** (idêntico). `f_lstrcpynA` (G52), `f_lstrlenA` (G58) e `pr_win32_ptr` **intocados**. `lstrlenW`/`lstrcpyW`/`lstrcpynW`/`lstrcmpA`/`TlsAlloc`/`TlsSetValue`/`GetCurrentProcessId`/`GetCurrentThreadId`/`IsDebuggerPresent` e demais APIs fora do escopo **não** implementadas/promovidas.

## Resultado

Todas as assertions (1–8 + dois caminhos NULL) passaram; exit code **59**. Falhas esperadas 10–18: nenhuma disparada.

## Repetições

**20/20 execuções com rc=59** (log=40 em todas) — sem crashes, sem variação, sem corrupção, sem estado residual entre execuções. + 1 execução diagnóstico inicial = 21 execuções bem-sucedidas.

## Regressão

### C
`3392 verificações, 0 falhas` ✓ (nenhum teste unitário novo necessário: `test_win32.c:81` já cobre a semântica básica e foi mantido intacto — §16)

### Swift
`Executed 63 tests, with 0 failures` ✓

### Warnings
`gcc -Wall -Wextra`: **0** novos ✓

### Analyzer
`clang --analyze`: **7 pré-existentes / 0 novos** ✓ (receita idêntica; lista em `docs/RELATORIO_GRUPO49.md` §15)

### PE battery
`15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓

## Checklist acumulada

hello_stdhandle = 42 · hello_lstrcpyn = 52 · hello_getenv = 53 · hello_tickcount64 = 54 · hello_tlsgetvalue = 55 · hello_getcommandline = 56 · hello_getcommandlinew = 57 · hello_lstrlen = 58 · **hello_lstrcpy = 59** · hello_winsock_ord = 42 · hello_version_systeminfo = 42 · hello_wait_multiple = 42 · hello_thread_timeout = 42 · hello_mutex_timeout = 42 · hello_thread_shared = 42 · hello_thread = 42 · hello_mutex = 42 · hello_cs = 42 · hello_virt = 42 · hello_virt_protect = 42 · hello_virt_query = 42 · hello_qpc = 42 · hello_heap = 42 · hello_mbwc = 42 · hello_file = 42 · hello_file_w = 42 · hello_file_seek = 42 · hello_stdio = 5 · hello_gl12 = 42 · hello_sse = 7 — **30/30; nenhum PE anterior removido ou substituído** (`hello_lstrcpyn = 52` mantido; G51–G58 verdes).

## Cobertura efetivamente demonstrada

Um programa x64 real **importou `lstrcpyA` por nome**, o loader **resolveu o símbolo**, as chamadas ocorreram **pela IAT**, **`lpString1` chegou em RCX** e **`lpString2` em RDX** como ponteiros de 64 bits, o **handler real executou**, **escreveu na memória real do guest** o conteúdo + NUL (e **nada além** do NUL), **preservou a origem**, manteve o **canary intacto**, **retornou em RAX o próprio ponteiro de destino** (verificado contra o original), demonstrou os **dois caminhos NULL explícitos** (`NULL` + error 87) e **LastError preservado no sucesso** — e o programa **continuou normalmente até o CRT** (exit code 59).

## Limitações

- Comportamento de `NULL` validado é o **caminho explícito do handler do Portico** (retorno NULL + error 87); não é afirmado como contrato universal de Windows.
- **Destinos insuficientemente pequenos não testados** (o escopo determinou validar apenas o caso com espaço suficiente); não se alega segurança para destinos pequenos. Pelo código, destino sem espaço `len+1` → erro 87 sem escrita — não exercitado por PE.
- **Sobreposição origem/destino (overlap) não testada** — `memcpy` não garante comportamento sobreposto (como em `lstrcpyA` de Windows, que também não documenta overlap).
- Strings extremamente grandes e **memória inválida além de NULL** (ponteiros arbitrários) não testadas em PE.
- APIs wide (`lstrlenW`/`lstrcpyW`/`lstrcpynW`) e irmãs (`lstrcmpA`, `TlsAlloc`, `TlsSetValue`, `GetCurrentProcessId`, `GetCurrentThreadId`, `IsDebuggerPresent`, demais) **não** implementadas/promovidas/validadas.
- **Não se declara** compatibilidade geral com Windows, Win32 ou Winlator; **não** se declara compatibilidade com **GTA V**, **MX Bikes** ou qualquer jogo comercial. Somente o comportamento efetivamente demonstrado pelo PE é relatado.

## Próximo passo

`lstrcmpA` — irmã direta da família ANSI já validada (`lstrlenA`/`lstrcpyA`/`lstrcpynA` = catálogo L4789–4791); observado no inventário que **não possui handler** hoje (comparação lexicográfica = lacuna real da família). Alternativas subsequentes observadas nos caminhos já exercitados: `IsDebuggerPresent` (retorno BOOL), `GetCurrentProcessId`/`GetCurrentThreadId` (retorno DWORD). Nada disso foi implementado/validado no G59; **G60 não foi avançado**.
