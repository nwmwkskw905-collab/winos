# RELATÓRIO DA ETAPA — GRUPO 53

## Status

**CONCLUÍDO** — `GetEnvironmentVariableA` validada por PE x64 real (`hello_getenv.exe`, rc de sucesso **53**), import por nome, chamada real via IAT, cenários A/B/C/D + lookup case-insensitive. **Nenhum blocker**; **nenhuma alteração no runtime** (MD5 antes == depois). Ambiente de teste determinístico semeado pela infraestrutura existente (`pr_win32_env_set`), sem sistema novo de environment.

## API validada

`kernel32!GetEnvironmentVariableA(LPCSTR lpName, LPSTR lpBuffer, DWORD nSize)`.

## Inventário

Escolhida como "próximo passo" recomendado no G52. Confirmado no código existente antes de qualquer teste:
- Handler `f_GetEnvironmentVariableA` (`pr_win32.c`), registro `IMPL_NOTE("kernel32.dll", "GetEnvironmentVariableA", f_GetEnvironmentVariableA, 12, "ambiente do processo (Portico)")` — **já implementada**; reutilizada sem alteração.
- Comportamento: busca case-insensitive (`env_ieq`) em `env_names[8][64]`/`env_vals[8][128]`/`env_count`; inexistente → `ERROR_ENVVAR_NOT_FOUND (203)` + retorno 0; buffer ausente/curto → `ERROR_INSUFFICIENT_BUFFER (122)` + retorno do tamanho **com** NUL; buffer suficiente → cópia byte-exata + retorno do tamanho **sem** NUL. Os códigos 203/122 são os valores **públicos** do Win32 (`ERROR_ENVVAR_NOT_FOUND`/`ERROR_INSUFFICIENT_BUFFER` em `winerror.h`).
- Unit-tests: `test_win32x.c` (cenários A/B/C/W via `pr_win32_env_set(ctx, "FASE3_VAR", ...)` + `pr_win32_call`).
- Infra de env: **`pr_win32_env_set(ctx, name, value)`** — API pública já suportada (usada pelos unit-tests); `pr_host_start_info` também aceita `env_keys/env_vals/env_count`.
- `GetLastError`/`SetLastError` relacionados: implementados (`IMPL`), usados neste PE apenas como **auxiliares de assertion** do contrato de erro da própria API (§11.8 do briefing).

## PE

- **Nome**: `hello_getenv.exe` — **14.848 bytes**.
- **Origem**: `realpe/hello_getenv.c` (criado neste grupo; nenhum PE anterior alterado).

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_getenv.c -o Tests/PorticoRuntimeTests/data/hello_getenv.exe` (MinGW-w64 posix).

## Imports (`x86_64-w64-mingw32-objdump -p`)

| DLL | Símbolo | Hint | Ordinal | Binding |
|---|---|---|---|---|
| `KERNEL32.dll` | **`GetEnvironmentVariableA`** | `0258` | `<none>` | **por nome** |
| `KERNEL32.dll` | `GetLastError` (auxiliar de assertion) | `0283` | `<none>` | por nome |
| `KERNEL32.dll` | `SetLastError` (auxiliar de assertion) | `0554` | `<none>` | por nome |
| `msvcrt.dll` | CRT de startup | — | `<none>` | por nome |

Import **por nome confirmado**; chamada real provada pelo caminho completo: `hello_getenv.exe` → PE x64 → loader → import resolver → `KERNEL32.dll` → `GetEnvironmentVariableA` → handler real → retorno → asserções no PE → CRT → exit code (sem mock, sem chamada direta de handler).

## Execução inicial (runtime intacto)

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_getenv.exe`:

```
exited=1 rc=53 steps=1 exec=437
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 53
```

Resultado: **passou direto** (fluxo completo) — import resolvido, handler correto atingido (log do processo), sem crash.

## Primeiro blocker

**Nenhum.**

## Diagnóstico de ABI (call sites reais, `objdump -d`, antes de qualquer correção)

```
; cenário A: lpName em RCX, lpBuffer em RDX, nSize em R8D (32 bits, zero-extendido)
mov    $0x40,%r8d        ; nSize = 64 (DWORD) → R8D
mov    %rbp,%rcx         ; lpName  ("G53_VAR")
mov    %rbx,%rdx         ; lpBuffer
call   *%rsi             ; via IAT (0x140008180)

; cenário C: nSize = 8
mov    $0x8,%r8d
mov    %rbp,%rcx
mov    %rbx,%rdx
call   *%rsi

; cenário D: lpBuffer = NULL, nSize = 0
xor    %edx,%edx         ; lpBuffer = NULL (RDX = 0)
xor    %r8d,%r8d         ; nSize = 0
mov    %rbp,%rcx
call   *%rsi
```

- **RCX** = `lpName` (ponteiro 64 bits); **RDX** = `lpBuffer` (ponteiro 64 bits ou 0); **R8** = `nSize` **`DWORD` de 32 bits** carregado por `r8d` — **extensão zero** de 32→64 (`mov $imm,%r8d` / `xor %r8d,%r8d`).
- **Retorno**: `DWORD` em EAX/RAX (bits baixos) — comprimento consumido corretamente pelo PE.
- O handler lê `args[2]` como `(size_t)` (64 bits crus); com a entrega zero-extendida da ABI real, todos os valores `DWORD` (incluindo 0) são coerentes — **nenhuma incompatibilidade de largura observada** (a classe de defeito do G51 foi investigada e está ausente neste caminho).

## Correção

**Nenhuma alteração no runtime.**
- MD5 do runtime (`Sources/PorticoRuntime/src/*.c` + `include/portico/*.h`): **`3ee3ce898084047420c02e7ef62179c9` antes == `3ee3ce898084047420c02e7ef62179c9` depois** (idêntico).
- Infraestrutura de teste (não é runtime): `tools/dbg_input.c` e `tools/dbg_diag.c` receberam **1 chamada** cada a `pr_win32_env_set(pr_peproc_win32(p), "G53_VAR", "abcdefghij0123456789")` após o `prepare` — usando o suporte de environment **existente** (o mesmo dos unit-tests), conforme o §4 ("se existir uma forma já suportada de configurar uma variável para o processo de teste, utilize-a"). Nenhum sistema novo de environment foi implementado; nenhuma outra API foi criada; os PEs anteriores não foram alterados.

## Resultado

Assertions (propriedades estáveis do contrato público; `G53_VAR` = `"abcdefghij0123456789"`, comprimento 20, need=21):
1. **Cenário A** (variável existente, buffer 64): retorno == 20 (sem NUL) ✓ · conteúdo `'a'…'9'` exato + NUL em `buf[20]` ✓ · canário `0xCC` intacto em `buf[21]` (nenhuma escrita além de strlen+1) ✓
2. **Lookup case-insensitive** (`"g53_var"`): retorno == 20 ✓
3. **Cenário B** (variável inexistente): retorno == 0 ✓ · `GetLastError() == ERROR_ENVVAR_NOT_FOUND` ✓ · buffer intocado ✓
4. **Cenário C** (buffer 8 < need 21): retorno == 21 (tamanho com NUL) ✓ · `GetLastError() == ERROR_INSUFFICIENT_BUFFER` ✓ · nenhum acesso/escrita além da capacidade (canário intacto) ✓
5. **Cenário D** (consulta de tamanho, `lpBuffer = NULL`, `nSize = 0`): retorno == 21 ✓ · `GetLastError() == ERROR_INSUFFICIENT_BUFFER` ✓

**Exit code final: 53** (exclusivo do G53; contratos 10–21 identificam cada assertion que falhou; `SetLastError(0)` antes de cada caminho de erro torna as verificações de `GetLastError` inequívocas).

## Repetições

**20/20 execuções com rc=53** (log=38 em todas) — determinístico, sem crash, sem variação, sem estado residual (cada execução recria o processo e semeia `G53_VAR` de forma idêntica). + 1 execução diagnóstico inicial = 21 execuções bem-sucedidas.

## Regressão

- **C**: `3392 verificações, 0 falhas` ✓
- **Swift**: `Executed 63 tests, with 0 failures` ✓
- **warnings** (`gcc -Wall -Wextra`, receita do projeto): **0** (nenhum novo) ✓
- **analyzer** (`clang --analyze`, receita reproduzível): **7 achados — os mesmos pré-existentes** (`docs/RELATORIO_GRUPO49.md` §15); **nenhum novo** ✓
- **PE battery**: `15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓
- **Nenhum PE anterior regrediu** — checklist 23/23: hello_winsock_ord=42 · hello_version_systeminfo=42 · hello_stdhandle=42 · hello_lstrcpyn=52 · hello_wait_multiple=42 · hello_thread_timeout=42 · hello_mutex_timeout=42 · hello_thread_shared=42 · hello_thread=42 · hello_mutex=42 · hello_cs=42 · hello_virt=42 · hello_virt_protect=42 · hello_virt_query=42 · hello_qpc=42 · hello_heap=42 · hello_mbwc=42 · hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_stdio=5 · hello_gl12=42 · hello_sse=7 ✓

## Cobertura

Validado por PE x64 real (caminho completo):
- `GetEnvironmentVariableA` com **variável existente**: comprimento correto, conteúdo byte-exato, NUL, escrita limitada (canário);
- **variável inexistente**: retorno 0 + `ERROR_ENVVAR_NOT_FOUND` (contrato público) + buffer intocado;
- **buffer insuficiente**: retorno do tamanho necessário **com** NUL + `ERROR_INSUFFICIENT_BUFFER` + nenhuma escrita além da capacidade;
- **consulta de tamanho** (`lpBuffer = NULL`): retorno 21 + `ERROR_INSUFFICIENT_BUFFER`;
- busca **case-insensitive** (contrato público Win32);
- `DWORD nSize` investigado na ABI real (R8D, extensão zero) e consumido corretamente.

## Limitações

- `GetEnvironmentVariableW` **não** validada por PE (variante Unicode fora do escopo).
- `SetEnvironmentVariableA/W` **não implementadas** (TODO) — o PE não modifica o ambiente; a variável vem semeada pela infraestrutura.
- Limites da infra de env existente (8 variáveis, nome ≤63, valor ≤127) — não testados até os extremos.
- Valores com espaços/aspas/caracteres especiais e nomes além do par `G53_VAR`/`g53_var` não exercitados.
- `nSize = 0` com `lpBuffer != NULL` não exercitado (mesmo caminho de "buffer insuficiente" já coberto pelo cenário C/D).
- `GetLastError`/`SetLastError` usados como auxiliares; não constituem nova validação de API alvo.
- **Não se declara** compatibilidade geral com Windows, com Win32, nem com jogos comerciais (GTA V, MX Bikes ou outros). O G53 valida somente o comportamento efetivamente demonstrado pelo PE.

## Próximo passo (sem implementar o G54)

**`GetTickCount64`** — candidata do inventário com a classe de ABI ainda não exercitada: **largura de retorno de 64 bits** (`ULONGLONG` em RAX), propriedades estáveis de monotonicidade; implementada (`IMPL`), unit-tested, sem validação por PE real. Alternativas subsequentes: `TlsGetValue` (índice `DWORD`; TLS pela metade — `TlsAlloc` é TODO), `lstrcpyA`/`lstrlenA` (irmãos da `lstrcpynA`), `GetCommandLineA` (retorno de ponteiro/estado). Nenhuma delas foi implementada ou tocada neste grupo.
