/* test_gl2.c — GRUPO 9: matrizes + vertex arrays do segundo PE 3D.
 * Unidade (APIs novas + erros reais + parâmetros inválidos) + hello_gl2.exe
 * (validação de pixels na superfície) + regressão declarada do caminho G8. */
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

static void gl_setup(pr_win32_ctx* ctx, uint64_t* out_dc) {
    uint64_t a[8], ret = 0;
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
    *out_dc = dc;
}

static void test_gl2_matrizes_arrays(void) {
    printf("-- test_gl2_matrizes_arrays\n");
    pr_win32_ctx* ctx = pr_win32_create(NULL);
    CHECK(ctx != NULL);
    size_t sz = 0;
    uint8_t* s = (uint8_t*)pr_win32_scratch(ctx, &sz);
    CHECK(s && sz >= 256);
    uint8_t* buf = s + 8;
    /* dados do convidado: triângulo 3 vértices + cores + índices */
    float* vdata = (float*)(s + 32);
    float* cdata = (float*)(s + 32 + 36);
    uint16_t* idata = (uint16_t*)(s + 32 + 72);
    vdata[0] = -0.4f; vdata[1] = -0.4f; vdata[2] = 0.0f;
    vdata[3] =  0.4f; vdata[4] = -0.4f; vdata[5] = 0.0f;
    vdata[6] =  0.0f; vdata[7] =  0.4f; vdata[8] = 0.0f;
    cdata[0] = 0.0f; cdata[1] = 0.0f; cdata[2] = 1.0f;
    cdata[3] = 0.0f; cdata[4] = 0.0f; cdata[5] = 1.0f;
    cdata[6] = 0.0f; cdata[7] = 0.0f; cdata[8] = 1.0f;
    idata[0] = 0; idata[1] = 1; idata[2] = 2;

    uint64_t dc = 0;
    gl_setup(ctx, &dc);
    uint64_t a[8], ret = 0;

    /* matrizes: projeção orto + modelview transladado */
    memset(a, 0, sizeof(a));
    a[0] = 0x1701;   /* GL_PROJECTION */
    gl_call(ctx, "glMatrixMode", a, 1);
    gl_call(ctx, "glLoadIdentity", NULL, 0);
    a[0] = dbits(-2.0); a[1] = dbits(2.0); a[2] = dbits(-1.5);
    a[3] = dbits(1.5); a[4] = dbits(-10.0); a[5] = dbits(10.0);
    gl_call(ctx, "glOrtho", a, 6);
    a[0] = 0x1700;   /* GL_MODELVIEW */
    gl_call(ctx, "glMatrixMode", a, 1);
    gl_call(ctx, "glLoadIdentity", NULL, 0);
    a[0] = fbits(0.5f); a[1] = fbits(0.0f); a[2] = fbits(0.0f);
    gl_call(ctx, "glTranslatef", a, 3);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    a[0] = fbits(0.0f); a[1] = fbits(0.0f); a[2] = fbits(0.0f); a[3] = fbits(1.0f);
    gl_call(ctx, "glClearColor", a, 4);
    a[0] = 0x4100;
    gl_call(ctx, "glClear", a, 1);

    /* vertex arrays */
    a[0] = 0x8074;   /* GL_VERTEX_ARRAY */
    gl_call(ctx, "glEnableClientState", a, 1);
    a[0] = 0x8076;   /* GL_COLOR_ARRAY */
    gl_call(ctx, "glEnableClientState", a, 1);
    memset(a, 0, sizeof(a));
    a[0] = 3; a[1] = 0x1406; a[2] = 0;
    a[3] = (uint64_t)(uintptr_t)vdata;
    gl_call(ctx, "glVertexPointer", a, 4);
    a[3] = (uint64_t)(uintptr_t)cdata;
    gl_call(ctx, "glColorPointer", a, 4);
    memset(a, 0, sizeof(a));
    a[0] = 0x0004;   /* GL_TRIANGLES */
    a[1] = 3;
    a[2] = 0x1403;   /* GL_UNSIGNED_SHORT */
    a[3] = (uint64_t)(uintptr_t)idata;
    gl_call(ctx, "glDrawElements", a, 4);
    gl_call(ctx, "glFinish", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* triângulo transladado: centro projetado ≈ (200, 109) — azul chapado */
    memset(a, 0, sizeof(a));
    a[0] = 200; a[1] = 109; a[2] = 1; a[3] = 1;
    a[4] = 0x1908; a[5] = 0x1401;
    a[6] = (uint64_t)(uintptr_t)buf;
    gl_call(ctx, "glReadPixels", a, 7);
    CHECK(buf[0] == 0 && buf[1] == 0 && buf[2] == 255);
    a[0] = 100; a[1] = 100;
    gl_call(ctx, "glReadPixels", a, 7);
    CHECK(buf[0] == 0 && buf[1] == 0 && buf[2] == 0);   /* fora → preto */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* apresentação */
    memset(a, 0, sizeof(a));
    a[0] = dc;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "SwapBuffers", a, 1, &ret) == PR_OK);
    CHECK(ret == 1);
    pr_surf* sp = pr_win32_surface(ctx);
    CHECK(sp != NULL);
    uint32_t* px = sp ? pr_surf_pixels(sp) : NULL;
    CHECK(px != NULL);
    if (px) {
        CHECK(px[(239 - 109) * 320 + 200] == 0x000000FFu);  /* azul (flip) */
        CHECK(px[(239 - 100) * 320 + 100] == 0x00000000u);
    }

    /* ---- erros reais e parâmetros inválidos ---- */
    a[0] = 0x184E;   /* GL_TEXTURE — enum inválido */
    gl_call(ctx, "glMatrixMode", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    /* operação de matriz dentro de glBegin → INVALID_OPERATION */
    a[0] = 0x0004;
    gl_call(ctx, "glBegin", a, 1);
    gl_call(ctx, "glLoadIdentity", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x502);
    gl_call(ctx, "glEnd", NULL, 0);
    /* glOrtho degenerado → INVALID_VALUE */
    a[0] = dbits(1.0); a[1] = dbits(1.0); a[2] = dbits(-1.0);
    a[3] = dbits(1.0); a[4] = dbits(-1.0); a[5] = dbits(1.0);
    gl_call(ctx, "glOrtho", a, 6);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    /* client state desconhecida → INVALID_ENUM (âncora G10→G11:
     * GL_NORMAL_ARRAY tornou-se REAL; usa-se GL_EDGE_FLAG_ARRAY) */
    a[0] = 0x8079;   /* GL_EDGE_FLAG_ARRAY */
    gl_call(ctx, "glEnableClientState", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    /* ponteiro: tipo/formato inválidos */
    memset(a, 0, sizeof(a));
    a[0] = 3; a[1] = 0x140A; a[2] = 0;   /* GL_DOUBLE */
    a[3] = (uint64_t)(uintptr_t)vdata;
    gl_call(ctx, "glVertexPointer", a, 4);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[1] = 0x1406; a[0] = 2;             /* size=2 */
    gl_call(ctx, "glVertexPointer", a, 4);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    /* glDrawElements: modo/tipo inválidos */
    a[0] = 0x0007; a[1] = 3; a[2] = 0x1403;
    a[3] = (uint64_t)(uintptr_t)idata;
    gl_call(ctx, "glDrawElements", a, 4);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);   /* GL_QUADS */
    a[0] = 0x0004; a[2] = 0x1401;                          /* UNSIGNED_BYTE */
    gl_call(ctx, "glDrawElements", a, 4);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    /* glRotatef com eixo nulo = identidade (sem erro) — exercita o caminho */
    a[0] = fbits(45.0f); a[1] = 0; a[2] = 0; a[3] = 0;
    gl_call(ctx, "glRotatef", a, 4);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* ---- honestidade (âncoras atualizadas G9→G10: glDrawArrays/
     * glTexImage2D/glPushMatrix tornaram-se REAIS; usa-se APIs GL 1.1
     * REAIS ainda fora do subconjunto) ---- */
    CHECK(pr_win32_lookup("opengl32.dll", "glMatrixMode") != NULL);  /* REAL G9 */
    CHECK(pr_win32_lookup("opengl32.dll", "glDrawArrays") != NULL);  /* REAL G10 */
    CHECK(pr_win32_lookup("opengl32.dll", "glTexImage2D") != NULL);  /* REAL G10 */
    CHECK(pr_win32_lookup("opengl32.dll", "glPushMatrix") != NULL);  /* REAL G10 */
    CHECK(pr_win32_lookup("opengl32.dll", "glTexImage3D") == NULL);  /* fora */
    CHECK(pr_win32_lookup("opengl32.dll", "glLightfv") != NULL);    /* REAL G11 */
    CHECK(pr_win32_lookup("opengl32.dll", "glFogf") == NULL);        /* fora */
    CHECK(pr_win32_lookup("opengl32.dll", "glAlphaFunc") == NULL);   /* fora */
    CHECK(pr_win32_lookup("opengl32.dll", "glTexEnvf") == NULL);     /* fora */
    CHECK(pr_win32_lookup("d3d9.dll", "Direct3DCreate9") == NULL);   /* G9: sem D3D9 */
    memset(a, 0, sizeof(a));
    a[0] = 0x0B60; a[1] = 0x0B62; a[2] = 0;
    CHECK(pr_win32_call(ctx, "opengl32.dll", "glFogf", a, 3, &ret)
          == PR_ERR_RANGE);

    pr_win32_destroy(ctx);
}

static void test_gl2_hello_pe(void) {
    printf("-- test_gl2_hello_pe (hello_gl2.exe)\n");
    size_t ilen = 0, dlen = 0;
    uint8_t* img = pe_real_load("Tests/PorticoRuntimeTests/data/hello_gl2.exe", &ilen);
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

    /* pixels derivados DA GEOMETRIA (não do runtime) na superfície (flip) */
    pr_win32_ctx* ctx = pr_peproc_win32(p);
    pr_surf* sp = pr_win32_surface(ctx);
    CHECK(sp != NULL && pr_surf_width(sp) == 320 && pr_surf_height(sp) == 240);
    uint32_t* px = sp ? pr_surf_pixels(sp) : NULL;
    CHECK(px != NULL);
    if (px) {
        CHECK(px[(239 - 108) * 320 + 162] == 0x0000FF00u);  /* frente: verde */
        CHECK(px[(239 - 130) * 320 + 96] == 0x00FF0000u);   /* esquerda: vermelho */
        CHECK(px[(239 - 175) * 320 + 150] == 0x000000FFu);  /* topo: azul */
        CHECK(px[(239 - 8) * 320 + 8] == 0x00000000u);      /* fundo: preto */
    }
    pr_peproc_destroy(p);
    free(img);
    free(dll);
}

void test_gl2(void) {
    test_gl2_matrizes_arrays();
    test_gl2_hello_pe();
}
