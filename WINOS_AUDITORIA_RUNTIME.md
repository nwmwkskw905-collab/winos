# WINOS — AUDITORIA OBRIGATÓRIA DO RUNTIME EXISTENTE

Data: 2026-09-29
Objetivo: Inventário completo antes de evolução para desktop real

## 1. Win32 APIs Implementadas (pr_win32.c g_catalog)

### kernel32.dll — IMPLEMENTED (real)
- GetTickCount64, QueryPerformanceFrequency/Counter, GetProcessHeap
- HeapAlloc/Free/Size/ReAlloc, LocalAlloc/Free/ReAlloc/Size, GlobalAlloc/Free/ReAlloc/Size
- VirtualAlloc/Free/Query/Protect
- GetStdHandle, WriteFile/ReadFile/ReadFileEx, CloseHandle
- SetLastError/GetLastError, lstrlenA/lstrcpyA/lstrcpynA/lstrcmpA
- ExitProcess/TerminateProcess/ExitThread, GetCurrentProcessId/ThreadId, GetCurrentThread, OpenThread, Suspend/ResumeThread, GetExitCodeThread
- IsDebuggerPresent, GetModuleHandleA/W, GetProcAddress, LoadLibraryA/W, FreeLibrary
- IsDBCSLeadByteEx, MultiByteToWideChar, WideCharToMultiByte
- TlsAlloc/SetValue/GetValue/Free
- Sleep, SetUnhandledExceptionFilter
- CriticalSection: Initialize/Enter/Leave/Delete
- File: CreateFileA/W, CreateDirectoryA/W, RemoveDirectoryA/W, DeleteFileA/W, FindFirstFileA/W, FindNextFileA/W, FindClose, GetFileSize, SetFilePointer/Ex, GetFileAttributesA/W/ExA/W, GetFullPathNameA/W, MoveFileA/W, CopyFileA/W, GetTempPathA/W, FlushFileBuffers, GetDiskFreeSpaceExA/W, SetCurrentDirectoryA/W, GetCurrentDirectoryA/W, GetModuleFileNameA/W
- Thread/Sync: CreateThread, WaitForSingleObject/MultipleObjects, CreateMutexA/W, ReleaseMutex, CreateEventA/W, SetEvent/ResetEvent, CreateSemaphoreA/W, ReleaseSemaphore, SRWLock: Initialize/AcquireExclusive/Shared/ReleaseExclusive/Shared
- Env: GetEnvironmentVariableA/W, GetCommandLineA/W, GetStartupInfoA, GetSystemInfo, GetVersion/ExA

### user32.dll — IMPLEMENTED (real)
- MessageBoxA/W (sem UI, log + IDOK)
- RegisterClassA (classes reais, hbrBackground)
- CreateWindowExA (janela real → superfície interna, sem área não-cliente)
- ShowWindow (SW_HIDE/visível, WM_SHOWWINDOW)
- GetClientRect, GetDC/ReleaseDC
- InvalidateRect (região parcial → janela inteira), UpdateWindow (WM_ERASEBKGND+WM_PAINT síncronos)
- BeginPaint (fundo apagado com brush classe, subset DefWindowProc), EndPaint
- DestroyWindow, PostQuitMessage
- GetMessageA/W (fila real+WM_TIMER, espera limitada por timer, vazia = EXECUTION STOPPED)
- PeekMessageA/W (fila real, WM_QUIT permanece)
- TranslateMessage (WM_KEYDOWN imprimível → WM_CHAR ASCII)
- DispatchMessageA/W (WndProc via reentrada convidado guest_call)
- DefWindowProcA/W (WM_ERASEBKGND/WM_PAINT/WM_CLOSE reais, demais 0)
- FillRect, PostMessageA/W (fila real, hwnd=0 = thread), SetFocus/GetFocus
- SetTimer/KillTimer (WM_TIMER real, TIMERPROC via reentrada)
- GetKeyState/AsyncKeyState (bit15 pressionada), GetSystemMetrics

### gdi32.dll — IMPLEMENTED (real)
- CreateCompatibleDC/Bitmap, CreateSolidBrush/Pen/FontA/W, SelectObject/DeleteObject/DeleteDC
- PatBlt, BitBlt, StretchBlt (SRCCOPY vizinho), MoveToEx/LineTo/Rectangle
- TextOutA/W, DrawTextA/W, SetPixel/GetPixel, GetDeviceCaps, CreateDIBSection, SwapBuffers (apresenta framebuffer GL na superfície XRGB)

### Outros
- opengl32: gl* (via pr_gl.c) — 22 testes G1..G22

### Win32 APIs Parcialmente Implementadas
- RegisterClassA: hbrBackground = brush ou (COLOR_xxx+1) — parcial, sem ícones/cursor
- CreateWindowExA: sem área não-cliente, sem estilos WS_OVERLAPPEDWINDOW completos
- ShowWindow: apenas SW_HIDE/visível
- InvalidateRect: região parcial tratada como janela inteira
- GetMessage: vazia = EXECUTION STOPPED (não bloqueia infinito)
- MessageBox: sem UI real
- SwapBuffers: apresenta framebuffer GL na superfície XRGB (simples)

### Win32 APIs Ausentes (necessárias para desktop real)
- CreateWindowExW, DestroyWindow já existe mas precisa W version
- MoveWindow, SetWindowPos, GetWindowRect, SetWindowTextA/W, GetWindowTextA/W, GetClassNameA/W
- IsWindow, IsWindowVisible, EnableWindow, SetForegroundWindow/GetForegroundWindow, GetWindow, GetParent/SetParent
- GetMessageW/PeekMessageW já alias A, mas precisa queue real multi-janela
- SendMessageA/W, GetWindowLongA/W, SetWindowLongA/W, SetWindowPos
- LoadIcon/Cursor, SetCursor, ShowCursor
- GetSystemMetrics já existe, mas precisa mais
- GetClientRect existe, GetWindowRect ausente
- InvalidateRect existe, mas ValidateRect, RedrawWindow ausentes
- ClipCursor, SetCapture/ReleaseCapture
- RegisterClassW, UnregisterClass
- AdjustWindowRect, ClientToScreen/ScreenToClient
- EnumWindows, FindWindowA/W

## 2. PE Loader
- PEInspector.swift: looksLikePE, scan (pr_pe_scan, pr_pe_import_name, pr_pe_sections)
- PELoader.swift: inspect (imports por função, exports, diagnóstico pr_pe_diagnose), loadImage (pr_pe_load)
- PELoadedImage: classe proprietária pr_pe_loaded, imageSize, entryRVA, moduleName, entryPointer, string(atRVA), pointer(atRVA)
- pr_pe.c: parser PE real (DOS MZ, NT headers, sections, imports, exports, relocs)
- Testes: 76/76 PASS (via C harness)

## 3. PE Process Lifecycle
- pr_peproc.c: pr_peproc_create (valida PE, cria VM, mapeia seções, cria Win32 ctx, log), pr_peproc_prepare (resolve IAT → thunks stdcall, proteções), pr_peproc_step (budget instruções, executa CPU IA-32/x64 mínimo, dispatch Win32, verifica halt), pr_peproc_state (STOP_NONE/EXIT/etc), pr_peproc_surface (framebuffer), pr_peproc_win32, pr_peproc_log, pr_peproc_diagnostic, pr_peproc_exit_code, pr_peproc_set_fs_root, pr_peproc_destroy
- WindowsPEBackend.swift: load (Data, PELoader, pr_peproc_create), initialize (Win32Catalog.coverage, env vars, cwd, fs_root, cmdline, pr_peproc_prepare), run/pause/resume/stop/shutdown, stepFrame (pr_peproc_step), consumeGraphicsFrame (pr_peproc_surface → GfxSurfaceBuffer), drainLogs
- Estados: idle→loaded→initialized→running→paused→stopped→failed

## 4. VFS
- pr_vfs.c? Não existe arquivo dedicado, mas pr_win32.c tem filesystem virtual com fs_root único = diretório sandbox da sessão, normalização Windows→host, negação ".." e fugas com log
- AppSandbox.swift: root = Application Support/Portico, documentsWinOS = Documents/WinOS, cachesWinOS = Caches/WinOS, gamesDir, environmentsDir, logsDir, importsDir, etc, ensureDirectories, isInsideSandbox, resolveInside (defesa path traversal, bloqueia ".."), listDirectory, deleteItem, moveItem, importFrom (security-scoped URLs iOS), directorySize, availableSpaceBytes
- VFS real via sandbox + Win32 fs_root

## 5. Sandbox
- AppSandbox.standard() = Application Support/Portico
- Estrutura Documents/WinOS/PCs, Library, Imports, Logs + Caches/WinOS + AppSupport Runtime/VFS
- ensureDirectories cria todos
- isInsideSandbox verifica prefixo
- resolveInside limpa "\\" → "/", trim "/", bloqueia "..", verifica inside

## 6. Filesystem
- Via AppSandbox + pr_win32 fs_root + pr_peproc_set_fs_root
- Win32 APIs: CreateFileA/W, ReadFile/WriteFile, FindFirst/Next/Close, GetFileAttributes, Create/RemoveDirectory, DeleteFile, MoveFile/CopyFile, GetFullPathName, GetTempPath, etc — todas com VFS real
- Normalização: Windows path → host path dentro fs_root, "/" "." ".." handling, case handling (iOS case-sensitive, Windows case-insensitive parcialmente)
- Path traversal fora do sandbox: NEGADO com log

## 7. Process/Thread Subsystem
- ProcessManager.swift: spawn, markRunning/Finished/Failed, reap
- pr_cpu.c: CPU IA-32 interpretada, 8 MiB flat, ESP 0x00700000, 2M budget por frame
- pr_cpu64.c: x64 mínimo straight-line (movs/ALU/call [rip]/ret/INT/hlt), instrução fora → EXECUTION STOPPED com opcode/RIP
- pr_win32.c: threads via CreateThread, ExitThread, Suspend/Resume, TlsAlloc/Set/Get/Free, critical sections, SRWLock, mutex, semaphore, event
- Process: PID via GetCurrentProcessId, parent PID não implementado completamente, command line, working directory, environment, exit code, windows, threads, memory stats

## 8. Handles
- pr_win32.c: handle table, tipos: file, event, mutex, semaphore, thread, window, DC, brush, etc
- CloseHandle, DuplicateHandle parcial
- WaitForSingleObject/MultipleObjects

## 9. Memory Manager
- pr_vm.c: pr_vm_map/unmap/protect/alloc/read/write/loader_write/find_gap
- VirtualAlloc/Free/Query/Protect, HeapAlloc/Free/ReAlloc/Size, Local/GlobalAlloc/Free/ReAlloc/Size
- pr_cpu.c: flat memory 8 MiB

## 10. Synchronization
- CreateEventA/W, SetEvent/ResetEvent, CreateMutexA/W, ReleaseMutex, CreateSemaphoreA/W, ReleaseSemaphore, InitializeSRWLock/Acquire/Release, Enter/LeaveCriticalSection, Initialize/DeleteCriticalSection
- WaitForSingleObject/MultipleObjects

## 11. Environment Variables
- EnvironmentManager.swift: environments.json, create/update/destroy, mergedVariables, markInUse, sizeBytes
- EnvironmentModels.swift: EnvironmentProfile, EnvironmentRef, defaultVariables
- pr_win32: GetEnvironmentVariableA/W, Set, GetCommandLineA/W, GetStartupInfoA

## 12. Loader/DLL Subsystem
- LoadLibraryA/W, FreeLibrary, GetModuleHandleA/W, GetProcAddress, pr_peproc_provide_dll/load_dll/free_dll/call
- PELoader para imports, Win32Catalog para coverage

## 13. Graphics
- GraphicsBackend.swift: GfxFrame, GfxColor, GfxRect, GfxVertex, GfxCommand (clear, viewport, scissor, drawTriangles, present, filter)
- SurfaceBridge.swift: XRGB8888 → BGRA8 codec, MetalFrameUpload staging
- GfxSurfaceBuffer: reutilizado, update(width,height,copyFrom)
- pr_gfx.c: pr_gfx_stream, pr_gfx_push, pr_gfx_cmd_clear/viewport/draw/present, pr_gfx_set_surface
- pr_surf.c: pr_surf, pr_surf_width/height/pixels, pr_surf_present
- pr_gl.c: OpenGL subset via pr_gl.h

## 14. Metal
- MTKGameView.swift: MTKView wrapper, renderScale, attach renderer
- MetalGameRenderer.swift: MTLDevice, CAMetalLayer, drawable, render target, command buffer/queue, render pass, pipeline state, texture upload, framebuffer, presentation, vertex buffer, index buffer, setVSyncLimit, execute(frame)
- SurfaceBridge: XRGB8888 → BGRA8 para Metal
- Diagnostics: MTLCreateSystemDefaultDevice, supportsFamily Apple4/3/1/2

## 15. Software Renderer
- Fallback? Não implementado dedicado, mas pr_gfx pode renderizar via CPU e SurfaceBridge converte para BGRA8, então Metal é primário e software seria via CPU surface
- Necessário criar SoftwareRenderer como fallback quando Metal indisponível

## 16. Input
- InputCore.swift: InputState (buttons, moveX/Y, lookX/Y, triggerLT/RT)
- TouchInputAdapter.swift: Touch → InputState
- GameControllerAdapter.swift: GCController → InputState
- GameControllerBridge.swift: attach/detach, router
- VirtualControlsView.swift: layout touch controls
- pr_input.c: pr_input, pr_win32_input_key/char/mouse/touch/controller/advance_time
- Win32 message queue: GetMessage/PeekMessage já tem fila real+WM_TIMER

## 17. Audio
- AudioCore.swift: AudioMixState
- AVAudioEngineBackend.swift: initialize, start/stop, enqueue(interleaved), setMix, sampleRate, bufferFrames
- pr_audio.c: pr_audio ring, pr_audio_push/pull
- WindowsPEBackend pullAudio retorna [] (waveOut não implementado) — honesto
- PXPInterpreterBackend pullAudio via pr_host_audio

## 18. Swift ↔ C Bridge
- PorticoRuntime.h: umbrella header
- pr_types.h: pr_status enum (OK, INVALID, NOMEM, IO, FORMAT, UNSUPPORTED, STATE, FAULT, RANGE)
- pr_host.h: pr_host, pr_host_backend_v1, pr_host_start_info, pr_host_frame_in/out, pr_host_register_backend, pr_host_create/start/frame/stop/destroy, pr_selftest_payload_build
- pr_pe.h: pr_pe_info, pr_pe_scan, pr_pe_import_name, pr_pe_sections, pr_pe_load, pr_pe_diagnose, pr_pe_exports, etc
- pr_peproc.h: pr_peproc, pr_peproc_create/prepare/step/state/surface/win32/log/diagnostic/exit_code/set_fs_root/destroy, pr_peproc_present/provide_dll/load_dll/free_dll/call
- pr_win32.h: pr_win32_ctx, pr_win32_create/destroy/call, pr_win32_lookup, pr_win32_modules, pr_win32_call_entry, pr_win32_set_fs_root/cwd/cmdline/env_set, pr_win32_last_error/halted/exit_code/calls_implemented/unsupported, pr_win32_scratch/bind_image/bind_vm/bind_thunks, pr_win32_stdout_read, pr_win32_input_*, pr_win32_advance_time
- ExecutionBackend.swift: PXPInterpreterBackend (pr_host) e WindowsPEBackend (pr_peproc)
- RuntimeManager.swift: coordena sandbox, backends, processManager, FramePacer, events

## 19. Reutilização

Não recriar:
- PELoader, PEInspector, PELoadedImage, pr_pe, pr_peproc
- AppSandbox, VFS via fs_root
- pr_win32 com 100+ APIs já implementadas
- pr_cpu, pr_cpu64, pr_vm
- GraphicsBackend, SurfaceBridge, MetalGameRenderer, MTKGameView
- InputCore, TouchInputAdapter, GameControllerBridge
- AVAudioEngineBackend, pr_audio
- RuntimeManager, BackendRegistry, ProcessManager

Criar novo (real, não mock):
- WindowManager central
- FileManager real usando VFS/AppSandbox
- ProcessManager estendido com PID/exe/cmdline/wd/env/state/exit/windows/threads/mem stats
- RenderEngine com Metal primário + Software fallback + Compositor + dirty rectangles + triple buffering + frame pacing
- InputBridge mapeando Touch → Desktop coords → WindowManager hit testing → Win32 message
- Taskbar funcional com apps reais
- StartMenu funcional
- Shell sobre runtime
- Diagnostics expandido com FPS/frame time/CPU/GPU/draw calls/texture count/memory/surfaces/windows/processes

## 20. Status atual

- C: 3411/3411 PASS
- PE: 76/76 PASS
- Swift: compilava antes, agora com fixes ExecutionBackend PEReport e WinOSDiagnosticsView interpolation → deve compilar
- Metal: READY (MTLDevice OK)
- Desktop: PARTIAL (WinOSDesktopView existe mas era visual, não WindowManager real)
- File Manager: NOT IMPLEMENTED (real)
- Window Manager: NOT IMPLEMENTED (real)
- Process Manager: PARTIAL (ProcessManager existe básico)
- VFS: READY (AppSandbox + fs_root)
- Win32: PARTIAL (100+ APIs IMPLEMENTED, muitas PARTIAL, muitas UNIMPLEMENTED)
- iPhone: UNVERIFIED (sem teste físico)
- GTA V: NOT TESTED
- MX Bikes: NOT TESTED
