# RELATÓRIO — GRUPO 6: Camada gráfica mínima do WinOS

**Objetivo concreto ATINGIDO:** um PE Windows real (`hello_gdi.exe`, MinGW-w64) desenha um frame pelo caminho completo — **PE → Win32/GDI → superfície BGRA8 → framebuffer → estágio de submissão isolado → Metal → frame apresentável no iOS** — com WndProc real do convidado, verificação de pixels por `GetPixel` e ciclo de mensagens Win32 real.

---

## 1. Funcionalidades implementadas

### FASE 1 — Superfície BGRA8 consolidada
- **Formato (contrato C↔Swift↔Metal, documentado em `SurfaceBridge.swift`)**: pixels `XRGB8888` = `UInt32` LE `0x00RRGGBB`; em memória LE os bytes são B,G,R,0 — layout idêntico ao BGRA8 do Metal exceto alfa (0 → forçado 255 no upload); **stride = `width*4`** contíguo sem padding; origem = `pr_surf` (superfície GDI virtual do processo).
- Acesso seguro: `pr_win32_ptr` com limites (imagem/scratch/heap do convidado) — ponteiros fora do espaço são rejeitados honestamente.
- **BitBlt** (já existente, com clipping/limites testados), **StretchBlt** (SRCCOPY vizinho-mais-próximo), **SetPixel/GetPixel** (roundtrip COLORREF↔XRGB, `CLR_INVALID=0xFFFFFFFF` fora dos limites), **FillRect** (NOVO — user32, com clipping e RECT invertido = no-op).
- Testes de clipping, stride (`pitch == w*4` em bitmaps 8×4 e janelas 32×24/320×240) e limites (negativos, ≥w, ≥h, handles inválidos, memória inválida).

### FASE 2 — Janelas Win32 reais (→ superfície interna; nunca janela nativa)
Novas APIs implementadas com comportamento real (18 user32):
`RegisterClassA · CreateWindowExA · ShowWindow · GetClientRect · GetDC · ReleaseDC · InvalidateRect · UpdateWindow · BeginPaint · EndPaint · DestroyWindow · PostQuitMessage · GetMessageA · PeekMessageA · TranslateMessage · DispatchMessageA · DefWindowProcA · FillRect`
- Modelo: a **1ª janela cria a superfície do processo** (contrato Metal preservado — `pr_peproc_surface`); `WM_CREATE/WM_SIZE/WM_SHOWWINDOW/WM_ERASEBKGND/WM_PAINT/WM_DESTROY` reais; `WM_CLOSE` → `DestroyWindow` (como o DefWindowProc real); fila de mensagens real com `WM_QUIT` posto por `PostQuitMessage` e consumido por `GetMessageA` (return 0 + `wParam` = código de saída).
- **WndProc REAL do convidado** via `guest_call` (reentrada já usada pelo atexit) — provado com `WndProc` do `hello_gdi.exe` endereçada em código guest (`wndproc=0x0000000000FE1450` no log).
- Limites honestos documentados (IMPL_NOTE no catálogo): `GetMessageA` com fila vazia e sem `WM_QUIT` = **EXECUTION STOPPED** (pump bloqueante sem entrada); `WM_QUIT` mantido após `PostQuitMessage`; `InvalidateRect` parcial = janela inteira; `TranslateMessage` sem tradução de teclado; sem área não-cliente (cliente = dimensões pedidas); superfície única por processo (2ª janela com dimensões diferentes = erro 87 honesto); `DispatchMessageA` sem `guest_call` = STOP honesto.

### FASE 3 — BGRA8 → Metal (backend existente)
- `MetalFrameUpload` (estágio BGRA8 reutilizado) + `MetalGameRenderer` (`PorticoApp/Metal`): `surfaceTexture` `.shared` no tamanho da superfície, upload eficiente via `replaceRegion` (`bytesPerRow = w*4`, `.bgra8Unorm`), blit interno→drawable e `cmdBuffer.present(drawable)` + `[METAL] frame presented`. Sem Windows real, VM ou streaming — renderização local no iOS (código Metal compila sob `canImport(Metal)`; aqui validado até a fronteira da API com os bytes exatos que o `replaceRegion` recebe).
- Contrato de upload coberto por teste byte a byte (alfa=255, stride, resize).

### FASE 4 — Testes automatizados (novos)
| Arquivo | Cobertura |
|---|---|
| `Tests/PorticoRuntimeTests/test_gdiwin.c` (NOVO, 154 checks) | GDI→BGRA8 (formato/stride), clipping (FillRect/StretchBlt), limites SetPixel/GetPixel, memória inválida (RECT/ponteiro/handle), StretchBlt 2× + recusas honestas, janelas (RegisterClass→CreateWindow→Show→BeginPaint/EndPaint→Destroy), fila (GetMessage/PeekMessage/DispatchMessage/WM_QUIT), múltiplos frames (5× Invalidate+Update), **PE gráfico real hello_gdi.exe** (exit 42 + 5 pixels exatos na superfície + trilha de log + tempo < 2 s) |
| `Tests/PorticoCoreTests/GraphicsBridgeTests.swift` (NOVO, 7 testes) | codec XRGB→BGRA8 byte-exato, GDI→BGRA8, **BGRA8→Metal (contrato replaceRegion)**, 128 frames sem realocação (upload eficiente), **resize** (320×240↔640×480), **memória inválida** (recusas honestas), gancho de frame estagiado |

Âncoras de honestidade obsoletas atualizadas (feature mandada virou real — padrão estabelecido no Grupo 1, nunca mascarando): `test_win32.c` (PeekMessageA→GetSystemMetrics mantém classe "não implementada"; contagem user32 2→20; lookup IMPLEMENTED/UNSUPPORTED) e `CompatLayerTests.swift` (mesmas 2 âncoras). Nenhum teste removido.

## 2. PE gráfico utilizado
**`realpe/hello_gdi.c` → `hello_gdi.exe`** (MinGW-w64 `-O2 -static -lgdi32 -luser32`, em `Tests/PorticoRuntimeTests/data/`): programa Windows canônico — `WNDCLASSA` + `CreateWindowExA(320×240)` + `ShowWindow` + `UpdateWindow`; desenho em `WM_PAINT` (`BeginPaint` → fundo `RGB(20,24,64)`, retângulos `RGB(200,40,40)` e `RGB(40,180,80)`, 3 `SetPixel`, `EndPaint`); verificação real com `GetPixel` ×5; `DestroyWindow` → `WM_DESTROY` → `PostQuitMessage(42)`; loop `GetMessageA/TranslateMessage/DispatchMessageA` → `WM_QUIT` → **exit 42**.

## 3. Resultado da apresentação BGRA8/Metal
- Frame desenhado pelo PE real chega **íntegro** ao estágio de upload: os 5 pixels verificados via `GetPixel` no guest batem com a superfície XRGB8888 (checados também no host em `test_gdiwin`), e o `MetalFrameUpload` converte byte a byte para BGRA8 conforme o contrato do `replaceRegion` (testado).
- Saída do pipeline no run real: `[PROCESS] exit code 42 after 0.68 ms` / `0.79 ms` (2 medições) — **tempo do frame completo do guest** (CRT→janela→GDI→desenho→verificação→ciclo de mensagens→exit). Pipeline CPU ≈ **0,7 ms/frame** (>1.000 fps teóricos até o estágio de submissão; a taxa real será a do display no device). Estágio de upload: 128 frames de 320×240 em sequência sem realocação (teste Swift `testMultipleFramesStageWithoutReallocation`).
- Trilha real registrada: `class registered "PorticoGdi" wndproc=0xFE1450` → `window created 320x240` → `[GDI] surface updated` → API calls (FillRect/SetPixel/GetPixel/…) → `PostQuitMessage code=42` → `window destroyed` → `exit(42)`.

## 4. Regressão (FASE 5 — obrigatória)
| Item | Resultado |
|---|---|
| `make c-test` | **1524 verificações, 0 falhas** (era 1370) |
| `swift test` | **57 testes, 0 falhas** (era 53) |
| `gcc -Wall -Wextra` (src+tests) | **0 warnings** |
| `gcc -fanalyzer` manual (src, arquivo a arquivo) | **0 findings** |
| builders v0–v13 | preservados e exercitados na suíte |
| `hello_real.exe` | exit 42 ✓ |
| `hello_user.exe` + `hello_dll.dll` | exit 42 ✓ |
| `hello_app.exe` + `hello_dll.dll` | exit 42 + stdout `x=24.75 r=4.975 fib=55 heap=11 dll=42 / cpu=PORTICO_VRTX tsc>0=1` ✓ |
| `hello_gdi.exe` (NOVO) | exit 42 + frame verificado ✓ |

## 5. Bloqueadores encontrados
1. **Nenhum bloqueador novo na execução do PE gráfico real** — `hello_gdi.exe` completou com exit 42 no caminho completo.
2. Limites honestos implementados como EXECUTION STOPPED/comportamento documentado (seção 1) — em especial **pump de mensagens bloqueante sem entrada** (sem teclado/mouse neste build) e **callback WndProc sem reentrada** (par caminho peproc nunca ocorre).
3. Stubs diagnósticos existentes não-chamados (chamada = STOP honesto): `__C_specific_handler`, `signal`, etc.
4. **Fronteira conhecida**: o código Metal real (`MetalGameRenderer`) só compila/executa em iOS/macOS (`canImport(Metal)`); no host validamos até a API (`replaceRegion` bytes exatos). A apresentação no drawable é etapa de device (Xcode).

## 6. Próximo passo do Grupo 6 (recomendado)
1. **Validação on-device (iPhone físico)**: `MTKGameView` → `MetalGameRenderer.draw(in:)` com o `hello_gdi.exe` → confirmar `[METAL] frame presented` e o frame visível (o estágio já está isolado e testado).
2. Entrada de convidado (SetTimer/teclado/mouse → fila Win32) para pumps de mensagem reais de jogos (`PeekMessageA` já cobre o lado do jogo).
3. Só depois disso: DirectX mínimo (apenas o que um PE real de jogo exigir) — sem camada falsa.
