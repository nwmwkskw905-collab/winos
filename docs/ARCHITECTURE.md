# Arquitetura do Portico

Plataforma iOS/iPadOS de execução/compatibilidade para jogos de PC —
biblioteca, importação, configurações por jogo, ambientes/prefixos, runtime,
gráficos (Metal), áudio, controles, overlay e diagnóstico.

```
App (SwiftUI)
│
├── UI/                      Biblioteca, detalhe, importação, configurações,
│                            editor de controles, sessão, overlay, logs, diagnóstico
│
├── Library (LibraryStore)   GameProfile persistente (library.json)
│
├── ConfigurationManager    globais + overrides por jogo → EffectiveConfig
│
├── RuntimeManager          coordena a sessão (preparing→running⇄paused→stopped/failed)
│   │
│   ├── ProcessManager      "processos" in-process (iOS não tem fork/exec)
│   ├── ExecutionBackend    pxp-interpreter (REAL) · windows-pe (interface honesta)
│   ├── InputManager        InputRouter (touch + GameController)
│   ├── AudioBackend        AVAudioEngine ← ring de áudio do runtime
│   └── GraphicsBackend     MetalBackend ← stream de comandos do runtime
│
├── CompatibilityLayer      vereditos SUPPORTED/PARTIAL/NOT SUPPORTED
│   ├── GraphicsTranslationFrontend (OpenGL/D3D → comandos internos)
│   ├── OpenGLTranslationMap (tabela de tradução real, testada)
│   └── Win32SurfaceArea    superfície de API a cobrir no futuro
│
├── EnvironmentManager      prefixos independentes (drive_c/, vars, estado, versão)
├── FileManager (AppSandbox) operações seguras dentro do sandbox
└── Logging/Diagnostics     LogCenter + DiagnosticsReport + pr_cap_probe
```

## Núcleo C (`Sources/PorticoRuntime`) — módulos e contratos

```
pr_types   status/tempo            pr_log   ring thread-safe de logs
pr_pe     parser + LOADER PE real  pr_zip   ZIP (stored/deflate, zlib) + path-safe
pr_cpu    interpretador IA-32      pr_asm   emissor de código p/ payloads e testes
pr_gfx    stream de comandos       pr_audio ring SPSC + síntese de tom
pr_surf   superfície/janela virtual + fence de sincronização
pr_win32  catálogo/dispatch Win32 por subsistema (kernel32…shell32)
pr_input  estado de entrada        pr_cap   sondagem JIT/W^X/memória
pr_host   sessão + ABI de backends pr_selftest payload de diagnóstico (PXP)
```

### Pipeline de um frame (execução real)

```
display timer
   → RuntimeManager.tick(input)
       → backend.stepFrame()                 [pr_host_frame]
           → CPU interpretada executa o guest até o PRESENT
           → guest emite MMIO → pr_gfx_cmd / PCM / logs
       → backend.consumeGraphicsFrame()      [pr_gfx_pull + vértices]
       → backend.pullAudio()                 [pr_audio_pull → AVAudioEngine]
       → backend.drainLogs()                 [pr_log_read → LogCenter]
   → MetalBackend.execute(frame)
       passe 1: comandos → framebuffer interno (resolução do jogo × qualidade)
       passe 2: blit escalado (linear/nearest) → drawable
```

### Formato PXP (payload nativo)

```
offset 0  "PXP0"
offset 4  u32 version (1)
offset 8  u32 load_addr
offset 12 u32 entry
offset 16 u32 code_size
offset 20 u32 bss_size
offset 24   código IA-32 (interpretado)
```

O guest conversa com a plataforma por **MMIO** em `0xF0000000` (gráficos +
vértices), `+0x100` (entrada), `+0x200` (tom de áudio), `+0x300` (relógio,
log, halt, RNG). Coordenadas usam **Q16.16** (o guest não tem FPU). Especificação
completa no cabeçalho de `pr_selftest.c`.

### Decisões de desempenho

- Sem alocações por frame: ring buffers e comandos em memória pré-alocada.
- Vértices copiados uma única vez por frame para `MTLBuffer` persistente.
- Framebuffer interno reutilizado (`storageMode .private`), blit por pipeline fixo.
- Pacing de FPS com deadlines acumulativos (sem drift).
- Comunicação Swift↔C por ponteiros opacos estáveis; conversões de string só em
  fronteiras frias (logs/diagnóstico).

## Persistência

```
Application Support/Portico/
├── library.json         [GameProfile]
├── settings.json        GlobalSettings
├── environments.json    [EnvironmentProfile]
├── Games/<id>/          arquivos importados
├── Environments/<id>/   prefixos (drive_c/, logs/, environment.json)
├── Logs/, Covers/, Imports/, Temp/
```
