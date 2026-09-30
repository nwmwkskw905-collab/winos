/* test_gl3.c — GRUPO 10: TERCEIRO PE 3D + texturas + perspectiva real.
 * Matrix stack real, glFrustum, glDrawArrays, textura 2x2 com amostragem
 * GL_NEAREST/MODULATE, z-buffer e o PE hello_gl3.exe (pixels derivados). */
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

/* textura 2x2 de teste: (0,0) vermelho (1,0) verde (0,1) azul (1,1) branco */
static void load_tex2x2(pr_win32_ctx* ctx, uint8_t* slot, uint32_t* namep) {
    uint64_t a[12];
    memcpy(slot, (uint8_t[]){ 255, 0, 0,  0, 255, 0,  0, 0, 255,  255, 255, 255 }, 12);
    *namep = 0;
    memset(a, 0, sizeof(a));
    a[0] = 1;
    a[1] = (uint64_t)(uintptr_t)namep;
    gl_call(ctx, "glGenTextures", a, 2);
    CHECK(*namep != 0);
    a[0] = 0x0DE1;              /* GL_TEXTURE_2D */
    a[1] = *namep;
    gl_call(ctx, "glBindTexture", a, 2);
    a[0] = 0x0DE1; a[1] = 0x2801; a[2] = 0x2600;  /* MIN / GL_NEAREST */
    gl_call(ctx, "glTexParameteri", a, 3);
    a[0] = 0x0DE1; a[1] = 0x2800; a[2] = 0x2600;  /* MAG / GL_NEAREST */
    gl_call(ctx, "glTexParameteri", a, 3);
    memset(a, 0, sizeof(a));
    a[0] = 0x0DE1; a[1] = 0; a[2] = 3; a[3] = 2; a[4] = 2; a[5] = 0;
    a[6] = 0x1907;              /* GL_RGB */
    a[7] = 0x1401;              /* GL_UNSIGNED_BYTE */
    a[8] = (uint64_t)(uintptr_t)slot;
    gl_call(ctx, "glTexImage2D", a, 9);
    a[0] = 0x0DE1;
    gl_call(ctx, "glEnable", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
}

static void test_gl3_unidade(void) {
    printf("-- test_gl3_unidade (matrix stack + frustum + texturas)\n");
    pr_win32_ctx* ctx = pr_win32_create(NULL);
    CHECK(ctx != NULL);

    size_t sz = 0;
    uint8_t* s = (uint8_t*)pr_win32_scratch(ctx, &sz);
    CHECK(s != NULL);
    uint32_t* namep = (uint32_t*)(void*)(s + 300);
    uint64_t dc = 0;
    gl_setup(ctx, &dc);

    uint64_t a[12];
    memset(a, 0, sizeof(a));
    a[0] = 0; a[1] = 0; a[2] = 320; a[3] = 240;
    gl_call(ctx, "glViewport", a, 4);

    /* ---- pilha de matrizes REAL: overflow/underflow/começo ---- */
    a[0] = 0x1700;   /* GL_MODELVIEW */
    gl_call(ctx, "glMatrixMode", a, 1);
    /* push dentro de glBegin → INVALID_OPERATION */
    a[0] = 0x0004;
    gl_call(ctx, "glBegin", a, 1);
    gl_call(ctx, "glPushMatrix", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x502);
    gl_call(ctx, "glEnd", NULL, 0);
    /* underflow: pop sem push → GL_STACK_UNDERFLOW */
    gl_call(ctx, "glPopMatrix", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x504);
    /* profundidade do MODELVIEW = 32: push 32x ok, 33o → OVERFLOW */
    for (int i = 0; i < 32; i++) gl_call(ctx, "glPushMatrix", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
    gl_call(ctx, "glPushMatrix", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x503);
    for (int i = 0; i < 32; i++) gl_call(ctx, "glPopMatrix", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
    /* profundidade da PROJECTION = 4 */
    a[0] = 0x1701;   /* GL_PROJECTION */
    gl_call(ctx, "glMatrixMode", a, 1);
    for (int i = 0; i < 4; i++) gl_call(ctx, "glPushMatrix", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
    gl_call(ctx, "glPushMatrix", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x503);
    for (int i = 0; i < 4; i++) gl_call(ctx, "glPopMatrix", NULL, 0);
    gl_call(ctx, "glPopMatrix", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x504);

    /* ---- glFrustum: erros reais + caminho válido ---- */
    a[0] = dbits(-0.2); a[1] = dbits(0.2); a[2] = dbits(-0.15);
    a[3] = dbits(0.15); a[4] = dbits(0.0); a[5] = dbits(20.0);
    gl_call(ctx, "glFrustum", a, 6);            /* n = 0 → INVALID_VALUE */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    a[0] = dbits(1.0); a[1] = dbits(1.0); a[4] = dbits(0.5);
    gl_call(ctx, "glFrustum", a, 6);            /* l == r → INVALID_VALUE */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    a[0] = dbits(-0.2); a[1] = dbits(0.2);
    gl_call(ctx, "glFrustum", a, 6);            /* válido */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* ---- texturas: nomes, erros de parâmetros/formatos ---- */
    memset(a, 0, sizeof(a));
    a[0] = 0; a[1] = (uint64_t)(uintptr_t)namep;
    gl_call(ctx, "glGenTextures", a, 2);        /* n=0 → INVALID_VALUE */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    load_tex2x2(ctx, s, namep);                 /* nome + upload + enable */
    a[0] = 0x0DE1; a[1] = *namep;
    gl_call(ctx, "glBindTexture", a, 2);        /* mesmo nome = ok */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
    a[0] = 0x8888; a[1] = *namep;
    gl_call(ctx, "glBindTexture", a, 2);        /* target inválido */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x0DE1; a[1] = 200;
    gl_call(ctx, "glBindTexture", a, 2);        /* nome >= 64 */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    a[0] = 0x0DE1; a[1] = 0x2801; a[2] = 0x2601;  /* GL_LINEAR → fora do
                                                   * subconjunto NEAREST */
    gl_call(ctx, "glTexParameteri", a, 3);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x0DE1; a[1] = 0x2802; a[2] = 0x2900;  /* GL_CLAMP → só REPEAT */
    gl_call(ctx, "glTexParameteri", a, 3);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x0DE1; a[1] = 0x0000; a[2] = 0x2600;  /* pname desconhecido */
    gl_call(ctx, "glTexParameteri", a, 3);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    memset(a, 0, sizeof(a));
    a[0] = 0x0DE1; a[1] = 1; a[2] = 3; a[3] = 2; a[4] = 2; a[5] = 0;
    a[6] = 0x1907; a[7] = 0x1401;
    a[8] = (uint64_t)(uintptr_t)s;
    gl_call(ctx, "glTexImage2D", a, 9);         /* level != 0 */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    a[1] = 0; a[2] = 3; a[5] = 1;
    gl_call(ctx, "glTexImage2D", a, 9);         /* border != 0 */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    a[5] = 0; a[6] = 0x1908;
    gl_call(ctx, "glTexImage2D", a, 9);         /* formato != RGB */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[6] = 0x1907; a[7] = 0x140A;
    gl_call(ctx, "glTexImage2D", a, 9);         /* tipo != UNSIGNED_BYTE */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[7] = 0x1401; a[3] = 0;
    gl_call(ctx, "glTexImage2D", a, 9);         /* width = 0 */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);

    /* ---- glTexCoordPointer/glDrawArrays: validação real ---- */
    a[0] = 3; a[1] = 0x1406; a[2] = 0; a[3] = (uint64_t)(uintptr_t)s;
    gl_call(ctx, "glTexCoordPointer", a, 4);    /* size != 2 */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    a[0] = 2; a[1] = 0x140A;
    gl_call(ctx, "glTexCoordPointer", a, 4);    /* GL_DOUBLE */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[1] = 0x1406; a[2] = 4;
    gl_call(ctx, "glTexCoordPointer", a, 4);    /* stride 4 < 8 */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    a[2] = 0;
    gl_call(ctx, "glTexCoordPointer", a, 4);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
    a[0] = 0x0005; a[1] = 0; a[2] = 3;
    gl_call(ctx, "glDrawArrays", a, 3);         /* GL_TRIANGLE_STRIP fora */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x0004; a[1] = ~(uint64_t)0;
    gl_call(ctx, "glDrawArrays", a, 3);         /* first < 0 */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x501);
    a[0] = 0x0004; a[1] = 0; a[2] = 3;
    gl_call(ctx, "glDrawArrays", a, 3);         /* sem VERTEX_ARRAY */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x502);

    /* ---- glDisable + client state ---- */
    a[0] = 0x0DE1;
    gl_call(ctx, "glDisable", a, 1);            /* GL_TEXTURE_2D → ok */
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
    a[0] = 0x0B44;                              /* GL_CULL_FACE — fora (G11) */
    gl_call(ctx, "glDisable", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);
    a[0] = 0x8079;                              /* GL_EDGE_FLAG_ARRAY — fora (G11) */
    gl_call(ctx, "glEnableClientState", a, 1);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500);

    pr_win32_destroy(ctx);
}

static void test_gl3_textura_render(void) {
    printf("-- test_gl3_textura_render (amostragem + MODULATE + pilha)\n");
    pr_win32_ctx* ctx = pr_win32_create(NULL);
    CHECK(ctx != NULL);
    size_t sz = 0;
    uint8_t* s = (uint8_t*)pr_win32_scratch(ctx, &sz);
    uint8_t* buf = s + 256;
    uint32_t* namep = (uint32_t*)(void*)(s + 300);
    uint64_t dc = 0;
    gl_setup(ctx, &dc);

    uint64_t a[12];
    memset(a, 0, sizeof(a));
    a[0] = 0; a[1] = 0; a[2] = 320; a[3] = 240;
    gl_call(ctx, "glViewport", a, 4);
    a[0] = fbits(0.0f); a[1] = fbits(0.0f); a[2] = fbits(0.0f); a[3] = fbits(1.0f);
    gl_call(ctx, "glClearColor", a, 4);
    a[0] = dbits(1.0);
    gl_call(ctx, "glClearDepth", a, 1);
    a[0] = 0x4100;
    gl_call(ctx, "glClear", a, 1);
    /* matrizes identidade (orthonormal: NDC → janela direto) */
    a[0] = 0x1701;
    gl_call(ctx, "glMatrixMode", a, 1);
    gl_call(ctx, "glLoadIdentity", NULL, 0);
    a[0] = 0x1700;
    gl_call(ctx, "glMatrixMode", a, 1);
    gl_call(ctx, "glLoadIdentity", NULL, 0);

    load_tex2x2(ctx, s, namep);

    /* quad ±0.75 (cobre x 40..280, y 30..210) com cor MODULATE (1,1,0.5) */
    float (*qv)[3] = (float(*)[3])(void*)(s + 16);
    float (*quv)[2] = (float(*)[2])(void*)(s + 64);
    float (*qc)[3] = (float(*)[3])(void*)(s + 96);
    unsigned short* qidx = (unsigned short*)(void*)(s + 148);
    float vv[4][3] = { {-0.75f, -0.75f, 0}, { 0.75f, -0.75f, 0},
                       { 0.75f,  0.75f, 0}, {-0.75f,  0.75f, 0} };
    float uv[4][2] = { {0, 0}, {1, 0}, {1, 1}, {0, 1} };
    memcpy(qv, vv, 48);
    memcpy(quv, uv, 32);
    for (int i = 0; i < 4; i++) {
        qc[i][0] = 1.0f; qc[i][1] = 1.0f; qc[i][2] = 0.5f;
    }
    memcpy(qidx, (unsigned short[]){ 0, 1, 2, 0, 2, 3 }, 12);

    a[0] = 0x8074;
    gl_call(ctx, "glEnableClientState", a, 1);
    a[0] = 0x8076;
    gl_call(ctx, "glEnableClientState", a, 1);
    a[0] = 0x8078;
    gl_call(ctx, "glEnableClientState", a, 1);
    memset(a, 0, sizeof(a));
    a[0] = 3; a[1] = 0x1406; a[2] = 0; a[3] = (uint64_t)(uintptr_t)qv;
    gl_call(ctx, "glVertexPointer", a, 4);
    a[0] = 3; a[1] = 0x1406; a[2] = 0; a[3] = (uint64_t)(uintptr_t)qc;
    gl_call(ctx, "glColorPointer", a, 4);
    a[0] = 2; a[1] = 0x1406; a[2] = 0; a[3] = (uint64_t)(uintptr_t)quv;
    gl_call(ctx, "glTexCoordPointer", a, 4);
    a[0] = 0x0004; a[1] = 6; a[2] = 0x1403; a[3] = (uint64_t)(uintptr_t)qidx;
    gl_call(ctx, "glDrawElements", a, 4);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    /* centros de texel MODULADOS por (1,1,0.5): esperados derivados */
    CHECK(read_px(ctx, buf, 100, 75) == 0x00FF0000u);   /* vermelho×tint */
    CHECK(read_px(ctx, buf, 220, 75) == 0x0000FF00u);   /* verde×tint */
    CHECK(read_px(ctx, buf, 100, 165) == 0x00000080u);  /* azul×tint */
    CHECK(read_px(ctx, buf, 220, 165) == 0x00FFFF80u);  /* branco×tint */

    /* pilha MODELVIEW: pop restaura (ponto volta ao lugar) e push
     * transladado desloca a imagem 80px (0.5 NDC × 160) */
    gl_call(ctx, "glPushMatrix", NULL, 0);
    a[0] = fbits(0.5f); a[1] = fbits(0.0f); a[2] = fbits(0.0f);
    gl_call(ctx, "glTranslatef", a, 3);
    a[0] = 0x0004; a[1] = 6; a[2] = 0x1403; a[3] = (uint64_t)(uintptr_t)qidx;
    gl_call(ctx, "glDrawElements", a, 4);
    CHECK(read_px(ctx, buf, 180, 75) == 0x00FF0000u);   /* deslocado */
    gl_call(ctx, "glPopMatrix", NULL, 0);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
    a[0] = 0x4000;
    gl_call(ctx, "glClear", a, 1);
    a[0] = 0x0004; a[1] = 6; a[2] = 0x1403; a[3] = (uint64_t)(uintptr_t)qidx;
    gl_call(ctx, "glDrawElements", a, 4);
    CHECK(read_px(ctx, buf, 100, 75) == 0x00FF0000u);   /* restaurado */

    /* glDisable(GL_TEXTURE_2D) → cor chapada do vértice (1,1,0.5) */
    a[0] = 0x0DE1;
    gl_call(ctx, "glDisable", a, 1);
    a[0] = 0x4000;
    gl_call(ctx, "glClear", a, 1);
    a[0] = 0x0004; a[1] = 6; a[2] = 0x1403; a[3] = (uint64_t)(uintptr_t)qidx;
    gl_call(ctx, "glDrawElements", a, 4);
    CHECK(read_px(ctx, buf, 100, 75) == 0x00FFFF80u);
    CHECK(read_px(ctx, buf, 220, 165) == 0x00FFFF80u);
    CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

    pr_win32_destroy(ctx);
}

static void test_gl3_hello_pe(void) {
    printf("-- test_gl3_hello_pe (hello_gl3.exe)\n");
    size_t ilen = 0, dlen = 0;
    uint8_t* img = pe_real_load("Tests/PorticoRuntimeTests/data/hello_gl3.exe",
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
    for (int i = 0; i < 200000 && !pr_peproc_exited(p); i++)
        pr_peproc_step(p, 10000, &exec);
    CHECK(pr_peproc_exited(p) == 1);
    CHECK(pr_peproc_exit_code(p) == 42);

    /* frame apresentado na superfície WinOS (flip y_surf = 239 - y_gl) */
    pr_win32_ctx* ctx = pr_peproc_win32(p);
    pr_surf* sp = pr_win32_surface(ctx);
    CHECK(sp != NULL && pr_surf_width(sp) == 320 && pr_surf_height(sp) == 240);
    uint32_t* px = sp ? pr_surf_pixels(sp) : NULL;
    CHECK(px != NULL);
    if (px) {
        CHECK(px[(239 - 90) * 320 + 86] == 0x00FF0000u);   /* texel vermelho */
        CHECK(px[(239 - 45) * 320 + 235] == 0x0000FF00u);  /* texel verde */
        CHECK(px[(239 - 181) * 320 + 93] == 0x000000FFu);  /* texel azul */
        CHECK(px[(239 - 179) * 320 + 219] == 0x00FFFFFFu); /* texel branco */
        CHECK(px[(239 - 102) * 320 + 166] == 0x00FF8000u); /* tri oclui (z) */
        CHECK(px[(239 - 8) * 320 + 8] == 0x00000000u);     /* fundo preto */
    }
    pr_peproc_destroy(p);
    free(img);
    free(dll);
}

static void test_gl3_honestidade(void) {
    printf("-- test_gl3_honestidade (GL fora do subconjunto)\n");
    pr_win32_ctx* ctx = pr_win32_create(NULL);
    CHECK(ctx != NULL);

    /* GL 1.1/1.2/1.3 REAIS que continuam fora do subconjunto */
    CHECK(pr_win32_lookup("opengl32.dll", "glTexImage3D") == NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glLightfv") != NULL);  /* REAL G11 */
    CHECK(pr_win32_lookup("opengl32.dll", "glFogf") == NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glTexEnvf") == NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glMultiTexCoord2f") == NULL);
    /* APIs G10 agora REAIS */
    CHECK(pr_win32_lookup("opengl32.dll", "glPushMatrix") != NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glFrustum") != NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glTexImage2D") != NULL);
    CHECK(pr_win32_lookup("opengl32.dll", "glDrawArrays") != NULL);
    /* fora do catálogo → PR_ERR_RANGE (nunca sucesso falso); no PE real a
     * chamada vira EXECUTION STOPPED (stub de diagnóstico do loader) */
    uint64_t ret = 7;
    uint64_t a[4] = { 0x0B60, 0x0B62, 0, 0 };   /* glFogf(GL_FOG, ...) */
    CHECK(pr_win32_call(ctx, "opengl32.dll", "glFogf", a, 3, &ret)
          == PR_ERR_RANGE);
    CHECK(ret == 0);
    memset(a, 0, sizeof(a));
    CHECK(pr_win32_call(ctx, "opengl32.dll", "glTexImage3D", a, 9, &ret)
          == PR_ERR_RANGE);
    /* D3D9 continua inteiramente fora (decisão G8/G9 preservada) */
    CHECK(pr_win32_lookup("d3d9.dll", "Direct3DCreate9") != NULL); /* FASE 7: D3D9 stub honesto UNIMPLEMENTED → EXECUTION STOPPED (antes NULL, agora detectado) */

    pr_win32_destroy(ctx);
}

void test_gl3(void) {
    test_gl3_unidade();
    test_gl3_textura_render();
    test_gl3_hello_pe();
    test_gl3_honestidade();
}
