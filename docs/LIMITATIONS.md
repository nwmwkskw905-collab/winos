# Limitações reais do iOS — e o que o Portico faz a respeito

Este documento existe para **nunca** fingir que algo funciona quando não funciona.
Cada capacidade é rotulada `SUPPORTED`, `PARTIALLY SUPPORTED` ou `NOT SUPPORTED`,
com detecção em tempo de execução quando a capacidade depende do ambiente.

## 1. Execução de software Windows (o ponto central)

> **Atualização (FASES 1–7, 2026-09-23):** CPU x64 expandida (ALU 8/16/32/64,
> RFLAGS, Jcc/SETcc, LEA, shifts/rotates, MOVZX/SX, ENTER/LEAVE — ver
> `RELATORIO_EXPANSAO_X64.md`); VirtualProtect, StretchBlt (SRCCOPY vizinho
> mais próximo), SetPixel/GetPixel, DeleteDC implementados; **MessageBoxA/W: sem
> UI neste build (sem janelas)** — registra caption/texto em log WARN e retorna
> IDOK, sem fingir janela; GetEnvironmentVariableA/W, GetSystemInfo (valores do
> espaço do Portico), GetModuleHandleW/GetCommandLineW/GetCurrentDirectoryA
> implementados; **TextOutA/W e CreateFontA continuam fora** (texto GDI só com
> fonte rasterizada completa — implementar somente quando correto). SSE/x87,
> FS/GS (TEB), string ops (rep movs/stos) e LoadLibrary continuam fora do
> subconjunto — recusa honesta com opcode/RIP/bytes.

| Item | Estado | Detalhe |
|---|---|---|
| Executar `.exe` (PE) Windows | **PARTIALLY SUPPORTED** | PEs 32-bit (IA-32) e PE32+ (subconjunto x64 straight-line) executam de verdade (pr_peproc + interpretadores + Win32 parcial). Instruções/APIs fora do suportado → `EXECUTION STOPPED` com diagnóstico completo. Jogos comerciais NÃO são declarados suportados. |
| Analisar/carrer PE (loader) | **SUPPORTED** | `pr_pe_load`: validação MZ/PE, arquitetura, PE32/PE32+, seções, entry point, imports por função (nome/ordinal), exports, diagnóstico legível. Mapeia a imagem em memória p/ inspeção — nenhum código é executado. |
| APIs Win32 (dispatch) | **PARTIALLY SUPPORTED** | `pr_win32`: catálogo por subsistema (kernel32/user32/advapi32/ws2_32/gdi32/ole32/shell32); 28 APIs kernel32 com comportamento real (heap, console, relógio, handles, módulos, ExitProcess); demais catalogadas retornam UNSUPPORTED com log (nunca fingem). |
| Interface de backend plugável | SUPPORTED | `pr_host_backend_v1` (C) + `ExecutionBackend` (Swift) com ciclo de vida `load/initialize/run/pause/resume/stop/shutdown`. |
| JIT (tradução dinâmica) | **NOT SUPPORTED no iOS** | O iOS proíbe apps de terceiros de gerar e executar código em tempo de execução (codesigning/W^X). Detectado em runtime por `pr_cap_probe()` — em device `jit_available = 0`. Nada de "JIT fake": sem JIT, tradução x86 só por **interpretação**. |
| Interpretação de CPU x86 (IA-32) | **PARTIALLY SUPPORTED** | Interpretador real de um subconjunto IA-32 (ALU, branches, pilha, MUL/DIV, INT, MMIO...). Executa payloads PXP nativos reais. Não cobre o conjunto completo; x86-64 cobre só o subconjunto straight-line (`pr_cpu64`) com recusa honesta fora dele. |
| fork/exec de processos | **NOT SUPPORTED** | O iOS não permite criar processos. O `ProcessManager` executa tarefas **in-process** com ciclo de vida equivalente (iniciar, capturar saída, detectar término/falha, encerrar). |

### Por que não há "falso Wine" aqui?
Uma camada de compatibilidade real exige: emulação/interpretação de CPU x86/x64
+ implementação das APIs Win32/NT (milhares de funções) + tradução gráfica +
tradução de áudio/entrada. Qualquer coisa menos que isso seria simulação. O
Portico entrega a **arquitetura e os pontos de encaixe testados**; a execução
Windows aparece como `NOT SUPPORTED` até que um componente real (ver
`BACKEND_INTEGRATION.md`) seja integrado.

## 2. Plataforma e sistema de arquivos

| Item | Estado | Detalhe |
|---|---|---|
| Sandbox do app | SUPPORTED | Todo conteúdo vive em `Application Support/Portico`. Nenhum acesso fora dele. |
| Importar arquivos | SUPPORTED | Document picker oficial (`fileImporter`), com security-scoped URLs. ZIP (stored/deflate) e pastas. |
| ZIP64 | **NOT SUPPORTED** | Limitação v1 do leitor ZIP próprio; detectada e reportada. |
| Arquivos > 1 GiB no ZIP | NOT SUPPORTED | Proteção contra arquivos malformados. |
| File sharing (app ↔ Finder/iTunes) | SUPPORTED | `UIFileSharingEnabled` para exportar logs/relatórios. |
| Acesso a diretórios arbitrários do usuário | **NOT SUPPORTED (por design)** | Política da plataforma e do projeto: sem contornar o sandbox. |

## 3. Gráficos

| Item | Estado | Detalhe |
|---|---|---|
| Metal (nativo) | SUPPORTED | Pipeline de comandos interno → framebuffer escalado → tela. |
| Superfície/janela virtual + apresentação por pixels | **PARTIALLY SUPPORTED** | `pr_surf`: surface XRGB8888 real (buffers), clear/fill_rect, apresentação no stream com textura do frame (GfxSurfaceBuffer → Metal) + fence de sincronização. Caminho estilo GDI/BitBlt: clear/fill/BitBlt (SRCCOPY com clipping e stride) prontos e testados; StretchBlt: pendente. |
| Controle de resolução interna | SUPPORTED | Renderização em `resolução do jogo × qualidade` + upscale. |
| Controle de FPS | SUPPORTED | `FramePacer` + `preferredFramesPerSecond`. |
| Tradução OpenGL → Metal | **PARTIALLY SUPPORTED** | Tabela de mapeamento e tracker de estado implementados e testados; frontend completo (GL 1.x/2.x) pendente. |
| Tradução Direct3D → Metal | **NOT SUPPORTED** | Interface `GraphicsTranslationFrontend` definida; frontend D3D não implementado. |
| x87/SSE no guest | **NOT SUPPORTED** | O interpretador IA-32 atual é inteiro (sem FPU); payloads PXP usam ponto fixo Q16.16 nas portas MMIO. |

## 4. Áudio

| Item | Estado | Detalhe |
|---|---|---|
| Reprodução durante execução | SUPPORTED | AVAudioEngine + source node + ring buffer. |
| Volume/pausa/retomada | SUPPORTED | `AudioSessionController`. |
| Interrupções (chamadas, alarmes) | SUPPORTED | `AVAudioSession.interruptionNotification`. |
| Áudio em segundo plano | PARTIAL | Depende do entitlement/`UIBackgroundModes` — desativado por padrão; a configuração global existe. |

## 5. Entrada

| Item | Estado | Detalhe |
|---|---|---|
| Touchscreen (virtuais configuráveis) | SUPPORTED | Editor por jogo (posição, tamanho, transparência, função). |
| Controles físicos (MFi/Bluetooth) | SUPPORTED | `GameController` (extended gamepad), hotplug. |
| Vibração/haptics de controle | NOT SUPPORTED | Fora do escopo v1. |

## 6. Restrições gerais do iOS levadas em conta

- **Sem JIT** para apps de terceiros → qualquer tradução de CPU é por interpretação.
- **Sem fork/exec** → "processos" são tarefas in-process.
- **Sem carregamento de código dinâmico** → backends devem ser compilados dentro do app.
- **W^X / codesigning** → memória é ou W ou X; sondado em `pr_cap_probe()`.
- **Execução em segundo plano limitada** → o runtime roda apenas com o app ativo.
- **Políticas de segurança não serão contornadas** (sem bypass de sandbox, assinatura ou DRM; somente conteúdo que o usuário tem direito de usar).

## 7. Detecção em tempo de execução

`pr_cap_probe()` (C) sonda: JIT disponível, mapeamento de memória executável, W^x,
tamanho de página, RAM, CPU, plataforma (device/simulador/dev-host). O
relatório aparece em **Configurações → Diagnóstico → Relatório de capacidades**
e é exportável. Testes automatizados garantem coerência (ex.: sem JIT, o
recurso não pode aparecer como suportado).

## Limitações restantes — execução Windows (Fase 7, 2026-09-22)

- **PE64/x64**: LOADER completo (carga/imports/relocations/proteções) +
  EXECUÇÃO pelo interpretador mínimo `pr_cpu64` (subconjunto straight-line:
  movs/ALU/pilha/call [rip+disp]/ret/INT 0x2E/hlt, Win64 ABI nos argumentos).
  Instrução fora do subconjunto → `EXECUTION STOPPED` com rip/opcode/addr.
  NÃO é compatibilidade x64 completa, não é JIT, nada é simulado.
- **Relocations**: HIGHLOW/DIR64 aplicadas de verdade (com validação); tipos
  raros (HIGH, LOW, HIGHADJ, MIPS…) recusados com motivo.
- **Import por ordinal**: resolvido apenas contra ordinais PÚBLICOS/estáveis
  (winsock.def); ordinal desconhecido recusado — nunca se adivinha.
- **Instruções**: subconjunto IA-32 (MOV/ALU/shifts/pilha/branch/MUL/DIV/INT/
  HLT/CPUID/BSWAP…); sem 16-bit, far call e FPU (usados por software real
  compilado com FPU → precisarão extensão).
- **Win32**: 40 APIs com comportamento real (kernel32 + gdi32 com
  CreateCompatibleBitmap/SelectObject/PatBlt/BitBlt SRCCOPY + ws2_32 ordinais).
  Demais catalogadas mas NÃO implementadas (StretchBlt, TextOutA/Fontes,
  MessageBoxA, COM, sockets…) → EXECUTION STOPPED com Module/Function.
- **Áudio/Input Win32** (waveOut, DirectInput, mensagens de janela): não.
- **Superfície GDI**: 320×240 XRGB fixa para o alvo do processo
  (CreateCompatibleDC(NULL)); bitmaps aceitam 1..4096 por lado. Escalonar o
  alvo conforme o software exigir.
- **MetalGameRenderer (app iOS)**: a PONTE `GfxFrame.surface` → MTLTexture
  está implementada (textura `.shared` + `replaceRegion` + blit fullscreen,
  renderer existente preservado); apresentação em tela aguarda validação em
  dispositivo iOS (Xcode + iPhone físico) — status PARTIAL.
- **Tela/janela Win32** (CreateWindow, message loop): fora do escopo atual.
