/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_gfx.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct pr_gfx_stream {
    pr_gfx_cmd* cmds;
    size_t cmd_cap;
    size_t cmd_head;   /* escrita */
    size_t cmd_tail;   /* leitura */
    pr_vertex* verts;
    size_t vert_cap;
    size_t vert_count;
    uint32_t* surface;      /* textura do frame (buffer reaproveitado) */
    size_t surface_cap;     /* em pixels */
    uint32_t surf_w, surf_h;
    pthread_mutex_t mu;
};

pr_gfx_stream* pr_gfx_stream_create(size_t max_cmds, size_t max_vertices) {
    if (max_cmds == 0) max_cmds = 1024;
    if (max_vertices == 0) max_vertices = 65536;
    pr_gfx_stream* s = (pr_gfx_stream*)calloc(1, sizeof(pr_gfx_stream));
    if (!s) return NULL;
    s->cmds = (pr_gfx_cmd*)calloc(max_cmds, sizeof(pr_gfx_cmd));
    s->verts = (pr_vertex*)calloc(max_vertices, sizeof(pr_vertex));
    if (!s->cmds || !s->verts) {
        pr_gfx_stream_destroy(s);
        return NULL;
    }
    s->cmd_cap = max_cmds;
    s->vert_cap = max_vertices;
    pthread_mutex_init(&s->mu, NULL);
    return s;
}

void pr_gfx_stream_destroy(pr_gfx_stream* s) {
    if (!s) return;
    pthread_mutex_destroy(&s->mu);
    free(s->cmds);
    free(s->verts);
    free(s->surface);
    free(s);
}

void pr_gfx_stream_reset(pr_gfx_stream* s) {
    if (!s) return;
    pthread_mutex_lock(&s->mu);
    s->cmd_head = s->cmd_tail = 0;
    s->vert_count = 0;
    s->surf_w = s->surf_h = 0; /* buffer mantido (sem alocação por frame) */
    pthread_mutex_unlock(&s->mu);
}

pr_status pr_gfx_push(pr_gfx_stream* s, const pr_gfx_cmd* cmd) {
    if (!s || !cmd) return PR_ERR_INVALID;
    pthread_mutex_lock(&s->mu);
    size_t pending = s->cmd_head - s->cmd_tail;
    if (pending >= s->cmd_cap) {
        pthread_mutex_unlock(&s->mu);
        return PR_ERR_RANGE;
    }
    s->cmds[s->cmd_head % s->cmd_cap] = *cmd;
    s->cmd_head++;
    pthread_mutex_unlock(&s->mu);
    return PR_OK;
}

pr_status pr_gfx_push_vertex(pr_gfx_stream* s, const pr_vertex* v) {
    if (!s || !v) return PR_ERR_INVALID;
    pthread_mutex_lock(&s->mu);
    pr_status st = PR_OK;
    if (s->vert_count >= s->vert_cap) {
        st = PR_ERR_RANGE;
    } else {
        s->verts[s->vert_count++] = *v;
    }
    pthread_mutex_unlock(&s->mu);
    return st;
}

pr_status pr_gfx_push_vertices(pr_gfx_stream* s, const pr_vertex* v, size_t count) {
    if (!s || (!v && count)) return PR_ERR_INVALID;
    pthread_mutex_lock(&s->mu);
    pr_status st = PR_OK;
    if (s->vert_count + count > s->vert_cap) {
        st = PR_ERR_RANGE;
    } else {
        memcpy(&s->verts[s->vert_count], v, count * sizeof(pr_vertex));
        s->vert_count += count;
    }
    pthread_mutex_unlock(&s->mu);
    return st;
}

size_t pr_gfx_pull(pr_gfx_stream* s, pr_gfx_cmd* out, size_t max) {
    if (!s || !out || max == 0) return 0;
    pthread_mutex_lock(&s->mu);
    size_t n = 0;
    while (s->cmd_tail < s->cmd_head && n < max) {
        out[n++] = s->cmds[s->cmd_tail % s->cmd_cap];
        s->cmd_tail++;
    }
    pthread_mutex_unlock(&s->mu);
    return n;
}

const pr_vertex* pr_gfx_vertices(const pr_gfx_stream* s, size_t* out_count) {
    if (!s) { if (out_count) *out_count = 0; return NULL; }
    if (out_count) *out_count = s->vert_count;
    return s->verts;
}

size_t pr_gfx_pending(const pr_gfx_stream* s) {
    if (!s) return 0;
    return s->cmd_head - s->cmd_tail;
}

pr_status pr_gfx_cmd_clear(pr_gfx_stream* s, pr_color c) {
    pr_gfx_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = PR_GFX_CLEAR;
    cmd.a.clear.color = c;
    return pr_gfx_push(s, &cmd);
}

pr_status pr_gfx_cmd_viewport(pr_gfx_stream* s, pr_rect r) {
    pr_gfx_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = PR_GFX_SET_VIEWPORT;
    cmd.a.rect.rect = r;
    return pr_gfx_push(s, &cmd);
}

pr_status pr_gfx_cmd_draw(pr_gfx_stream* s, uint32_t vertex_count, uint32_t first_vertex) {
    pr_gfx_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = PR_GFX_DRAW_TRIANGLES;
    cmd.a.draw.vertex_count = vertex_count;
    cmd.a.draw.first_vertex = first_vertex;
    return pr_gfx_push(s, &cmd);
}

pr_status pr_gfx_cmd_present(pr_gfx_stream* s, uint32_t w, uint32_t h) {
    pr_gfx_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.op = PR_GFX_PRESENT;
    cmd.a.present.width = w;
    cmd.a.present.height = h;
    return pr_gfx_push(s, &cmd);
}

/* ---- Superfície/textura do frame ---- */

pr_status pr_gfx_set_surface(pr_gfx_stream* s, const uint32_t* pixels,
                             uint32_t w, uint32_t h) {
    if (!s || !pixels || w == 0 || h == 0) return PR_ERR_INVALID;
    if (w > 8192 || h > 8192) return PR_ERR_RANGE;
    size_t need = (size_t)w * h;
    pthread_mutex_lock(&s->mu);
    if (need > s->surface_cap) {
        /* cresce com margem (aloca só quando a resolução muda) */
        size_t cap = s->surface_cap ? s->surface_cap * 2 : need;
        if (cap < need) cap = need;
        uint32_t* nb = (uint32_t*)realloc(s->surface, cap * sizeof(uint32_t));
        if (!nb) {
            pthread_mutex_unlock(&s->mu);
            return PR_ERR_NOMEM;
        }
        s->surface = nb;
        s->surface_cap = cap;
    }
    memcpy(s->surface, pixels, need * sizeof(uint32_t));
    s->surf_w = w;
    s->surf_h = h;
    pthread_mutex_unlock(&s->mu);
    return PR_OK;
}

const uint32_t* pr_gfx_surface(const pr_gfx_stream* s,
                               uint32_t* out_w, uint32_t* out_h) {
    if (!s || s->surf_w == 0) return NULL;
    if (out_w) *out_w = s->surf_w;
    if (out_h) *out_h = s->surf_h;
    return s->surface;
}
