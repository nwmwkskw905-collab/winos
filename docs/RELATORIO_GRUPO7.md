# RELATÓRIO — GRUPO 7: ENTRADA E INTERAÇÃO DO WINOS

Base: GRUPO 5+6 (CPU/PE/GDI/janelas/mensagens certificados). Tudo preservado:
nenhum teste removido, nenhuma funcionalidade quebrada, builders v0–v13 intactos.

**META ATINGIDA: 0 falhas, 0 warnings, 0 findings.**

| Verificação | Resultado |
|---|---|
| C tests (`make c-test`) | **1649 verificações, 0 falhas** (GRUPO 6: 1524 → +125 novas) |
| Swift tests (`swift test`) | **61 tests, 0 failures** (GRUPO 6: 57 → +4 novos) |
| `gcc -Wall -Wextra` (src+tests, arquivo a arquivo) | **0 warnings** |
| `gcc -fanalyzer` (src, arquivo a arquivo) | **0 findings** |
| Builders v0–v13 (exercitados pela suíte) | **✓** |
| hello_real.exe | **exit 42** |
| hello_user.exe + hello_dll.dll | **exit 42** |
| hello_app.exe | **exit 42** |
| hello_gdi.exe | **exit 42** |
| **hello_input.exe (NOVO)** | **exit 42** (roteiro completo de entrada) |

---

## 1. APIs Win32 adicionadas (user32: 20 → 32 implementadas)

Todas com **comportamento real**; catálogo com notas honestas; aliases W
(mesmo caminho de código — sem strings no caminho, conforme o subset A):

| API | Comportamento real |
|---|---|
| `PostMessageA/W` | fila real; `hwnd=0` = mensagem de thread; hwnd inválido → erro **1400** (INVALID_WINDOW_HANDLE) |
| `GetMessageW/PeekMessageW/DispatchMessageW/DefWindowProcW` | aliases W das implementações A |
| `SetFocus/GetFocus` | foco rastreado; `SetFocus` entrega entrada retida |
| `SetTimer` | timer real (id gerado se 0; elapse mínimo 1 ms; sem janela exige TIMERPROC → erro 87) |
| `KillTimer` | remoção real; timer inexistente → erro 87 |
| `GetKeyState` | SHORT: bit15=pressionada, bit0=toggle (CAPS/NUM/SCROLL) |
| `GetAsyncKeyState` | bit15=pressionada (thread única documentada) |

**API pública da camada de entrada host** (`pr_win32.h`, SEPARADA da
representação Win32): `pr_win32_input_key/char/mouse/touch/controller`,
`pr_win32_input_pad`, `pr_win32_advance_time`, `pr_win32_focus_hwnd` +
enums `PR_MOUSE_*`, `PR_TOUCH_*`, `PR_CTRL_*`, `W32_VK_*` (subseto de virtual
keys). Estado de controle consulta `pr_input_state` (PR_BTN/PR_AXIS/triggers
do `pr_input.h`, estendido).

`TranslateAccelerator` **NÃO implementado** (não necessário pelos testes
reais — decisão registrada).

## 2. Eventos de janela (FASE 1)

Ciclo completo e real: `PeekMessageA/W` (PM_REMOVE/PM_NOREMOVE) →
`GetMessageA/W` → `TranslateMessage` → `DispatchMessageA/W` (WndProc real do
convidado) → `PostQuitMessage` → `WM_QUIT` (`GetMessage` retorna 0 com
`wParam` = código de saída). `PostMessage` injeta na fila. Constantes:
`WM_KEYDOWN 0x100/KEYUP 0x101/CHAR 0x102/TIMER 0x113`, `WM_MOUSEMOVE 0x200`,
`LBUTTONDOWN/UP 0x201/0x202`, `RBUTTONDOWN/UP 0x204/0x205`,
`MBUTTONDOWN/UP 0x207/0x208`, `MOUSEWHEEL 0x20A`, `MK_*`.

**Bloqueante não representável → EXECUTION STOPPED honesto** (mandato):
`GetMessageA` com fila vazia **espera limitada** pelo próximo timer
(nanosleep ≤ 100 ms/iteração, até 4 tentativas, sem busy-wait) e reavalia;
sem timers armados → **STOP** com diagnóstico (nunca trava o executor).

## 3. Teclado (FASE 2)

`pr_win32_input_key(vk, down)` → `WM_KEYDOWN/WM_KEYUP` reais
(lParam: repeat=1; previous-state em KEYUP). Estado por tecla
(`key_down[256]`, `key_toggle[256]` — CAPS/NUM/SCROLL alternam).
`TranslateMessage` **real**: `WM_KEYDOWN` imprimível → `WM_CHAR`
(conversão segura: space/enter/tab/back/esc, `'0'-'9'`, `'A'-'Z'`→minúsculas
sem shift). `pr_win32_input_char` para IME/teclas sem VK (WM_CHAR direto).
Conversão iOS→virtual keys = teclado do emulador mapeado para `W32_VK_*`
(subconjunto documentado no header). Testes **determinísticos sem hardware**.

## 4. Mouse (FASE 3)

`pr_win32_input_mouse(kind, x, y, wheel)` — **camada de entrada host
SEPARADA** → `WM_MOUSEMOVE/LBUTTONDOWN/LBUTTONUP/RBUTTONDOWN/RBUTTONUP/
MBUTTONDOWN/MBUTTONUP/MOUSEWHEEL`. `lParam = (y<<16)|x` (coords cliente);
`wParam = MK_*` (L=1, R=2, SHIFT=4, CTRL=8, M=0x10 — botões/shift/ctrl
rastreados); wheel `wParam = (delta<<16)|MK_*`. Estado (`mouse_buttons/x/y`)
na estrutura do WinOS.

## 5. Touch/iOS (FASE 4)

`pr_win32_input_touch(id, phase, x, y)` com fases BEGAN/MOVED/ENDED/CANCELLED
(`PR_TOUCH_*`). Pipeline iOS→WinOS→Win32 documentado no adaptador Swift
`TouchInputAdapter` (`UITouch` → `TouchEvent` → evento interno → mensagens).
**Promoção do toque primário a mouse** (BEGAN→MOVE+LDOWN, MOVED→MOVE,
ENDED→LUP); sem toque ativo o novo toque é promovido (semântica UIKit-like).
**Multi-touch** em tabela de 10 toques: toques secundários rastreados sem
gerar mensagens de mouse. `WM_TOUCH` NÃO é gerado (limitação documentada).
Testes não inventam eventos: só o que o host entrega é injetado.

## 6. GameController (FASE 5)

**Infraestrutura** pronta (`pr_win32_input_controller(BUTTON/AXIS/TRIGGER)` →
`pr_input_state` consultável; `GameControllerAdapter.swift` com
`#if canImport(GameController)` + snapshot genérico de
`GCExtendedGamepad` → `GamePadSample`). Botões (máscara `PR_BTN_*`),
analógicos (clamp [-1,1]) e gatilhos (clamp [0,1]). **Sem mensagens Win32**
(entrada de console XInput = futuro — documentado). **Sem mapeamentos
específicos de jogos** (mandato). Recusas honestas (botão não-potência-de-2 →
PR_ERR_INVALID) testadas.

## 7. Timers (FASE 6)

`SetTimer/KillTimer` + `WM_TIMER` real (id em wParam; coalesce: no máximo 1
`WM_TIMER` pendente por timer). `TIMERPROC` do convidado via reentrada
(`guest_call`) — testado. `pr_win32_advance_time(ctx, ms)` = relógio
determinístico (`time_offset_ms`) p/ testes (sem timers falsos). Espera do
`GetMessage` usa **nanosleep limitado** (evita busy-wait; nunca bloqueia o
executor indefinidamente).

## 8. Entrada sem foco (parking determinístico)

Eventos injetados antes de existir janela ficam retidos (`inp_pend[32]`) e
são entregues à primeira janela com foco (CreateWindowExA/SetFocus) —
determinístico para testes (injetar tudo antes do run). Documentado.

## 9. PE real (FASE 7) — `realpe/hello_input.c` (x86_64-w64-mingw32-gcc -O2)

Janela 320×240 + **WndProc real do binário** processando `WM_KEYDOWN 'A'`,
`WM_CHAR 'a'` (via TranslateMessage), `WM_LBUTTONDOWN`, `WM_MOUSEWHEEL` e
`WM_TIMER` (SetTimer 10 ms). Cada evento desenha marca via GDI (SetPixel) e
atualiza o frame; com as 5 marcas → `PostQuitMessage(42)`; verificação final
real dos pixels via `GetPixel` (7 asserts dentro do próprio PE) → **exit 42**.
**Fonte de eventos determinística** no driver: injeta tecla/clique/wheel ANTES
do run (parking) + `advance_time` — 100 % reproduzível em CI, sem hardware.

## 10. Pipeline Metal (FASE 8)

Cadeia completa: **iOS input → InputManager (`TouchInputAdapter`/
`GameControllerAdapter`) → mensagem Win32 → WndProc guest (hello_input.exe)
→ desenho GDI → superfície XRGB8888 → BGRA8 (`SurfaceBridge`) →
`MetalGameRenderer` (`.bgra8Unorm`/`replaceRegion`/blit/letterbox) → frame
apresentado**. Etapas validadas por: `test_input.c` (C, pixels do PE) +
`InputPipelineTests.swift` (touch→`WM_MOUSEMOVE/WM_LBUTTONDOWN` observados,
teclado→`WM_CHAR`, controle→pad, timers) + `GraphicsBridgeTests` (7 testes
BGRA8→Metal preservados).

## 11. Testes adicionados

**C — `Tests/PorticoRuntimeTests/test_input.c`** (+125 verificações):
`test_input_mensagens` (fila, teclado→CHAR, GetKeyState, mouse+MK+wheel,
touch promoção/multi-touch, controle+recusas, foco, PostMessage+erro 1400,
alias GetMessageW), `test_input_timers` (SetTimer/WM_TIMER, coalesce,
KillTimer, TIMERPROC via reentrada, erro 87, GetMessageA vazio → STOP),
`test_input_hello_pe` (hello_input.exe com WndProc REAL + roteiro
determinístico + asserts de pixels).

**Swift — `Tests/PorticoCoreTests/InputPipelineTests.swift`** (+4 testes):
touch iOS→mensagens Win32, teclado→WM_CHAR, GameController→pad,
timers determinísticos. Âncoras atualizadas (G6→G7, padrão honesto):
`user32 == 32 implementadas`, `PeekMessageA` REAL, `GetSystemMetrics`
continua sendo a âncora **não implementada**.

## 12. Falhas, limitações e honestidades

- **Sem `TranslateAccelerator`** — não exigido pelos testes reais.
- **Sem `WM_TOUCH`/`WM_POINTER`** — multi-touch rastreado internamente
  (toques secundários sem mensagens Win32; promoção de primário documentada).
- **`GetKeyState`/`GetAsyncKeyState`** — thread única (documentado); sem
  fila de teclado baixo nível.
- **TIMERPROC** usa a mesma reentrada de WndProc; no máximo 1 `WM_TIMER`
  pendente por timer (coalesce real do Windows).
- **`GetMessageA` bloqueante infinito** = não representável → espera
  limitada por timers e **EXECUTION STOPPED** honesto (mandato FASE 1).
- **GameController sem XInput** (`XInputGetState` etc. = futuro); estado
  consultável via `pr_input_state`; sem mapeamentos de jogos (mandato).
- **Sem idle/`MsgWaitForMultipleObjects`**; fila é thread única.
- Sempre-pressed via `GetAsyncKeyState` não rastreado.

## 13. Próximo bloqueador

Suporte a **Direct3D/OpenGL mínimo** para PEs que exigem contexto 3D —
atualmente qualquer chamada d3d9/opengl32 → EXECUTION STOPPED honesto.

## 14. Próximo passo recomendado (próximo grupo)

**Camada gráfica 3D mínima exigida por um PE/jogo real** (menor subconjunto
de APIs comprovado pelo teste real): provavelmente um subconjunto de
**Direct3D 9** (CreateDevice/BeginScene/EndScene/Present sobre a superfície
interna + GDI→BGRA8→Metal já validado) ou GL 1.1 — escolher pelo PE de
controle real; manter o mandato: só APIs realmente necessárias, tudo o mais
→ EXECUTION STOPPED honesto.
