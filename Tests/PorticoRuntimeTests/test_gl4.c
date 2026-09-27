/* test_gl4.c — GRUPO 11: iluminação fixed-function + blending/alpha.
 * Gouraud por vértice (ambiente/difusa/especular Blinn), blend real,
 * interação z-buffer×blend, PE hello_gl4.exe (pixels derivados). */
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

/* luz difusa VERMELHA direcional + material determinístico (como o PE) */
static void setup_light(pr_win32_ctx* ctx) {
    uint64_t a[12];
    size_t ssz = 0;
    uint8_t* s = (uint8_t*)pr_win32_scratch(ctx, &ssz);
    float* p = (float*)(void*)(s + 32);   /* 4 floats por chamada */
    memset(a, 0, sizeof(a));
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 1;             /* amb 0 */
    a[0] = 0x4000; a[1] = 0x1200; a[2] = (uint64_t)(uintptr_t)p;
    gl_call(ctx, "glLightfv", a, 3);
    p[0] = 1; p[1] = 0; p[2] = 0; p[3] = 1;             /* difusa vermelha */
    a[1] = 0x1201;
    gl_call(ctx, "glLightfv", a, 3);
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 1;             /* especular 0 */
    a[1] = 0x1202;
    gl_call(ctx, "glLightfv", a, 3);
    p[0] = 0; p[1] = 0; p[2] = 1; p[3] = 0;             /* direcional +z */
    a[1] = 0x1203;
    gl_call(ctx, "glLightfv", a, 3);
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 1;             /* mat ambiente 0 */
    a[0] = 0x0404; a[1] = 0x1200;
    gl_call(ctx, "glMaterialfv", a, 3);
    p[0] = 1; p[1] = 1; p[2] = 1; p[3] = 1;             /* mat difusa branca */
    a[1] = 0x1201;
    gl_call(ctx, "glMaterialfv", a, 3);
    a[0] = 0x0B50;
    gl_call(ctx, "glEnable", a, 1);
    a[0] = 0x4000;
    gl_call(ctx, "glEnable", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
}

/* quad imediato ±size com normal n (2 triângulos) */
static void draw_quad_n(pr_win32_ctx* ctx, float sz, const float n[3]) {
    uint64_t a[8];
    memset(a, 0, sizeof(a));
    a[0] = 0x0004;
    gl_call(ctx, "glBegin", a, 1);
    a[0] = fbits(n[0]); a[1] = fbits(n[1]); a[2] = fbits(n[2]);
    gl_call(ctx, "glNormal3f", a, 3);
    float v[6][3] = {
        {-sz, -sz, 0}, { sz, -sz, 0}, { sz,  sz, 0},
        {-sz, -sz, 0}, { sz,  sz, 0}, {-sz,  sz, 0}
    };
    for (int i = 0; i < 6; i++) {
        a[0] = fbits(v[i][0]); a[1] = fbits(v[i][1]); a[2] = fbits(v[i][2]);
        gl_call(ctx, "glVertex3f", a, 3);
    }
    gl_call(ctx, "glEnd", NULL, 0);
}

static void clear_all(pr_win32_ctx* ctx) {
    uint64_t a[8];
    a[0] = fbits(0.0f); a[1] = fbits(0.0f); a[2] = fbits(0.0f); a[3] = fbits(1.0f);
    gl_call(ctx, "glClearColor", a, 4);
    a[0] = 0x4100;
    gl_call(ctx, "glClear", a, 1);
}

static void test_gl4_unidade(void) {
    printf("-- test_gl4_unidade (erros GL reais de luz/material/blend)\n");
    pr_win32_ctx* ctx = pr_win32_create(NULL);
    CHECK(ctx != NULL);
    size_t sz = 0;
    uint8_t* s = (uint8_t*)pr_win32_scratch(ctx, &sz);
    uint8_t* buf = s + 256;
    uint64_t dc = 0;
    gl_setup(ctx, &dc);
    float* p = (float*)(void*)(s + 32);

    uint64_t a[12];

    /* glNormal3f é atributo de vértice: VÁLIDO dentro de glBegin (GL real) */
    memset(a, 0, sizeof(a));
    a[0] = 0x0004;
    gl_call(ctx, "glBegin", a, 1);
    a[0] = fbits(0); a[1] = fbits(0); a[2] = fbits(1);
    gl_call(ctx, "glNormal3f", a, 3);
    gl_call(ctx, "glEnd", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* glLightfv: luz/pname inválidos + fora de glBegin */
    memset(a, 0, sizeof(a));
    a[0] = 0x4008; a[1] = 0x1200; a[2] = (uint64_t)(uintptr_t)p;
    gl_call(ctx, "glLightfv", a, 3);            /* GL_LIGHT8 inexistente */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x4000; a[1] = 0x1205;
    gl_call(ctx, "glLightfv", a, 3);            /* GL_SPOT_EXPONENT: fora */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x0004;
    gl_call(ctx, "glBegin", a, 1);
    a[0] = 0x4000; a[1] = 0x1203; a[2] = (uint64_t)(uintptr_t)p;
    gl_call(ctx, "glLightfv", a, 3);            /* dentro de glBegin */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x502);
    gl_call(ctx, "glEnd", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* glMaterialfv: face/pname/valor inválidos */
    p[0] = 1; p[1] = 1; p[2] = 1; p[3] = 1;
    a[0] = 0x0405; a[1] = 0x1201; a[2] = (uint64_t)(uintptr_t)p;
    gl_call(ctx, "glMaterialfv", a, 3);         /* GL_BACK: fora do subset */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x0009; a[1] = 0x1201;
    gl_call(ctx, "glMaterialfv", a, 3);         /* face inválida */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x0404; a[1] = 0x1601;
    p[0] = 200.0f;
    gl_call(ctx, "glMaterialfv", a, 3);         /* SHININESS fora de [0,128] */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    p[0] = 32.0f;
    gl_call(ctx, "glMaterialfv", a, 3);         /* SHININESS válido */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
    a[1] = 0x1602;
    p[0] = 1; p[1] = 1; p[2] = 1; p[3] = 1;
    gl_call(ctx, "glMaterialfv", a, 3);         /* AMBIENT_AND_DIFFUSE */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
    a[0] = 0x0004;
    gl_call(ctx, "glBegin", a, 1);
    a[0] = 0x0404; a[1] = 0x1201;
    gl_call(ctx, "glMaterialfv", a, 3);         /* dentro de glBegin */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x502);
    gl_call(ctx, "glEnd", NULL, 0);

    /* glNormalPointer: tipo/stride inválidos */
    a[0] = 0x140A; a[1] = 0; a[2] = (uint64_t)(uintptr_t)s;
    gl_call(ctx, "glNormalPointer", a, 3);      /* GL_DOUBLE */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x1406; a[1] = 8;
    gl_call(ctx, "glNormalPointer", a, 3);      /* stride < 12 */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    a[1] = 0;
    gl_call(ctx, "glNormalPointer", a, 3);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* glBlendFunc: fatores inválidos + fora de glBegin */
    a[0] = 0x0306; a[1] = 0x0303;
    gl_call(ctx, "glBlendFunc", a, 2);          /* GL_DST_COLOR: fora */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x0302; a[1] = 0x0309;
    gl_call(ctx, "glBlendFunc", a, 2);          /* dst inválido */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x0302; a[1] = 0x0303;
    gl_call(ctx, "glBlendFunc", a, 2);          /* ok */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* caps: GL_LIGHT0-7/ BLEND ok; GL_LIGHT8/FOG/CULL_FACE fora */
    a[0] = 0x4001;
    gl_call(ctx, "glEnable", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
    a[0] = 0x0BE2;
    gl_call(ctx, "glEnable", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
    a[0] = 0x4008;
    gl_call(ctx, "glDisable", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x0B60;
    gl_call(ctx, "glDisable", a, 1);            /* GL_FOG — âncora honesta */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x8075;
    gl_call(ctx, "glEnableClientState", a, 1);  /* GL_NORMAL_ARRAY */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    pr_win32_destroy(ctx);
    (void)buf;
}

static void test_gl4_light_render(void) {
    printf("-- test_gl4_light_render (Gouraud: N.L, ambiente, especular)\n");
    pr_win32_ctx* ctx = pr_win32_create(NULL);
    CHECK(ctx != NULL);
    size_t sz = 0;
    uint8_t* s = (uint8_t*)pr_win32_scratch(ctx, &sz);
    uint8_t* buf = s + 256;
    uint64_t dc = 0;
    gl_setup(ctx, &dc);
    float* p = (float*)(void*)(s + 32);
    uint64_t a[12];
    memset(a, 0, sizeof(a));
    a[0] = 0; a[1] = 0; a[2] = 320; a[3] = 240;
    gl_call(ctx, "glViewport", a, 4);
    setup_light(ctx);

    /* N=(0,0,1): N.L=1 -> (255,0,0) | N=(0.6,0,0.8): 0.8 -> (204,0,0)
     * | N=(0,0,-1): 0 -> (0,0,0) — centro (160,120), quad ±0.5 */
    float n1[3] = {0, 0, 1}, n2[3] = {0.6f, 0, 0.8f}, n3[3] = {0, 0, -1};
    clear_all(ctx);
    draw_quad_n(ctx, 0.5f, n1);
    CHECK(read_px(ctx, buf, 160, 120) == 0x00FF0000u);
    clear_all(ctx);
    draw_quad_n(ctx, 0.5f, n2);
    CHECK(read_px(ctx, buf, 160, 120) == 0x00CC0000u);
    clear_all(ctx);
    draw_quad_n(ctx, 0.5f, n3);
    CHECK(read_px(ctx, buf, 160, 120) == 0x00000000u);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* ambiente: emissão 0 + 0.2·mat.amb(1) + light.amb(0.25)·mat.amb(1)
     * = 0.45 -> (115,115,115) mesmo de costas (N=(0,0,-1)) */
    a[0] = 0x0B50;
    gl_call(ctx, "glDisable", a, 1);            /* ajustar estado sem luz */
    p[0] = 0.25f; p[1] = 0.25f; p[2] = 0.25f; p[3] = 1;
    a[0] = 0x4000; a[1] = 0x1200; a[2] = (uint64_t)(uintptr_t)p;
    gl_call(ctx, "glLightfv", a, 3);
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 1;
    a[1] = 0x1201;
    gl_call(ctx, "glLightfv", a, 3);            /* difusa 0 */
    p[0] = 1; p[1] = 1; p[2] = 1; p[3] = 1;
    a[0] = 0x0404; a[1] = 0x1200;
    gl_call(ctx, "glMaterialfv", a, 3);         /* mat amb 1 */
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 1;
    a[1] = 0x1201;
    gl_call(ctx, "glMaterialfv", a, 3);         /* mat dif 0 */
    a[0] = 0x0B50;
    gl_call(ctx, "glEnable", a, 1);
    clear_all(ctx);
    draw_quad_n(ctx, 0.5f, n3);
    CHECK(read_px(ctx, buf, 160, 120) == 0x00737373u);   /* 0x73 = 115 */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* especular Blinn: N=L=v=(0,0,1), shine 32, mat.spec 0.5, light.spec 1
     * difusa/ambiente 0 -> pow(1,32)·1·0.5 = 0.5 -> (128,128,128) */
    a[0] = 0x0B50;
    gl_call(ctx, "glDisable", a, 1);
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 1;
    a[0] = 0x4000; a[1] = 0x1200; a[2] = (uint64_t)(uintptr_t)p;
    gl_call(ctx, "glLightfv", a, 3);
    a[1] = 0x1201;
    gl_call(ctx, "glLightfv", a, 3);
    p[0] = 1; p[1] = 1; p[2] = 1; p[3] = 1;
    a[1] = 0x1202;
    gl_call(ctx, "glLightfv", a, 3);
    a[0] = 0x0404; a[1] = 0x1200;
    p[0] = 0; p[1] = 0; p[2] = 0; p[3] = 1;
    gl_call(ctx, "glMaterialfv", a, 3);
    a[1] = 0x1201;
    gl_call(ctx, "glMaterialfv", a, 3);
    p[0] = 0.5f; p[1] = 0.5f; p[2] = 0.5f; p[3] = 1;
    a[1] = 0x1202;
    gl_call(ctx, "glMaterialfv", a, 3);
    p[0] = 32.0f;
    a[1] = 0x1601;
    gl_call(ctx, "glMaterialfv", a, 3);
    a[0] = 0x0B50;
    gl_call(ctx, "glEnable", a, 1);
    clear_all(ctx);
    draw_quad_n(ctx, 0.5f, n1);
    CHECK(read_px(ctx, buf, 160, 120) == 0x00808080u);   /* 0x80 = 128 */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    pr_win32_destroy(ctx);
}

static void test_gl4_blend_render(void) {
    printf("-- test_gl4_blend_render (fatores + alpha + z)\n");
    pr_win32_ctx* ctx = pr_win32_create(NULL);
    CHECK(ctx != NULL);
    size_t sz = 0;
    uint8_t* s = (uint8_t*)pr_win32_scratch(ctx, &sz);
    uint8_t* buf = s + 256;
    uint64_t dc = 0;
    gl_setup(ctx, &dc);
    uint64_t a[12];

    /* dst VERMELHO opaco; src AZUL α=0.25 (binário exato) -> (191, 0, 64) */
    memset(a, 0, sizeof(a));
    a[0] = 0x0BE2;
    gl_call(ctx, "glEnable", a, 1);
    a[0] = 0x0302; a[1] = 0x0303;
    gl_call(ctx, "glBlendFunc", a, 2);
    clear_all(ctx);
    a[0] = fbits(1); a[1] = fbits(0); a[2] = fbits(0); a[3] = fbits(1);
    gl_call(ctx, "glColor4f", a, 4);            /* dst VERMELHO opaco */
    draw_quad_n(ctx, 0.5f, (const float[3]){0, 0, 1});
    CHECK(read_px(ctx, buf, 160, 120) == 0x00FF0000u);
    /* glColor3f NÃO zera o alpha (GL real): glColor4f(0.25) + glColor3f
     * mantém α=0.25 na mistura -> (191, 0, 64) */
    a[0] = fbits(0); a[1] = fbits(0); a[2] = fbits(1); a[3] = fbits(0.25f);
    gl_call(ctx, "glColor4f", a, 4);
    a[0] = fbits(0); a[1] = fbits(0); a[2] = fbits(1);
    gl_call(ctx, "glColor3f", a, 3);
    draw_quad_n(ctx, 0.3f, (const float[3]){0, 0, 1});
    CHECK(read_px(ctx, buf, 160, 120) == 0x00BF0040u);  /* 0xBF=191, 0x40=64 */

    /* GL_ONE / GL_ZERO: src substitui -> (0, 0, 255) */
    a[0] = 0x0001; a[1] = 0x0000;
    gl_call(ctx, "glBlendFunc", a, 2);
    clear_all(ctx);
    a[0] = fbits(1); a[1] = fbits(0); a[2] = fbits(0);
    gl_call(ctx, "glColor3f", a, 3);
    draw_quad_n(ctx, 0.5f, (const float[3]){0, 0, 1});
    a[0] = fbits(0); a[1] = fbits(0); a[2] = fbits(1); a[3] = fbits(1);
    gl_call(ctx, "glColor4f", a, 4);
    draw_quad_n(ctx, 0.3f, (const float[3]){0, 0, 1});
    CHECK(read_px(ctx, buf, 160, 120) == 0x000000FFu);

    /* GL_ONE / GL_ONE: aditivo -> (255, 0, 255) */
    a[0] = 0x0001; a[1] = 0x0001;
    gl_call(ctx, "glBlendFunc", a, 2);
    clear_all(ctx);
    a[0] = fbits(1); a[1] = fbits(0); a[2] = fbits(0); a[3] = fbits(1);
    gl_call(ctx, "glColor4f", a, 4);
    draw_quad_n(ctx, 0.5f, (const float[3]){0, 0, 1});
    a[0] = fbits(0); a[1] = fbits(0); a[2] = fbits(1); a[3] = fbits(1);
    gl_call(ctx, "glColor4f", a, 4);
    draw_quad_n(ctx, 0.3f, (const float[3]){0, 0, 1});
    CHECK(read_px(ctx, buf, 160, 120) == 0x00FF00FFu);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* z-buffer × blend (projeção identidade: menor z = menor depth = perto):
     * overlay VERMELHO atrás (z=+0.5) desenhado depois do azul na frente
     * (z=-0.5) é REJEITADO (GL_LESS) -> o pixel fica azul */
    a[0] = 0x0B71;
    gl_call(ctx, "glEnable", a, 1);             /* GL_DEPTH_TEST */
    a[0] = dbits(1.0);
    gl_call(ctx, "glClearDepth", a, 1);
    a[0] = 0x4100;
    gl_call(ctx, "glClear", a, 1);
    a[0] = 0x0001; a[1] = 0x0000;
    gl_call(ctx, "glBlendFunc", a, 2);          /* opaco */
    a[0] = fbits(0); a[1] = fbits(0); a[2] = fbits(1); a[3] = fbits(1);
    gl_call(ctx, "glColor4f", a, 4);
    a[0] = 0x0004;
    gl_call(ctx, "glBegin", a, 1);
    for (int i = 0; i < 6; i++) {
        float vx = ((i == 1 || i == 2 || i == 4) ? 0.5f : -0.5f);
        float vy = ((i == 2 || i == 4 || i == 5) ? 0.5f : -0.5f);
        a[0] = fbits(vx); a[1] = fbits(vy); a[2] = fbits(-0.5f);
        gl_call(ctx, "glVertex3f", a, 3);
    }
    gl_call(ctx, "glEnd", NULL, 0);
    a[0] = 0x0302; a[1] = 0x0303;
    gl_call(ctx, "glBlendFunc", a, 2);
    a[0] = fbits(1); a[1] = fbits(0); a[2] = fbits(0); a[3] = fbits(1);
    gl_call(ctx, "glColor4f", a, 4);
    a[0] = 0x0004;
    gl_call(ctx, "glBegin", a, 1);
    for (int i = 0; i < 6; i++) {
        float vx = ((i == 1 || i == 2 || i == 4) ? 0.5f : -0.5f);
        float vy = ((i == 2 || i == 4 || i == 5) ? 0.5f : -0.5f);
        a[0] = fbits(vx); a[1] = fbits(vy); a[2] = fbits(0.5f);
        gl_call(ctx, "glVertex3f", a, 3);
    }
    gl_call(ctx, "glEnd", NULL, 0);
    CHECK(read_px(ctx, buf, 160, 120) == 0x000000FFu);  /* atrás rejeitado */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    pr_win32_destroy(ctx);
}

static void test_gl4_hello_pe(void) {
    printf("-- test_gl4_hello_pe (hello_gl4.exe)\n");
    size_t ilen = 0, dlen = 0;
    uint8_t* img = pe_real_load("Tests/PorticoRuntimeTests/data/hello_gl4.exe",
                                &ilen);
    uint8_t* dll = pe_real_load("Tests/PorticoRuntimeTests/data/hello_dll.dll",
                                &dlen);
    CHECK(img != NULL && ilen > 1024);
    CHECK(dll != NULL);
    if (!img || !dll) { free(img); free(dll); return; }

    pr_log* log = pr_log_create(1024);
    pr_peproc* p = NULL;
    CHECK(pr_peproc_create(img, ilen, log, &p) == PR_OK);
    CHECK(pr_peproc_provide_dll(p, "hello_dll.dll", dll, (uint32_t)dlen)
          == PR_OK);
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

    /* superfície WinOS (flip y_surf = 239 - y_gl) — pixels derivados */
    pr_win32_ctx* ctx = pr_peproc_win32(p);
    pr_surf* sp = pr_win32_surface(ctx);
    CHECK(sp != NULL && pr_surf_width(sp) == 320 && pr_surf_height(sp) == 240);
    uint32_t* px = sp ? pr_surf_pixels(sp) : NULL;
    CHECK(px != NULL);
    if (px) {
        CHECK(px[(239 - 85) * 320 + 105] == 0x00FF0000u);   /* N.L=1 */
        CHECK(px[(239 - 85) * 320 + 215] == 0x00CC0000u);   /* N.L=0.8 (0xCC) */
        CHECK(px[(239 - 150) * 320 + 105] == 0x00BF0040u);  /* blend (0xBF,0x40) */
        CHECK(px[(239 - 150) * 320 + 215] == 0x00CC0000u);  /* z rejeitou */
        CHECK(px[(239 - 30) * 320 + 160] == 0x00000000u);   /* fundo */
        CHECK(px[(239 - 8) * 320 + 8] == 0x00000000u);      /* fundo */
    }
    pr_peproc_destroy(p);
    free(img);
    free(dll);
}

static void test_gl4_honestidade(void) {
    printf("-- test_gl4_honestidade (GL fora do subconjunto)\n");
    pr_win32_ctx* ctx = pr_win32_create(NULL);
    CHECK(ctx != NULL);

    /* APIs GL 1.1/1.2/1.3 REAIS ainda fora do subconjunto */
    CHECK(pr_win32_lookup("opengl32.dll", "glFogf") == NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glAlphaFunc") == NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glTexImage3D") == NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glTexEnvf") == NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glMultiTexCoord2f") == NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glLightModeli") == NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glCullFace") == NULL);
    /* APIs G11 agora REAIS */
    CHECK(pr_win32_lookup("opengl32.dll", "glLightfv") != NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glMaterialfv") != NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glBlendFunc") != NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glNormal3f") != NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glNormalPointer") != NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glColor4f") != NULL);
    /* fora do catálogo → PR_ERR_RANGE (nunca sucesso falso); no PE real a
     * chamada vira EXECUTION STOPPED (stub de diagnóstico do loader) */
    uint64_t ret = 7;
    uint64_t a[4] = { 0x0B60, 0x0B62, 0, 0 };   /* glFogf(GL_FOG, ...) */
    CHECK(pr_win32_call(ctx, "opengl32.dll", "glFogf", a, 3, &ret)
          == PR_ERR_RANGE);
    CHECK(ret == 0);
    memset(a, 0, sizeof(a));
    CHECK(pr_win32_call(ctx, "opengl32.dll", "glAlphaFunc", a, 2, &ret)
          == PR_ERR_RANGE);
    /* D3D9 continua inteiramente fora */
    CHECK(pr_win32_lookup("d3d9.dll", "Direct3DCreate9") == NULL);

    pr_win32_destroy(ctx);
}

void test_gl4(void) {
    test_gl4_unidade();
    test_gl4_light_render();
    test_gl4_blend_render();
    test_gl4_hello_pe();
    test_gl4_honestidade();
}
