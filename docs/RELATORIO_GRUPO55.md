# RELATÓRIO DA ETAPA — GRUPO 55

## Status

**CONCLUÍDO** — `TlsGetValue` validada por PE x64 real (`hello_tlsgetvalue.exe`, rc de sucesso **55**) nos caminhos demonstráveis do TLS parcial do Portico (slot vazio e índice inválido). **Nenhum blocker**; **nenhuma alteração no runtime** (MD5 antes == depois). `TlsAlloc` e `TlsSetValue` **não** foram implementadas (proibição cumprida).

## API validada

`kernel32!TlsGetValue(DWORD dwTlsIndex)` → `LPVOID`.

## Inventário

- **Implementação encontrada**: `f_TlsGetValue` (`pr_win32.c`), registro `IMPL("kernel32.dll", "TlsGetValue", f_TlsGetValue, 4)` (L4807; 1 argumento = 4 bytes). Comportamento: `idx = a[0]`; `idx >= 64` → `last_error = 6` (`ERROR_INVALID_HANDLE`) + retorno `0`; senão `last_error = 0` + retorno `ctx->tls_slots[idx]`.
- **Estado atual do TLS (parcial)**: `uint64_t tls_slots[64]` (L221) — índices válidos **0..63**. Convenções internas do CRT (registradas no próprio código): **[63] = célula iob**, **[62] = errno**, **[61] = strerror pool** — por isso foram evitados nos cenários. Slots 0..60 iniciam vazios.
- **`TlsAlloc`**: **NÃO implementada** — `TODO("kernel32.dll", "TlsAlloc", "TLS do convidado")` (L4843). **Não implementada neste grupo.**
- **`TlsSetValue`**: **ausente do catálogo** (sem registro IMPL/TODO). **Não implementada neste grupo.**
- **`TlsGetValue`**: implementada e funcional para os caminhos abaixo — reutilizada sem alteração.
- **Unit-tests**: grep exato não encontrou referência direta a `TlsGetValue` nas fontes de teste C/Swift (as menções em buscas anteriores eram da família na procura agregada; os `.exe` apenas importam o símbolo pelo startup). A validação deste grupo é, portanto, a **primeira** cobertura comportamental por teste.
- **Por que os cenários são possíveis no estado atual**: slot vazio (índice 0) e índice inválido (fora de 0..63) são integralmente demonstráveis com `TlsGetValue` + `GetLastError`/`SetLastError`; armazenamento/persistência exigiriam `TlsAlloc`/`TlsSetValue` (fora do escopo — §3).

## PE

- **Nome**: `hello_tlsgetvalue.exe` — **14.336 bytes**.
- **Origem**: `realpe/hello_tlsgetvalue.c` (criado neste grupo; nenhum PE anterior alterado).

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_tlsgetvalue.c -o Tests/PorticoRuntimeTests/data/hello_tlsgetvalue.exe` (MinGW-w64 posix).

## Imports (`x86_64-w64-mingw32-objdump -p`)

| DLL | Símbolo | Hint | Ordinal | Binding |
|---|---|---|---|---|
| `KERNEL32.dll` | **`TlsGetValue`** | `05c7` | `<none>` | **por nome** |
| `KERNEL32.dll` | `SetLastError` (auxiliar de assertion) | `0554` | `<none>` | por nome |
| `KERNEL32.dll` | `GetLastError` (auxiliar de assertion) | `0283` | `<none>` | por nome |
| `msvcrt.dll` | CRT de startup | — | `<none>` | por nome |

Import por nome confirmado e resolvido pelo loader. Nota: o startup MinGW já listava `TlsGetValue` na tabela de imports dos PEs anteriores (hint `05c7`), mas o símbolo **nunca fora despachado** — as chamadas deste PE são as **primeiras invocações reais** com validação (caminho completo `hello_tlsgetvalue.exe → … → f_TlsGetValue → retorno → PE → CRT → exit code`; sem mock, sem handler direto, sem estado TLS fabricado).

## Execução inicial (runtime intacto)

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_tlsgetvalue.exe`:

```
exited=1 rc=55 steps=1 exec=369
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 55
```

Resultado: **passou direto** (fluxo completo) — import resolvido, handler correto atingido, sem crash.

## Primeiro blocker

**Nenhum.**

## Diagnóstico de ABI (call site real, `objdump -d`, antes de qualquer correção)

```
mov    $0xdead,%ecx             ; SetLastError(0xDEAD) — DWORD via ECX
mov    0x5af8(%rip),%rsi        ; rsi = IAT[SetLastError] (0x140008190)
call   *%rsi
mov    0x5b07(%rip),%rbx        ; rbx = IAT[TlsGetValue] (0x1400081a8)
xor    %ecx,%ecx                ; dwTlsIndex = 0 — DWORD via ECX (extensão ZERO 32→64)
call   *%rbx                    ; TlsGetValue(0)
mov    %rax,%rdx                ; retorno LPVOID → RDX (move de 64 BITS, sem truncamento)
test   %rdx,%rdx                ; r != NULL ? (teste de ponteiro de 64 bits)
...
mov    0x5ab7(%rip),%rdi        ; rdi = IAT[GetLastError] (0x140008178)
call   *%rdi
mov    %eax,%edx                ; retorno DWORD → EDX (32 bits — correto para DWORD)
```

Classe `DWORD argumento → RCX/ECX → handler → LPVOID → RAX` demonstrada:
- **Argumento**: `dwTlsIndex` (`DWORD` de 32 bits) em **RCX/ECX** — `xor %ecx,%ecx` (0), e nos demais cenários `mov $0x40,%ecx` (64) e `mov $0xffffffff,%ecx` (0xFFFFFFFF) — **extensão zero de 32→64** em todos ✓;
- **Retorno**: `LPVOID` em **RAX** tratado como **ponteiro de 64 bits** (`mov %rax,%rdx` + `test %rdx,%rdx`) — sem truncamento ✓;
- Thunks da IAT confirmados (`jmp *…0x81a8` para `TlsGetValue`).

## LastError

Comportamento **observado** (e validado pelo PE):
- **Sucesso (slot vazio)**: retorno `NULL` **e `LastError` limpo para 0** — o handler faz `ctx->last_error = 0` no caminho de sucesso. Para tornar a assertion inequívoca (o caso clássico de `NULL`-ambíguo do TLS), o PE usa `SetLastError(0xDEAD)` antes e verifica `GetLastError() == 0` depois (técnica documentada; §13 do briefing). Isso distingue "sucesso com valor NULL" de falha.
- **Índice inválido**: retorno `NULL` e `GetLastError() == ERROR_INVALID_HANDLE (6)` — contrato efetivamente implementado pelo Portico. Registrada a diferença: a documentação do Windows real para índice TLS inválido não fixa um código único (varia entre fontes); **não se afirma equivalência** — o teste valida o contrato do Portico documentado no próprio handler.
- `GetLastError`/`SetLastError` são **auxiliares de assertion** (§13); não constituem API-alvo do G55.

## Correção

**Nenhuma alteração no runtime.**
- MD5 do runtime (`Sources/PorticoRuntime/src/*.c` + `include/portico/*.h`): **`3ee3ce898084047420c02e7ef62179c9` antes == `3ee3ce898084047420c02e7ef62179c9` depois** (idêntico).
- Nenhuma infraestrutura alterada; `TlsAlloc`/`TlsSetValue` **não** implementadas; G51–G54 preservados (verificados no início).

## Resultado

Assertions (propriedades estáveis; nenhum valor mágico privado; nenhum estado fabricado):
1. **Cenário A — índice válido vazio (0)**: `TlsGetValue(0)` retorna `NULL` ✓ · `GetLastError() == 0` após `SetLastError(0xDEAD)` (sucesso limpa LastError) ✓ · sem crash ✓
2. **Cenário B1 — índice inválido (64)** (primeiro fora de `tls_slots[0..63]`, limite do inventário): retorno `NULL` ✓ · `GetLastError() == ERROR_INVALID_HANDLE` ✓ · sem acesso fora da estrutura / sem crash ✓
3. **Cenário B2 — `DWORD` máximo (0xFFFFFFFF)**: retorno `NULL` ✓ · `GetLastError() == ERROR_INVALID_HANDLE` ✓
4. **Argumento `DWORD` corretamente transmitido** e **retorno tratado como ponteiro de 64 bits** — evidência de ABI acima ✓

**Exit code final: 55** (exclusivo do G55; contratos: 10/11 = A(0) · 12/13 = B(64) · 14/15 = B(0xFFFFFFFF) · **55 = sucesso**).

## Repetições

**20/20 execuções com rc=55** (log=36 em todas) — mesmo exit code, mesmos resultados nos caminhos testados, sem crash, sem acesso inválido, sem estado residual (índice 0 documentado no inventário: slot livre entre 0..60). + 1 execução diagnóstico inicial = 21 execuções bem-sucedidas.

## Regressão

- **C**: `3392 verificações, 0 falhas` ✓
- **Swift**: `Executed 63 tests, with 0 failures` ✓
- **warnings** (`gcc -Wall -Wextra`, receita do projeto): **0** (nenhum novo) ✓
- **analyzer** (`clang --analyze`, receita reproduzível): **7 achados — os mesmos pré-existentes** (`docs/RELATORIO_GRUPO49.md` §15); **nenhum novo** ✓
- **PE battery**: `15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓
- **Checklist completa (25/25) — explicitamente incluindo `hello_stdhandle` (42), `hello_lstrcpyn` (52), `hello_getenv` (53), `hello_tickcount64` (54) e todos os demais** (hello_winsock_ord=42 · hello_version_systeminfo=42 · hello_wait_multiple=42 · hello_thread_timeout=42 · hello_mutex_timeout=42 · hello_thread_shared=42 · hello_thread=42 · hello_mutex=42 · hello_cs=42 · hello_virt=42 · hello_virt_protect=42 · hello_virt_query=42 · hello_qpc=42 · hello_heap=42 · hello_mbwc=42 · hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_stdio=5 · hello_gl12=42 · hello_sse=7) ✓ — **nenhum PE anterior regrediu**.

## Cobertura

Validado por PE x64 real (caminho completo, sem mocks):
- `TlsGetValue` chamada de verdade via IAT nos três cenários;
- **caminho de slot vazio** (índice 0): `NULL` + sucesso (LastError limpo);
- **caminho de índice inválido** (64 e 0xFFFFFFFF): `NULL` + `ERROR_INVALID_HANDLE`;
- ABI `DWORD → ECX (extensão zero) → handler → LPVOID → RAX (64 bits)` demonstrada;
- sem crash e sem acesso fora da estrutura de slots (determinístico em 21 execuções).

## Limitações (especialmente o que depende de `TlsAlloc`/`TlsSetValue`)

- **`TlsAlloc` (TODO) e `TlsSetValue` (ausente) não foram implementadas** — portanto **NÃO** foi validado e permanece fora do escopo: criação de índice TLS; armazenamento de valor; recuperação de valor previamente armazenado; persistência entre chamadas; isolamento de valores entre threads; comportamento completo de TLS.
- Índices 61–63 (convenções internas do CRT: strerror/errno/iob) não foram tratados como "vazios" — não testados.
- Índices 1..60 não foram exaustivamente testados (o contrato é idêntico ao do índice 0 no handler).
- Não há unit-test direto prévio de `TlsGetValue` — esta é a primeira cobertura comportamental (registrado no inventário).
- `ERROR_INVALID_HANDLE (6)` para índice inválido = **contrato do Portico**; sem afirmação de equivalência com o Windows real (código variável na documentação).
- **Não se declara** compatibilidade geral com Windows, com Win32, comportamento completo de TLS, nem com jogos comerciais (GTA V, MX Bikes ou outros) — §18. O G55 valida somente o comportamento efetivamente demonstrado pelo PE.

## Próximo passo (sem implementar o G56)

**`GetCommandLineA`** — retorno `LPSTR` de 64 bits com conteúdo/estado (linha de comando do processo, já semeada pelo loader), propriedades estáveis de NUL-terminação/consistência entre chamadas; implementada (`IMPL`), sem validação por PE real. Alternativas subsequentes: `lstrcpyA`/`lstrlenA` (irmãos da `lstrcpynA`), `IsDebuggerPresent` (retorno BOOL), `GetCurrentProcessId`/`GetCurrentThreadId` (retorno DWORD). Nenhuma delas foi implementada ou tocada neste grupo.
