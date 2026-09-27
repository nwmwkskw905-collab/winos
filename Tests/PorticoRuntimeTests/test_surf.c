/* Testes da superfície virtual + sincronização (caminho gráfico por pixels). */
#include "pt_util.h"

#include "portico/pr_surf.h"
#include "portico/pr_gfx.h"

void test_surf(void) {
    pr_surf* s = pr_surf_create(4, 4, PR_SURF_XRGB8888);
    CHECK(s != NULL);
    CHECK_EQ_U32(pr_surf_width(s), 4);
    CHECK_EQ_U32(pr_surf_height(s), 4);
    CHECK_EQ_U32(pr_surf_pitch(s), 16);
    CHECK(pr_surf_pixels(s) != NULL);

    /* parâmetros inválidos */
    CHECK(pr_surf_create(0, 4, PR_SURF_XRGB8888) == NULL);
    CHECK(pr_surf_create(9000, 4, PR_SURF_XRGB8888) == NULL);

    /* clear */
    pr_color red = {1, 0, 0, 1};
    pr_surf_clear(s, red);
    uint32_t* px = pr_surf_pixels(s);
    CHECK_EQ_U32(px[0], 0xFF0000u);
    CHECK_EQ_U32(px[15], 0xFF0000u);

    /* fill_rect (recorta nos limites) */
    pr_color blue = {0, 0, 1, 1};
    pr_rect r = {1, 1, 2, 2};
    pr_surf_fill_rect(s, r, blue);
    CHECK_EQ_U32(px[0], 0xFF0000u);   /* fora */
    CHECK_EQ_U32(px[5], 0x0000FFu);   /* (1,1) */
    CHECK_EQ_U32(px[6], 0x0000FFu);   /* (2,1) */
    CHECK_EQ_U32(px[9], 0x0000FFu);   /* (1,2) */
    CHECK_EQ_U32(px[10], 0x0000FFu);  /* (2,2) */
    CHECK_EQ_U32(px[7], 0xFF0000u);   /* (3,1) fora */
    /* retângulo totalmente fora não altera nada */
    pr_rect far = {100, 100, 5, 5};
    pr_surf_fill_rect(s, far, red);
    CHECK_EQ_U32(px[5], 0x0000FFu);

    /* apresentação no stream: textura do frame + PRESENT */
    pr_gfx_stream* stream = pr_gfx_stream_create(16, 0);
    CHECK(stream != NULL);
    CHECK(pr_surf_present(s, stream, 640, 360) == PR_OK);
    CHECK_EQ_U32(pr_gfx_pending(stream), 1);

    pr_gfx_cmd cmd;
    CHECK_EQ_U32(pr_gfx_pull(stream, &cmd, 1), 1);
    CHECK_EQ_U32(cmd.op, PR_GFX_PRESENT);
    CHECK_EQ_U32(cmd.a.present.width, 640);
    CHECK_EQ_U32(cmd.a.present.height, 360);

    uint32_t w = 0, h = 0;
    const uint32_t* surf_px = pr_gfx_surface(stream, &w, &h);
    CHECK(surf_px != NULL);
    CHECK_EQ_U32(w, 4);
    CHECK_EQ_U32(h, 4);
    CHECK_EQ_U32(surf_px[5], 0x0000FFu); /* pixels chegaram ao frame */

    /* buffer reaproveitado: segunda apresentação sem realocar (dados novos) */
    pr_surf_clear(s, red);
    CHECK(pr_surf_present(s, stream, 640, 360) == PR_OK);
    surf_px = pr_gfx_surface(stream, &w, &h);
    CHECK(surf_px != NULL && surf_px[5] == 0xFF0000u);

    /* reset limpa a superfície do frame (próximo frame começa vazio) */
    pr_gfx_stream_reset(stream);
    CHECK(pr_gfx_surface(stream, &w, &h) == NULL);

    /* limites */
    CHECK(pr_gfx_set_surface(stream, px, 8193, 2) == PR_ERR_RANGE);
    CHECK(pr_surf_present(NULL, stream, 0, 0) == PR_ERR_INVALID);

    /* stream cheio → erro propagado (não trava) */
    pr_gfx_stream* tiny = pr_gfx_stream_create(1, 0);
    pr_gfx_cmd_clear(tiny, red);
    CHECK(pr_surf_present(s, tiny, 32, 32) == PR_ERR_RANGE);

    pr_gfx_stream_destroy(tiny);
    pr_gfx_stream_destroy(stream);
    pr_surf_destroy(s);

    /* -- fence de sincronização -- */
    pr_sync_fence* fence = pr_sync_fence_create();
    CHECK(fence != NULL);
    CHECK_EQ_U32(pr_sync_fence_value(fence), 0);
    CHECK_EQ_U32(pr_sync_fence_reached(fence, 1), 0);
    pr_sync_fence_signal(fence);
    pr_sync_fence_signal(fence);
    CHECK_EQ_U32(pr_sync_fence_value(fence), 2);
    CHECK_EQ_U32(pr_sync_fence_reached(fence, 1), 1);
    CHECK_EQ_U32(pr_sync_fence_reached(fence, 3), 0);
    pr_sync_fence_destroy(fence);
}
