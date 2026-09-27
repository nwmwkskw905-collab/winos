/* test_gl5.c — GRUPO 12: glScalef (escala de modelo real) + PE hello_gl5.exe.
 * Recurso isolado: M = M·S. Erros GL reais, pixels derivados da escala
 * (anti-fraude: no-op/uniforme errado/eixos trocados falham), honestidade
 * das APIs ainda ausentes (glGetString/glGetIntegerv/glFogf). */
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
    a[4] = 0x1908;
    a[5] = 0x1401;
    a[6] = (uint64_t)(uintptr_t)buf;
    gl_call(ctx, "glReadPixels", a, 7);
    return ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
}

static void gl_setup(pr_win32_ctx* ctx, uint64_t* out_dc) {
    uint64_t a[8], ret = 0;
    memset(a, 0, sizeof(a));
    CHECK(pr_win32_call(ctx, "user32.dll", "GetDC", a, 1, &ret) == PR_OK);
    CHECK(ret != 0);
    uint64_t dc = ret;
    memset(a, 0, sizeof(a));
    a[0] = dc;
    CHECK(pr_win32_call(ctx, "opengl32.dll", "wglCreateContext", a, 1, &ret)
          == PR_OK);
    CHECK(ret != 0);
    a[1] = ret;
    CHECK(pr_win32_call(ctx, "opengl32.dll", "wglMakeCurrent", a, 2, &ret)
          == PR_OK);
    CHECK(ret == 1);
    *out_dc = dc;
}

static void clear_all(pr_win32_ctx* ctx) {
    uint64_t a[8];
    memset(a, 0, sizeof(a));
    a[0] = fbits(0); a[1] = fbits(0); a[2] = fbits(0); a[3] = fbits(1);
    gl_call(ctx, "glClearColor", a, 4);
    a[0] = 0x4000;   /* GL_COLOR_BUFFER_BIT */
    gl_call(ctx, "glClear", a, 1);
}

/* 2 triângulos de um quad (modo imediato) */
static void draw_quad(pr_win32_ctx* ctx, float x0, float y0,
                      float x1, float y1) {
    uint64_t a[4];
    memset(a, 0, sizeof(a));
    gl_call(ctx, "glBegin", (uint64_t[1]){ 0x0004 }, 1);
    {
        const float v[6][2] = {
            {x0, y0}, {x1, y0}, {x1, y1}, {x0, y0}, {x1, y1}, {x0, y1}
        };
        for (int i = 0; i < 6; i++) {
            a[0] = fbits(v[i][0]); a[1] = fbits(v[i][1]); a[2] = fbits(0);
            gl_call(ctx, "glVertex3f", a, 3);
        }
    }
    gl_call(ctx, "glEnd", NULL, 0);
}

/* projeção ortográfica + MODELVIEW com glScalef(2, 0.5, 1) — como o PE */
static void setup_scale(pr_win32_ctx* ctx, float sx, float sy) {
    uint64_t a[8];
    memset(a, 0, sizeof(a));
    a[0] = 0x1701;   /* GL_PROJECTION */
    gl_call(ctx, "glMatrixMode", a, 1);
    gl_call(ctx, "glLoadIdentity", NULL, 0);
    a[0] = dbits(-1.0); a[1] = dbits(1.0); a[2] = dbits(-1.0);
    a[3] = dbits(1.0);  a[4] = dbits(-1.0); a[5] = dbits(1.0);
    gl_call(ctx, "glOrtho", a, 6);
    a[0] = 0x1700;   /* GL_MODELVIEW */
    gl_call(ctx, "glMatrixMode", a, 1);
    gl_call(ctx, "glLoadIdentity", NULL, 0);
    a[0] = fbits(sx); a[1] = fbits(sy); a[2] = fbits(1);
    gl_call(ctx, "glScalef", a, 3);
}

static void test_gl5_unidade(void) {
    printf("-- test_gl5_unidade (glScalef: erros GL reais + honestidade)\n");
    pr_win32_ctx* gl = pr_win32_create(NULL);
    CHECK(gl != NULL);
    uint64_t dc = 0;
    uint64_t a[8], ret = 0;
    gl_setup(gl, &dc);

    /* glScalef fora de begin = OK */
    a[0] = fbits(2); a[1] = fbits(0.5f); a[2] = fbits(1);
    gl_call(gl, "glScalef", a, 3);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);

    /* glScalef DENTRO de glBegin -> GL_INVALID_OPERATION (0x502) */
    gl_call(gl, "glBegin", (uint64_t[1]){ 0x0004 }, 1);
    a[0] = fbits(1); a[1] = fbits(1); a[2] = fbits(1);
    gl_call(gl, "glScalef", a, 3);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x502);
    gl_call(gl, "glEnd", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);

    /* wrapper: n < 3 -> PR_ERR_INVALID */
    memset(a, 0, sizeof(a));
    ret = 7;
    CHECK(pr_win32_call(gl, "opengl32.dll", "glScalef", a, 2, &ret)
          == PR_ERR_INVALID);

    /* honestidade: ainda ausentes (NÃO implementadas por antecipação) */
    memset(a, 0, sizeof(a));
    a[0] = 0x1F00;   /* GL_VERSION */
    ret = 9;
    CHECK(pr_win32_call(gl, "opengl32.dll", "glGetDoublev", a, 2, &ret)
          == PR_ERR_RANGE);
    CHECK(ret == 0);   /* âncora G12->G13: glGetString virou REAL */
    ret = 9;
    CHECK(pr_win32_call(gl, "opengl32.dll", "glGetFloatv", a, 2, &ret)
          == PR_ERR_RANGE);
    ret = 9;
    CHECK(pr_win32_call(gl, "opengl32.dll", "glFogf", a, 2, &ret)
          == PR_ERR_RANGE);
}

static void test_gl5_render(void) {
    printf("-- test_gl5_render (escala real: pixels derivados)\n");
    pr_win32_ctx* gl = pr_win32_create(NULL);
    CHECK(gl != NULL);
    uint64_t dc = 0;
    uint64_t a[8];
    size_t ssz = 0;
    uint8_t* s = (uint8_t*)pr_win32_scratch(gl, &ssz);
    uint8_t* buf = s + 256;   /* 4 bytes p/ glReadPixels */
    gl_setup(gl, &dc);

    setup_scale(gl, 2.0f, 0.5f);
    clear_all(gl);
    a[0] = fbits(1); a[1] = fbits(0); a[2] = fbits(0);
    gl_call(gl, "glColor3f", a, 3);
    draw_quad(gl, -0.4f, -0.4f, 0.4f, 0.4f);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);

    /* mesmos 8 pontos derivados do PE (GL y bottom-up) */
    CHECK(read_px(gl, buf, 160, 120) == 0x00FF0000u);   /* centro */
    CHECK(read_px(gl, buf, 264, 120) == 0x00FF0000u);   /* X int 24px */
    CHECK(read_px(gl, buf,  56, 120) == 0x00FF0000u);   /* X int 24px */
    CHECK(read_px(gl, buf, 160, 156) == 0x00000000u);   /* Y ext 12px */
    CHECK(read_px(gl, buf, 160,  84) == 0x00000000u);   /* Y ext 12px */
    CHECK(read_px(gl, buf, 312, 120) == 0x00000000u);   /* X ext 24px */
    CHECK(read_px(gl, buf, 160, 130) == 0x00FF0000u);   /* Y int 14px */
    CHECK(read_px(gl, buf, 208, 156) == 0x00000000u);   /* canto ext */

    /* escala (1,1) = identidade restaura o quad base (sanidade) */
    setup_scale(gl, 1.0f, 1.0f);
    clear_all(gl);
    a[0] = fbits(1); a[1] = fbits(0); a[2] = fbits(0);
    gl_call(gl, "glColor3f", a, 3);
    draw_quad(gl, -0.4f, -0.4f, 0.4f, 0.4f);
    CHECK(read_px(gl, buf, 208, 156) == 0x00FF0000u);   /* borda base */
    CHECK(read_px(gl, buf, 264, 120) == 0x00000000u);   /* fora da base */
}

static void test_gl5_hello_pe(void) {
    printf("-- test_gl5_hello_pe (hello_gl5.exe)\n");
    size_t ilen = 0;
    uint8_t* img = pe_real_load("Tests/PorticoRuntimeTests/data/hello_gl5.exe",
                                &ilen);
    CHECK(img != NULL && ilen > 1024);
    if (!img) return;

    pr_log* log = pr_log_create(1024);
    pr_peproc* p = NULL;
    CHECK(pr_peproc_create(img, ilen, log, &p) == PR_OK);
    CHECK(p != NULL);
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
    if (rc != 42)
        printf("[LOG-hello_gl5] %llu entradas de log\n",
               (unsigned long long)pr_log_count(log));
    CHECK(rc == 42);   /* glScalef real + 8 asserts de pixel */

    pr_surf* sp = pr_win32_surface(ctx);
    CHECK(sp != NULL && pr_surf_width(sp) == 320);
    uint32_t* px = sp ? pr_surf_pixels(sp) : NULL;
    CHECK(px != NULL);
    if (px) {
        /* superfície (flip: y_surf = 239 - y_gl) */
        CHECK(px[(239 - 120) * 320 + 160] == 0x00FF0000u);  /* centro */
        CHECK(px[(239 - 120) * 320 + 264] == 0x00FF0000u);  /* X int */
        CHECK(px[(239 - 120) * 320 +  56] == 0x00FF0000u);  /* X int */
        CHECK(px[(239 - 156) * 320 + 160] == 0x00000000u);  /* Y ext */
        CHECK(px[(239 -  84) * 320 + 160] == 0x00000000u);  /* Y ext */
        CHECK(px[(239 - 156) * 320 + 208] == 0x00000000u);  /* canto ext */
        CHECK(px[(239 - 130) * 320 + 160] == 0x00FF0000u);  /* Y int */
    }
    pr_peproc_destroy(p);
    free(img);
}

void test_gl5(void) {
    test_gl5_unidade();
    test_gl5_render();
    test_gl5_hello_pe();
}
