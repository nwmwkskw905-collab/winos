/* -----------------------------------------------------------
 * Portico Runtime — bateria de testes (C11, sem dependências iOS).
 * GRUPO 14: glGetIntegerv (5 pnames reais), glTexCoord2f (imediato) e
 * PCMPEQD (SSE, coberto via código real do PE hello_gl7).
 *
 * Valores = capacidades REAIS do runtime (confirmadas no código):
 *   GL_VIEWPORT (0x0BA2) = estado real (default 0,0,320,240);
 *   GL_MAX_TEXTURE_SIZE (0x0D33) = 1024 (tex_image2d: w>1024 -> 0x501);
 *   GL_MAX_LIGHTS (0x0D31) = 8 (lights[8]);
 *   GL_MAX_MODELVIEW_STACK_DEPTH (0x0D36) = 32 (mv_stack[32]);
 *   GL_MAX_PROJECTION_STACK_DEPTH (0x0D38) = 4 (pj_stack[4]).
 * Cada bloco mede o RESULTADO real: valor, saída intacta em erro, provas
 * de limite (overflow/underflow/teto de textura), glGetError e PE 42.
 * ----------------------------------------------------------- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pt_util.h"
#include "portico/pr_win32.h"
#include "portico/pr_peproc.h"
#include "portico/pr_surf.h"

/* CHECK variático (mesmos contadores do harness) com mensagem opcional */
#undef CHECK
#define CHECK(cond, ...) do {                                               \
    pt_checks++;                                                            \
    if (cond) { printf("  [ok] "); }                                        \
    else { pt_failures++; printf("  [FALHA] %s:%d: ", __FILE__, __LINE__); } \
    printf("" __VA_ARGS__);                                                 \
    printf("\n");                                                           \
} while (0)

static uint64_t fbits(float f) {
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}

static uint64_t gl_call(pr_win32_ctx* ctx, const char* fn,
                        const uint64_t* a, size_t n) {
    uint64_t ret = 12345;
    pr_status st = pr_win32_call(ctx, "opengl32.dll", fn, a, n, &ret);
    CHECK(st == PR_OK, "%s st=%d", fn, (int)st);
    return ret;
}

static void gl_setup(pr_win32_ctx* ctx, uint64_t* out_dc) {
    uint64_t a[8], ret = 0;
    memset(a, 0, sizeof(a));
    CHECK(pr_win32_call(ctx, "user32.dll", "GetDC", a, 1, &ret) == PR_OK, "GetDC ok");
    *out_dc = ret;
    a[0] = ret;
    CHECK(pr_win32_call(ctx, "opengl32.dll", "wglCreateContext", a, 1, &ret)
          == PR_OK, "wglCreateContext ok");
    a[0] = *out_dc;
    a[1] = ret;
    CHECK(pr_win32_call(ctx, "opengl32.dll", "wglMakeCurrent", a, 2, &ret)
          == PR_OK, "wglMakeCurrent ok");
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

/* glGetIntegerv via API real; out = endereço do scratch (mesmo padrão read_px) */
static pr_status qgl(pr_win32_ctx* ctx, uint64_t out_addr, unsigned pname) {
    uint64_t a[2];
    a[0] = pname;
    a[1] = out_addr;
    uint64_t ret = 0;
    return pr_win32_call(ctx, "opengl32.dll", "glGetIntegerv", a, 2, &ret);
}

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

static void poke4(uint8_t* sb, int off, int32_t v) { memcpy(sb + off, &v, 4); }
static int32_t peek4(const uint8_t* sb, int off) {
    int32_t v;
    memcpy(&v, sb + off, 4);
    return v;
}

/* ============================ test_gl7_unidade ============================ */
static void test_gl7_unidade(void) {
    printf("-- test_gl7_unidade (glGetIntegerv: valores reais + erros + limites)\n");
    pr_win32_ctx* gl = pr_win32_create(NULL);
    CHECK(gl != NULL, "ctx criado");
    size_t ssz = 0;
    uint8_t* sb = (uint8_t*)pr_win32_scratch(gl, &ssz);
    CHECK(sb != NULL && ssz >= 64, "scratch >= 64 bytes");
    uint64_t saddr = (uint64_t)(uintptr_t)sb;
    uint64_t a[16];

    /* sem contexto GL: saída INTOCADA (sentinela) */
    poke4(sb, 0, 0x5A5A5A5A);
    CHECK(qgl(gl, saddr, 0x0D33) == PR_OK, "sem contexto: wrapper sobrevive");
    CHECK(peek4(sb, 0) == 0x5A5A5A5A, "sem contexto: saida intacta (sentinela)");

    uint64_t dc = 0;
    gl_setup(gl, &dc);
    gl_call(gl, "glGetError", NULL, 0);

    /* ---- 5 pnames = capacidades reais ---- */
    memset(a, 0, sizeof(a));
    for (int i = 0; i < 4; i++) poke4(sb, 16 + i * 4, -9);
    CHECK(qgl(gl, saddr + 16, 0x0BA2) == PR_OK, "GL_VIEWPORT consulta ok");
    CHECK(peek4(sb, 16) == 0 && peek4(sb, 20) == 0 &&
          peek4(sb, 24) == 320 && peek4(sb, 28) == 240,
          "GL_VIEWPORT default=(0,0,320,240) real (%d,%d,%d,%d)",
          peek4(sb, 16), peek4(sb, 20), peek4(sb, 24), peek4(sb, 28));
    poke4(sb, 0, -1);
    CHECK(qgl(gl, saddr, 0x0D33) == PR_OK && peek4(sb, 0) == 1024,
          "GL_MAX_TEXTURE_SIZE=1024 (teto real) =%d", peek4(sb, 0));
    poke4(sb, 0, -1);
    CHECK(qgl(gl, saddr, 0x0D31) == PR_OK && peek4(sb, 0) == 8,
          "GL_MAX_LIGHTS=8 (lights[8]) =%d", peek4(sb, 0));
    poke4(sb, 0, -1);
    CHECK(qgl(gl, saddr, 0x0D36) == PR_OK && peek4(sb, 0) == 32,
          "GL_MAX_MODELVIEW_STACK_DEPTH=32 (mv_stack[32]) =%d", peek4(sb, 0));
    poke4(sb, 0, -1);
    CHECK(qgl(gl, saddr, 0x0D38) == PR_OK && peek4(sb, 0) == 4,
          "GL_MAX_PROJECTION_STACK_DEPTH=4 (pj_stack[4]) =%d", peek4(sb, 0));
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0, "consultas validas: NO_ERROR");

    /* consultas repetidas estáveis */
    poke4(sb, 0, -1);
    qgl(gl, saddr, 0x0D33);
    int32_t first = peek4(sb, 0);
    poke4(sb, 0, -1);
    qgl(gl, saddr, 0x0D33);
    CHECK(first == 1024 && peek4(sb, 0) == 1024, "consultas multiplas estaveis");

    /* alteração de estado observável: glViewport -> GL_VIEWPORT */
    a[0] = 10; a[1] = 20; a[2] = 100; a[3] = 80;
    gl_call(gl, "glViewport", a, 4);
    for (int i = 0; i < 4; i++) poke4(sb, 16 + i * 4, -9);
    qgl(gl, saddr + 16, 0x0BA2);
    CHECK(peek4(sb, 16) == 10 && peek4(sb, 20) == 20 &&
          peek4(sb, 24) == 100 && peek4(sb, 28) == 80,
          "GL_VIEWPORT acompanha glViewport (%d,%d,%d,%d)",
          peek4(sb, 16), peek4(sb, 20), peek4(sb, 24), peek4(sb, 28));
    a[0] = 0; a[1] = 0; a[2] = 320; a[3] = 240;
    gl_call(gl, "glViewport", a, 4);

    /* pname inválido: GL_INVALID_ENUM + saída INTOCADA */
    poke4(sb, 0, 0x13579BDF);
    CHECK(qgl(gl, saddr, 0x9999) == PR_OK, "pname invalido: consulta completa");
    CHECK(peek4(sb, 0) == 0x13579BDF, "pname invalido: saida intacta");
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x500, "pname invalido: INVALID_ENUM");

    /* dentro de glBegin: GL_INVALID_OPERATION + saída INTOCADA */
    gl_call(gl, "glBegin", (uint64_t[1]){ 0x0004 }, 1);
    poke4(sb, 0, 0x2468ACE0);
    CHECK(qgl(gl, saddr, 0x0D31) == PR_OK, "em glBegin: consulta completa");
    CHECK(peek4(sb, 0) == 0x2468ACE0, "em glBegin: saida intacta");
    gl_call(gl, "glEnd", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x502, "em glBegin: INVALID_OPERATION");

    /* ponteiro de saída nulo / nargs insuficiente: erro honesto */
    {
        uint64_t aa[2] = { 0x0D33u, 0 };
        uint64_t rv = 0;
        CHECK(pr_win32_call(gl, "opengl32.dll", "glGetIntegerv", aa, 2, &rv)
              == PR_ERR_FAULT, "saida nula: PR_ERR_FAULT");
        CHECK(pr_win32_call(gl, "opengl32.dll", "glGetIntegerv", aa, 1, &rv)
              == PR_ERR_INVALID, "nargs<2: PR_ERR_INVALID");
        gl_call(gl, "glGetError", NULL, 0);
    }

    /* ---- provas: as capacidades são os limites REAIS ---- */
    /* MODELVIEW: 32 pushes cabem; 33o -> STACK_OVERFLOW; pop extra -> UNDERFLOW */
    gl_call(gl, "glMatrixMode", (uint64_t[1]){ 0x1700 }, 1);  /* GL_MODELVIEW */
    gl_call(gl, "glLoadIdentity", NULL, 0);
    for (int i = 0; i < 32; i++) gl_call(gl, "glPushMatrix", NULL, 0);
    {
        uint64_t e = gl_call(gl, "glGetError", NULL, 0);
        CHECK(e == 0, "MODELVIEW: 32 pushes cabem (erro=0x%x)", (unsigned)e);
    }
    gl_call(gl, "glPushMatrix", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x503, "MODELVIEW: 33o push = STACK_OVERFLOW");
    for (int i = 0; i < 32; i++) gl_call(gl, "glPopMatrix", NULL, 0);
    {
        uint64_t e = gl_call(gl, "glGetError", NULL, 0);
        CHECK(e == 0, "MODELVIEW: 32 pops restauram (erro=0x%x)", (unsigned)e);
    }
    gl_call(gl, "glPopMatrix", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x504, "MODELVIEW: pop extra = STACK_UNDERFLOW");
    /* PROJECTION: 4 pushes cabem; 5o -> OVERFLOW */
    gl_call(gl, "glMatrixMode", (uint64_t[1]){ 0x1701 }, 1);  /* GL_PROJECTION */
    gl_call(gl, "glLoadIdentity", NULL, 0);
    for (int i = 0; i < 4; i++) gl_call(gl, "glPushMatrix", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0, "PROJECTION: 4 pushes cabem");
    gl_call(gl, "glPushMatrix", NULL, 0);
    {
        uint64_t e = gl_call(gl, "glGetError", NULL, 0);
        CHECK(e == 0x503, "PROJECTION: 5o push = STACK_OVERFLOW (erro=0x%x)", (unsigned)e);
    }
    for (int i = 0; i < 4; i++) gl_call(gl, "glPopMatrix", NULL, 0);
    gl_call(gl, "glPopMatrix", NULL, 0);
    {
        uint64_t e = gl_call(gl, "glGetError", NULL, 0);
        CHECK(e == 0x504, "PROJECTION: pop extra = STACK_UNDERFLOW (erro=0x%x)", (unsigned)e);
    }
    gl_call(gl, "glMatrixMode", (uint64_t[1]){ 0x1700 }, 1);

    /* textura: 1024x1 ok; 1025x1 -> INVALID_VALUE (teto real = MAX_TEXTURE_SIZE) */
    memset(a, 0, sizeof(a));
    a[0] = 1;
    a[1] = saddr + 32;
    gl_call(gl, "glGenTextures", a, 2);
    uint32_t name;
    memcpy(&name, sb + 32, 4);
    a[0] = 0x0DE1u;
    a[1] = name;
    gl_call(gl, "glBindTexture", a, 2);
    memset(a, 0, sizeof(a));
    a[0] = 0x0DE1u; a[1] = 0; a[2] = 3; a[3] = 1024; a[4] = 1;
    a[5] = 0; a[6] = 0x1907u; a[7] = 0x1401u; a[8] = 0;   /* pixels=NULL: indefinida */
    gl_call(gl, "glTexImage2D", a, 9);
    {
        uint64_t e = gl_call(gl, "glGetError", NULL, 0);
        CHECK(e == 0, "textura 1024x1 valida (erro=0x%x)", (unsigned)e);
    }
    a[3] = 1025;
    gl_call(gl, "glTexImage2D", a, 9);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x501, "textura 1025x1 = INVALID_VALUE");

    /* glTexCoord2f: begin-safe (atributo real) e legal fora do begin */
    gl_call(gl, "glBegin", (uint64_t[1]){ 0x0004 }, 1);
    a[0] = fbits(0.5f); a[1] = fbits(0.5f);
    gl_call(gl, "glTexCoord2f", a, 2);
    a[0] = 0; a[1] = 0; a[2] = 0;
    gl_call(gl, "glVertex3f", a, 3);
    gl_call(gl, "glVertex3f", a, 3);
    gl_call(gl, "glVertex3f", a, 3);
    gl_call(gl, "glEnd", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0, "glTexCoord2f begin-safe (sem erro)");
    gl_call(gl, "glTexCoord2f", (uint64_t[2]){ 0, 0 }, 2);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0, "glTexCoord2f fora do begin ok");

    /* honestidade: glGetFloatv continua AUSENTE (âncora migra com o real) */
    {
        uint64_t rv = 0;
        CHECK(pr_win32_call(gl, "opengl32.dll", "glGetFloatv", NULL, 0, &rv)
              == PR_ERR_RANGE, "glGetFloatv continua ausente (ainda nao exigido)");
        gl_call(gl, "glGetError", NULL, 0);
    }

    pr_win32_destroy(gl);
}

/* ============================ test_gl7_render ============================
 * Textura imediata (glTexCoord2f + UV preservado em raster_list) + luz L7. */
static void test_gl7_render(void) {
    printf("-- test_gl7_render (textura imediata + luz GL_MAX_LIGHTS-1)\n");
    pr_win32_ctx* gl = pr_win32_create(NULL);
    CHECK(gl != NULL, "ctx criado");
    size_t ssz = 0;
    uint8_t* sb = (uint8_t*)pr_win32_scratch(gl, &ssz);
    uint64_t saddr = (uint64_t)(uintptr_t)sb;
    uint64_t a[16];
    uint64_t dc = 0;
    gl_setup(gl, &dc);

    /* textura 32x2: linha 0 azul (v=0), linha 1 verde (v=1) */
    for (int i = 0; i < 32 * 3; i += 3) {
        sb[40 + i] = 0; sb[41 + i] = 0; sb[42 + i] = 255;
        sb[40 + 32 * 3 + i] = 0; sb[41 + 32 * 3 + i] = 255;
        sb[42 + 32 * 3 + i] = 0;
    }
    memset(a, 0, sizeof(a));
    a[0] = 1; a[1] = saddr + 32;
    gl_call(gl, "glGenTextures", a, 2);
    uint32_t name;
    memcpy(&name, sb + 32, 4);
    a[0] = 0x0DE1u; a[1] = name;
    gl_call(gl, "glBindTexture", a, 2);
    memset(a, 0, sizeof(a));
    a[0] = 0x0DE1u; a[1] = 0; a[2] = 3; a[3] = 32; a[4] = 2;
    a[5] = 0; a[6] = 0x1907u; a[7] = 0x1401u; a[8] = saddr + 40;
    gl_call(gl, "glTexImage2D", a, 9);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0, "upload textura 32x2 ok");
    gl_call(gl, "glEnable", (uint64_t[1]){ 0x0DE1u }, 1);

    gl_call(gl, "glViewport", (uint64_t[4]){ 0, 0, 320, 240 }, 4);
    gl_call(gl, "glMatrixMode", (uint64_t[1]){ 0x1701 }, 1);  /* GL_PROJECTION */
    gl_call(gl, "glLoadIdentity", NULL, 0);
    gl_call(gl, "glMatrixMode", (uint64_t[1]){ 0x1700 }, 1);  /* GL_MODELVIEW */
    gl_call(gl, "glLoadIdentity", NULL, 0);
    gl_call(gl, "glClearColor", (uint64_t[4]){ 0, 0, 0, fbits(1) }, 4);
    gl_call(gl, "glClear", (uint64_t[1]){ 0x4000u }, 1);
    gl_call(gl, "glColor3f", (uint64_t[3]){ fbits(1), fbits(1), fbits(1) }, 3);

    gl_call(gl, "glBegin", (uint64_t[1]){ 0x0004 }, 1);
    /* t1 */
    gl_call(gl, "glTexCoord2f", (uint64_t[2]){ fbits(0), fbits(0) }, 2);
    gl_call(gl, "glVertex3f", (uint64_t[3]){ fbits(-1), fbits(-1), 0 }, 3);
    gl_call(gl, "glTexCoord2f", (uint64_t[2]){ fbits(1), fbits(0) }, 2);
    gl_call(gl, "glVertex3f", (uint64_t[3]){ fbits(1), fbits(-1), 0 }, 3);
    gl_call(gl, "glTexCoord2f", (uint64_t[2]){ fbits(1), fbits(1) }, 2);
    gl_call(gl, "glVertex3f", (uint64_t[3]){ fbits(1), fbits(1), 0 }, 3);
    /* t2 */
    gl_call(gl, "glTexCoord2f", (uint64_t[2]){ fbits(0), fbits(0) }, 2);
    gl_call(gl, "glVertex3f", (uint64_t[3]){ fbits(-1), fbits(-1), 0 }, 3);
    gl_call(gl, "glTexCoord2f", (uint64_t[2]){ fbits(1), fbits(1) }, 2);
    gl_call(gl, "glVertex3f", (uint64_t[3]){ fbits(1), fbits(1), 0 }, 3);
    gl_call(gl, "glTexCoord2f", (uint64_t[2]){ fbits(0), fbits(1) }, 2);
    gl_call(gl, "glVertex3f", (uint64_t[3]){ fbits(-1), fbits(1), 0 }, 3);
    gl_call(gl, "glEnd", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0, "quad texturizado imediato: NO_ERROR");

    {
        uint32_t c = read_px(gl, sb + 48, 160, 60);
        CHECK(c == 0x0000FFu, "y=60 (v=0.25) = azul linha 0 (#%06x)", (unsigned)c);
        c = read_px(gl, sb + 48, 160, 180);
        CHECK(c == 0x00FF00u, "y=180 (v=0.75) = verde linha 1 (#%06x)", (unsigned)c);
    }

    /* luz (GL_MAX_LIGHTS-1)=GL_LIGHT7 difusa azul -> centro = (0,0,255) */
    gl_call(gl, "glDisable", (uint64_t[1]){ 0x0DE1u }, 1);
    gl_call(gl, "glClear", (uint64_t[1]){ 0x4000u }, 1);
    {
        uint64_t L7 = 0x4007u;
        uint64_t al[3];
        al[0] = L7; al[1] = 0x1200u; al[2] = saddr + 40;   /* AMBIENT 0 */
        memset(sb + 40, 0, 16);
        gl_call(gl, "glLightfv", al, 3);
        al[1] = 0x1201u;                                    /* DIFFUSE azul */
        memset(sb + 40, 0, 16);
        poke4(sb, 48, 0x3F800000);                          /* z=1 */
        gl_call(gl, "glLightfv", al, 3);
        al[1] = 0x1202u;                                    /* SPECULAR 0 */
        memset(sb + 40, 0, 16);
        gl_call(gl, "glLightfv", al, 3);
        al[1] = 0x1203u;                                    /* POSITION (0,0,1,0) */
        memset(sb + 40, 0, 16);
        poke4(sb, 48, 0x3F800000);
        gl_call(gl, "glLightfv", al, 3);
        uint64_t mt[3] = { 0x0404u, 0x1200u, saddr + 40 };  /* FRONT/GL_AMBIENT 0 */
        memset(sb + 40, 0, 16);
        gl_call(gl, "glMaterialfv", mt, 3);
        mt[1] = 0x1201u;                                    /* GL_DIFFUSE branco */
        memset(sb + 40, 0, 16);
        poke4(sb + 40, 0, 0x3F800000); poke4(sb + 40, 4, 0x3F800000);
        poke4(sb + 40, 8, 0x3F800000); poke4(sb + 40, 12, 0x3F800000);
        gl_call(gl, "glMaterialfv", mt, 3);
        gl_call(gl, "glEnable", (uint64_t[1]){ 0x0B50u }, 1);  /* LIGHTING */
        gl_call(gl, "glEnable", (uint64_t[1]){ L7 }, 1);
        gl_call(gl, "glColor3f", (uint64_t[3]){ fbits(1), fbits(1), fbits(1) }, 3);
        gl_call(gl, "glBegin", (uint64_t[1]){ 0x0004 }, 1);
        gl_call(gl, "glNormal3f", (uint64_t[3]){ 0, 0, fbits(1) }, 3);
        gl_call(gl, "glVertex3f", (uint64_t[3]){ fbits(-0.5f), fbits(-0.5f), 0 }, 3);
        gl_call(gl, "glVertex3f", (uint64_t[3]){ fbits(0.5f), fbits(-0.5f), 0 }, 3);
        gl_call(gl, "glVertex3f", (uint64_t[3]){ fbits(0.5f), fbits(0.5f), 0 }, 3);
        gl_call(gl, "glNormal3f", (uint64_t[3]){ 0, 0, fbits(1) }, 3);
        gl_call(gl, "glVertex3f", (uint64_t[3]){ fbits(-0.5f), fbits(-0.5f), 0 }, 3);
        gl_call(gl, "glVertex3f", (uint64_t[3]){ fbits(0.5f), fbits(0.5f), 0 }, 3);
        gl_call(gl, "glVertex3f", (uint64_t[3]){ fbits(-0.5f), fbits(0.5f), 0 }, 3);
        gl_call(gl, "glEnd", NULL, 0);
    }
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0, "luz L7 (GL_MAX_LIGHTS-1): NO_ERROR");
    {
        uint32_t c = read_px(gl, sb + 48, 160, 120);
        CHECK(c == 0x0000FFu, "centro iluminado por L7 = azul (#%06x)", (unsigned)c);
        c = read_px(gl, sb + 48, 20, 20);
        CHECK(c == 0x000000u, "fora do quad: preto (#%06x)", (unsigned)c);
    }

    pr_win32_destroy(gl);
}

/* ============================ test_gl7_hello_pe ============================ */
static void test_gl7_hello_pe(void) {
    printf("-- test_gl7_hello_pe (hello_gl7.exe: 5 pnames observaveis)\n");
    size_t ilen = 0;
    uint8_t* img = pe_real_load("Tests/PorticoRuntimeTests/data/hello_gl7.exe",
                                &ilen);
    CHECK(img != NULL && ilen > 1024, "hello_gl7.exe carregado (%u bytes)",
          (unsigned)ilen);
    if (!img) return;

    pr_log* log = pr_log_create(1024);
    pr_peproc* p = NULL;
    CHECK(pr_peproc_create(img, ilen, log, &p) == PR_OK, "peproc create");
    CHECK(p != NULL, "peproc nao nulo");
    if (!p) { free(img); return; }
    CHECK(pr_peproc_prepare(p) == PR_OK, "prepare (imports: glGetIntegerv+glTexCoord2f)");
    pr_win32_ctx* ctx = pr_peproc_win32(p);
    CHECK(ctx != NULL, "win32 ctx do PE");

    uint64_t exec = 0;
    int i = 0;
    while (i < 5000 && !pr_peproc_exited(p)) {
        if (pr_peproc_step(p, 10000, &exec) != PR_OK) break;
        i++;
    }
    CHECK(pr_peproc_exited(p), "executou ate o fim (i=%d)", i);
    uint32_t rc = pr_peproc_exit_code(p);
    CHECK(rc == 42, "exit=%u (42 = pnames + pixels + provas corretas)",
          (unsigned)rc);

    pr_surf* sp = pr_win32_surface(ctx);
    CHECK(sp != NULL && pr_surf_width(sp) == 320, "surface 320x240 real");
    uint32_t* px = sp ? pr_surf_pixels(sp) : NULL;
    CHECK(px != NULL, "pixels extraidos (extraido=%d)", (int)pr_peproc_state(p));
    if (px) {
        int nonblack = 0;
        for (int k = 0; k < 320 * 240; k++) {
            if (px[k] & 0x00FFFFFFu) nonblack++;
        }
        CHECK(nonblack > 1000, "frame nao preto (%d px acesos)", nonblack);
        /* cena final = luz L7 (a cena de textura é provada pelo exit=42):
         * 0x00RRGGBB; flip y_surf=239-y_gl; quad +/-0.5 -> x 80..240, y 60..180 */
        CHECK(px[(239 - 120) * 320 + 160] == 0x000000FFu,
              "centro L7 (azul) visivel #%08x",
              (unsigned)px[(239 - 120) * 320 + 160]);
        CHECK(px[(239 - 100) * 320 + 200] == 0x000000FFu,
              "interno L7 (azul) visivel #%08x",
              (unsigned)px[(239 - 100) * 320 + 200]);
        CHECK(px[(239 -  20) * 320 +  20] == 0x00000000u, "fora: preto #%08x",
              (unsigned)px[(239 - 20) * 320 + 20]);
        CHECK(px[(239 - 220) * 320 + 300] == 0x00000000u, "fora: preto #%08x",
              (unsigned)px[(239 - 220) * 320 + 300]);
    }
    pr_peproc_destroy(p);
    free(img);
}

void test_gl7(void) {
    test_gl7_unidade();
    test_gl7_render();
    test_gl7_hello_pe();
}
