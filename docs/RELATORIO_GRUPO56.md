# RELATÓRIO DA ETAPA — GRUPO 56

## Status

CONCLUÍDO

## API validada

kernel32!GetCommandLineA

## Inventário

1. **Onde está implementada**: `f_GetCommandLineA`, `Sources/PorticoRuntime/src/pr_win32.c` L2575.
2. **Registro no catálogo**: `IMPL("kernel32.dll", "GetCommandLineA", f_GetCommandLineA, 0)` (L4824; 0 argumentos). Irmã: `f_GetCommandLineW` (L2670) + `IMPL` (L4829) — **não** validada neste grupo.
3. **Estrutura que mantém a linha de comando**: `pr_win32_ctx` — `uint8_t* scratch` (512 bytes host; L187) e `uint32_t guest_cmdline` (VA do guest; L197) com `uint32_t guest_cmdline_w` (UTF-16 em `guest_cmdline+256`; L198).
4. **Como é inicializada**: `pr_win32_create` (L5112) faz `scratch = calloc(512)` e semeia `const char* cmdline = "portico"` (L5121-5122). `pr_win32_bind_vm` (L5215) aloca uma página RO do guest (`"w32static"`, 0x1000), copia `scratch` (slen+1) para `g` e escreve a versão UTF-16 em `g+256`; fixa `guest_cmdline = g`.
5. **O loader já fornece linha de comando?** Sim — via `pr_peproc → pr_win32_create + pr_win32_bind_vm`. Existe `pr_win32_set_cmdline` (L5260; declarada em `pr_win32.h` L177), porém **nenhum código o chama** (grep em `Sources/`, `include/`, `tools/`) → todo PE real vê exatamente **"portico"**. Nada foi fabricado para o teste (§8).
6. **Estado persistente entre chamadas**: sim — o handler retorna sempre o mesmo `guest_cmdline` (ou o mesmo `scratch` no fallback sem VM).
7. **Unit-tests existentes**: `test_pes.c`, `test_win32.c`, `test_win32x.c`.
8. **PE anterior que importe por causa do startup**: **nenhum** — 0 ocorrências de `GetCommandLine` nos imports de PE real inspecionados (amostras dos 3 formatos de startup: hello_real/hello_stdio/hello_thread) e na varredura completa do G48 (lista B = nunca importada).

## Implementação encontrada

```c
static uint64_t f_GetCommandLineA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)a; (void)n; *st = PR_OK;
    if (ctx->vm && ctx->guest_cmdline) return ctx->guest_cmdline;
    return (uint64_t)(uintptr_t)ctx->scratch;
}
```

Retorno puro de ponteiro; **não toca `last_error`**; sem cópia nova por chamada.

## Estado da command line

Conteúdo efetivo para todo PE real: **`"portico"`** (7 caracteres + NUL), residente em página **read-only do guest** (`w32static`, VA `guest_cmdline`; ANSI em 256 bytes; UTF-16 em +256). Determinístico e idêntico em qualquer forma de execução via `pr_peproc` (os harnesses `dbg_*` usam o mesmo caminho do host real).

## PE

`hello_getcommandline.exe` — 14.336 bytes. Origem: `realpe/hello_getcommandline.c` (criado neste grupo; nenhum PE anterior alterado).

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_getcommandline.c -o Tests/PorticoRuntimeTests/data/hello_getcommandline.exe` (MinGW-w64 posix).

## Imports

| DLL | Símbolo | Hint | Binding | IAT | Thunk |
|---|---|---|---|---|---|
| `KERNEL32.dll` | **`GetCommandLineA`** | `01f2` | **por nome** (`Ordinal <none>`) | `0x140008180` | `jmp *0x5b12(%rip)` em `0x140002668` |
| `KERNEL32.dll` | `SetLastError` (auxiliar) | `0554` | por nome | `0x1400081a0` | presente |
| `KERNEL32.dll` | `GetLastError` (auxiliar) | `0283` | por nome | `0x140008188` | presente |
| `msvcrt.dll` | CRT de startup | — | por nome | — | — |

Endereço efetivamente usado na chamada: `mov 0x5aed(%rip),%rdi ; # 0x140008180` + `call *%rdi`. Auxiliares usados somente na assertion "LastError permanece intacto" (§10). Nenhuma API nova foi implementada.

## Execução inicial

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_getcommandline.exe` com o **runtime completamente intacto**:

```
exited=1 rc=56 steps=1 exec=390
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 56
```

Import resolvido, handler real executado, string lida pelo PE, fluxo completo.

## Primeiro blocker

Nenhum.

## Diagnóstico de ABI

Call site real (`objdump -d`):

```asm
mov    0x5aed(%rip),%rdi     ; rdi = IAT[GetCommandLineA] (0x140008180)
call   *%rdi                  ; GetCommandLineA() — SEM argumentos
mov    %rax,%rsi              ; LPSTR → RSI (move de 64 BITS)
test   %rax,%rax              ; a != NULL (teste de 64 bits)
...
cmpb   $0x0,(%rsi,%rbx,1)     ; leitura byte a byte da string no guest
...
call   *%rdi                  ; segunda chamada
mov    %rax,%rdx              ; b (64 bits)
cmp    %rdx,%rsi              ; identidade a == b
...
cmpb   $0x6f,0x6(%rsi)        ; verificação de conteúdo byte a byte
```

- **Argumentos: nenhum** (RDI/RSI/RDX/RCX/R8/R9 não são parâmetros — nenhum setup antes do `call`).
- **Retorno em RAX**, tratado como **`LPSTR` de 64 bits** (`mov %rax,%rsi`/`%rdx`, `test %rax,%rax`, indexação `(%rsi,%rbx,1)`) — sem truncamento.
- Classe `GetCommandLineA() → RAX → LPSTR → memória com string ANSI NUL-terminada` **demonstrada**, incluindo a leitura da memória retornada pelo PE (`cmpb` sobre `(%rsi,%rbx,1)`).

## Retorno LPSTR

Entregue em RAX como ponteiro de 64 bits para memória **válida no espaço do guest** (página `w32static` alocada por `pr_win32_bind_vm`) — lido pelo PE sem qualquer erro de memória.

## Conteúdo da string

`"portico"` — exatamente a string semeada pelo runtime atual (`pr_win32_create`), validada byte a byte (`'p','o','r','t','i','c','o'`). Nenhum contrato foi inventado: §9 autoriza propriedades concretas quando a linha é conhecida e determinística, o que é o caso (e ela **não** varia com a forma de iniciar o processo, pois `pr_win32_set_cmdline` não é chamado por ninguém).

## NUL termination

Demonstrada por varredura local limitada a 256 bytes (tamanho da região ANSI do guest), sem `lstrlenA` (§11): NUL encontrado em `len = 7`.

## Consistência entre chamadas

`a == b` (identidade de ponteiro) — o runtime retorna armazenamento persistente (`guest_cmdline` fixo); a identidade **faz parte do contrato atual** e foi validada. O conteúdo das duas chamadas é trivialmente o mesmo pela identidade, e o conteúdo em si foi validado byte a byte.

## LastError

O handler **não toca `last_error`** (retorno puro). Verificado no PE com `SetLastError(0xDEAD)` → `GetCommandLineA()` → `GetLastError() == 0xDEAD` (comportamento existente permanece intacto; §10 — não há contrato de erro nesta API e nenhum foi inventado).

## Correção

**Nenhuma alteração no runtime.**
MD5 do runtime: **`3ee3ce898084047420c02e7ef62179c9` antes == `3ee3ce898084047420c02e7ef62179c9` depois** (idêntico). Nenhuma infraestrutura alterada; nenhum loader/subsistema de processo refatorado; TLS intocado; `TlsAlloc`/`TlsSetValue`/`lstrcpyA`/`lstrlenA`/demais APIs do §16 **não** implementadas.

## Resultado

Assertions (contrato do PE — 56 = sucesso; códigos de falha 10–15):
1. **A — ponteiro não nulo**: `a != NULL` ✓ (10)
2. **B — NUL termination**: NUL dentro do limite seguro de 256 ✓ (11)
3. **C — consistência/identidade**: `b == a` ✓ (12)
4. **D — conteúdo**: `len == 7` e bytes `'p','o','r','t','i','c','o'` exatos ✓ (13/14)
5. **E — memória acessível**: a própria varredura byte a byte sem crash ✓
6. **LastError intacto**: `GetLastError() == 0xDEAD` após a chamada ✓ (15)

## Repetições

**20/20 execuções com rc=56** (log=32 em todas) — mesmo resultado, sem crash, sem corrupção, sem alteração da string, sem estado residual, independente de execuções anteriores. + 1 execução diagnóstico inicial = 21 execuções bem-sucedidas.

## Regressão

### C
`3392 verificações, 0 falhas` ✓ (nenhum teste antigo apagado/alterado).

### Swift
`Executed 63 tests, with 0 failures` ✓

### Warnings
`gcc -Wall -Wextra` (receita do projeto): **0** novos ✓

### Analyzer
`clang --analyze` (receita reproduzível): **7 antigos** (pré-existentes, lista no `docs/RELATORIO_GRUPO49.md` §15) · **0 novos** ✓

### PE battery
`15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓

## Checklist acumulada

hello_stdhandle = 42 · hello_lstrcpyn = 52 · hello_getenv = 53 · hello_tickcount64 = 54 · hello_tlsgetvalue = 55 · **hello_getcommandline = 56** · hello_winsock_ord = 42 · hello_version_systeminfo = 42 · hello_wait_multiple = 42 · hello_thread_timeout = 42 · hello_mutex_timeout = 42 · hello_thread_shared = 42 · hello_thread = 42 · hello_mutex = 42 · hello_cs = 42 · hello_virt = 42 · hello_virt_protect = 42 · hello_virt_query = 42 · hello_qpc = 42 · hello_heap = 42 · hello_mbwc = 42 · hello_file = 42 · hello_file_w = 42 · hello_file_seek = 42 · hello_stdio = 5 · hello_gl12 = 42 · hello_sse = 7 — **27/27, nenhum PE anterior removido ou regredido (G51–G55 verdes)**.

## Cobertura efetivamente demonstrada

Um programa x64 real **importou `GetCommandLineA` por nome**, o loader **resolveu o import**, a chamada ocorreu **via IAT**, o **handler real** executou, o retorno **`LPSTR` chegou em RAX** como ponteiro de 64 bits, o PE **leu a string na memória do guest**, validou **NUL termination**, **consistência/identidade** entre chamadas, **conteúdo determinístico** e **LastError intacto**, e **continuou sua execução normalmente** até o exit code 56 via CRT.

## Limitações

- O conteúdo é a string fixa **"portico"** do runtime atual — **não** reflete `argv`/linha de comando de um Windows real ou do host; se um host futuro passar a chamar `pr_win32_set_cmdline`, o contrato de conteúdo deste PE descreveria o estado atual, não o futuro (registrado).
- `GetCommandLineW` (variante UTF-16, já implementada em `f_GetCommandLineW`) **não** foi validada por PE.
- O fallback `ctx->scratch` (sem VM) não foi exercitado por PE real — todo PE roda com VM ligada (`pr_win32_bind_vm`).
- Nenhuma API nova foi implementada (`TlsAlloc`, `TlsSetValue`, `lstrcpyA`, `lstrlenA`, `IsDebuggerPresent`, `GetCurrentProcessId`, `GetCurrentThreadId`, `GetCommandLineW`, `GetEnvironmentVariableW`, `SetEnvironmentVariableA/W` permanecem fora do escopo — §16).
- **Não se afirma** compatibilidade geral com Windows, compatibilidade geral com Win32, nem que jogos comerciais funcionam — **GTA V, MX Bikes ou qualquer outro jogo NÃO são declarados compatíveis**. O relatório descreve somente o que o PE real demonstrou.

## Próximo passo

`GetCommandLineW` — variante gêmea já implementada (`f_GetCommandLineW`, `IMPL` L4829); validaria a string UTF-16 em `guest_cmdline+256` (ponteiro 64-bit + conteúdo wide + NUL wide), sem qualquer implementação nova. Alternativas subsequentes: `lstrlenA`/`lstrcpyA` (irmãos da `lstrcpynA`), `IsDebuggerPresent` (retorno BOOL), `GetCurrentProcessId`/`GetCurrentThreadId` (retorno DWORD). Nada disso foi implementado no G56; **G57 não foi avançado**.
