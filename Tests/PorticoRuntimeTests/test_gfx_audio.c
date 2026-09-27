#include "pt_util.h"
#include "portico/pr_gfx.h"
#include "portico/pr_audio.h"
#include "portico/pr_cap.h"

void test_gfx_audio(void) {
    /* stream de comandos */
    pr_gfx_stream* s = pr_gfx_stream_create(8, 16);
    CHECK(s != NULL);

    pr_vertex v[3] = {
        {0, 0, 1, 0, 0},
        {1, 0, 0, 1, 0},
        {0, 1, 0, 0, 1},
    };
    CHECK(pr_gfx_push_vertices(s, v, 3) == PR_OK);
    pr_color c = {0.1f, 0.2f, 0.3f, 1.0f};
    CHECK(pr_gfx_cmd_clear(s, c) == PR_OK);
    CHECK(pr_gfx_cmd_draw(s, 3, 0) == PR_OK);
    CHECK(pr_gfx_cmd_present(s, 640, 360) == PR_OK);
    CHECK_EQ_U32((unsigned)pr_gfx_pending(s), 3);

    pr_vertex outv[3];
    memset(outv, 0, sizeof(outv));
    size_t vc = 0;
    const pr_vertex* vp = pr_gfx_vertices(s, &vc);
    CHECK_EQ_U32((unsigned)vc, 3);
    CHECK(vp != NULL);
    CHECK(vp[1].g == 1.0f);

    pr_gfx_cmd cmds[8];
    size_t n = pr_gfx_pull(s, cmds, 8);
    CHECK_EQ_U32((unsigned)n, 3);
    CHECK_EQ_U32(cmds[0].op, PR_GFX_CLEAR);
    CHECK(cmds[0].a.clear.color.g == 0.2f);
    CHECK_EQ_U32(cmds[1].op, PR_GFX_DRAW_TRIANGLES);
    CHECK_EQ_U32(cmds[1].a.draw.vertex_count, 3);
    CHECK_EQ_U32(cmds[2].op, PR_GFX_PRESENT);
    CHECK_EQ_U32((unsigned)pr_gfx_pending(s), 0);

    /* overflow de comandos */
    for (int i = 0; i < 8; i++) CHECK(pr_gfx_cmd_clear(s, c) == PR_OK);
    CHECK(pr_gfx_cmd_clear(s, c) == PR_ERR_RANGE);
    pr_gfx_stream_reset(s);
    CHECK_EQ_U32((unsigned)pr_gfx_pending(s), 0);
    pr_gfx_stream_destroy(s);

    /* ring de áudio */
    pr_audio_ring* r = pr_audio_ring_create(64);
    CHECK(r != NULL);
    float in[8] = {1, -1, 0.5f, -0.5f, 0, 0, 1, 1};
    CHECK_EQ_U32((unsigned)pr_audio_push(r, in, 4), 4);
    CHECK_EQ_U32((unsigned)pr_audio_available(r), 4);
    float out[8] = {0};
    CHECK_EQ_U32((unsigned)pr_audio_pull(r, out, 4), 4);
    CHECK(out[0] == 1.0f && out[1] == -1.0f);
    CHECK_EQ_U32((unsigned)pr_audio_available(r), 0);

    /* tom quadrado */
    pr_tone t;
    pr_tone_init(&t);
    t.wave = PR_WAVE_SQUARE;
    t.freq_hz = 480.0f;
    t.volume = 0.5f;
    float buf[128];
    pr_tone_render(&t, buf, 64);
    int nonzero = 0;
    for (int i = 0; i < 64; i++) if (buf[i * 2] != 0.0f) nonzero = 1;
    CHECK(nonzero == 1);
    CHECK(buf[0] == 0.5f || buf[0] == -0.5f);

    /* capacidades: sonda real */
    pr_cap_info cap;
    pr_cap_probe(&cap);
    CHECK(cap.page_size > 0);
    CHECK(cap.note[0] != 0);
    CHECK(cap.cpu_brand[0] != 0);
    printf("   [cap] jit=%d exec_mem=%d wx=%d '%s'\n",
           cap.jit_available, cap.exec_mem_mappable, cap.write_xor_execute, cap.note);

    pr_audio_ring_destroy(r);
}
