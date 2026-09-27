# RELATÓRIO — GRUPO 35: inventário de cobertura PE e escolha do próximo alvo

**Desfecho: CONCLUÍDO — inventário somente-leitura (runtime byte-for-byte
idêntico). Próximo alvo escolhido: TEMPO — `QueryPerformanceFrequency` +
`QueryPerformanceCounter` (implementadas, unit-testadas, SEM PE real). Nenhuma
funcionalidade foi implementada ou corrigida neste grupo.**

---

## 1. Baseline (FASE 1)

```text
C tests:     3392 / 0        Swift tests:   63 / 0
warnings:       0            analyzer:       0
PE battery: 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
hello_input=42 (log=105) · hello_gl11=42 · hello_sse=7 · hello_gl12=42
hello_stdio=5 · hello_file=42 · hello_file_w=42 · hello_file_seek=42
hello_mbwc=42 · hello_heap=42
```

Nota metodológica: `hello_input` tem contrato com feed de entrada (via
`dbg_input`/bateria `noinput`); sob `dbg_diag` sem feed espera até o timeout —
comportamento conhecido, não-regressão.

## 2. Inventário das capacidades (FASE 2 — estado real do código)

- **Superfície Win32 registrada**: **194 entradas** em `pr_win32.c` —
  `IMPL` 113 + `IMPL_NOTE` 59 + `IMPL_ORD` 5 + `IMPL_FX` 13 + `DATA_SYM` 4
  (todas com status `PR_WIN32_IMPLEMENTED` e função real) + **33 TODOs
  honestos** (não implementados → parada declarada). **182 handlers `f_*`**.
  *Correção de contagem do G33*: "113" media apenas o macro `IMPL(`; a pilha de
  janelas/mensagens (`RegisterClassA`, `GetMessageA`, `CreateWindowExA`, …) está
  em `IMPL_NOTE` com o mesmo status de implementação real.
- **Classificação (1=IMP+unit+PE · 2=IMP+unit, sem PE · 3=IMP+teste parcial ·
  4=não implementada · 5=fora do escopo)**:
  - **1**: File I/O, VFS, SetFilePointer(FILE_BEGIN), Heap, stdio/console,
    loader/módulos, GDI, User32 janelas/mensagens, Input, Timers, OpenGL 1.1+wgl,
    SSE, CPU x64, CRT msvcrt.
  - **2**: **Tempo QPC/QPF**, memória virtual (Alloc/Free/Protect; Query =
    parcial/3), linha de comando A/W, GetCurrentDirectoryA, GetVersion/ExA,
    lstrcpyA/lstrcpynA/lstrlenA, GetLastError/SetLastError (sem PE dedicado),
    GetCurrentProcessId.
  - **3**: **Seções críticas** (CS 48B real, sem testes), Sleep (nanosleep real),
    VirtualQuery, TlsGetValue (sem `TlsAlloc` = incompleto), GetCurrentThreadId,
    IsDebuggerPresent, OutputDebugStringA, TerminateProcess,
    SetUnhandledExceptionFilter, IsDBCSLeadByteEx.
  - **4**: GetModuleFileNameA, ReadFileEx, HeapReAlloc, CreateThread,
    WaitForSingleObject, TlsAlloc, TextOutA, CreateFontA, GetSystemMetrics,
    MultiByte→ (já era; hoje IMPL), etc. (tabela TODO).
  - **5**: Reg* (registry), Co* (COM), Shell*, ws2_32 (sockets), áudio,
    DirectX, Vulkan, jogos comerciais.
- **Exceção de cobertura (registrada)**: conversão Unicode MB2WC/WC2MB = PE REAL
  ✓ (G33) mas **sem** testes unitários — candidata a teste unitário futuro.

## 3. Matriz Implementado / Unit / PE Real (critério estrito de FASE 3)

Evidência PE real exigida: um PE x64 **chamou** a capacidade e o fluxo esperado
**se completou** (rc de contrato). Uso indireto (CRT/loader) não conta.

| Capacidade | Implementada | Unit test | PE real | Estado |
| --- | :-: | :-: | :-: | --- |
| File I/O (CreateFileA/W, Read/WriteFile, GetFileSize, CloseHandle) | ✓ | ✓ | ✓ | **VALIDADO** (G29–G31 `hello_file*`) |
| SetFilePointer (FILE_BEGIN) | ✓ | ✓ | ✓ | **VALIDADO** (G32 `hello_file_seek`) |
| Unicode MB2WC/WC2MB (CP_UTF8/BMP/-1) | ✓ | ✗ | ✓ | **VALIDADO** (G33 `hello_mbwc`; unit = lacuna) |
| Heap (GetProcessHeap/HeapAlloc/HeapSize/HeapFree) | ✓ | ✓ | ✓ | **VALIDADO** (G34 `hello_heap`) |
| VFS/fs_root | ✓ | ✓ | ✓ | **VALIDADO** (fluxos de arquivo) |
| stdio/console (GetStdHandle + captura) | ✓ | ✓ | ✓ | **VALIDADO** (`hello_stdio`) |
| CRT msvcrt (exit/printf/malloc) | ✓ | ✓ | ✓ | **VALIDADO** (`hello_stdio` + cadeia de todos) |
| Loader/módulos (LoadLibraryA/GetProcAddress/FreeLibrary/GetModuleHandleA) | ✓ | ✓ | ✓ | **VALIDADO** (`hello_app` + windowed) |
| GDI (BitBlt/SetPixel/GetPixel/brushes/DCs) | ✓ | ✓ | ✓ | **VALIDADO** (`hello_gdi` + GL) |
| User32 janelas/mensagens (IMPL_NOTE) | ✓ | ✓ | ✓ | **VALIDADO** (`hello_user/app/input`) |
| Input (GetKeyState/GetAsyncKeyState) | ✓ | ✓ | ✓ | **VALIDADO** (`hello_input`) |
| Timers (SetTimer/KillTimer + WM_TIMER) | ✓ | ✓ | ✓ | **VALIDADO** (`hello_input`) |
| OpenGL 1.1 + wgl | ✓ | ✓ | ✓ | **VALIDADO** (`hello_gl1–12`) |
| SSE | ✓ | ✓ | ✓ | **VALIDADO** (`hello_sse`) |
| CPU x64 | ✓ | ✓ | ✓ | **VALIDADO** (todos) |
| **Tempo — QPC/QPF** | **✓** | **✓** | **✗** | **AUDITADO → ALVO G36** |
| Tempo — GetTickCount64 | ✓ | ✓ | ~ | PE (`hello_real`) chama sem verificar valor |
| **Virtual Memory (VirtualAlloc/Free/Protect/Query)** | ✓ | parcial | **✗** | AUDITADO → candidato 2º |
| **Critical Section (Init/Enter/Leave/Delete, 48B)** | ✓ | ✗ | **✗** | AUDITADO → candidato 3º |
| TLS | parcial (só TlsGetValue) | ✗ | ✗ | 3/4 + fora do escopo |
| Module — GetModuleFileNameA | ✗ | — | — | 4 (TODO) |
| Threads (CreateThread/WaitForSingleObject) | ✗ | — | — | 4 + fora do escopo |
| Linha de comando (GetCommandLineA/W) | ✓ | ✓ | ✗ | 2 |
| Diretório atual (GetCurrentDirectoryA) | ✓ | ✓ | ✗ | 2 |
| Versão (GetVersion/GetVersionExA) | ✓ | ✓ | ✗ | 2 |
| Strings (lstrcpyA/lstrcpynA/lstrlenA) | ✓ | ✓ | ✗ | 2 |
| Erro (Get/SetLastError) | ✓ | ✓ | ✗ (implícito) | 2 |
| Thin (Sleep, IsDebuggerPresent, OutputDebugStringA, TerminateProcess, …) | ✓ | ~ | ✗ | 2/3 |
| Registry/COM/Shell/sockets/TextOutA/CreateFontA/áudio/D3D/Vulkan | ✗ | — | — | 4/5 (fora) |

## 4. Capacidades ainda sem PE real

**Tempo QPC/QPF** · Virtual Memory · Critical Section · GetCommandLineA/W ·
GetCurrentDirectoryA · GetVersion/ExA · lstrcpy/lstrcpyn/lstrlen ·
Get/SetLastError (dedicado) · GetCurrentProcessId/ThreadId · Sleep · thin
(IsDebuggerPresent, OutputDebugStringA, TerminateProcess,
SetUnhandledExceptionFilter, IsDBCSLeadByteEx) · TlsGetValue (incompleto).
Ordem de valor para cobertura real: **Tempo > Virtual Memory > Critical Section
> resto (finas)**.

## 5. Capacidade escolhida para o próximo grupo

**TEMPO — contadores de alta resolução: `QueryPerformanceFrequency` +
`QueryPerformanceCounter`.**

## 6. Justificativa técnica da escolha

1. **Implementada**: `f_QueryPerformanceFrequency`/`f_QueryPerformanceCounter`
   (pr_win32.c) — comportamento real (`mono_ns()` monotônico do host; frequência
   declarada `1 000 000 000` Hz em nanossegundos, coerente com o contador).
2. **Sem PE real**: zero chamadas em `realpe/*.c` (o irmão `GetTickCount64` é
   chamado em `hello_real` sem verificação de valor — não conta como validação
   de semântica).
3. **Isolável**: 2 APIs, 1 out-param cada, sem VFS/janelas/GL/heap.
4. **Sem cadeia de dependências**: só `pr_win32_ptr` + relógio do host.
5. **Relevância real máxima**: QPC/QPF é o contador de alta resolução padrão de
   toda aplicação/jogo Windows real (timing de frame, profiling) — muito mais
   usado que GetTickCount64 em software moderno.
6. **PE mínimo e determinístico**: ~30 linhas, contrato `10..14`.
7. **Não exige implementação antecipada** de nada.

Candidatos equivalentes descartados: **Virtual Memory** — superfície maior
(flags `MEM_*`/`PAGE_*`, `MEMORY_BASIC_INFORMATION` de 48 bytes,
`VirtualQuery` sem teste unitário) e listada em "não tocar" deste grupo;
**Critical Section** — implementada, mas semântica quase vazia sem threads
(validaria apenas "não estourar"); **GetCommandLineA/GetCurrentDirectoryA/
GetVersion** — finas, valor de cobertura menor.

## 7. Dependências do próximo PE

| Item | Detalhe |
|---|---|
| Capacidade | Tempo — QPC/QPF |
| Handlers | `f_QueryPerformanceFrequency` (pr_win32.c), `f_QueryPerformanceCounter` |
| ABI | `BOOL QueryPerformanceFrequency(LARGE_INTEGER* lpFrequency)`; `BOOL QueryPerformanceCounter(LARGE_INTEGER* lpPerformanceCount)` — out-param **`LARGE_INTEGER` = 8 bytes** (ABI correta; **não** é `LPDWORD`) |
| Estado atual | IMPLEMENTADOS (comportamento real) + UNIT-TESTADOS (`test_win32.c`: freq == 1e9; counter > 0) + **SEM PE REAL** |
| Dependências | `pr_win32_ptr` (validação do out-param), `mono_ns()` (host) — nenhuma outra |
| Por que é um bom próximo alvo | ver §6 |
| **PE mínimo previsto** | `realpe/hello_qpc.c` → `Tests/PorticoRuntimeTests/data/hello_qpc.exe`: `QueryPerformanceFrequency(&f)` (10 = FALSE; 11 = f==0; conferir f==1e9 ou f>0) → `QueryPerformanceCounter(&t1)` (12 = FALSE) → `QueryPerformanceCounter(&t2)` (13 = FALSE) → `t2 >= t1` (14 = não-monotônico) → `return 42`. Imports: só QPF/QPC + CRT. Sem threads/arquivos/GUI/GL/sockets |
| **Blocker estático evidente** | **NENHUM** — a ABI `LARGE_INTEGER` de 8 bytes já foi auditada como correta (G30); o caso LPDWORD defeituoso não se aplica aqui. Nada a corrigir por antecipação |

## 8. Regressão (FASE 6)

```text
Integridade do runtime: md5(Sources/PorticoRuntime/src+include) =
  4a23bc7bf8bc2d6e0ff55969290bfcd2  (ANTES == DEPOIS — byte-for-byte idêntico)
make c-test            → 3392 verificações, 0 falhas (re-executado pós-inventário)
swift test             → Executed 63 tests, with 0 failures
gcc -Wall -Wextra      → 0 warnings
gcc -fanalyzer (build/analyzer35.err) → 0 diagnósticos
PE battery (15 PEs)    → 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
hello_input=42 · hello_gl11=42 · hello_sse=7 · hello_gl12=42 · hello_stdio=5
hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_mbwc=42 · hello_heap=42
```

## 9. Estado final

- **Nenhuma API foi implementada, corrigida ou alterada** — o grupo foi
  somente-leitura (regra principal cumprida; md5 idêntico prova).
- Cobertura: 15 capacidades VALIDADAS POR PE REAL; **Tempo (QPC/QPF)** definido
  como próximo alvo técnico; Virtual Memory e Critical Section como reservas.
- Lacunas documentadas preservadas (não antecipadas): flags de heap, consulta
  MB/WC, não-BMP, `lpDistanceToMoveHigh`, `FILE_CURRENT/FILE_END` por PE,
  `GetTickCount64` sem verificação de valor, TlsGetValue incompleto.

Não há declaração de compatibilidade geral com aplicações Windows nem com jogos
comerciais.
