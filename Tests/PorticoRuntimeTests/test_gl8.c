/* test_gl8.c — GRUPO 15: glDisableClientState (única implementação do grupo)
 * + combinação 3D natural hello_gl8.exe (42).
 * Semântica real do disable observável (arranjo desligado volta à cor atual;
 * vertex array desligado -> GL_INVALID_OPERATION no draw), enum inválido
 * 0x500, e tipos de índice de glDrawElements: GL_UNSIGNED_SHORT (certificado
 * G9-G14) e GL_UNSIGNED_INT (certificado G16 — ver test_gl9) desenham;
 * GL_UNSIGNED_BYTE continua GL_INVALID_ENUM honesto + nada desenhado. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pt_util.h"
#include "portico/pr_win32.h"
#include "portico/pr_peproc.h"
#include "portico/pr_surf.h"

static uint8_t* pe_real_load(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

static uint64_t fbits(float f) {
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

static uint64_t gl_call(pr_win32_ctx* ctx, const char* fn,
                        const uint64_t* a, size_t n) {
    uint64_t ret = 0;
    pr_win32_call(ctx, "opengl32.dll", fn, a, n, &ret);
    return ret;
}

static void gl_setup(pr_win32_ctx* ctx, uint64_t* out_dc) {
    uint64_t a[2], ret = 0;
    a[0] = 0;
    pr_win32_call(ctx, "user32.dll", "GetDC", a, 1, &ret);
    *out_dc = ret;
    a[0] = ret;
    pr_win32_call(ctx, "opengl32.dll", "wglCreateContext", a, 1, &ret);
    a[0] = *out_dc; a[1] = ret;
    pr_win32_call(ctx, "opengl32.dll", "wglMakeCurrent", a, 2, &ret);
}

/* pixel via glReadPixels (o buffer GL é a fonte; a superfície só após flush) */
static uint32_t read_gl_px(pr_win32_ctx* gl, uint8_t* px4, int x, int y) {
    uint64_t a[7] = { (uint64_t)(int64_t)x, (uint64_t)(int64_t)y, 1, 1,
                      0x1908, 0x1401, (uint64_t)(uintptr_t)px4 };
    gl_call(gl, "glReadPixels", a, 7);
    return ((uint32_t)px4[0] << 16) | ((uint32_t)px4[1] << 8) | px4[2];
}

static uint32_t surf_px(pr_surf* s, int x, int y_gl) {
    return pr_surf_pixels(s)[((size_t)pr_surf_height(s) - 1u - (size_t)y_gl)
                             * (size_t)pr_surf_width(s) + (size_t)x];
}

static void setup_ortho(pr_win32_ctx* gl) {
    uint64_t a[1];
    double d[6] = { 0.0, 320.0, 0.0, 240.0, -1.0, 1.0 };
    uint64_t ad[6];
    memcpy(ad, d, sizeof d);
    a[0] = 0x1701;                       /* GL_PROJECTION */
    gl_call(gl, "glMatrixMode", a, 1);
    gl_call(gl, "glLoadIdentity", NULL, 0);
    gl_call(gl, "glOrtho", ad, 6);
    a[0] = 0x1700;                       /* GL_MODELVIEW */
    gl_call(gl, "glMatrixMode", a, 1);
    gl_call(gl, "glLoadIdentity", NULL, 0);
}

static void clear_black(pr_win32_ctx* gl) {
    uint64_t a[4] = { 0, 0, 0, fbits(1.0f) };
    gl_call(gl, "glClearColor", a, 4);
    a[0] = 0x4000;                       /* GL_COLOR_BUFFER_BIT */
    gl_call(gl, "glClear", a, 1);
}

static void load_id(pr_win32_ctx* gl) {
    gl_call(gl, "glLoadIdentity", NULL, 0);
}

static void translate3(pr_win32_ctx* gl, float x, float y, float z) {
    uint64_t a[3] = { fbits(x), fbits(y), fbits(z) };
    gl_call(gl, "glTranslatef", a, 3);
}

static void draw_arrays3(pr_win32_ctx* gl) {
    uint64_t a[3] = { 0x0004, 0, 3 };
    gl_call(gl, "glDrawArrays", a, 3);
    gl_call(gl, "glFinish", NULL, 0);
}

static void test_gl8_engine(void) {
    printf("-- test_gl8_engine (disable observavel + gap registrado)\n");
    pr_win32_ctx* gl = pr_win32_create(NULL);
    CHECK(gl != NULL);
    uint64_t dc = 0;
    uint64_t a[8];
    gl_setup(gl, &dc);
    CHECK(dc != 0);

    size_t ssz = 0;
    uint8_t* scr = (uint8_t*)pr_win32_scratch(gl, &ssz);
    CHECK(scr != NULL && ssz >= 256);
    uint8_t* px4 = scr + 240;

    /* dados do convidado no scratch (pr_win32_ptr valida o intervalo) */
    {
        static const float vtri[9] = { 0, 0, 0, 50, 0, 0, 0, 50, 0 };
        static const float cols[9] = { 1, 1, 0, 1, 1, 0, 1, 1, 0 };
        static const unsigned short idx16[3] = { 0, 1, 2 };
        static const unsigned int idx32[3] = { 0, 1, 2 };
        memcpy(scr + 0, vtri, sizeof vtri);
        memcpy(scr + 64, cols, sizeof cols);
        memcpy(scr + 128, idx16, sizeof idx16);
        memcpy(scr + 160, idx32, sizeof idx32);
    }

    setup_ortho(gl);
    clear_black(gl);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);

    /* 1) com COLOR_ARRAY ligado o lote usa a cor do ARRANJO (amarelo) */
    a[0] = 0x8074; gl_call(gl, "glEnableClientState", a, 1);  /* VERT */
    a[0] = 0x8076; gl_call(gl, "glEnableClientState", a, 1);  /* COLOR */
    a[0] = 3; a[1] = 0x1406; a[2] = 0; a[3] = (uint64_t)(uintptr_t)scr;
    gl_call(gl, "glVertexPointer", a, 4);
    a[3] = (uint64_t)(uintptr_t)(scr + 64);
    gl_call(gl, "glColorPointer", a, 4);
    load_id(gl); translate3(gl, 10, 10, 0);
    draw_arrays3(gl);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 25, 25) == 0xFFFF00u);

    /* 2) glDisableClientState(GL_COLOR_ARRAY): o MESMO lote passa a usar a
     *    cor ATUAL (verde) — se o disable fosse no-op, o lote continuaria
     *    amarelo; o efeito é observável no pixel */
    a[0] = 0x8076; gl_call(gl, "glDisableClientState", a, 1);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    a[0] = fbits(0.0f); a[1] = fbits(1.0f); a[2] = fbits(0.0f);
    gl_call(gl, "glColor3f", a, 3);
    load_id(gl); translate3(gl, 80, 10, 0);
    draw_arrays3(gl);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 95, 25) == 0x00FF00u);

    /* 3) sem VERTEX_ARRAY o draw falha honesto (GL_INVALID_OPERATION) e não
     *    desenha nada */
    a[0] = 0x8074; gl_call(gl, "glDisableClientState", a, 1);
    load_id(gl); translate3(gl, 10, 80, 0);
    draw_arrays3(gl);
    CHECK(read_gl_px(gl, px4, 25, 95) == 0u);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x502u);

    /* 4) enum inválido -> GL_INVALID_ENUM */
    a[0] = 0x9999; gl_call(gl, "glDisableClientState", a, 1);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x500u);

    /* 5) tipos de índice (atualizado no G16): GL_UNSIGNED_INT agora é REAL
     *    (32-bit, ver test_gl9); controle GL_UNSIGNED_SHORT idêntico; tipos
     *    ainda não suportados continuam honestos (GL_UNSIGNED_BYTE -> 0x500) */
    a[0] = 0x8074; gl_call(gl, "glEnableClientState", a, 1);
    a[0] = 3; a[1] = 0x1406; a[2] = 0; a[3] = (uint64_t)(uintptr_t)scr;
    gl_call(gl, "glVertexPointer", a, 4);
    load_id(gl); translate3(gl, 80, 80, 0);
    a[0] = 0x0004; a[1] = 3; a[2] = 0x1405;
    a[3] = (uint64_t)(uintptr_t)(scr + 160);
    gl_call(gl, "glDrawElements", a, 4);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(read_gl_px(gl, px4, 95, 95) == 0x00FF00u);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    load_id(gl); translate3(gl, 150, 80, 0);
    a[0] = 0x0004; a[1] = 3; a[2] = 0x1403;
    a[3] = (uint64_t)(uintptr_t)(scr + 128);
    gl_call(gl, "glDrawElements", a, 4);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 165, 95) == 0x00FF00u);
    load_id(gl); translate3(gl, 220, 80, 0);
    a[0] = 0x0004; a[1] = 3; a[2] = 0x1401;   /* GL_UNSIGNED_BYTE */
    a[3] = (uint64_t)(uintptr_t)(scr + 128);
    gl_call(gl, "glDrawElements", a, 4);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(read_gl_px(gl, px4, 235, 95) == 0u);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x500u);

    /* gl_setup finish */
    {
        uint64_t ar[2] = { 0, 0 }, ret = 0;
        pr_win32_call(gl, "opengl32.dll", "wglMakeCurrent", ar, 2, &ret);
        ar[0] = dc; pr_win32_call(gl, "user32.dll", "ReleaseDC", ar, 2, &ret);
    }
    pr_win32_destroy(gl);
}

static void test_gl8_pe(void) {
    printf("-- test_gl8_pe (hello_gl8.exe: frame 3D natural a 42)\n");
    size_t ilen = 0;
    uint8_t* img = pe_real_load("Tests/PorticoRuntimeTests/data/hello_gl8.exe",
                                &ilen);
    CHECK(img != NULL && ilen > 0);
    if (!img) return;
    pr_log* log = pr_log_create(1024);
    pr_peproc* p = NULL;
    CHECK(pr_peproc_create(img, ilen, log, &p) == PR_OK);
    CHECK(p != NULL);
    if (!p) { free(img); return; }
    CHECK(pr_peproc_prepare(p) == PR_OK);
    pr_win32_ctx* ctx = pr_peproc_win32(p);
    CHECK(ctx != NULL);

    uint64_t exec = 0;
    int i = 0;
    while (i < 5000 && !pr_peproc_exited(p)) {
        if (pr_peproc_step(p, 10000, &exec) != PR_OK) break;
        i++;
    }
    CHECK(pr_peproc_exited(p));
    uint32_t rc = pr_peproc_exit_code(p);
    CHECK(rc == 42u);
    pr_surf* sp = pr_win32_surface(ctx);
    CHECK(sp != NULL && pr_surf_width(sp) == 320);
    uint32_t* px = sp ? pr_surf_pixels(sp) : NULL;
    CHECK(px != NULL);
    if (px) {
        CHECK(surf_px(sp, 120, 120) == 0x00FF0000u);
        CHECK(surf_px(sp, 180, 120) == 0x00FF0000u);
        CHECK(surf_px(sp, 240, 120) == 0x000000FFu);
        CHECK(surf_px(sp, 225, 185) == 0x00FFFF00u);
        CHECK(surf_px(sp, 300, 200) == 0x00000000u);
        {
            uint32_t got = surf_px(sp, 45, 45);
            CHECK(got == 0x007F7F00u || got == 0x00808000u);
        }
    }
    pr_peproc_destroy(p);
    pr_log_destroy(log);
    free(img);
}

void test_gl8(void) {
    printf("== G15: glDisableClientState + combinacao 3D natural (hello_gl8) ==\n");
    test_gl8_engine();
    test_gl8_pe();
}
