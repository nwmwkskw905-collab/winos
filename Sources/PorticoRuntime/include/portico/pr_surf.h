/* Portico Runtime — superfície de janela virtual + sincronização.
 *
 * Camada mínima REAL da tradução gráfica estilo Windows (GDI/surface):
 *   criação de janela/surface virtual → buffers de pixels (textura futura) →
 *   comandos básicos (clear/fill) → apresentação no stream comum → sincronização.
 * O consumidor (Metal) recebe os pixels como textura do frame (pr_gfx_surface).
 */
#ifndef PORTICO_PR_SURF_H
#define PORTICO_PR_SURF_H

#include "pr_types.h"
#include "pr_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pr_surf_format {
    PR_SURF_XRGB8888 = 0  /* 32 bpp little-endian, 0x00RRGGBB */
} pr_surf_format;

typedef struct pr_surf pr_surf;

pr_surf*  pr_surf_create(uint32_t w, uint32_t h, pr_surf_format fmt);
void      pr_surf_destroy(pr_surf* s);
uint32_t  pr_surf_width(const pr_surf* s);
uint32_t  pr_surf_height(const pr_surf* s);
pr_surf_format pr_surf_format_of(const pr_surf* s);
/* Buffer de pixels real (largura*altura*4 bytes). */
uint32_t* pr_surf_pixels(pr_surf* s);
size_t    pr_surf_pitch(const pr_surf* s); /* bytes por linha */

void pr_surf_clear(pr_surf* s, pr_color c);
void pr_surf_fill_rect(pr_surf* s, pr_rect r, pr_color c);

/* Apresenta a surface: copia os pixels para o stream (buffer de textura do
 * frame) e emite PRESENT. PR_ERR_STATE se stream cheio. */
pr_status pr_surf_present(pr_surf* s, pr_gfx_stream* stream,
                          uint32_t target_w, uint32_t target_h);

/* ---- Sincronização por fence (contador monotônico) ---- */
typedef struct pr_sync_fence pr_sync_fence;

pr_sync_fence* pr_sync_fence_create(void);
void     pr_sync_fence_destroy(pr_sync_fence* f);
void     pr_sync_fence_signal(pr_sync_fence* f);   /* value++ (fim de frame) */
uint64_t pr_sync_fence_value(const pr_sync_fence* f);
int      pr_sync_fence_reached(const pr_sync_fence* f, uint64_t target);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_SURF_H */
