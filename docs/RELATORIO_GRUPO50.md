# Relatório do Grupo 50 — Validação real de GetVersion/GetVersionExA + GetSystemInfo

## Status

**CONCLUÍDO** — as três APIs foram exercitadas por PE x64 real (`hello_version_systeminfo.exe` → **rc=42**) **sem nenhuma alteração no runtime**: o PE passou direto na primeira execução com o runtime intacto (MD5 antes == depois). Nenhuma API adicional foi implementada.

## PE

- **Nome**: `hello_version_systeminfo.exe` (fonte `realpe/hello_version_systeminfo.c`, 14.848 bytes no binário).
- **Compilação**: `x86_64-w64-mingw32-gcc -O2 -s realpe/hello_version_systeminfo.c -o Tests/PorticoRuntimeTests/data/hello_version_systeminfo.exe` (MinGW-w64 posix; só startup/CRT além das APIs alvo).
- **Imports reais** (`x86_64-w64-mingw32-objdump -p`):

| DLL | Símbolo | Hint | Ordinal | Binding |
|---|---|---|---|---|
| `KERNEL32.dll` | `GetSystemInfo` | 030c | `<none>` | **por nome** |
| `KERNEL32.dll` | `GetVersion` | 0348 | `<none>` | **por nome** |
| `KERNEL32.dll` | `GetVersionExA` | 0349 | `<none>` | **por nome** |
| `msvcrt.dll` | CRT de startup (memset, exit, …) | — | `<none>` | por nome |

As três APIs estão **realmente vinculadas por nome** e foram **realmente chamadas** (o fluxo completo passou; nenhuma foi meramente importada sem uso — cada retorno alimentou asserções que determinam o `rc`).

- **Inventário rápido das implementações** (pré-execução, como exigido):
  - `f_GetVersion` (`pr_win32.c` L2948, `IMPL`): retorna `(2195<<16)|5` = **5.0 build 2195 — Windows 2000**, versão declarada e documentada no código-fonte.
  - `f_GetVersionExA` (L2955, `IMPL`): escreve major=5, minor=0, build=2195, platform=2 (`VER_PLATFORM_WIN32_NT`) em `OSVERSIONINFOA` (valida 20 bytes; **não** toca `dwOSVersionInfoSize` — preserva); retorna `TRUE`.
  - `f_GetSystemInfo` (L2728, `IMPL_NOTE` "valores do espaço do Pórtico"): `memset` 48 bytes + arch=9 (AMD64), page=4096, min=0x10000, max=espaço-1, mask=1, nproc=1, ptype=8664, gran=65536, level=15; retorna `TRUE`.

## Execução inicial

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_version_systeminfo.exe` com o **runtime completamente intacto**:

```
exited=1 rc=42 steps=1 exec=525
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 42
```

- exit code **42** (fluxo completo — nenhuma asserção falhou); steps=1; exec=525; log=30 entradas (determinístico).
- **Primeiro blocker: NENHUM.** Sem erro de ABI, memória, estrutura ou import; sem ponto de parada; sem crash.

## Correção

**Nenhuma.** Nenhum blocker foi observado, portanto nenhum arquivo do runtime foi alterado (a regra proibia alterar antes de observar comportamento incorreto — nada foi observado).
- **Arquivo alterado**: nenhum do runtime (somente criados `realpe/hello_version_systeminfo.c` e o `.exe` em `data/`).
- **Função alterada**: nenhuma.
- **MD5 antes/depois**: `c9e8afa71214412be92bff3de0b35278` → `c9e8afa71214412be92bff3de0b35278` — **idêntico**.

## Resultado final

- **exit code**: 42 (contrato: 10 = GetVersion zero · 11 = major não decomponível · 12 = GetVersionExA FALSE · 13 = tamanho não preservado · 14 = versão inconsistente · 15 = canário corrompido · 16/17/18 = campos de GetSystemInfo zerados · 42 = completo).
- **steps**: 1 · **exec**: 525 (primeira) · **log**: 30 entradas.
- **Assertions passadas** (todas):
  1. `GetVersion()` != 0 e major = `LOBYTE(LOWORD(v))` != 0 (decomposição major/minor conforme o contrato do PE) ✓
  2. `GetVersionExA` retornou sucesso (conforme o contrato implementado = `TRUE`) ✓
  3. `dwOSVersionInfoSize` preservado == `sizeof(OSVERSIONINFOA)` ✓
  4. Campos principais acessíveis (`dwMajorVersion`, `dwMinorVersion`, `dwBuildNumber`, `szCSDVersion[0]`) e consistentes com `GetVersion` (major=5, minor=0 nos dois caminhos) ✓
  5. **Nenhuma escrita fora da estrutura**: canários de 16 bytes antes/depois de `OSVERSIONINFOA` intactos (0xA5) ✓
  6. `GetSystemInfo` sem crash com `SYSTEM_INFO` zerada; estrutura preenchida: `dwPageSize`=4096 ≠ 0 ✓, `dwAllocationGranularity`=65536 ≠ 0 ✓, `dwNumberOfProcessors`=1 ≠ 0 ✓
  7. Ponteiros/campos de arquitetura acessíveis sem acesso inválido (`lpMinimumApplicationAddress`, `lpMaximumApplicationAddress`, `wProcessorArchitecture`) ✓
- **Execuções/repetições**: **20/20 com rc=42** (log=30 em todas — determinístico) + 1 execução diagnóstico inicial = 21 execuções.

## Regressão

- **C tests**: `3392 verificações, 0 falhas` ✓
- **Swift tests**: `Executed 63 tests, with 0 failures` ✓
- **warnings**: 0 (receita do projeto: `gcc -std=c11 -Wall -Wextra` sobre os 20 `.c` do runtime) ✓
- **analyzer** (receita reproduzível `clang --analyze -std=c11`): **7 achados — os mesmos 7 PRÉ-EXISTENTES** do G49 (NonNullParam×2, BitwiseShift×1, DeadStores×4; lista completa no `docs/RELATORIO_GRUPO49.md` §15), **idênticos antes/depois** e **zero novos** (MD5 do runtime prova que nenhum código foi alterado). Mesma ressalva de receita já declarada no G49.
- **PE battery**: `15/15` — logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105` ✓
- **Checklist completa**: hello_wait_multiple=42 · hello_thread_timeout=42 · hello_mutex_timeout=42 · hello_thread_shared=42 · hello_thread=42 · hello_mutex=42 · hello_cs=42 · hello_virt=42 · hello_virt_protect=42 · hello_virt_query=42 · hello_qpc=42 · hello_heap=42 · hello_mbwc=42 · hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_stdio=5 · hello_gl12=42 · hello_sse=7 ✓

## Cobertura obtida

- `GetVersion`: **VALIDADO por PE real** — retorno não-nulo decomponível (5.0 build 2195, contrato documentado do ambiente).
- `GetVersionExA`: **VALIDADO por PE real** — sucesso, `OSVERSIONINFOA` preenchido sem corrupção, tamanho preservado, campos acessíveis e consistentes com `GetVersion`, sem escrita fora da estrutura.
- `GetSystemInfo`: **VALIDADO por PE real** — estrutura 48 bytes (layout x64) preenchida, `dwPageSize`/`dwAllocationGranularity`/`dwNumberOfProcessors` não-nulos, ponteiros e arquitetura acessíveis.

## Limitações restantes

- `GetVersionExA` foi validado somente com `OSVERSIONINFOA` (276 bytes) — o caminho `OSVERSIONINFOEXA`/`GetVersionExW` **não** foi validado (não é alvo desta etapa).
- O handler de `GetVersionExA` **não valida** `dwOSVersionInfoSize` de entrada (não há comportamento observado para tamanho errado); o PE provou preservação do campo e ausência de escrita fora do contrato, nada além disso.
- `szCSDVersion` validado apenas como acessível — o runtime não o preenche (conteúdo vindo do padrão de memória do chamador).
- A versão emulada é **deliberadamente própria** (5.0/2195) e `GetSystemInfo` usa "valores do espaço do Pórtico" (`IMPL_NOTE`: nproc=1, ptype=8664) — **não** se afirma correspondência a hardware/versões do Windows real além do contrato documentado.
- Os 7 achados pré-existentes do analyzer permanecem (correção exigiria alterar o runtime — fora do escopo desta etapa).
- **Não se afirma compatibilidade geral com Windows nem com jogos comerciais.**

**Não se implementou o próximo grupo.**
