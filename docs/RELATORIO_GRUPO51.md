# Relatório do Grupo 51 — Validação real de GetStdHandle

## Status

**CONCLUÍDO** — `GetStdHandle` importado e realmente chamado por PE x64 real (`hello_stdhandle.exe`), com os **três tipos** de standard handle exercitados. Foi observado **um blocker** (retorno `NULL` por mismatch de largura do parâmetro) e corrigido com a **menor alteração possível** (um `switch` em `f_GetStdHandle`, comparando como `DWORD`/32 bits). Depois da correção: rc=42, **20/20 execuções** e regressão completa verde. Nenhuma outra API foi implementada.

## PE

- **Nome**: `hello_stdhandle.exe` (fonte `realpe/hello_stdhandle.c`).
- **Tamanho**: 14.336 bytes.
- **Compilação**: `x86_64-w64-mingw32-gcc -O2 -s realpe/hello_stdhandle.c -o Tests/PorticoRuntimeTests/data/hello_stdhandle.exe` (MinGW-w64; só startup/CRT além de `GetStdHandle`).
- **Imports** (`x86_64-w64-mingw32-objdump -p`):

| DLL | Símbolo | Hint | Ordinal | Binding |
|---|---|---|---|---|
| `KERNEL32.dll` | `GetStdHandle` | `02fb` | `<none>` | **por nome** |
| `msvcrt.dll` | CRT de startup | — | `<none>` | por nome |

`GetStdHandle` foi **realmente importado por nome** e **realmente chamado**: cada uma das 6 chamadas (2 por tipo) participa de assertion que altera o `rc` (contratos 10–18).

- **Inventário prévio da implementação** (passo 1 do fluxo): `f_GetStdHandle` (`pr_win32.c` L481, `IMPL("kernel32.dll", "GetStdHandle", f_GetStdHandle, 4)`): `switch` sobre o índice com `W32_STD_INPUT_HANDLE/OUTPUT/ERROR = (uint64_t)-10/-11/-12` retornando os handles mágicos fixos `PR_WIN32_H_STDIN/OUT/ERR` (`0xF0F0F003` e irmãos); `default` = `ERROR_INVALID_PARAMETER` + retorno `0`.

## Execução inicial (runtime completamente intacto)

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_stdhandle.exe`:

```
exited=1 rc=10 steps=1 exec=323
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 10
```

- **exit code 10 = primeiro blocker observado**: `GetStdHandle(STD_INPUT_HANDLE)` retornou **NULL** (a assertion 10 do PE alterou o `rc` conforme exigido).
- steps=1 · exec=323 · log do processo: nenhuma API errada logada; o handler foi chamado e caiu no `default`.
- Sem crash, sem falha de ABI de saída, sem problema de import resolver (o bind funcionou; a chamada chegou ao handler).

**Diagnóstico do primeiro blocker** (comportamento incorreto observado): a API real declara `HANDLE GetStdHandle(DWORD nStdHandle)` com `STD_*_HANDLE = ((DWORD)-10/-11/-12)`. Na ABI real o mingw emite `mov ecx, -10` → o argumento chega ao handler **zero-extended** (`0x00000000FFFFFFF6`) no slot de 64 bits (`args[0] = RCX` cru, `pr_win32.c` L2073/2080). O `switch` compara com `(uint64_t)-10` (`0xFFFFFFFFFFFFFFF6`) → **não casa** → `default` → `ERROR_INVALID_PARAMETER` + `0`. Os unit-tests nunca pegaram isso porque invocam o handler com `-10` já em 64 bits.

## Correção

- **Arquivo**: `Sources/PorticoRuntime/src/pr_win32.c`.
- **Função**: `f_GetStdHandle` (somente ela).
- **Alteração mínima**: o `switch` passou a comparar **`(uint32_t)a[0]`** (o tipo real do parâmetro, `DWORD`) com os cases `(uint32_t)W32_STD_*_HANDLE` (+ comentário). Isso cobre o caminho do PE real (`0xFFFFFFF6`) **e** preserva o caminho dos unit-tests (`(uint64_t)-10` truncado casa o mesmo case). Nada mais foi tocado.
- **MD5 antes/depois**: `c9e8afa71214412be92bff3de0b35278` → `3ee3ce898084047420c02e7ef62179c9` (**alterado** — registrados ambos, como exigido).

## Resultado

- **Assertions** (contrato do PE — somente propriedades estáveis; nenhum valor numérico específico exigido):
  1–3. `GetStdHandle(STD_INPUT_HANDLE/STD_OUTPUT_HANDLE/STD_ERROR_HANDLE)` ≠ `NULL` ✓
  4–6. os três retornos ≠ `INVALID_HANDLE_VALUE` ✓
  7–9. chamadas repetidas dos três tipos produzem **o mesmo HANDLE** (consistência) ✓
  (cada retorno é armazenado em `HANDLE` e reutilizado nas comparações ✓; nenhum crash; nenhum acesso inválido ✓)
- **Repetições**: **20/20 com rc=42** (log=33 em todas — determinístico) + 1 execução diagnóstico pós-correção = 21 execuções bem-sucedidas; antes da correção: 1 execução com o blocker (rc=10).
- **rc final**: 42.

## Regressão (pós-correção)

- **C**: `3392 verificações, 0 falhas` ✓ (os unit-tests de `GetStdHandle` seguem verdes com a comparação de 32 bits).
- **Swift**: `Executed 63 tests, with 0 failures` ✓
- **warnings**: 0 (receita do projeto `gcc -Wall -Wextra`; a correção usou casts explícitos, sem novos avisos) ✓
- **analyzer** (receita reproduzível `clang --analyze`): **7 achados — os mesmos pré-existentes** já listados no `docs/RELATORIO_GRUPO49.md` §15; **zero novos** (a correção não alterou essa contagem).
- **PE battery**: `15/15` — logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105` ✓
- **Testes já validados anteriormente** (checklist completa): hello_wait_multiple=42 · hello_thread_timeout=42 · hello_mutex_timeout=42 · hello_thread_shared=42 · hello_thread=42 · hello_mutex=42 · hello_cs=42 · hello_virt=42 · hello_virt_protect=42 · hello_virt_query=42 · hello_qpc=42 · hello_heap=42 · hello_mbwc=42 · hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_stdio=5 · hello_gl12=42 · hello_sse=7 ✓ (inclui `hello_stdio=5`, que exercita o caminho dos std handles pelo CRT — intacto após a correção).

## Cobertura

**`GetStdHandle`: VALIDADO por PE real** — importado por nome, chamado de verdade, os três tipos (`STD_INPUT_HANDLE`, `STD_OUTPUT_HANDLE`, `STD_ERROR_HANDLE`) exercitados por assertions que determinam o código de saída, retornos coerentes com o contrato implementado (handles mágicos fixos ≠ NULL ≠ INVALID_HANDLE_VALUE) e consistentes em chamadas repetidas.

## Limitações restantes

- Os valores numéricos dos HANDLEs são os **handles mágicos do Pórtico** (`PR_WIN32_H_STDIN/OUT/ERR`) — **não** se afirma igualdade com os valores do Windows real (nem era exigido).
- O contrato de saída/erro de `WriteFile`/stdio sobre esses handles já era exercitado por `hello_stdio` e unit-tests; este grupo validou **somente** `GetStdHandle` (sem `ReadFile`, `WriteFile`, `GetConsoleMode`, como determinado).
- Índices **inválidos** de `nStdHandle` (diferentes de -10/-11/-12) continuam com o comportamento `ERROR_INVALID_PARAMETER` + `NULL` do `default` — não exercitados por PE real neste grupo (não era alvo).
- Os 7 achados pré-existentes do analyzer permanecem (fora do escopo — a correção deles exigiria alterações não observadas por PE).
- **Não se afirma compatibilidade geral com Windows nem com jogos comerciais.**

**Não se implementou o G52.**
