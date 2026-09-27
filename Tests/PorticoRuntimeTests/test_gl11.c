/* test_gl11.c — GRUPO 18: glDepthFunc (funcao de comparacao do z-buffer).
 * Descoberto por execução de hello_gl11.exe (STOP "Unsupported Win32 API |
 * OPENGL32.dll | glDepthFunc" ANTES da implementação — ver RELATORIO_G18).
 * Casos: decal coplanar observavel (GL_LESS rejeita igualdade; GL_LEQUAL
 * aceita), os 8 modos de comparacao, enum invalido -> 0x500 com estado
 * preservado, sem contexto silencioso, depth test desligado ignora o modo,
 * wrapper sem argumentos -> PR_ERR_INVALID (API sem ponteiro: casos de
 * ponteiro/overflow N/A por construcao), glGetError, preservacao do default
 * certificado, e hello_gl11.exe no loader real. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pt_util.h"
#include "portico/pr_win32.h"
#include "portico/pr_gl.h"
#include "portico/pr_peproc.h"
#include "portico/pr_surf.h"

#define GL_DEPTH_TEST_B  0x0B71u
#define GL_NEVER_B       0x0200u
#define GL_LESS_B        0x0201u
#define GL_EQUAL_B       0x0202u
#define GL_LEQUAL_B      0x0203u
#define GL_GREATER_B     0x0204u
#define GL_NOTEQUAL_B    0x0205u
#define GL_GEQUAL_B      0x0206u
#define GL_ALWAYS_B      0x0207u

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

static void clear_all(pr_win32_ctx* gl) {
    uint64_t a[4] = { 0, 0, 0, fbits(1.0f) };
    gl_call(gl, "glClearColor", a, 4);
    a[0] = 0x4100u;   /* GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT */
    gl_call(gl, "glClear", a, 1);
    gl_call(gl, "glFinish", NULL, 0);
}

static void set_color(pr_win32_ctx* gl, float r, float g, float b) {
    uint64_t a[3] = { fbits(r), fbits(g), fbits(b) };
    gl_call(gl, "glColor3f", a, 3);
}

/* quad 120x60 em x_gl 100..220, y_gl 100..160 na profundidade z_eye */
static void draw_quad_at(pr_win32_ctx* gl, uint8_t* scr, float zeye) {
    float v[18] = {
        100, 100, zeye,   220, 100, zeye,   220, 160, zeye,
        100, 100, zeye,   220, 160, zeye,   100, 160, zeye
    };
    uint64_t a[4];
    memcpy(scr + 0, v, sizeof v);
    a[0] = 0x8074; gl_call(gl, "glEnableClientState", a, 1);
    a[0] = 3; a[1] = 0x1406; a[2] = 0; a[3] = (uint64_t)(uintptr_t)scr;
    gl_call(gl, "glVertexPointer", a, 4);
    a[0] = 0x0004; a[1] = 0; a[2] = 6;
    gl_call(gl, "glDrawArrays", a, 3);
    gl_call(gl, "glFinish", NULL, 0);
    a[0] = 0x8074; gl_call(gl, "glDisableClientState", a, 1);
}

static void depth_func_call(pr_win32_ctx* gl, uint64_t fn) {
    uint64_t a[1] = { fn };
    gl_call(gl, "glDepthFunc", a, 1);
}

/* quad 120x60 com color array: v0 vermelho, v1 verde, v2 azul, v3 amarelo;
 * triangulos {0,1,2} e {0,2,3} (6 vertices) — espelho do painel do G22 */
static void draw_quad_cv(pr_win32_ctx* gl, uint8_t* scr) {
    float v[18] = {
        100, 100, 0,   220, 100, 0,   220, 160, 0,
        100, 100, 0,   220, 160, 0,   100, 160, 0
    };
    float c[18] = {
        1, 0, 0,   0, 1, 0,   0, 0, 1,
        1, 0, 0,   0, 0, 1,   1, 1, 0
    };
    uint64_t a[4];
    memcpy(scr + 0, v, sizeof v);
    memcpy(scr + 128, c, sizeof c);
    a[0] = 0x8074; gl_call(gl, "glEnableClientState", a, 1);
    a[0] = 0x8076; gl_call(gl, "glEnableClientState", a, 1);
    a[0] = 3; a[1] = 0x1406; a[2] = 0; a[3] = (uint64_t)(uintptr_t)scr;
    gl_call(gl, "glVertexPointer", a, 4);
    a[0] = 3; a[1] = 0x1406; a[2] = 0;
    a[3] = (uint64_t)(uintptr_t)(scr + 128);
    gl_call(gl, "glColorPointer", a, 4);
    a[0] = 0x0004; a[1] = 0; a[2] = 6;
    gl_call(gl, "glDrawArrays", a, 3);
    gl_call(gl, "glFinish", NULL, 0);
    a[0] = 0x8076; gl_call(gl, "glDisableClientState", a, 1);
    a[0] = 0x8074; gl_call(gl, "glDisableClientState", a, 1);
}

static void shade_model_call(pr_win32_ctx* gl, uint64_t mode) {
    uint64_t a[1] = { mode };
    gl_call(gl, "glShadeModel", a, 1);
}

/* quad 4 vertices + color array via glDrawElements GL_UNSIGNED_INT (espelho
 * do painel do hello_gl12): v0 vermelho, v1 verde, v2 azul, v3 amarelo */
static void draw_quad_idx_cv(pr_win32_ctx* gl, uint8_t* scr,
                             const unsigned int idx[6]) {
    float v[12] = {
        100, 100, 0,   220, 100, 0,   220, 160, 0,   100, 160, 0
    };
    float c[12] = {
        1, 0, 0,   0, 1, 0,   0, 0, 1,   1, 1, 0
    };
    uint64_t a[4];
    memcpy(scr + 0, v, sizeof v);
    memcpy(scr + 128, c, sizeof c);
    memcpy(scr + 256, idx, 6 * sizeof(unsigned int));
    a[0] = 0x8074; gl_call(gl, "glEnableClientState", a, 1);
    a[0] = 0x8076; gl_call(gl, "glEnableClientState", a, 1);
    a[0] = 3; a[1] = 0x1406; a[2] = 0; a[3] = (uint64_t)(uintptr_t)scr;
    gl_call(gl, "glVertexPointer", a, 4);
    a[0] = 3; a[1] = 0x1406; a[2] = 0;
    a[3] = (uint64_t)(uintptr_t)(scr + 128);
    gl_call(gl, "glColorPointer", a, 4);
    a[0] = 0x0004; a[1] = 6; a[2] = 0x1405;
    a[3] = (uint64_t)(uintptr_t)(scr + 256);
    gl_call(gl, "glDrawElements", a, 4);
    gl_call(gl, "glFinish", NULL, 0);
    a[0] = 0x8076; gl_call(gl, "glDisableClientState", a, 1);
    a[0] = 0x8074; gl_call(gl, "glDisableClientState", a, 1);
}

/* z_eye -> z_ndc = -z_eye (glOrtho -1..1): menor z_ndc = mais perto.
 * d1 = profundidade armazenada pelo 1º desenho; d2 = do 2º. */
static int compare_passes(uint32_t mode, float d2, float d1) {
    switch (mode) {
    case GL_NEVER_B:    return 0;
    case GL_LESS_B:     return d2 < d1;
    case GL_EQUAL_B:    return d2 == d1;
    case GL_LEQUAL_B:   return d2 <= d1;
    case GL_GREATER_B:  return d2 > d1;
    case GL_NOTEQUAL_B: return d2 != d1;
    case GL_GEQUAL_B:   return d2 >= d1;
    default:            return 1;
    }
}

void test_gl11(void) {
    printf("== G18: glDepthFunc (comparacao do z-buffer; decals coplanares) ==\n");
    pr_win32_ctx* gl = pr_win32_create(NULL);
    CHECK(gl != NULL);
    uint64_t dc = 0;
    gl_setup(gl, &dc);
    size_t ssz = 0;
    uint8_t* scr = (uint8_t*)pr_win32_scratch(gl, &ssz);
    CHECK(scr != NULL && ssz >= 512);
    uint8_t* px4 = scr + 128;

    setup_ortho(gl);
    uint64_t a[1];
    a[0] = GL_DEPTH_TEST_B;
    gl_call(gl, "glEnable", a, 1);

    /* ---- 1) PRESERVACAO do default GL_LESS (sem nunca chamar glDepthFunc):
     *         igualdade e' REJEITADA (z >= d descarta) e menor z vence ---- */
    clear_all(gl);
    set_color(gl, 1, 0, 0);
    draw_quad_at(gl, scr, 0.0f);          /* d1 = 0 */
    set_color(gl, 0, 1, 0);
    draw_quad_at(gl, scr, 0.0f);          /* d2 = 0 (coplanar) -> rejeitado */
    CHECK(read_gl_px(gl, px4, 160, 130) == 0xFF0000u);
    set_color(gl, 0, 0, 1);
    draw_quad_at(gl, scr, 0.5f);          /* d2 = -0.5 (mais perto) -> vence */
    CHECK(read_gl_px(gl, px4, 160, 130) == 0x0000FFu);

    /* ---- 2) CASO VALIDO: decal coplanar com GL_LEQUAL (o tapete do PE) ---- */
    clear_all(gl);
    set_color(gl, 1, 0, 0);
    draw_quad_at(gl, scr, 0.0f);
    depth_func_call(gl, GL_LEQUAL_B);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    set_color(gl, 0, 1, 0);
    draw_quad_at(gl, scr, 0.0f);          /* mesma profundidade -> ACEITO */
    CHECK(read_gl_px(gl, px4, 160, 130) == 0x00FF00u);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);

    /* ---- 3) OS 8 MODOS: tabela de comparacao observavel ---- */
    {
        static const uint32_t modes[8] = {
            GL_NEVER_B, GL_LESS_B, GL_EQUAL_B, GL_LEQUAL_B,
            GL_GREATER_B, GL_NOTEQUAL_B, GL_GEQUAL_B, GL_ALWAYS_B
        };
        static const float z[3] = { 0.5f, 0.0f, -0.5f };  /* d = -z = -0.5,0,0.5 */
        for (int m = 0; m < 8; m++) {
            for (int i = 0; i < 3; i++) {
                for (int j = 0; j < 3; j++) {
                    clear_all(gl);
                    depth_func_call(gl, GL_ALWAYS_B);  /* 1º grava d1 */
                    set_color(gl, 1, 0, 0);
                    draw_quad_at(gl, scr, z[i]);
                    depth_func_call(gl, modes[m]);     /* modo sob teste */
                    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
                    set_color(gl, 0, 1, 0);
                    draw_quad_at(gl, scr, z[j]);
                    float d1 = -z[i], d2 = -z[j];
                    uint32_t want = compare_passes(modes[m], d2, d1)
                                        ? 0x00FF00u : 0xFF0000u;
                    CHECK(read_gl_px(gl, px4, 160, 130) == want);
                }
            }
        }
    }

    /* ---- 3b) a profundidade do CLEAR participa da comparacao.
     *         Profundidades em espaco janela [0,1]: z_win=(1-z_olho)/2
     *         (z_olho=0 -> 0.5; z_olho=0.5 -> 0.25; z_olho=-0.5 -> 0.75) */
    clear_all(gl);                    /* clear_d = 1.0 */
    depth_func_call(gl, GL_GREATER_B);
    set_color(gl, 1, 0, 0);
    draw_quad_at(gl, scr, 0.0f);      /* 0.5 > 1.0? nao -> nada desenhado */
    CHECK(read_gl_px(gl, px4, 160, 130) == 0x000000u);
    {   /* glClearDepth(0.0): o MESMO desenho agora passa */
        double z0 = 0.0;
        uint64_t ad[1];
        memcpy(ad, &z0, sizeof z0);
        gl_call(gl, "glClearDepth", ad, 1);
    }
    clear_all(gl);
    set_color(gl, 1, 0, 0);
    draw_quad_at(gl, scr, 0.0f);      /* 0.5 > 0.0 -> desenha */
    CHECK(read_gl_px(gl, px4, 160, 130) == 0xFF0000u);
    set_color(gl, 0, 0, 1);
    draw_quad_at(gl, scr, -0.5f);     /* 0.75 > 0.5 (gravado) -> sobrescreve */
    CHECK(read_gl_px(gl, px4, 160, 130) == 0x0000FFu);
    {   /* restaura o clear default para os casos seguintes */
        double z1 = 1.0;
        uint64_t ad[1];
        memcpy(ad, &z1, sizeof z1);
        gl_call(gl, "glClearDepth", ad, 1);
    }
    depth_func_call(gl, GL_LESS_B);

    /* ---- 4) CASO INVALIDO: enum fora dos 8 modos -> 0x500, estado
     *         PRESERVADO (GL_LEQUAL continua ativo) ---- */
    depth_func_call(gl, GL_LEQUAL_B);
    depth_func_call(gl, 0x9999u);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0x500u);
    CHECK(gl_call(gl, "glGetError", NULL, 0) == 0);
    clear_all(gl);
    set_color(gl, 1, 0, 0);
    draw_quad_at(gl, scr, 0.0f);
    set_color(gl, 0, 1, 0);
    draw_quad_at(gl, scr, 0.0f);          /* coplanar ainda ACEITO */
    CHECK(read_gl_px(gl, px4, 160, 130) == 0x00FF00u);

    /* ---- 5) ESTADO: depth test desligado ignora o modo (sempre desenha) */
    a[0] = GL_DEPTH_TEST_B;
    gl_call(gl, "glDisable", a, 1);
    depth_func_call(gl, GL_NEVER_B);
    clear_all(gl);
    set_color(gl, 1, 0, 0);
    draw_quad_at(gl, scr, 0.5f);
    set_color(gl, 0, 1, 0);
    draw_quad_at(gl, scr, -0.5f);         /* sem teste -> sobrescreve */
    CHECK(read_gl_px(gl, px4, 160, 130) == 0x00FF00u);
    a[0] = GL_DEPTH_TEST_B;
    gl_call(gl, "glEnable", a, 1);
    depth_func_call(gl, GL_LESS_B);

    /* ---- 6) wrapper: <1 argumento -> PR_ERR_INVALID (sem ponteiro/sem
     *         carga de memoria: casos de ponteiro/overflow N/A) ---- */
    {
        uint64_t ret = 0;
        CHECK(pr_win32_call(gl, "opengl32.dll", "glDepthFunc", NULL, 0,
                            &ret) == PR_ERR_INVALID);
    }

    /* ---- 7) ESTADO INVALIDO: sem contexto atual -> silencioso PR_OK ---- */
    {
        pr_win32_ctx* gl2 = pr_win32_create(NULL);
        CHECK(gl2 != NULL);
        uint64_t ar[1] = { GL_LEQUAL_B }, ret = 0;
        CHECK(pr_win32_call(gl2, "opengl32.dll", "glDepthFunc", ar, 1,
                            &ret) == PR_OK);   /* silencioso, sem MakeCurrent */
        pr_win32_destroy(gl2);
    }

    /* ================= G21: glDepthMask (escritas no z-buffer) =========
     * Descoberto por execucao de hello_gl12.exe (STOP "Unsupported Win32 API
     * | OPENGL32.dll | glDepthMask" ANTES da implementacao). */
    {
        pr_win32_ctx* ctx = pr_win32_create(NULL);
        CHECK(ctx != NULL);
        size_t ssz = 0;
        uint8_t* scr = (uint8_t*)pr_win32_scratch(ctx, &ssz);
        CHECK(scr != NULL && ssz >= 512);
        uint8_t* px4 = scr + 128;   /* ponteiro GUEST (stack do host nao vale) */
        uint64_t a[6];

        printf("== G21: glDepthMask (escritas no z-buffer) ==\n");

        /* wrapper: sem argumentos e sem ctx -> PR_ERR_INVALID (honesto) */
        {
            uint64_t ret = 0;
            CHECK(pr_win32_call(ctx, "opengl32.dll", "glDepthMask", a, 0,
                                &ret) == PR_ERR_INVALID);
            CHECK(pr_win32_call(NULL, "opengl32.dll", "glDepthMask", a, 1,
                                &ret) == PR_ERR_INVALID);
        }
        /* sem contexto corrente: silencioso (padrao glDepthFunc, sem crash) */
        {
            uint64_t ret = 0;
            a[0] = 1;
            CHECK(pr_win32_call(ctx, "opengl32.dll", "glDepthMask", a, 1,
                                &ret) == PR_OK);
        }

        uint64_t dc = 0;
        gl_setup(ctx, &dc);
        setup_ortho(ctx);
        a[0] = GL_DEPTH_TEST_B;
        gl_call(ctx, "glEnable", a, 1);
        depth_func_call(ctx, GL_LESS_B);

        /* -- 1) write-off + restauracao: o marcador atras "atravessa" -- */
        clear_all(ctx);
        set_color(ctx, 1.0f, 0.0f, 0.0f);
        draw_quad_at(ctx, scr, -0.5f);            /* z_ndc 0.5 (longe) */
        CHECK(read_gl_px(ctx, px4, 150, 130) == 0x00FF0000u);
        a[0] = 0;
        gl_call(ctx, "glDepthMask", a, 1);        /* sem escrita */
        set_color(ctx, 0.0f, 1.0f, 0.0f);
        draw_quad_at(ctx, scr, 0.0f);             /* z_ndc 0.0: passa, nao grava */
        CHECK(read_gl_px(ctx, px4, 150, 130) == 0x0000FF00u);
        a[0] = 1;
        gl_call(ctx, "glDepthMask", a, 1);        /* restaura escrita */
        set_color(ctx, 0.0f, 0.0f, 1.0f);
        draw_quad_at(ctx, scr, -0.25f);           /* z_ndc 0.25 < 0.5 (inalterado) */
        CHECK(read_gl_px(ctx, px4, 150, 130) == 0x000000FFu);

        /* -- 2) controle write-on: o mesmo passo 3 e' rejeitado -- */
        clear_all(ctx);
        set_color(ctx, 1.0f, 0.0f, 0.0f);
        draw_quad_at(ctx, scr, -0.5f);
        set_color(ctx, 0.0f, 1.0f, 0.0f);
        draw_quad_at(ctx, scr, 0.0f);             /* grava z_ndc 0.0 */
        set_color(ctx, 0.0f, 0.0f, 1.0f);
        draw_quad_at(ctx, scr, -0.25f);           /* 0.25 >= 0.0: rejeitado */
        CHECK(read_gl_px(ctx, px4, 150, 130) == 0x0000FF00u);

        /* -- 3) flag != 0 vale como true (GLboolean) -- */
        clear_all(ctx);
        set_color(ctx, 1.0f, 0.0f, 0.0f);
        draw_quad_at(ctx, scr, -0.5f);
        a[0] = 2;
        gl_call(ctx, "glDepthMask", a, 1);        /* != 0 => escrita ligada */
        set_color(ctx, 0.0f, 1.0f, 0.0f);
        draw_quad_at(ctx, scr, 0.0f);
        set_color(ctx, 0.0f, 0.0f, 1.0f);
        draw_quad_at(ctx, scr, -0.25f);
        CHECK(read_gl_px(ctx, px4, 150, 130) == 0x0000FF00u);

        /* -- 4) glClear de profundidade respeita a mascara -- */
        clear_all(ctx);
        set_color(ctx, 1.0f, 0.0f, 0.0f);
        draw_quad_at(ctx, scr, -0.5f);            /* grava z_ndc 0.5 */
        a[0] = 0;
        gl_call(ctx, "glDepthMask", a, 1);
        a[0] = 0x100;                             /* GL_DEPTH_BUFFER_BIT */
        gl_call(ctx, "glClear", a, 1);            /* NAO limpa (mascarado) */
        a[0] = 1;
        gl_call(ctx, "glDepthMask", a, 1);
        set_color(ctx, 0.0f, 0.0f, 1.0f);
        draw_quad_at(ctx, scr, -0.75f);           /* z_ndc 0.75 >= 0.5: rejeitado */
        CHECK(read_gl_px(ctx, px4, 150, 130) == 0x00FF0000u);

        /* -- 5) controle: clear com mascara ligada limpa normalmente -- */
        clear_all(ctx);
        set_color(ctx, 1.0f, 0.0f, 0.0f);
        draw_quad_at(ctx, scr, -0.5f);
        a[0] = 0x100;
        gl_call(ctx, "glClear", a, 1);            /* limpa (mascara ligada) */
        set_color(ctx, 0.0f, 0.0f, 1.0f);
        draw_quad_at(ctx, scr, -0.75f);           /* 0.75 < 1.0: passa */
        CHECK(read_gl_px(ctx, px4, 150, 130) == 0x000000FFu);

        /* sem erros GL novos; API sem ponteiro => overflow/ptr N/A */
        CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

        pr_win32_destroy(ctx);
    }

    /* ================= G22: glShadeModel (GL_FLAT/GL_SMOOTH) ===========
     * Descoberto por execucao de hello_gl12.exe (STOP "Unsupported Win32 API
     * | OPENGL32.dll | glShadeModel" ANTES da implementacao). */
    {
        pr_win32_ctx* ctx = pr_win32_create(NULL);
        CHECK(ctx != NULL);
        size_t ssz = 0;
        uint8_t* scr = (uint8_t*)pr_win32_scratch(ctx, &ssz);
        CHECK(scr != NULL && ssz >= 512);
        uint8_t* px4 = scr + 384;
        uint64_t a[6];

        printf("== G22: glShadeModel (GL_FLAT/GL_SMOOTH) ==\n");

        /* wrapper: sem argumentos e sem ctx -> PR_ERR_INVALID */
        {
            uint64_t ret = 0;
            CHECK(pr_win32_call(ctx, "opengl32.dll", "glShadeModel", a, 0,
                                &ret) == PR_ERR_INVALID);
            CHECK(pr_win32_call(NULL, "opengl32.dll", "glShadeModel", a, 1,
                                &ret) == PR_ERR_INVALID);
        }
        /* sem contexto corrente: silencioso, sem crash */
        {
            uint64_t ret = 0;
            a[0] = 0x1D00;
            CHECK(pr_win32_call(ctx, "opengl32.dll", "glShadeModel", a, 1,
                                &ret) == PR_OK);
        }

        uint64_t dc = 0;
        gl_setup(ctx, &dc);
        setup_ortho(ctx);
        a[0] = GL_DEPTH_TEST_B;
        gl_call(ctx, "glDisable", a, 1);          /* somente shading */

        /* -- 1) DEFAULT GL_SMOOTH preservado (sem nunca chamar a API):
         *    interpolacao por baricentro em (185.5,110.5) de T{0,1,2}
         *    = 0.2875*vermelho + 0.5375*verde + 0.175*azul = (73,137,45) -- */
        clear_all(ctx);
        draw_quad_cv(ctx, scr);
        {
            uint32_t got = read_gl_px(ctx, px4, 185, 110);
            int r = (int)((got >> 16) & 0xFF), gg = (int)((got >> 8) & 0xFF),
                b = (int)(got & 0xFF);
            CHECK(r >= 72 && r <= 74);
            CHECK(gg >= 136 && gg <= 138);
            CHECK(b >= 44 && b <= 46);
        }

        /* -- 2) CASO VALIDO do hello_gl12: glShadeModel(GL_FLAT) ->
         *    cor solida do ULTIMO vertice por triangulo (provoking) -- */
        shade_model_call(ctx, 0x1D00);
        clear_all(ctx);
        draw_quad_cv(ctx, scr);
        CHECK(read_gl_px(ctx, px4, 185, 110) == 0x0000FFu);   /* T{0,1,2}=v2 azul */
        CHECK(read_gl_px(ctx, px4, 135, 130) == 0x00FFFF00u); /* T{0,2,3}=v3 amarelo */
        CHECK(read_gl_px(ctx, px4, 150, 130) == 0x00FFFF00u); /* idem, outra sonda */
        CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);

        /* -- 3) GL_SMOOTH restaura a interpolacao (estado comutavel) -- */
        shade_model_call(ctx, 0x1D01);
        clear_all(ctx);
        draw_quad_cv(ctx, scr);
        {
            uint32_t got = read_gl_px(ctx, px4, 185, 110);
            CHECK(got != 0x0000FFu && got != 0x00FFFF00u);    /* nao e' flat */
            int r = (int)((got >> 16) & 0xFF), gg = (int)((got >> 8) & 0xFF),
                b = (int)(got & 0xFF);
            CHECK(r >= 72 && r <= 74 && gg >= 136 && gg <= 138 &&
                  b >= 44 && b <= 46);
        }

        /* -- 4) enum invalido -> 0x500, estado PRESERVADO (continua FLAT) -- */
        shade_model_call(ctx, 0x1D00);
        shade_model_call(ctx, 0x9999u);
        CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0x500u);
        CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
        clear_all(ctx);
        draw_quad_cv(ctx, scr);
        CHECK(read_gl_px(ctx, px4, 185, 110) == 0x0000FFu);   /* FLAT intacto */

        pr_win32_destroy(ctx);
    }

    /* ================= G26: GL_FLAT em glDrawElements + color array ========
     * Causa raiz (comprovada por trace da execucao real do hello_gl12 + objdump
     * + header w32api + registro Khronos): os enums do G22 estavam invertidos
     * (GL_FLAT=0x1D00/GL_SMOOTH=0x1D01 sao os valores canonicos) — o PE
     * chamava glShadeModel(GL_FLAT)=0x1D00 e o runtime o tratava como SMOOTH.
     * 2o defeito (item 9 da investigacao): no flip de winding, o gate FLAT
     * escolhia col[2] (reordenado) em vez do terceiro vertice SUBMETIDO. */

    /* helper: quad do hello_gl12 (4 verts + color array) via glDrawElements
     * GL_UNSIGNED_INT com idx = {0,1,2, 0,2,3} (ou variante de winding);
     * v0 vermelho, v1 verde, v2 azul, v3 amarelo */
    {
        static const unsigned int idx_ccw[6] = { 0, 1, 2, 0, 2, 3 };
        static const unsigned int idx_cw[6]  = { 0, 2, 1, 0, 3, 2 };

        /* -- 1) GL_FLAT + DrawElements + color array (caso EXATO do PE):
         *    T{0,1,2}=v2 azul; T{0,2,3}=v3 amarelo -- */
        {
            pr_win32_ctx* ctx = pr_win32_create(NULL);
            CHECK(ctx != NULL);
            size_t ssz = 0;
            uint8_t* scr = (uint8_t*)pr_win32_scratch(ctx, &ssz);
            CHECK(scr != NULL && ssz >= 512);
            uint8_t* px4 = scr + 384;
            uint64_t a[6];
            uint64_t dc = 0;
            gl_setup(ctx, &dc);
            setup_ortho(ctx);
            a[0] = GL_DEPTH_TEST_B;
            gl_call(ctx, "glDisable", a, 1);
            shade_model_call(ctx, 0x1D00);          /* GL_FLAT canonico */
            clear_all(ctx);
            draw_quad_idx_cv(ctx, scr, idx_ccw);
            CHECK(read_gl_px(ctx, px4, 185, 110) == 0x0000FFu);   /* v2 azul */
            CHECK(read_gl_px(ctx, px4, 135, 130) == 0x00FFFF00u); /* v3 amarelo */
            CHECK(read_gl_px(ctx, px4, 150, 130) == 0x00FFFF00u);
            CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
            pr_win32_destroy(ctx);
        }

        /* -- 2) provoking = terceiro vertice SUBMETIDO, imune ao flip de
         *    winding: idx {0,2,1} -> provoking v1 (verde);
         *    idx {0,3,2} -> provoking v2 (azul) -- */
        {
            pr_win32_ctx* ctx = pr_win32_create(NULL);
            CHECK(ctx != NULL);
            size_t ssz = 0;
            uint8_t* scr = (uint8_t*)pr_win32_scratch(ctx, &ssz);
            CHECK(scr != NULL && ssz >= 512);
            uint8_t* px4 = scr + 384;
            uint64_t a[6];
            uint64_t dc = 0;
            gl_setup(ctx, &dc);
            setup_ortho(ctx);
            a[0] = GL_DEPTH_TEST_B;
            gl_call(ctx, "glDisable", a, 1);
            shade_model_call(ctx, 0x1D00);          /* GL_FLAT canonico */
            clear_all(ctx);
            draw_quad_idx_cv(ctx, scr, idx_cw);
            CHECK(read_gl_px(ctx, px4, 185, 110) == 0x0000FF00u); /* v1 verde */
            CHECK(read_gl_px(ctx, px4, 135, 130) == 0x0000FFu);   /* v2 azul */
            CHECK(gl_call(ctx, "glGetError", NULL, 0) == 0);
            pr_win32_destroy(ctx);
        }

        /* -- 3) GL_SMOOTH continua interpolando na MESMA via DrawElements:
         *    (185,110) = interpolacao baricentrica (73,137,45) -- */
        {
            pr_win32_ctx* ctx = pr_win32_create(NULL);
            CHECK(ctx != NULL);
            size_t ssz = 0;
            uint8_t* scr = (uint8_t*)pr_win32_scratch(ctx, &ssz);
            CHECK(scr != NULL && ssz >= 512);
            uint8_t* px4 = scr + 384;
            uint64_t a[6];
            uint64_t dc = 0;
            gl_setup(ctx, &dc);
            setup_ortho(ctx);
            a[0] = GL_DEPTH_TEST_B;
            gl_call(ctx, "glDisable", a, 1);
            shade_model_call(ctx, 0x1D01);          /* GL_SMOOTH canonico */
            clear_all(ctx);
            draw_quad_idx_cv(ctx, scr, idx_ccw);
            {
                uint32_t got = read_gl_px(ctx, px4, 185, 110);
                int r = (int)((got >> 16) & 0xFF), gg = (int)((got >> 8) & 0xFF),
                    b = (int)(got & 0xFF);
                CHECK(got != 0x0000FFu && got != 0x00FFFF00u);
                CHECK(r >= 72 && r <= 74);
                CHECK(gg >= 136 && gg <= 138);
                CHECK(b >= 44 && b <= 46);
            }
            pr_win32_destroy(ctx);
        }

        /* -- 4) estado GL_FLAT permanece ATIVO durante/entre DrawElements
         *    (duas passagens sem re-chamar a API) -- */
        {
            pr_win32_ctx* ctx = pr_win32_create(NULL);
            CHECK(ctx != NULL);
            size_t ssz = 0;
            uint8_t* scr = (uint8_t*)pr_win32_scratch(ctx, &ssz);
            CHECK(scr != NULL && ssz >= 512);
            uint8_t* px4 = scr + 384;
            uint64_t a[6];
            uint64_t dc = 0;
            gl_setup(ctx, &dc);
            setup_ortho(ctx);
            a[0] = GL_DEPTH_TEST_B;
            gl_call(ctx, "glDisable", a, 1);
            shade_model_call(ctx, 0x1D00);          /* GL_FLAT canonico */
            clear_all(ctx);
            draw_quad_idx_cv(ctx, scr, idx_ccw);
            CHECK(read_gl_px(ctx, px4, 185, 110) == 0x0000FFu);
            clear_all(ctx);
            draw_quad_idx_cv(ctx, scr, idx_ccw);    /* SEM re-set do estado */
            CHECK(read_gl_px(ctx, px4, 185, 110) == 0x0000FFu);
            CHECK(read_gl_px(ctx, px4, 135, 130) == 0x00FFFF00u);
            pr_win32_destroy(ctx);
        }

        /* -- 5) glDrawArrays + GL_FLAT continua correto (via existente) -- */
        {
            pr_win32_ctx* ctx = pr_win32_create(NULL);
            CHECK(ctx != NULL);
            size_t ssz = 0;
            uint8_t* scr = (uint8_t*)pr_win32_scratch(ctx, &ssz);
            CHECK(scr != NULL && ssz >= 512);
            uint8_t* px4 = scr + 384;
            uint64_t a[6];
            uint64_t dc = 0;
            gl_setup(ctx, &dc);
            setup_ortho(ctx);
            a[0] = GL_DEPTH_TEST_B;
            gl_call(ctx, "glDisable", a, 1);
            shade_model_call(ctx, 0x1D00);          /* GL_FLAT canonico */
            clear_all(ctx);
            draw_quad_cv(ctx, scr);                 /* DrawArrays (G22) */
            CHECK(read_gl_px(ctx, px4, 185, 110) == 0x0000FFu);
            CHECK(read_gl_px(ctx, px4, 135, 130) == 0x00FFFF00u);
            pr_win32_destroy(ctx);
        }
        printf("== G26: GL_FLAT em glDrawElements ==\\n");
    }

    /* ---- hello_gl11.exe: galeria em 2 fases a 42 (o tapete coplanar so
     *     aparece com a comparacao correta) ---- */
    {
        size_t ilen = 0;
        uint8_t* img = pe_real_load(
            "Tests/PorticoRuntimeTests/data/hello_gl11.exe", &ilen);
        CHECK(img != NULL && ilen > 0);
        if (!img) return;
        pr_log* log = pr_log_create(1024);
        pr_peproc* p = NULL;
        CHECK(pr_peproc_create(img, ilen, log, &p) == PR_OK);
        CHECK(p != NULL);
        if (!p) { free(img); return; }
        /* glDepthFunc importa e resolve no loader real (1º bloqueador
         * implementado): o prepare PASSA */
        CHECK(pr_peproc_prepare(p) == PR_OK);
        uint64_t exec = 0;
        int i = 0;
        while (i < 5000 && !pr_peproc_exited(p)) {
            if (pr_peproc_step(p, 10000, &exec) != PR_OK) break;
            i++;
        }
        /* G20: a sonda do PE foi corrigida (somente coordenadas/valores de
         * validacao) — o caminho completo EXIGE rc=42; qualquer outro estado
         * (outro exit code ou EXECUTION STOPPED) e' falha. */
        CHECK(pr_peproc_exited(p) && pr_peproc_exit_code(p) == 42u);
        if (pr_peproc_exited(p) && pr_peproc_exit_code(p) == 42u) {
            /* validacao completa do frame final (fase 2) na superficie */
            pr_surf* sp = pr_win32_surface(pr_peproc_win32(p));
            CHECK(sp != NULL);
            if (sp) {
                CHECK(surf_px(sp, 235, 85) == 0x00FFFFFFu);   /* texel branco */
                CHECK(surf_px(sp, 100, 140) == 0x0000FFu);    /* tapete azul (T2) */
                CHECK(surf_px(sp, 140, 145) == 0x0000FFu);    /* tapete azul (T2) */
                CHECK(surf_px(sp, 190, 125) == 0x00FF0000u);  /* caixa A' vermelha */
                CHECK(surf_px(sp, 255, 120) == 0x0000FF00u);  /* caixa B verde */
                {
                    uint32_t got = surf_px(sp, 45, 45);       /* HUD 50% */
                    CHECK(got == 0x007F7F00u || got == 0x00808000u);
                    got = surf_px(sp, 150, 85);               /* sombra 50% */
                    CHECK(got == 0x007F7F00u || got == 0x00808000u);
                }
                CHECK(surf_px(sp, 275, 30) == 0x00FF0000u);   /* marcador vermelho */
                CHECK(surf_px(sp, 50, 90) == 0x00BEBE41u);    /* parede (190,190,65) */
                CHECK(surf_px(sp, 265, 165) == 0x001F1FE0u);  /* parede (31,31,224) */
                CHECK(surf_px(sp, 300, 200) == 0x00000000u);  /* fundo */
            }
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
