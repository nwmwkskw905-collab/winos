#define _POSIX_C_SOURCE 200809L
/* GRUPO 6 — FASE 1/2/4: camada gráfica mínima.
 * - FASE 1: superfície BGRA8 (XRGB8888 documentado) com stride, clipping e
 *   limites em SetPixel/GetPixel/FillRect/StretchBlt.
 * - FASE 2: janelas/mensagens reais (RegisterClass/CreateWindow/ShowWindow/
 *   GetDC/BeginPaint/EndPaint/UpdateWindow/DestroyWindow/PostQuitMessage/
 *   GetMessage/DispatchMessage) sobre a superfície interna.
 * - FASE 4: PE gráfico REAL hello_gdi.exe pelo caminho completo do app. */
#include "pt_util.h"

#include "portico/pr_win32.h"
#include "portico/pr_surf.h"
#include "portico/pr_peproc.h"
#include "portico/pr_log.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SRCCOPY 0x00CC0020u

/* mensagens Win32 (espelham o motor) */
#define WM_CREATE     0x0001u
#define WM_DESTROY    0x0002u
#define WM_SIZE       0x0005u
#define WM_PAINT      0x000Fu
#define WM_CLOSE      0x0010u
#define WM_QUIT       0x0012u
#define WM_ERASEBKGND 0x0014u
#define WM_SHOWWINDOW 0x0018u

static uint32_t px_at(pr_surf* s, uint32_t x, uint32_t y) {
    const uint32_t* p = pr_surf_pixels(s);
    return p[y * (pr_surf_pitch(s) / 4) + x];
}

static uint8_t* gfx_load(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) { fclose(f); return NULL; }
    uint8_t* buf = (uint8_t*)malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f); free(buf); return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

static int log_has(const pr_log* log, const char* needle) {
    pr_log_entry es[256];
    size_t n = pr_log_read(log, es, 256, 0);
    for (size_t i = 0; i < n; i++)
        if (strstr(es[i].msg, needle)) return 1;
    return 0;
}

/* ---- WndProc de teste via o SEAM real de reentrada (guest_call) ---- */
typedef struct {
    pr_win32_ctx* ctx;
    uint8_t* scratch;   /* para o PAINTSTRUCT do handler */
    int n_create, n_size, n_show, n_paint, n_erase, n_destroy;
    uint32_t last_msg;
} wnd_rec;

static uint64_t test_wndproc(void* ud, uint64_t fn,
                             const uint64_t args[4], uint64_t* out) {
    (void)fn;
    wnd_rec* r = (wnd_rec*)ud;
    uint32_t msg = (uint32_t)args[1];
    r->last_msg = msg;
    switch (msg) {
    case WM_CREATE: r->n_create++; break;
    case WM_SIZE:   r->n_size++; break;
    case WM_SHOWWINDOW: r->n_show++; break;
    case WM_ERASEBKGND: r->n_erase++; break;
    case WM_PAINT: {
        /* handler canônico: BeginPaint/EndPaint valida a região (real) */
        r->n_paint++;
        uint64_t a2[2] = { args[0], (uint64_t)(uintptr_t)(r->scratch + 184) };
        uint64_t ret = 0;
        pr_win32_call(r->ctx, "user32.dll", "BeginPaint", a2, 2, &ret);
        pr_win32_call(r->ctx, "user32.dll", "EndPaint", a2, 2, &ret);
        break;
    }
    case WM_DESTROY:
        r->n_destroy++;
        { uint64_t a2[1] = { 42 }; uint64_t ret = 0;
          pr_win32_call(r->ctx, "user32.dll", "PostQuitMessage", a2, 1, &ret); }
        break;
    default: break;
    }
    *out = 0;
    return 1;
}

void test_gdiwin(void) {
    /* ================= FASE 1: superfície BGRA8 (stride/limites) ================= */
    {
        pr_log* log = pr_log_create(256);
        pr_win32_ctx* ctx = pr_win32_create(log);
        CHECK(ctx != NULL);
        uint64_t args[12], ret = 0;
        size_t scratch_sz = 0;
        uint8_t* scratch = pr_win32_scratch(ctx, &scratch_sz);
        CHECK(scratch != NULL && scratch_sz >= 256);

        /* bitmap 8×4: formato/stride documentados (XRGB8888, pitch = w*4) */
        args[0] = 0;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleDC", args, 1, &ret) == PR_OK);
        uint32_t hdc = (uint32_t)ret;
        args[0] = hdc; args[1] = 8; args[2] = 4;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleBitmap", args, 3, &ret) == PR_OK);
        uint32_t hbm = (uint32_t)ret;
        pr_surf* bmp = pr_win32_object_surface(ctx, hbm);
        CHECK(bmp != NULL);
        CHECK_EQ_U32((uint32_t)pr_surf_format_of(bmp), PR_SURF_XRGB8888);
        CHECK_EQ_U32((uint32_t)pr_surf_pitch(bmp), 32);   /* stride = 8*4 */
        args[0] = hdc; args[1] = hbm;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "SelectObject", args, 2, &ret) == PR_OK);

        /* SetPixel/GetPixel: roundtrip COLORREF na superfície */
        args[0] = hdc; args[1] = 3; args[2] = 2; args[3] = 0x00123456;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "SetPixel", args, 4, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)px_at(bmp, 3, 2), 0x00563412); /* XRGB = 0x00RRGGBB */
        args[0] = hdc; args[1] = 3; args[2] = 2;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "GetPixel", args, 3, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0x00123456);              /* COLORREF de volta */

        /* limites: fora da superfície = CLR_INVALID (0xFFFFFFFF), nada muda */
        args[0] = hdc; args[1] = (uint64_t)(uint32_t)-1; args[2] = 0; args[3] = 0x00FFFFFF;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "SetPixel", args, 4, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0xFFFFFFFFu);
        args[0] = hdc; args[1] = 8; args[2] = 0; args[3] = 0x00FFFFFF;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "SetPixel", args, 4, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0xFFFFFFFFu);
        args[0] = hdc; args[1] = 0; args[2] = 4; args[3] = 0x00FFFFFF;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "SetPixel", args, 4, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0xFFFFFFFFu);
        args[0] = hdc; args[1] = 9; args[2] = 9;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "GetPixel", args, 3, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0xFFFFFFFFu);
        CHECK_EQ_U32((uint32_t)px_at(bmp, 3, 2), 0x00563412); /* intacto */

        /* FillRect: completo, com clipping e com RECT invertido (no-op) */
        args[0] = 0x00FFFFFF;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateSolidBrush", args, 1, &ret) == PR_OK);
        uint32_t br = (uint32_t)ret;
        int32_t rc_full[4] = { 0, 0, 8, 4 };
        memcpy(scratch + 0, rc_full, 16);
        args[0] = hdc; args[1] = (uint64_t)(uintptr_t)(scratch + 0); args[2] = br;
        CHECK(pr_win32_call(ctx, "user32.dll", "FillRect", args, 3, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 1);
        CHECK_EQ_U32((uint32_t)px_at(bmp, 3, 2), 0x00FFFFFF); /* sobrescrito */
        int32_t rc_clip[4] = { 6, 2, 20, 20 };                 /* clipa em 6..7×2..3 */
        memcpy(scratch + 16, rc_clip, 16);
        args[0] = 0x000000FF;   /* vermelho (COLORREF) */
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateSolidBrush", args, 1, &ret) == PR_OK);
        args[0] = hdc; args[1] = (uint64_t)(uintptr_t)(scratch + 16); args[2] = (uint32_t)ret;
        CHECK(pr_win32_call(ctx, "user32.dll", "FillRect", args, 3, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)px_at(bmp, 6, 2), 0x00FF0000); /* XRGB vermelho */
        CHECK_EQ_U32((uint32_t)px_at(bmp, 7, 3), 0x00FF0000);
        CHECK_EQ_U32((uint32_t)px_at(bmp, 5, 2), 0x00FFFFFF); /* fora do clip */
        CHECK_EQ_U32((uint32_t)px_at(bmp, 6, 1), 0x00FFFFFF);
        int32_t rc_inv[4] = { 4, 2, 2, 3 };                    /* invertido */
        memcpy(scratch + 32, rc_inv, 16);
        args[0] = hdc; args[1] = (uint64_t)(uintptr_t)(scratch + 32); args[2] = br;
        CHECK(pr_win32_call(ctx, "user32.dll", "FillRect", args, 3, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)px_at(bmp, 2, 2), 0x00FFFFFF); /* nada mudou */
        CHECK_EQ_U32((uint32_t)px_at(bmp, 3, 2), 0x00FFFFFF);

        /* memória inválida: RECT fora do espaço → erro honesto 87 */
        args[0] = hdc; args[1] = 0; args[2] = br;
        CHECK(pr_win32_call(ctx, "user32.dll", "FillRect", args, 3, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0);
        CHECK_EQ_U32(pr_win32_last_error(ctx), 87);

        /* StretchBlt SRCCOPY: 4×2 → 8×4 (2×) com vizinho mais próximo */
        args[0] = 0;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleDC", args, 1, &ret) == PR_OK);
        uint32_t hdc2 = (uint32_t)ret;
        args[0] = hdc2; args[1] = 8; args[2] = 4;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleBitmap", args, 3, &ret) == PR_OK);
        uint32_t hbm2 = (uint32_t)ret;
        pr_surf* bmp2 = pr_win32_object_surface(ctx, hbm2);
        args[0] = hdc2; args[1] = hbm2;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "SelectObject", args, 2, &ret) == PR_OK);
        args[0] = hdc2; args[1] = 0; args[2] = 0; args[3] = 8; args[4] = 4;
        args[5] = hdc; args[6] = 0; args[7] = 0; args[8] = 4; args[9] = 2;
        args[10] = SRCCOPY;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "StretchBlt", args, 11, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 1);
        /* 2× horizontal/vertical: dst(2i,2j) = src(i,j) */
        for (uint32_t y = 0; y < 2; y++)
            for (uint32_t x = 0; x < 4; x++) {
                uint32_t s = (uint32_t)px_at(bmp, x, y);
                CHECK_EQ_U32((uint32_t)px_at(bmp2, 2 * x, 2 * y), s);
                CHECK_EQ_U32((uint32_t)px_at(bmp2, 2 * x + 1, 2 * y + 1), s);
            }
        /* pixel conhecido: src(3,1) branco (linha da origem) → dst(6..7, 2..3) */
        CHECK_EQ_U32((uint32_t)px_at(bmp2, 6, 2), 0x00FFFFFF);
        CHECK_EQ_U32((uint32_t)px_at(bmp2, 7, 3), 0x00FFFFFF);

        /* StretchBlt: dimensões inválidas e rop não suportado = recusa honesta */
        args[3] = 0;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "StretchBlt", args, 11, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0);
        args[3] = 8; args[10] = 0x00EE0086u;   /* MERGEPAINT (≠ SRCCOPY) */
        CHECK(pr_win32_call(ctx, "gdi32.dll", "StretchBlt", args, 11, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0);
        /* handle DC inválido */
        args[0] = 0xDEAD0000u; args[10] = SRCCOPY;
        CHECK(pr_win32_call(ctx, "gdi32.dll", "StretchBlt", args, 11, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0);
        CHECK_EQ_U32(pr_win32_last_error(ctx), 6);

        pr_win32_destroy(ctx);
        pr_log_destroy(log);
    }

    /* ================= FASE 2: janelas + fila de mensagens ================= */
    {
        pr_log* log = pr_log_create(256);
        pr_win32_ctx* ctx = pr_win32_create(log);
        CHECK(ctx != NULL);
        wnd_rec rec;
        memset(&rec, 0, sizeof(rec));
        rec.ctx = ctx;
        pr_win32_set_guestcall(ctx, test_wndproc, &rec);
        uint64_t args[12], ret = 0;
        size_t scratch_sz = 0;
        uint8_t* scratch = pr_win32_scratch(ctx, &scratch_sz);
        CHECK(scratch != NULL && scratch_sz >= 256);
        rec.scratch = scratch;

        /* WNDCLASSA x64 (72B): style@0 wndproc@8 ... hbrBackground@48 nome@64 */
        uint8_t* wc = scratch + 0;          /* WNDCLASSA em memória do win32 */
        memset(wc, 0, 72);
        uint64_t wndproc = 0x1234;   /* endereço "guest" opaco */
        uint64_t hbr_bg = 6;         /* (COLOR_WINDOW+1) */
        char* cname = (char*)(scratch + 80);
        memcpy(cname, "TestGdi", 8);
        memcpy(wc + 8, &wndproc, 8);
        memcpy(wc + 48, &hbr_bg, 8);
        uint64_t gname = (uint64_t)(uintptr_t)cname;
        memcpy(wc + 64, &gname, 8);
        args[0] = (uint64_t)(uintptr_t)wc;
        CHECK(pr_win32_call(ctx, "user32.dll", "RegisterClassA", args, 1, &ret) == PR_OK);
        CHECK(ret != 0);   /* ATOM */
        /* duplicata recusada com erro real 1411 */
        args[0] = (uint64_t)(uintptr_t)wc;
        CHECK(pr_win32_call(ctx, "user32.dll", "RegisterClassA", args, 1, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0);
        CHECK_EQ_U32(pr_win32_last_error(ctx), 1411);
        /* memória inválida: lpWndClass nulo */
        args[0] = 0;
        CHECK(pr_win32_call(ctx, "user32.dll", "RegisterClassA", args, 1, &ret) == PR_ERR_INVALID);

        /* CreateWindowExA 32×24 → superfície interna do processo */
        char* title = (char*)(scratch + 96);
        memcpy(title, "Test", 5);
        args[0] = 0;
        args[1] = gname; args[2] = (uint64_t)(uintptr_t)title;
        args[3] = 0;  /* style sem WS_VISIBLE */
        args[4] = 0; args[5] = 0; args[6] = 32; args[7] = 24;
        args[8] = 0; args[9] = 0; args[10] = 0; args[11] = 0;
        CHECK(pr_win32_call(ctx, "user32.dll", "CreateWindowExA", args, 12, &ret) == PR_OK);
        uint32_t hwnd = (uint32_t)ret;
        CHECK(hwnd != 0);
        CHECK_EQ_U32((uint32_t)rec.n_create, 1);   /* WM_CREATE real */
        CHECK_EQ_U32((uint32_t)rec.n_size, 1);     /* WM_SIZE real */
        pr_surf* ws = pr_win32_surface(ctx);
        CHECK(ws != NULL);
        CHECK_EQ_U32(pr_surf_width(ws), 32);
        CHECK_EQ_U32(pr_surf_height(ws), 24);
        CHECK_EQ_U32((uint32_t)pr_surf_pitch(ws), 128);  /* stride = 32*4 */

        /* GetClientRect */
        int32_t* rc = (int32_t*)(scratch + 112);
        rc[0] = rc[1] = rc[2] = rc[3] = 0;
        args[0] = hwnd; args[1] = (uint64_t)(uintptr_t)rc;
        CHECK(pr_win32_call(ctx, "user32.dll", "GetClientRect", args, 2, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 1);
        CHECK(rc[0] == 0 && rc[1] == 0 && rc[2] == 32 && rc[3] == 24);
        args[1] = 0;   /* RECT nulo = memória inválida */
        CHECK(pr_win32_call(ctx, "user32.dll", "GetClientRect", args, 2, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0);
        CHECK_EQ_U32(pr_win32_last_error(ctx), 87);

        /* 2ª janela com dimensões diferentes = recusa honesta (superfície única) */
        args[1] = gname;
        args[6] = 64; args[7] = 48;
        CHECK(pr_win32_call(ctx, "user32.dll", "CreateWindowExA", args, 12, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0);
        CHECK_EQ_U32(pr_win32_last_error(ctx), 87);
        args[6] = 32; args[7] = 24;

        /* ShowWindow (SW_SHOW=5) → WM_SHOWWINDOW + update pendente */
        args[0] = hwnd; args[1] = 5;
        CHECK(pr_win32_call(ctx, "user32.dll", "ShowWindow", args, 2, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0);   /* visibilidade anterior = 0 */
        CHECK_EQ_U32((uint32_t)rec.n_show, 1);

        /* UpdateWindow → WM_ERASEBKGND + WM_PAINT (desenho) */
        args[0] = hwnd;
        CHECK(pr_win32_call(ctx, "user32.dll", "UpdateWindow", args, 1, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 1);
        CHECK_EQ_U32((uint32_t)rec.n_erase, 1);
        CHECK_EQ_U32((uint32_t)rec.n_paint, 1);
        /* sem update pendente → não repinta */
        CHECK(pr_win32_call(ctx, "user32.dll", "UpdateWindow", args, 1, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)rec.n_paint, 1);

        /* múltiplos frames: InvalidateRect + UpdateWindow ×5 */
        for (int i = 0; i < 5; i++) {
            args[0] = hwnd; args[1] = 0; args[2] = 1;
            CHECK(pr_win32_call(ctx, "user32.dll", "InvalidateRect", args, 3, &ret) == PR_OK);
            args[0] = hwnd;
            CHECK(pr_win32_call(ctx, "user32.dll", "UpdateWindow", args, 1, &ret) == PR_OK);
        }
        CHECK_EQ_U32((uint32_t)rec.n_paint, 6);

        /* BeginPaint/EndPaint: fErase + rcPaint + DC válido */
        uint8_t* ps = scratch + 184;        /* PAINTSTRUCT em memória do win32 */
        memset(ps, 0, 72);
        args[0] = hwnd; args[1] = (uint64_t)(uintptr_t)ps;
        CHECK(pr_win32_call(ctx, "user32.dll", "InvalidateRect", args, 3, &ret) == PR_OK);
        CHECK(pr_win32_call(ctx, "user32.dll", "BeginPaint", args, 2, &ret) == PR_OK);
        uint32_t pdc = (uint32_t)ret;
        CHECK(pdc != 0);
        int32_t fErase = 0;
        memcpy(&fErase, ps + 8, 4);
        CHECK_EQ_U32((uint32_t)fErase, 1);
        int32_t rcp[4];
        memcpy(rcp, ps + 12, 16);
        CHECK(rcp[2] == 32 && rcp[3] == 24);   /* rcPaint = cliente */
        CHECK(pr_win32_call(ctx, "user32.dll", "EndPaint", args, 2, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 1);

        /* DispatchMessageA → WndProc real (via seam de reentrada) */
        uint8_t* msg = scratch + 128;       /* MSG em memória do win32 */
        memset(msg, 0, 48);
        uint32_t mh = hwnd, mm = WM_CLOSE;
        uint64_t wp = 7, lp = 9;
        memcpy(msg + 0, &mh, 4);
        memcpy(msg + 8, &mm, 4);
        memcpy(msg + 16, &wp, 8);
        memcpy(msg + 24, &lp, 8);
        args[0] = (uint64_t)(uintptr_t)msg;
        CHECK(pr_win32_call(ctx, "user32.dll", "DispatchMessageA", args, 1, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)rec.last_msg, WM_CLOSE);
        /* WM_QUIT nunca é despachado */
        mm = WM_QUIT;
        memcpy(msg + 8, &mm, 4);
        CHECK(pr_win32_call(ctx, "user32.dll", "DispatchMessageA", args, 1, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0);
        CHECK_EQ_U32((uint32_t)rec.last_msg, WM_CLOSE);   /* não mudou */

        /* fila vazia + sem WM_QUIT → STOP honesto (pump bloqueante) */
        args[0] = (uint64_t)(uintptr_t)msg; args[1] = 0; args[2] = 0; args[3] = 0;
        CHECK(pr_win32_call(ctx, "user32.dll", "GetMessageA", args, 4, &ret)
              == PR_ERR_UNSUPPORTED);

        /* PeekMessageA sem mensagens = FALSE real */
        args[4] = 1;
        CHECK(pr_win32_call(ctx, "user32.dll", "PeekMessageA", args, 5, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0);

        /* DestroyWindow → WM_DESTROY → WndProc chama PostQuitMessage(42) */
        args[0] = hwnd;
        CHECK(pr_win32_call(ctx, "user32.dll", "DestroyWindow", args, 1, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 1);
        CHECK_EQ_U32((uint32_t)rec.n_destroy, 1);

        /* GetMessageA consome WM_QUIT → 0 com wParam = 42 (semântica real) */
        memset(msg, 0, 48);
        args[0] = (uint64_t)(uintptr_t)msg; args[1] = 0; args[2] = 0; args[3] = 0;
        CHECK(pr_win32_call(ctx, "user32.dll", "GetMessageA", args, 4, &ret) == PR_OK);
        CHECK_EQ_U32((uint32_t)ret, 0);
        memcpy(&mm, msg + 8, 4);
        memcpy(&wp, msg + 16, 8);
        CHECK_EQ_U32(mm, WM_QUIT);
        CHECK_EQ_U32((uint32_t)wp, 42);

        pr_win32_destroy(ctx);
        pr_log_destroy(log);
    }

    /* ================= FASE 4: PE gráfico REAL (hello_gdi.exe) ================= */
    {
        size_t alen = 0;
        uint8_t* app = gfx_load("Tests/PorticoRuntimeTests/data/hello_gdi.exe", &alen);
        CHECK(app != NULL && alen > 4096);
        if (app) {
            pr_log* log = pr_log_create(256);
            pr_peproc* p = NULL;
            CHECK(pr_peproc_create(app, alen, log, &p) == PR_OK);
            if (p) {
                CHECK(pr_peproc_prepare(p) == PR_OK);
                struct timespec t0, t1;
                clock_gettime(CLOCK_MONOTONIC, &t0);
                for (int i = 0; i < 5000 && !pr_peproc_exited(p); i++) {
                    uint64_t exec = 0;
                    if (pr_peproc_step(p, 10000, &exec) != PR_OK) break;
                }
                clock_gettime(CLOCK_MONOTONIC, &t1);
                double ms = (double)(t1.tv_sec - t0.tv_sec) * 1000.0 +
                            (double)(t1.tv_nsec - t0.tv_nsec) / 1e6;
                CHECK(pr_peproc_exited(p));
                CHECK_EQ_U32(pr_peproc_exit_code(p), 42);
                CHECK(ms < 2000.0);   /* frame completo com folga (medido <5 ms) */

                /* o frame DESENHADO está na superfície (BGRA8 → Metal) */
                pr_surf* s = pr_peproc_surface(p);
                CHECK(s != NULL);
                if (s) {
                    CHECK_EQ_U32(pr_surf_width(s), 320);
                    CHECK_EQ_U32(pr_surf_height(s), 240);
                    CHECK_EQ_U32((uint32_t)pr_surf_pitch(s), 1280);  /* 320*4 */
                    /* XRGB8888: RGB(20,24,64) → 0x00141840 */
                    CHECK_EQ_U32(px_at(s, 5, 200), 0x00141840u);     /* fundo */
                    CHECK_EQ_U32(px_at(s, 30, 30), 0x00C82828u);     /* ret. vermelho */
                    CHECK_EQ_U32(px_at(s, 150, 50), 0x0028B450u);    /* ret. verde */
                    CHECK_EQ_U32(px_at(s, 10, 10), 0x00FFFF00u);     /* pixel amarelo */
                    CHECK_EQ_U32(px_at(s, 310, 230), 0x00FF00FFu);   /* pixel magenta */
                }
                /* trilha real completa do frame */
                CHECK(log_has(log, "class registered"));
                CHECK(log_has(log, "window created 320x240"));
                CHECK(log_has(log, "PostQuitMessage code=42"));
                CHECK(log_has(log, "window destroyed"));
                pr_peproc_destroy(p);
            }
            pr_log_destroy(log);
            free(app);
        }
    }
}
