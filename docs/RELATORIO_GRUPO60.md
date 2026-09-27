# RELATÓRIO DA ETAPA — GRUPO 60

## Status

CONCLUÍDO

## API validada

kernel32!lstrcmpA

## Lacuna encontrada

Confirmada por varredura somente-leitura antes de qualquer alteração (§3):
- `lstrcmpA`, `f_lstrcmpA`, `lstrcmpW`: **0 ocorrências** em `Sources/`, `Tests/`, `realpe/`, `tools/` — sem declaração, sem protótipo, sem handler, sem registro no catálogo, sem teste unitário, sem PE que importasse o símbolo.
- Estado da família ANSI no inventário: `lstrlenA` (validada G58) · `lstrcpyA` (validada G59) · `lstrcpynA` (validada G52) · **`lstrcmpA` = lacuna real**.
- Auxiliar de comparação existente: `f_msvcrt_strncmp` (pr_win32.c L1003) — semântica `strncmp` limitada (msvcrt), **não** reaproveitada (API distinta; manter o menor handler possível). Os `strcmp` do código (L465, L1730+) operam sobre ponteiros do host (nomes de módulo) — não reutilizáveis para memória do guest.
- Lógica reutilizável de `f_lstrlenA`/`f_lstrcpyA`: o padrão de varredura limitada com `pr_win32_ptr` — adaptado ao novo handler.

## Inventário

1. Convenção da família (mantida): `f(ctx, args, nargs, st)` com retorno `uint64_t`.
2. `args[0] = lpString1`, `args[1] = lpString2` (RCX/RDX na ABI do PE).
3. `pr_win32_ptr(ctx, addr, len)`: `addr == 0` → NULL determinístico (L259) — base do tratamento de NULL.
4. Família no catálogo: `lstrlenA(4)`, `lstrcpyA(8)`, `lstrcpynA(12)` (L4809–4811) — nova entrada segue a mesma convenção (`8` = 2 argumentos).
5. Ponto de inserção: logo após `f_lstrcpynA` (fim em L855), antes do bloco de sincronização/TLS.
6. Nenhum PE existente importava `lstrcmpA` (varredura por `objdump -p` sobre os 43 PEs = 0).

## Implementação

Inserida após `f_lstrcpynA` (adição pura; nenhum handler existente foi alterado):

```c
static uint64_t f_lstrcmpA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    const char* q = (const char*)pr_win32_ptr(ctx, a[1], 1);
    if (!p || !q) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t i = 0;
    for (;;) {
        if (!pr_win32_ptr(ctx, a[0] + i, 1) || !pr_win32_ptr(ctx, a[1] + i, 1)) {
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return 0;
        }
        unsigned char c1 = (unsigned char)p[i];
        unsigned char c2 = (unsigned char)q[i];
        if (c1 != c2) return (uint64_t)(int64_t)((int)c1 - (int)c2);
        if (c1 == 0) return 0;
        i++;
    }
}
```

Retorno de `int` em `uint64_t`: o valor `(int)c1 - (int)c2` é estendido com sinal via `(uint64_t)(int64_t)`; o dispatcher entrega RAX cru e o PE consome **EAX como `int`** (32 bits dois-complemento) — verificado na ABI real (`test %eax,%eax`, `jns`), sem conversão arbitrária.

## Registro no catálogo

```c
    IMPL("kernel32.dll", "lstrlenA", f_lstrlenA, 4),
    IMPL("kernel32.dll", "lstrcpyA", f_lstrcpyA, 8),
    IMPL("kernel32.dll", "lstrcpynA", f_lstrcpynA, 12),
    IMPL("kernel32.dll", "lstrcmpA", f_lstrcmpA, 8),   /* NOVO — G60 */
```

**2 argumentos** (`stdcall_bytes = 8`), exatamente a convenção das irmãs. Registros existentes intocados.

## Algoritmo de comparação

Byte a byte (`unsigned char`), parando no primeiro par diferente e nunca acessando byte após o primeiro NUL: se `c1 != c2` retorna a **fórmula concreta `(int)c1 − (int)c2`** (faixa −255..255, sinal real); se ambos são `0` simultaneamente retorna `0` (fim simultâneo); `respeita o primeiro NUL`. Cada byte é validado por `pr_win32_ptr` antes da leitura (segurança da memória do guest; falha de validação → `last_error = 87` + retorno 0, sem acesso inválido). Comparação **não** usa locale (como `strcmp` padrão) — ver Limitações.

## Tratamento de NULL

Caminhos determinísticos por construção na infraestrutura existente: `pr_win32_ptr(0) → NULL` → `!p` (lpString1 NULL) ou `!q` (lpString2 NULL) → **retorno 0** + `last_error = 87`. Atenção: o retorno 0 coincide com "iguais"; a distinção é feita por `GetLastError()`. Validado nos dois casos no PE (contrato 19) e registrado como **comportamento observado do Portico** — não é contrato universal de Windows (em Windows real NULL não é garantido e pode falhar).

## PE

`hello_lstrcmp.exe` — 15.872 bytes. Origem: `realpe/hello_lstrcmp.c` (criado neste grupo; nenhum PE anterior alterado). Sem acesso a `pr_win32_ctx`, `f_lstrcmpA`, `pr_win32_call` ou qualquer estrutura interna — apenas a importação normal por DLL/IAT.

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_lstrcmp.c -o Tests/PorticoRuntimeTests/data/hello_lstrcmp.exe` (MinGW-w64 posix GCC 14).

## Imports

| DLL | Símbolo | Hint | Binding | IAT |
|---|---|---|---|---|
| `KERNEL32.dll` | **`lstrcmpA`** | `0661` | **por nome** (`Ordinal <none>`) | `0x1400081c8` |
| `KERNEL32.dll` | `SetLastError` (auxiliar §9) | `0554` | por nome | `0x140008198` |
| `KERNEL32.dll` | `GetLastError` (auxiliar §9) | `0283` | por nome | `0x140008180` |
| `msvcrt.dll` | CRT de startup | — | por nome | — |

Somente auxiliares já validados em G51–G59; nenhuma dependência nova.

## Execução inicial

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_lstrcmp.exe` após a implementação mínima e reparo (ver §Primeiro blocker):

```
exited=1 rc=60 steps=1 exec=1122
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 60
```

## Primeiro blocker

1. **Lacuna de implementação (esperada e comprovada)**: antes de existir o handler, o runtime parou com o protocolo honesto: `prepare falhou — Unsupported Win32 API | KERNEL32.dll | lstrcmpA | EXECUTION STOPPED | "API conhecida do módulo mas sem implementação"` — evidência de que a lacuna era real (nada foi mascarado).
2. **Corrupção de ferramenta durante a implementação** (transparência total): duas edições concorrentes no mesmo arquivo (`pr_win32.c`) corromperam o catálogo — o bloco `TODO(MultiByteToWideChar)…CreateWindowExA/ShowWindow` foi **duplicado** com um splice no meio da linha do `ShowWindow` (L4852–4858) — e a inserção do handler foi perdida (compilação falhou com `use of undeclared identifier 'f_lstrcmpA'`). **Reparo mínimo e verificável**: cirurgia por linhas com asserts (L4852–4858 → uma única linha `IMPL_NOTE("user32.dll", "ShowWindow", … "envia WM_SHOWWINDOW")` idêntica ao original; handler reinserido por âncora única) — verificado por contagens (`MessageBoxA` no catálogo = 1; `f_lstrcmpA` = 2 refs; `envia WM_SHOWWINDOW` = 1) e pela compilação limpa seguinte. Nenhuma alteração semântica fora de `lstrcmpA`.
3. Depois do reparo: **rc=60 direto na primeira execução** — nenhum blocker de ABI/memória/retorno/CRT.

## Diagnóstico de ABI

```asm
lea    0x65(%rsp),%rdx        ; lpString2 → RDX
lea    0x64(%rsp),%rcx        ; lpString1 → RCX
call   *%rbx                  ; lstrcmpA via IAT (rbx = IAT[0x1400081c8])
test   %eax,%eax              ; int em EAX — teste de igualdade (== 0)
je     ...
...
mov    %rbp,%rdx
mov    %r12,%rcx              ; segundo argumento → RCX
call   *%rbx
mov    %eax,%edx              ; mov de 32 bits = int
mov    $0xb,%eax
test   %edx,%edx
jns    ...                    ; teste de MENOR com SINAL (< 0) em 32 bits
```

Cadeia demonstrada: **ponteiro 64-bit → RCX/RDX → handler → `int` sinalizado em EAX** (`test %eax,%eax` para igualdade; `test %edx,%edx` + `jns` para `< 0` = interpretado como **inteiro assinado de 32 bits**; `> 0` equivalente). Sinais exigidos apenas como `== 0` / `< 0` / `> 0` (§7) — nenhum valor numérico fixo é exigido pelo PE.

## Argumento lpString1

Chegou em **RCX** (`lea …,%rcx` / `mov %r12,%rcx`) como ponteiro de 64 bits para buffers locais reais do PE (memória do guest); o handler leu os bytes via `pr_win32_ptr` (tradução guest).

## Argumento lpString2

Chegou em **RDX** (`lea …,%rdx` / `mov %rbp,%rdx`) como ponteiro de 64 bits para as segundas strings; comparado byte a byte contra `lpString1` na memória real do guest.

## Retorno int

Entregue em RAX pelo handler (uint64 cru com `(int)c1−(int)c2` estendido com sinal) e consumido pelo PE como **`int` de 32 bits** (`int r`; `test %eax,%eax`; `jns`), sem conversão para sem sinal. Fórmula/valor concreto do handler: **`diferença (int)c1 − (int)c2`** (faixa −255..255; ex.: `'a'−'b' = −1`); o contrato validado é apenas o sinal.

## Assertions

| # | Assertion | Esperado | Resultado |
|---|---|---|---|
| 1 (10) | `lstrcmpA("abc","abc")` | `== 0` | ✓ |
| 2 (10) | `lstrcmpA("","")` | `== 0` | ✓ |
| 3 (11) | `"abc"` vs `"abd"` | `< 0` | ✓ |
| 4 (12) | `"abd"` vs `"abc"` | `> 0` | ✓ |
| 5 (13) | `"abc"` vs `"abcd"` (prefixo) | `< 0` | ✓ |
| 6 (13) | `"abcd"` vs `"abc"` (prefixo inverso) | `> 0` | ✓ |
| 7 (14) | `"apple"` vs `"banana"` (1º byte) | `< 0` | ✓ |
| 8 (15) | `"PorticoA"` vs `"PorticoB"` (diferença tardia) | `< 0` | ✓ |
| 9 (16) | `"abc\0xyz"` vs `"abc\0other"` (NUL interno) | `== 0` (para no 1º NUL) | ✓ |
| 10 (17) | buffers originais após todas as chamadas | byte a byte intactos (todos os pares verificados) | ✓ |
| 11 (18) | `SetLastError(0xDEAD); lstrcmpA("abc","abc"); GetLastError()` | `0xDEAD` (intacto) | ✓ |
| §8 (19) | `lstrcmpA(NULL, s)` e `lstrcmpA(s, NULL)` | retorno `0` **e** `GetLastError() == 87` nos dois | ✓ |

## LastError

- **Sucesso**: `last_error` **não** tocado — `0xDEAD` preservado (assertion 11).
- **Erro** (argumento NULL/inválido): `last_error = 87` (`W32_ERROR_INVALID_PARAMETER`) — documentado e testado; **não** alterado artificialmente para passar em teste.
- Observação do Portico; sem equivalência universal com Windows.

## Correção

Implementação do zero era o objetivo do grupo (§5). Alterações no runtime — **somente**:
1. `f_lstrcmpA` (nova, ~22 linhas, após `f_lstrcpynA`);
2. `IMPL("kernel32.dll", "lstrcmpA", f_lstrcmpA, 8),` (1 linha nova no catálogo);
3. reparo do dano de ferramenta descrito em §Primeiro blocker (restauração byte a byte do original; sem mudança semântica).

MD5: **`3ee3ce898084047420c02e7ef62179c9` antes → `a0901f348f7482a45288d060469ac386` depois** (mudança esperada e mínima — §19). Nenhuma outra API implementada/promovida: `lstrcmpW`/`lstrncmpA`/`lstrncmpW`/`lstrlenW`/`lstrcpyW`/`lstrcpynW`/`TlsAlloc`/`TlsSetValue`/etc. permanecem ausentes (`TlsAlloc` segue como `TODO` no catálogo).

## Auditoria de escopo

Nenhuma alteração não relacionada permaneceu após o reparo. Preservados e **reexecutados verdes**: `lstrlenA` (hello_lstrlen=58), `lstrcpyA` (hello_lstrcpy=59), `lstrcpynA` (hello_lstrcpyn=52), `TlsGetValue` (hello_tlsgetvalue=55), `GetCommandLineA` (hello_getcommandline=56), `GetCommandLineW` (hello_getcommandlinew=57) + todos os demais PEs históricos (checklist completa abaixo). Testes unitários: **somente adição** de 6 CHECKs de `lstrcmpA` em `test_win32.c` (nenhum teste existente modificado/removido).

## Resultado

Todas as assertions (1–11 + par NULL) passaram; exit code **60**; falhas 10–19 nenhuma disparada. Teste unitário novo mínimo (igualdade/menor/maior via `pr_win32_call`) verde.

## Repetições

**20/20 execuções com rc=60** (log=45 em todas) — sem crashes, sem variação, sem corrupção, sem estado residual. + 1 execução diagnóstico = 21 execuções bem-sucedidas.

## Regressão

### C
`3398 verificações, 0 falhas` — **novo baseline** (3392 + **6** CHECKs novos legítimos de `lstrcmpA` em `test_win32.c`; §15). Nenhum teste anterior removido.

### Swift
`Executed 63 tests, with 0 failures` ✓

### Warnings
`gcc -Wall -Wextra`: **0** novos ✓ (compilação limpa também no rebuild dos tools)

### Analyzer
`clang --analyze`: **7 pré-existentes / 0 novos** ✓ (receita idêntica; lista em `docs/RELATORIO_GRUPO49.md` §15)

### PE battery
`15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓

## Checklist acumulada

hello_stdhandle = 42 · hello_lstrcpyn = 52 · hello_getenv = 53 · hello_tickcount64 = 54 · hello_tlsgetvalue = 55 · hello_getcommandline = 56 · hello_getcommandlinew = 57 · hello_lstrlen = 58 · hello_lstrcpy = 59 · **hello_lstrcmp = 60** · hello_winsock_ord = 42 · hello_version_systeminfo = 42 · hello_wait_multiple = 42 · hello_thread_timeout = 42 · hello_mutex_timeout = 42 · hello_thread_shared = 42 · hello_thread = 42 · hello_mutex = 42 · hello_cs = 42 · hello_virt = 42 · hello_virt_protect = 42 · hello_virt_query = 42 · hello_qpc = 42 · hello_heap = 42 · hello_mbwc = 42 · hello_file = 42 · hello_file_w = 42 · hello_file_seek = 42 · hello_stdio = 5 · hello_gl12 = 42 · hello_sse = 7 — **31/31; nenhum PE anterior removido ou substituído** (G51–G59 verdes).

## Cobertura efetivamente demonstrada

**PE x64 real → import `lstrcmpA` por nome → loader resolveu → IAT (`call *%rbx`) → RCX/RDX reais → `f_lstrcmpA` executou → comparação byte a byte na memória real do guest (parando no primeiro NUL) → retorno `int` sinalizado consumido em EAX (`==0`/`<0`/`>0`) → buffers intactos → LastError preservado/definido nos caminhos corretos → CRT → rc=60.** Igualdade, menor, maior, prefixos, NUL interno e os dois caminhos NULL demonstrados.

## Limitações

- **NULL**: comportamento observado do Portico (retorno `0` + error `87`); o retorno `0` é ambíguo com "iguais" — a distinção é por `GetLastError()`. Não é contrato universal de Windows.
- **Memória inválida além de NULL** (ponteiros arbitrários não mapeados) não testada em PE — pelo código, a validação `pr_win32_ptr` falha → error `87` + retorno 0 sem acesso inválido.
- **Strings extremamente grandes** não testadas (varredura limitada byte a byte por validação; sem limite artificial de tamanho).
- **Bytes acima de ASCII (0x80–0xFF)** não exercitados no PE; a comparação é **byte a byte unsigned** — **não** é comparação por locale/regras linguísticas (ex.: ordem alfabética acentuada pode diferir de comparações sensíveis a locale do Windows). Não há suporte a locale nesta API.
- APIs wide (`lstrcmpW`, `lstrlenW`, `lstrcpyW`, `lstrcpynW`) e irmãs (`lstrncmpA`/`lstrncmpW`; `f_msvcrt_strncmp` existe em msvcrt mas `lstrncmpA` de kernel32 não) **não** implementadas/validadas. `TlsAlloc`/`TlsSetValue` seguem **não** implementados.
- **Não se declara** compatibilidade geral com Windows, Win32 ou Winlator; **não** se declara compatibilidade com **GTA V**, **MX Bikes** ou qualquer jogo comercial. Somente o comportamento efetivamente demonstrado pelo PE é relatado.

## Próximo passo

`GetCurrentProcessId` (e/ou `GetCurrentThreadId`) — **observado no inventário deste grupo**: os handlers `f_GetCurrentProcessId`/`f_GetCurrentThreadId` já existem no catálogo (`IMPL(…, 0)` args, L4814–4815) e **nenhum PE real os validou** (retorno DWORD de 32 bits = classe a confirmar pelo caminho do PE, como no G51). Alternativa subsequente observada: `IsDebuggerPresent` (verificar presença de handler no inventário do próximo grupo). Nada disso foi implementado/validado no G60; **G61 não foi avançado**.
