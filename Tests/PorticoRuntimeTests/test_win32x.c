/* Testes unitários das APIs Win32 expandidas (FASE 3):
 * GetModuleHandleW/GetCommandLineW, GetEnvironmentVariableA/W,
 * GetSystemInfo, VirtualProtect, GetCurrentDirectoryA,
 * DeleteDC/SetPixel/GetPixel, StretchBlt (SRCCOPY vizinho mais próximo). */
#include "pt_util.h"

#include "portico/pr_win32.h"
#include "portico/pr_surf.h"
#include "portico/pr_log.h"

#include <stdlib.h>
#include <string.h>

void test_win32x(void) {
    pr_log* log = pr_log_create(256);
    pr_win32_ctx* ctx = pr_win32_create(log);
    CHECK(ctx != NULL);
    uint64_t args[12], ret = 0;
    uint8_t* scratch = pr_win32_scratch(ctx, NULL);
    CHECK(scratch != NULL);

    /* ---- GetModuleHandleW (UTF-16 → lookup honesto) ---- */
    {
        const char* nm = "gdi32.dll";
        for (size_t i = 0; i <= strlen(nm); i++) {
            scratch[2 * i] = (uint8_t)nm[i];
            scratch[2 * i + 1] = 0;
        }
        args[0] = (uint64_t)(uintptr_t)scratch;
        CHECK(pr_win32_call(ctx, "kernel32.dll", "GetModuleHandleW", args, 1, &ret) == PR_OK);
        CHECK(ret != 0);
        /* módulo desconhecido → 0 + MOD_NOT_FOUND (nunca inventa) */
        scratch[0] = 'n'; scratch[2] = 'a'; scratch[4] = 'd'; scratch[6] = 'a';
        scratch[8] = '.'; scratch[10] = 'd'; scratch[12] = 'l'; scratch[14] = 'l';
        scratch[16] = 0; scratch[17] = 0;
        args[0] = (uint64_t)(uintptr_t)scratch;
        ret = 0xDEAD;
        CHECK(pr_win32_call(ctx, "kernel32.dll", "GetModuleHandleW", args, 1, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 0);
        CHECK_EQ_U32(pr_win32_last_error(ctx), 126);
    }

    /* ---- GetCommandLineW (UTF-16 do scratch) ---- */
    {
        CHECK(pr_win32_set_cmdline(ctx, "portico") == PR_OK); /* estado conhecido */
        uint64_t cmdw = 0;
        CHECK(pr_win32_call(ctx, "kernel32.dll", "GetCommandLineW", NULL, 0, &cmdw) == PR_OK);
        CHECK(cmdw != 0);
        const uint8_t* w = (const uint8_t*)pr_win32_ptr(ctx, cmdw, 2);
        CHECK(w != NULL && w[0] == 'p' && w[1] == 0); /* "portico" */
    }

    /* ---- GetEnvironmentVariableA/W ---- */
    {
        CHECK(pr_win32_env_set(ctx, "FASE3_VAR", "valor7") == PR_OK);
        memcpy(scratch + 64, "FASE3_VAR", 10);
        args[0] = (uint64_t)(uintptr_t)(scratch + 64);
        args[1] = (uint64_t)(uintptr_t)(scratch + 128);
        args[2] = 64;
        CHECK(pr_win32_call(ctx, "kernel32.dll", "GetEnvironmentVariableA", args, 3, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 6); /* strlen("valor7") */
        CHECK(memcmp(scratch + 128, "valor7", 7) == 0);
        /* buffer pequeno → tamanho necessário + INSUFFICIENT_BUFFER */
        args[2] = 3;
        CHECK(pr_win32_call(ctx, "kernel32.dll", "GetEnvironmentVariableA", args, 3, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 7);
        CHECK_EQ_U32(pr_win32_last_error(ctx), 122);
        /* variável ausente → 0 + ENVVAR_NOT_FOUND */
        memcpy(scratch + 64, "NAO_EXISTE", 11);
        args[0] = (uint64_t)(uintptr_t)(scratch + 64);
        args[1] = (uint64_t)(uintptr_t)(scratch + 128);
        args[2] = 64;
        ret = 0xDEAD;
        CHECK(pr_win32_call(ctx, "kernel32.dll", "GetEnvironmentVariableA", args, 3, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 0);
        CHECK_EQ_U32(pr_win32_last_error(ctx), 203);
        /* variante W: nome UTF-16 + valor UTF-16 */
        for (size_t i = 0; i < 10; i++) {
            scratch[256 + 2 * i] = (uint8_t)"FASE3_VAR"[i];
            scratch[256 + 2 * i + 1] = 0;
        }
        scratch[256 + 20] = 0; scratch[256 + 21] = 0;
        args[0] = (uint64_t)(uintptr_t)(scratch + 256);
        args[1] = (uint64_t)(uintptr_t)(scratch + 320);
        args[2] = 32;
        CHECK(pr_win32_call(ctx, "kernel32.dll", "GetEnvironmentVariableW", args, 3, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 6);
        CHECK(scratch[320] == 'v' && scratch[321] == 0); /* UTF-16 'v' */
    }

    /* ---- GetSystemInfo (valores honestos do Portico) ---- */
    {
        args[0] = (uint64_t)(uintptr_t)(scratch + 64);
        CHECK(pr_win32_call(ctx, "kernel32.dll", "GetSystemInfo", args, 1, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 1);
        uint16_t arch = 0; uint32_t page = 0, nproc = 0, gran = 0;
        memcpy(&arch, scratch + 64, 2);
        memcpy(&page, scratch + 68, 4);
        memcpy(&nproc, scratch + 96, 4);
        memcpy(&gran, scratch + 104, 4);
        CHECK_EQ_U32(arch, 9);        /* AMD64 */
        CHECK_EQ_U32(page, 4096);
        CHECK_EQ_U32(nproc, 1);
        CHECK_EQ_U32(gran, 65536);
    }

    /* ---- GetCurrentDirectoryA ---- */
    {
        CHECK(pr_win32_set_cwd(ctx, "C:\\portico") == PR_OK);
        args[0] = 32;
        args[1] = (uint64_t)(uintptr_t)(scratch + 64);
        CHECK(pr_win32_call(ctx, "kernel32.dll", "GetCurrentDirectoryA", args, 2, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 10); /* strlen */
        CHECK(memcmp(scratch + 64, "C:\\portico", 11) == 0);
    }

    /* ---- VirtualProtect (blocos host: HeapAlloc) ---- */
    {
        uint64_t heap = 0;
        CHECK(pr_win32_call(ctx, "kernel32.dll", "GetProcessHeap", NULL, 0, &heap) == PR_OK);
        args[0] = heap; args[1] = 0; args[2] = 64;
        CHECK(pr_win32_call(ctx, "kernel32.dll", "HeapAlloc", args, 3, &ret) == PR_OK);
        CHECK(ret != 0);
        uint64_t blk = ret;
        args[0] = blk; args[1] = 64; args[2] = 0x40; /* PAGE_EXECUTE_READWRITE */
        args[3] = (uint64_t)(uintptr_t)(scratch + 64);
        CHECK(pr_win32_call(ctx, "kernel32.dll", "VirtualProtect", args, 4, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 1);
        uint32_t old = 0;
        memcpy(&old, scratch + 64, 4);
        CHECK_EQ_U32(old, 0x04); /* PAGE_READWRITE (estado anterior real) */
        /* endereço fora de blocos → recusa honesta */
        args[0] = 0x1234;
        ret = 0xDEAD;
        CHECK(pr_win32_call(ctx, "kernel32.dll", "VirtualProtect", args, 4, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 0);
    }

    /* ---- DeleteDC ---- */
    {
        args[0] = 0;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleDC", args, 1, &ret) == PR_OK);
        uint32_t hdc = (uint32_t)ret;
        CHECK(hdc != 0);
        args[0] = hdc;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "DeleteDC", args, 1, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 1);
        /* duas vezes → handle inválido (não finge) */
        ret = 0xDEAD;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "DeleteDC", args, 1, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 0);
        CHECK_EQ_U32(pr_win32_last_error(ctx), 6);
    }

    /* ---- SetPixel/GetPixel (COLORREF <-> XRGB) ---- */
    {
        args[0] = 0;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleDC", args, 1, &ret) == PR_OK);
        uint32_t hdc = (uint32_t)ret;
        args[0] = hdc; args[1] = 8; args[2] = 4;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleBitmap", args, 3, &ret) == PR_OK);
        uint32_t hbm = (uint32_t)ret;
        args[0] = hdc; args[1] = hbm;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "SelectObject", args, 2, &ret) == PR_OK);
        /* COLORREF vermelho = 0x000000FF → XRGB 0x00FF0000 */
        args[0] = hdc; args[1] = 2; args[2] = 3; args[3] = 0x000000FFu;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "SetPixel", args, 4, &ret) == PR_OK);
        pr_surf* bmp = pr_win32_object_surface(ctx, hbm);
        CHECK(bmp != NULL);
        if (bmp) {
            const uint32_t* px = pr_surf_pixels(bmp);
            CHECK_EQ_U32(px[3 * (pr_surf_pitch(bmp) / 4) + 2], 0x00FF0000);
        }
        args[1] = 2; args[2] = 3;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "GetPixel", args, 3, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 0x000000FFu); /* volta COLORREF */
        /* fora dos limites → CLR_INVALID */
        args[1] = 99;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "GetPixel", args, 3, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 0xFFFFFFFFu);
    }

    /* ---- StretchBlt: 2x com vizinho mais próximo + rop guardado ---- */
    {
        args[0] = 0;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleDC", args, 1, &ret) == PR_OK);
        uint32_t hdcs = (uint32_t)ret;
        args[0] = 0;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleDC", args, 1, &ret) == PR_OK);
        uint32_t hdcd = (uint32_t)ret;
        args[0] = hdcs; args[1] = 2; args[2] = 1;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleBitmap", args, 3, &ret) == PR_OK);
        uint32_t bs = (uint32_t)ret;
        args[0] = hdcd; args[1] = 4; args[2] = 2;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleBitmap", args, 3, &ret) == PR_OK);
        uint32_t bd = (uint32_t)ret;
        args[0] = hdcs; args[1] = bs;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "SelectObject", args, 2, &ret) == PR_OK);
        args[0] = hdcd; args[1] = bd;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "SelectObject", args, 2, &ret) == PR_OK);
        /* src 2x1: esq vermelho (COLORREF 0x000000FF), dir azul (0x00FF0000) */
        args[0] = hdcs; args[1] = 0; args[2] = 0; args[3] = 0x000000FFu;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "SetPixel", args, 4, &ret) == PR_OK);
        args[1] = 1; args[3] = 0x00FF0000u;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "SetPixel", args, 4, &ret) == PR_OK);
        /* StretchBlt(dst, 0,0, 4,2, src, 0,0, 2,1, SRCCOPY) */
        args[0] = hdcd; args[1] = 0; args[2] = 0; args[3] = 4; args[4] = 2;
        args[5] = hdcs; args[6] = 0; args[7] = 0; args[8] = 2; args[9] = 1;
        args[10] = 0x00CC0020u;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "StretchBlt", args, 11, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 1);
        pr_surf* d = pr_win32_object_surface(ctx, bd);
        CHECK(d != NULL);
        if (d) {
            const uint32_t* px = pr_surf_pixels(d);
            size_t pitch = pr_surf_pitch(d) / 4;
            CHECK_EQ_U32(px[0 * pitch + 0], 0x00FF0000); /* XRGB vermelho */
            CHECK_EQ_U32(px[0 * pitch + 1], 0x00FF0000); /* vizinho mais próximo */
            CHECK_EQ_U32(px[0 * pitch + 2], 0x000000FF);
            CHECK_EQ_U32(px[1 * pitch + 3], 0x000000FF);
        }
        /* rop desconhecido → recusa honesta */
        args[10] = 0x12345678u;
        ret = 0xDEAD;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "StretchBlt", args, 11, &ret) == PR_OK);
        CHECK_EQ_U32(ret, 0);
        /* log estruturado do FASE 6 */
        pr_log_entry es[256];
        size_t n = pr_log_read(log, es, 256, 0);
        int saw_stretch = 0;
        for (size_t i = 0; i < n; i++)
            if (strstr(es[i].msg, "[GDI] StretchBlt")) saw_stretch = 1;
        CHECK(saw_stretch);
    }

    pr_win32_destroy(ctx);
    pr_log_destroy(log);
}
