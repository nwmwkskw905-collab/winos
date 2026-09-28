/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_surf.h"

#include <stdlib.h>
#include <string.h>
#include <pthread.h>

struct pr_surf {
    uint32_t w, h;
    pr_surf_format fmt;
    uint32_t* pixels;   /* w*h */
};

static uint32_t color_to_xrgb(pr_color c) {
    /* pr_color: floats 0..1 → 0x00RRGGBB */
    float r = c.r < 0 ? 0 : (c.r > 1 ? 1 : c.r);
    float g = c.g < 0 ? 0 : (c.g > 1 ? 1 : c.g);
    float b = c.b < 0 ? 0 : (c.b > 1 ? 1 : c.b);
    uint32_t R = (uint32_t)(r * 255.0f + 0.5f);
    uint32_t G = (uint32_t)(g * 255.0f + 0.5f);
    uint32_t B = (uint32_t)(b * 255.0f + 0.5f);
    return (R << 16) | (G << 8) | B;
}

pr_surf* pr_surf_create(uint32_t w, uint32_t h, pr_surf_format fmt) {
    if (w == 0 || h == 0 || w > 8192 || h > 8192) return NULL;
    pr_surf* s = (pr_surf*)calloc(1, sizeof(pr_surf));
    if (!s) return NULL;
    s->pixels = (uint32_t*)calloc((size_t)w * h, sizeof(uint32_t));
    if (!s->pixels) { free(s); return NULL; }
    s->w = w;
    s->h = h;
    s->fmt = fmt;
    return s;
}

void pr_surf_destroy(pr_surf* s) {
    if (!s) return;
    free(s->pixels);
    free(s);
}

uint32_t pr_surf_width(const pr_surf* s) { return s ? s->w : 0; }
uint32_t pr_surf_height(const pr_surf* s) { return s ? s->h : 0; }
pr_surf_format pr_surf_format_of(const pr_surf* s) { return s ? s->fmt : PR_SURF_XRGB8888; }
uint32_t* pr_surf_pixels(pr_surf* s) { return s ? s->pixels : NULL; }
size_t pr_surf_pitch(const pr_surf* s) { return s ? (size_t)s->w * 4 : 0; }

void pr_surf_clear(pr_surf* s, pr_color c) {
    if (!s) return;
    uint32_t v = color_to_xrgb(c);
    size_t n = (size_t)s->w * s->h;
    for (size_t i = 0; i < n; i++) s->pixels[i] = v;
}

void pr_surf_fill_rect(pr_surf* s, pr_rect r, pr_color c) {
    if (!s) return;
    int32_t x0 = (int32_t)r.x, y0 = (int32_t)r.y;
    int32_t x1 = (int32_t)(r.x + r.w), y1 = (int32_t)(r.y + r.h);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int32_t)s->w) x1 = (int32_t)s->w;
    if (y1 > (int32_t)s->h) y1 = (int32_t)s->h;
    uint32_t v = color_to_xrgb(c);
    for (int32_t y = y0; y < y1; y++) {
        uint32_t* row = s->pixels + (size_t)y * s->w;
        for (int32_t x = x0; x < x1; x++) row[x] = v;
    }
}

pr_status pr_surf_present(pr_surf* s, pr_gfx_stream* stream,
                          uint32_t target_w, uint32_t target_h) {
    if (!s || !stream) return PR_ERR_INVALID;
    pr_status st = pr_gfx_set_surface(stream, s->pixels, s->w, s->h);
    if (st != PR_OK) return st;
    return pr_gfx_cmd_present(stream, target_w ? target_w : s->w,
                              target_h ? target_h : s->h);
}

/* ---- Fence ---- */

struct pr_sync_fence {
    uint64_t value;
    pthread_mutex_t mu;
};

pr_sync_fence* pr_sync_fence_create(void) {
    pr_sync_fence* f = (pr_sync_fence*)calloc(1, sizeof(pr_sync_fence));
    if (!f) return NULL;
    pthread_mutex_init(&f->mu, NULL);
    return f;
}

void pr_sync_fence_destroy(pr_sync_fence* f) {
    if (!f) return;
    pthread_mutex_destroy(&f->mu);
    free(f);
}

void pr_sync_fence_signal(pr_sync_fence* f) {
    if (!f) return;
    pthread_mutex_lock(&f->mu);
    f->value++;
    pthread_mutex_unlock(&f->mu);
}

uint64_t pr_sync_fence_value(const pr_sync_fence* f) {
    return f ? f->value : 0;
}

int pr_sync_fence_reached(const pr_sync_fence* f, uint64_t target) {
    return f ? (f->value >= target) : 0;
}
