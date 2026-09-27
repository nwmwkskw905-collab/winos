# Portico

App iOS/iPadOS para execução de jogos de PC, com foco em **Windows** — inspirado
em soluções da categoria "contêiner/compatibilidade" (Winlator), porém
**implementação própria**: nenhum código, asset ou marca de terceiros foi copiado;
componentes open source futuros entram isolados e com licenças respeitadas
(ver `docs/LICENSES.md`).

> **Aviso honesto (leia `docs/LIMITATIONS.md`)**
> O iOS **não permite** JIT nem fork/exec, e este build **não inclui** uma camada
> Win32 completa: executar jogos `.exe` Windows hoje está rotulado
> `NOT SUPPORTED` (com interface e contrato de backend prontos em
> `docs/BACKEND_INTEGRATION.md`). O que **roda de verdade agora** é o pipeline
> completo de execução nativa **PXP** (código IA-32 interpretado + gráficos
> Metal + áudio + entrada + logs), embarcado como "Jogo de Auto-Teste" —
> instale o app e toque em **Jogar**.

## Recursos

- **Biblioteca** com `GameProfile` persistente por jogo (executável, argumentos,
  resolução, FPS, renderer, áudio, controles, prefixo/ambiente, extras).
- **Importação** via document picker oficial (ZIP stored/deflate e pastas),
  com inspeção PE (`.exe`) real — arquitetura, PE32/PE32+.
- **Configurações globais e por jogo** (overrides efetivos).
- **EnvironmentManager**: prefixos independentes (`drive_c/`, variáveis,
  estado, versão) — caminho natural para futuros prefixos compatíveis.
- **Runtime**: iniciar/preparar/encerrar, stdout/stderr (log), detecção de
  falha com mensagem clara.
- **Metal**: renderização em resolução do jogo × qualidade, upscale
  linear/nearest, controle de FPS (sem alocações por frame).
- **Áudio**: AVAudioEngine com interrupções, volume, pausa.
- **Controles**: touch configuráveis por jogo (posição/tamanho/transparência/
  função, salvos no perfil) + gamepads físicos (GameController).
- **Overlay**: voltar, pausar/continuar, configurações, controles, FPS,
  resolução, áudio, logs, encerrar jogo (sem parar o runtime à toa).
- **Logs**: INFO/WARNING/ERROR/DEBUG por execução, ver/limpar/exportar.
- **Diagnóstico**: sondagem real de capacidades (JIT, W^x, device/simulador) e
  relatório exportável — sem simulação de capacidades inexistentes.

## Estrutura

```
Sources/PorticoRuntime   núcleo C11 (CPU, PE, ZIP, gfx, áudio, host, cap)
Sources/PorticoCore      núcleo Swift (perfis, runtime, graphics, input...)
Sources/PorticoApp       app iOS (SwiftUI + Metal + AVFoundation + GameController)
Tests/                   188 checks C + 23 testes Swift (E2E incluso)
docs/                    LIMITATIONS, ARCHITECTURE, BACKEND_INTEGRATION...
```

## Build e testes

No Linux/macOS (núcleo; não requer Xcode):

```sh
./scripts/build_and_test.sh        # toolchain (se preciso) + make test
```

No macOS com Xcode 16+ (app iOS):

```sh
python3 scripts/gen_xcodeproj.py   # gera Portico.xcodeproj
# ou: xcodegen generate            # via project.yml
open Portico.xcodeproj             # build/run em iPhone físico > iPad > simulador
```

Alvos do Makefile: `make c-test`, `make swift-test`, `make test`,
`make gen-xcodeproj`, `make clean`.

## Política de honestidade

- Nada é declarado pronto sem estar compilado e testado.
- Nenhuma camada falsa (sem "DirectX fake", sem "JIT fake", sem fingir execução).
- Capacidades rotuladas `SUPPORTED` / `PARTIALLY SUPPORTED` / `NOT SUPPORTED`
  (`docs/LIMITATIONS.md`).
- Sem bypass de sandbox/assinatura/DRM e sem pirataria; somente conteúdo que o
  usuário tem direito de usar.

> **Execução real (Fase 7)**: PEs Windows 32-bit são executados de verdade —
> PE → memória virtual (R/W/X) → imports → Win32 (subset) → CPU IA-32
> interpretada → ExitProcess. GDI desenha na superfície apresentada ao Metal.
> PE64 e APIs ausentes são recusados com diagnóstico EXECUTION STOPPED. JIT é
> detectado (indisponível no iOS) e não há simulação de funcionalidades.
