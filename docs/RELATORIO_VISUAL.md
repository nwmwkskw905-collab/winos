# Portico — Relatório: PRIMEIRO MARCO VISUAL REAL

**Data:** 2026-09-22 · **Ambiente de validação:** Linux (gcc + Swift 6.1.2) · **Alvo:** iOS 17 (iPhone físico > iPad > simulador)

## Objetivo e resultado

Pipeline visual mínimo completo, com PE Windows de teste controlado pelo projeto
(`pr_winhello` variante 6), sem simulação e sem sucesso falso:

```
PE visual x64 → CPU x64 (subconjunto) → Win32 → GDI → Bitmap → BitBlt
             → superfície do processo → GfxFrame.surface → ponte Metal → tela → exit
```

**Resultado: o pipeline de software está COMPROVADO até GfxFrame.surface + estágio
de submissão Metal (7 das 8 etapas com evidência executada). A última etapa
(Metal apresentar em tela iOS) está IMPLEMENTADA mas é PARTIAL neste ambiente:
não há GPU/dispositivo iOS no Linux para comprovar a apresentação real.**

## Status por componente

| Componente | Status | Evidência |
|---|---|---|
| CPU x64 (subconjunto mínimo) | **WORKING** | `pr_cpu64.c` novo: movs/ALU/call [rip+disp]/ret/INT/hlt straight-line; PE visual executa 112 instruções e termina com exit 0 |
| CPU x64 fora do subconjunto | **WORKING (recusa honesta)** | opcode/RIP/endereço registrados + `EXECUTION STOPPED … rip=… opcode=… addr=…` (teste C + Swift) |
| Win32 (Kernel32/GDI32) | **WORKING (parcial)** | 40 APIs reais (38 + CreateCompatibleBitmap + BitBlt); não implementadas → UNSUPPORTED + log |
| GDI superfície virtual | **WORKING** | `pr_surf`: XRGB8888 (0x00RRGGBB), largura/altura/formato/buffer/stride (`pitch = width*4`), leitura/escrita |
| Bitmap (CreateCompatibleBitmap) | **WORKING** | buffer próprio por bitmap, dimensões validadas (1..4096), seleção de alvo em DC, DeleteObject libera |
| BitBlt SRCCOPY | **WORKING** | cópia real por linha com stride; limites/clip origem+destino+negativos testados; rop≠SRCCOPY → recusa honesta |
| GfxFrame.surface | **WORKING** | `GfxSurfaceBuffer` reutilizado (1 cópia por frame, sem duplicação extra); formato documentado em `SurfaceBridge.swift` |
| Conversão de formato | **WORKING** | XRGB8888→BGRA8 (B,G,R,A=255) testada pixel a pixel (`GraphicsBridgeTests`) |
| Metal (ponte) | **PARTIAL** | código pronto (`MetalGameRenderer`: textura `.shared` + `replaceRegion` + blit fullscreen) e estágio de submissão testado; execução em GPU/dispositivo iOS **não é comprovável no Linux** |
| PE visual (marco) | **WORKING** | variante 6: fundo branco + retângulo vermelho + retângulo azul → padrão pixel-exato na superfície + exit 0 |
| MessageBoxA | **NÃO NECESSÁRIA** | o marco visual não a exige (não criada — sem APIs falsas) |

## As 8 etapas do marco — evidência executada

Log real da execução do PE visual (suíte C `test_visual` / suíte Swift
`testVisualPEPaintsSurfaceThroughFrame`):

```
[PE] loaded base=0x00400000 entry=0x00402000 arch=x86-64
[CPU] execution started arch=x86-64
[WIN32] API gdi32.dll!CreateCompatibleDC
[WIN32] API gdi32.dll!CreateCompatibleDC
[WIN32] API gdi32.dll!CreateCompatibleBitmap
[GDI] bitmap created 320x240 handle=0xD1300002
[WIN32] API gdi32.dll!SelectObject
[WIN32] API gdi32.dll!CreateSolidBrush
[WIN32] API gdi32.dll!SelectObject
[WIN32] API gdi32.dll!PatBlt          (fundo branco)
[WIN32] API gdi32.dll!CreateSolidBrush
[WIN32] API gdi32.dll!SelectObject
[WIN32] API gdi32.dll!PatBlt          (retângulo vermelho 40,40,120,80)
[WIN32] API gdi32.dll!CreateSolidBrush
[WIN32] API gdi32.dll!SelectObject
[WIN32] API gdi32.dll!PatBlt          (retângulo azul 160,120,100,60)
[WIN32] API gdi32.dll!BitBlt
[GDI] BitBlt 320x240 (0,0)->(0,0) rop=SRCCOPY
[GDI] surface updated
[WIN32] API kernel32.dll!ExitProcess
[PROCESS] exit code 0
```

| # | Etapa | Status | Evidência |
|---|---|---|---|
| 1 | PE carregado | **WORKING** | `[PE] loaded` + imagem em grupos de página com prot COFF |
| 2 | CPU executou | **WORKING** | `[CPU] execution started` + 112 instruções x64 reais (subconjunto) |
| 3 | Win32 executou | **WORKING** | 13 dispatches `[WIN32] API` com argumentos reais (Win64 ABI: RCX/RDX/R8/R9 + pilha) |
| 4 | Desenhou (bitmap) | **WORKING** | `[GDI] bitmap created 320x240` + PatBlt preencheu o buffer do bitmap |
| 5 | BitBlt | **WORKING** | `[GDI] BitBlt 320x240 (0,0)->(0,0) rop=SRCCOPY` |
| 6 | GfxFrame.surface recebeu | **WORKING** | `[GDI] surface updated` + padrão pixel-exato (branco/vermelho/azul) via `consumeGraphicsFrame` |
| 7 | Metal recebeu | **PARTIAL** | estágio de submissão `MetalFrameUpload` (BGRA8) TESTADO; `MTLTexture.replaceRegion` escrito no renderer — requer dispositivo iOS p/ executar |
| 8 | Frame apresentado + exit | **apresentar=PARTIAL / exit=WORKING** | `[METAL] frame presented` (NSLog no renderer) e `cmdBuffer.present` escritos; `[PROCESS] exit code 0` comprovado |

**Último estágio comprovadamente executado: etapa 6 — GfxFrame.surface recebeu o
padrão desenhado pelo PE (bitmaps → BitBlt → superfície → frame), com exit 0.**

### Por que Metal é PARTIAL (explicação exata)

O ambiente de validação é Linux: não existe Metal, MTKView, nem dispositivo iOS.
A ponte foi implementada com APIs permitidas e seguras no iOS
(`MTLTexture.storageMode.shared` + `replaceRegion` + blit fullscreen no pipeline
existente — **o renderer Metal que funciona NÃO foi substituído**, só recebeu a
ponte) e o estágio CPU-side (conversão XRGB8888→BGRA8, buffer reutilizado,
dimensões) está coberto por testes em `GraphicsBridgeTests` +
`testVisualPEPaintsSurfaceThroughFrame`. A comprovação visual em tela exige
build no Xcode + iPhone físico (prioridade: iPhone > iPad > simulador).

## Padrão visual do PE de teste (contrato de pixels)

Superfície 320×240, XRGB8888 (`0x00RRGGBB`), stride 1280 bytes/linha:

| Região (x,y,w,h) | Cor | Pixel |
|---|---|---|
| fundo (0,0,320,240) | branco | `0x00FFFFFF` |
| retângulo (40,40,120,80) | vermelho | `0x00FF0000` |
| retângulo (160,120,100,60) | azul | `0x000000FF` |

## Verificações e testes (build final)

| Item | Quantidade |
|---|---|
| Checks C (`build/pr_tests`) | **741** (545 anteriores preservados + 196 novos) |
| Testes Swift (`swift test`) | **47** (42 anteriores + 5 novos em 2 suítes) |
| Falhas | **0** |
| Warnings (`-Wall -Wextra -Wshadow`) | **0** |
| Testes removidos | **0** |

Testes novos: `test_cpu64.c` (ALU/movs/stack/call [rip]/RIP-relative/INT/hlt/
ret imm16/budget + faults com opcode/RIP/endereço/guard) · `test_visual.c`
(bitmap, seleção, PatBlt, BitBlt SRCCOPY com limites/negativos/rop inválido,
PE visual completo com padrão e logs) · `GraphicsBridgeTests` (conversão de
formato, upload com reuso de buffer, formato documentado) ·
`testVisualPEPaintsSurfaceThroughFrame` (marco E2E Swift) ·
`testX64SubsetFaultIsDiagnosed` (fronteira honesta do subconjunto x64) ·
`testUnsupportedArchRefusedHonestly` (ARM → recusa com "Win32 … NÃO está integrada").

3 asserts de fronteira antiga foram atualizados para o novo comportamento real
(não eram regressões): x64 deixou de ser recusado no step (agora executa o
subconjunto → `PROCESS EXIT` em vez de `ARCH_UNSUPPORTED`); `BitBlt` deixou de
estar "sem implementação" (caso de recusa honesta agora coberto por `StretchBlt`).

## CPU x64 — escopo real (não é compatibilidade completa)

Implementados: `push/pop r64`, `mov r32/r64` (imm e reg/mem), `mov [rsp+disp], imm32/r64`,
`add/sub/and/or/xor/cmp` (imm8/imm32), `xor rm,r`, `call rm64 (RIP-relative/ABS)`,
`jmp rm64`, `ret`, `ret imm16`, `INT n`, `nop`, `hlt` + ModRM/SIB (reg, [base+disp],
SIB, RIP-relative). Semântica real de zero-extend de ops de 32 bits.

NÃO implementados (com recusa honesta de opcode/RIP/endereço): flags/jcc, SSE/FPU,
shifts/mul/div, string ops, chamadas de sistema além de `INT 0x2E`, o restante da
x86-64. **Não é JIT** (proibido no iOS) — é interpretador. **Nada disso é falso:
instrução fora do subconjunto = `EXECUTION STOPPED` com diagnóstico completo.**

## Limites declarados (sem sucesso falso)

- **Jogos Windows NÃO estão declarados funcionando.** Este marco prova um PE
  gráfico de teste controlado pelo projeto, não software comercial.
- Win32: 40 APIs com comportamento real; o resto é catalogado como
  não-implementado com log detalhado (`StretchBlt`, fontes, texto, COM, sockets…).
- DirectX/DXVK/Vulkan/Wine/multiplayer/shaders complexos: fora do escopo desta etapa.
- Metal em tela iOS: PARTIAL até validação em dispositivo físico.
- JIT: não utilizado (proibido no iOS); bypass de sandbox/assinatura: nunca.

## Próximos passos recomendados

1. Build no Xcode + iPhone físico: comprovar etapas 7–8 (MTLTexture → tela).
2. Ampliar o subconjunto x64 conforme novos PEs exigirem (com testes por instrução).
3. `StretchBlt`/`TextOutA` para PEs com texto; MessageBoxA quando um PE real exigir.
