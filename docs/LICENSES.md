# Licenças e origens de componentes

## Código deste projeto

Todo o código em `Sources/`, `Tests/` e `scripts/` é **produção original deste
projeto** (sem cópia de código proprietário, assets ou marcas de terceiros).
Licença do projeto: à escolha do autor (recomendado: MIT, que convive bem com
eventuais componentes LGPL isolados em módulos próprios).

## Componentes usados (já presentes)

| Componente | Origem | Licença | Uso |
|---|---|---|---|
| zlib (`libz`) | Biblioteca do sistema (SDK do iOS / Linux) | zlib License | inflate/crc32 do leitor ZIP |
| Metal, MetalKit, AVFoundation, GameController, SwiftUI, UIKit, Foundation | Apple SDK (não redistribuído) | Termos da Apple | Gráficos, áudio, entrada, UI |
| Swift toolchain | swift.org (ferramenta de build) | Apache-2.0 (com exceções LLVM) | build/test no Linux/macOS |

Nenhum código de terceiros é embutido no repositório. Os testes usam apenas
as bibliotecas do sistema.

## Componentes avaliados para integração futura (NÃO incluídos)

Detalhes, restrições e viabilidade em `BACKEND_INTEGRATION.md`. Licenças
conforme os repositórios oficiais dos projetos — **confirmar no ato da
integração** e cumprir as obrigações (avisos, oferta de fonte para LGPL,
preservação de notices):

- Wine — LGPL-2.1-or-later
- Box86 / Box64 — MIT
- FEX-Emu — MIT
- Unicorn Engine — BSD-2-Clause
- QEMU — BSD-2-Clause (algumas partes GPL)
- DXVK — Zlib
- MoltenVK — Apache-2.0
- VKD3D-Proton — LGPL-2.1-or-later
- Mesa (zink) — MIT; ANGLE — BSD-3-Clause

Marcas citadas (Windows, Direct3D, OpenGL, Metal, Wine...) pertencem aos seus
respectivos donos e são usadas aqui apenas como referência técnica.
