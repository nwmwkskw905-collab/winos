# RELATÓRIO DA ETAPA — GRUPO 61

## Status

CONCLUÍDO

## API validada

kernel32!GetCurrentProcessId

## Inventário

1. **Arquivo/linha**: `Sources/PorticoRuntime/src/pr_win32.c` **L1716–1719**.
2. **Handler**: `f_GetCurrentProcessId`.
3. **Registro no catálogo**: `IMPL("kernel32.dll", "GetCurrentProcessId", f_GetCurrentProcessId, 0)` (**L4815**) — `stdcall_bytes = 0` → **zero argumentos** confirmado.
4. **Tipo de retorno interno**: `uint64_t` contendo `(uint64_t)getpid()` — valor **zero-extendido** (não há sinal: `pid_t` positivo; não há truncamento).
5. **Fonte do PID**: **`getpid()` do host POSIX** (processo que hospeda o runtime) — ver §Fonte do PID.
6. **Estado em `pr_win32_ctx`**: **nenhum** — o handler não usa `ctx` (`(void)ctx`); não existe campo de PID no contexto.
7. **PID fixo/gerado/derivado**: **não é fixo nem virtual** — é o PID real do processo host, atribuído pelo SO do host.
8. **Múltiplas chamadas**: retorno puro de `getpid()` → **estável dentro do mesmo processo** (determinístico por construção).
9. **Tratamento de `LastError`**: **não toca** `ctx->last_error` (somente `*st = PR_OK`) — caminho de erro **não existe**.
10. **Testes unitários existentes**: `Tests/PorticoRuntimeTests/test_win32.c:178` (`pr_win32_call(..., "GetCurrentProcessId", NULL, 0, &pid)`) — mantido intacto; **nenhum teste novo** adicionado (§13: desnecessário).
11. **PEs que importavam a API**: **nenhum** (varredura `objdump -p` sobre os 44 PEs: 0 ocorrências) — nunca validada por PE real antes.

## Implementação encontrada

```c
static uint64_t f_GetCurrentProcessId(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)a; (void)n; *st = PR_OK;
    return (uint64_t)getpid();
}
```

## Registro no catálogo

```c
    IMPL("kernel32.dll", "GetCurrentProcessId", f_GetCurrentProcessId, 0),
    IMPL("kernel32.dll", "GetCurrentThreadId", f_GetCurrentThreadId, 0),
```

Zero argumentos; registros existentes intocados.

## Fonte do PID

**`getpid()` do processo host POSIX** (o processo `dbg_diag`/`dbg_input` que hospeda o runtime), zero-extendido para 64 bits. Consequências documentadas:
- **Não** é um PID virtual do Portico e **não** é fixo;
- **varia entre processos/execuções** (cada execução do PE = um processo host novo com PID do SO do host);
- **não** representa o mecanismo de PID do Windows (sem isolamento de processo guest próprio — o "processo" do guest é o próprio processo host);
- dentro do mesmo processo o valor é **estável** (requisito principal do §7 atendido).

## PE

`hello_getcurrentprocessid.exe` — 14.336 bytes. Origem: `realpe/hello_getcurrentprocessid.c` (criado neste grupo; nenhum PE anterior alterado). Somente importação normal por DLL/IAT — **nenhum** acesso a `pr_win32_ctx`, `f_GetCurrentProcessId` ou estruturas internas.

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_getcurrentprocessid.c -o Tests/PorticoRuntimeTests/data/hello_getcurrentprocessid.exe` (MinGW-w64 posix GCC 14).

## Imports

| DLL | Símbolo | Hint | Binding | IAT |
|---|---|---|---|---|
| `KERNEL32.dll` | **`GetCurrentProcessId`** | `0235` | **por nome** (`Ordinal <none>`) | `0x140008180` |
| `KERNEL32.dll` | `GetLastError` (auxiliar §6.5) | `0283` | por nome | `0x140008188` |
| `KERNEL32.dll` | `SetLastError` (auxiliar §6.5) | `0554` | por nome | `0x1400081a0` |
| `msvcrt.dll` | CRT de startup | — | por nome | — |

Somente auxiliares já validados (G51–G60); nenhuma dependência nova.

## Execução inicial

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_getcurrentprocessid.exe` com o **runtime completamente intacto**:

```
exited=1 rc=61 steps=1 exec=336
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 61
```

## Primeiro blocker

Nenhum.

## Diagnóstico de ABI

```asm
mov    0x5aee(%rip),%rsi        ; rsi = IAT[0x140008180] (GetCurrentProcessId)
call   *%rsi                    ; chamada SEM argumentos pela IAT
mov    %eax,%ebx                ; DWORD: mov de 32 BITS (EAX → EBX)
mov    $0xa,%eax
test   %ebx,%ebx                ; a == 0? (teste de 32 bits) → falha 10
...
call   *%rsi                    ; 2ª chamada sem argumentos
cmp    %eax,%ebx                ; a == b (comparação de 32 bits) → falha 11
...
call   *%rsi                    ; 3ª chamada (XOR consumido como comparação)
cmp    %ebx,%eax
...
mov    $0xdead,%ecx
call   *…0x1400081a0            ; SetLastError
call   *%rsi                    ; 4ª chamada
call   *…0x140008188            ; GetLastError
mov    %eax,%edx                ; DWORD lido em 32 bits
mov    $0xc,%eax                ; falha 12 se != 0xDEAD
```

Zero argumentos (nenhum registrador configurado como parâmetro); retorno consumido **exclusivamente como DWORD de 32 bits** (`mov %eax,%ebx`, `test %ebx,%ebx`, `cmp %eax,%ebx`) — **nunca** como ponteiro.

## Retorno DWORD

Fronteira `handler uint64_t → dispatcher → RAX → EAX no PE → DWORD`:
- o handler retorna `(uint64_t)getpid()` = **zero-extension** de `pid_t` positivo (bits 63:32 = 0);
- o dispatcher entrega RAX cru, sem conversão;
- o PE consome **EAX** com instruções de **32 bits** (`mov %eax,%ebx`) em variável `DWORD`;
- **sem truncamento** (o valor cabe em 32 bits por construção) e **sem sign-extension** (classe **unsigned 32-bit**; nenhuma evidência de sinal — declarado apenas como DWORD).

## Estabilidade do PID

Três chamadas no mesmo processo: `a = b = c` (assertions `a == b` e `third() ^ b == 0` no assembly: `cmp %eax,%ebx` idêntico) — **estabilidade dentro do mesmo processo demonstrada**. O valor é consumido por operações reais do programa (`test`, `cmp`, `^`), impedindo otimização da chamada.

## LastError

### Sucesso
`SetLastError(0xDEAD)` → `GetCurrentProcessId()` → `GetLastError() == 0xDEAD` — **intacto** (o handler não toca `last_error`; comportamento observado).

### Erro
**API não possui caminho de erro exercitado** (não existe caminho de erro no handler — retorno puro incondicional). Nenhum erro artificial criado.

## Correção

**Nenhuma alteração no runtime.**
MD5: **`a0901f348f7482a45288d060469ac386` antes == `a0901f348f7482a45288d060469ac386` depois** (idêntico — §15). Nenhuma API implementada/promovida/alterada; `GetCurrentThreadId` **não** validada neste grupo (§1).

## Resultado

Assertions (61 = sucesso): **retorno não-zero** como DWORD ✓ (10); **`a == b`** (duas chamadas, mesmo PID) ✓ (11) + terceira chamada `^ b == 0` (valor usado pelo programa) ✓; **LastError `0xDEAD` intacto** ✓ (12). Exit code **61**; falhas 10–12 nenhuma disparada.

## Repetições

**20/20 execuções com rc=61** (log=33 em todas) — sem crashes, sem variação inesperada, sem estado residual; PID estável dentro de cada processo em todas as execuções. + 1 execução diagnóstico inicial = 21 execuções bem-sucedidas.

## Regressão

### C
`3398 verificações, 0 falhas` ✓ (baseline G60 mantido; nenhum teste unitário novo — §13)

### Swift
`Executed 63 tests, with 0 failures` ✓

### Warnings
`gcc -Wall -Wextra`: **0** novos ✓

### Analyzer
`clang --analyze`: **7 pré-existentes / 0 novos** ✓ (receita idêntica; lista em `docs/RELATORIO_GRUPO49.md` §15)

### PE battery
`15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓

## Checklist acumulada

hello_stdhandle = 42 · hello_lstrcpyn = 52 · hello_getenv = 53 · hello_tickcount64 = 54 · hello_tlsgetvalue = 55 · hello_getcommandline = 56 · hello_getcommandlinew = 57 · hello_lstrlen = 58 · hello_lstrcpy = 59 · hello_lstrcmp = 60 · **hello_getcurrentprocessid = 61** · hello_winsock_ord = 42 · hello_version_systeminfo = 42 · hello_wait_multiple = 42 · hello_thread_timeout = 42 · hello_mutex_timeout = 42 · hello_thread_shared = 42 · hello_thread = 42 · hello_mutex = 42 · hello_cs = 42 · hello_virt = 42 · hello_virt_protect = 42 · hello_virt_query = 42 · hello_qpc = 42 · hello_heap = 42 · hello_mbwc = 42 · hello_file = 42 · hello_file_w = 42 · hello_file_seek = 42 · hello_stdio = 5 · hello_gl12 = 42 · hello_sse = 7 — **32/32; nenhum PE anterior removido ou substituído** (G51–G60 verdes).

## Cobertura efetivamente demonstrada

**PE x64 real → import `GetCurrentProcessId` por nome → loader resolveu → IAT (`call *%rsi`) → chamada sem argumentos → handler real executou → retorno DWORD zero-extendido em RAX consumido como 32 bits em EAX → valor não-zero e utilizável → segunda e terceira chamadas retornaram o MESMO PID dentro do processo → LastError preservado → CRT → rc=61.** Comparação (§3) registrada: `GetCurrentProcessId` = DWORD 32-bit zero-extend (vs. `GetCurrentThreadId` = `pthread_self()` ponteiro 64-bit **não validada**; `GetTickCount64` = ULONGLONG 64-bit (G54); `TlsGetValue` = LPVOID (G55); `GetStdHandle` = HANDLE zero-extend (G51)).

## Limitações

- **PID é real do host POSIX (`getpid()`)** — não é virtual, **não é fixo**, **varia entre processos/execuções**; **não** representa o mecanismo de PID do Windows.
- **Não há isolamento real entre processos guest**: cada execução do PE é um processo host; não existem múltiplos processos guest simultâneos — **múltiplos processos simultâneos não testados**.
- Estabilidade demonstrada **dentro do mesmo processo** (3 chamadas); igualdade entre processos diferentes **não** é exigida e tipicamente não ocorre.
- **Overflow não testado** (PIDs reais cabem em DWORD positivo; sem gama de valores exercitada).
- `GetCurrentThreadId` **não** validada neste grupo (fora de escopo; retorno de ponteiro 64-bit observado no inventário).
- **Não se declara** compatibilidade geral com Windows, Win32 ou Winlator; **não** se declara compatibilidade com **GTA V**, **MX Bikes** ou qualquer jogo comercial. Somente o comportamento efetivamente demonstrado pelo PE é relatado.

## Próximo passo

`GetCurrentThreadId` — irmã direta observada neste inventário (`f_GetCurrentThreadId` L1721–1724: `return (uint64_t)(uintptr_t)pthread_self();`, catálogo `IMPL(…, 0)` L4816), sem validação por PE real; retorno de largura de ponteiro (classe distinta de DWORD) a confirmar pelo caminho do PE. Alternativa subsequente observada: `IsDebuggerPresent` (handler pré-existente em L1726–1730, retorna `0`, também nunca validada por PE real). Nada disso foi validado no G61; **G62 não foi avançado**.
