/* test_gl6.c — GRUPO 13: glGetString (consultas do runtime) + PE hello_gl6.exe.
 * Valores reais e estáveis (backend SoftRaster/Metal — sem GPU Windows),
 * erros GL reais (enum inválido 0x500; em begin 0x502), ponteiro de convidado
 * estável, honestidade das consultas ainda ausentes (glGetIntegerv/glGetDoublev
 * + glFogf), e uso observável do resultado (cor derivada das strings). */
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

static int guest_str_eq(pr_win32_ctx* ctx, uint64_t g, const char* exp) {
    const char* h = (const char*)pr_win32_ptr(ctx, g, strlen(exp) + 1);
    CHECK(h != NULL);
    return h && strcmp(h, exp) == 0;
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

static void test_gl6_unidade(void) {
    printf("-- test_gl6_unidade (glGetString: valores/erros/estabilidade)\n");
    pr_win32_ctx* gl = pr_win32_create(NULL);
    CHECK(gl != NULL);
    uint64_t dc = 0;
    uint64_t a[8];
    gl_setup(gl, &dc);

    /* os 4 enums com conteúdo EXATO (não no-op, não placeholder) */
    a[0] = 0x1F00;
    uint64_t vend = gl_call(gl, "glGetString", a, 1);
    CHECK(vend != 0);
    CHECK(guest_str_eq(gl, vend, "Portico"));
    a[0] = 0x1F01;
    uint64_t rend = gl_call(gl, "glGetString", a, 1);
    CHECK(rend != 0);
    CHECK(guest_str_eq(gl, rend, "Portico SoftRaster/Metal"));
    a[0] = 0x1F02;
    uint64_t vers = gl_call(gl, "glGetString", a, 1);
    CHECK(vers != 0);
    CHECK(guest_str_eq(gl, vers, "1.1 Portico Subset"));
    a[0] = 0x1F03;
    uint64_t exts = gl_call(gl, "glGetString", a, 1);
    CHECK(exts != 0);
    CHECK(guest_str_eq(gl, exts, ""));

    /* ponteiro ESTÁVEL: duas consultas do mesmo pname = mesmo endereço */
    a[0] = 0x1F00;
    CHECK(gl_call(gl, "glGetString", a, 1) == vend);
    a[0] = 0x1F01;
    CHECK(gl_call(gl, "glGetString", a, 1) == rend);

    /* enum desconhecido -> NULL + GL_INVALID_ENUM (0x500) */
    a[0] = 0x9999;
    CHECK(gl_call(gl, "glGetString", a, 1) == 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x500);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);

    /* dentro de glBegin -> NULL + GL_INVALID_OPERATION (0x502) */
    gl_call(gl, "glBegin", (uint64_t[1]){ 0x0004 }, 1);
    a[0] = 0x1F02;
    CHECK(gl_call(gl, "glGetString", a, 1) == 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x502);
    gl_call(gl, "glEnd", NULL, 0);

    /* wrapper: n < 1 -> PR_ERR_INVALID */
    uint64_t ret = 7;
    CHECK(pr_win32_call(gl, "opengl32.dll", "glGetString", NULL, 0, &ret)
          == PR_ERR_INVALID);

    /* honestidade: ainda ausentes (não implementadas por antecipação) */
    memset(a, 0, sizeof(a));
    ret = 9;
    CHECK(pr_win32_call(gl, "opengl32.dll", "glGetFloatv", a, 2, &ret)
          == PR_ERR_RANGE);
    ret = 9;
    CHECK(pr_win32_call(gl, "opengl32.dll", "glGetDoublev", a, 2, &ret)
          == PR_ERR_RANGE);
    ret = 9;
    CHECK(pr_win32_call(gl, "opengl32.dll", "glFogf", a, 2, &ret)
          == PR_ERR_RANGE);

    /* comportamento SEM contexto GL (wgl não chamado): NULL, sem crash */
    {
        pr_win32_ctx* bare = pr_win32_create(NULL);
        CHECK(bare != NULL);
        uint64_t br = 5;
        CHECK(pr_win32_call(bare, "opengl32.dll", "glGetString", a, 1, &br)
              == PR_OK);
        CHECK(br == 0);
    }
}

static void test_gl6_render(void) {
    printf("-- test_gl6_render (uso observável: cor derivada das strings)\n");
    pr_win32_ctx* gl = pr_win32_create(NULL);
    CHECK(gl != NULL);
    uint64_t dc = 0;
    uint64_t a[8];
    size_t ssz = 0;
    uint8_t* s = (uint8_t*)pr_win32_scratch(gl, &ssz);
    uint8_t* buf = s + 256;
    gl_setup(gl, &dc);

    /* mesma derivação do PE: r=match vendor, g=len(renderer)/255, b=exts */
    a[0] = 0x1F00;
    uint64_t vend = gl_call(gl, "glGetString", a, 1);
    a[0] = 0x1F01;
    uint64_t rend = gl_call(gl, "glGetString", a, 1);
    a[0] = 0x1F03;
    uint64_t exts = gl_call(gl, "glGetString", a, 1);
    const char* rv = (const char*)pr_win32_ptr(gl, vend, 8);   /* "Portico" */
    const char* rr = (const char*)pr_win32_ptr(gl, rend, 25);  /* +NUL */
    const char* re = (const char*)pr_win32_ptr(gl, exts, 1);   /* "" */
    CHECK(rv && rr && re);
    if (!rv || !rr || !re) return;
    float fr = (strcmp(rv, "Portico") == 0) ? 1.0f : 0.0f;
    float fg = (float)strlen(rr) / 255.0f;
    float fb = (re[0] == 0) ? 0.0f : 1.0f;

    a[0] = 0x1701;
    gl_call(gl, "glMatrixMode", a, 1);
    gl_call(gl, "glLoadIdentity", NULL, 0);
    a[0] = dbits(-1.0); a[1] = dbits(1.0); a[2] = dbits(-1.0);
    a[3] = dbits(1.0);  a[4] = dbits(-1.0); a[5] = dbits(1.0);
    gl_call(gl, "glOrtho", a, 6);
    a[0] = 0x1700;
    gl_call(gl, "glMatrixMode", a, 1);
    gl_call(gl, "glLoadIdentity", NULL, 0);

    a[0] = fbits(0); a[1] = fbits(0); a[2] = fbits(0); a[3] = fbits(1);
    gl_call(gl, "glClearColor", a, 4);
    a[0] = 0x4000;
    gl_call(gl, "glClear", a, 1);
    a[0] = fbits(fr); a[1] = fbits(fg); a[2] = fbits(fb);
    gl_call(gl, "glColor3f", a, 3);
    gl_call(gl, "glBegin", (uint64_t[1]){ 0x0004 }, 1);
    {
        const float v[6][2] = {
            {-0.5f, -0.5f}, {0.5f, -0.5f}, {0.5f, 0.5f},
            {-0.5f, -0.5f}, {0.5f, 0.5f}, {-0.5f, 0.5f}
        };
        for (int i = 0; i < 6; i++) {
            a[0] = fbits(v[i][0]); a[1] = fbits(v[i][1]); a[2] = fbits(0);
            gl_call(gl, "glVertex3f", a, 3);
        }
    }
    gl_call(gl, "glEnd", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);

    CHECK(read_px(gl, buf, 160, 120) == 0x00FF1800u);   /* (255, 24, 0) */
    CHECK(read_px(gl, buf, 200, 100) == 0x00FF1800u);
    CHECK(read_px(gl, buf,  20,  20) == 0x00000000u);
    CHECK(read_px(gl, buf, 300, 220) == 0x00000000u);
}

static void test_gl6_hello_pe(void) {
    printf("-- test_gl6_hello_pe (hello_gl6.exe)\n");
    size_t ilen = 0;
    uint8_t* img = pe_real_load("Tests/PorticoRuntimeTests/data/hello_gl6.exe",
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
        printf("[LOG-hello_gl6] %llu entradas de log\n",
               (unsigned long long)pr_log_count(log));
    CHECK(rc == 42);   /* glGetString real + cvtsi2ss + pixels derivados */

    pr_surf* sp = pr_win32_surface(ctx);
    CHECK(sp != NULL && pr_surf_width(sp) == 320);
    uint32_t* px = sp ? pr_surf_pixels(sp) : NULL;
    CHECK(px != NULL);
    if (px) {
        /* superfície (flip: y_surf = 239 - y_gl) */
        CHECK(px[(239 - 120) * 320 + 160] == 0x00FF1800u);  /* centro */
        CHECK(px[(239 - 100) * 320 + 200] == 0x00FF1800u);  /* interno */
        CHECK(px[(239 -  20) * 320 +  20] == 0x00000000u);  /* fora */
        CHECK(px[(239 - 220) * 320 + 300] == 0x00000000u);  /* fora */
    }
    pr_peproc_destroy(p);
    free(img);
}

void test_gl6(void) {
    test_gl6_unidade();
    test_gl6_render();
    test_gl6_hello_pe();
}
