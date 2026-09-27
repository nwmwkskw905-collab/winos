/* pr_gl.c — rasterizador por software do subconjunto REAL do OpenGL 1.1
 * exercitado pelo PE de controle (GRUPO 8). Implementação própria a partir
 * da especificação pública do GL 1.1: pipeline vértice→clip→viewport→
 * rasterização barycentrica (regra top-left)→z-buffer (GL_LESS)→framebuffer
 * XRGB8888 (origem inferior-esquerda, como o GL). glEnd produz os pixels de
 * forma síncrona; glFinish confirma (sem comandos pendentes).
 *
 * FORA do subconjunto: matrix stack, texturas, iluminação, alpha-func,
 * blend, GL_QUADS/LINES/POINTS, glReadPixels != (GL_RGBA,GL_UNSIGNED_BYTE),
 * glGet* — cada um permanece não implementado (EXECUTION STOPPED no
 * dispatch) ou sinaliza erro GL real quando o PE chama o subconjunto
 * existente com parâmetros inválidos (GL_INVALID_ENUM/VALUE/OPERATION). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "portico/pr_gl.h"
#include "portico/pr_win32.h"
#include "portico/pr_surf.h"
#include "portico/pr_log.h"

/* constantes públicas GL 1.1 (especificação Khronos) */
#define GL_NO_ERROR          0u
#define GL_INVALID_ENUM      0x0500u
#define GL_INVALID_VALUE     0x0501u
#define GL_INVALID_OPERATION 0x0502u
#define GL_OUT_OF_MEMORY     0x0505u
#define GL_TRIANGLES         0x0004u
#define GL_DEPTH_BUFFER_BIT  0x00000100u
#define GL_COLOR_BUFFER_BIT  0x00004000u
#define GL_DEPTH_TEST        0x0B71u
/* G18: modos de comparacao do teste de profundidade (glDepthFunc) */
/* G26: valores canonicos (Khronos gl.h / glad / Android GL10 / w32api):
 * GL_FLAT = 0x1D00 e GL_SMOOTH = 0x1D01. Os defines do G22 estavam
 * invertidos — causa raiz da divergencia das sondas do hello_gl12. */
#define GL_FLAT              0x1D00u
#define GL_SMOOTH            0x1D01u
#define GL_NEVER             0x0200u
#define GL_LESS              0x0201u
#define GL_EQUAL             0x0202u
#define GL_LEQUAL            0x0203u
#define GL_GREATER           0x0204u
#define GL_NOTEQUAL          0x0205u
#define GL_GEQUAL            0x0206u
#define GL_ALWAYS            0x0207u
#define GL_UNSIGNED_BYTE     0x1401u
#define GL_RGBA              0x1908u
#define GL_MODELVIEW         0x1700u
#define GL_PROJECTION        0x1701u
#define GL_FLOAT             0x1406u
#define GL_UNSIGNED_SHORT    0x1403u
#define GL_UNSIGNED_INT      0x1405u
#define GL_VERTEX_ARRAY      0x8074u
#define GL_COLOR_ARRAY       0x8076u
#define GL_STACK_OVERFLOW     0x0503u
#define GL_STACK_UNDERFLOW    0x0504u
#define GL_TEXTURE_2D         0x0DE1u
#define GL_TEXTURE_COORD_ARRAY 0x8078u
#define GL_TEXTURE_MAG_FILTER 0x2800u
#define GL_TEXTURE_MIN_FILTER 0x2801u
#define GL_TEXTURE_WRAP_S     0x2802u
#define GL_TEXTURE_WRAP_T     0x2803u
#define GL_NEAREST            0x2600u
#define GL_REPEAT             0x2901u
#define GL_RGB                0x1907u
#define GL_NORMAL_ARRAY       0x8075u
#define GL_BLEND              0x0BE2u
#define GL_LIGHTING           0x0B50u
#define GL_LIGHT0             0x4000u
#define GL_ZERO               0u
#define GL_ONE                1u
#define GL_SRC_ALPHA          0x0302u
#define GL_ONE_MINUS_SRC_ALPHA 0x0303u
#define GL_FRONT              0x0404u
#define GL_BACK               0x0405u
#define GL_FRONT_AND_BACK     0x0408u
#define GL_AMBIENT            0x1200u
#define GL_DIFFUSE            0x1201u
#define GL_SPECULAR           0x1202u
#define GL_POSITION           0x1203u
#define GL_SPOT_DIRECTION     0x1204u
#define GL_SPOT_EXPONENT      0x1205u
#define GL_SPOT_CUTOFF        0x1206u
#define GL_CONSTANT_ATTENUATION 0x1207u
#define GL_LINEAR_ATTENUATION   0x1208u
#define GL_QUADRATIC_ATTENUATION 0x1209u
#define GL_EMISSION           0x1600u
#define GL_SHININESS          0x1601u
#define GL_AMBIENT_AND_DIFFUSE 0x1602u
#define GL9_MAX_IDX          4096

#define GL_MAX_CTX   4
#define GL_MAX_VERTS 256
#define GL_HGLRC_BASE 0xD7000000u

typedef struct gl_vertex {
    float x, y, z;      /* clip-space */
    float col[3];
    float u, v;         /* GRUPO 10: UV (texto) */
    float iw;           /* GRUPO 10: 1/w_clip (perspectiva correta) */
    float a;            /* GRUPO 11: alpha (blend) */
} gl_vertex;

/* GRUPO 11: luz + material (subconjunto fixed-function) */
typedef struct gl_light {
    float amb[4], diff[4], spec[4];
    float pos[4];       /* eye-space (GL_POSITION transformado pelo MV) */
    float att_c, att_l, att_q;
    int on;
} gl_light;
typedef struct gl_material {
    float amb[4], diff[4], spec[4], emiss[4];
    float shine;
} gl_material;

/* GRUPO 10: objeto textura (subconjunto RGB888 level 0) */
typedef struct gl_tex_obj {
    int used, defined;
    int w, h;
    unsigned char* texels;   /* RGB888 (w*h*3) */
    int min_f, mag_f;
} gl_tex_obj;

typedef struct gl_vtxarr {
    int enabled;        /* glEnableClientState */
    int size, type, stride;
    uint64_t addr;      /* ponteiro de CONVIDADO (validado no draw) */
} gl_vtxarr;

typedef struct gl_ctx {
    int used;
    uint32_t handle;
    uint32_t w, h;
    uint32_t* color;    /* XRGB8888, origem inferior-esquerda */
    float*    depth;    /* [0,1] */
    float clear_r, clear_g, clear_b, clear_a;
    float clear_d;
    int depth_test;
    uint32_t depth_func;   /* G18: glDepthFunc, default GL_LESS */
    int depth_write;       /* G21: glDepthMask, default GL_TRUE */
    unsigned shade_model;  /* G22: glShadeModel, default GL_SMOOTH */
    int vp_x, vp_y, vp_w, vp_h;
    unsigned error;     /* sticky-first (semântica GL real) */
    int in_begin;
    unsigned begin_mode;
    float cur_col[3];
    gl_vertex verts[GL_MAX_VERTS];
    int nverts;
    /* GRUPO 9: matrizes (column-major, pós-multiplicação como o GL) */
    int matrix_mode;
    float mv[16], proj[16];
    gl_vtxarr varr, carr;
    /* GRUPO 10: pilhas reais + texturas + UV array */
    float mv_stack[32][16]; int mv_sp;
    float pj_stack[4][16];  int pj_sp;
    gl_tex_obj tex[GL10_MAX_TEX];
    uint32_t tex_bound;
    int tex_enabled;
    gl_vtxarr tarr;
    /* GRUPO 11: iluminação + blending */
    float cur_n[3];
    float cur_a;
    float cur_uv[2];   /* G14: textura corrente (glTexCoord2f), default 0,0 */
    gl_light lights[8];
    gl_material mat_front;
    int lighting;
    int blend_on;
    unsigned blend_s, blend_d;
    gl_vtxarr narr;
} gl_ctx;

struct pr_gl_state {
    struct pr_win32_ctx* owner;
    gl_ctx ctxs[GL_MAX_CTX];
    gl_ctx* current;
};

/* ------------------------------- utilidades ------------------------------ */

static void set_error(gl_ctx* g, unsigned e) {
    if (g && !g->error) g->error = e;
}

static uint8_t f2b(float v) {
    if (v <= 0.0f) return 0;
    if (v >= 1.0f) return 255;
    return (uint8_t)(v * 255.0f + 0.5f);
}

static float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/* ------------------------------- ciclo de vida --------------------------- */

pr_gl_state* pr_gl_new(struct pr_win32_ctx* owner) {
    pr_gl_state* s = (pr_gl_state*)calloc(1, sizeof(pr_gl_state));
    if (s) s->owner = owner;
    return s;
}

void pr_gl_release(struct pr_win32_ctx* owner) {
    if (!owner) return;
    pr_gl_state* s = (pr_gl_state*)pr_win32_gl_state(owner);
    if (!s) return;
    for (int i = 0; i < GL_MAX_CTX; i++) {
        free(s->ctxs[i].color);
        free(s->ctxs[i].depth);
        for (int t = 0; t < GL10_MAX_TEX; t++) free(s->ctxs[i].tex[t].texels);
    }
    free(s);
    pr_win32_gl_set_state(owner, NULL);
}

static gl_ctx* ctx_by_handle(pr_gl_state* s, uint32_t handle) {
    if (!s) return NULL;
    for (int i = 0; i < GL_MAX_CTX; i++)
        if (s->ctxs[i].used && s->ctxs[i].handle == handle) return &s->ctxs[i];
    return NULL;
}

uint32_t pr_gl_wgl_create(pr_gl_state* s, uint32_t hdc) {
    if (!s || !hdc) return 0;
    int slot = -1;
    for (int i = 0; i < GL_MAX_CTX; i++)
        if (!s->ctxs[i].used) { slot = i; break; }
    if (slot < 0) return 0;
    gl_ctx* g = &s->ctxs[slot];
    memset(g, 0, sizeof(*g));
    /* framebuffer = superfície GDI do caminho certificado (320×240 XRGB) */
    g->w = 320;
    g->h = 240;
    g->color = (uint32_t*)calloc((size_t)g->w * g->h, 4);
    g->depth = (float*)malloc((size_t)g->w * g->h * sizeof(float));
    if (!g->color || !g->depth) {
        free(g->color); free(g->depth);
        g->color = NULL; g->depth = NULL;
        return 0;
    }
    g->clear_d = 1.0f;
    g->depth_func = GL_LESS;   /* G18: preserva o comportamento certificado */
    g->depth_write = 1;        /* G21: GL_TRUE (default real) */
    g->shade_model = GL_SMOOTH; /* G22: default real preservado */
    g->vp_w = (int)g->w;
    g->vp_h = (int)g->h;
    g->matrix_mode = GL_MODELVIEW;
    g->tex[0].used = 1;   /* textura default (nome 0) existe no GL real */
    g->tex[0].min_f = (int)GL_NEAREST;
    g->tex[0].mag_f = (int)GL_NEAREST;
    /* G11: defaults de iluminação do GL real */
    g->cur_n[2] = 1.0f;
    g->cur_a = 1.0f;
    for (int li = 0; li < 8; li++) {
        gl_light* L = &g->lights[li];
        L->amb[3] = 1.0f; L->diff[3] = 1.0f; L->spec[3] = 1.0f;
        if (li == 0) L->diff[0] = L->diff[1] = L->diff[2] = 1.0f;
        L->pos[2] = 1.0f;          /* direcional (0,0,1,0) */
        L->att_c = 1.0f;
    }
    g->mat_front.amb[0] = g->mat_front.amb[1] = g->mat_front.amb[2] = 0.2f;
    g->mat_front.amb[3] = 1.0f;
    g->mat_front.diff[0] = g->mat_front.diff[1] = g->mat_front.diff[2] = 0.8f;
    g->mat_front.diff[3] = 1.0f;
    g->mat_front.spec[3] = 1.0f;
    g->mat_front.emiss[3] = 1.0f;
    for (int k = 0; k < 16; k++) {
        g->mv[k] = (k % 5 == 0) ? 1.0f : 0.0f;
        g->proj[k] = (k % 5 == 0) ? 1.0f : 0.0f;
    }
    g->handle = GL_HGLRC_BASE + 1u + (uint32_t)slot;
    g->used = 1;
    for (uint32_t i = 0; i < g->w * g->h; i++) g->depth[i] = 1.0f;
    return g->handle;
}

int pr_gl_wgl_make_current(pr_gl_state* s, uint32_t hdc, uint32_t hglrc) {
    if (!s || !hdc) return 0;
    if (hglrc == 0) { s->current = NULL; return 1; }
    gl_ctx* g = ctx_by_handle(s, hglrc);
    if (!g) return 0;
    s->current = g;
    return 1;
}

int pr_gl_wgl_delete(pr_gl_state* s, uint32_t hglrc) {
    gl_ctx* g = ctx_by_handle(s, hglrc);
    if (!g) return 0;
    if (s->current == g) s->current = NULL;
    free(g->color);
    free(g->depth);
    for (int t = 0; t < GL10_MAX_TEX; t++) free(g->tex[t].texels);
    memset(g, 0, sizeof(*g));
    return 1;
}

/* --------------------------- apresentação (frame) ------------------------ */

int pr_gl_swap_buffers(pr_gl_state* s, struct pr_surf* target) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g || !target) return 0;
    if (pr_surf_width(target) != g->w || pr_surf_height(target) != g->h)
        return 0;
    uint32_t* sp = pr_surf_pixels(target);
    if (!sp) return 0;
    /* GL (origem inferior-esq.) → superfície GDI (origem superior-esq.) */
    for (uint32_t y = 0; y < g->h; y++)
        memcpy(&sp[(g->h - 1 - y) * g->w], &g->color[y * g->w], g->w * 4);
    {
        char msg[96];
        snprintf(msg, sizeof(msg),
                 "[GL] frame presented %ux%u (SurfaceBridge/BGRA8 path)",
                 g->w, g->h);
        pr_win32_gl_log(s->owner, msg);
    }
    return 1;
}

/* ------------------------------ estados GL ------------------------------- */

void pr_gl_viewport(pr_gl_state* s, int x, int y, int w, int h) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (w < 0 || h < 0) { set_error(g, GL_INVALID_VALUE); return; }
    g->vp_x = x; g->vp_y = y; g->vp_w = w; g->vp_h = h;
}

void pr_gl_clear_color(pr_gl_state* s, float r, float g_, float b, float a) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    g->clear_r = clamp01(r);
    g->clear_g = clamp01(g_);
    g->clear_b = clamp01(b);
    g->clear_a = clamp01(a);
}

void pr_gl_clear_depth(pr_gl_state* s, double d) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    g->clear_d = (float)(d < 0.0 ? 0.0 : (d > 1.0 ? 1.0 : d));
}

void pr_gl_enable(pr_gl_state* s, unsigned cap) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (cap == GL_DEPTH_TEST) { g->depth_test = 1; return; }
    if (cap == GL_TEXTURE_2D) { g->tex_enabled = 1; return; }  /* G10 */
    if (cap == GL_LIGHTING) { g->lighting = 1; return; }       /* G11 */
    if (cap == GL_BLEND) { g->blend_on = 1; return; }
    if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + 8) {
        g->lights[cap - GL_LIGHT0].on = 1;
        return;
    }
    set_error(g, GL_INVALID_ENUM);
}

void pr_gl_disable(pr_gl_state* s, unsigned cap) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (cap == GL_DEPTH_TEST) { g->depth_test = 0; return; }
    if (cap == GL_TEXTURE_2D) { g->tex_enabled = 0; return; }
    if (cap == GL_LIGHTING) { g->lighting = 0; return; }
    if (cap == GL_BLEND) { g->blend_on = 0; return; }
    if (cap >= GL_LIGHT0 && cap < GL_LIGHT0 + 8) {
        g->lights[cap - GL_LIGHT0].on = 0;
        return;
    }
    set_error(g, GL_INVALID_ENUM);   /* espelho de glEnable */
}

/* G18: glDepthFunc — funcao de comparacao do teste de profundidade.
 * Espelho da familia de estado (pr_gl_enable): sem contexto -> silencioso.
 * Enum fora dos 8 modos -> GL_INVALID_ENUM. O default GL_LESS inicializado
 * no wglCreateContext preserva o comportamento certificado bit a bit. */
void pr_gl_depth_func(pr_gl_state* s, unsigned func) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    switch (func) {
    case GL_NEVER: case GL_LESS: case GL_EQUAL: case GL_LEQUAL:
    case GL_GREATER: case GL_NOTEQUAL: case GL_GEQUAL: case GL_ALWAYS:
        g->depth_func = func;
        return;
    default:
        set_error(g, GL_INVALID_ENUM);
    }
}

/* GRUPO 21: glDepthMask(flag) — liga/desliga escritas no z-buffer
 * (flag != 0 = true; qualquer valor e' aceito, semântica GLboolean).
 * Sem erros de enum: nao ha enum. Afeta fragmentos E glClear de
 * profundidade (semântica GL real). Sem contexto -> silencioso. */
void pr_gl_depth_mask(pr_gl_state* s, int enabled) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    g->depth_write = (enabled != 0);
}

/* GRUPO 22: glShadeModel(mode) — GL_SMOOTH (default, interpolacao por
 * baricentro) ou GL_FLAT (cor do ultimo vertice = provoking vertex).
 * Enum fora dos dois -> GL_INVALID_ENUM com estado preservado. Sem contexto
 * -> silencioso (familia de estado). */
void pr_gl_shade_model(pr_gl_state* s, unsigned mode) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    switch (mode) {
    case GL_SMOOTH: case GL_FLAT:
        g->shade_model = mode;
        return;
    default:
        set_error(g, GL_INVALID_ENUM);
    }
}

/* G18: comparacao parametrizada do z-buffer. GL_LESS = a expressao
 * certificada original (rejeita z >= d), preservada bit a bit. */
static int depth_passes(uint32_t fn, float z, float d) {
    switch (fn) {
    case GL_NEVER:    return 0;
    case GL_LESS:     return !(z >= d);
    case GL_EQUAL:    return z == d;
    case GL_LEQUAL:   return z <= d;
    case GL_GREATER:  return z > d;
    case GL_NOTEQUAL: return z != d;
    case GL_GEQUAL:   return z >= d;
    default:          return 1;              /* GL_ALWAYS */
    }
}

void pr_gl_clear(pr_gl_state* s, unsigned mask) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (mask & ~(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)) {
        set_error(g, GL_INVALID_VALUE);
        return;
    }
    if (mask & GL_COLOR_BUFFER_BIT) {
        uint32_t c = ((uint32_t)f2b(g->clear_r) << 16) |
                     ((uint32_t)f2b(g->clear_g) << 8) |
                     (uint32_t)f2b(g->clear_b);
        for (uint32_t i = 0; i < g->w * g->h; i++) g->color[i] = c;
    }
    if (mask & GL_DEPTH_BUFFER_BIT)
        if (g->depth_write)   /* G21: glClear de profundidade respeita glDepthMask */
        for (uint32_t i = 0; i < g->w * g->h; i++) g->depth[i] = g->clear_d;
}

/* G13: consultas glGetString — strings estáveis e coerentes com o backend
 * REAL (rasterizador por software do WinOS -> Metal; não há GPU Windows).
 * GL_EXTENSIONS vazio: nenhuma extensão inventada. Enum inválido ->
 * GL_INVALID_ENUM; dentro de glBegin -> GL_INVALID_OPERATION + NULL (GL real). */
const char* pr_gl_get_string(pr_gl_state* s, unsigned name) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return NULL;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return NULL; }
    switch (name) {
    case 0x1F00: return "Portico";                    /* GL_VENDOR */
    case 0x1F01: return "Portico SoftRaster/Metal";   /* GL_RENDERER */
    case 0x1F02: return "1.1 Portico Subset";         /* GL_VERSION */
    case 0x1F03: return "";                           /* GL_EXTENSIONS */
    default: set_error(g, GL_INVALID_ENUM); return NULL;
    }
}

/* G14: glGetIntegerv — SOMENTE os pnames consultados por hello_gl7.exe.
 * Valores = capacidades REAIS confirmadas no código: lights[8] (G11);
 * tex_image2d recusa > 1024 (0x501); mv_stack[32] com overflow no 33o push;
 * pj_stack[4] com overflow no 5o push (GL_STACK_OVERFLOW 0x0503); viewport =
 * estado real de pr_gl_viewport (default 0,0,w,h). Pname inválido ->
 * GL_INVALID_ENUM com a saída INTOCADA; em glBegin -> GL_INVALID_OPERATION. */
void pr_gl_get_integer_v(pr_gl_state* s, unsigned name, int* out) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g || !out) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    switch (name) {
    case 0x0BA2:   /* GL_VIEWPORT (4 ints) */
        out[0] = g->vp_x; out[1] = g->vp_y; out[2] = g->vp_w; out[3] = g->vp_h;
        return;
    case 0x0D31:   /* GL_MAX_LIGHTS */
        *out = 8;
        return;
    case 0x0D33:   /* GL_MAX_TEXTURE_SIZE */
        *out = 1024;
        return;
    case 0x0D36:   /* GL_MAX_MODELVIEW_STACK_DEPTH */
        *out = 32;
        return;
    case 0x0D38:   /* GL_MAX_PROJECTION_STACK_DEPTH */
        *out = 4;
        return;
    default:
        set_error(g, GL_INVALID_ENUM);
        return;
    }
}

unsigned pr_gl_get_error(pr_gl_state* s) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return GL_INVALID_OPERATION;
    unsigned e = g->error;
    g->error = GL_NO_ERROR;
    return e;
}

void pr_gl_finish(pr_gl_state* s) {
    (void)s;   /* rasterização síncrona em glEnd: nada pendente */
}

/* ---------------------- primitivas (triângulos) -------------------------- */

void pr_gl_begin(pr_gl_state* s, unsigned mode) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (mode != GL_TRIANGLES) {
        set_error(g, GL_INVALID_ENUM);   /* subconjunto: só GL_TRIANGLES */
        return;
    }
    g->in_begin = 1;
    g->begin_mode = mode;
    g->nverts = 0;
}

/* GRUPO 11: iluminação fixed-function (Gouraud por vértice, eye-space).
 * cor = emissão + ambiente·globalAmb(0.2,0.2,0.2,1) + Σ luzes
 *   [att·(amb·mat.amb) + max(N·L,0)·att·(dif·mat.dif) + Blinn especular].
 * Normal transformada pelo MV SEM renormalizar (GL real, sem GL_NORMALIZE). */
static void g11_light_vertex(gl_ctx* g, float wx, float wy, float wz,
                             const float* n, float* rgb, float* alpha) {
    const float* m = g->mv;
    float ex = m[0] * wx + m[4] * wy + m[8] * wz + m[12];
    float ey = m[1] * wx + m[5] * wy + m[9] * wz + m[13];
    float ez = m[2] * wx + m[6] * wy + m[10] * wz + m[14];
    float nx = m[0] * n[0] + m[4] * n[1] + m[8] * n[2];
    float ny = m[1] * n[0] + m[5] * n[1] + m[9] * n[2];
    float nz = m[2] * n[0] + m[6] * n[1] + m[10] * n[2];
    float c[3] = {
        g->mat_front.emiss[0] + 0.2f * g->mat_front.amb[0],
        g->mat_front.emiss[1] + 0.2f * g->mat_front.amb[1],
        g->mat_front.emiss[2] + 0.2f * g->mat_front.amb[2]
    };
    float ca = g->mat_front.emiss[3] + 0.2f * g->mat_front.amb[3];
    for (int i = 0; i < 8; i++) {
        gl_light* L = &g->lights[i];
        if (!L->on) continue;
        float lx = L->pos[0], ly = L->pos[1], lz = L->pos[2], att = 1.0f;
        float d = sqrtf(lx * lx + ly * ly + lz * lz);
        if (L->pos[3] != 0.0f) {
            lx -= ex; ly -= ey; lz -= ez;
            d = sqrtf(lx * lx + ly * ly + lz * lz);
            float den = L->att_c + L->att_l * d + L->att_q * d * d;
            att = (den != 0.0f) ? (1.0f / den) : 0.0f;
        }
        if (d > 0.0f) { lx /= d; ly /= d; lz /= d; }
        c[0] += att * (L->amb[0] * g->mat_front.amb[0]);
        c[1] += att * (L->amb[1] * g->mat_front.amb[1]);
        c[2] += att * (L->amb[2] * g->mat_front.amb[2]);
        ca += att * (L->amb[3] * g->mat_front.amb[3]);
        float ndl = nx * lx + ny * ly + nz * lz;
        if (ndl > 0.0f) {
            c[0] += att * ndl * (L->diff[0] * g->mat_front.diff[0]);
            c[1] += att * ndl * (L->diff[1] * g->mat_front.diff[1]);
            c[2] += att * ndl * (L->diff[2] * g->mat_front.diff[2]);
            ca += att * ndl * (L->diff[3] * g->mat_front.diff[3]);
            float hx = lx, hy = ly, hz = lz + 1.0f;
            float hd = sqrtf(hx * hx + hy * hy + hz * hz);
            if (hd > 0.0f) { hx /= hd; hy /= hd; hz /= hd; }
            float ns = nx * hx + ny * hy + nz * hz;
            if (ns > 0.0f && g->mat_front.shine > 0.0f) {
                float sp = powf(ns, g->mat_front.shine) * att;
                c[0] += sp * (L->spec[0] * g->mat_front.spec[0]);
                c[1] += sp * (L->spec[1] * g->mat_front.spec[1]);
                c[2] += sp * (L->spec[2] * g->mat_front.spec[2]);
                ca += sp * (L->spec[3] * g->mat_front.spec[3]);
            }
        }
    }
    rgb[0] = clamp01(c[0]); rgb[1] = clamp01(c[1]); rgb[2] = clamp01(c[2]);
    *alpha = clamp01(ca);
}

void pr_gl_color3f(pr_gl_state* s, float r, float g_, float b) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    g->cur_col[0] = clamp01(r);
    g->cur_col[1] = clamp01(g_);
    g->cur_col[2] = clamp01(b);
}

void pr_gl_color4f(pr_gl_state* s, float r, float g_, float b, float a_) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    g->cur_col[0] = clamp01(r);
    g->cur_col[1] = clamp01(g_);
    g->cur_col[2] = clamp01(b);
    g->cur_a = clamp01(a_);
}

void pr_gl_normal3f(pr_gl_state* s, float x, float y, float z) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    /* GL real: glNormal3f é atributo de vértice — VÁLIDO dentro de glBegin */
    g->cur_n[0] = x; g->cur_n[1] = y; g->cur_n[2] = z;
}

/* G14: glTexCoord2f — atributo de vértice: BEGIN-SAFE (como glNormal3f/glColor3f
 * em GL real); define o UV corrente levado pelos próximos glVertex3f. */
void pr_gl_tex_coord2f(pr_gl_state* s, float u, float v) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    g->cur_uv[0] = u;
    g->cur_uv[1] = v;
}

void pr_gl_vertex3f(pr_gl_state* s, float x, float y, float z) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (!g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (g->nverts >= GL_MAX_VERTS) { set_error(g, GL_OUT_OF_MEMORY); return; }
    gl_vertex* v = &g->verts[g->nverts++];
    v->x = x; v->y = y; v->z = z;
    v->u = g->cur_uv[0]; v->v = g->cur_uv[1]; v->iw = 1.0f;
    v->a = g->cur_a;
    if (g->lighting) {
        /* G11: cor do vértice vem da iluminação (glColor ignorado, GL real) */
        g11_light_vertex(g, x, y, z, g->cur_n, v->col, &v->a);
    } else {
        v->col[0] = g->cur_col[0];
        v->col[1] = g->cur_col[1];
        v->col[2] = g->cur_col[2];
    }
}

static void raster_list(gl_ctx* g, const gl_vertex* vs, int n);
static int g10_fetch_vertex(gl_ctx* g, uint32_t vi, gl_vertex* out,
                            pr_gl_ptr_fn resolve, void* ud);

/* edge function (2× área orientada) */
static float edge2(float ax, float ay, float bx, float by, float px, float py) {
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static int edge_is_top_left(float ax, float ay, float bx, float by) {
    /* convenção y para cima (janela GL): borda superior (horizontal,
     * apontando para -x) ou borda esquerda (y cresce) */
    if (ay == by) return ax > bx;
    return by > ay;
}

static void raster_tri(gl_ctx* g, const gl_vertex* a, const gl_vertex* b,
                       const gl_vertex* c) {
    /* GRUPO 10: textura ativa (objeto completo) para amostragem */
    const gl_tex_obj* tx = NULL;
    if (g->tex_enabled) {
        gl_tex_obj* o = &g->tex[g->tex_bound];
        if (o->used && o->defined && o->w > 0 && o->h > 0 && o->texels) tx = o;
    }
    /* viewport: clip → janela GL (origem inferior-esquerda) */
    float vx[3], vy[3], vz[3];
    const gl_vertex* vv[3] = { a, b, c };
    for (int i = 0; i < 3; i++) {
        vx[i] = (float)g->vp_x + (vv[i]->x + 1.0f) * 0.5f * (float)g->vp_w;
        vy[i] = (float)g->vp_y + (vv[i]->y + 1.0f) * 0.5f * (float)g->vp_h;
        vz[i] = clamp01((vv[i]->z + 1.0f) * 0.5f);   /* NDC z → [0,1] */
    }
    float area = edge2(vx[0], vy[0], vx[1], vy[1], vx[2], vy[2]);
    if (area == 0.0f) return;
    /* dois lados (GL sem culling por padrão): normaliza p/ CCW */
    int flip = 0;
    if (area < 0.0f) {
        float t;
        t = vx[1]; vx[1] = vx[2]; vx[2] = t;
        t = vy[1]; vy[1] = vy[2]; vy[2] = t;
        t = vz[1]; vz[1] = vz[2]; vz[2] = t;
        area = -area;
        flip = 1;
    }
    const gl_vertex* col[3] = { a, b, c };
    if (flip) {
        col[1] = c; col[2] = b;
        col[0] = a;
    }

    int minx = (int)floorf(fminf(vx[0], fminf(vx[1], vx[2])) - 0.5f);
    int maxx = (int)ceilf(fmaxf(vx[0], fmaxf(vx[1], vx[2])) - 0.5f);
    int miny = (int)floorf(fminf(vy[0], fminf(vy[1], vy[2])) - 0.5f);
    int maxy = (int)ceilf(fmaxf(vy[0], fmaxf(vy[1], vy[2])) - 0.5f);
    if (minx < 0) minx = 0;
    if (miny < 0) miny = 0;
    if (maxx > (int)g->w - 1) maxx = (int)g->w - 1;
    if (maxy > (int)g->h - 1) maxy = (int)g->h - 1;

    int tl[3];
    tl[0] = edge_is_top_left(vx[1], vy[1], vx[2], vy[2]);
    tl[1] = edge_is_top_left(vx[2], vy[2], vx[0], vy[0]);
    tl[2] = edge_is_top_left(vx[0], vy[0], vx[1], vy[1]);

    for (int py = miny; py <= maxy; py++) {
        for (int px = minx; px <= maxx; px++) {
            float pxf = (float)px + 0.5f, pyf = (float)py + 0.5f;
            float w0 = edge2(vx[1], vy[1], vx[2], vy[2], pxf, pyf);
            float w1 = edge2(vx[2], vy[2], vx[0], vy[0], pxf, pyf);
            float w2 = edge2(vx[0], vy[0], vx[1], vy[1], pxf, pyf);
            /* regra top-left (bordas compartilhadas determinísticas) */
            if (!((w0 > 0 || (w0 == 0 && tl[0])) &&
                  (w1 > 0 || (w1 == 0 && tl[1])) &&
                  (w2 > 0 || (w2 == 0 && tl[2]))))
                continue;
            float b0 = w0 / area, b1 = w1 / area, b2 = w2 / area;
            float z = b0 * vz[0] + b1 * vz[1] + b2 * vz[2];
            uint32_t idx = (uint32_t)py * g->w + (uint32_t)px;
            if (g->depth_test && !depth_passes(g->depth_func, z,
                                               g->depth[idx]))
                continue;   /* G18: GL_LESS default rejeita z >= d */
            if (g->depth_write) g->depth[idx] = z;   /* G21: glDepthMask */
            float r, gg, bl;
            if (g->shade_model == GL_FLAT) {
                /* GL_FLAT — cor do provoking vertex = terceiro vertice
                 * SUBMETIDO (c), conforme a convencao GL/GL_TRIANGLES; o
                 * flip de winding nao pode troca-lo (G26: col[2] seguia a
                 * reordenacao de raster e escolhia o vertice errado). */
                r = c->col[0];
                gg = c->col[1];
                bl = c->col[2];
            } else {
                /* G22: GL_SMOOTH (default) — interpolacao por baricentro */
                r = b0 * col[0]->col[0] + b1 * col[1]->col[0] + b2 * col[2]->col[0];
                gg = b0 * col[0]->col[1] + b1 * col[1]->col[1] + b2 * col[2]->col[1];
                bl = b0 * col[0]->col[2] + b1 * col[1]->col[2] + b2 * col[2]->col[2];
            }
            /* GRUPO 10: textura MODULATE + GL_NEAREST + GL_REPEAT com
             * interpolação de UV perspectiva-corrreta (1/w). Textura
             * incompleta = texturing disabled (comportamento GL real). */
            if (tx) {
                float iwp = b0 * col[0]->iw + b1 * col[1]->iw + b2 * col[2]->iw;
                if (iwp != 0.0f) {
                    float uu = (b0 * col[0]->u * col[0]->iw +
                                b1 * col[1]->u * col[1]->iw +
                                b2 * col[2]->u * col[2]->iw) / iwp;
                    float tv = (b0 * col[0]->v * col[0]->iw +
                                b1 * col[1]->v * col[1]->iw +
                                b2 * col[2]->v * col[2]->iw) / iwp;
                    int t_x = (int)floorf(uu * (float)tx->w);
                    int t_y = (int)floorf(tv * (float)tx->h);
                    t_x %= tx->w; if (t_x < 0) t_x += tx->w;
                    t_y %= tx->h; if (t_y < 0) t_y += tx->h;
                    const unsigned char* te = &tx->texels[
                        ((size_t)t_y * (size_t)tx->w + (size_t)t_x) * 3];
                    r = r * ((float)te[0] / 255.0f);
                    gg = gg * ((float)te[1] / 255.0f);
                    bl = bl * ((float)te[2] / 255.0f);
                }
            }
            /* G11: blending real src·sf + dst·df (alpha Gouraud) */
            if (g->blend_on) {
                float sa = b0 * col[0]->a + b1 * col[1]->a + b2 * col[2]->a;
                sa = clamp01(sa);
                float sf = (g->blend_s == GL_ONE) ? 1.0f
                         : (g->blend_s == GL_SRC_ALPHA) ? sa
                         : (g->blend_s == GL_ONE_MINUS_SRC_ALPHA) ? (1.0f - sa)
                         : 0.0f;
                float df = (g->blend_d == GL_ONE) ? 1.0f
                         : (g->blend_d == GL_SRC_ALPHA) ? sa
                         : (g->blend_d == GL_ONE_MINUS_SRC_ALPHA) ? (1.0f - sa)
                         : 0.0f;
                uint32_t fb = g->color[idx];
                float dr = (float)((fb >> 16) & 255u) / 255.0f;
                float dg = (float)((fb >> 8) & 255u) / 255.0f;
                float db = (float)(fb & 255u) / 255.0f;
                r = clamp01(r * sf + dr * df);
                gg = clamp01(gg * sf + dg * df);
                bl = clamp01(bl * sf + db * df);
            }
            g->color[idx] = ((uint32_t)f2b(r) << 16) |
                            ((uint32_t)f2b(gg) << 8) |
                            (uint32_t)f2b(bl);
        }
    }
}

void pr_gl_end(pr_gl_state* s) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (!g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    g->in_begin = 0;
    if (g->nverts % 3 != 0) {
        /* vértices órfãos = erro real de uso */
        set_error(g, GL_INVALID_OPERATION);
    }
    raster_list(g, g->verts, g->nverts);   /* aplica P·Mv (identidade = G8) */
    g->nverts = 0;
}

/* ---------------- GRUPO 9: matrizes (column-major, pós-multiplicação) ----- */

static void mat_mul(float d[16], const float a[16], const float b[16]) {
    float r[16];
    for (int c = 0; c < 4; c++)
        for (int ro = 0; ro < 4; ro++) {
            float s = 0.0f;
            for (int k = 0; k < 4; k++)
                s += a[k * 4 + ro] * b[c * 4 + k];
            r[c * 4 + ro] = s;
        }
    memcpy(d, r, sizeof(r));
}

void pr_gl_matrix_mode(pr_gl_state* s, unsigned mode) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION) {
        set_error(g, GL_INVALID_ENUM);
        return;
    }
    g->matrix_mode = (int)mode;
}

void pr_gl_load_identity(pr_gl_state* s) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    float* m = (g->matrix_mode == GL_PROJECTION) ? g->proj : g->mv;
    for (int k = 0; k < 16; k++) m[k] = (k % 5 == 0) ? 1.0f : 0.0f;
}

void pr_gl_ortho(pr_gl_state* s, double l, double r, double b, double t,
                 double n, double f) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (l == r || b == t || n == f) { set_error(g, GL_INVALID_VALUE); return; }
    float o[16] = {
        (float)(2.0 / (r - l)), 0, 0, 0,
        0, (float)(2.0 / (t - b)), 0, 0,
        0, 0, (float)(-2.0 / (f - n)), 0,
        (float)(-(r + l) / (r - l)), (float)(-(t + b) / (t - b)),
        (float)(-(f + n) / (f - n)), 1.0f
    };
    float* m = (g->matrix_mode == GL_PROJECTION) ? g->proj : g->mv;
    mat_mul(m, m, o);
}

void pr_gl_translatef(pr_gl_state* s, float x, float y, float z) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    float o[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, x,y,z,1 };
    float* m = (g->matrix_mode == GL_PROJECTION) ? g->proj : g->mv;
    mat_mul(m, m, o);
}

void pr_gl_rotatef(pr_gl_state* s, float angle_deg, float x, float y, float z) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    float len = sqrtf(x * x + y * y + z * z);
    if (len == 0.0f) return;   /* GL real: rotação com eixo nulo = identidade */
    x /= len; y /= len; z /= len;
    float a = angle_deg * 3.14159265358979f / 180.0f;
    float c = cosf(a), sN = sinf(a), ic = 1.0f - c;
    float o[16] = {
        x * x * ic + c,     y * x * ic + z * sN, z * x * ic - y * sN, 0,
        x * y * ic - z * sN, y * y * ic + c,     z * y * ic + x * sN, 0,
        x * z * ic + y * sN, y * z * ic - x * sN, z * z * ic + c,     0,
        0, 0, 0, 1
    };
    float* m = (g->matrix_mode == GL_PROJECTION) ? g->proj : g->mv;
    mat_mul(m, m, o);
}

/* vértice → NDC (M = proj·mv; divisão perspectiva real) */
static void xform_ndc(const float pm[16], float x, float y, float z,
                      float* ox, float* oy, float* oz, float* ow) {
    float cx = pm[0] * x + pm[4] * y + pm[8] * z + pm[12];
    float cy = pm[1] * x + pm[5] * y + pm[9] * z + pm[13];
    float cz = pm[2] * x + pm[6] * y + pm[10] * z + pm[14];
    float cw = pm[3] * x + pm[7] * y + pm[11] * z + pm[15];
    if (ow) *ow = cw;                 /* G10: 1/w para UV perspectiva */
    if (cw == 1.0f) { *ox = cx; *oy = cy; *oz = cz; return; }
    if (cw == 0.0f) cw = 1e-12f;
    *ox = cx / cw; *oy = cy / cw; *oz = cz / cw;
}

static void raster_list(gl_ctx* g, const gl_vertex* vs, int n) {
    float pm[16];
    mat_mul(pm, g->proj, g->mv);
    gl_vertex tmp[3];
    for (int i = 0; i + 2 < n; i += 3) {
        for (int k = 0; k < 3; k++) {
            tmp[k] = vs[i + k];
            float nx, ny, nz, nw;
            xform_ndc(pm, vs[i + k].x, vs[i + k].y, vs[i + k].z,
                      &nx, &ny, &nz, &nw);
            tmp[k].x = nx; tmp[k].y = ny; tmp[k].z = nz;
            /* G14: preserva UV do vértice (glTexCoord2f) + 1/w real
             * (antes o caminho imediato zerava u,v — resquício do G10) */
            tmp[k].iw = (nw != 0.0f) ? 1.0f / nw : 1.0f;
        }
        raster_tri(g, &tmp[0], &tmp[1], &tmp[2]);
    }
}

/* ---------------- GRUPO 9: vertex arrays (formato do 2º PE) --------------- */

void pr_gl_scalef(pr_gl_state* s, float x, float y, float z) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    float o[16] = { x,0,0,0, 0,y,0,0, 0,0,z,0, 0,0,0,1 };
    float* m = (g->matrix_mode == GL_PROJECTION) ? g->proj : g->mv;
    mat_mul(m, m, o);   /* M = M·S (pós-multiplicação, GL real) */
}

void pr_gl_enable_client_state(pr_gl_state* s, unsigned array) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (array == GL_VERTEX_ARRAY) { g->varr.enabled = 1; return; }
    if (array == GL_COLOR_ARRAY) { g->carr.enabled = 1; return; }
    if (array == GL_TEXTURE_COORD_ARRAY) { g->tarr.enabled = 1; return; }
    if (array == GL_NORMAL_ARRAY) { g->narr.enabled = 1; return; }
    set_error(g, GL_INVALID_ENUM);
}

/* G15: glDisableClientState — par canônico do enable (espelho real: limpa o
 * flag do arranjo; enum inválido -> GL_INVALID_ENUM; sem contexto -> no-op,
 * idêntico ao lado enable). Descoberto por execução de hello_gl8.exe. */
void pr_gl_disable_client_state(pr_gl_state* s, unsigned array) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (array == GL_VERTEX_ARRAY) { g->varr.enabled = 0; return; }
    if (array == GL_COLOR_ARRAY) { g->carr.enabled = 0; return; }
    if (array == GL_TEXTURE_COORD_ARRAY) { g->tarr.enabled = 0; return; }
    if (array == GL_NORMAL_ARRAY) { g->narr.enabled = 0; return; }
    set_error(g, GL_INVALID_ENUM);
}

static int set_ptr(gl_ctx* g, gl_vtxarr* va, int size, unsigned type,
                   int stride, uint64_t addr) {
    if (type != GL_FLOAT) { set_error(g, GL_INVALID_ENUM); return 0; }
    if (size != 3) { set_error(g, GL_INVALID_VALUE); return 0; }
    if (stride < 0 || (stride > 0 && stride < 12)) {
        set_error(g, GL_INVALID_VALUE);
        return 0;
    }
    va->size = size;
    va->type = (int)type;
    va->stride = stride;
    va->addr = addr;
    return 1;
}

void pr_gl_vertex_pointer(pr_gl_state* s, int size, unsigned type, int stride,
                          uint64_t addr) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    (void)set_ptr(g, &g->varr, size, type, stride, addr);
}

void pr_gl_color_pointer(pr_gl_state* s, int size, unsigned type, int stride,
                         uint64_t addr) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    (void)set_ptr(g, &g->carr, size, type, stride, addr);
}

void pr_gl_tex_coord_pointer(pr_gl_state* s, int size, unsigned type,
                             int stride, uint64_t addr) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (type != GL_FLOAT) { set_error(g, GL_INVALID_ENUM); return; }
    if (size != 2) { set_error(g, GL_INVALID_VALUE); return; }  /* subconjunto */
    if (stride < 0 || (stride > 0 && stride < 8)) {
        set_error(g, GL_INVALID_VALUE);
        return;
    }
    g->tarr.size = size;
    g->tarr.type = (int)type;
    g->tarr.stride = stride;
    g->tarr.addr = addr;
}

/* G16: índice 32-bit — recusa ANTES de qualquer leitura se base+vi*stride+len
 * transbordaria uint64 em qualquer arranjo ativo (o resolve validaria depois,
 * mas a soma aqui é explícita contra overflow). Só caminho GL_UNSIGNED_INT;
 * o caminho certificado GL_UNSIGNED_SHORT permanece intacto. */
static int g16_arr_ok(uint64_t base, uint64_t vi, uint64_t stride,
                      uint64_t len) {
    if (base > UINT64_MAX - len) return 0;
    if (stride == 0) return 1;
    uint64_t room = UINT64_MAX - base - len;
    return vi <= room / stride;   /* base + vi*stride + len cabe em uint64 */
}

static int g16_idx_addr_ok(gl_ctx* g, uint32_t vi) {
    uint64_t v = vi;
    if (!g16_arr_ok(g->varr.addr, v,
                    g->varr.stride ? (uint64_t)g->varr.stride : 12u, 12u))
        return 0;
    if (g->carr.enabled && g->carr.addr &&
        !g16_arr_ok(g->carr.addr, v,
                    g->carr.stride ? (uint64_t)g->carr.stride : 12u, 12u))
        return 0;
    if (g->narr.enabled && g->narr.addr &&
        !g16_arr_ok(g->narr.addr, v,
                    g->narr.stride ? (uint64_t)g->narr.stride : 12u, 12u))
        return 0;
    if (g->tarr.enabled && g->tarr.addr &&
        !g16_arr_ok(g->tarr.addr, v,
                    g->tarr.stride ? (uint64_t)g->tarr.stride : 8u, 8u))
        return 0;
    return 1;
}

void pr_gl_draw_elements(pr_gl_state* s, unsigned mode, int count,
                         unsigned type, uint64_t idx_addr,
                         pr_gl_ptr_fn resolve, void* ud) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (mode != GL_TRIANGLES) { set_error(g, GL_INVALID_ENUM); return; }
    if (type != GL_UNSIGNED_SHORT && type != GL_UNSIGNED_INT) {
        set_error(g, GL_INVALID_ENUM);
        return;
    }
    if (count < 0 || count > GL9_MAX_IDX) { set_error(g, GL_INVALID_VALUE); return; }
    if (!g->varr.enabled || !g->varr.addr || !resolve) {
        set_error(g, GL_INVALID_OPERATION);
        return;
    }
    if (count == 0) return;
    /* G16: cada índice ocupa 2 bytes (SHORT certificado) ou 4 bytes (INT);
     * o bloco inteiro é validado na memória do convidado antes de qualquer
     * leitura (ponteiro inválido/curto -> GL_INVALID_VALUE, como sempre). */
    size_t isz = (type == GL_UNSIGNED_INT) ? 4u : 2u;
    const unsigned char* idx = (const unsigned char*)resolve(
        ud, idx_addr, (size_t)count * isz);
    if (!idx) { set_error(g, GL_INVALID_VALUE); return; }
    int cstride = g->carr.enabled && g->carr.addr
                  ? (g->carr.stride ? g->carr.stride : 12) : 0;
    int vstride = g->varr.stride ? g->varr.stride : 12;

    (void)cstride; (void)vstride;
    gl_vertex tri[3];
    for (int i = 0; i + 2 < count; i += 3) {
        for (int k = 0; k < 3; k++) {
            uint32_t vi;
            if (type == GL_UNSIGNED_INT) {
                /* leitura little-endian de 32 bits, sem aliasing */
                const unsigned char* e = idx + (size_t)(i + k) * 4u;
                vi = (uint32_t)e[0] | ((uint32_t)e[1] << 8)
                   | ((uint32_t)e[2] << 16) | ((uint32_t)e[3] << 24);
                if (!g16_idx_addr_ok(g, vi)) {
                    set_error(g, GL_INVALID_VALUE);
                    return;
                }
            } else {
                vi = (uint32_t)((const uint16_t*)idx)[i + k];
            }
            if (!g10_fetch_vertex(g, vi, &tri[k], resolve, ud)) {
                set_error(g, GL_INVALID_VALUE);
                return;
            }
        }
        float pm[16];
        mat_mul(pm, g->proj, g->mv);
        gl_vertex t2[3] = { tri[0], tri[1], tri[2] };
        for (int k = 0; k < 3; k++) {
            float nx, ny, nz, nw;
            xform_ndc(pm, tri[k].x, tri[k].y, tri[k].z, &nx, &ny, &nz, &nw);
            t2[k].x = nx; t2[k].y = ny; t2[k].z = nz;
            t2[k].iw = (nw != 0.0f) ? (1.0f / nw) : 0.0f;
        }
        raster_tri(g, &t2[0], &t2[1], &t2[2]);
    }
}

/* ---------------- GRUPO 10: glDrawArrays + matrix stack + texturas ------- */

/* fetch de vértice por índice (posição + cor + UV) — compartilhado por
 * glDrawElements e glDrawArrays (mesma aritmética do caminho G9) */
static int g10_fetch_vertex(gl_ctx* g, uint32_t vi, gl_vertex* out,
                            pr_gl_ptr_fn resolve, void* ud) {
    int vstride = g->varr.stride ? g->varr.stride : 12;
    int cstride = g->carr.enabled && g->carr.addr
                  ? (g->carr.stride ? g->carr.stride : 12) : 0;
    const float* vp = (const float*)resolve(
        ud, g->varr.addr + (uint64_t)vi * (uint64_t)vstride, 12);
    if (!vp) return 0;
    out->x = vp[0]; out->y = vp[1]; out->z = vp[2];
    if (cstride) {
        const float* cp = (const float*)resolve(
            ud, g->carr.addr + (uint64_t)vi * (uint64_t)cstride, 12);
        if (!cp) return 0;
        out->col[0] = cp[0]; out->col[1] = cp[1]; out->col[2] = cp[2];
    } else {
        out->col[0] = g->cur_col[0];
        out->col[1] = g->cur_col[1];
        out->col[2] = g->cur_col[2];
    }
    out->u = 0.0f; out->v = 0.0f; out->iw = 1.0f;
    out->a = g->cur_a;
    {
        /* G11: normal por array (ou corrente) + iluminação por vértice */
        float nrm[3] = { g->cur_n[0], g->cur_n[1], g->cur_n[2] };
        if (g->narr.enabled && g->narr.addr) {
            int nstride = g->narr.stride ? g->narr.stride : 12;
            const float* np = (const float*)resolve(
                ud, g->narr.addr + (uint64_t)vi * (uint64_t)nstride, 12);
            if (!np) return 0;
            nrm[0] = np[0]; nrm[1] = np[1]; nrm[2] = np[2];
        }
        if (g->lighting) {
            g11_light_vertex(g, out->x, out->y, out->z, nrm, out->col, &out->a);
        }
    }
    if (g->tarr.enabled && g->tarr.addr) {
        int tstride = g->tarr.stride ? g->tarr.stride : 8;
        const float* tp = (const float*)resolve(
            ud, g->tarr.addr + (uint64_t)vi * (uint64_t)tstride, 8);
        if (!tp) return 0;
        out->u = tp[0]; out->v = tp[1];
    }
    return 1;
}

void pr_gl_draw_arrays(pr_gl_state* s, unsigned mode, int first, int count,
                       pr_gl_ptr_fn resolve, void* ud) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (mode != GL_TRIANGLES) { set_error(g, GL_INVALID_ENUM); return; }
    if (first < 0 || count < 0 || count > GL9_MAX_IDX) {
        set_error(g, GL_INVALID_VALUE);
        return;
    }
    if (!g->varr.enabled || !g->varr.addr || !resolve) {
        set_error(g, GL_INVALID_OPERATION);
        return;
    }
    if (count < 3) return;
    gl_vertex tri[3];
    for (int i = 0; i + 2 < count; i += 3) {
        for (int k = 0; k < 3; k++) {
            if (!g10_fetch_vertex(g, (uint32_t)(first + i + k), &tri[k],
                                  resolve, ud)) {
                set_error(g, GL_INVALID_VALUE);
                return;
            }
        }
        float pm[16];
        mat_mul(pm, g->proj, g->mv);
        gl_vertex t2[3] = { tri[0], tri[1], tri[2] };
        for (int k = 0; k < 3; k++) {
            float nx, ny, nz, nw;
            xform_ndc(pm, tri[k].x, tri[k].y, tri[k].z, &nx, &ny, &nz, &nw);
            t2[k].x = nx; t2[k].y = ny; t2[k].z = nz;
            t2[k].iw = (nw != 0.0f) ? (1.0f / nw) : 0.0f;
        }
        raster_tri(g, &t2[0], &t2[1], &t2[2]);
    }
}

void pr_gl_push_matrix(pr_gl_state* s) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (g->matrix_mode == GL_PROJECTION) {
        if (g->pj_sp >= 4) { set_error(g, GL_STACK_OVERFLOW); return; }
        memcpy(g->pj_stack[g->pj_sp++], g->proj, sizeof(g->proj));
    } else {
        if (g->mv_sp >= 32) { set_error(g, GL_STACK_OVERFLOW); return; }
        memcpy(g->mv_stack[g->mv_sp++], g->mv, sizeof(g->mv));
    }
}

void pr_gl_pop_matrix(pr_gl_state* s) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (g->matrix_mode == GL_PROJECTION) {
        if (g->pj_sp <= 0) { set_error(g, GL_STACK_UNDERFLOW); return; }
        memcpy(g->proj, g->pj_stack[--g->pj_sp], sizeof(g->proj));
    } else {
        if (g->mv_sp <= 0) { set_error(g, GL_STACK_UNDERFLOW); return; }
        memcpy(g->mv, g->mv_stack[--g->mv_sp], sizeof(g->mv));
    }
}

void pr_gl_frustum(pr_gl_state* s, double l, double r, double b, double t,
                   double n, double f) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (n <= 0.0 || f <= 0.0 || l == r || b == t) {
        set_error(g, GL_INVALID_VALUE);
        return;
    }
    float fr[16] = {
        (float)(2.0 * n / (r - l)), 0, 0, 0,
        0, (float)(2.0 * n / (t - b)), 0, 0,
        (float)((r + l) / (r - l)), (float)((t + b) / (t - b)),
        (float)(-(f + n) / (f - n)), -1.0f,
        0, 0, (float)(-2.0 * f * n / (f - n)), 0
    };
    float* m = (g->matrix_mode == GL_PROJECTION) ? g->proj : g->mv;
    mat_mul(m, m, fr);
}

void pr_gl_gen_textures(pr_gl_state* s, int n, uint32_t* names) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g || !names) return;
    if (n <= 0) { set_error(g, GL_INVALID_VALUE); return; }
    for (int i = 0; i < n; i++) {
        uint32_t slot = 0;
        for (uint32_t t = 1; t < GL10_MAX_TEX; t++) {
            if (!g->tex[t].used) {
                memset(&g->tex[t], 0, sizeof(g->tex[t]));
                g->tex[t].used = 1;
                g->tex[t].min_f = (int)GL_NEAREST;
                g->tex[t].mag_f = (int)GL_NEAREST;
                slot = t;
                break;
            }
        }
        if (!slot) { set_error(g, GL_INVALID_OPERATION); return; }
        names[i] = slot;
    }
}

/* G17: glDeleteTextures — par real de glGenTextures (ciclo de vida de
 * objetos de textura), descoberto por execução de hello_gl10.exe.
 * Espelha o gen: n <= 0 -> GL_INVALID_VALUE; sem contexto ou nomes nulos ->
 * silencioso. GL 1.1: nomes 0, fora da faixa ou ainda nao criados sao
 * ignorados; excluir o objeto vinculado faz a vinculacao voltar a 0; o slot
 * volta a ficar livre (reusavel pelo proximo glGenTextures). */
void pr_gl_delete_textures(pr_gl_state* s, int n, const uint32_t* names) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g || !names) return;
    if (n <= 0) { set_error(g, GL_INVALID_VALUE); return; }
    for (int i = 0; i < n; i++) {
        uint32_t name = names[i];
        if (name == 0 || name >= GL10_MAX_TEX) continue;
        if (!g->tex[name].used) continue;
        free(g->tex[name].texels);
        memset(&g->tex[name], 0, sizeof(g->tex[name]));
        if (g->tex_bound == name) g->tex_bound = 0;
    }
}

void pr_gl_bind_texture(pr_gl_state* s, unsigned target, uint32_t name) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (target != GL_TEXTURE_2D) { set_error(g, GL_INVALID_ENUM); return; }
    if (name >= GL10_MAX_TEX) { set_error(g, GL_INVALID_VALUE); return; }
    if (!g->tex[name].used) {
        /* GL 1.1: binding cria o objeto implicitamente */
        memset(&g->tex[name], 0, sizeof(g->tex[name]));
        g->tex[name].used = 1;
        g->tex[name].min_f = (int)GL_NEAREST;
        g->tex[name].mag_f = (int)GL_NEAREST;
    }
    g->tex_bound = name;
}

void pr_gl_tex_parameteri(pr_gl_state* s, unsigned target, unsigned pname,
                          int param) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (target != GL_TEXTURE_2D) { set_error(g, GL_INVALID_ENUM); return; }
    gl_tex_obj* o = &g->tex[g->tex_bound];
    if (!o->used) { set_error(g, GL_INVALID_OPERATION); return; }
    if (pname == GL_TEXTURE_MIN_FILTER || pname == GL_TEXTURE_MAG_FILTER) {
        if (param != (int)GL_NEAREST) {
            /* subconjunto: apenas GL_NEAREST (sem LINEAR/mipmaps) */
            pr_win32_gl_log(s->owner,
                            "[GL] filtro de textura nao suportado "
                            "(subconjunto: apenas GL_NEAREST)");
            set_error(g, GL_INVALID_ENUM);
            return;
        }
        if (pname == GL_TEXTURE_MIN_FILTER) o->min_f = param;
        else o->mag_f = param;
        return;
    }
    if (pname == GL_TEXTURE_WRAP_S || pname == GL_TEXTURE_WRAP_T) {
        if (param != (int)GL_REPEAT) {
            set_error(g, GL_INVALID_ENUM);   /* subconjunto: só GL_REPEAT */
            return;
        }
        return;
    }
    set_error(g, GL_INVALID_ENUM);
}

void pr_gl_tex_image2d(pr_gl_state* s, unsigned target, int level,
                       int internalformat, int width, int height, int border,
                       unsigned format, unsigned type, uint64_t pixels_addr,
                       pr_gl_ptr_fn resolve, void* ud) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (target != GL_TEXTURE_2D) { set_error(g, GL_INVALID_ENUM); return; }
    if (level != 0 || border != 0) { set_error(g, GL_INVALID_VALUE); return; }
    if (width <= 0 || height <= 0 || width > 1024 || height > 1024) {
        set_error(g, GL_INVALID_VALUE);
        return;
    }
    if (format != GL_RGB || type != GL_UNSIGNED_BYTE) {
        set_error(g, GL_INVALID_ENUM);   /* subconjunto: RGB/UNSIGNED_BYTE */
        return;
    }
    if (internalformat != 3 && internalformat != (int)GL_RGB) {
        set_error(g, GL_INVALID_VALUE);
        return;
    }
    gl_tex_obj* o = &g->tex[g->tex_bound];
    if (!o->used) { set_error(g, GL_INVALID_OPERATION); return; }
    free(o->texels);
    o->texels = (unsigned char*)malloc((size_t)width * (size_t)height * 3);
    if (!o->texels) { set_error(g, GL_OUT_OF_MEMORY); return; }
    o->w = width;
    o->h = height;
    o->defined = 0;
    if (pixels_addr) {
        const void* src = resolve ? resolve(ud, pixels_addr,
                                            (size_t)width * (size_t)height * 3u) : NULL;
        if (!src) { set_error(g, GL_INVALID_VALUE); return; }
        memcpy(o->texels, src, (size_t)width * (size_t)height * 3u);
        o->defined = 1;
    }
    /* pixels_addr == 0 → aloca indefinida (textura incompleta, GL real) */
}

/* ---------------- GRUPO 11: iluminação + blending (APIs novas) ------------ */

void pr_gl_normal_pointer(pr_gl_state* s, unsigned type, int stride,
                          uint64_t addr) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (type != GL_FLOAT) { set_error(g, GL_INVALID_ENUM); return; }
    if (stride < 0 || (stride > 0 && stride < 12)) {
        set_error(g, GL_INVALID_VALUE);
        return;
    }
    g->narr.size = 3;
    g->narr.type = (int)type;
    g->narr.stride = stride;
    g->narr.addr = addr;
}

void pr_gl_light_fv(pr_gl_state* s, unsigned light, unsigned pname,
                    const float* v) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g || !v) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (light < GL_LIGHT0 || light >= GL_LIGHT0 + 8) {
        set_error(g, GL_INVALID_ENUM);
        return;
    }
    gl_light* L = &g->lights[light - GL_LIGHT0];
    if (pname == GL_AMBIENT) memcpy(L->amb, v, 4 * sizeof(float));
    else if (pname == GL_DIFFUSE) memcpy(L->diff, v, 4 * sizeof(float));
    else if (pname == GL_SPECULAR) memcpy(L->spec, v, 4 * sizeof(float));
    else if (pname == GL_POSITION) {
        /* GL real: GL_POSITION é transformado pelo MODELVIEW corrente */
        const float* m = g->mv;
        float x = v[0], y = v[1], z = v[2], w = v[3];
        L->pos[0] = m[0] * x + m[4] * y + m[8] * z + m[12] * w;
        L->pos[1] = m[1] * x + m[5] * y + m[9] * z + m[13] * w;
        L->pos[2] = m[2] * x + m[6] * y + m[10] * z + m[14] * w;
        L->pos[3] = m[3] * x + m[7] * y + m[11] * z + m[15] * w;
    } else if (pname == GL_CONSTANT_ATTENUATION) L->att_c = v[0];
    else if (pname == GL_LINEAR_ATTENUATION) L->att_l = v[0];
    else if (pname == GL_QUADRATIC_ATTENUATION) L->att_q = v[0];
    else {
        /* subset: sem spot (GL_SPOT_*) */
        pr_win32_gl_log(s->owner,
                        "[GL] glLightfv pname fora do subconjunto (spot)");
        set_error(g, GL_INVALID_ENUM);
    }
}

void pr_gl_material_fv(pr_gl_state* s, unsigned face, unsigned pname,
                       const float* v) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g || !v) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    if (face == GL_BACK) {
        pr_win32_gl_log(s->owner,
                        "[GL] glMaterialfv(GL_BACK) fora do subconjunto");
        set_error(g, GL_INVALID_ENUM);
        return;
    }
    if (face != GL_FRONT && face != GL_FRONT_AND_BACK) {
        set_error(g, GL_INVALID_ENUM);
        return;
    }
    gl_material* m = &g->mat_front;
    if (pname == GL_AMBIENT) memcpy(m->amb, v, 4 * sizeof(float));
    else if (pname == GL_DIFFUSE) memcpy(m->diff, v, 4 * sizeof(float));
    else if (pname == GL_SPECULAR) memcpy(m->spec, v, 4 * sizeof(float));
    else if (pname == GL_EMISSION) memcpy(m->emiss, v, 4 * sizeof(float));
    else if (pname == GL_AMBIENT_AND_DIFFUSE) {
        memcpy(m->amb, v, 4 * sizeof(float));
        memcpy(m->diff, v, 4 * sizeof(float));
    } else if (pname == GL_SHININESS) {
        if (v[0] < 0.0f || v[0] > 128.0f) {
            set_error(g, GL_INVALID_VALUE);   /* faixa real do GL */
            return;
        }
        m->shine = v[0];
    } else set_error(g, GL_INVALID_ENUM);
}

void pr_gl_blend_func(pr_gl_state* s, unsigned sfactor, unsigned dfactor) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (g->in_begin) { set_error(g, GL_INVALID_OPERATION); return; }
    int s_ok = sfactor == GL_ZERO || sfactor == GL_ONE ||
               sfactor == GL_SRC_ALPHA || sfactor == GL_ONE_MINUS_SRC_ALPHA;
    int d_ok = dfactor == GL_ZERO || dfactor == GL_ONE ||
               dfactor == GL_SRC_ALPHA || dfactor == GL_ONE_MINUS_SRC_ALPHA;
    if (!s_ok || !d_ok) {
        pr_win32_gl_log(s->owner,
                        "[GL] glBlendFunc fora do subconjunto "
                        "(ZERO|ONE|SRC_ALPHA|ONE_MINUS_SRC_ALPHA)");
        set_error(g, GL_INVALID_ENUM);
        return;
    }
    g->blend_s = sfactor;
    g->blend_d = dfactor;
}

/* ------------------------------ glReadPixels ----------------------------- */

void pr_gl_read_pixels(pr_gl_state* s, int x, int y, int w, int h,
                       unsigned fmt, unsigned type, void* out) {
    gl_ctx* g = s ? s->current : NULL;
    if (!g) return;
    if (!out || w < 0 || h < 0) { set_error(g, GL_INVALID_VALUE); return; }
    if (fmt != GL_RGBA || type != GL_UNSIGNED_BYTE) {
        set_error(g, GL_INVALID_ENUM);   /* subconjunto: GL_RGBA+UNSIGNED_BYTE */
        return;
    }
    if (x < 0 || y < 0 || x + w > (int)g->w || y + h > (int)g->h) {
        set_error(g, GL_INVALID_VALUE);
        return;
    }
    uint8_t* o = (uint8_t*)out;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            uint32_t c = g->color[(uint32_t)(y + row) * g->w + (uint32_t)(x + col)];
            uint8_t* p = o + ((size_t)row * (size_t)w + (size_t)col) * 4;
            p[0] = (uint8_t)((c >> 16) & 0xFF);   /* R */
            p[1] = (uint8_t)((c >> 8) & 0xFF);    /* G */
            p[2] = (uint8_t)(c & 0xFF);           /* B */
            p[3] = 255;
        }
    }
}
