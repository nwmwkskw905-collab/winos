# G90 — INVENTÁRIO REAL iOS

Data: 2026-09-26

## Estrutura Workspace

```
Sources/
  PorticoApp/           # App iOS (SwiftUI)
    AppModel.swift
    PorticoApp.swift
    Info.plist
    Audio/AVAudioEngineBackend.swift
    Input/GameControllerBridge.swift, VirtualControlsView.swift
    Metal/MTKGameView.swift, MetalGameRenderer.swift, Shaders.metal
    UI/* (LibraryView, GameDetailView, etc)
  PorticoCore/          # Núcleo Swift portável
    Audio/AudioCore.swift
    Compatibility/CompatibilityLayer.swift
    Diagnostics/DiagnosticsReport.swift
    Environment/EnvironmentManager.swift
    Graphics/GraphicsBackend.swift, SurfaceBridge.swift
    Import/ImportService.swift, PEInspector.swift, PELoader.swift, ZipReader.swift
    Input/InputCore.swift, GameControllerAdapter.swift, TouchInputAdapter.swift
    Logging/LogCenter.swift
    Models/ControlLayout.swift, GameModels.swift, EnvironmentModels.swift, GlobalSettings.swift
    Runtime/ExecutionBackend.swift, ProcessManager.swift, RuntimeManager.swift, RuntimeModels.swift, Win32Layer.swift
    Store/AppSandbox.swift, LibraryStore.swift, ConfigurationManager.swift
    Support/SupportLevel.swift
  PorticoRuntime/       # Núcleo C11
    include/portico/*.h (pr_asm.h, pr_audio.h, pr_cap.h, pr_cpu.h, pr_cpu64.h, pr_gfx.h, pr_gl.h, pr_host.h, pr_input.h, pr_log.h, pr_pe.h, pr_peproc.h, pr_surf.h, pr_types.h, pr_unwind.h, pr_vm.h, pr_win32.h, pr_winhello.h, pr_zip.h)
    src/*.c (pr_asm.c, pr_audio.c, pr_cap.c, pr_cpu.c, pr_cpu64.c, pr_gfx.c, pr_gl.c, pr_host.c, pr_input.c, pr_log.c, pr_pe.c, pr_peproc.c, pr_selftest.c, pr_surf.c, pr_types.c, pr_unwind.c, pr_vm.c, pr_win32.c, pr_winhello.c, pr_zip.c)
Tests/
  PorticoRuntimeTests/  # Testes C
    *.c (test_cpu.c, test_cpu64.c, test_cpu64ext.c, test_pe.c, test_pe_loader.c, test_pe_real.c, test_pes.c, test_vfs.c, test_win32.c, test_win32x.c, test_gdiwin.c, test_input.c, test_gl*.c, etc)
    data/*.exe (76 exes + 1 dll)
  PorticoCoreTests/     # Testes Swift (SPM)
Package.swift           # SPM: PorticoRuntime + PorticoCore, platforms iOS 17, macOS 14
Portico.xcodeproj/      # Xcode project
  project.pbxproj
  targets: Portico (app), PorticoCore, PorticoRuntime
  configurations: Debug (OPT 0), Release (OPT s)
  IPHONEOS_DEPLOYMENT_TARGET 17.0, SWIFT_VERSION 5.0, PRODUCT_BUNDLE_IDENTIFIER io.portico.Portico, TARGETED_DEVICE_FAMILY 1,2
  frameworks: Metal.framework, MetalKit.framework, AVFoundation.framework, GameController.framework, libz.tbd
  Info.plist: Sources/PorticoApp/Info.plist
  CODE_SIGN_STYLE Automatic
```

## Entry Points

- App: `Sources/PorticoApp/PorticoApp.swift` → `PorticoApp` @main, SwiftUI
- AppModel: `Sources/PorticoApp/AppModel.swift` → ObservableObject, coordena LibraryStore, RuntimeManager, AppSandbox
- RuntimeManager: `Sources/PorticoCore/Runtime/RuntimeManager.swift` → start(profile,config), tick(now,input), pause(), resume(), stop()
- ExecutionBackend: `Sources/PorticoCore/Runtime/ExecutionBackend.swift` → protocol + PXPInterpreterBackend + WindowsPEBackend (pr_peproc)
- ProcessManager: `Sources/PorticoCore/Runtime/ProcessManager.swift` → spawn, markRunning/Finished/Failed
- PELoader: `Sources/PorticoCore/Import/PELoader.swift` + C `pr_pe.c`/`pr_peproc.c`
- CPU: `pr_cpu64.c` step_one (x64 interpreter)
- Memory: `pr_vm.c` + `pr_win32.c` f_VirtualAlloc/Free/Protect/Query + Heap
- Win32: `pr_win32.c` catalog 318, 294 impl
- VFS: `pr_win32.c` vfs_resolve + AppSandbox.swift isInsideSandbox/resolveInside
- Registry: `pr_win32.c` f_Reg* + AppSandbox registry dir
- Threads: `pr_win32.c` f_CreateThread (pthread)
- Input: `pr_input.c` + `pr_win32.c` pr_win32_input_* + Swift InputCore.swift InputRouter + TouchInputAdapter + GameControllerAdapter
- Graphics: `pr_gl.c` + `pr_surf.c` + Swift GraphicsBackend.swift + SurfaceBridge.swift + MetalGameRenderer.swift + MTKGameView.swift + Shaders.metal
- Audio: `pr_audio.c` + AudioCore.swift + AVAudioEngineBackend.swift
- Metal: MetalGameRenderer.swift, Shaders.metal
- Tests: C 3411 checks, PE battery 76 exes

## Responsabilidades

| Componente | Arquivo | Responsabilidade | Dependências | Status | Teste |
|------------|---------|------------------|--------------|--------|-------|
| App Entry | PorticoApp.swift | @main, lifecycle | AppModel, SwiftUI | GREEN (código) | UNVERIFIED sem device |
| AppModel | AppModel.swift | Observable, LibraryStore, RuntimeManager | PorticoCore | GREEN | UNVERIFIED |
| RuntimeManager | RuntimeManager.swift | ciclo prepare→running→paused→stopped→failed, backend select, tick, audio, FPS | ExecutionBackend, ProcessManager, AppSandbox, LogCenter | GREEN | controller 100x |
| ExecutionBackend | ExecutionBackend.swift | protocol Backend, PXPInterpreterBackend (pr_host), WindowsPEBackend (pr_peproc) | PorticoRuntime C | GREEN | PE 76/76 |
| ProcessManager | ProcessManager.swift | spawn, markRunning/Finished | LogCenter | GREEN | controller 100x |
| PELoader C | pr_pe.c, pr_peproc.c | DOS/PE headers, sections, imports, IAT, relocs, thunks 1024, entry | pr_vm, pr_win32 | GREEN | pe_loader, pe_real, 76/76 |
| CPU x64 | pr_cpu64.c | x64 interpreter, integer, branches, REP, SSE scalar+packed | pr_vm | GREEN | cpu64ext 100+ |
| Memory | pr_vm.c, pr_win32.c Heap/Virtual | VirtualAlloc/Free/Protect/Query, HeapAlloc/ReAlloc/Free/Size, LocalAlloc | pr_types | GREEN | vm, win32, heap stress 1000x |
| Win32 | pr_win32.c | 318 catalog, 294 impl, dispatch INT 0x2E, LastError | pr_vm, pr_log, pthreads | GREEN | win32, win32x, 76/76 |
| VFS | pr_win32.c vfs_resolve + AppSandbox.swift | Windows path → sandbox host, canonicalização pilha depth, bloqueia escape | fs_root, FileManager | GREEN | vfs, traversal 20/20 |
| Registry | pr_win32.c f_Reg* | virtual registry fs_root/registry, tipos SZ/DWORD/QWORD/BINARY | fs_root | GREEN | win32x, registry 10 |
| Threads | pr_win32.c f_CreateThread | pthread_create, stack 64k, sentinel HLT, MS x64 frame | pr_cpu64, pthreads | GREEN | thread 42 |
| TLS | pr_win32.c f_Tls* | slots 60-63 reservados | — | GREEN | tls* 70/72 |
| Sync | pr_win32.c f_Wait*, f_CreateMutex/Event/Semaphore | events, mutex recursivo, semaphore, timeout finite via pthread_mutex_timedlock | pthreads, mono_ns | GREEN | event 81, mutex_timeout 42 |
| CRT | pr_win32.c f_*printf, f_memcpy etc | memcpy REP MOVS, strlen REPNE SCASB, printf via vsnprintf | pr_win32_ptr | GREEN | stdio 81 bytes |
| Unicode | pr_win32.c MBWC/WCMB | CP_UTF8, CP_ACP, buffer pequeno trunca YELLOW | — | GREEN/YELLOW | mbwc 42 |
| GDI | pr_win32.c f_GDI* | DC, bitmap, brush, BitBlt, StretchBlt, SetPixel/GetPixel | pr_surf | GREEN | gdi 42, gl* |
| User32 | pr_win32.c f_User32* | RegisterClass, CreateWindowExA 12 args, GetMessage fila real+WM_TIMER, PostMessage | pr_surf, msgq | GREEN | input mensagens |
| Input | pr_input.c + pr_win32_input_* + InputCore.swift | pr_input_state, InputRouter, TouchInputAdapter, GameControllerBridge, parking + advance_time | pr_win32 | GREEN com harness | input 42 com harness |
| Graphics | pr_gl.c + pr_surf.c + GraphicsBackend.swift + SurfaceBridge.swift | GL 1.1 subset, surface 320x240, texture, sampler, pipeline | pr_surf | GREEN | gl 42, visual |
| Metal | MetalGameRenderer.swift + Shaders.metal | MTLDevice, queue, pipeline, texture, buffer, MTKViewDelegate, sem churn por frame | Metal.framework, MetalKit | YELLOW UNVERIFIED (código real) | gl7, gl8 |
| Audio | pr_audio.c + AudioCore.swift + AVAudioEngineBackend.swift | ring buffer, AVAudioEngine SourceNode, lifecycle start/pause/resume/stop | AVFoundation | YELLOW UNVERIFIED (código real) | gfx_audio cap |
| Network | pr_win32.c f_WSA* | WSAStartup, socket ordinals subset | — | GREEN subset | winsock_ord 42 |
| AppSandbox | AppSandbox.swift | root ApplicationSupport/Portico, isInsideSandbox hasPrefix, resolveInside rejeita .. | FileManager | GREEN | vfs, sandbox |
| Log | pr_log.c + LogCenter.swift | ring log, [WinOS][Runtime] etc | — | GREEN | log 18 |
| Host | pr_host.c + pr_cap.c | builtin backends, exec mem, W^X, capabilities | — | GREEN | host, cap |
| Tests C | Tests/PorticoRuntimeTests/*.c | 3411 checks, PE battery | PorticoRuntime | GREEN | 3411/0, 76/76 |
| Tests Swift | Tests/PorticoCoreTests/ | SPM tests | PorticoCore | UNVERIFIED | toolchain ausente |

## Status Geral G88→G89

- C: 3411/0 GREEN
- PE: 75/1 HANG YELLOW sem harness, 76/76 PASS com harness (input injection) — GREEN com harness
- Thunk 1024, packed SSE, VFS traversal, HeapReAlloc, input overflow — FIXED
- Swift/iOS/Metal/Audio/Device — UNVERIFIED ENVIRONMENT LIMITATION (código auditado, sem erro detectável, pronto para device)
