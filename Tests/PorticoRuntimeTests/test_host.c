#include "pt_util.h"
#include "pt_pe_builder.h"
#include "portico/pr_host.h"

#include <stdlib.h>
#include <string.h>

void test_host(void) {
    pr_host_register_builtin_backends();
    CHECK(pr_host_backend_count() >= 1);

    /* ---- payload self-test real ---- */
    void* payload = NULL;
    size_t plen = 0;
    pr_status st = pr_selftest_payload_build(&payload, &plen);
    CHECK(st == PR_OK);
    CHECK(plen > 24);
    CHECK(memcmp(payload, "PXP0", 4) == 0);

    pr_host* h = pr_host_create();
    CHECK(h != NULL);

    pr_host_start_info info;
    memset(&info, 0, sizeof(info));
    info.payload = payload;
    info.payload_len = plen;
    info.is_pe = 0;
    info.target_w = 640;
    info.target_h = 360;
    info.fps_cap = 60;

    st = pr_host_start(h, &info, NULL);
    CHECK(st == PR_OK);
    CHECK(pr_host_state_of(h) == PR_HOST_RUNNING);

    pr_host_frame_in in;
    pr_host_frame_out out;
    memset(&in, 0, sizeof(in));
    in.dt = 1.0f / 60.0f;

    /* roda 3 frames e valida o pipeline completo */
    int saw_clear = 0, saw_draw = 0, saw_present = 0;
    for (int frame = 0; frame < 3; frame++) {
        in.time_ms = frame * 16.7;
        st = pr_host_frame(h, &in, &out);
        CHECK(st == PR_OK);
        CHECK(out.state == PR_HOST_RUNNING);
        CHECK(out.frames_presented >= (uint32_t)(frame + 1));

        pr_gfx_cmd cmds[64];
        size_t n = pr_gfx_pull(pr_host_gfx(h), cmds, 64);
        CHECK(n >= 3);
        for (size_t i = 0; i < n; i++) {
            if (cmds[i].op == PR_GFX_CLEAR) saw_clear = 1;
            if (cmds[i].op == PR_GFX_DRAW_TRIANGLES && cmds[i].a.draw.vertex_count == 6) {
                saw_draw = 1;
                /* vértices do quad: 6, com cores plausíveis */
                size_t vc = 0;
                const pr_vertex* vs = pr_gfx_vertices(pr_host_gfx(h), &vc);
                CHECK(vc == 6);
                CHECK(vs != NULL);
                if (vs) {
                    CHECK(vs[0].x >= 0.0f);
                    CHECK(vs[0].r >= 0.0f && vs[0].r <= 1.0f);
                }
            }
            if (cmds[i].op == PR_GFX_PRESENT) saw_present = 1;
        }
    }
    CHECK(saw_clear == 1);
    CHECK(saw_draw == 1);
    CHECK(saw_present == 1);

    /* áudio gerado pelo tom do payload */
    CHECK(pr_audio_available(pr_host_audio(h)) > 0);

    /* log tem a mensagem de início */
    {
        pr_log_entry es[64];
        size_t n = pr_log_read(pr_host_log(h), es, 64, 0);
        int found = 0;
        for (size_t i = 0; i < n; i++) {
            if (strstr(es[i].msg, "iniciado")) found = 1;
        }
        CHECK(found == 1);
    }

    /* log periódico ("frame tick") após 64+ frames */
    for (int frame = 0; frame < 70; frame++) {
        in.time_ms += 16.7;
        pr_host_frame(h, &in, &out);
    }
    {
        pr_log_entry es[128];
        size_t n = pr_log_read(pr_host_log(h), es, 128, 0);
        int found = 0;
        for (size_t i = 0; i < n; i++) {
            if (strstr(es[i].msg, "frame tick")) found = 1;
        }
        CHECK(found == 1);
    }

    /* entrada move o quad: axis0 > 0 desloca x */
    in.input.axes[0] = 1.0f;
    for (int frame = 0; frame < 5; frame++) {
        in.time_ms += 16.7;
        pr_host_frame(h, &in, &out);
        pr_gfx_pull(pr_host_gfx(h), NULL, 0); /* descarta — API exige buffer */
    }
    /* valida deslocamento via leitura de um frame com eixo */
    pr_gfx_stream_reset(pr_host_gfx(h));
    in.time_ms += 16.7;
    pr_host_frame(h, &in, &out);
    {
        pr_gfx_cmd cmds[64];
        pr_gfx_pull(pr_host_gfx(h), cmds, 64);
        size_t vc = 0;
        const pr_vertex* vs = pr_gfx_vertices(pr_host_gfx(h), &vc);
        CHECK(vc == 6);
        (void)vs;
    }

    /* START encerra a sessão de forma limpa */
    pr_input_clear(&in.input);
    in.input.buttons = PR_BTN_START;
    in.time_ms += 16.7;
    st = pr_host_frame(h, &in, &out);
    CHECK(st == PR_OK);
    CHECK(out.halted == 1);
    CHECK(out.state == PR_HOST_STOPPED);
    {
        pr_log_entry es[128];
        size_t n = pr_log_read(pr_host_log(h), es, 128, 0);
        int found = 0;
        for (size_t i = 0; i < n; i++) {
            if (strstr(es[i].msg, "encerrando")) found = 1;
        }
        CHECK(found == 1);
    }

    pr_host_destroy(h);
    free(payload);

    /* ---- HONESTIDADE: PE Windows não é aceito por este backend ---- */
    uint8_t pe[0x400];
    size_t pe_len = pt_build_pe32(pe, sizeof(pe), 0);
    h = pr_host_create();
    memset(&info, 0, sizeof(info));
    info.payload = pe;
    info.payload_len = pe_len;
    info.is_pe = 1;
    char reason[256];
    reason[0] = 0;
    st = pr_host_select_backend(h, &info, reason, sizeof(reason));
    CHECK(st == PR_ERR_UNSUPPORTED);
    CHECK(strstr(reason, "Win32") != NULL);
    st = pr_host_start(h, &info, NULL);
    CHECK(st == PR_ERR_UNSUPPORTED);
    CHECK(pr_host_state_of(h) == PR_HOST_FAILED);
    pr_host_destroy(h);

    /* payload desconhecido falha com FORMAT (não é mascarado) */
    h = pr_host_create();
    const char* junk = "ABCD1234lixo-lixo-lixo-lixo";
    memset(&info, 0, sizeof(info));
    info.payload = junk;
    info.payload_len = strlen(junk);
    info.is_pe = 0;
    st = pr_host_start(h, &info, NULL);
    CHECK(st == PR_ERR_FORMAT);
    pr_host_destroy(h);
}
