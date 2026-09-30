# WINOS GAME RUNTIME — AUDIT

Data: 2026-09-30
Baseline: projeto existente WinOS com 63 Swift files, 20 C files, 20 headers, 1 target Portico (app), 36 C tests, 10 Swift tests

## A. PE Loader

**Arquivos:**
- `Sources/PorticoRuntime/include/portico/pr_pe.h`
- `Sources/PorticoRuntime/src/pr_pe.c` (parte de `pr_pe.o`)
- `Sources/PorticoCore/Import/PELoader.swift`

**Formatos suportados:**
- PE32 (x86 32-bit, machine 0x014C, isPE32Plus=false) — SUPORTADO, execução real via `pr_peproc` + IA-32 interpreter
- PE32+ (x64 64-bit, machine 0x8664, isPE32Plus=true) — SUPORTADO PARCIAL, subconjunto straight-line (movs/ALU/call [rip]/ret/INT/hlt), fora do subconjunto → EXECUTION STOPPED com opcode/RIP
- ARM64 (machine 0xAA64) — NÃO SUPORTADO, recusa honesta `unsupportedArch`
- Outros (ARM, etc) — NÃO SUPORTADO, recusa honesta

**Funcionalidades:**
- `pr_pe_looks_like`: verifica MZ — IMPLEMENTADA
- `pr_pe_scan`: analisa DOS header, e_lfanew, PE signature, machine, sections, arch string — IMPLEMENTADA
- `pr_pe_sections`: lista seções — IMPLEMENTADA
- `pr_pe_import_name`: nome DLL importada — IMPLEMENTADA
- `pr_pe_import_func_count` / `pr_pe_import_func_at`: funções importadas, by ordinal vs by name — IMPLEMENTADA
- `pr_pe_import_iat_rva`: IAT RVA — IMPLEMENTADA
- `pr_pe_data_dir`: data directories — IMPLEMENTADA
- `pr_pe_exports`: exports — IMPLEMENTADA
- `pr_pe_reloc_count` / `pr_pe_reloc_at` / `pr_pe_reloc_validate` / `pr_pe_reloc_apply`: relocations — IMPLEMENTADA
- `pr_pe_load` / `pr_pe_loaded_free` / `pr_pe_loaded_entry` / `pr_pe_loaded_ptr` / `pr_pe_loaded_string`: carregamento em memória virtual R/W/X — IMPLEMENTADA
- `pr_pe_diagnose`: diagnóstico textual — IMPLEMENTADA

**Limites:**
- `PR_PE_MAX_SECTIONS = 96`
- `PR_PE_MAX_NAME = 64`
- `module_name` sugerido do arquivo

**Swift wrapper:**
- `PELoader.swift`: `loadImage(data, moduleName)` → `PELoadedImage` com `PEReport` (image.arch, machine, isPE32Plus, imports, sections) — IMPLEMENTADA
- `PEInspector.swift`: inspeção rápida — IMPLEMENTADA

**Testes:**
- `test_pe.c`, `test_pe_loader.c`, `test_pes.c`, `test_pe_real.c`, `test_peproc.c`
- Dados: 70+ EXEs de teste em `Tests/PorticoRuntimeTests/data/` (hello_*.exe, hello_dll.dll)
- Status: 76/76 PASS (referência anterior, precisa confirmar em build atual)

**Faltando:**
- TLS callbacks — NÃO IMPLEMENTADO (pode ser P0 para jogos que usam TLS)
- Exception handling (SEH) — PARCIAL (pr_unwind existe, mas SEH completo não)
- Delay-load imports — NÃO IMPLEMENTADO
- Bound imports — NÃO IMPLEMENTADO

## B. Windows API — Inventário

**Arquivo:** `Sources/PorticoRuntime/src/pr_win32.c` (8478 linhas), `pr_win32.h`

**Catálogo:** `g_catalog[]` com `sizeof(g_catalog)/sizeof(g_catalog[0])` entradas, cada `pr_win32_export` com module, name, ordinal, status `PR_WIN32_IMPLEMENTED`, fn, etc.

**Contagem por módulo (via grep `"*.dll"`):**
- `kernel32.dll`: 132 APIs — IMPLEMENTADAS (ex: CreateFileA/W, ReadFile, WriteFile, CreateThread, WaitForSingleObject, VirtualAlloc, HeapAlloc, etc)
- `opengl32.dll`: 49 APIs — IMPLEMENTADAS (wglCreateContext, wglMakeCurrent, glBegin, glEnd, glVertex3f, glClear, etc)
- `msvcrt.dll`: 42 APIs — IMPLEMENTADAS (printf, puts, malloc, etc)
- `user32.dll`: 38 APIs — IMPLEMENTADAS (CreateWindowExA/W parcial, MessageBox, GetSystemMetrics, etc)
- `gdi32.dll`: 29 APIs — IMPLEMENTADAS (MoveToEx, LineTo, Rectangle, CreatePen, TextOutA/W, etc)
- `advapi32.dll`: 23 APIs — IMPLEMENTADAS (RegOpenKeyA/W, RegQueryValue, etc)
- `ws2_32.dll`: 15 APIs — IMPLEMENTADAS (socket, connect, etc — parcial)
- `ole32.dll`: 7 APIs — IMPLEMENTADAS (CoInitialize, etc — parcial)
- `shell32.dll`: 5 APIs — IMPLEMENTADAS (parcial)
- Outros: 2 genéricos

**Total implementadas:** 218 `IMPL()` macros + 11 `PR_WIN32_IMPLEMENTED` checks = ~218-294 APIs (referência anterior 294/318)

**Separação:**

IMPLEMENTADA (comportamento real, fn != NULL):
- kernel32: File I/O (CreateFile, ReadFile, WriteFile, DeleteFile, FindFirstFile, GetFileAttributes, etc), Memory (VirtualAlloc, VirtualFree, HeapAlloc, GlobalAlloc), Thread (CreateThread, ExitThread, WaitForSingleObject, CreateMutex, CreateEvent, CreateSemaphore, SRWLock), Process (GetCurrentProcessId, GetCurrentThreadId, ExitProcess, TerminateProcess, GetCommandLine, GetEnvironmentVariable), Time (GetTickCount, QueryPerformanceCounter, Sleep), etc
- user32: Window (CreateWindowEx parcial — cria WinOSWindow via WindowManager? Na verdade pr_win32.c tem f_CreateWindowEx que chama pr_win32_surface? Precisa verificar), Message (PeekMessage, GetMessage, DispatchMessage, TranslateMessage), SystemMetrics, etc
- gdi32: GDI básico (MoveToEx, LineTo, Rectangle, TextOut, etc) + CreateDIBSection
- opengl32: OpenGL 1.1 software rasterizer via pr_gl.c (wglCreateContext, wglMakeCurrent, glBegin/End, glVertex, glClear, etc) — IMPLEMENTADO REAL
- msvcrt: C runtime (printf, malloc, etc)
- advapi32: Registry (RegOpenKey, RegQueryValue, etc) — mapeado para `fs_root/registry/`
- ws2_32: Winsock básico — PARCIAL

PARCIAL:
- user32: CreateWindowEx — cria janela? Verifica pr_win32.c f_CreateWindowEx — precisa auditar se cria WinOSWindow real ou apenas stub
- gdi32: CreateDIBSection — implementado?
- d3d* / dxgi — NÃO ENCONTRADO no grep `"d3d` (0 resultados) — NÃO IMPLEMENTADO
- xinput, dinput — NÃO ENCONTRADO — NÃO IMPLEMENTADO

STUB (status != IMPLEMENTED ou fn == NULL):
- Muitas APIs no catálogo com status CATALOGED mas não IMPLEMENTED — retornam PR_ERR_UNSUPPORTED com log honesto, nunca sucesso falso

NÃO IMPLEMENTADA:
- d3d9.dll, d3d10.dll, d3d11.dll, d3d12.dll, dxgi.dll — NÃO IMPLEMENTADAS (0 ocorrências)
- xinput*.dll, dinput*.dll — NÃO IMPLEMENTADAS
- bcrypt.dll, crypt32.dll, version.dll, shlwapi.dll, oleaut32.dll, comctl32.dll — NÃO IMPLEMENTADAS ou parcial (não encontradas no count)
- DirectSound, XAudio2, WASAPI — NÃO IMPLEMENTADAS (audio backend é waveOut stub?)

DESCONHECIDA:
- APIs fora do catálogo — tratadas como unknown em Win32Coverage

**Cobertura:**
- `Win32Catalog.coverage(for: PEReport)`: separa resolved/unresolved/unknown
- Exemplo log: `[WINOS-RUNTIME] WIN32_INIT unresolved: kernel32!SomeAPI`

## C. Filesystem

**Arquivo:** `pr_win32.c` com `fs_root[256]`

**Verificação:**

- `C:\` → mapeado para `fs_root` (host dir) — IMPLEMENTADO via `pr_win32_set_fs_root`
- `Windows\` → `fs_root/Windows/`? — IMPLEMENTADO parcial (normaliza path Windows para dentro de fs_root, recusa `..` que escapam)
- `System32` → `fs_root/System32/` ou `fs_root/Windows/System32/`? — IMPLEMENTADO parcial (search path)
- `Program Files` — NÃO VERIFICADO, mas path normalization deve lidar
- `AppData` — NÃO VERIFICADO
- `TEMP` → `fs_root/TEMP` ou `/tmp`? — NÃO VERIFICADO
- Registry → `fs_root/registry/<full>` — IMPLEMENTADO (pr_win32.c linha 2556: `snprintf(out_host, "%s/registry/%s", fs_root, out_full)`)
- Caminhos relativos — IMPLEMENTADO (normaliza)
- Caminhos absolutos — IMPLEMENTADO (normaliza, recusa `..`)
- File handles — IMPLEMENTADO (tabela de handles)
- Directory handles — IMPLEMENTADO (FindFirstFile)
- File attributes — IMPLEMENTADO (GetFileAttributesA/W)
- Copy/Move/Delete/Create/Read/Write/Seek — IMPLEMENTADOS (CreateFile, ReadFile, WriteFile, DeleteFile, SetFilePointer, etc)

**VFS:**
- `AppSandbox`: `Application Support/Portico/` + `Documents/WinOS/` + `Caches/WinOS/`
- `LibraryStore`: `library.json`
- `ConfigurationManager`: `config.json`
- `EnvironmentManager`: `environments.json`
- `ImportService`: suporta `.exe`, `.7z`, `.zip`, folder via document picker
- Testes: `test_vfs.c` — 20/20 PASS (referência)

**Faltando:**
- File locking — NÃO IMPLEMENTADO
- Async I/O (ReadFileEx) — PARCIAL (hello_readfileex.exe existe como teste)
- Memory mapped files (CreateFileMapping) — NÃO IMPLEMENTADO (pode ser P0)
- Junctions/Symlinks — NÃO IMPLEMENTADO

## D. Process/Thread

**Arquivo:** `pr_win32.c`, `pr_vm.c`, `pr_cpu.c`, `pr_cpu64.c`, `ProcessManager.swift`, `WinOSProcessManagerReal.swift`

**Verificação:**

- Threads: `CreateThread` → `pr_win32_thread`? — IMPLEMENTADO (f_CreateThread)
- Synchronization: Mutex (`CreateMutexA/W`, `ReleaseMutex`), Events (`CreateEventA/W`, `SetEvent`, `ResetEvent`, `WaitForSingleObject`), Semaphore (`CreateSemaphoreA/W`, `ReleaseSemaphore`), SRWLock (`InitializeSRWLock`, `AcquireSRWLockExclusive/Shared`, `ReleaseSRWLockExclusive/Shared`), Critical Sections — IMPLEMENTADOS
- TLS: `TlsAlloc`, `TlsFree`, `TlsGetValue`, `TlsSetValue` — IMPLEMENTADOS (testes hello_tls*.exe)
- Process handles: `GetCurrentProcess`, `GetCurrentProcessId`, `OpenProcess`? — PARCIAL
- Wait functions: `WaitForSingleObject`, `WaitForMultipleObjects` — IMPLEMENTADOS (com timeout)
- Timers: `SetTimer`, `KillTimer`? — NÃO VERIFICADO
- Sleeps: `Sleep`, `SleepEx` — IMPLEMENTADOS
- Exit: `ExitProcess`, `ExitThread`, `TerminateProcess` — IMPLEMENTADOS (set halted flag, exit_code)

**ProcessManager.swift:**
- `ManagedProcess` in-process (iOS não tem fork/exec) — IMPLEMENTADO
- `spawn(label, onExit)` → `markRunning()`, `markFinished()`, `markFailed()` — IMPLEMENTADO
- `reap()` — IMPLEMENTADO

**WinOSProcessManagerReal.swift:**
- Process manager real com lista de processos, handle table — IMPLEMENTADO

**Testes:**
- `test_win32.c`, `test_win32x.c`, `test_peproc.c`
- Dados: `hello_thread.exe`, `hello_mutex.exe`, `hello_event.exe`, `hello_tls*.exe`, `hello_wait_multiple.exe`, etc

**Faltando:**
- APCs — NÃO IMPLEMENTADO
- Fiber — NÃO IMPLEMENTADO
- Thread pool — NÃO IMPLEMENTADO

## E. Graphics

**Arquivos:**
- `pr_gl.c` (1463 linhas) — OpenGL 1.1 software rasterizer REAL
- `pr_gfx.c`, `pr_surf.c`
- `MetalGameRenderer.swift`, `MTKGameView.swift`, `Shaders.metal`
- `WinOSRenderEngine.swift`, `WinOSCompositor.swift`, `WinOSWindowManager.swift`, `SurfaceBridge.swift`

**Verificação:**

- OpenGL: OpenGL 1.1 software — IMPLEMENTADO REAL (49 APIs opengl32.dll)
  - `wglCreateContext(hdc)` → `pr_gl_wgl_create` — IMPLEMENTADO
  - `wglMakeCurrent(hdc, hglrc)` → `pr_gl_wgl_make_current` — IMPLEMENTADO
  - `wglDeleteContext` — IMPLEMENTADO
  - `SwapBuffers(hdc)` → `pr_gl_swap_buffers(surf)` — IMPLEMENTADO (apresenta para `pr_surf`)
  - `glBegin`, `glEnd`, `glVertex3f`, `glColor3f`, `glClear`, `glEnable`, `glViewport`, `glMatrixMode`, `glLoadIdentity`, `glOrtho`, `glTranslatef`, `glRotatef`, `glGetString`, `glGetIntegerv`, `glGetError`, `glFinish`, `glReadPixels`, etc — IMPLEMENTADOS
  - Testes: `test_gl.c` até `test_gl11.c` (12 testes GL)

- DirectDraw: NÃO IMPLEMENTADO

- Direct3D 9: NÃO IMPLEMENTADO (0 ocorrências d3d9)

- Direct3D 10: NÃO IMPLEMENTADO

- Direct3D 11: NÃO IMPLEMENTADO

- Direct3D 12: NÃO IMPLEMENTADO

- DXGI: NÃO IMPLEMENTADO

- Shader compilation: NÃO IMPLEMENTADO (OpenGL 1.1 não tem shaders, é fixed-function)

- Textures: OpenGL textures? `glTexImage2D`, `glBindTexture`? — NÃO VERIFICADO em pr_gl.c, pode ser PARCIAL

- Buffers: VBOs? — NÃO IMPLEMENTADO (OpenGL 1.1 não tem VBOs)

- Render targets: `pr_surf` (software surface) — IMPLEMENTADO

- Depth/Stencil: `glClearDepth`, `glDepthFunc`, `glDepthMask` — IMPLEMENTADOS

- Samplers: NÃO IMPLEMENTADO (OpenGL 1.1 usa `glTexParameter`)

- Blend state: `glEnable(GL_BLEND)` — IMPLEMENTADO parcial

- Rasterizer: software rasterizer em pr_gl.c — IMPLEMENTADO

- Command submission: `pr_gl_draw_arrays`, `pr_gl_draw_elements` — IMPLEMENTADOS

- Presentation: `pr_surf` → `GfxSurfaceBuffer` → `MetalGameRenderer` — IMPLEMENTADO

- Swap chain: `SwapBuffers` → `pr_surf` → present — IMPLEMENTADO para OpenGL

**Faltando para jogos reais:**
- Jogos modernos usam D3D11/D3D12, não OpenGL 1.1 — BLOQUEIO ARQUITETURAL
- Sem D3D11, sem DXGI, sem swapchain D3D — jogos D3D não iniciam
- Sem shader model — jogos com shaders falham

**Prioridade:**
- Para jogo de teste que usa OpenGL 1.1 (ex: hello_gl.exe), P0 já implementado
- Para jogo D3D, P0 é implementar D3D11 mínimo ou detectar e recusar honestamente

## F. Metal

**Arquivos:**
- `MetalGameRenderer.swift` (352 linhas)
- `MTKGameView.swift`
- `Shaders.metal`
- `WinOSRenderEngine.swift`
- `SurfaceBridge.swift`
- `WinOSCompositor.swift`

**Verificação:**

- Device: `MTLCreateSystemDefaultDevice()` — IMPLEMENTADO, com log `MTLDevice: Apple A15 GPU` em iPhone 13
- Command queue: `device.makeCommandQueue()` — IMPLEMENTADO
- Command buffer: `queue.makeCommandBuffer()` — IMPLEMENTADO
- Render pipeline: `device.makeRenderPipelineState(descriptor)` com `game_vertex` / `game_fragment` e `blit_vertex` / `blit_fragment` — IMPLEMENTADO
- Shaders: `Shaders.metal` com `game_vertex`, `game_fragment`, `blit_vertex`, `blit_fragment` — IMPLEMENTADO
- Textures: `internalTexture` (private, renderTarget) e `surfaceTexture` (shared, shaderRead, bgra8Unorm) — IMPLEMENTADO
- Buffers: `vertexBuffer` (storageModeShared, max 65536*3 vertices), `uniformBuffer` — IMPLEMENTADO
- Drawable: `view.currentDrawable`, `currentRenderPassDescriptor` — IMPLEMENTADO com logs de erro se nil
- Presentation: `cmdBuffer.present(drawable)`, `commit()` — IMPLEMENTADO
- Synchronization: command buffer present + commit, sem espera explícita (Metal sincroniza) — IMPLEMENTADO
- Frame pacing: `FramePacer` com `targetFPS`, `shouldRender(now)` — IMPLEMENTADO
- Resize: `initialize(targetSize, renderScale)`, `resize(targetSize, renderScale)`, `rebuildInternalTexture()` — IMPLEMENTADO
- Orientation: `mtkView(_:drawableSizeWillChange:)` — IMPLEMENTADO

**SurfaceBridge:**
- Formato: `XRGB8888 = 0x00RRGGBB` UInt32 LE, stride `width*4`, sem padding — DOCUMENTADO
- Conversão: `xrgb8888ToBGRA8` B = v&0xFF, G = (v>>8)&0xFF, R = (v>>16)&0xFF, A=255 — IMPLEMENTADO sem cópias desnecessárias (1 cópia GfxSurfaceBuffer + 1 conversão MetalFrameUpload)
- Upload: `MetalFrameUpload.stage(surface)` → `replaceRegion` com `bytesPerRow = width*4` — IMPLEMENTADO
- Reuso: buffer reutilizado, só realoca quando resolução muda — IMPLEMENTADO

**Compositor:**
- `WinOSCompositor.swift`: dirty rects, triple buffering — IMPLEMENTADO

**RenderEngine:**
- `WinOSRenderEngine.swift`: Metal primary + Software fallback — IMPLEMENTADO
- Software fallback: `[UInt32]` XRGB8888, stride width*4 — IMPLEMENTADO com validação bounds

**Faltando:**
- Validação real em iPhone 13 físico — UNVERIFIED REQUIRES PHYSICAL DEVICE (sem xcodebuild no Linux)
- Performance metrics — PARCIAL (FPS via FramePacer, mas sem GPU timing)
- HDR, etc — NÃO NECESSÁRIO para P0

## G. Input

**Arquivos:**
- `InputCore.swift`, `TouchInputAdapter.swift`, `GameControllerAdapter.swift`
- `WinOSInputBridge.swift`
- `GameControllerBridge.swift`, `VirtualControlsView.swift`
- `pr_input.c`, `pr_input.h`

**Verificação:**

- Touch: `TouchInputAdapter` → `InputState` (moveX, moveY, lookX, lookY, buttons, triggerLT/RT) — IMPLEMENTADO
- Mouse: Touch traduzido para mouse? `WM_MOUSEMOVE`, `WM_LBUTTONDOWN/UP` — PARCIAL (via WinOSInputBridge)
- Keyboard: `WinOSInputBridge` com `pr_win32_input_key`, `pr_win32_input_char` — IMPLEMENTADO
- Gamepad: `GameControllerAdapter` + `GCController` — IMPLEMENTADO, com log `Controllers: %d`
- Virtual buttons: `VirtualControlsView.swift` — IMPLEMENTADO
- Analog sticks: `axes[0..3]` (moveX, moveY, lookX, lookY) — IMPLEMENTADO
- Triggers: `trigger_lt`, `trigger_rt` — IMPLEMENTADO
- D-pad: buttons bitmask — IMPLEMENTADO
- Pointer coordinates: `x, y` — IMPLEMENTADO
- Windows message translation: `WM_MOUSEMOVE`, `WM_LBUTTONDOWN`, `WM_KEYDOWN`, etc — PARCIAL (via pr_win32_input_*)

**WinOSInputBridge.swift:**
- DPI, hit testing — IMPLEMENTADO
- Mapeia InputState → pr_win32_input_* — IMPLEMENTADO

**Testes:**
- `test_input.c`, `InputPipelineTests.swift`

**Faltando:**
- Validação real touch→WM_* em jogo real — UNVERIFIED
- XInput — NÃO IMPLEMENTADO (mas GameController pode mapear)
- DirectInput — NÃO IMPLEMENTADO

## H. Audio

**Arquivos:**
- `AudioCore.swift`, `AVAudioEngineBackend.swift`
- `pr_audio.c`, `pr_audio.h`
- `pr_host.c` (audio ring)

**Verificação:**

- waveOut: `waveOutOpen`, `waveOutWrite`, etc — NÃO IMPLEMENTADO (stub que retorna UNSUPPORTED)
- DirectSound: NÃO IMPLEMENTADO
- XAudio2: NÃO IMPLEMENTADO
- WASAPI: NÃO IMPLEMENTADO
- PCM: `pr_audio` ring buffer — IMPLEMENTADO para PXP self-test (220Hz + buttons)
- Sample rate: `AVAudioSession.sharedInstance().sampleRate` — IMPLEMENTADO, log
- Channels: stereo (maxFrames*2) — IMPLEMENTADO
- Buffer: `pullAudio(maxFrames: 2048)` → `[Float]` — IMPLEMENTADO
- Playback: `AVAudioEngineBackend` — IMPLEMENTADO parcial (onAudioFrames callback)
- Latency: NÃO MEDIDO

**AudioCore.swift:**
- `AudioProfile`, `AudioBackend` — IMPLEMENTADO

**AVAudioEngineBackend.swift:**
- `AVAudioEngine` + `AVAudioPlayerNode` — IMPLEMENTADO

**Faltando:**
- waveOut, DirectSound, XAudio2 compatíveis — BLOQUEIO para jogos que usam áudio
- Audio real para PE — atualmente `pullAudio` retorna `[]` em `WindowsPEBackend` (linha: `// Win32/waveOut ainda não implementado — sem áudio (honesto). []`)

**Prioridade:**
- P0 para jogos sem áudio: OK (pode rodar sem áudio)
- P1 para jogos com áudio: implementar waveOut mínimo → AVAudioEngine

## Resumo Baseline

- C runtime: 3411 checks — referência, precisa confirmar via `test_main.c` (36 testes)
- PE: 76/76 PASS — referência, com 70+ EXEs de teste
- VFS: 20/20 PASS — referência
- Win32: 218 IMPL macros encontradas (vs 294/318 referência) — IMPLEMENTADA parcial, precisa contagem exata via `pr_win32_modules`
- Desktop Runtime: implementado (WindowManager, ProcessManagerReal, FileManagerReal, RenderEngine, Compositor, InputBridge, DesktopShell, RealDesktopView) — 8 arquivos
- Input: implementado, precisa validação real
- Metal: estrutura preparada, precisa validação real iPhone 13
- Audio: parcial (PXP OK, PE sem áudio)
- FPS/frame pacing: FramePacer implementado, não validado hardware
- Jogo comercial: ainda não validado — NENHUM jogo Windows real executado com sucesso completo (BOOT→LOAD→MENU→RENDER→INPUT→AUDIO→GAMEPLAY)
- GTA V, MX Bikes: NÃO declarar funcionando — conforme regra

## Próximos Passos (Fase 2+)

1. Executar baseline tests: C tests (pr_tests), PE tests, Swift tests
2. Implementar GameCompatibility layer com logs GAME START/DLL LOAD/API CALL/etc
3. DLL loader: dependency resolution, LoadLibrary, GetProcAddress — já existe pr_peproc, mas precisa diagnosticar MISSING_DLL e UNIMPLEMENTED
4. Win32: priorizar P0 APIs necessárias para jogo de teste (ex: CreateFileMappingW pode ser BLOCKING)
5. Graphics: detectar API gráfica do jogo escolhido, implementar D3D11 mínimo se necessário ou usar OpenGL 1.1 se jogo usar
6. Render loop: métricas FPS, frameTime, etc
7. Input real: mapear Touch → WM_*
8. Audio real: waveOut → AVAudioEngine
9. Diagnostics: WinOSGameDiagnostics com categorias INFO/WARNING/UNIMPLEMENTED/MISSING_DLL/etc
10. Game compatibility report
11. Teste primeiro jogo Windows real (ex: hello_real.exe ou similar)
12. Loop autocorreção: BUILD→TEST→ANALYZE→FIX→TEST→REGRESSÃO

---
Auditoria gerada em `WINOS_GAME_RUNTIME_AUDIT.md`
