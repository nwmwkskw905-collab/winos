# RELATÓRIO DA ETAPA — GRUPO 64

## Status

CONCLUÍDO

## API validada

kernel32!TerminateProcess

## Inventário

1. **Busca completa** por `TerminateProcess`/`f_TerminateProcess` em `Sources/`, `Tests/`, `realpe/`, `tools/` (código atual, antes de qualquer alteração): **3 ocorrências** — definição do handler, `w32_log` interno e registro no catálogo. **Nenhum** teste unitário, **nenhum** PE, **nenhuma** ferramenta, **nenhum** uso indireto.
2. **Arquivo/linhas**: `Sources/PorticoRuntime/src/pr_win32.c` **L1708–1713**.
3. **Assinatura interna**: `f(ctx, args, nargs, st)`; **2 argumentos** (`args[0]` = `hProcess`, `args[1]` = `uExitCode`).
4. **Uso de `ctx`**: `ctx->exit_code` (grava), `ctx->halted` (grava 1), `w32_log` (emite `PR_LOG_INFO "TerminateProcess(code=%u)"`).
5. **`LastError`**: **não** toca `ctx->last_error`; nenhum caminho de erro.
6. **Retorno**: `uint64_t` = `1` (`BOOL TRUE`); `*st = PR_OK` incondicional.
7. **Registro no catálogo**: `IMPL("kernel32.dll", "TerminateProcess", f_TerminateProcess, 8)` (**L4814**) — `stdcall_bytes = 8` = **2 argumentos × 4 bytes**; registro único.
8. **Tratamento de `halted` no dispatcher/execução** (confirmado no código atual): `pr_win32_halted(ctx)` (L5191 = `ctx->halted`) é consultado no loop de execução do PE (`pr_peproc.c` L179/274/1195) e na porta de chamadas (L2108: `if (pr_win32_halted(ctx)) return 1;`) → **parada real da execução** após a chamada.
9. **PEs existentes**: varredura `objdump -p` sobre todos os PEs = **nenhum importa `TerminateProcess`** — PE novo exclusivo para G64.

## Implementação encontrada

```c
static uint64_t f_TerminateProcess(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    ctx->exit_code = n >= 2 ? (uint32_t)a[1] : 0;
    ctx->halted = 1;
    w32_log(ctx, PR_LOG_INFO, "TerminateProcess(code=%u)", ctx->exit_code);
    return 1;
}
```

## Registro no catálogo

```c
    IMPL("kernel32.dll", "TerminateProcess", f_TerminateProcess, 8),
```

Único; `8 bytes = 2 argumentos` confirmado; registro já correto — **não alterado**.

## PE criado

`hello_terminateprocess.exe` — 14.336 bytes. Origem: `realpe/hello_terminateprocess.c` (criado neste grupo; nenhum PE histórico substituído/alterado). x86-64 MinGW-w64; importa `KERNEL32.dll!TerminateProcess`; chamada real pela IAT; **nenhum** acesso a internals do Portico (auditoria §20: 0 referências). Conforme §6, as asserções do contrato ficam **antes** da chamada (`SetLastError(0xDEAD)` → smoke `GetLastError()==0xDEAD` → falha 10) e a chamada de `TerminateProcess` é o **evento final** (código de falha 11 existe apenas para detectar um retorno indevido — nunca executado).

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_terminateprocess.c -o Tests/PorticoRuntimeTests/data/hello_terminateprocess.exe` (MinGW-w64 posix GCC 14).

## Imports

| DLL | Símbolo | Hint | Binding | IAT |
|---|---|---|---|---|
| `KERNEL32.dll` | **`TerminateProcess`** | `05b3` | **por nome** (`Ordinal <none>`) | `0x1400081b0` |
| `KERNEL32.dll` | `SetLastError` (smoke §13) | `0554` | por nome | `0x140008198` |
| `KERNEL32.dll` | `GetLastError` (smoke §13) | `0283` | por nome | `0x140008180` |
| `msvcrt.dll` | CRT de startup | — | por nome | — |

Somente auxiliares já validados (G51–G63); **nenhuma** API extra importada (em particular, `GetCurrentProcess` **não** foi necessária — §7).

## Execução inicial

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_terminateprocess.exe` com o **runtime completamente intacto**:

```
exited=1 rc=64 steps=1 exec=299
PROCESS EXIT
Reason: ExitProcess
Module: kernel32.dll
Function: TerminateProcess
Address: 0x00FF76BD
Architecture: x86-64
Exit code: 64
```

Razão de saída atribuída pelo harness a **`kernel32.dll!TerminateProcess`**; mensagem `w32_log` `TerminateProcess(code=64)` emitida (contagem `log=29` estável). Sem `Unsupported Win32 API`; sem crash; sem comportamento inesperado.

## Primeiro blocker

Nenhum.

## Diagnóstico de ABI

```asm
; main (0x140002680):
call   0x140001500                  ; CRT init
mov    $0xdead,%ecx                 ; smoke: SetLastError(0xDEAD) — DWORD em ECX
call   *0x5b04(%rip)  # 0x140008198 ; SetLastError via IAT
call   *0x5ae6(%rip)  # 0x140008180 ; GetLastError via IAT
mov    %eax,%edx
cmp    $0xdead,%edx                 ; smoke: ambiente funcional? (falha 10)
...
mov    $0x40,%edx                   ; *** RDX = 0x40 = 64 = uExitCode ***
or     $0xffffffffffffffff,%rcx     ; *** RCX = -1 = (HANDLE)-1 = hProcess ***
call   *0x5af3(%rip)  # 0x1400081b0 ; *** TerminateProcess via IAT ***
mov    $0xb,%eax                    ; (código 11 de retorno indevido — gerado pelo compilador;
jmp    ...ret                       ;  NUNCA executado em qualquer das 21 execuções)
```

- **Primeiro argumento (RCX = `hProcess`)**: `or $0xffffffffffffffff,%rcx` = `(HANDLE)-1` — pseudo-handle padrão do processo atual (valor de `GetCurrentProcess()`), usado diretamente sem import extra (§7).
- **Segundo argumento (RDX = `uExitCode`)**: `mov $0x40,%edx` = **64** (decimal), DWORD de 32 bits.
- **Retorno**: o compilador gerou o caminho `return 11` após a chamada (controle de fluxo); em execução real **esse caminho nunca ocorreu** — registrando somente o que realmente acontece (§10).

## Tratamento de `hProcess`

**O handler IGNORA completamente `args[0]`** — o valor nunca é lido, validado (`pr_win32_ptr` não é usado) ou comparado; não há semântica de handle no caminho atual. Qualquer valor passado leva à terminação do processo guest corrente. O PE passou `(HANDLE)-1` por ser o valor semanticamente apropriado (pseudo-handle), mas o resultado seria idêntico com qualquer outro valor. Sem validação de handle → registrado em Limitações (§12).

## Tratamento de `uExitCode`

`ctx->exit_code = n >= 2 ? (uint32_t)a[1] : 0;` — **truncamento DWORD de 32 bits** do segundo argumento. Cadeia completa comprovada (§8/§11):

```text
RDX = 64 (0x40) → a[1] = 64 → ctx->exit_code = (uint32_t)64 = 64
→ ctx->halted = 1 → w32_log("TerminateProcess(code=64)")
→ loop pr_peproc detecta pr_win32_halted() e para a execução
→ harness reporta "Module: kernel32.dll — Function: TerminateProcess — Exit code: 64"
→ rc observado = 64
```

**`ctx->exit_code` (64) == rc observado pelo harness (64)** — sem diferença entre os valores; o exit code **64** é exclusivo de G64 e claramente distinto das falhas pré-chamada (10) e de retorno indevido (11).

## Caminho `ctx->halted`/parada

`ctx->halted = 1` (L1711) → `pr_win32_halted()` (L5191) → consultado em `pr_peproc.c` (L179, L274, L1195) e na porta de chamadas de API (L2108) → **parada real da execução do PE imediatamente após a chamada** (não há execução de código posterior do guest; `steps=1`, `exec=299` determinísticos). A ausência de código posterior **não** é tratada como falha (§14) — é o resultado esperado e foi observado em 100% das execuções.

## LastError

- Smoke **antes** da chamada: `SetLastError(0xDEAD)` → `GetLastError() == 0xDEAD` — ambiente funcional (contrato 10).
- **LastError pós-TerminateProcess não observável** (§13) — a chamada encerra a execução; o handler não toca `last_error` e não possui caminho de erro, e **nenhuma chamada posterior foi adicionada** para recuperar estado que a própria API encerra.

## Correção

**Nenhuma alteração no runtime** (§15): `pr_win32.c` intocado.
MD5: **`a0901f348f7482a45288d060469ac386` antes == `a0901f348f7482a45288d060469ac386` depois** (idêntico). Auditoria §20: handler = **1** implementação; catálogo = **1** registro; PE novo presente; sem duplicação; PE sem internals; nenhuma API não relacionada implementada; nenhum PE histórico substituído.

## Resultado das 20 execuções

**20/20 com rc=64** (log=29 em todas) — terminação correta pelo mecanismo real do Portico (`halted` + `ctx->exit_code`), sem crash, sem `Unsupported Win32 API`, sem variação. + 1 execução diagnóstico inicial = 21 execuções bem-sucedidas; em **nenhuma** o código após a chamada executou.

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
`15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓ — nenhum PE removido, substituído ou alterado.

## Checklist acumulada

hello_stdhandle = 42 · hello_lstrcpyn = 52 · hello_getenv = 53 · hello_tickcount64 = 54 · hello_tlsgetvalue = 55 · hello_getcommandline = 56 · hello_getcommandlinew = 57 · hello_lstrlen = 58 · hello_lstrcpy = 59 · hello_lstrcmp = 60 · hello_getcurrentprocessid = 61 · hello_getcurrentthreadid = 62 · hello_isdebuggerpresent = 63 · **hello_terminateprocess = 64** · hello_winsock_ord = 42 · hello_version_systeminfo = 42 · hello_wait_multiple = 42 · hello_thread_timeout = 42 · hello_mutex_timeout = 42 · hello_thread_shared = 42 · hello_thread = 42 · hello_mutex = 42 · hello_cs = 42 · hello_virt = 42 · hello_virt_protect = 42 · hello_virt_query = 42 · hello_qpc = 42 · hello_heap = 42 · hello_mbwc = 42 · hello_file = 42 · hello_file_w = 42 · hello_file_seek = 42 · hello_stdio = 5 · hello_gl12 = 42 · hello_sse = 7 — **35/35**. Revalidação dos checkpoints recentes (§18): lstrcpyn=52 · getenv=53 · tickcount64=54 · tlsgetvalue=55 · getcommandline=56 · getcommandlinew=57 · lstrlen=58 · lstrcpy=59 · lstrcmp=60 · getcurrentprocessid=61 · getcurrentthreadid=62 · isdebuggerpresent=63 · **terminateprocess=64** — todos verdes.

## Cobertura efetivamente demonstrada

**PE x64 real → import `TerminateProcess` por nome → loader resolveu → IAT (`call *… # 0x1400081b0`) → RCX = `(HANDLE)-1` (hProcess) e RDX = `64` (uExitCode) preparados pelo PE → `f_TerminateProcess` real executou → `ctx->exit_code = 64` + `ctx->halted = 1` + `w32_log` → mecanismo real de parada (`pr_win32_halted` no loop `pr_peproc`) interrompeu a execução imediatamente após a chamada → harness observou rc = 64 atribuído a `kernel32.dll!TerminateProcess` → 20/20 execuções idênticas.** Dois argumentos confirmados na ABI real; exit code do PE = valor enviado em RDX (sem diferença).

## Limitações

- **`hProcess` é ignorado** pelo handler — **sem validação de handle**, sem erro para handle inválido, sem capacidade de terminar outro processo (sempre o processo guest corrente). Em Windows real o handle é validado e pode falhar (`ERROR_ACCESS_DENIED`, etc.) — não é equivalência.
- `uExitCode` é truncado em **DWORD de 32 bits** (`(uint32_t)a[1]`).
- **Retorno (`TRUE`) e LastError pós-chamada não observáveis** (a terminação impede execução posterior).
- O código após a chamada existe no binário por controle de fluxo do compilador (contrato 11 = retorno indevido) mas **nunca** foi observado em execução.
- Não testado: terminação de outros processos, múltiplos processos, interação com threads em execução.
- **Não se declara** compatibilidade geral com Windows, Win32 ou Winlator; **não** se declara compatibilidade com **GTA V**, **MX Bikes** ou qualquer jogo comercial. Somente o comportamento efetivamente demonstrado pelo PE é relatado.

## Próximo candidato

`GetModuleFileNameA` — após analisar o relatório de G64 (passou intacto): candidata observada como lacuna real do catálogo desde o G60 (`TODO("kernel32.dll", "GetModuleFileNameA", "caminho do módulo no FS do convidado")`), requer implementação mínima (estilo G60) e valida retorno DWORD de contagem + escrita em buffer. Alternativas observadas: `MultiByteToWideChar`/`WideCharToMultiByte` (TODOs reais no catálogo; família de conversão de código de página). Nada disso foi implementado/validado no G64; **G65 não foi avançado**.
