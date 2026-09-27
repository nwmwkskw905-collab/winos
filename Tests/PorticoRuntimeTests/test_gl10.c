/* test_gl10.c — GRUPO 17: glDeleteTextures (par real de glGenTextures).
 * Descoberto por execução de hello_gl10.exe (STOP "Unsupported Win32 API |
 * OPENGL32.dll | glDeleteTextures" ANTES da implementação — ver RELATORIO_G17).
 * Casos: válido observável (exclusão real + desvinculação + reciclagem de
 * slot + nova textura), nomes ignorados (0/fora da faixa/não criados/repetido),
 * n inválido, ponteiro inválido, bloco curto, overflow do bloco de nomes,
 * estado sem contexto, preservação do ciclo gen/bind/teximage/draw. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pt_util.h"
#include "portico/pr_win32.h"
#include "portico/pr_gl.h"
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
    a[0] = 0x1701;
    gl_call(gl, "glMatrixMode", a, 1);
    gl_call(gl, "glLoadIdentity", NULL, 0);
    gl_call(gl, "glOrtho", ad, 6);
    a[0] = 0x1700;
    gl_call(gl, "glMatrixMode", a, 1);
    gl_call(gl, "glLoadIdentity", NULL, 0);
}

/* triângulo texturizado fixo (x_gl 10..60, y_gl 150..200; uv 0..1) */
static void draw_tri(pr_win32_ctx* gl, uint8_t* scr) {
    static const float v[9] = { 10, 150, 0, 60, 150, 0, 10, 200, 0 };
    static const float t[6] = { 0, 0, 1, 0, 0, 1 };
    uint64_t a[4];
    memcpy(scr + 0, v, sizeof v);
    memcpy(scr + 48, t, sizeof t);
    a[0] = 0x8074; gl_call(gl, "glEnableClientState", a, 1);
    a[0] = 0x8078; gl_call(gl, "glEnableClientState", a, 1);
    a[0] = 3; a[1] = 0x1406; a[2] = 0; a[3] = (uint64_t)(uintptr_t)scr;
    gl_call(gl, "glVertexPointer", a, 4);
    a[0] = 2; a[3] = (uint64_t)(uintptr_t)(scr + 48);
    gl_call(gl, "glTexCoordPointer", a, 4);
    a[0] = 0x0004; a[1] = 0; a[2] = 3;
    gl_call(gl, "glDrawArrays", a, 3);
    gl_call(gl, "glFinish", NULL, 0);
    a[0] = 0x8078; gl_call(gl, "glDisableClientState", a, 1);
    a[0] = 0x8074; gl_call(gl, "glDisableClientState", a, 1);
}

static void clear_black(pr_win32_ctx* gl) {
    uint64_t a[4] = { 0, 0, 0, fbits(1.0f) };
    gl_call(gl, "glClearColor", a, 4);
    a[0] = 0x4000;
    gl_call(gl, "glClear", a, 1);
    gl_call(gl, "glFinish", NULL, 0);
}

static void del_tex(pr_win32_ctx* gl, int32_t n, uint64_t names_addr) {
    uint64_t a[2] = { (uint64_t)(int64_t)n, names_addr };
    gl_call(gl, "glDeleteTextures", a, 2);
}

void test_gl10(void) {
    printf("== G17: glDeleteTextures (ciclo de vida de objetos de textura) ==\n");
    pr_win32_ctx* gl = pr_win32_create(NULL);
    CHECK(gl != NULL);
    uint64_t dc = 0;
    gl_setup(gl, &dc);
    size_t ssz = 0;
    uint8_t* scr = (uint8_t*)pr_win32_scratch(gl, &ssz);
    CHECK(scr != NULL && ssz >= 512);
    uint8_t* px4 = scr + 128;
    uint32_t* names = (uint32_t*)(void*)(scr + 96);
    uint8_t* texels = scr + 80;

    setup_ortho(gl);
    uint64_t a[9];
    a[0] = 0x0DE1; gl_call(gl, "glEnable", a, 1);      /* GL_TEXTURE_2D */
    a[0] = fbits(1); a[1] = fbits(1); a[2] = fbits(1);
    gl_call(gl, "glColor3f", a, 3);

    /* textura azul|branco 2x1 */
    memcpy(texels, (unsigned char[]){ 0, 0, 255, 255, 255, 255 }, 6);

    /* ---- 1) válido observável: exclusão real + desvinculação ---- */
    clear_black(gl);
    a[0] = 1; a[1] = (uint64_t)(uintptr_t)names;
    gl_call(gl, "glGenTextures", a, 2);
    a[0] = 0x0DE1; a[1] = names[0];
    gl_call(gl, "glBindTexture", a, 2);
    a[0] = 0x0DE1; a[1] = 0; a[2] = 3; a[3] = 2; a[4] = 1; a[5] = 0;
    a[6] = 0x1907; a[7] = 0x1401; a[8] = (uint64_t)(uintptr_t)texels;
    gl_call(gl, "glTexImage2D", a, 9);
    draw_tri(gl, scr);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 20, 160) == 0x0000FFu);      /* texel azul */

    del_tex(gl, 1, (uint64_t)(uintptr_t)names);            /* exclui a VINCULADA */
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    draw_tri(gl, scr);
    /* exclusão real: sem objeto de textura -> cor base branca (se fosse
     * no-op, o triângulo continuaria AZUL) */
    CHECK(read_gl_px(gl, px4, 20, 160) == 0xFFFFFFu);

    /* ---- 2) reciclagem de slot + ciclo completo de novo ---- */
    {
        uint32_t old_name = names[0];
        a[0] = 1; a[1] = (uint64_t)(uintptr_t)(names + 1);
        gl_call(gl, "glGenTextures", a, 2);
        CHECK(names[1] == old_name);                       /* slot livre reusado */
    }
    memcpy(texels, (unsigned char[]){ 255, 0, 0, 255, 255, 255 }, 6);
    a[0] = 0x0DE1; a[1] = names[1];
    gl_call(gl, "glBindTexture", a, 2);
    a[0] = 0x0DE1; a[1] = 0; a[2] = 3; a[3] = 2; a[4] = 1; a[5] = 0;
    a[6] = 0x1907; a[7] = 0x1401; a[8] = (uint64_t)(uintptr_t)texels;
    gl_call(gl, "glTexImage2D", a, 9);
    clear_black(gl);
    draw_tri(gl, scr);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 20, 160) == 0xFF0000u);      /* nova textura viva */

    /* ---- 3) nomes ignorados (GL 1.1): 0, fora da faixa, não criados,
     *         repetido — sem erro e sem destruir a textura atual ---- */
    {
        uint32_t* aux = (uint32_t*)(void*)(scr + 112);
        aux[0] = 0;
        del_tex(gl, 1, (uint64_t)(uintptr_t)aux);
        CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
        aux[0] = 9999;
        del_tex(gl, 1, (uint64_t)(uintptr_t)aux);
        CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
        aux[0] = 7;
        del_tex(gl, 1, (uint64_t)(uintptr_t)aux);
        CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    }
    clear_black(gl);
    draw_tri(gl, scr);
    CHECK(read_gl_px(gl, px4, 20, 160) == 0xFF0000u);      /* atual intacta */
    {   /* repetido no mesmo array: 1ª exclui, 2ª ignorada (array no guest) */
        uint32_t* aux = (uint32_t*)(void*)(scr + 112);
        aux[0] = names[1];
        aux[1] = names[1];
        del_tex(gl, 2, (uint64_t)(uintptr_t)aux);
        CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    }
    draw_tri(gl, scr);
    CHECK(read_gl_px(gl, px4, 20, 160) == 0xFFFFFFu);      /* excluída de fato */

    /* ---- 4) n inválido -> GL_INVALID_VALUE no motor (espelho do gen) ---- */
    pr_gl_delete_textures(pr_win32_gl_state(gl), -1,
                          (const uint32_t*)(void*)(scr + 96));
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x501u);
    pr_gl_delete_textures(pr_win32_gl_state(gl), 0,
                          (const uint32_t*)(void*)(scr + 96));
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x501u);

    /* ---- 5) ponteiro inválido / bloco curto / overflow (wrapper) ---- */
    {
        uint64_t ar[2], ret = 123;
        ar[0] = 1; ar[1] = 0xFFFFFFFFFFFFF000ull;
        CHECK(pr_win32_call(gl, "opengl32.dll", "glDeleteTextures", ar, 2,
                            &ret) == PR_ERR_RANGE);
        ar[0] = 8; ar[1] = (uint64_t)(uintptr_t)(scr + 500);  /* 32 pedidos, 12 ok */
        CHECK(pr_win32_call(gl, "opengl32.dll", "glDeleteTextures", ar, 2,
                            &ret) == PR_ERR_RANGE);
        ar[0] = UINT64_MAX / 4u + 1u; ar[1] = (uint64_t)(uintptr_t)names;
        CHECK(pr_win32_call(gl, "opengl32.dll", "glDeleteTextures", ar, 2,
                            &ret) == PR_ERR_RANGE);
        ar[0] = 1; ar[1] = (uint64_t)(uintptr_t)names;
        CHECK(pr_win32_call(gl, "opengl32.dll", "glDeleteTextures", ar, 1,
                            &ret) == PR_ERR_INVALID);
    }

    /* ---- 6) estado: objeto NÃO vinculado não mexe no binding atual ---- */
    memcpy(texels, (unsigned char[]){ 0, 0, 255, 255, 255, 255 }, 6);
    a[0] = 1; a[1] = (uint64_t)(uintptr_t)names;
    gl_call(gl, "glGenTextures", a, 2);
    a[0] = 1; a[1] = (uint64_t)(uintptr_t)(names + 1);
    gl_call(gl, "glGenTextures", a, 2);
    a[0] = 0x0DE1; a[1] = names[0];                        /* vincula A */
    gl_call(gl, "glBindTexture", a, 2);
    a[0] = 0x0DE1; a[1] = 0; a[2] = 3; a[3] = 2; a[4] = 1; a[5] = 0;
    a[6] = 0x1907; a[7] = 0x1401; a[8] = (uint64_t)(uintptr_t)texels;
    gl_call(gl, "glTexImage2D", a, 9);
    del_tex(gl, 1, (uint64_t)(uintptr_t)(names + 1));      /* exclui B (não vinculada) */
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    clear_black(gl);
    draw_tri(gl, scr);
    CHECK(read_gl_px(gl, px4, 20, 160) == 0x0000FFu);      /* A segue viva */

    /* ---- 7) sem contexto GL atual: silencioso (espelho do gen) ---- */
    {
        pr_win32_ctx* gl2 = pr_win32_create(NULL);
        CHECK(gl2 != NULL);
        size_t s2sz = 0;
        uint8_t* scr2 = (uint8_t*)pr_win32_scratch(gl2, &s2sz);
        CHECK(scr2 != NULL);
        uint64_t ar[2] = { 1, (uint64_t)(uintptr_t)scr2 }, ret = 0;
        CHECK(pr_win32_call(gl2, "opengl32.dll", "glDeleteTextures", ar, 2,
                            &ret) == PR_OK);               /* sem MakeCurrent */
        pr_win32_destroy(gl2);
    }

    /* ---- hello_gl10.exe: cena em 2 fases a 42 (o delete exercitado no PE) */
    {
        size_t ilen = 0;
        uint8_t* img = pe_real_load(
            "Tests/PorticoRuntimeTests/data/hello_gl10.exe", &ilen);
        CHECK(img != NULL && ilen > 0);
        if (!img) return;
        pr_log* log = pr_log_create(1024);
        pr_peproc* p = NULL;
        CHECK(pr_peproc_create(img, ilen, log, &p) == PR_OK);
        CHECK(p != NULL);
        if (!p) { free(img); return; }
        CHECK(pr_peproc_prepare(p) == PR_OK);
        uint64_t exec = 0;
        int i = 0;
        while (i < 5000 && !pr_peproc_exited(p)) {
            if (pr_peproc_step(p, 10000, &exec) != PR_OK) break;
            i++;
        }
        CHECK(pr_peproc_exited(p));
        CHECK(pr_peproc_exit_code(p) == 42u);
        pr_surf* sp = pr_win32_surface(pr_peproc_win32(p));
        CHECK(sp != NULL);
        if (sp) {   /* quadro final (fase 2) */
            CHECK(surf_px(sp, 115, 80) == 0x00FFFFFFu);    /* residual s/ textura */
            CHECK(surf_px(sp, 245, 80) == 0x0000FF00u);    /* tex2 verde */
            CHECK(surf_px(sp, 300, 110) == 0x00FFFF00u);   /* tex2 amarelo */
            CHECK(surf_px(sp, 130, 110) == 0x00FF0000u);   /* caixa iluminada */
            {
                uint32_t got = surf_px(sp, 45, 45);
                CHECK(got == 0x007F7F00u || got == 0x00808000u);
            }
            CHECK(surf_px(sp, 275, 30) == 0x0000FFFFu);    /* marcador fase 2 */
            CHECK(surf_px(sp, 300, 200) == 0x00000000u);
        }
        pr_peproc_destroy(p);
        pr_log_destroy(log);
        free(img);
    }

    /* gl_setup finish */
    {
        uint64_t ar[2] = { 0, 0 }, ret = 0;
        pr_win32_call(gl, "opengl32.dll", "wglMakeCurrent", ar, 2, &ret);
        ar[0] = dc; pr_win32_call(gl, "user32.dll", "ReleaseDC", ar, 2, &ret);
    }
    pr_win32_destroy(gl);
}
