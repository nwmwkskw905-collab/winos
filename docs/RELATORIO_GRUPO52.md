# RELATÓRIO DA ETAPA — GRUPO 52

## Status

**CONCLUÍDO** — `lstrcpynA` validada por PE x64 real (`hello_lstrcpyn.exe`, rc de sucesso **52**) com import por nome e chamadas reais. **Nenhum blocker** foi observado: o PE passou direto na primeira execução com o runtime intacto. **Nenhuma alteração no runtime** (MD5 antes == depois). Nada fora do escopo foi implementado; a arquitetura e os PEs/testes existentes foram preservados (incluídos na regressão).

## API validada

`kernel32!lstrcpynA(LPSTR lpCharDest, LPCSTR lpCharSource, int iCharCount)` — escolhida no inventário por: (1) implementada (`f_lstrcpynA`, `IMPL` "comportamento real"); (2) unit-tested (família `lstr*` em `test_win32.c`/`test_win32x.c`); (3) sem validação equivalente por PE real (nunca importada por nenhum PE anterior); (4) pequena e isolada; (5) **risco semelhante ao do `GetStdHandle`**: parâmetro inteiro **assinado de 32 bits** (`iCharCount`) viajando por ABI real — a mesma classe de largura/extensão que escondeu o bug do G51 — além de largura de retorno (LPSTR) e escrita delimitada com assertions byte-exatas (maior poder de detecção da lista de candidatas).

## PE

- **Nome**: `hello_lstrcpyn.exe` — **14.848 bytes**.
- **Origem do código**: `realpe/hello_lstrcpyn.c` (criado neste grupo; nenhum PE anterior foi alterado).

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_lstrcpyn.c -o Tests/PorticoRuntimeTests/data/hello_lstrcpyn.exe` (MinGW-w64 posix).

## Imports (`x86_64-w64-mingw32-objdump -p`)

| DLL | Símbolo | Hint | Ordinal | Binding |
|---|---|---|---|---|
| `KERNEL32.dll` | `lstrcpynA` | `066a` | `<none>` | **por nome** |
| `msvcrt.dll` | CRT de startup | — | `<none>` | por nome |

Símbolo **realmente importado por nome** e **realmente chamado**: as 4 chamadas participam de assertions que determinam o `rc` (contratos 10–19). Caminho completo provado: PE real → loader → import resolver → `kernel32.dll!lstrcpynA` → handler → retorno ao PE → CRT `exit` → exit code observado (sem mocks, sem chamada direta de handler).

## Execução inicial (runtime completamente intacto)

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_lstrcpyn.exe`:

```
exited=1 rc=52 steps=1 exec=394
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 52
```

Resultado: **passou direto** (fluxo completo), sem crash, sem falha de import, sem falha de ABI.

## Primeiro blocker

**Nenhum.** Nenhum comportamento incorreto foi observado.

## Diagnóstico (investigação de ABI real, feita antes de qualquer correção)

Disassembly do call site real (binário MinGW, `objdump -d`):

```
mov    $0x8,%r8d          ; iCharCount (int) → R8D de 32 bits (zero-extendido)
mov    %rbx,%rcx          ; lpCharDest   → RCX
mov    %rdi,%rdx          ; lpCharSource → RDX
call   *%rsi              ; via IAT (0x1400081b8)
mov    %rax,%rdx          ; retorno LPSTR em RAX (64 bits)
cmp    %rdx,%rbx          ; identidade do ponteiro de retorno
```

- **Largura real dos parâmetros**: `lpCharDest`/`lpCharSource` = ponteiros de 64 bits em RCX/RDX; `iCharCount` = **`int` de 32 bits** carregado por `r8d` (extensão **zero** de 32→64).
- **Registradores/convenção**: RCX, RDX, R8 (win32 x64), retorno em RAX.
- **Tipo real declarado pela API**: `int iCharCount` (signed) — no handler chega como `args[2]` (64 bits crus) e é convertido com `(size_t)a[2]`; para valores positivos a entrega zero-extendida é coerente — **sem mismatch observado**.
- **Largura do retorno**: LPSTR (RAX de 64 bits) — a identidade `r == buf` foi validada nas 4 chamadas.
- O gcc -O2 também emitiu SSE2 no preenchimento do canário (`pshufd`/`movups`) — instruções já pertencentes ao subconjunto CPU validado; nenhuma instrução nova foi observada.

## Correção

**Nenhuma alteração no runtime.** Nenhum blocker foi observado, portanto nada foi modificado. MD5 do runtime (`Sources/PorticoRuntime/src/*.c` + `include/portico/*.h`): **`3ee3ce898084047420c02e7ef62179c9` antes == `3ee3ce898084047420c02e7ef62179c9` depois** (idêntico). A correção do G51 em `f_GetStdHandle` está preservada (verificada antes de qualquer ação).

## Resultado

- **Assertions** (somente propriedades estáveis do contrato; nenhum valor mágico do Pórtico):
  1. Cópia normal (`"abc"`, cap=8): retorno == ponteiro destino ✓; bytes `'a','b','c'`,NUL exatos ✓; nada escrito além de strlen+1 (canário `0xCC` intacto) ✓
  2. Truncamento (`"abcdefgh"`, cap=4): retorno == destino ✓; `'a','b','c'`,NUL ✓ (NUL sempre presente); nada além de `iCharCount` ✓
  3. Limite `iCharCount=1`: retorno == destino ✓; somente NUL escrito ✓; canário intacto ✓
  4. Fonte vazia (`""`): retorno == destino ✓; somente NUL ✓
- **Exit code final**: **52** (fluxo completo; contrato: 10–19 = falhas por assertion; 52 = sucesso — código distinto dos PEs anteriores para auto-identificação, conforme §9).

## Repetições

**20/20 execuções com rc=52** (log=31 em todas — determinístico, sem variação, sem crash) + 1 execução diagnóstico inicial = 21 execuções bem-sucedidas.

## Regressão

- **C**: `3392 verificações, 0 falhas` ✓
- **Swift**: `Executed 63 tests, with 0 failures` ✓
- **warnings** (receita do projeto `gcc -Wall -Wextra`): **0** (nenhum novo) ✓
- **analyzer** (`clang --analyze`, receita reproduzível): **7 achados — os mesmos pré-existentes** já listados no `docs/RELATORIO_GRUPO49.md` §15; **zero novos** ✓
- **PE battery**: `15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓ — nenhum teste anterior regrediu.
- **Checklist completa**: hello_winsock_ord=42 · hello_version_systeminfo=42 · hello_stdhandle=42 · hello_wait_multiple=42 · hello_thread_timeout=42 · hello_mutex_timeout=42 · hello_thread_shared=42 · hello_thread=42 · hello_mutex=42 · hello_cs=42 · hello_virt=42 · hello_virt_protect=42 · hello_virt_query=42 · hello_qpc=42 · hello_heap=42 · hello_mbwc=42 · hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_stdio=5 · hello_gl12=42 · hello_sse=7 ✓ (22/22 — inclui todos os PEs dos grupos anteriores).

## Cobertura

**Validado** (caminho completo PE real → import resolver → handler → retorno → exit code):
- `lstrcpynA` cópia normal com NUL e escrita limitada (byte-exata);
- truncamento com **NUL sempre** (propriedade documentada de `lstrcpynA`, distinta de `strncpy`);
- fronteira `iCharCount=1` (somente NUL);
- fonte vazia;
- identidade do ponteiro de retorno (64 bits) em todas as chamadas;
- parâmetro `int iCharCount` entregue zero-extendido pela ABI real e consumido corretamente (classe de largura do G51 — **sem o defeito** nesta API).

## Limitações (o que NÃO foi validado)

- `iCharCount` **negativo ou zero** não foi exercitado (comportamento indefinido no Windows real — não é contrato estável).
- `iCharCount` maior que a memória real do destino não foi exercitado: o handler exige que a região destino comporta `iCharCount` bytes (`pr_win32_ptr(dst, cap)`); não se afirma paridade com o Windows real para esse caso-limite.
- Variantes `lstrcpynW`, `lstrcpyA/W`, `lstrlenA/W` **não** validadas por PE (não são alvo desta etapa).
- Binding por ordinal não se aplica (KERNEL32 por nome).
- **Não se afirma compatibilidade geral com Windows nem com jogos comerciais** (GTA V, MX Bikes ou outros) por causa desta etapa.

## Próximo passo (sem implementar o G53)

Investigar posteriormente **`GetEnvironmentVariableA`** — contrato rico e pequeno: caminho de variável inexistente (retorno 0), consulta de tamanho (`lpBuffer=NULL` → tamanho com NUL, `ERROR_INSUFFICIENT_BUFFER` no buffer curto), cópia byte-exata com canário, e parâmetro **`DWORD nSize` de 32 bits** (mesma classe de largura). Alternativas viáveis do inventário: `GetTickCount64` (largura de **retorno** de 64 bits), `TlsGetValue` (índice `DWORD`; TLS pela metade — `TlsAlloc` é TODO), `lstrcpyA`/`lstrlenA` (irmãos da validada), `GetCommandLineA` (retorno de ponteiro/estado). Nenhuma delas foi implementada ou tocada neste grupo.
