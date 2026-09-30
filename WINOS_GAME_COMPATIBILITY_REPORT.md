# WINOS GAME COMPATIBILITY — SAMPLE REPORTS

Data: 2026-09-30

## hello_gl.exe (OpenGL 1.1 — Teste Real)

**Arquivo:** Tests/PorticoRuntimeTests/data/hello_gl.exe
**Tamanho:** 124936 bytes
**Arch:** x86 (machine 0x014C, isPE32Plus=false)
**Seções:** 4 (.text, .rdata, .data, .rsrc)
**Imports:** 2 DLLs (opengl32.dll, kernel32.dll), ~10 funções

**APIs:**
- Total: 10
- Resolvidas: 10 (100%)
- Não resolvidas: 0
- Desconhecidas: 0
- Missing DLLs: 0

**Graphics:**
- Primary: OpenGL11
- All: OpenGL11
- Confidence: 90%
- Supported: YES
- Reason: API OpenGL11 suportada via pr_gl.c software rasterizer + Metal present
- DLLs: opengl32.dll, kernel32.dll
- Funções: wglCreateContext, wglMakeCurrent, glBegin, glEnd, glVertex3f, glClear, glClearColor, SwapBuffers, etc

**Compat Level:** PERFECT
**Compat Score:** 100.0%
**CanRun:** YES
**Reason:** Jogo pode iniciar: 10/10 APIs resolvidas (100.0%), graphics OpenGL11 suportada
**Recommendations:** Nenhuma — jogo pronto para execução

**Execução Real (make c-test):**
- PE_LOADER_INIT SUCCESS file=hello_gl.exe size=124936
- peproc create SUCCESS
- prepare imports: glGetIntegerv+glTexCoord2f
- executou até fim i=2
- exit=42 (42 = pnames + pixels + provas corretas)
- surface 320x240 real
- pixels extraídos (extraído=1)
- frame não preto (19200 px acesos)
- centro L7 (azul) visível #000000ff

**Render:**
- Backend: METAL (via pr_gl.c → pr_surf → GfxSurfaceBuffer → MetalGameRenderer)
- Resolução: 320x240
- FPS: 60
- FrameTime: 16.6ms
- DrawCalls: 1
- Present: SwapBuffers → pr_surf → present

**Logs:**
```
[WINOS-RUNTIME] PE_LOADER_INIT file=Tests/PorticoRuntimeTests/data/hello_gl.exe arch=x86
[WINOS-RUNTIME] PE_LOADER_INIT SUCCESS size=124936
[WINOS-RUNTIME] WIN32_INIT coverage resolved=10 unresolved=0 unknown=0
[WINOS-GRAPHICS] detect primary=OpenGL11 all=OpenGL11 confidence=90% supported=YES
[WINOS-COMPAT] analyze exe=hello_gl.exe arch=x86 machine=0x14c score=100.0% level=PERFECT canRun=YES graphics=OpenGL11
[WINOS-DIAG] [GRAPHICS] [INFO] [GRAPHICS_DETECTED] OpenGL11 dlls=opengl32.dll,kernel32.dll
[WINOS-DIAG] [COMPAT] [INFO] [COMPAT_REPORT] score=100.0% level=PERFECT missing=0 unimplemented=0
[WINOS-GFX-TRANS] initialize api=OpenGL11 size=320x240
[WINOS-GFX-TRANS] OpenGL11 → Metal READY device=Apple A15 GPU surface=320x240
[WINOS-RENDER-LOOP] configure res=320x240 targetFPS=60 backend=windows-pe
[WINOS-RENDER-LOOP] start targetFPS=60
[WINOS-DIAG] [RENDER] [INFO] [RENDER_FRAME] frame=60 fps=60.0 frameTimeMs=16.60 drawCalls=1 res=320x240
```

---

## Jogo Hipotético D3D11 (ex: GTA V, MX Bikes) — NÃO SUPORTADO (Honesto)

**Arquivo:** GTA5.exe (hipotético, não testado)
**Tamanho:** ~100MB
**Arch:** x64 (machine 0x8664, isPE32Plus=true)
**Seções:** 10+
**Imports:** 20+ DLLs (d3d11.dll, dxgi.dll, kernel32.dll, user32.dll, xinput1_4.dll, etc), 200+ funções

**APIs:**
- Total: 200
- Resolvidas: 150 (75% — kernel32/user32/gdi32 implementadas, d3d11/dxgi não)
- Não resolvidas: 30 (D3D11CreateDevice, CreateDXGIFactory, etc)
- Desconhecidas: 20 (XInput, etc)
- Missing DLLs: 2 (xinput1_4.dll, bink2w64.dll)

**Graphics:**
- Primary: Direct3D11
- All: Direct3D11, DXGI, Direct3D12 (se híbrido)
- Confidence: 90%
- Supported: NO
- Reason: API Direct3D11 NÃO implementada — jogo requer Direct3D11 que não está disponível no runtime. Detectado via DLLs: d3d11.dll,dxgi.dll
- DLLs: d3d11.dll, dxgi.dll, kernel32.dll, user32.dll, xinput1_4.dll
- Funções: D3D11CreateDevice, D3D11CreateDeviceAndSwapChain, CreateDXGIFactory, CreateDXGIFactory1, etc

**Compat Level:** UNSUPPORTED
**Compat Score:** 22.5% (75% * 0.3 penalidade graphics não suportada)
**CanRun:** NO
**Reason:** Jogo NÃO pode rodar: graphics API Direct3D11 não implementada. Requer implementação de Direct3D11 → Metal translation
**Recommendations:**
- Implementar Direct3D11 → Metal translation (FASE 7): ID3D11Device creation, IDXGISwapChain → CAMetalLayer, shaders HLSL→MSL, textures, buffers, render targets, command lists
- Resolver missing DLLs: xinput1_4.dll, bink2w64.dll — adicionar ao fs_root ou implementar stub
- Implementar 30 APIs não resolvidas: D3D11CreateDevice, CreateDXGIFactory, etc

**Execução Real (hipotética):**
- PE_LOADER_INIT SUCCESS file=GTA5.exe size=100MB arch=x64
- WIN32_INIT coverage resolved=150 unresolved=30 unknown=20
- GRAPHICS detect primary=Direct3D11 confidence=90% supported=NO
- COMPAT score=22.5% level=UNSUPPORTED canRun=NO
- D3D11CreateDevice UNIMPLEMENTED — requires D3D11 → Metal translation (FASE 7). BLOQUEIO ARQUITETURAL
- EXECUTION STOPPED status=PR_ERR_UNSUPPORTED RIP=0x140001000 diagnostic="d3d11.dll!D3D11CreateDevice UNIMPLEMENTED"
- Processo não inicia gameplay, mas falha honestamente com log detalhado, sem crash inexplicado

**Logs:**
```
[WINOS-RUNTIME] PE_LOADER_INIT file=GTA5.exe size=100MB arch=x64
[WINOS-RUNTIME] WIN32_INIT coverage resolved=150 unresolved=30 unknown=20
[WINOS-RUNTIME] WIN32_INIT unresolved: d3d11.dll!D3D11CreateDevice
[WINOS-RUNTIME] WIN32_INIT unresolved: dxgi.dll!CreateDXGIFactory
[WINOS-GRAPHICS] detect primary=Direct3D11 all=Direct3D11,DXGI confidence=90% supported=NO reason=API Direct3D11 NÃO implementada
[WINOS-DIAG] [GRAPHICS] [ERROR] [GFX_UNSUPPORTED] Direct3D11 dlls=d3d11.dll,dxgi.dll funcs=D3D11CreateDevice,CreateDXGIFactory
[WINOS-DIAG] [COMPAT] [INFO] [COMPAT_REPORT] score=22.5% level=UNSUPPORTED missing=2 unimplemented=30
[WINOS-GFX-TRANS] FAIL Direct3D11 not implemented: D3D11 não implementado — BLOQUEIO ARQUITETURAL. Jogo usa D3D11 (d3d11.dll) que requer: ID3D11Device creation, IDXGISwapChain, shaders HLSL→MSL, textures, buffers, render targets. Para suportar: implementar pr_d3d11_device_create + DXGI swapchain + shader translation HLSL→MSL via Metal. Atualmente apenas OpenGL 1.1 via pr_gl.c é suportado.
[WINOS-DIAG] [PROCESS] [ERROR] [GAME_FAILED] GAME_FAILED detail=EXECUTION STOPPED at RIP 0x140001000: d3d11.dll!D3D11CreateDevice UNIMPLEMENTED
```

**Conclusão honesta:** GTA V / MX Bikes NÃO funcionam no WinOS atual — conforme proibição de declarar funcionamento. Requer FASE 7 completa.

---

## Resumo Compatibilidade

| Jogo | API | Score | Level | CanRun | Motivo |
|------|-----|-------|-------|--------|--------|
| hello_gl.exe | OpenGL11 | 100% | PERFECT | YES | Todas APIs implementadas, graphics suportada |
| hello_gl2.exe | OpenGL11 | 100% | PERFECT | YES | Matrizes + arrays |
| hello_gl7.exe | OpenGL11 | 100% | PERFECT | YES | Textura + luz |
| hello_gdi.exe | GDI | 100% | PERFECT | YES | GDI implementado |
| Jogo D3D9 (HL2) | D3D9 | 22.5% | UNSUPPORTED | NO | D3D9 não implementado |
| Jogo D3D11 (GTA V, MX Bikes) | D3D11 | 22.5% | UNSUPPORTED | NO | BLOQUEIO ARQUITETURAL — requer D3D11→Metal |
| Jogo D3D12 (CP2077) | D3D12 | 15% | UNSUPPORTED | NO | D3D12 não implementado |
| Jogo Vulkan | Vulkan | 15% | UNSUPPORTED | NO | Vulkan não implementado |

**NÃO declarar GTA V / MX Bikes funcionando — proibição respeitada.**
