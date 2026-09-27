# Relatório — Fase 7: Execução Real de Software Windows Compatível

Data: 2026-09-22 · Projeto Portico · Ambiente: Linux (SPM/gcc) + iOS (Xcode)

## Marco central atingido

**PE → CPU → Memory → Imports → Win32 → Process → Exit funcionando de verdade**
(sem simulação, sem fingimento). Prova com os PEs de teste do próprio projeto:

1. `pr_winhello[0]` — chama `kernel32!GetTickCount64` via IAT e
   `ExitProcess(42)`: processo inicia, executa na CPU interpretada, resolve
   imports para thunks stdcall reais (`mov eax, idx; INT 0x2E; ret N`), despacha
   Win32 e encerra com exit code 42.
2. `pr_winhello[1]` — cadeia GDI: `CreateCompatibleDC → CreateSolidBrush →
   SelectObject → PatBlt(PATCOPY)` desenha um retângulo vermelho em pixels
   reais da superfície, que chega ao `GfxFrame.surface` (caminho Metal).

PLAY da biblioteca: `LibraryView → GameProfile → RuntimeManager →
WindowsPEBackend → pr_peproc → frames → ExitProcess → onFinished` — testado.

## Tabela de status

| Componente        | Status    | Detalhe |
|-------------------|-----------|---------|
| CPU (IA-32)       | PARTIAL   | Interpretador com guard R/W/X por página; MOV/ALU/shifts/pilha/branch/MUL/DIV/INT/HLT/CPUID/BSWAP; sem 16-bit, far call, FPU, x64 |
| PE Loader         | PARTIAL   | PE32 executado (carga, seções com prot, IAT, entry); PE64 analisado e recusado com diagnóstico; sem relocations |
| Win32             | PARTIAL   | 34 APIs reais (kernel32 28 + gdi32 6); demais → UNSUPPORTED + log (Module/Function) |
| DLL (resolver)    | PARTIAL   | Imports por nome → thunk real; ordinal → recusa com diagnóstico |
| Graphics (GDI)    | PARTIAL   | DC + pincel + PatBlt → superfície XRGB; BitBlt/StretchBlt/bitmaps/fontes não |
| Metal             | PARTIAL   | Pipeline + self-test intactos; `GfxFrame.surface` testado; desenho no renderer iOS é pendência de app |
| Audio             | UNSUPPORTED (Win32) | waveOut/DirectSound não; áudio do runtime PXP segue funcionando |
| Input             | PARTIAL   | InputState → runtime pronto; APIs Win32 de input/mensagens não |
| JIT               | UNAVAILABLE | Detectado de verdade (W^X/codesigning iOS); UI mostra JIT NOT AVAILABLE; arquitetura aceita backend futuro |
| **EXECUÇÃO DE PE**| **WORKING** | PE32 mínimo ponta a ponta: load → init → run → pause/resume → stop → shutdown → exit code |

## Testes executados

`make test` (build + testes + validações) — **0 falhas**:

| Suíte                          | Quantidade | Resultado |
|--------------------------------|-----------:|-----------|
| C — 11 suítes (9 antigas + test_vm + test_peproc) | 475 checks | ✅ 0 falhas |
| Swift — 5 suítes (33 testes antigos + 9 novos)    | 42 testes  | ✅ 0 falhas |

Novos testes C: `test_vm` (memória virtual: map/unmap/protect/alloc/translate/
faults) e `test_peproc` (hello→42; GDI→pixels; API ausente; DLL ausente;
memória inválida; executável inválido; PE64; instrução fora do subconjunto —
todos com diagnóstico EXECUTION STOPPED).
Novos testes Swift (`WindowsExecutionTests`): exit code 42, ciclo pause/resume/
stop, API não suportada, DLL ausente, memória inválida, executável inválido,
x64 recusado, superfície GDI→GfxFrame, GameProfile→RuntimeManager→PLAY→Exit.

Build: SPM Linux (gcc C11 `-Wall -Wextra`, 0 warnings; Swift 6.1.2) e
`Portico.xcodeproj` regenerado (`scripts/gen_xcodeproj.py`).
Static analysis: compilador C em `-Wall -Wextra -Werror=implicit-function-declaration` sem warnings.

## Arquivos criados

- `Sources/PorticoRuntime/include/portico/pr_vm.h` + `src/pr_vm.c` — memória virtual do processo
- `Sources/PorticoRuntime/include/portico/pr_peproc.h` + `src/pr_peproc.c` — processo PE real
- `Sources/PorticoRuntime/include/portico/pr_winhello.h` + `src/pr_winhello.c` — PEs de teste
- `Tests/PorticoRuntimeTests/test_vm.c`, `test_peproc.c`
- `Tests/PorticoCoreTests/WindowsExecutionTests.swift`
- `docs/RELATORIO_FASE7.md` (este)

## Arquivos modificados

- `pr_cpu.h/c` — guard de permissões R/W/X (PXP intocado), `pr_cpu_mem`
- `pr_win32.h/c` — stdcall_bytes, gdi32 real (6 APIs), bind_vm/bind_thunks/
  call_entry/index_of/surface, lookup case-insensitive, caminhos guest
- `pr_pe.h/c` — `pr_pe_import_iat_rva`
- `ExecutionBackend.swift` — `WindowsPEBackend` com ciclo real (load/initialize/
  run/pause/resume/stop/shutdown/stepFrame/graphics/logs) sobre `pr_peproc`
- `DiagnosticsReport.swift` — windowsPEExecution = PARTIAL (real)
- `RuntimeAndHonestyTests.swift` — recusa honesta agora cobre x64 (3 asserções
  atualizadas do contrato "não integrada" da Fase 5; os 33 testes continuam)
- `docs/STATUS.md`, `docs/LIMITATIONS.md`, `docs/BACKEND_INTEGRATION.md`, `README.md`

## Garantias de honestidade (regra central)

- Nenhum jogo Windows é declarado "em execução" sem execução real de CPU.
- APIs não implementadas nunca retornam sucesso fingido — o processo PARA com
  "EXECUTION STOPPED / Reason / Module / Function" e log.
- JIT nunca é afirmado sem detecção real (indisponível no iOS — informado).
- Sandbox iOS respeitado: memória virtual do processo é memória própria do app
  (sem mmap executável; stubs escritos e depois RX — W^X).
- Nada foi copiado de implementações proprietárias; componentes isolados com
  origem/licenças documentadas (docs/LICENSES.md).

## Limitações restantes (detalhadas em docs/LIMITATIONS.md)

PE64/x64; relocations; import por ordinal; FPU/16-bit; Win32 além dos 34
implementados (BitBlt, MessageBoxA, janelas/mensagens, waveOut…); superfície
GDI fixa 320×240; `MetalGameRenderer` desenhar `GfxFrame.surface` (pendência
app-side iOS).
