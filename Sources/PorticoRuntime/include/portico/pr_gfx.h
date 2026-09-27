/* Portico Runtime — stream de comandos gráficos (ABI gráfico comum).
 * Frontends de tradução (OpenGL/D3D) e o payload interpretado emitem estes comandos;
 * o GraphicsBackend (Metal no iOS) consome e executa. */
#ifndef PORTICO_PR_GFX_H
#define PORTICO_PR_GFX_H

#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pr_color { float r, g, b, a; } pr_color;
typedef struct pr_rect  { float x, y, w, h; } pr_rect;

typedef enum pr_gfx_op {
    PR_GFX_NOP = 0,
    PR_GFX_CLEAR      = 1, /* limpa o framebuffer com cor */
    PR_GFX_SET_VIEWPORT = 2,
    PR_GFX_SET_SCISSOR  = 3,
    PR_GFX_DRAW_TRIANGLES = 4, /* usa vértices acumulados [first, first+count) */
    PR_GFX_PRESENT    = 5, /* finaliza o frame */
    PR_GFX_SET_FILTER = 6
} pr_gfx_op;

typedef enum pr_gfx_filter { PR_GFX_FILTER_LINEAR = 0, PR_GFX_FILTER_NEAREST = 1 } pr_gfx_filter;

typedef struct pr_gfx_cmd {
    uint32_t op; /* pr_gfx_op */
    union {
        struct { pr_color color; uint32_t flags; } clear;
        struct { pr_rect rect; } rect;                 /* viewport / scissor */
        struct { uint32_t vertex_count; uint32_t first_vertex; } draw;
        struct { uint32_t width, height; } present;
        struct { uint32_t filter; } filter;
    } a;
} pr_gfx_cmd;

/* Vértice simples: posição 2D + cor RGB (5 floats). Totalmente alinhado a pipeline Metal futura. */
typedef struct pr_vertex { float x, y, r, g, b; } pr_vertex;

typedef struct pr_gfx_stream pr_gfx_stream;

pr_gfx_stream* pr_gfx_stream_create(size_t max_cmds, size_t max_vertices);
void     pr_gfx_stream_destroy(pr_gfx_stream* s);
void     pr_gfx_stream_reset(pr_gfx_stream* s);
pr_status pr_gfx_push(pr_gfx_stream* s, const pr_gfx_cmd* cmd);
pr_status pr_gfx_push_vertex(pr_gfx_stream* s, const pr_vertex* v);
pr_status pr_gfx_push_vertices(pr_gfx_stream* s, const pr_vertex* v, size_t count);
/* Consome até max comandos (remove do stream). Retorna nº consumido. */
size_t   pr_gfx_pull(pr_gfx_stream* s, pr_gfx_cmd* out, size_t max);
/* Visão somente leitura dos vértices acumulados (inválida após reset). */
const pr_vertex* pr_gfx_vertices(const pr_gfx_stream* s, size_t* out_count);
size_t   pr_gfx_pending(const pr_gfx_stream* s);

/* ---- Superfície/textura do frame (apresentação por pixels) ---- */
/* Define a textura do frame atual (cópia interna; buffer reaproveitado).
 * Sobrevive a pull; limpa no reset. PR_ERR_RANGE acima de 8192². */
pr_status pr_gfx_set_surface(pr_gfx_stream* s, const uint32_t* pixels,
                             uint32_t w, uint32_t h);
/* Leitura após o pull; NULL se o frame não tem superfície. */
const uint32_t* pr_gfx_surface(const pr_gfx_stream* s,
                               uint32_t* out_w, uint32_t* out_h);

/* Helpers de alto nível. */
pr_status pr_gfx_cmd_clear(pr_gfx_stream* s, pr_color c);
pr_status pr_gfx_cmd_viewport(pr_gfx_stream* s, pr_rect r);
pr_status pr_gfx_cmd_draw(pr_gfx_stream* s, uint32_t vertex_count, uint32_t first_vertex);
pr_status pr_gfx_cmd_present(pr_gfx_stream* s, uint32_t w, uint32_t h);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_GFX_H */
