/* test_gl.c — GRUPO 8: subconjunto real do OpenGL 1.1 por software.
 * Rasterização/z-buffer reais, apresentação no caminho GDI→SurfaceBridge→
 * BGRA8→Metal e o PE de controle hello_gl.exe (validação de pixels). */
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
static uint64_t dbits(double d) {
    uint64_t b;
    memcpy(&b, &d, 8);
    return b;
}

static uint64_t gl_call(pr_win32_ctx* ctx, const char* fn,
                        const uint64_t* a, size_t n) {
    uint64_t ret = 12345;
    pr_status st = pr_win32_call(ctx, "opengl32.dll", fn, a, n, &ret);
    CHECK(st == PR_OK);
    return ret;
}

static uint32_t read_px(pr_win32_ctx* ctx, uint8_t* buf, int x, int y) {
    uint64_t a[8];
    memset(a, 0, sizeof(a));
    a[0] = (uint64_t)(int64_t)x;
    a[1] = (uint64_t)(int64_t)y;
    a[2] = 1;
    a[3] = 1;
    a[4] = 0x1908;   /* GL_RGBA */
    a[5] = 0x1401;   /* GL_UNSIGNED_BYTE */
    a[6] = (uint64_t)(uintptr_t)buf;
    gl_call(ctx, "glReadPixels", a, 7);
    return ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
}

static void gl_setup(pr_win32_ctx* ctx, uint8_t* scratch, uint64_t* out_dc) {
    size_t sz = 0;
    (void)sz;
    uint64_t a[8], ret = 0;
    /* DC sem janela (hdc=0 → superfície 320x240 + handle DC) */
    memset(a, 0, sizeof(a));
    CHECK(pr_win32_call(ctx, "user32.dll", "GetDC", a, 1, &ret) == PR_OK);
    CHECK(ret != 0);
    uint64_t dc = ret;
    memset(a, 0, sizeof(a));
    a[0] = dc;
    CHECK(pr_win32_call(ctx, "opengl32.dll", "wglCreateContext", a, 1, &ret) == PR_OK);
    CHECK(ret != 0);
    a[1] = ret;
    CHECK(pr_win32_call(ctx, "opengl32.dll", "wglMakeCurrent", a, 2, &ret) == PR_OK);
    CHECK(ret == 1);
    (void)scratch;
    *out_dc = dc;
}

static void test_gl_unidade(void) {
    printf("-- test_gl_unidade\n");
    pr_win32_ctx* ctx = pr_win32_create(NULL);
    CHECK(ctx != NULL);

    size_t sz = 0;
    uint8_t* s = (uint8_t*)pr_win32_scratch(ctx, &sz);
    CHECK(s && sz >= 256);
    uint8_t* buf = s + 128;
    uint64_t dc = 0;
    gl_setup(ctx, s, &dc);

    uint64_t a[8], ret = 0;
    /* viewport + clear preto + depth */
    memset(a, 0, sizeof(a));
    a[0] = 0; a[1] = 0; a[2] = 320; a[3] = 240;
    gl_call(ctx, "glViewport", a, 4);
    a[0] = fbits(0.0f); a[1] = fbits(0.0f); a[2] = fbits(0.0f); a[3] = fbits(1.0f);
    gl_call(ctx, "glClearColor", a, 4);
    a[0] = dbits(1.0);
    gl_call(ctx, "glClearDepth", a, 1);
    a[0] = 0x0B71;   /* GL_DEPTH_TEST */
    gl_call(ctx, "glEnable", a, 1);
    a[0] = 0x4100;   /* COLOR|DEPTH */
    gl_call(ctx, "glClear", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* triângulo TRÁS (z=+0.5) vermelho + FRENTE (z=-0.5) verde — oclusão */
    a[0] = 0x0004;   /* GL_TRIANGLES */
    gl_call(ctx, "glBegin", a, 1);
    a[0] = fbits(1.0f); a[1] = fbits(0.0f); a[2] = fbits(0.0f);
    gl_call(ctx, "glColor3f", a, 3);
    a[0] = fbits(-0.8f); a[1] = fbits(-0.8f); a[2] = fbits(0.5f);
    gl_call(ctx, "glVertex3f", a, 3);
    a[0] = fbits(0.8f); a[1] = fbits(-0.8f); a[2] = fbits(0.5f);
    gl_call(ctx, "glVertex3f", a, 3);
    a[0] = fbits(0.0f); a[1] = fbits(0.8f); a[2] = fbits(0.5f);
    gl_call(ctx, "glVertex3f", a, 3);
    gl_call(ctx, "glEnd", NULL, 0);
    a[0] = 0x0004;
    gl_call(ctx, "glBegin", a, 1);
    a[0] = fbits(0.0f); a[1] = fbits(1.0f); a[2] = fbits(0.0f);
    gl_call(ctx, "glColor3f", a, 3);
    a[0] = fbits(-0.4f); a[1] = fbits(-0.4f); a[2] = fbits(-0.5f);
    gl_call(ctx, "glVertex3f", a, 3);
    a[0] = fbits(0.4f); a[1] = fbits(-0.4f); a[2] = fbits(-0.5f);
    gl_call(ctx, "glVertex3f", a, 3);
    a[0] = fbits(0.0f); a[1] = fbits(0.4f); a[2] = fbits(-0.5f);
    gl_call(ctx, "glVertex3f", a, 3);
    gl_call(ctx, "glEnd", NULL, 0);
    gl_call(ctx, "glFinish", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* pixels do framebuffer (origem GL inferior-esquerda) */
    CHECK(read_px(ctx, buf, 160, 120) == 0x0000FF00u);  /* sobreposto → verde */
    CHECK(read_px(ctx, buf, 160, 40) == 0x00FF0000u);   /* só trás → vermelho */
    CHECK(read_px(ctx, buf, 20, 20) == 0x00000000u);    /* fora → preto */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* apresentação no caminho certificado (flip p/ origem GDI) */
    memset(a, 0, sizeof(a));
    a[0] = dc;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "SwapBuffers", a, 1, &ret) == PR_OK);
    CHECK(ret == 1);
    pr_surf* sp = pr_win32_surface(ctx);
    CHECK(sp != NULL && pr_surf_width(sp) == 320);
    uint32_t* px = sp ? pr_surf_pixels(sp) : NULL;
    CHECK(px != NULL);
    if (px) {
        CHECK(px[119 * 320 + 160] == 0x0000FF00u);   /* y_gl 120 → 239-120 */
        CHECK(px[199 * 320 + 160] == 0x00FF0000u);   /* y_gl 40  → 199 */
        CHECK(px[219 * 320 + 20] == 0x00000000u);
    }

    /* erros GL reais (sticky-first) e honestidade */
    a[0] = 0x0B44;   /* GL_CULL_FACE — fora (âncora G10->G11) */
    gl_call(ctx, "glEnable", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);   /* INVALID_ENUM */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);       /* limpo */
    a[0] = 0x0007;   /* GL_QUADS — fora do subconjunto */
    gl_call(ctx, "glBegin", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    /* glBegin aninhado → INVALID_OPERATION */
    a[0] = 0x0004;
    gl_call(ctx, "glBegin", a, 1);
    gl_call(ctx, "glBegin", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x502);
    gl_call(ctx, "glEnd", NULL, 0);
    /* glReadPixels fora do formato suportado → INVALID_ENUM */
    memset(a, 0, sizeof(a));
    a[0] = 0; a[1] = 0; a[2] = 1; a[3] = 1;
    a[4] = 0x80E1;   /* GL_BGRA (1.2/EXT — não suportado) */
    a[5] = 0x1401;
    a[6] = (uint64_t)(uintptr_t)buf;
    gl_call(ctx, "glReadPixels", a, 7);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);

    /* honestidade: resto do GL fora do catálogo → nunca sucesso falso.
     * Âncoras atualizadas G9→G10 (padrão honesto): glDrawArrays/glTexImage2D/
     * glPushMatrix tornaram-se REAIS no GRUPO 10; a âncora usa APIs GL 1.1
     * REAIS que continuam FORA do subconjunto implementado. */
    CHECK(pr_win32_lookup("opengl32.dll", "glMatrixMode") != NULL);  /* REAL G9 */
    CHECK(pr_win32_lookup("opengl32.dll", "glDrawArrays") != NULL);  /* REAL G10 */
    CHECK(pr_win32_lookup("opengl32.dll", "glTexImage2D") != NULL);  /* REAL G10 */
    CHECK(pr_win32_lookup("opengl32.dll", "glPushMatrix") != NULL);  /* REAL G10 */
    CHECK(pr_win32_lookup("opengl32.dll", "glTexImage3D") == NULL);  /* fora */
    CHECK(pr_win32_lookup("opengl32.dll", "glLightfv") != NULL);    /* REAL G11 */
    CHECK(pr_win32_lookup("opengl32.dll", "glFogf") == NULL);        /* fora */
    CHECK(pr_win32_lookup("opengl32.dll", "glAlphaFunc") == NULL);   /* fora */
    CHECK(pr_win32_lookup("opengl32.dll", "glTexEnvf") == NULL);     /* fora */
    memset(a, 0, sizeof(a));
    a[0] = 0x0B60; a[1] = 0x0B62; a[2] = 0;
    /* fora do catálogo → PR_ERR_RANGE (nunca sucesso falso); no PE real a
     * chamada vira EXECUTION STOPPED (stub de diagnóstico do loader) */
    CHECK(pr_win32_call(ctx, "opengl32.dll", "glFogf", a, 3, &ret)
          == PR_ERR_RANGE);
    CHECK(ret == 0);
    /* d3d9 inteiro = fora do grupo 8 → STOP honesto */
    CHECK(pr_win32_lookup("d3d9.dll", "Direct3DCreate9") == NULL);

    pr_win32_destroy(ctx);
}

static void test_gl_hello_pe(void) {
    printf("-- test_gl_hello_pe (hello_gl.exe)\n");
    size_t ilen = 0, dlen = 0;
    uint8_t* img = pe_real_load("Tests/PorticoRuntimeTests/data/hello_gl.exe", &ilen);
    uint8_t* dll = pe_real_load("Tests/PorticoRuntimeTests/data/hello_dll.dll", &dlen);
    CHECK(img != NULL && ilen > 1024);
    CHECK(dll != NULL);
    if (!img || !dll) { free(img); free(dll); return; }

    pr_log* log = pr_log_create(1024);
    pr_peproc* p = NULL;
    CHECK(pr_peproc_create(img, ilen, log, &p) == PR_OK);
    CHECK(pr_peproc_provide_dll(p, "hello_dll.dll", dll, (uint32_t)dlen) == PR_OK);
    CHECK(pr_peproc_prepare(p) == PR_OK);

    uint64_t exec = 0;
    int i = 0;
    while (i < 5000 && !pr_peproc_exited(p)) {
        if (pr_peproc_step(p, 10000, &exec) != PR_OK) break;
        i++;
    }
    CHECK(pr_peproc_exited(p));
    uint32_t rc = pr_peproc_exit_code(p);
    if (rc != 42) {
        pr_log_entry es[32];
        size_t n = pr_log_read(log, es, 32, 0);
        for (size_t k = 0; k < n; k++)
            printf("[LOG] %s: %s\n", pr_log_level_str(es[k].level), es[k].msg);
    }
    CHECK(rc == 42);

    /* frame APRESENTADO (SwapBuffers) na superfície do caminho Metal */
    pr_win32_ctx* ctx = pr_peproc_win32(p);
    pr_surf* sp = pr_win32_surface(ctx);
    CHECK(sp != NULL && pr_surf_width(sp) == 320 && pr_surf_height(sp) == 240);
    uint32_t* px = sp ? pr_surf_pixels(sp) : NULL;
    CHECK(px != NULL);
    /* mapeamento exato gl(y) → superfície 239-y (flip vertical) */
    if (px) {
        CHECK(px[119 * 320 + 160] == 0x0000FF00u);   /* oclusão: frente venceu */
        CHECK(px[199 * 320 + 160] == 0x00FF0000u);   /* só trás */
        CHECK(px[219 * 320 + 20] == 0x00000000u);    /* cor de limpeza */
    }
    pr_peproc_destroy(p);
    free(img);
    free(dll);
}

void test_gl(void) {
    test_gl_unidade();
    test_gl_hello_pe();
}
