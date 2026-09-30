/* pr_d3d.c — D3D11/D3D12/DXGI stubs honestos para diagnóstico de jogo real
 * Implementação: NÃO implementado, retorna UNSUPPORTED com log detalhado.
 * Isso permite que WinOSGameCompatibility detecte uso de D3D e recuse honestamente
 * em vez de falhar silenciosamente. FASE 7 — Graphics translation requer implementação futura.
 */

#include "portico/pr_types.h"
#include "portico/pr_log.h"
#include "portico/pr_win32.h"
#include <stdio.h>
#include <string.h>

/* Stub helper que loga UNIMPLEMENTED e retorna 0 / NULL / E_NOTIMPL conforme API */

pr_status pr_d3d_log_unimplemented(pr_log* log, const char* dll, const char* func) {
    if (log) {
        pr_log_write(log, PR_LOG_WARN, "GRAPHICS", "%s!%s UNIMPLEMENTED — requires %s → Metal translation (FASE 7)", dll, func, dll);
    }
    return PR_ERR_UNSUPPORTED;
}

/* D3D11 stubs — para diagnóstico, não para execução real */

const char* pr_d3d11_diagnostic(void) {
    return "D3D11 não implementado — BLOQUEIO ARQUITETURAL. Jogo usa D3D11 (d3d11.dll) que requer: ID3D11Device creation, IDXGISwapChain, shaders HLSL→MSL, textures, buffers, render targets, command lists. Para suportar: implementar pr_d3d11_device_create + DXGI swapchain + shader translation HLSL→MSL via Metal. Atualmente apenas OpenGL 1.1 via pr_gl.c é suportado.";
}

const char* pr_d3d12_diagnostic(void) {
    return "D3D12 não implementado — BLOQUEIO ARQUITETURAL. Jogo usa D3D12 (d3d12.dll) que requer: ID3D12Device, command queues, command lists, pipeline state, root signatures, descriptors. Mais complexo que D3D11. Para suportar: implementar D3D12 → Metal translation completa. Atualmente apenas OpenGL 1.1 via pr_gl.c é suportado.";
}

const char* pr_dxgi_diagnostic(void) {
    return "DXGI não implementado — requerido para D3D11/D3D12 swapchain. Jogo usa dxgi.dll (CreateDXGIFactory, IDXGISwapChain). Para suportar: implementar DXGI factory + swapchain que cria CAMetalLayer drawable para present. Atualmente apenas wglSwapBuffers via pr_gl.c é suportado.";
}

const char* pr_d3d9_diagnostic(void) {
    return "D3D9 não implementado — legado. Jogo usa d3d9.dll (Direct3DCreate9, IDirect3DDevice9). Para suportar: implementar D3D9 → Metal translation ou D3D9 → OpenGL 1.1 → Metal via pr_gl.c. Atualmente apenas OpenGL 1.1 é suportado.";
}

/* Estrutura para expor ao Swift via C bridge */

typedef struct pr_d3d_caps {
    int d3d9_supported;
    int d3d10_supported;
    int d3d11_supported;
    int d3d12_supported;
    int dxgi_supported;
    int vulkan_supported;
    int opengl11_supported;
    int opengl_modern_supported;
    char diagnostic[512];
} pr_d3d_caps;

void pr_d3d_get_caps(pr_d3d_caps* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->d3d9_supported = 0;
    out->d3d10_supported = 0;
    out->d3d11_supported = 0;
    out->d3d12_supported = 0;
    out->dxgi_supported = 0;
    out->vulkan_supported = 0;
    out->opengl11_supported = 1; // pr_gl.c
    out->opengl_modern_supported = 0;
    snprintf(out->diagnostic, sizeof(out->diagnostic),
        "Graphics: OpenGL11=YES via pr_gl.c (49 APIs) + Metal present, GDI=YES, D3D9/10/11/12=NO (requires FASE 7), DXGI=NO, Vulkan=NO, GL modern=NO");
}
