# RELATÓRIO — GRUPO 33: inventário e validação real da conversão Unicode

**Desfecho: CONCLUÍDO — capacidade escolhida = `MultiByteToWideChar` +
`WideCharToMultiByte` (conversão Unicode). `hello_mbwc.exe → rc=42` após corrigir
o primeiro (e único) blocker real: a validação de comprimento do modo
NUL-terminated (`-1`). Conversão Unicode = VALIDADA POR PE REAL (CP_UTF8/BMP,
idioma `-1`).**

---

## 1. Baseline (início do grupo)

```text
C tests:     3392 / 0        Swift tests:   63 / 0
warnings:       0            analyzer:       0
PEs:     15/15 verdes
hello_input=42 · hello_gl11=42 · hello_sse=7 · hello_gl12=42 · hello_stdio=5
hello_file=42 · hello_file_w=42 · hello_file_seek=42
```

## 2. Inventário (FASE 1)

Superfície real: **113 APIs na tabela IMPL** de `pr_win32.c` (kernel32, msvcrt,
gdi32, user32, opengl32) + 33 TODOs honestos + módulos irmãos (GL, win32x).
Matriz (síntese por capacidade):

| API/capacidade | Implementada | Unit-testada | PE real | Candidato |
|---|---|---|---|---|
| File I/O (CreateFileA/W, Read/WriteFile, GetFileSize, CloseHandle, SetFilePointer) | ✓ | ✓ | ✓ G29–G32 (`hello_file*`) | — |
| VFS/fs_root | ✓ | ✓ | ✓ | — |
| stdio/console (GetStdHandle + captura) | ✓ | ✓ | ✓ (`hello_stdio`) | — |
| Loader/módulos (LoadLibraryA, GetProcAddress, FreeLibrary, GetModuleHandleA) | ✓ | ✓ | ✓ (`hello_app` + windowed) | — |
| CRT msvcrt (exit/printf/malloc/…) | ✓ | ✓ | ✓ (todos) | — |
| OpenGL 1.1+wgl · GDI · User32 janelas/mensagens · Input · Timers · SSE · CPU x64 | ✓ | ✓ | ✓ (`hello_gl*`, `hello_gdi`, `hello_user`, `hello_input`, `hello_sse`) | — |
| Erro (Get/SetLastError) | ✓ | ✓ | implícito | — |
| **Conversão Unicode (`MultiByteToWideChar`/`WideCharToMultiByte`)** | **✓** (CP 0/1252/65001, BMP) | **✗** | **✗** | **✓✓** |
| Heap (GetProcessHeap/HeapAlloc/HeapSize/HeapFree) | ✓ (alloc no VM do guest) | ✓ | ✗ | ✓ |
| Memória virtual (VirtualAlloc/Free/Protect/Query) | ✓ | parcial | ✗ | ✓ |
| Tempo (QueryPerformanceCounter/Frequency) | ✓ | ✓ | ✗ (GetTickCount64 sim) | ✓ |
| Linha de comando (GetCommandLineA/W) · GetCurrentDirectoryA · GetVersion | ✓ | ✓ | ✗ | — fino |
| Seções críticas · IsDebuggerPresent · Sleep · OutputDebugString · TlsGetValue | ✓ | ✗ | ✗ | — fino |
| GetModuleFileNameA · ReadFileEx · CreateThread · WaitForSingleObject · TlsAlloc | ✗ TODO | — | — | — (não implementadas) |
| Reg* · COM · Shell · ws2_32 · TextOutA · CreateFontA | ✗ TODO | — | — | — (proibidos/fora) |

Observação: a tabela TODO guarda entradas obsoletas (`CloseHandle`,
`SetFilePointer`, `MultiByteToWideChar`, `WideCharToMultiByte`, `SetTimer` já
estão implementados) — a IMPL é a verdade operacional.

## 3. Capacidade escolhida

**Conversão Unicode — `MultiByteToWideChar` + `WideCharToMultiByte`** (um par
inseparável de uma capacidade: codificar/decodificar strings A/W).

- **Por quê**: (1) implementação real e substancial já existente (UTF-8,
  CP1252/ACP, BMP, modo consulta, diagnóstico honesto fora do subset); (2)
  dependências mínimas — só `pr_win32_ptr` + memória do guest; (3) PE mínimo
  possível (round-trip de 9 bytes); (4) **maior valor de validação**: a
  superfície A/W é a assinatura do Win32 e era a maior lacuna relativa
  (implementada, **zero** cobertura unitária e zero PE) — e fecha a linha Unicode
  aberta no G31.
- **Dependências**: nenhuma além das já validadas (CPU/loader/memória do guest).
- **PE usado**: `realpe/hello_mbwc.c` → `hello_mbwc.exe`.

## 4. PE

- **Fonte**: `realpe/hello_mbwc.c` (44 linhas).
- **Binário**: `Tests/PorticoRuntimeTests/data/hello_mbwc.exe`
  (`x86_64-w64-mingw32-gcc -O2 -s`, 0 warnings; entry `0x13F0`, CUI 5.2).
- **Imports**: `KERNEL32.dll` = **MultiByteToWideChar, WideCharToMultiByte**
  (só o que o fluxo precisa) + `msvcrt.dll` (CRT padrão dos `hello_*`).
- **Fluxo** (idioma real de todo programa Win32 — strings NUL-terminated com
  comprimento **-1**): `MultiByteToWideChar(CP_UTF8, "winos_ñ", -1, wbuf, 32)`
  → conferir 8 unidades (ñ = `0x00F1`, NUL) → `WideCharToMultiByte(CP_UTF8,
  wbuf, -1, out, 32, NULL, NULL)` → conferir os 9 bytes UTF-8 de volta → `42`.
- **Contrato de retorno**: 42 = OK; 10 = MB2WC retornou 0; 11 = contagem ≠ 8;
  12 = unidades erradas; 13 = WC2MB retornou 0; 14 = contagem ≠ 9; 15 = bytes ≠
  `"winos_ñ"` UTF-8 (`77 69 6e 6f 73 5f c3 b1 00`, bytes explícitos no fonte —
  independente de encoding do compilador).

## 5. Execução (FASE 4 — baseline, runtime intocado)

| Métrica | Valor |
|---|---|
| `rc` | **10** (etapa `MultiByteToWideChar`) |
| Instruções | **311** (`dbg_diag`) · log=28 (`dbg_input`) |
| API/etapa da 1ª falha | **`MultiByteToWideChar`** retornou 0 (`PR_ERR_FAULT`) |
| Erro Win32/diagnóstico | `PR_ERR_FAULT` na validação de `src` |

**Primeiro blocker real**: a validação de comprimento do modo NUL-terminated —
com `cbMultiByte = -1`, o handler chamava
`pr_win32_ptr(ctx, src, (uint32_t)(-1))` = tentativa de validar **4 GB** →
`NULL` → falha. O ramo `if (slen < 0)` abaixo (com `strlen` direto) provava que
o modo era intencional, mas **inalcançável**. O mesmo padrão defeituoso existia
na entrada de `f_WideCharToMultiByte` (`slen*2` gigante invalidava a varredura
segura que vinha logo abaixo).

## 6. Correção (FASE 5)

- **Causa**: interpretação de `cbMultiByte/cchWideChar = -1` como comprimento
  absoluto gigante na validação inicial de ponteiro.
- **Arquivos alterados**: `Sources/PorticoRuntime/src/pr_win32.c` — **2 sítios,
  1 classe de defeito** (validação do modo NUL-terminated do par de conversão —
  mesmo critério de classe único usado no G30).
- **Patch mínimo**: (a) `f_MultiByteToWideChar` — com `slen < 0`: validar 1 byte
  e varrer **com `pr_win32_ptr` por byte** até o NUL (cap `0x4000`, padrão do
  irmão `f_WideCharToMultiByte`/`f_CreateFileA`), eliminando o `strlen` inseguro;
  comprimento explícito preservado como estava. (b) `f_WideCharToMultiByte` —
  com `slen < 0`: validar **1 unidade** (2 bytes) na entrada; a varredura segura
  existente passa a ser alcançável.
- **Justificativa**: é o idioma padrão de uso real da API (todo programa Win32
  passa `-1` para strings NUL-terminated); sem ele a conversão é inutilizável por
  qualquer PE real. Nada mais foi tocado: código-pages, BMP, modo consulta,
  `fs_root`, handles e demais handlers **intocados**.
- **Nenhuma funcionalidade futura foi antecipada** (nada além do defeito
  observado; lacunas conhecidas — modo consulta do `WC2MB` com retorno por
  unidade vs byte fora de CP_ASCII, não-BMP, codepages fora de 0/1252/65001 —
  permanecem documentadas e não foram alteradas).

## 7. Regressão (FASE 6)

```text
make c-test            → 3392 verificações, 0 falhas
swift test             → Executed 63 tests, with 0 failures
gcc -Wall -Wextra      → 0 warnings
gcc -fanalyzer (build/analyzer33.err) → 0 diagnósticos
PE battery (15 PEs)    → 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
hello_input (sem noinput) → rc=42 log=105
hello_gl11             → rc=42   ✓
hello_sse              → rc=7    ✓
hello_gl12             → rc=42   ✓
hello_stdio            → rc=5    ✓
hello_file             → rc=42   ✓
hello_file_w           → rc=42   ✓
hello_file_seek        → rc=42   ✓
hello_mbwc             → rc=42   ✓ (NOVO — objetivo do grupo)
```

## 8. Resultado

| Item | Classificação |
|---|---|
| `MultiByteToWideChar` (CP_UTF8, BMP, `cbMultiByte = -1`/explícito) | **VALIDADA POR PE REAL** · IMPLEMENTADA |
| `WideCharToMultiByte` (CP_UTF8, BMP, `cchWideChar = -1`) | **VALIDADA POR PE REAL** · IMPLEMENTADA |
| Conversão — CP1252/ACP | IMPLEMENTADA · NÃO validada por PE real (sem blocker que exigisse) |
| Conversão — modo consulta (só tamanho) / não-BMP / outras codepages | NÃO VALIDADOS (lacunas documentadas; não-BMP recusa honesta) |

**Capacidade "conversão Unicode" (subset CP_UTF8/BMP com o idioma real `-1`) =
VALIDADA POR PE REAL.**

Sem declaração de compatibilidade geral com aplicações Windows nem com jogos
comerciais.

## 9. Próximo blocker real

**"Nenhum próximo blocker real foi derivado deste grupo."**
