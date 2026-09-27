# RELATÓRIO DA ETAPA — GRUPO 57

## Status

CONCLUÍDO

## API validada

kernel32!GetCommandLineW

## Inventário

1. **Localização do handler**: `f_GetCommandLineW`, `Sources/PorticoRuntime/src/pr_win32.c` L2670.
2. **Registro no catálogo**: `IMPL("kernel32.dll", "GetCommandLineW", f_GetCommandLineW, 0)` (L4829).
3. **Número de argumentos**: 0.
4. **Origem do ponteiro retornado**: `ctx->guest_cmdline_w` (VA do guest; caminho principal com VM); fallback sem VM = `ctx->scratch + 256` (buffer host, conversão sob demanda).
5. **Como a região UTF-16 é criada**: `pr_win32_bind_vm` (L5215) — conversão byte-a-byte com extensão zero (`wb[2*i] = scratch[i]; wb[2*i+1] = 0`), `i < min(slen, 120)`, seguida de NUL wide (`wb[2*i] = wb[2*i+1] = 0`) e `pr_vm_loader_write`.
6. **Onde fica na memória do guest**: `guest_cmdline + 256` (L5229) — página **read-only** `w32static` (0x1000 bytes) alocada no bind.
7. **Tamanho reservado**: 256 bytes por região dentro da página (ANSI em `g`, wide em `g+256`); staging `wb[256]` na escrita; máx. 120 caracteres + NUL wide.
8. **Como a string é terminada**: `w[2*i] = 0; w[2*i+1] = 0` — **NUL wide (0x0000) garantido** na criação; `pr_win32_set_cmdline` (L5260, não chamado por ninguém) também grava `2*(len+1)` bytes com terminador.
9. **Ponteiro persistente**: **sim** — o handler retorna sempre o mesmo `guest_cmdline_w` (identidade entre chamadas confirmada no código e validada no PE).
10. **Teste anterior**: `test_win32x.c` (família FASE 3). Nenhum PE real anterior importa `GetCommandLineW` (varredura completa do G48 + amostras de startup do G56: 0 ocorrências).

## Implementação encontrada

```c
static uint64_t f_GetCommandLineW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)a; (void)n; *st = PR_OK;
    if (ctx->vm && ctx->guest_cmdline_w) return ctx->guest_cmdline_w;
    const char* s = (const char*)ctx->scratch;
    uint8_t* w = ctx->scratch + 256;
    size_t i = 0;
    while (s[i] && i < 120) { w[2 * i] = (uint8_t)s[i]; w[2 * i + 1] = 0; i++; }
    w[2 * i] = 0; w[2 * i + 1] = 0;
    return (uint64_t)(uintptr_t)w;
}
```

Retorno puro; **não toca `ctx->last_error`**. Layout reconfirmado no código real (§2): `guest_cmdline_w = g + 256` — idêntico ao descrito no G56.

## Estado da command line wide

Conteúdo efetivo: **`L"portico"`** = `0x0070 0x006F 0x0072 0x0074 0x0069 0x0063 0x006F 0x0000` (UTF-16LE, 16 bytes), semeado por `pr_win32_bind_vm` a partir do ANSI `"portico"` de `pr_win32_create`. Determinístico para todo PE real.

## PE

`hello_getcommandlinew.exe` — 14.336 bytes. Origem: `realpe/hello_getcommandlinew.c` (criado neste grupo; nenhum PE anterior alterado).

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_getcommandlinew.c -o Tests/PorticoRuntimeTests/data/hello_getcommandlinew.exe` (MinGW-w64 posix).

## Imports

| DLL | Símbolo | Hint | Binding | IAT |
|---|---|---|---|---|
| `KERNEL32.dll` | **`GetCommandLineW`** | `01f3` | **por nome** (`Ordinal <none>`) | `0x140008190` (thunk `jmp *…` na tabela de thunks) |
| `KERNEL32.dll` | `GetCommandLineA` (comparação opcional §12) | `01f2` | por nome | `0x140008188` |
| `KERNEL32.dll` | `SetLastError` (auxiliar §13) | `0554` | por nome | `0x1400081b0` |
| `KERNEL32.dll` | `GetLastError` (auxiliar §13) | `0283` | por nome | `0x140008198` |
| `msvcrt.dll` | CRT de startup | — | por nome | — |

Nenhuma dependência nova além das permitidas; nenhuma API wide auxiliar (`lstrlenW` não usado).

## Execução inicial

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_getcommandlinew.exe` com o **runtime completamente intacto**:

```
exited=1 rc=57 steps=1 exec=477
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 57
```

## Primeiro blocker

Nenhum.

## Diagnóstico de ABI

```asm
mov    0x5ade(%rip),%rsi        ; rsi = IAT[GetCommandLineW] (0x140008190)
call   *%rsi                    ; GetCommandLineW() — SEM argumentos
mov    %rax,%rbx                ; LPWSTR → RBX (move de 64 BITS, sem truncamento)
test   %rax,%rax                ; p != NULL (teste de 64 bits)
...
call   *%rsi                    ; segunda chamada (consistência)
cmp    %rax,%rbx                ; a == b (identidade, 64 bits)
mov    $0xdead,%ecx
call   *…0x1400081b0            ; SetLastError via IAT
call   *%rsi                    ; terceira chamada
call   *…0x140008198            ; GetLastError
cmp    $0xdead,%eax             ; LastError intacto (DWORD, 32 bits)
call   *…0x140008188            ; GetCommandLineA (comparação §12)
```

- **Zero argumentos** (nenhum registrador configurado como parâmetro).
- **Retorno em RAX** tratado como **`LPWSTR` de 64 bits** (`mov %rax,%rbx`, `test %rax,%rax`, `cmp %rax,%rbx`).
- O PE **lê unidades UTF-16 de 16 bits** da memória retornada (varredura `uint16` + comparações de unidades contra `0x0070…0x006F`), com `movzwl`/leituras de palavra no loop gerado.

## Retorno LPWSTR

Entregue em RAX como ponteiro de 64 bits para a memória UTF-16 do guest — sem truncamento em nenhum ponto do caminho.

## Memória do guest

O endereço usado pelo PE veio **exclusivamente** do retorno de `GetCommandLineW()` (nenhum acesso a `guest_cmdline_w` ou `scratch` — proibido e não feito). As unidades `uint16` foram **realmente lidas** da memória do guest (página `w32static`) sem qualquer erro de acesso.

## Conteúdo UTF-16

Validado unidade a unidade: `0x0070, 0x006F, 0x0072, 0x0074, 0x0069, 0x0063, 0x006F` = **`L"portico"`** — o conteúdo determinístico semeado pelo runtime atual. Comparação opcional (§12) com `GetCommandLineA`: ANSI `"portico"` e UTF-16 `L"portico"` **representam o mesmo conteúdo** (8 posições comparadas, incluindo os terminadores) ✓.

## NUL wide

`p[7] == 0x0000` validado explicitamente ✓ + varredura limitada (128 unidades) demonstrando que o terminador wide existe dentro do limite seguro (sem leitura ilimitada).

## Consistência entre chamadas

`a == b` (identidade de ponteiro) — armazenamento persistente confirmado no inventário e mantido; conteúdos idênticos por identidade + validação byte a byte.

## LastError

O handler **não altera `ctx->last_error`** (analisado no inventário). Demonstrado no PE: `SetLastError(0xDEAD)` → `GetCommandLineW()` → `GetLastError() == 0xDEAD` (valor intacto). Isso é evidência do comportamento atual do Portico — **não** se afirma que represente todas as versões do Windows.

## Correção

**Nenhuma alteração no runtime.**
MD5 do runtime: **`3ee3ce898084047420c02e7ef62179c9` antes == `3ee3ce898084047420c02e7ef62179c9` depois** (idêntico). `f_GetCommandLineA` e `guest_cmdline` **intocados** (§16); `hello_getcommandline.exe = rc 56` preservado (verificado na checklist). TLS intocado; `TlsAlloc`/`TlsSetValue`/`lstrlenW`/`lstrcpyW`/`GetEnvironmentVariableW`/`SetEnvironmentVariableA/W` **não** implementadas.

## Resultado

Assertions (57 = sucesso; falhas 10–17):
1. **A — ponteiro**: `w != NULL` ✓ (10)
2. **B — NUL wide com varredura limitada**: terminador encontrado em `len = 7` dentro de 128 unidades ✓ (11)
3. **C — conteúdo UTF-16**: comprimento 7 ✓ (12) e unidades `0x70,0x6F,0x72,0x74,0x69,0x63,0x6F` exatas ✓ (13)
4. **D — NUL wide em p[7]**: `0x0000` ✓ (14)
5. **E — consistência**: `b == w` ✓ (15)
6. **LastError intacto** ✓ (16)
7. **ANSI ↔ UTF-16** (opcional §12): mesmo conteúdo ✓ (17)

## Repetições

**20/20 execuções com rc=57** (log=33 em todas) — sem crashes, sem variações, sem estado residual. + 1 execução diagnóstico inicial = 21 execuções bem-sucedidas.

## Regressão

### C
`3392 verificações, 0 falhas` ✓

### Swift
`Executed 63 tests, with 0 failures` ✓

### Warnings
`gcc -Wall -Wextra`: **0** novos ✓

### Analyzer
`clang --analyze`: **7 pré-existentes / 0 novos** ✓ (mesma receita; lista em `docs/RELATORIO_GRUPO49.md` §15)

### PE battery
`15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓

## Checklist acumulada

hello_stdhandle = 42 · hello_lstrcpyn = 52 · hello_getenv = 53 · hello_tickcount64 = 54 · hello_tlsgetvalue = 55 · hello_getcommandline = 56 · **hello_getcommandlinew = 57** · hello_winsock_ord = 42 · hello_version_systeminfo = 42 · hello_wait_multiple = 42 · hello_thread_timeout = 42 · hello_mutex_timeout = 42 · hello_thread_shared = 42 · hello_thread = 42 · hello_mutex = 42 · hello_cs = 42 · hello_virt = 42 · hello_virt_protect = 42 · hello_virt_query = 42 · hello_qpc = 42 · hello_heap = 42 · hello_mbwc = 42 · hello_file = 42 · hello_file_w = 42 · hello_file_seek = 42 · hello_stdio = 5 · hello_gl12 = 42 · hello_sse = 7 — **28/28; nenhum teste anterior removido; G51–G56 verdes** (G56 confirmado: hello_getcommandline = 56).

## Cobertura efetivamente demonstrada

Um programa x64 real **importou `GetCommandLineW` por nome**, o loader **resolveu o import**, a chamada ocorreu **pela IAT**, o **handler real** executou, o **`LPWSTR` chegou em RAX** como ponteiro de 64 bits, o PE **acessou a memória UTF-16 real do guest** (somente via o retorno da API), **encontrou o terminador wide**, **validou o conteúdo** `L"portico"`, demonstrou **consistência entre chamadas**, **LastError preservado** e a **correspondência ANSI↔UTF-16**, e **continuou normalmente até o CRT** (exit code 57).

## Limitações

- A conversão semeada é **extensão zero de ANSI** (`high byte = 0`); caracteres além de U+00FF/não-ASCII **não** são representados por esse caminho (não há conversão UTF-8/Unicode real) — não validado.
- Conteúdo é a string fixa **`"portico"`** do runtime atual (não reflete `argv` de um Windows real).
- Fallback sem VM (`scratch+256`) não exercitado por PE real.
- APIs do escopo proibido (`TlsAlloc`, `TlsSetValue`, `lstrlenW`, `lstrcpyW`, `GetEnvironmentVariableW`, `SetEnvironmentVariableA/W`, demais) permanecem **não implementadas**.
- **Não se declara** compatibilidade geral com Windows, compatibilidade geral com Win32, nem que jogos comerciais funcionam — **GTA V, MX Bikes ou qualquer outro jogo NÃO são declarados compatíveis**. Somente o comportamento efetivamente demonstrado pelo PE é relatado.

## Próximo passo

`lstrlenA` — irmão direto da `lstrcpynA` (G52); retorno `int` assinado de 32 bits (classe de retorno ainda não validada isoladamente), ponteiro de entrada de 64 bits, assertions exatas (`lstrlenA("")==0`, `lstrlenA("abc")==3`), sem qualquer implementação nova. Alternativas subsequentes: `IsDebuggerPresent` (retorno BOOL), `GetCurrentProcessId`/`GetCurrentThreadId` (retorno DWORD), `lstrcpyA`. Nada disso foi implementado no G57; **G58 não foi avançado**.
