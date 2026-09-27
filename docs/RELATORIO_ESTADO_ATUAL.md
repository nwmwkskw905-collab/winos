# Relatório Final — Correção test_peproc.c e Estado Atual do Projeto

Data: 2026-09-23 · Portico · números de execução real (`make test` + `-fanalyzer`).

## Correção aplicada (apenas o erro atual)

**Erro:** `test_peproc.c:82` — deref de ponteiro sem guarda após `CHECK`
(o macro `CHECK` **não aborta**, então um `NULL` viraria crash em vez de falha).

**Diagnóstico exato:**
- Linha 82 (`pr_surf* surf = pr_peproc_surface(p)`) já estava com
  `CHECK(surf != NULL)` + `if (surf)` da correção anterior.
- A **mesma classe de erro** seguia inalterada logo abaixo:
  1. `px[...]` (3 leituras) após `pr_surf_pixels(surf)` — ponteiro não validado;
  2. `s[20 * 320 + 20]` no bloco `pr_gfx_surface(stream, ...)` — deref direto
     após `CHECK(s != NULL)`.

**Correção (mínima, sem mascarar):** cada ponteiro ganhou `CHECK(ptr != NULL)`
**e** guarda `if (ptr) { ... }`. Se o ponteiro for NULL, o `CHECK` registra a
**FALHA real** (o teste continua reprovando) e a guarda apenas evita o crash —
as verificações de pixels **continuam obrigatórias** quando o ponteiro existe.
Nenhum teste foi removido; a intenção original (pixel dentro = vermelho
0x00FF0000, fora = 0) está intacta.

A análise estática completa revelou a **mesma classe** em mais 2 arquivos de
teste, fechada com o mesmo padrão:
- `test_wincompat.c` ×3: `bad = malloc(len)` → `memcpy(bad, ...)` sem guarda;
- `test_zip.c` ×1: `buf = calloc(...)` → escrita sem guarda (em `build_zip`,
  função `size_t` → `return 0` após o CHECK de OOM).

**Um passo intermediário teve erro de digitação meu** (`cmake:` em vez de
`command:` na chamada da ferramenta) e um `return;` indevido em função
`size_t` (corrigido para `return 0;`) — ambos do lado do processo de correção,
sem efeito no código do produto.

## Nota separada — "Something went wrong" da interface Arena

A mensagem "Something went wrong" veio da **própria ferramenta Arena**
(uma chamada `present_file` falhou com "Error calling present file tool" e
funcionou na chamada seguinte com caminho absoluto). **Não é do projeto.**
Registrado separadamente conforme pedido; os testes do projeto foram executados
com segurança (rodada completa verde após as correções).

---

## Estado atual (verificado nesta rodada)

| Métrica | Valor |
|---|---|
| **Checks C totais** | **962** (957 → +5 pelos novos CHECKs de guarda; nenhum removido) |
| **Testes Swift totais** | **50** (WindowsExecution 14, CompatLayer 10, PELoader 7, PXPSmoke 6, RuntimeAndHonesty 6, RuntimeManager 4, GraphicsBridge 3) |
| **Falhas** | **0** ("TODOS OS TESTES PASSARAM") |
| **Warnings** | **0** — build `-Wall -Wextra -Wshadow` limpa e **`gcc -fanalyzer` completo (src + tests) sem nenhum achado** |

### Instruções x64 já implementadas (pr_cpu64)

ALU completa 8/16/32/64-bit (ADD/OR/ADC/SBB/AND/SUB/XOR/CMP, famílias 0x00–0x3D
e grupos 80/81/83); CMP/TEST (3D, A8/A9, 84/85, F6/F7 /0); INC/DEC (CF
preservado); NOT/NEG; shifts/rotates ROL/ROR/RCL/RCR/SHL/SHR/SAR por 1/imm8/CL
(C0/C1/D0–D3); LEA com [base+index*scale+disp32] e RIP-relative (relativo ao fim
da instrução); MOV r/m8/16/32/64 + imediatos B0–BF; XCHG (86/87/90+r);
PUSH/POP r/imm (50–5F, 68/6A/8F); CALL rel32 (E8) e indireto (FF /2);
RET/RET imm16 (C3/C2); JMP rel8/rel32 (EB/E9) e indireto (FF /4);
**Jcc completos** rel8 (70–7F) e rel32 (0F 80–8F) nos 16 códigos;
**SETcc** (0F 90–9F); MOVZX/MOVSX (0F B6/B7/BE/BF); MOVSXD (63);
IMUL 2/3 operandos (0F AF, 69/6B); MUL/IMUL/DIV/IDIV unários (F6/F7 /4–/7);
CBW/CWDE/CDQE, CWD/CDQ/CQO; LEAVE (C9); ENTER n,0 (C8); endbr64 e NOP
multi-byte; flag-ops CLC/STC/CMC/CLD/STD; prefixo 66 (16-bit); bytes altos
AH..BH e SPL..DIL; INT (trap 0x2E); HLT. **RFLAGS reais** CF/PF/ZF/SF/OF(+DF).
Fault com opcode, RIP, **bytes da instrução**, endereço e motivo.

Fora (recusa honesta com diagnóstico): FPU/x87, SSE/AVX, string ops
(rep movs/stos), FS/GS (TEB), addr32, far call/jmp/ret, pushf/popf, BCD.

### APIs Win32 já implementadas (53)

**kernel32 (35):** GetTickCount64, QueryPerformanceFrequency/Counter,
GetProcessHeap, HeapAlloc/HeapFree/HeapSize, VirtualAlloc/VirtualFree/
**VirtualProtect**, GetStdHandle, WriteFile, ReadFile, SetLastError/GetLastError,
lstrlenA/lstrcpyA/lstrcpynA, ExitProcess, TerminateProcess,
GetCurrentProcessId, GetCurrentThreadId, IsDebuggerPresent,
GetModuleHandleA/**W**, GetProcAddress, GetCommandLineA/**W**,
OutputDebugStringA, GetVersion, GetVersionExA,
**GetEnvironmentVariableA/W**, **GetSystemInfo**, **GetCurrentDirectoryA**.

**gdi32 (10):** CreateCompatibleDC, **DeleteDC**, CreateCompatibleBitmap,
SelectObject, DeleteObject, CreateSolidBrush, PatBlt (PATCOPY), BitBlt
(SRCCOPY), **StretchBlt** (SRCCOPY, vizinho mais próximo), **SetPixel**,
**GetPixel**, GetDeviceCaps.

**user32 (2):** MessageBoxA/**W** — **sem UI neste build**: registra
caption/texto em log WARN e retorna IDOK (não finge janela).

**ws2_32 (4):** htonl@8, htons@9, ntohl@14, ntohs@15 (ordinais winsock.def).

Integração C: `pr_win32_env_set` / `pr_win32_set_cwd` / `pr_win32_set_cmdline`
(alimentadas pelo `RuntimeSessionContext`). NÃO implementadas → NOT_IMPLEMENTED
+ nome + parâmetros + log (ex.: TextOutA/W, CreateFontA, LoadLibraryA/W,
CreateFileA/ReadFile/SetFilePointer/CloseHandle, CreateThread, TlsAlloc,
MultiByteToWideChar/WideCharToMultiByte, CreateWindowExA/PeekMessageA,
Reg*, CoInitialize/CoCreateInstance, ShellExecuteA, winsock de socket).

### Estado do GDI

Superfície virtual XRGB8888 (0x00RRGGBB, stride=width*4, máx. 4096×4096) com
create/pixels/pitch/width/height/clear/fill_rect/present/destroy. Objetos GDI
reais: DC (com brush e bitmap selecionáveis), brush de cor, bitmap com buffer
próprio. BitBlt SRCCOPY com recorte/limites/stride corretos; StretchBlt SRCCOPY
2× testado pixel a pixel (PE 8); PatBlt PATCOPY → alvo real (bitmap selecionada
ou superfície do processo); SetPixel/GetPixel com conversão COLORREF↔XRGB
documentada. Pendência consciente: texto (TextOut/CreateFont) só quando houver
fonte rasterizada completa.

### Estado da ponte GDI → GfxFrame → Metal

GDI surface → `pr_win32_surface`/`pr_peproc_surface` → `GfxFrame.surface`
(comprovado pixel a pixel em C e em Swift) → `MetalFrameUpload` (conversão
XRGB→BGRA8, buffer reutilizado entre frames, gancho `onStaged` "[METAL] frame
submitted" **testado sem GPU**) → `MetalGameRenderer` (iOS): textura
`.shared` + `replaceRegion` (API permitida; renderer não alterado nesta etapa).
Logs `[GDI] …`/`[METAL] …`/[PROCESS] com tempo de execução.
**Apresentação em GPU real = PARTIAL**: exige dispositivo iOS; no Linux o
último estágio testável é o estágio BGRA8 + ponte escrita.

### Próximo bloqueador técnico real (PE Windows mais complexo)

**O CRT (C runtime) do convidado + o mínimo de instruções que ele usa.**
Qualquer .exe real compilado (MSVC/MinGW) começa por `_start`/`__scrt` que
chama `memcpy`/`memset` do CRT — o compilador emite **string ops
(`rep movsb`/`stosb`)** e **SSE (`movups`/`movaps`…)** até em código trivial;
hoje ambas as famílias caem em `EXECUTION STOPPED` honesto. Sem isso, nem o
"hello world" completo do CRT roda. Em ordem, o caminho é:

1. String ops x64 (movs/stos/lods + REP) e SSE mínimo (movups/movaps/pxor…);
2. MultiByteToWideChar/WideCharToMultiByte + `__chkstk` e ajustes do CRT;
3. E/S de arquivos sandbox-aware (CreateFileA/ReadFile/WriteFile/
   SetFilePointer/CloseHandle sobre prefixo do Portico);
4. LoadLibraryA/W + HeapReAlloc/VirtualQuery + TlsAlloc/GetModuleFileNameA;
5. TextOutA com fonte rasterizada + DIBs (CreateDIBSection/StretchDIBits);
6. Janelas/mensagens mapeadas para a superfície interna;
7. DirectX (d3d9/dxgi) — etapa dedicada, só para jogos, sem promessa prévia.

**Nenhuma declaração sobre jogos comerciais** (GTA V, MX Bikes ou outros): não
foram testados e não devem ser usados como critério nesta etapa.

## Verificação executada (log real)

```
$ make test
962 verificações, 0 falhas
Executed 50 tests, with 0 failures (0 unexpected)
== TODOS OS TESTES PASSARAM ==

$ gcc -std=c11 -Wall -Wextra -Wshadow -fanalyzer  (18 srcs + 16 testes)
   → 0 warnings, 0 errors
```
