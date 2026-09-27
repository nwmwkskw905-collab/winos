
## EXPANSÃO PEs x64 REAIS (FASES 1–7) — CONCLUÍDA (2026-09-23)

Base progressivamente capaz de executar PEs x64 reais — caminho verificável:
PE x64→loader→relocations/imports→CPU x64→memória/stack/heap→Win32→GDI→
bitmap/surface→BitBlt/StretchBlt→GfxFrame→Metal(bridge)→frame→encerramento.
**AINDA SEM compatibilidade declarada com jogos comerciais** (não testados).

- **CPU x64 (pr_cpu64)**: ALU completa 8/16/32/64-bit com RFLAGS (CF/PF/ZF/SF/
  OF), CMP/TEST/INC/DEC, shifts/rotates, LEA, MOVZX/MOVSX/MOVSXD, XCHG, Jcc/
  SETcc (16 condições), CALL/RET rel32/imm, ENTER/LEAVE, MUL/DIV, endbr64.
  Fault com opcode, RIP, **bytes da instrução** e motivo.
- **Memória/processo**: páginas com perms R/W/X (pr_vm), stack x64, heap
  (HeapAlloc→regiões w32heap), VirtualAlloc/VirtualFree/**VirtualProtect** reais;
  processo TEM ImageBase, módulos, stack, heap, **environment, command line e
  working directory** (pr_win32_env_set/set_cmdline/set_cwd + APIs Win32).
- **Win32**: **53 APIs implementadas** (eram 40): +GetModuleHandleW,
  GetCommandLineW, GetEnvironmentVariableA/W, GetSystemInfo, VirtualProtect,
  GetCurrentDirectoryA, DeleteDC, SetPixel, GetPixel, StretchBlt, MessageBoxA/W
  (sem UI — log + IDOK, documentado). NÃO implementadas → NOT_IMPLEMENTED.
- **GDI→GfxFrame→Metal**: StretchBlt SRCCOPY vizinho-mais-próximo; SetPixel/
  GetPixel (COLORREF↔XRGB); frame 320x240 testado ponta a ponta (PE 8);
  MetalFrameUpload.onStaged = gancho [METAL] testável sem GPU.
- **PEs de teste (FASE 5)**: variante 7 (memória), 8 (GDI+StretchBlt+BitBlt+
  SetPixel→frame), 9 (Kernel32+GDI+MessageBox+ambiente) — e2e C e Swift.
- **Logs estruturados (FASE 6)**: [PE][CPU][MEM][WIN32][GDI][METAL][PROCESS]
  com módulo, endereço, RIP, API, opcode, falha de memória, frame e **tempo de
  execução** ("exit code N after X ms").
- **Qualidade**: **957 checks C + 50 testes Swift + 0 falhas + 0 warnings**
  (`-Wall -Wextra -Wshadow` e `-fanalyzer` limpos). Nenhum teste removido.

Relatório completo: `docs/RELATORIO_EXPANSAO_X64.md`.

## PRIMEIRO MARCO VISUAL REAL (CONCLUÍDO com Metal PARTIAL — 2026-09-22)

Pipeline completo comprovado por execução: **PE visual x64 (fundo branco +
2 retângulos) → CPU x64 (subconjunto straight-line) → Win32 (CreateCompatibleDC/
CreateCompatibleBitmap/SelectObject/CreateSolidBrush/PatBlt/BitBlt) → GDI bitmap →
BitBlt SRCCOPY → superfície do processo → GfxFrame.surface (XRGB8888) → estágio de
submissão Metal (BGRA8) → exit 0**. 8 etapas com execution log
(`[PE] loaded` … `[GDI] BitBlt` … `[GDI] surface updated` … `[PROCESS] exit code 0`).

- CPU x64: `pr_cpu64` (interpretador mínimo; fora do subconjunto → fault com
  opcode/RIP/endereço). Win32: 40 APIs reais (+CreateCompatibleBitmap/+BitBlt).
- GDI: bitmap com buffer/stride próprios; BitBlt SRCCOPY com clipping testado.
- Metal: PONTE escrita (`MetalGameRenderer`: textura .shared + replaceRegion +
  blit fullscreen — renderer existente preservado). Apresentação em tela =
  **PARTIAL** (requer iPhone/Xcode; não comprovável no Linux).
- Build: **741 checks C + 47 testes Swift, 0 falhas, 0 warnings**. Detalhes:
  `docs/RELATORIO_VISUAL.md`.
- NÃO declarado: jogos Windows funcionando (apenas o PE de teste do projeto).

## Windows Compatibility — PE32+ / Relocations / Ordinal (CONCLUÍDO — 2026-09-22)

- **PE32+/x64 — LOADER completo**: validação, headers PE32+ (opt 240, ImageBase
  64-bit), sections, entry point, imports com thunks de 8 bytes, relocations
  DIR64, alignment validado (potências de 2; Section/File conforme a spec) e
  proteções por região. EXECUÇÃO x64 = recusada com diagnóstico
  `Architecture: x86-64` (sem backend de CPU x64 — sem simulação).
- **Relocations reais**: parser Basereloc (blocos/entradas), tipos ABSOLUTE/
  HIGHLOW/DIR64, validação de limites (slots fora da imagem, blocos inválidos,
  tipos desconhecidos → erro no diagnóstico). Carga na base preferida OU
  relocação automática para um vão livre; imagem SEM tabela + base ocupada →
  recusa honesta. E2E testado: programa roda em base relocalizada (exit 42).
- **Importação por nome + por ordinal**: resolver completo
  PE → Import Table → DLL Resolver → Win32 Dispatcher → Implementação.
  Ordinais apenas com correspondência PÚBLICA e estável do ABI (winsock.def:
  htonl@8, htons@9, ntohl@14, ntohs@15 — ws2_32); ordinal desconhecido →
  EXECUTION STOPPED ("nunca se adivinha ordinais"). GetProcAddress também
  aceita MAKEINTRESOURCE (ordinal).
- **Diagnóstico completo**: EXECUTION STOPPED / Reason / Module / Function /
  Address / Architecture (+ Technical).
- Win32: 38 APIs reais (kernel32 28 + gdi32 6 + ws2_32 4).

Testes: **545 checks C + 42 Swift, 0 falhas, 0 warnings**.


## Fase 7 — EXECUÇÃO REAL DE SOFTWARE WINDOWS COMPATÍVEL (CONCLUÍDA — 2026-09-22)

**EXECUÇÃO DE PE = WORKING** (PE32/IA-32 mínimo, ponta a ponta, sem simulação):

- Cadeia real: GameLibrary → GameProfile → PE Loader → ExecutionBackend →
  CPU → Win32 → Graphics Translation → Metal (superfície) → iPhone.
- `pr_vm`: memória virtual do processo (regiões R/W/X, map/unmap/protect/alloc/
  translate; por página; backing = memória física da CPU).
- `pr_peproc`: processo PE completo (carga → imports IAT→thunks stdcall reais
  `mov eax, idx; INT 0x2E; ret N` → pilha → entry point → step → ExitProcess).
- Win32 real: 34 APIs implementadas (kernel32 28 + gdi32 6: CreateCompatibleDC,
  CreateSolidBrush, SelectObject, DeleteObject, PatBlt, GetDeviceCaps);
  não implementadas → EXECUTION STOPPED (Reason/Module/Function) + log.
- Falhas diagnosticadas: API ausente, DLL ausente, memória inválida, instrução
  fora do subconjunto, executável inválido, PE64 (recusa honesta).
- PEs de teste do projeto (`pr_winhello`): (0) GetTickCount64 + ExitProcess(42);
  (1) GDI PatBlt desenha retângulo real → superfície → GfxFrame.surface.
- PLAY: LibraryView → GameProfile → RuntimeManager → WindowsPEBackend →
  pr_peproc → frames → ExitProcess → onFinished (testado).
- JIT: detectado de verdade (indisponível no iOS — W^X/codesigning); execução
  usa interpretador IA-32 e a UI mostra JIT NOT AVAILABLE.

Testes: **475 checks C (11 suítes) + 42 testes Swift (5 suítes), 0 falhas** —
os 366 checks + 33 testes antigos preservados (3 asserções atualizadas do
contrato "Win32 não integrada" da Fase 5 para o contrato real da Fase 7:
x64/API ausente/executável inválido continuam recusados com motivo honesto).

# Status das fases do projeto

Critério: "pronto" só quando compilado e coberto por testes automatizados.

| Fase | Escopo | Status | Evidência |
|---|---|---|---|
| 1 | Projeto iOS, interface, biblioteca, GameProfile, armazenamento | **CONCLUÍDA** | `LibraryStore`, `GameProfile` (Codable), UI Biblioteca/Detalhe/Editor; `StoreTests`, `ModelTests` |
| 2 | RuntimeManager, lifecycle de sessão, logs, recursos | **CONCLUÍDA** | Máquina de estados testada, `LogCenter` + ring C, `ProcessManager` |
| 3 | Metal, renderização, resolução, FPS | **CONCLUÍDA** | `MetalGameRenderer` (2 passes + blit), `FramePacer`/`ResolutionScaler` testados |
| 4 | Áudio, input, overlay | **CONCLUÍDA** | `AVAudioEngineBackend` + `AudioSessionController`, `InputRouter`, overlay completo |
| 5 | EnvironmentManager, CompatibilityLayer | **CONCLUÍDA** | Prefixos reais em disco, vereditos honestos + `OpenGLTranslationMap` |
| 6 | Integração com open source licenciado | **INICIADA** | Avaliação/licenças documentadas; integração binária pendente |
| 7 | Execução real de software compatível | **PARCIAL** | PXP real (IA-32 + pipeline completo, E2E). Execução Windows PE: **NOT SUPPORTED** |
| 8 | Otimização, testes, estabilidade | **EM ANDAMENTO** | 366 checks C + 33 testes Swift verdes |
| W1 | **PE Loader** (análise + carga em memória) | **CONCLUÍDA** | `pr_pe_load/imports/exports/diagnose` + `PELoader`/`PELoadedImage`; `test_pe_loader`, `testPELoaderInspectAndMap` |
| W2 | **Game Import / Biblioteca** | **CONCLUÍDA** | Import por document picker, escolha de executável principal, perfil salvo; `testGameImportChooseMainExecutable` |
| W3 | **ExecutionBackend (ciclo de vida)** | **CONCLUÍDA** | `load/initialize/run/pause/resume/stop/shutdown` reais em `PXPInterpreterBackend`; `WindowsPEBackend.load` real (carga PE) com recusa honesta em `run`; `testExecutionBackendLifecyclePXP`, `testExecutionBackendLoadPEAndHonestRefusal` |
| W4 | **Win32 API layer** | **PARCIAL** | `pr_win32`: dispatch por subsistema (kernel32/user32/advapi32/ws2_32/gdi32/ole32/shell32); 28 APIs kernel32 com comportamento real (heap, console, relógio, handles, módulos, ExitProcess); demais catalogadas → UNSUPPORTED com log. `test_win32`, `testWin32DispatchReal` |
| W5 | **Graphics translation (superfície)** | **PARCIAL** | `pr_surf`: janela/superfície virtual, buffers de pixels (textura do frame), clear/fill, apresentação no stream comum, fence de sincronização → Metal (`GfxSurfaceBuffer`). GDI/OpenGL/D3D completos: pendentes. `test_surf`, `testGraphicsSurfacePath` |
| W6 | **JIT / execução** | **CONCLUÍDA (honestamente)** | `pr_cap_probe` sonda JIT/W^x em runtime; sem JIT → interpretador; diagnóstico informa; ABI `pr_host_backend_v1` aceita backend alternativo |
| W7 | **Diagnóstico por subsistema** | **CONCLUÍDA** | Tela "Diagnóstico" com 9 itens (CPU/Graphics/Áudio/JIT/PE loader/Win32/Metal/Input/Storage), SUPPORTED/PARTIAL/NÃO SUP. + explicação técnica |
| W8 | **Self-test ampliado** | **CONCLUÍDA** | Testes antigos intactos + `test_pe_loader`, `test_win32`, `test_surf` (C) e `CompatLayerTests` (Swift): PE loader, GameProfile, import, ExecutionBackend, Win32 dispatch, gráficos, áudio, input, shutdown |

## Próximos passos / obstáculos técnicos

1. **Backend de CPU x86/x64 para PEs** — é o que falta para EXECUTAR código
   Windows: estender o interpretador IA-32 (FPU, mais opcodes), aplicar
   relocations e ligar o IAT aos stubs de dispatch `pr_win32_call`. Sem JIT no
   iOS; desempenho de interpretação pura é a restrição real da plataforma.
2. Frontends gráficos executáveis (GDI→`pr_surf`; OpenGL via
   `OpenGLTranslationMap`; D3D) e fila de mensagens `user32`.
3. Mais APIs Win32 com comportamento real (arquivos no sandbox, threads
   in-process, registro no prefixo).
4. Validar em iPhone físico (Metal/GameController/áudio exigem hardware Apple).
