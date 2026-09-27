# RELATÓRIO DA ETAPA — GRUPO 62

## Status

CONCLUÍDO

## API validada

kernel32!GetCurrentThreadId

## Inventário

1. **Handler**: `f_GetCurrentThreadId`, `Sources/PorticoRuntime/src/pr_win32.c` **L1721–1724** (confirmado no código atual antes de qualquer execução).
2. **Argumentos recebidos**: nenhum (`(void)a; (void)n` — catálogo `0` bytes stdcall).
3. **Tipo de retorno interno**: `uint64_t` = `(uint64_t)(uintptr_t)pthread_self()` — **inteiro 64-bit derivado de ponteiro** (handle `pthread_t` do host = endereço do TCB no Linux).
4. **Uso de `ctx`**: nenhum (`(void)ctx`); **uso de `LastError`**: nenhum (não toca `ctx->last_error`); **tratamento de erro**: inexistente (retorno puro; `*st = PR_OK` incondicional).
5. **Fonte do identificador**: `pthread_self()` do **host POSIX** (thread corrente do processo hospedeiro do runtime).
6. **Catálogo**: `IMPL("kernel32.dll", "GetCurrentThreadId", f_GetCurrentThreadId, 0)` (**L4816**) — DLL `kernel32.dll`, API `GetCurrentThreadId`, handler `f_GetCurrentThreadId`, `stdcall_bytes = 0` → **zero argumentos confirmado**.
7. **Testes existentes**: **nenhum** — busca por `GetCurrentThreadId` em `Sources/`, `Tests/`, `realpe/`, `tools/` = 0 usos (sem unit-test, sem tools, sem uso indireto).
8. **PEs existentes**: varredura `objdump -p` sobre todos os PEs anteriores = **nenhum importa `GetCurrentThreadId`** — PE novo criado exclusivamente para este grupo.

## Implementação encontrada

```c
static uint64_t f_GetCurrentThreadId(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)a; (void)n; *st = PR_OK;
    return (uint64_t)(uintptr_t)pthread_self();
}
```

(Confirmado idêntico ao indicado pelo inventário do G61 — verificado no código atual, §1.)

## Registro no catálogo

```c
    IMPL("kernel32.dll", "GetCurrentThreadId", f_GetCurrentThreadId, 0),
```

Único registro; zero argumentos; nenhum registro existente alterado.

## PE criado

`hello_getcurrentthreadid.exe` — 14.336 bytes. Origem: `realpe/hello_getcurrentthreadid.c` (criado neste grupo; nenhum PE anterior alterado). Importa `KERNEL32.dll!GetCurrentThreadId` e usa **somente** a importação normal por DLL/IAT — nenhum acesso a `pr_win32_ctx`, `f_GetCurrentThreadId`, estruturas internas, handlers diretos ou mecanismos especiais do runtime (auditoria §14: 0 referências a internals no código do PE).

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_getcurrentthreadid.c -o Tests/PorticoRuntimeTests/data/hello_getcurrentthreadid.exe` (MinGW-w64 x86-64 posix GCC 14).

## Imports

| DLL | Símbolo | Hint | Binding | IAT |
|---|---|---|---|---|
| `KERNEL32.dll` | **`GetCurrentThreadId`** | `0239` | **por nome** (`Ordinal <none>`) | `0x140008180` |
| `KERNEL32.dll` | `GetLastError` (auxiliar §5) | `0283` | por nome | `0x140008188` |
| `KERNEL32.dll` | `SetLastError` (auxiliar §5) | `0554` | por nome | `0x1400081a0` |
| `msvcrt.dll` | CRT de startup | — | por nome | — |

Somente auxiliares já validados (G51–G61); nenhuma dependência nova.

## Execução inicial

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_getcurrentthreadid.exe` com o **runtime intacto**:

```
exited=1 rc=62 steps=1 exec=339
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 62
```

(Exit code **62** = convenção exclusiva do grupo, seguindo a série G57→G61; falhas 10–12.)

## Primeiro blocker

Nenhum.

## Diagnóstico de ABI

```asm
mov    0x5aed(%rip),%rsi        ; rsi = IAT[0x140008180] (GetCurrentThreadId)
call   *%rsi                    ; 1ª chamada — SEM argumentos
mov    %eax,%ebx                ; a ← EAX (consumo de 32 BITS)
call   *%rsi                    ; 2ª chamada — SEM argumentos
mov    %eax,%edi                ; b ← EAX (32 bits)
call   *%rsi                    ; 3ª chamada — SEM argumentos (eax = c)
mov    $0xa,%edx
test   %ebx,%ebx                ; a == 0? (32 bits) → falha 10
je     ...
cmp    %eax,%edi                ; c == b? (32 bits)
jne    ...
cmp    %edi,%ebx                ; a == b? (32 bits) → falha 11
```

Três chamadas reais consecutivas pela IAT, **sem argumentos** (nenhum registrador configurado como parâmetro), com consumo real dos valores (`test`/`cmp`/branches + XOR `a ^ c`). Nenhuma instrução de 64 bits utiliza o retorno.

## Largura do retorno

Cadeia completa determinada pelo **consumidor real no disassembly** (não pelo tipo do handler):

```text
f_GetCurrentThreadId: uint64_t = (uintptr_t)pthread_self()   [inteiro 64-bit derivado de ponteiro]
        ↓
dispatcher: entrega RAX CRU (sem conversão de largura)
        ↓
consumidor (PE, assinatura Win32 DWORD GetCurrentThreadId(VOID)): mov %eax,%ebx / mov %eax,%edi
        ↓
DWORD de 32 bits = bits 31:0 de RAX
```

- O PE utiliza **EAX** (`mov %eax,…`, `test %ebx,%ebx`, `cmp %eax,%edi` — todas de **32 bits**) → largura efetiva no consumo = **DWORD**.
- **Conversão intermediária documentada**: **truncamento para 32 bits na fronteira do consumidor** — os bits 63:32 do valor 64-bit do handler são descartados pela leitura de EAX; o DWORD é interpretado como **unsigned**. Não há sign-extension nem zero-extension explícitos no caminho (o handler já devolve o valor 64-bit cru; o EAX lido é o fragmento inferior).
- **Nenhuma alteração do handler** foi feita para coincidir com expectativa teórica (§7/§10): o comportamento observado é este e foi apenas documentado.

## Estabilidade

`a == b == c` demonstrado no mesmo processo (três chamadas; `test`/`cmp`/XOR no assembly). Nas **20 execuções** (processos distintos) o valor pode variar — não exigido igualdade entre processos (§8); a estabilidade **dentro do processo** ocorreu em todas.

## LastError

### Sucesso
`SetLastError(0xDEAD)` → `GetCurrentThreadId()` → `GetLastError() == 0xDEAD` — **intacto** (comportamento observado; o handler não toca `last_error`).

### Erro
**API não possui caminho de erro exercitado** (não existe caminho de erro no handler — retorno puro incondicional). Nenhum erro artificial criado (§5).

## Correção

**Nenhuma alteração no runtime** — resultado ideal do grupo atingido (§10): PE novo + validação de ABI + testes + regressão, sem mudança em `pr_win32.c`.
MD5: **`a0901f348f7482a45288d060469ac386` antes == `a0901f348f7482a45288d060469ac386` depois** (idêntico). Nenhuma API não relacionada implementada (auditoria §14: handler = 1 implementação; catálogo = 1 registro; PE sem internals; sem duplicação).

## Resultado das 20 execuções

**20/20 com rc=62** (log=33 em todas) — sem crashes, sem `Unsupported Win32 API`, sem variação inesperada, sem corrupção, sem estado residual; Thread ID estável dentro de cada processo. + 1 execução diagnóstico inicial = 21 execuções bem-sucedidas.

## Regressão

### C
`3398 verificações, 0 falhas` ✓ (baseline G60 mantido; nenhum CHECK novo — §11: desnecessário, o PE é a evidência principal)

### Swift
`Executed 63 tests, with 0 failures` ✓

### Warnings
`gcc -Wall -Wextra`: **0** novos ✓

### Analyzer
`clang --analyze`: **7 pré-existentes / 0 novos** ✓ (receita idêntica; lista em `docs/RELATORIO_GRUPO49.md` §15)

### PE battery
`15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓ — nenhum PE histórico regrediu.

## Checklist acumulada

hello_stdhandle = 42 · hello_lstrcpyn = 52 · hello_getenv = 53 · hello_tickcount64 = 54 · hello_tlsgetvalue = 55 · hello_getcommandline = 56 · hello_getcommandlinew = 57 · hello_lstrlen = 58 · hello_lstrcpy = 59 · hello_lstrcmp = 60 · hello_getcurrentprocessid = 61 · **hello_getcurrentthreadid = 62** · hello_winsock_ord = 42 · hello_version_systeminfo = 42 · hello_wait_multiple = 42 · hello_thread_timeout = 42 · hello_mutex_timeout = 42 · hello_thread_shared = 42 · hello_thread = 42 · hello_mutex = 42 · hello_cs = 42 · hello_virt = 42 · hello_virt_protect = 42 · hello_virt_query = 42 · hello_qpc = 42 · hello_heap = 42 · hello_mbwc = 42 · hello_file = 42 · hello_file_w = 42 · hello_file_seek = 42 · hello_stdio = 5 · hello_gl12 = 42 · hello_sse = 7 — **33/33**. Revalidação dos checkpoints recentes (§12) completa: lstrcpyn=52 · getenv=53 · tickcount64=54 · tlsgetvalue=55 · getcommandline=56 · getcommandlinew=57 · lstrlen=58 · lstrcpy=59 · lstrcmp=60 · getcurrentprocessid=61 · **getcurrentthreadid=62** — todos verdes; nenhum PE substituído ou removido.

## Cobertura efetivamente demonstrada

**PE x64 real → import `GetCurrentThreadId` por nome → loader resolveu → IAT (`call *%rsi`) → chamadas sem argumentos (3×) → `f_GetCurrentThreadId` real executou → retorno 64-bit cru em RAX → consumidor real leu EAX (DWORD de 32 bits; bits 31:0) → valores consumidos por test/cmp/XOR → `a != 0`, `a == b`, `b == c` no mesmo processo → LastError preservado → CRT → rc=62.** A largura efetiva do retorno foi determinada pelo disassembly do consumidor: **DWORD (32 bits)**, com truncamento dos bits superiores documentado.

## Limitações

- O valor é o **`pthread_self()` do host truncado em 32 bits** (bits 31:0 do handle pthread/TCB) — **não** é um Thread ID do Windows; não representa o mecanismo de TID do Windows; **varia entre processos/execuções**.
- Estabilidade demonstrada **dentro do mesmo processo e da thread principal** (única thread do PE); **múltiplas threads não testadas** (este PE é single-thread; criação de threads com TIDs distintos não foi exercitada).
- **Overflow/faixa de valores não testados**; a faixa observada é a dos 32 bits inferiores de ponteiros do host.
- Múltiplos processos simultâneos **não** testados.
- `IsDebuggerPresent` e demais APIs **não** validadas/alteradas neste grupo.
- **Não se declara** compatibilidade geral com Windows, Win32 ou Winlator; **não** se declara compatibilidade com **GTA V**, **MX Bikes** ou qualquer jogo comercial. Somente o comportamento efetivamente demonstrado pelo PE é relatado.

## Próximo candidato

`IsDebuggerPresent` — após análise do resultado de G62 (passou intacto), o próximo candidato natural é esta API: já observada no inventário (`f_IsDebuggerPresent` L1726–1730, retorno `0` = "processo não está sob depuração"; verificar registro no catálogo e consumo BOOL/DWORD no PE real). Nada disso foi validado no G62; **G63 não foi avançado**.
