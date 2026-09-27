/* test_gl9.c — GRUPO 16: glDrawElements + GL_UNSIGNED_INT (32-bit LE).
 * 18 casos: semântica do índice 32-bit (incl. índice > 65535 que prova que o
 * vértice REAL do rasterizador mudou — sem truncamento uint32→uint16), memória
 * do convidado (fora do buffer, ponteiro inválido, bloco curto, overflow de
 * endereço), combinações com os 4 arranjos, preservação do caminho
 * GL_UNSIGNED_SHORT e honestidade de tipos não suportados.
 * Harness: resolve CONTROLADO (offsets em buffer do teste = memória do
 * convidado) chamando o mesmo pr_gl_draw_elements certificado. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pt_util.h"
#include "portico/pr_win32.h"
#include "portico/pr_gl.h"
#include "portico/pr_peproc.h"
#include "portico/pr_surf.h"

#define GL_TRIANGLES       0x0004u
#define GL_UNSIGNED_SHORT  0x1403u
#define GL_UNSIGNED_INT    0x1405u

#define NV        66003u          /* vértices: alcança índice 66002 */
#define V0        0x1000u         /* base dos arrays (addr 0 = "desligado") */
#define VSZ       (NV * 12u)
#define C0        (V0 + VSZ)
#define CSZ       (NV * 12u)
#define N0        (C0 + CSZ)
#define NSZ       (NV * 12u)
#define T0        (N0 + NSZ)
#define TSZ       (NV * 8u)
#define I0        (T0 + TSZ)      /* blocos de índice */
#define MEM_LEN   (I0 + 8192u)

static uint8_t* g_mem;
static size_t   g_mem_len;

/* "memória do convidado": endereços = offsets em g_mem; falha = NULL */
static void* test_resolve(void* ud, uint64_t addr, size_t len) {
    (void)ud;
    if (addr + len < addr) return NULL;              /* overflow do intervalo */
    if (addr > g_mem_len || len > g_mem_len - addr) return NULL;
    return g_mem + addr;
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

static uint32_t surf_px(pr_surf* s, int x, int y_gl) {
    return pr_surf_pixels(s)[((size_t)pr_surf_height(s) - 1u - (size_t)y_gl)
                             * (size_t)pr_surf_width(s) + (size_t)x];
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

static void clear_black(pr_win32_ctx* gl) {
    uint64_t a[4] = { 0, 0, 0, fbits(1.0f) };
    gl_call(gl, "glClearColor", a, 4);
    a[0] = 0x4000;
    gl_call(gl, "glClear", a, 1);
    gl_call(gl, "glFinish", NULL, 0);
}

static void set_color(pr_win32_ctx* gl, float r, float g2, float b) {
    uint64_t a[4] = { fbits(r), fbits(g2), fbits(b), fbits(1.0f) };
    gl_call(gl, "glColor4f", a, 4);
}

static void ptr4(pr_win32_ctx* gl, const char* fn, int size, uint64_t addr) {
    uint64_t a[4] = { (uint64_t)size, 0x1406, 0, addr };
    gl_call(gl, fn, a, 4);
}

static void set_client(pr_win32_ctx* gl, unsigned arr, int on) {
    uint64_t a[1] = { arr };
    gl_call(gl, on ? "glEnableClientState" : "glDisableClientState", a, 1);
}

static void draw_de(pr_gl_state* gls, unsigned type, uint64_t idx_off,
                    int count) {
    pr_gl_draw_elements(gls, GL_TRIANGLES, count, type, idx_off,
                        test_resolve, NULL);
}

static float* vert_at(uint32_t i) {
    return (float*)(g_mem + V0 + (size_t)i * 12u);
}
static float* col_at(uint32_t i) {
    return (float*)(g_mem + C0 + (size_t)i * 12u);
}
static float* nrm_at(uint32_t i) {
    return (float*)(g_mem + N0 + (size_t)i * 12u);
}
static float* uv_at(uint32_t i) {
    return (float*)(g_mem + T0 + (size_t)i * 8u);
}
static void put_v(uint32_t i, float x, float y, float z) {
    float* v = vert_at(i); v[0] = x; v[1] = y; v[2] = z;
}
static void put_c(uint32_t i, float r, float g2, float b) {
    float* c = col_at(i); c[0] = r; c[1] = g2; c[2] = b;
}
static void put_u32(uint64_t off, const uint32_t* v, int n) {
    memcpy(g_mem + off, v, (size_t)n * 4u);
}
static void put_u16(uint64_t off, const uint16_t* v, int n) {
    memcpy(g_mem + off, v, (size_t)n * 2u);
}

void test_gl9(void) {
    printf("== G16: glDrawElements GL_UNSIGNED_INT (32-bit, sem truncamento) ==\n");

    g_mem_len = MEM_LEN;
    g_mem = (uint8_t*)calloc(1, g_mem_len);
    CHECK(g_mem != NULL);
    if (!g_mem) return;

    /* geometria base (ortho 0..320 x 0..240, CCW, pontos internos) */
    for (uint32_t i = 0; i < NV; i++) put_v(i, -9.0f, -9.0f, 0.0f);
    put_v(0, 10, 200, 0); put_v(1, 40, 200, 0); put_v(2, 10, 230, 0);
    put_v(3, 60, 200, 0); put_v(4, 90, 200, 0); put_v(5, 60, 230, 0);
    put_v(6, 110, 200, 0); put_v(7, 140, 200, 0); put_v(8, 110, 230, 0);
    put_v(9, 200, 200, 0);
    /* alvo do TRUNCAMENTO (66000-65536=464...): azul no canto inferior */
    put_v(464, 10, 20, 0); put_v(465, 40, 20, 0); put_v(466, 10, 50, 0);
    /* triângulo dos ÍNDICES GRANDES: centro */
    put_v(66000, 120, 90, 0); put_v(66001, 240, 90, 0); put_v(66002, 120, 200, 0);
    for (uint32_t i = 0; i < 9; i++) put_c(i, 1, 1, 0);          /* amarelo */
    put_c(9, 1, 0, 0);
    put_c(464, 0, 0, 1); put_c(465, 0, 0, 1); put_c(466, 0, 0, 1); /* azul */
    put_c(66000, 1, 1, 0); put_c(66001, 1, 1, 0); put_c(66002, 1, 1, 0);
    for (uint32_t i = 0; i < NV; i++) {
        float* n = nrm_at(i); n[0] = 0; n[1] = 0; n[2] = 1;
    }
    uv_at(66000)[0] = 0.0f; uv_at(66000)[1] = 0.0f;
    uv_at(66001)[0] = 1.0f; uv_at(66001)[1] = 0.0f;
    uv_at(66002)[0] = 0.5f; uv_at(66002)[1] = 1.0f;

    const uint32_t q_seq[3]   = { 0, 1, 2 };
    const uint32_t q_nonseq[3]= { 8, 6, 7 };
    const uint32_t q_rep[3]   = { 9, 9, 9 };
    const uint32_t q_multi[6] = { 0, 1, 2, 3, 4, 5 };
    const uint32_t q_big[3]   = { 66000, 66001, 66002 };
    const uint32_t q_oob[3]   = { 250000, 250001, 250002 };
    const uint32_t q_ovf[3]   = { 500, 501, 502 };
    const uint32_t q_huge[3]  = { 0xFFFFFFFFu, 1, 2 };
    const uint16_t s_seq[3]   = { 0, 1, 2 };
    put_u32(I0 + 0,  q_seq, 3);
    put_u32(I0 + 16, q_nonseq, 3);
    put_u32(I0 + 32, q_rep, 3);
    put_u32(I0 + 48, q_multi, 6);
    put_u32(I0 + 80, q_big, 3);
    put_u32(I0 + 96, q_oob, 3);
    put_u32(I0 + 112, q_ovf, 3);
    put_u32(I0 + 128, q_huge, 3);
    put_u16(I0 + 144, s_seq, 3);

    pr_win32_ctx* gl = pr_win32_create(NULL);
    CHECK(gl != NULL);
    uint64_t dc = 0;
    gl_setup(gl, &dc);
    pr_gl_state* gls = pr_win32_gl_state(gl);
    CHECK(gls != NULL);
    size_t ssz = 0;
    uint8_t* scr = (uint8_t*)pr_win32_scratch(gl, &ssz);
    uint8_t* px4 = scr + 8;
    setup_ortho(gl);

    /* arrays padrão: vertex + color */
    set_client(gl, 0x8074, 1);              /* GL_VERTEX_ARRAY */
    set_client(gl, 0x8076, 1);              /* GL_COLOR_ARRAY */
    ptr4(gl, "glVertexPointer", 3, V0);
    ptr4(gl, "glColorPointer", 3, C0);

    /* ---- 1) índice simples 0,1,2 (32-bit) desenha o vértice real ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 0, 3);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 15, 205) == 0xFFFF00u);

    /* ---- 2) índice não sequencial {8,6,7} ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 16, 3);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 115, 205) == 0xFFFF00u);

    /* ---- 3) índice repetido {9,9,9}: aceito, degenerado, nada desenhado;
     *         o pipeline segue íntegro (draw válido pinta em seguida) ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 32, 3);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 205, 205) == 0u);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 0, 3);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 15, 205) == 0xFFFF00u);

    /* ---- 4) múltiplos triângulos (count=6) ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 48, 6);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 15, 205) == 0xFFFF00u);
    CHECK(read_gl_px(gl, px4, 65, 205) == 0xFFFF00u);
    {
        /* BISECT: o mesmo triângulo B isolado (count=3) */
        const uint32_t q_b[3] = { 3, 4, 5 };
        put_u32(I0 + 160, q_b, 3);
        clear_black(gl);
        draw_de(gls, GL_UNSIGNED_INT, I0 + 160, 3);
        gl_call(gl, "glFinish", NULL, 0);
        CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
        CHECK(read_gl_px(gl, px4, 65, 205) == 0xFFFF00u);
    }

    /* ---- 5) índice 32-bit > 65535 muda o VÉRTICE REAL usado: o triângulo
     *         66000..66002 pinta AMARELO no centro; se houvesse truncamento
     *         uint16 (66000->464) o triângulo AZUL do canto apareceria e o
     *         centro ficaria preto ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 80, 3);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 150, 120) == 0xFFFF00u);
    CHECK(read_gl_px(gl, px4, 15, 30) == 0x000000u);
    CHECK(read_gl_px(gl, px4, 20, 32) == 0x000000u);

    /* ---- 6) combinação com vertex array (base de todos os draws) ---- */
    CHECK(read_gl_px(gl, px4, 150, 120) == 0xFFFF00u);

    /* ---- 7) combinação com normal array (fetch resolve normal) ---- */
    clear_black(gl);
    set_client(gl, 0x8075, 1);              /* GL_NORMAL_ARRAY */
    {
        uint64_t a3[3] = { 0x1406, 0, N0 };
        gl_call(gl, "glNormalPointer", a3, 3);
    }
    draw_de(gls, GL_UNSIGNED_INT, I0 + 0, 3);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 15, 205) == 0xFFFF00u);
    set_client(gl, 0x8075, 0);

    /* ---- 8) combinação com color array: array amarelo; depois desligado
     *         -> cor ATUAL verde (disable observável + fetch de cor) ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 0, 3);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(read_gl_px(gl, px4, 15, 205) == 0xFFFF00u);
    set_client(gl, 0x8076, 0);
    set_color(gl, 0.0f, 1.0f, 0.0f);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 16, 3);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 115, 205) == 0x00FF00u);
    set_client(gl, 0x8076, 1);

    /* ---- 9) combinação com texture coordinate array (amostra real) ---- */
    clear_black(gl);
    {
        /* textura procedural 2x1: branco | azul — em scratch do ctx */
        uint8_t* texels = scr + 32;
        texels[0] = 255; texels[1] = 255; texels[2] = 255;
        texels[3] = 0;   texels[4] = 0;   texels[5] = 255;
        uint64_t tn = 0;
        uint64_t a[9];
        pr_gl_gen_textures(gls, 1, (uint32_t*)&tn);
        a[0] = 0x0DE1; a[1] = (uint32_t)tn;
        gl_call(gl, "glBindTexture", a, 2);
        a[0] = 0x0DE1; a[1] = 0; a[2] = 3; a[3] = 2; a[4] = 1; a[5] = 0;
        a[6] = 0x1907; a[7] = 0x1401; a[8] = (uint64_t)(uintptr_t)texels;
        gl_call(gl, "glTexImage2D", a, 9);
        a[0] = 0x0DE1;
        gl_call(gl, "glEnable", a, 1);
        set_client(gl, 0x8078, 1);          /* GL_TEXTURE_COORD_ARRAY */
        ptr4(gl, "glTexCoordPointer", 2, T0);
        set_client(gl, 0x8076, 0);          /* cor atual branca */
        set_color(gl, 1.0f, 1.0f, 1.0f);
        draw_de(gls, GL_UNSIGNED_INT, I0 + 80, 3);   /* triângulo grande c/ uv */
        gl_call(gl, "glFinish", NULL, 0);
        CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
        /* (190,105): u≈0.66 -> texel azul; se o uv fosse ignorado (u=0) o
         * pixel seria branco */
        CHECK(read_gl_px(gl, px4, 190, 105) == 0x0000FFu);
        set_client(gl, 0x8078, 0);
        gl_call(gl, "glDisable", a, 1);
        set_client(gl, 0x8076, 1);
    }

    /* ---- 10) GL_TRIANGLES: modo válido em todos os draws acima ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 0, 3);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);

    /* ---- 11) índice além da memória do convidado: 0x501 + nada novo ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 96, 3);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(read_gl_px(gl, px4, 15, 205) == 0u);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x501u);

    /* ---- 12) ponteiro de índice inválido (fora da memória / intervalo
     *         parcialmente válido): 0x501 + nada desenhado ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_INT, MEM_LEN + 0x100, 3);
    CHECK(read_gl_px(gl, px4, 15, 205) == 0u);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x501u);
    draw_de(gls, GL_UNSIGNED_INT, MEM_LEN - 4, 3);   /* 12 bytes pedidos, 4 ok */
    CHECK(read_gl_px(gl, px4, 15, 205) == 0u);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x501u);

    /* ---- 13) memória insuficiente para TODOS os índices (count=6 num
     *         bloco curto): 0x501 + nada desenhado ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_INT, MEM_LEN - 8, 6);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(read_gl_px(gl, px4, 15, 205) == 0u);
    CHECK(read_gl_px(gl, px4, 65, 205) == 0u);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x501u);

    /* ---- 14) overflow de cálculo de endereço: (a) índice gigante num
     *         array normal -> resolve falha; (b) base quase em 2^64 com
     *         índice moderado -> guarda anti-overflow recusa ANTES de ler
     *         (sem a guarda, base+vi*stride envolveria para offset baixo e
     *         leria memória errada) ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 128, 3);      /* 0xFFFFFFFF */
    CHECK(read_gl_px(gl, px4, 15, 205) == 0u);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x501u);
    ptr4(gl, "glVertexPointer", 3, 0xFFFFFFFFFFFFF000ull);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 112, 3);       /* índices 500..502 */
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x501u);
    ptr4(gl, "glVertexPointer", 3, V0);

    /* ---- 15) glGetError limpo em caso válido ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 0, 3);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);

    /* ---- 16) GL_INVALID_OPERATION quando o estado exigir: sem vertex
     *         array; dentro de glBegin ---- */
    set_client(gl, 0x8074, 0);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 0, 3);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x502u);
    set_client(gl, 0x8074, 1);
    gl_call(gl, "glBegin", (uint64_t[1]){ 0x0004 }, 1);
    draw_de(gls, GL_UNSIGNED_INT, I0 + 0, 3);
    gl_call(gl, "glEnd", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x502u);

    /* ---- 17) preservação do caminho GL_UNSIGNED_SHORT ---- */
    clear_black(gl);
    draw_de(gls, GL_UNSIGNED_SHORT, I0 + 144, 3);
    gl_call(gl, "glFinish", NULL, 0);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    CHECK(read_gl_px(gl, px4, 15, 205) == 0xFFFF00u);

    /* ---- 18) tipos ainda não suportados: GL_INVALID_ENUM honesto ---- */
    clear_black(gl);
    draw_de(gls, 0x1401, I0 + 0, 3);                 /* GL_UNSIGNED_BYTE */
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x500u);
    draw_de(gls, 0x1400, I0 + 0, 3);                 /* GL_BYTE */
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x500u);
    draw_de(gls, 0x1406, I0 + 0, 3);                 /* GL_FLOAT */
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x500u);
    draw_de(gls, 0x1234, I0 + 0, 3);                 /* enum desconhecido */
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x500u);
    CHECK(read_gl_px(gl, px4, 15, 205) == 0u);

    /* gl_setup finish */
    {
        uint64_t ar[2] = { 0, 0 }, ret = 0;
        pr_win32_call(gl, "opengl32.dll", "wglMakeCurrent", ar, 2, &ret);
        ar[0] = dc; pr_win32_call(gl, "user32.dll", "ReleaseDC", ar, 2, &ret);
    }
    pr_win32_destroy(gl);
    free(g_mem);

    /* ---- hello_gl9.exe: GL_UNSIGNED_INT real num PE, a 42 ---- */
    {
        size_t ilen = 0;
        uint8_t* img = pe_real_load("Tests/PorticoRuntimeTests/data/hello_gl9.exe",
                                    &ilen);
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
        if (sp) {
            CHECK(surf_px(sp, 30, 210) == 0x0000FF00u);
            CHECK(surf_px(sp, 150, 120) == 0x00FFFF00u);
            CHECK(surf_px(sp, 30, 35) == 0x00000000u);
            CHECK(surf_px(sp, 300, 30) == 0x00000000u);
        }
        pr_peproc_destroy(p);
        pr_log_destroy(log);
        free(img);
    }
}
