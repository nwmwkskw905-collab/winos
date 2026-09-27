/* Testes do marco visual: bitmap GDI real, BitBlt SRCCOPY com limites/stride,
 * superfície virtual e o PE visual x64 completo
 * PE→CPU x64→Win32→GDI→Bitmap→BitBlt→superfície→exit + execution log. */
#include "pt_util.h"

#include "portico/pr_win32.h"
#include "portico/pr_surf.h"
#include "portico/pr_winhello.h"
#include "portico/pr_peproc.h"
#include "portico/pr_log.h"

#include <stdlib.h>
#include <string.h>

#define PATCOPY  0x00F00021u
#define SRCCOPY  0x00CC0020u

static int log_has(const pr_log* log, const char* needle) {
    pr_log_entry es[256];
    size_t n = pr_log_read(log, es, 256, 0);
    for (size_t i = 0; i < n; i++)
        if (strstr(es[i].msg, needle)) return 1;
    return 0;
}

static uint32_t px_at(pr_surf* s, uint32_t x, uint32_t y) {
    const uint32_t* p = pr_surf_pixels(s);
    return p[y * (pr_surf_pitch(s) / 4) + x];
}

void test_visual(void) {
    pr_log* log = pr_log_create(256);
    pr_win32_ctx* ctx = pr_win32_create(log);
    CHECK(ctx != NULL);
    uint64_t args[9], ret = 0;

    /* ============ bitmap GDI real ============ */
    args[0] = 0;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleDC", args, 1, &ret) == PR_OK);
    uint32_t hdc = (uint32_t)ret;
    CHECK(hdc != 0);

    args[0] = hdc; args[1] = 16; args[2] = 8;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleBitmap", args, 3, &ret) == PR_OK);
    uint32_t hbm = (uint32_t)ret;
    CHECK(hbm != 0 && hbm != hdc);

    pr_surf* bmp = pr_win32_object_surface(ctx, hbm);
    CHECK(bmp != NULL);
    CHECK_EQ_U32(pr_surf_width(bmp), 16);
    CHECK_EQ_U32(pr_surf_height(bmp), 8);
    CHECK_EQ_U32(pr_surf_format_of(bmp), PR_SURF_XRGB8888);
    CHECK_EQ_U32((uint32_t)pr_surf_pitch(bmp), 64); /* stride real = 16*4 */

    /* dimensões inválidas recusadas (nunca finge) */
    args[0] = hdc; args[1] = 0; args[2] = 8;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleBitmap", args, 3, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)ret, 0);
    CHECK_EQ_U32(pr_win32_last_error(ctx), 87);

    /* seleção: bitmap vira alvo do DC */
    args[0] = hdc; args[1] = hbm;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "SelectObject", args, 2, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)ret, 0); /* nenhuma anterior */

    /* fundo branco no bitmap via PatBlt */
    args[0] = 0x00FFFFFF;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateSolidBrush", args, 1, &ret) == PR_OK);
    uint32_t br = (uint32_t)ret;
    args[0] = hdc; args[1] = br;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "SelectObject", args, 2, &ret) == PR_OK);
    args[0] = hdc; args[1] = 0; args[2] = 0; args[3] = 16; args[4] = 8; args[5] = PATCOPY;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "PatBlt", args, 6, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)px_at(bmp, 0, 0), 0x00FFFFFF);
    CHECK_EQ_U32((uint32_t)px_at(bmp, 15, 7), 0x00FFFFFF);

    /* retângulo vermelho (2,3,4,2) */
    args[0] = 0x000000FF;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateSolidBrush", args, 1, &ret) == PR_OK);
    args[0] = hdc; args[1] = (uint32_t)ret;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "SelectObject", args, 2, &ret) == PR_OK);
    args[0] = hdc; args[1] = 2; args[2] = 3; args[3] = 4; args[4] = 2; args[5] = PATCOPY;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "PatBlt", args, 6, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)px_at(bmp, 2, 3), 0x00FF0000);
    CHECK_EQ_U32((uint32_t)px_at(bmp, 5, 4), 0x00FF0000);
    CHECK_EQ_U32((uint32_t)px_at(bmp, 6, 3), 0x00FFFFFF); /* fora */
    CHECK_EQ_U32((uint32_t)px_at(bmp, 2, 5), 0x00FFFFFF); /* fora */
    CHECK_EQ_U32((uint32_t)px_at(bmp, 1, 3), 0x00FFFFFF); /* fora */

    /* ============ BitBlt SRCCOPY (origem/destino/limites) ============ */
    args[0] = 0;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleDC", args, 1, &ret) == PR_OK);
    uint32_t hdc2 = (uint32_t)ret;
    args[0] = hdc2; args[1] = 16; args[2] = 8;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateCompatibleBitmap", args, 3, &ret) == PR_OK);
    uint32_t hbm2 = (uint32_t)ret;
    pr_surf* bmp2 = pr_win32_object_surface(ctx, hbm2);
    CHECK(bmp2 != NULL);
    args[0] = hdc2; args[1] = hbm2;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "SelectObject", args, 2, &ret) == PR_OK);
    /* limpa destino para azul */
    args[0] = 0x00FF0000;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateSolidBrush", args, 1, &ret) == PR_OK);
    args[0] = hdc2; args[1] = (uint32_t)ret;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "SelectObject", args, 2, &ret) == PR_OK);
    args[0] = hdc2; args[1] = 0; args[2] = 0; args[3] = 16; args[4] = 8; args[5] = PATCOPY;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "PatBlt", args, 6, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)px_at(bmp2, 0, 0), 0x000000FF);

    /* cópia básica: (0,0,8,8) de (4,0) → colunas 0..7 do destino = 4..11 da origem */
    args[0] = hdc2; args[1] = 0; args[2] = 0; args[3] = 8; args[4] = 8;
    args[5] = hdc; args[6] = 4; args[7] = 0; args[8] = SRCCOPY;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "BitBlt", args, 9, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)ret, 1);
    for (uint32_t x = 0; x < 8; x++)
        for (uint32_t y = 0; y < 8; y++)
            CHECK_EQ_U32((uint32_t)px_at(bmp2, x, y), (uint32_t)px_at(bmp, x + 4, y));
    /* pixel conhecido: destino (0,3) = origem (4,3) = vermelho (rect 2..5 × 3..4) */
    CHECK_EQ_U32((uint32_t)px_at(bmp2, 0, 3), 0x00FF0000);
    /* fora da cópia: (8..15, *) continuam azuis */
    CHECK_EQ_U32((uint32_t)px_at(bmp2, 8, 0), 0x000000FF);
    CHECK_EQ_U32((uint32_t)px_at(bmp2, 15, 7), 0x000000FF);

    /* limite do destino: (12,0,8,8) de (0,0) → só 4 colunas (12..15) */
    args[0] = hdc2; args[1] = 12; args[2] = 0; args[3] = 8; args[4] = 8;
    args[5] = hdc; args[6] = 0; args[7] = 0; args[8] = SRCCOPY;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "BitBlt", args, 9, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)px_at(bmp2, 12, 3), (uint32_t)px_at(bmp, 0, 3));
    CHECK_EQ_U32((uint32_t)px_at(bmp2, 15, 3), (uint32_t)px_at(bmp, 3, 3));

    /* limite da origem: (0,0,8,8) de (12,0) → só 4 colunas válidas da origem */
    args[0] = hdc2; args[1] = 0; args[2] = 0; args[3] = 8; args[4] = 8;
    args[5] = hdc; args[6] = 12; args[7] = 0; args[8] = SRCCOPY;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "BitBlt", args, 9, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)px_at(bmp2, 0, 3), (uint32_t)px_at(bmp, 12, 3));
    CHECK_EQ_U32((uint32_t)px_at(bmp2, 3, 3), (uint32_t)px_at(bmp, 15, 3));

    /* origem negativa: (-2,0,8,8) de... sx=-2 → clip: dx ajusta, 6 colunas */
    args[0] = hdc2; args[1] = 0; args[2] = 0; args[3] = 8; args[4] = 8;
    args[5] = hdc; args[6] = (uint64_t)(uint32_t)-2; args[7] = 0; args[8] = SRCCOPY;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "BitBlt", args, 9, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)px_at(bmp2, 0, 3), (uint32_t)px_at(bmp, 0, 3));
    CHECK_EQ_U32((uint32_t)px_at(bmp2, 5, 3), (uint32_t)px_at(bmp, 5, 3));

    /* totalmente fora: nada muda, sucesso sem cópia */
    uint32_t before = (uint32_t)px_at(bmp2, 0, 0);
    args[0] = hdc2; args[1] = 20; args[2] = 0; args[3] = 4; args[4] = 4;
    args[5] = hdc; args[6] = 0; args[7] = 0; args[8] = SRCCOPY;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "BitBlt", args, 9, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)px_at(bmp2, 0, 0), before);

    /* parâmetros inválidos → recusa honesta (0) */
    args[0] = hdc2; args[1] = 0; args[2] = 0; args[3] = 0; args[4] = 8;
    args[5] = hdc; args[6] = 0; args[7] = 0; args[8] = SRCCOPY;
    CHECK(pr_win32_call(ctx, "gdi32.dll", "BitBlt", args, 9, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)ret, 0);
    args[3] = 8; args[8] = 0x12345678; /* rop desconhecido */
    CHECK(pr_win32_call(ctx, "gdi32.dll", "BitBlt", args, 9, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)ret, 0);

    pr_win32_destroy(ctx);
    pr_log_destroy(log);

    /* ============ PE visual x64 completo: o marco ============ */
    log = pr_log_create(512);
    void* data = NULL;
    size_t len = 0;
    CHECK(pr_winhello_build(6, &data, &len) == PR_OK);
    CHECK(data != NULL && len == 0xA00);

    pr_peproc* p = NULL;
    CHECK(pr_peproc_create(data, len, log, &p) == PR_OK);
    CHECK(p != NULL);
    CHECK(pr_peproc_prepare(p) == PR_OK);

    uint64_t executed = 0, total = 0;
    pr_status st = PR_OK;
    for (int i = 0; i < 200 && pr_peproc_state(p) == PR_PEPROC_STOP_NONE; i++) {
        st = pr_peproc_step(p, 1000, &executed);
        total += executed;
        if (st != PR_OK) break;
    }
    if (st != PR_OK)
        printf("  diag: %s\n", pr_peproc_diagnostic(p));
    CHECK(st == PR_OK);
    CHECK(pr_peproc_state(p) == PR_PEPROC_STOP_EXIT);
    CHECK(pr_peproc_exited(p) == 1);
    CHECK_EQ_U32(pr_peproc_exit_code(p), 0);
    CHECK(total > 30); /* prologue + ~15 chamadas de API + exit */

    /* superfície final: padrão exato (XRGB8888 0x00RRGGBB) */
    pr_surf* s = pr_peproc_surface(p);
    CHECK(s != NULL);
    CHECK_EQ_U32(pr_surf_width(s), 320);
    CHECK_EQ_U32(pr_surf_height(s), 240);
    CHECK_EQ_U32((uint32_t)pr_surf_pitch(s), 1280); /* stride documentado */
    if (s) {
        CHECK_EQ_U32((uint32_t)px_at(s, 10, 10), 0x00FFFFFF);    /* fundo */
        CHECK_EQ_U32((uint32_t)px_at(s, 0, 0), 0x00FFFFFF);
        CHECK_EQ_U32((uint32_t)px_at(s, 319, 239), 0x00FFFFFF);
        CHECK_EQ_U32((uint32_t)px_at(s, 40, 40), 0x00FF0000);   /* vermelho */
        CHECK_EQ_U32((uint32_t)px_at(s, 159, 119), 0x00FF0000); /* vermelho */
        CHECK_EQ_U32((uint32_t)px_at(s, 160, 40), 0x00FFFFFF);  /* fora */
        CHECK_EQ_U32((uint32_t)px_at(s, 40, 120), 0x00FFFFFF);  /* fora */
        CHECK_EQ_U32((uint32_t)px_at(s, 160, 120), 0x000000FF); /* azul */
        CHECK_EQ_U32((uint32_t)px_at(s, 259, 179), 0x000000FF); /* azul */
        CHECK_EQ_U32((uint32_t)px_at(s, 260, 120), 0x00FFFFFF); /* fora */
        CHECK_EQ_U32((uint32_t)px_at(s, 160, 180), 0x00FFFFFF); /* fora */
    }

    /* execution log das etapas do marco */
    CHECK(log_has(log, "[PE] loaded"));
    CHECK(log_has(log, "[CPU] execution started"));
    CHECK(log_has(log, "[WIN32] API"));
    CHECK(log_has(log, "[GDI] bitmap created"));
    CHECK(log_has(log, "[GDI] BitBlt"));
    CHECK(log_has(log, "[GDI] surface updated"));
    CHECK(log_has(log, "[PROCESS] exit code"));

    pr_peproc_destroy(p);
    free(data);
    pr_log_destroy(log);
}
