# RELATÓRIO DA ETAPA — GRUPO 63

## Status

CONCLUÍDO

## API validada

kernel32!IsDebuggerPresent

## Inventário

1. **Busca completa** por `IsDebuggerPresent`/`f_IsDebuggerPresent` em `Sources/`, `Tests/`, `realpe/`, `tools/` (código atual, antes de qualquer alteração): **2 ocorrências** — a definição do handler e o registro no catálogo. **Nenhum** teste unitário, **nenhum** PE, **nenhuma** ferramenta, **nenhum** uso indireto.
2. **Arquivo/linha**: `Sources/PorticoRuntime/src/pr_win32.c` **L1726–1729** (posição confirmada no código atual).
3. **Assinatura/argumentos**: `f(ctx, args, nargs, st)`; nenhum argumento (`(void)a; (void)n`).
4. **Tipo de retorno interno**: `uint64_t` constante **`0`** (= `FALSE`).
5. **Uso de `ctx`**: nenhum (`(void)ctx`). **Uso de `LastError`**: nenhum (não toca `ctx->last_error`). **Tratamento de erro**: inexistente (retorno puro; `*st = PR_OK` incondicional).
6. **Registro no catálogo**: `IMPL("kernel32.dll", "IsDebuggerPresent", f_IsDebuggerPresent, 0)` (**L4817**) — `stdcall_bytes = 0` = **zero argumentos**; **registro único** (sem duplicatas).
7. **PEs existentes**: varredura `objdump -p` sobre todos os PEs anteriores = **nenhum importa `IsDebuggerPresent`** — PE novo criado exclusivamente para G63.

## Implementação encontrada

```c
static uint64_t f_IsDebuggerPresent(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)a; (void)n; *st = PR_OK;
    return 0; /* real: processo não está sob depuração */
}
```

Comportamento atual registrado (§2): o Portico reporta **`FALSE` (0) = "o processo não está sob depuração"**. Isso **não** é alegação de equivalência universal com Windows — é o comportamento efetivamente implementado, validado pelo PE.

## Registro no catálogo

```c
    IMPL("kernel32.dll", "IsDebuggerPresent", f_IsDebuggerPresent, 0),
```

Único registro; DLL `kernel32.dll`; `stdcall_bytes = 0` confirmado (**zero argumentos**); registro já correto — **não alterado** (§3).

## PE criado

`hello_isdebuggerpresent.exe` — 14.336 bytes. Origem: `realpe/hello_isdebuggerpresent.c` (criado neste grupo; nenhum PE histórico substituído/removido). x86-64 MinGW-w64; importa `KERNEL32.dll!IsDebuggerPresent`; usa somente a importação normal pela IAT; chamada real `BOOL a = IsDebuggerPresent();` (não constante substituta); **nenhum** acesso a internals (`pr_win32_ctx`, `f_IsDebuggerPresent`, mecanismos especiais = 0 referências — auditoria §17).

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_isdebuggerpresent.c -o Tests/PorticoRuntimeTests/data/hello_isdebuggerpresent.exe` (MinGW-w64 posix GCC 14).

## Imports

| DLL | Símbolo | Hint | Binding | IAT |
|---|---|---|---|---|
| `KERNEL32.dll` | **`IsDebuggerPresent`** | `03ad` | **por nome** (`Ordinal <none>`) | `0x140008190` |
| `KERNEL32.dll` | `GetLastError` (auxiliar §10) | `0283` | por nome | `0x140008180` |
| `KERNEL32.dll` | `SetLastError` (auxiliar §10) | `0554` | por nome | `0x1400081a0` |
| `msvcrt.dll` | CRT de startup | — | por nome | — |

Somente auxiliares já validados (G51–G62); nenhuma dependência nova.

## Execução inicial

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_isdebuggerpresent.exe` com o **runtime intacto**:

```
exited=1 rc=63 steps=1 exec=337
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 63
```

(Exit code **63** = convenção exclusiva da série; falhas 10–12. Sem `Unsupported Win32 API`; sem crash; sem comportamento inesperado.)

## Primeiro blocker

Nenhum.

## Diagnóstico de ABI

```asm
mov    0x5afd(%rip),%rsi        ; rsi = IAT[0x140008190] (IsDebuggerPresent)
call   *%rsi                    ; 1ª chamada — SEM argumentos
mov    %eax,%ebx                ; a ← EAX (consumo de 32 BITS)
call   *%rsi                    ; 2ª chamada — SEM argumentos
mov    %eax,%edi                ; b ← EAX (32 bits)
call   *%rsi                    ; 3ª chamada — SEM argumentos (eax = c)
or     %edi,%ebx                ; a|b (32 bits) — consumo real
mov    %eax,%edx                ; c ← EAX (32 bits)
mov    $0xa,%eax
or     %edx,%ebx                ; (a|b|c) == 0? (32 bits) → falha 10 se != 0
je     ...
```

**Chamada**: sem argumentos — nenhum valor funcional preparado em RCX/RDX/R8/R9 antes dos `call` (a única configuração de registradores observada é a cópia de retornos `mov %eax,…` após cada chamada). **Retorno**: consumido via **EAX** (`mov %eax,%ebx/%edi/%edx`, `or` de 32 bits) — o compilador transformou `a!=0||b!=0||c!=0` em `(a|b|c)!=0`, consumindo os três retornos em operações reais.

## Largura do retorno

Cadeia determinada pelo **consumidor real no disassembly** (§8/§9 — não inferida do tipo `BOOL`):

```text
f_IsDebuggerPresent: uint64_t = 0 (constante)
        ↓
dispatcher: entrega RAX CRU (sem conversão)
        ↓
consumidor (PE, assinatura Win32 BOOL IsDebuggerPresent(VOID)): mov %eax,…
        ↓
BOOL/DWORD de 32 bits (EAX) = bits 31:0 de RAX
```

- O PE consome **EAX** → **retorno efetivamente consumido como DWORD/BOOL de 32 bits**.
- Conversão intermediária: leitura de EAX descarta bits 63:32 (aqui irrelevantes: o valor é constante 0). Sem sign-extension; classe **unsigned/inteiro de 32 bits** (`FALSE` = 0).
- **Handler não alterado** para adequá-lo a expectativa teórica (§9/§12).

## Resultado das chamadas

Três chamadas reais no mesmo processo: **`a == 0`, `b == 0`, `c == 0`** (= `FALSE`/0, exatamente o comportamento do handler atual — §6) e **`a == b == c`** (consistente). Valores consumidos por operações reais (`or`/`je` no assembly + XOR no código-fonte) — nenhuma chamada eliminada pelo compilador. **20/20 execuções retornaram FALSE/0** (§11) — registrado sem transformar em afirmação de compatibilidade geral.

## LastError

### Sucesso
`SetLastError(0xDEAD)` → `IsDebuggerPresent()` → `GetLastError() == 0xDEAD` — **intacto** (comportamento observado; o handler não toca `last_error`).

### Erro
**API não possui caminho de erro exercitado** (não existe caminho de erro no handler — retorno constante puro). Nenhum erro inventado (§10/§12).

## Correção

**Nenhuma alteração no runtime** (§12/§13): nenhuma lógica de detecção real de debugger foi adicionada; `pr_win32.c` intocado.
MD5: **`a0901f348f7482a45288d060469ac386` antes == `a0901f348f7482a45288d060469ac386` depois** (idêntico). Auditoria §17: handler = **1** implementação; catálogo = **1** registro; PE novo presente; sem duplicação; PE sem acesso a internals; nenhuma API não relacionada implementada; nenhuma alteração desnecessária.

## Resultado das 20 execuções

**20/20 com rc=63** (log=33 em todas) — sem crashes, sem `Unsupported Win32 API`, sem variação; comportamento determinístico (`FALSE/0`) em todas as execuções. + 1 execução diagnóstico inicial = 21 execuções bem-sucedidas.

## Regressão

### C
`3398 verificações, 0 falhas` ✓ (baseline G60 mantido; nenhum CHECK novo — desnecessário)

### Swift
`Executed 63 tests, with 0 failures` ✓

### Warnings
`gcc -Wall -Wextra`: **0** novos ✓

### Analyzer
`clang --analyze`: **7 pré-existentes / 0 novos** ✓ (receita idêntica; lista em `docs/RELATORIO_GRUPO49.md` §15)

### PE battery
`15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓ — nenhum PE histórico removido, substituído ou enfraquecido.

## Checklist acumulada

hello_stdhandle = 42 · hello_lstrcpyn = 52 · hello_getenv = 53 · hello_tickcount64 = 54 · hello_tlsgetvalue = 55 · hello_getcommandline = 56 · hello_getcommandlinew = 57 · hello_lstrlen = 58 · hello_lstrcpy = 59 · hello_lstrcmp = 60 · hello_getcurrentprocessid = 61 · hello_getcurrentthreadid = 62 · **hello_isdebuggerpresent = 63** · hello_winsock_ord = 42 · hello_version_systeminfo = 42 · hello_wait_multiple = 42 · hello_thread_timeout = 42 · hello_mutex_timeout = 42 · hello_thread_shared = 42 · hello_thread = 42 · hello_mutex = 42 · hello_cs = 42 · hello_virt = 42 · hello_virt_protect = 42 · hello_virt_query = 42 · hello_qpc = 42 · hello_heap = 42 · hello_mbwc = 42 · hello_file = 42 · hello_file_w = 42 · hello_file_seek = 42 · hello_stdio = 5 · hello_gl12 = 42 · hello_sse = 7 — **34/34**. Revalidação dos checkpoints recentes (§15): lstrcpyn=52 · getenv=53 · tickcount64=54 · tlsgetvalue=55 · getcommandline=56 · getcommandlinew=57 · lstrlen=58 · lstrcpy=59 · lstrcmp=60 · getcurrentprocessid=61 · getcurrentthreadid=62 · **isdebuggerpresent=63** — todos verdes.

## Cobertura efetivamente demonstrada

**PE x64 real → import `IsDebuggerPresent` por nome → loader resolveu → IAT (`call *%rsi`) → chamadas sem argumentos (3×) → `f_IsDebuggerPresent` real executou → retorno em RAX → consumidor real leu EAX (BOOL/DWORD de 32 bits) → comportamento consistente `FALSE (0)` nas três chamadas e em 20 execuções → valores consumidos por operações reais → LastError preservado → CRT → rc=63.** A largura efetiva do retorno foi determinada pelo disassembly do consumidor: **BOOL de 32 bits**.

## Limitações

- O retorno é uma **constante `0`** = "não está sob depuração" — **não** existe detecção real de debugger no Portico; o valor **não mudaria** mesmo sob depuração do host. Não é equivalência com o comportamento do Windows.
- Apenas o caso `FALSE` foi exercitado (o handler nunca produz `TRUE`); a semântica `TRUE` **não** foi demonstrada.
- `CheckRemoteDebuggerPresent` e outras APIs de depuração **não** validadas.
- Não testado com o processo host sob depuração real (irrelevante para o handler atual, mas registrado).
- **Não se declara** compatibilidade geral com Windows, Win32 ou Winlator; **não** se declara compatibilidade com **GTA V**, **MX Bikes** ou qualquer jogo comercial. Somente o comportamento efetivamente demonstrado pelo PE é relatado.

## Próximo candidato

`TerminateProcess` — após analisar o relatório de G63 (passou intacto): o próximo candidato observado é `TerminateProcess` (handler observado com caminho real `ctx->halted`/`ctx->exit_code` e `w32_log`; catálogo `IMPL(…, 8)` = 2 argumentos; exercita largura de DWORD + handle). Alternativas observadas como lacunas reais do catálogo (requerem implementação, estilo G60): `GetModuleFileNameA`, `MultiByteToWideChar`/`WideCharToMultiByte` (ambas `TODO` no catálogo). Nada disso foi validado/avanzado no G63; **G64 não foi avançado**.
