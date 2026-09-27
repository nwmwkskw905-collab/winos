# Relatório — Expansão para Execução de PEs x64 Reais (FASES 1–7)

Data: 2026-09-22/23 · Projeto Portico · todos os números abaixo são de execução
real (`make test`), não de estimativa.

**Critério de conclusão atendido:** caminho verificável ponta a ponta
**PE x64 → loader → relocations/imports → CPU x64 → memória/stack/heap → Win32 →
GDI → bitmap/surface → BitBlt/StretchBlt → GfxFrame → Metal (ponte) → frame →
encerramento correto**, comprovado por 3 PEs de teste executados nos testes C e
Swift. **NÃO há declaração de compatibilidade com jogos comerciais** (GTA V,
MX Bikes ou outros) — eles não foram e não devem ser executados nesta etapa.

---

## 1) Arquivos criados/modificados

**Criados**
| Arquivo | Conteúdo |
|---|---|
| `Tests/PorticoRuntimeTests/test_win32x.c` | Testes unitários das 13 APIs Win32 novas + env/cwd/cmdline |
| `Tests/PorticoRuntimeTests/test_pes.c` | E2E dos PEs de teste 7/8/9 + logs [MEM]/[GDI]/[WIN32]/[PROCESS] |
| `docs/RELATORIO_EXPANSAO_X64.md` | este relatório |

**Modificados**
| Arquivo | Mudança |
|---|---|
| `Sources/PorticoRuntime/include/portico/pr_cpu64.h` | `pr_cpu64_fault` ganhou `bytes[8]`/`nbytes`; docs do subset |
| `Sources/PorticoRuntime/src/pr_cpu64.c` | decoder x64 expandido (FASE 1 completo — ver item 2) |
| `Sources/PorticoRuntime/src/pr_peproc.c` | logs `[MEM]` (stack/stubs/image/fault com R/W/X+addr+len+RIP), `Technical: … bytes=XX XX…`, timing `[PROCESS] exit code N after X ms` |
| `Sources/PorticoRuntime/src/pr_win32.c` | 13 APIs novas (item 3), `pr_win32_env_set`/`set_cwd`/`set_cmdline`, cmdline UTF-16 no guest |
| `Sources/PorticoRuntime/include/portico/pr_win32.h` | declarações públicas dos setters de ambiente/cwd/cmdline |
| `Sources/PorticoRuntime/src/pr_winhello.c` | PEs de teste variantes **7 (memória) / 8 (GDI+StretchBlt+BitBlt+SetPixel) / 9 (Kernel32+GDI+MessageBox)** + montador de imports reutilizável |
| `Tests/PorticoRuntimeTests/test_cpu64.c` | +74 checks da FASE 1 (flags, Jcc, LEA, shifts, extensão, 8/16-bit, SIB, bytes de fault) |
| `Tests/PorticoRuntimeTests/test_win32.c` | asserts de limites superados atualizados (MessageBoxA/StretchBlt agora implementados) |
| `Tests/PorticoRuntimeTests/test_peproc.c` | guarda NULL no acesso ao surface (segurança) |
| `Tests/PorticoRuntimeTests/test_main.c` | registro de `test_win32x` e `test_pes` |
| `Sources/PorticoCore/Graphics/SurfaceBridge.swift` | `MetalFrameUpload.onStaged` (gancho testável "[METAL] frame submitted") |
| `Sources/PorticoCore/Runtime/ExecutionBackend.swift` | `WindowsPEBackend.initialize` injeta **environment/cwd/cmdline** reais do contexto de sessão |
| `Tests/PorticoCoreTests/WindowsExecutionTests.swift` | 3 testes e2e (PE7 memória, PE8 frame, PE9 APIs+ambiente) |
| `Tests/PorticoCoreTests/CompatLayerTests.swift` | asserts de MessageBoxA/catálogo atualizados para o estado novo |
| `docs/STATUS.md`, `docs/LIMITATIONS.md` | estado atual documentado |

Nenhum teste removido; nenhum código substituído por mock.

## 2) Novas instruções x64 (pr_cpu64)

Em relação ao subset straight-line anterior (push/pop/mov/xor/c3/ff/cd):

- **ALU família 0x00–0x3D completa**: ADD/OR/ADC/SBB/AND/SUB/XOR/CMP em
  r/m8, r/m16, r/m32, r/m64 (4 formas: r/m↔r e acumulador↔imediato)
- **Grupo 1 (80/81/83)** com todos os 8 subcódigos (ADC/SBB incluídos)
- **CMP/TEST**: 3D, A8/A9, 84/85, F6/F7 /0
- **INC/DEC** (FE/FF — CF preservado); **NOT/NEG** (F6/F7 /2 /3)
- **Shifts/rotates** (C0/C1/D0–D3): ROL/ROR/RCL/RCR/SHL/SHR/SAR por 1, imm8 ou CL
- **LEA** (8D) com [base+index*scale+disp32] e RIP-relative
- **MOV adicionais**: r/m8 (88/8A/C6), r16 (66), imm16/32/64 (B0–BF/B8–BF)
- **XCHG** (86/87/90+r); **PUSH/POP imm** (68/6A/8F /0)
- **CALL rel32** (E8), **RET/RET imm16** (C3/C2), **JMP rel8/rel32** (EB/E9)
- **Jcc**: todos os 16 códigos de condição, rel8 (70–7F) e rel32 (0F 80–8F)
- **SETcc** (0F 90–9F); **MOVZX/MOVSX** 8/16→32/64 (0F B6/B7/BE/BF); **MOVSXD** (63)
- **IMUL** 2/3 operandos (0F AF, 69/6B); **MUL/IMUL/DIV/IDIV** unários (F6/F7 /4–/7)
  com fault honesto em divisão por zero/estouro
- **CBW/CWDE/CDQE** (98), **CWD/CDQ/CQO** (99), **LEAVE** (C9), **ENTER n,0** (C8)
- **endbr64** (F3 0F 1E FA = NOP) e NOP multi-byte (0F 1F) — gerados por
  compiladores reais; **flag-ops** CLC/STC/CMC/CLD/STD
- **Operandos 8/16/32/64-bit** com prefixo 66 e REX completo (incl. bytes
  altos AH..BH sem REX e SPL..DIL com REX); SIB base+index*scale+disp;
  RIP-rel **relativo ao fim da instrução** (inclusive com imediato após ModRM)
- **RFLAGS reais**: CF/PF/ZF/SF/OF (+DF) calculados por aritmética/lógica/
  shifts/inc/dec/mul/div
- **Fault**: `pr_cpu64_fault` agora inclui **bytes da instrução** (`bytes[8]`,
  `nbytes`) além de opcode, RIP, endereço e motivo — impresso no diagnóstico
  como `Technical: rip=… opcode=0x.. addr=0x.. bytes=48 8B 03 …`

**Fora do subset (recusa honesta, documentado)**: FPU/x87, SSE/AVX, string ops
(movs/stos/rep — memcpy do CRT), FS/GS (TEB/PEB), addr32 (67h), far call/jmp/ret,
DAA/BCD, pushf/popf, loops/jecxz, CLI/STI.

## 3) Novas APIs Win32 implementadas (13)

| API | Comportamento real |
|---|---|
| kernel32!GetModuleHandleW | UTF-16→lookup de módulo (mesmo que A) |
| kernel32!GetCommandLineW | linha UTF-16 do processo (guest em w32static) |
| kernel32!GetEnvironmentVariableA/W | ambiente do processo; semântica Win32 (tamanho necessário + ERROR_INSUFFICIENT_BUFFER/ENVVAR_NOT_FOUND) |
| kernel32!GetSystemInfo | SYSTEM_INFO x64 honesto (AMD64, page 4096, gran 65536, limites = espaço do Portico) |
| kernel32!VirtualProtect | prot real via pr_vm_regions/pr_vm_protect; devolve prot anterior |
| kernel32!GetCurrentDirectoryA | cwd real do processo |
| gdi32!DeleteDC | destruição real do DC |
| gdi32!SetPixel / GetPixel | pixels reais; COLORREF 0x00BBGGRR↔XRGB 0x00RRGGBB documentado; CLR_INVALID fora dos limites |
| gdi32!StretchBlt | SRCCOPY com vizinho mais próximo (11 args) + log [GDI] |
| user32!MessageBoxA / MessageBoxW | **sem UI neste build** (sem janelas): registra caption/texto em log WARN e retorna IDOK — não finge janela (nota IMPL_NOTE no catálogo) |

Total de APIs implementadas no catálogo: **53** (eram 40). Novas funções de
integração C: `pr_win32_env_set`, `pr_win32_set_cwd`, `pr_win32_set_cmdline`
(consumidas por `WindowsPEBackend.initialize` a partir do contexto de sessão).

## 4) APIs ainda NÃO implementadas (NOT_IMPLEMENTED honesto)

- **Texto GDI**: TextOutA/W, CreateFontA — exigem fonte rasterizada completa;
  só implementar quando puder ser feito corretamente (decisão consciente).
- **kernel32**: LoadLibraryA/W, FreeLibrary, GetModuleFileNameA, CreateFileA,
  ReadFileEx, SetFilePointer, CloseHandle, WaitForSingleObject, CreateThread,
  TlsAlloc, MultiByteToWideChar, WideCharToMultiByte.
- **user32**: CreateWindowExA, ShowWindow, PeekMessageA, DispatchMessageA,
  GetSystemMetrics, SetTimer, DestroyWindow (janela = superfície interna).
- **advapi32**: RegOpenKeyExA/RegQueryValueExA/RegSetValueExA/RegCloseKey.
- **ole32**: CoInitialize(Ex)/CoCreateInstance/CoUninitialize (COM).
- **shell32**: ShellExecuteA, CommandLineToArgvW.
- **ws2_32**: além de htonl/htons/ntohl/ntohs (ordinais winsock.def): WSAStartup,
  socket, connect, send, recv, closesocket, getaddrinfo.

Toda chamada a API ausente registra **NOT_IMPLEMENTED + nome + parâmetros
relevantes** no log e `EXECUTION STOPPED` com diagnóstico — nunca sucesso falso.

## 5) Mudanças GDI/Metal

- GDI: **StretchBlt** SRCCOPY (vizinho mais próximo documentado), **SetPixel/**
  **GetPixel**, **DeleteDC**; superfície XRGB8888 (stride=width*4) com
  `width/height/pixel format/stride/buffer`; BitBlt SRCCOPY preservado;
  logs `[GDI] StretchBlt …`, `[GDI] BitBlt …`, `[GDI] surface updated`.
- Metal: `MetalFrameUpload` (estágio BGRA8 reutilizado) ganhou **`onStaged`** —
  gancho "[METAL] frame submitted" testado sem GPU; o renderer
  `MetalGameRenderer` **não foi alterado** (ponte surface→`replaceRegion`
  `.shared` preservada; textura interna `.private` intocada). Conversão
  XRGB→BGRA8 testada pixel a pixel.

## 6) Total de checks C

**957** (eram 741 — +216 novos em test_cpu64/test_win32x/test_pes; nenhum
removido; contagens `n==` existentes preservadas).

## 7) Total de testes Swift

**50** (eram 47 — +3: `testPE7MemoryAllocatesWritesAndExitsClean`,
`testPE8GDIStretchBitBltProducesFrame`, `testPE9Kernel32GDIAndEnvironment`;
nenhum removido). Suites: WindowsExecution 14, CompatLayer 10, PELoader 7,
PXPSmoke 6, RuntimeAndHonesty 6, RuntimeManager 4, GraphicsBridge 3.

## 8) Falhas

**0** (`make test`: "TODOS OS TESTES PASSARAM" — 957 checks C + 50 Swift).

## 9) Warnings

**0** — todos os 18 arquivos C compilados com `-Wall -Wextra -Wshadow` sem
warnings; **análise estática `gcc -fanalyzer` limpa**; Swift build limpa.

## 10) O que falta para o primeiro programa Windows real mais complexo

1. **C runtime do convidado (CRT)**: mem*/strlen via chamadas do compilador,
   `__chkstk`, MultiByteToWideChar/WideCharToMultiByte — sem isso nem um
   "hello world" compilado com MSVC/MinGW completa o `_start`.
2. **String ops x64** (rep movsb/stosb — memcpy/memset do CRT) + **SSE mínimo**
   (movups/movaps…): compiladores modernos emitem SSE até em código trivial.
3. **E/S de arquivos sandbox-aware**: CreateFileA/ReadFile/WriteFile/
   SetFilePointer/CloseHandle sobre um FS de prefixo (regras do iOS).
4. **LoadLibraryA/W + GetProcAddress dinâmico** (carregar DLLs de sistema do
   convidado apenas como stubs catalogados).
5. **Heap completo**: HeapReAlloc/HeapDestroy/flags (HEAP_ZERO_MEMORY etc.) e
   VirtualQuery.
6. **Mais GDI para ferramentas Win32**: TextOutA com fonte rasterizada completa,
   CreateDIBSection/StretchDIBits (DIB↔bitmap), GetDC/ReleaseDC, stock objects.
7. **Janelas/mensagens**: CreateWindowExA + PeekMessage/DispatchMessage mapeados
   para a superfície interna (janela Windows = superfície; sem janela nativa).
8. **TLS/PEB (FS/GS)** e exceções (SEH) mínimas.
9. **Para jogos**: camada DirectX (d3d9/dxgi/d3d11) — etapa dedicada e separada,
   com alternativa documentada; **não** será anunciada antes de testes reais.
10. Prioridade de hardware inalterada: iPhone físico > iPad físico > simulador;
    apresentação final do Metal exige dispositivo (no Linux segue PARTIAL com
    explicação exata — estágio BGRA8 testado, GPU não comprovável aqui).

---

### Log de verificação (execução real desta etapa)

```
$ make test
957 verificações, 0 falhas          (C — inclui PEs 7/8/9 e2e)
Executed 50 tests, with 0 failures  (Swift — inclui PE7/PE8/PE9)
== TODOS OS TESTES PASSARAM ==
$ gcc -std=c11 -Wall -Wextra -Wshadow -fanalyzer src/*.c   → 0 warnings
$ make gen-xcodeproj                → swift: 44  c: 18  headers: 17
```

Trecho do log estruturado de um PE de teste (FASE 6):

```
[PE] loaded base=0x00400000 entry=0x00402000 arch=x64
[MEM] stack mapped 0x00D00000..0x00DFFFFF prot=rw
[MEM] api stubs 0x00E00000..0x00E00FFF prot=rx count=53
[MEM] image mapped base=0x00400000 size=0x3000 pages=3
[CPU] execution started arch=x64
[WIN32] API kernel32.dll!VirtualAlloc
[GDI] StretchBlt src 160x120 (0,0) dst 320x240 (0,0) rop=SRCCOPY
[GDI] surface updated
[PROCESS] exit code 0 after 0.42 ms
```
