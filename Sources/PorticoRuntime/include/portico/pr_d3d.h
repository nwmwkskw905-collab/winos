#ifndef PR_D3D_H
#define PR_D3D_H

#include "pr_types.h"
#include "pr_log.h"

#ifdef __cplusplus
extern "C" {
#endif

pr_status pr_d3d_log_unimplemented(pr_log* log, const char* dll, const char* func);

const char* pr_d3d11_diagnostic(void);
const char* pr_d3d12_diagnostic(void);
const char* pr_dxgi_diagnostic(void);
const char* pr_d3d9_diagnostic(void);

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

void pr_d3d_get_caps(pr_d3d_caps* out);

#ifdef __cplusplus
}
#endif

#endif /* PR_D3D_H */
