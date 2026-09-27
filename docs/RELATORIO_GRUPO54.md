# RELATÓRIO DA ETAPA — GRUPO 54

## Status

**CONCLUÍDO** — `GetTickCount64` validada por PE x64 real (`hello_tickcount64.exe`, rc de sucesso **54**), import por nome, chamada real via IAT, retorno efetivamente tratado como 64 bits. **Nenhum blocker**; **nenhuma alteração no runtime** (MD5 antes == depois). Monotonicidade não decrescente validada; truncamento ausente na medida possível (disassembly + armazenamento + delta), conforme o fallback do briefing.

## API validada

`kernel32!GetTickCount64(void)` → `ULONGLONG` (retorno inteiro de 64 bits em RAX).

## Inventário

Recomendada no G54 como a classe de ABI ainda não exercitada: **retorno inteiro de 64 bits**. Confirmado no código existente:
- Handler `f_GetTickCount64` (`pr_win32.c`): `return (mono_ns() - ctx->start_ns) / 1000000ull;` — registro `IMPL("kernel32.dll", "GetTickCount64", f_GetTickCount64, 0)` (**0 argumentos**).
- Relógio: `mono_ns()` = tempo monotônico real do host; `start_ns` fixado na criação do contexto (L5119). Existe infraestrutura determinística de tempo **virtual** (`time_offset_ms` + `pr_win32_advance_time`, usada pelo message pump/WM_TIMER em L3339/L3481), mas ela **não alimenta** `GetTickCount64` (detalhe de implementação registrado; não é requisito do contrato testado). Portanto **não** há como produzir deterministicamente valores > `0xFFFFFFFF` neste caminho — aplicado o fallback do §3D (largura validada via call site/disassembly + armazenamento; nenhum valor artificial foi fabricado).
- Unit-tests: `test_pe_loader.c` (via PE sintético `pt_pe_builder2.h`), `WindowsExecutionTests.swift`, `CompatLayerTests.swift`.
- Tipos: retorno `ULONGLONG` (`uint64_t` interno). Nenhuma API de tempo adicional foi implementada (`GetTickCount`, `GetSystemTime` etc. permanecem fora do escopo).

## PE

- **Nome**: `hello_tickcount64.exe` — **14.336 bytes**.
- **Origem**: `realpe/hello_tickcount64.c` (criado neste grupo; nenhum PE anterior alterado).

## Compilação

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_tickcount64.c -o Tests/PorticoRuntimeTests/data/hello_tickcount64.exe` (MinGW-w64 posix).

## Imports (`x86_64-w64-mingw32-objdump -p`)

| DLL | Símbolo | Hint | Ordinal | Binding |
|---|---|---|---|---|
| `KERNEL32.dll` | **`GetTickCount64`** | `0335` | `<none>` | **por nome** |
| `msvcrt.dll` | CRT de startup | — | `<none>` | por nome |

Import por nome confirmado e resolvido pelo loader; chamada real única da API alvo saindo do PE pelo mecanismo normal de import (caminho completo `hello_tickcount64.exe → … → handler real → retorno 64-bit → PE → CRT → exit code`; sem mock, sem handler direto, sem auxiliar C).

## Execução inicial (runtime intacto)

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_tickcount64.exe`:

```
exited=1 rc=54 steps=1 exec=323
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 54
```

Resultado: **passou direto** (fluxo completo) — import resolvido, handler correto chamado, sem crash.

## Primeiro blocker

**Nenhum.**

## Diagnóstico de ABI (call site real, `objdump -d`, antes de qualquer correção)

```
mov    0x5aed(%rip),%rdi        # rdi = IAT[GetTickCount64] (0x140008180)
call   *%rdi                    ; a = GetTickCount64()   — SEM argumentos
mov    %rax,%rsi                ; a → RSI (move de 64 bits, sem truncamento)
call   *%rdi                    ; b = GetTickCount64()
mov    %rax,%rbx                ; b → RBX (move de 64 bits)
mov    $0xa,%eax                ; código de falha 10 preparado
cmp    %rsi,%rbx                ; b >= a ? (comparação de 64 bits)
jae    ...
```

- **Argumentos: nenhum** (nenhum setup de RCX/RDX/R8 antes do `call`) ✓
- **Call site real**: via **IAT** (`0x140008180`, carregado em %rdi; thunk `jmp *0x…8180` em 0x140002658) ✓
- **Retorno em RAX** ✓ e **tratado como 64 bits**: os movimentos são `mov %rax,%rsi` / `mov %rax,%rbx` (registradores inteiros de 64 bits) e as comparações são `cmp %rsi,%rbx` de 64 bits — **nenhum truncamento posterior para 32 bits observado** (nem `%eax` em qualquer etapa do valor) ✓
- O valor chega integralmente ao PE (armazenado em `ULONGLONG a, b, c`) ✓
- Nenhuma extensão/truncamento intermediário observado entre o handler e o armazenamento.

## Correção

**Nenhuma alteração no runtime.**
- MD5 do runtime (`Sources/PorticoRuntime/src/*.c` + `include/portico/*.h`): **`3ee3ce898084047420c02e7ef62179c9` antes == `3ee3ce898084047420c02e7ef62179c9` depois** (idêntico).
- Nenhuma infraestrutura foi alterada neste grupo (os harnesses `dbg_*` seguem com o seeding de `G53_VAR` do G53, inalterado).

## Resultado

Assertions (somente propriedades estáveis; nenhum valor absoluto de host; nenhum sleep/atraso):
1. **Cenário A** — chamada inicial retorna sem crash e é armazenada integralmente em `ULONGLONG` (64 bits) ✓
2. **Cenário B** — `b = GetTickCount64() >= a` (sem regressão temporal entre chamadas sucessivas; `b > a` não é exigido) ✓
3. **Cenário C** — `c >= b` ✓ e `c >= a` ✓ (monotonicidade não decrescente)
4. **Ausência de truncamento/lixo observável** — `c - a <= 0x00FFFFFF` ms (três chamadas consecutivas não transcorrem ~4,6 h nem exibem lixo em bits altos) ✓ + evidência de disassembly acima (sem truncamento no caminho)

**Exit code final: 54** (exclusivo do G54; contratos: 10 = `b < a` · 11 = `c < b` · 12 = `c < a` · 13 = delta incoerente · **54 = sucesso**).

## Repetições

**20/20 execuções com rc=54** (log=30 em todas) — mesmo exit code, determinístico nas assertions, sem crash, sem acesso inválido, monotonicidade preservada em todas (os valores absolutos do contador não são exigidos iguais entre execuções, conforme §13). + 1 execução diagnóstico inicial = 21 execuções bem-sucedidas.

## Regressão

- **C**: `3392 verificações, 0 falhas` ✓
- **Swift**: `Executed 63 tests, with 0 failures` ✓
- **warnings** (`gcc -Wall -Wextra`, receita do projeto): **0** (nenhum novo) ✓
- **analyzer** (`clang --analyze`, receita reproduzível): **7 achados — os mesmos pré-existentes** (`docs/RELATORIO_GRUPO49.md` §15); **nenhum novo** ✓
- **PE battery**: `15/15` (logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105`) ✓
- **Checklist completa (24/24) — explicitamente incluindo `hello_stdhandle` (42), `hello_lstrcpyn` (52), `hello_getenv` (53) e todos os demais**: hello_winsock_ord=42 · hello_version_systeminfo=42 · hello_wait_multiple=42 · hello_thread_timeout=42 · hello_mutex_timeout=42 · hello_thread_shared=42 · hello_thread=42 · hello_mutex=42 · hello_cs=42 · hello_virt=42 · hello_virt_protect=42 · hello_virt_query=42 · hello_qpc=42 · hello_heap=42 · hello_mbwc=42 · hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_stdio=5 · hello_gl12=42 · hello_sse=7 ✓ — **nenhum PE anterior regrediu**.

## Cobertura

Validado por PE x64 real (caminho completo):
- `GetTickCount64` chamada de verdade via import por nome, **sem argumentos**, retorno `ULONGLONG`;
- retorno tratado como **64 bits** pelo compilador no call site real (moves `rax→rsi/rbx`, comparações de 64 bits, sem truncamento observado);
- armazenamento integral em `ULONGLONG` no PE;
- **monotonicidade não decrescente** (`b >= a`, `c >= b`, `c >= a`);
- ausência de truncamento/lixo observável na medida possível do ambiente (delta `c - a` limitado + disassembly);
- comportamento estável/determinístico do resultado das assertions em 21 execuções.

## Limitações

- Valores **acima de `0xFFFFFFFF`** não foram produzidos: o caminho usa tempo monotônico real e não há infra determinística para forçar o overflow nesta API (o offset virtual de `pr_win32_advance_time` não a afeta). Nenhum valor artificial foi fabricado; a largura foi validada pelo fallback previsto (disassembly + armazenamento + delta).
- **Precisão temporal absoluta não validada** e **não se afirma equivalência com o relógio de um Windows real**: o valor é milissegundos desde a criação do contexto do processo (contrato do Portico), não uptime de sistema real.
- Monotonicidade validada para tripletes de chamadas consecutivas em um mesmo processo (não entre processos/reinicializações).
- `GetTickCount` (32 bits) **não** está implementada nem validada; `QueryPerformanceCounter/Frequency` e `Sleep` já eram validados por PEs anteriores e não foram tocados.
- **Não se declara** compatibilidade geral com Windows, com Win32, precisão temporal, nem com jogos comerciais (GTA V, MX Bikes ou outros). O G54 valida somente o comportamento efetivamente demonstrado pelo PE.

## Próximo passo (sem implementar o G55)

**`TlsGetValue`** — candidata do inventário com parâmetro `DWORD` de índice (classe de largura do G51) e retorno de ponteiro de 64 bits com semântica de estado (64 slots; `last_error` limpo no sucesso). Atenção honesta: o TLS está "pela metade" (`TlsAlloc`/`TlsSetValue` = TODO) — o PE validaria os caminhos de slot vazio (retorno NULL determinístico) e índice inválido (`ERROR_INVALID_HANDLE`), sem estado persistido. Alternativas subsequentes: `GetCommandLineA` (retorno de ponteiro/estado), `lstrcpyA`/`lstrlenA` (irmãos da `lstrcpynA`), `IsDebuggerPresent` (retorno BOOL). Nenhuma delas foi implementada ou tocada neste grupo.
