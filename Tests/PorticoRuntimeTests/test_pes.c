/* Testes dos PEs de teste da FASE 5 (variantes 7/8/9):
 * 7 — memória: VirtualAlloc/HeapAlloc, escrita/leitura de padrão, exit code;
 * 8 — GDI+frame: bitmap 160x120 → StretchBlt 2x → BitBlt recorte → SetPixel;
 * 9 — APIs Kernel32 + GDI + MessageBoxA honesta, ambiente/cwd/cmdline.
 * Caminho completo: PE→imports→CPU x64→Win32→GDI→superfície→exit + logs
 * [MEM]/[GDI]/[WIN32]/[PROCESS] com tempo de execução. */
#include "pt_util.h"

#include "portico/pr_win32.h"
#include "portico/pr_surf.h"
#include "portico/pr_winhello.h"
#include "portico/pr_peproc.h"
#include "portico/pr_log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static int pes_log_has(const pr_log* log, const char* needle) {
    pr_log_entry es[512];
    size_t n = pr_log_read(log, es, 512, 0);
    for (size_t i = 0; i < n; i++)
        if (strstr(es[i].msg, needle)) return 1;
    return 0;
}

static uint32_t pes_px(pr_surf* s, uint32_t x, uint32_t y) {
    const uint32_t* p = pr_surf_pixels(s);
    return p[y * (pr_surf_pitch(s) / 4) + x];
}

/* roda o PE até a parada; retorna status final */
static pr_status pes_run(pr_peproc* p, uint64_t* total) {
    uint64_t executed = 0;
    if (total) *total = 0;
    pr_status st = PR_OK;
    for (int i = 0; i < 300 && pr_peproc_state(p) == PR_PEPROC_STOP_NONE; i++) {
        st = pr_peproc_step(p, 1000, &executed);
        if (total) *total += executed;
        if (st != PR_OK) break;
    }
    return st;
}

static void pes_fail_report(pr_peproc* p, pr_status st) {
    if (st != PR_OK || pr_peproc_state(p) != PR_PEPROC_STOP_EXIT)
        printf("  diag: %s\n", pr_peproc_diagnostic(p));
}

void test_pes(void) {
    /* ============ PE 7: memória (aloca, escreve, lê, retorna) ============ */
    {
        pr_log* log = pr_log_create(512);
        void* data = NULL;
        size_t len = 0;
        CHECK(pr_winhello_build(7, &data, &len) == PR_OK);
        pr_peproc* p = NULL;
        CHECK(pr_peproc_create(data, len, log, &p) == PR_OK);
        CHECK(pr_peproc_prepare(p) == PR_OK);
        uint64_t total = 0;
        pr_status st = pes_run(p, &total);
        pes_fail_report(p, st);
        CHECK(st == PR_OK);
        CHECK(pr_peproc_state(p) == PR_PEPROC_STOP_EXIT);
        CHECK_EQ_U32(pr_peproc_exit_code(p), 0);   /* 4 checagens internas */
        CHECK(total > 15);
        /* diagnóstico completo + logs estruturados da FASE 6 */
        CHECK(strstr(pr_peproc_diagnostic(p), "PROCESS EXIT") != NULL);
        CHECK(pes_log_has(log, "[PE] loaded"));
        CHECK(pes_log_has(log, "[MEM] stack mapped"));
        CHECK(pes_log_has(log, "[MEM] api stubs"));
        CHECK(pes_log_has(log, "[MEM] image mapped"));
        CHECK(pes_log_has(log, "[CPU] execution started"));
        CHECK(pes_log_has(log, "[WIN32] API kernel32.dll!VirtualAlloc"));
        CHECK(pes_log_has(log, "[WIN32] API kernel32.dll!HeapAlloc"));
        CHECK(pes_log_has(log, "[PROCESS] exit code 0 after"));
        pr_peproc_destroy(p);
        free(data);
        pr_log_destroy(log);
    }

    /* ============ PE 8: GDI + StretchBlt + BitBlt + SetPixel → frame ============ */
    {
        pr_log* log = pr_log_create(512);
        void* data = NULL;
        size_t len = 0;
        CHECK(pr_winhello_build(8, &data, &len) == PR_OK);
        pr_peproc* p = NULL;
        CHECK(pr_peproc_create(data, len, log, &p) == PR_OK);
        CHECK(pr_peproc_prepare(p) == PR_OK);
        uint64_t total = 0;
        pr_status st = pes_run(p, &total);
        pes_fail_report(p, st);
        CHECK(st == PR_OK);
        CHECK(pr_peproc_state(p) == PR_PEPROC_STOP_EXIT);
        CHECK_EQ_U32(pr_peproc_exit_code(p), 0);

        /* frame 320x240 com retângulos escalados 2x (mesmo padrão do marco) */
        pr_surf* s = pr_peproc_surface(p);
        CHECK(s != NULL);
        if (s) {
            CHECK_EQ_U32(pr_surf_width(s), 320);
            CHECK_EQ_U32(pr_surf_height(s), 240);
            CHECK_EQ_U32(pes_px(s, 10, 10), 0x00FFFFFF);
            CHECK_EQ_U32(pes_px(s, 40, 40), 0x00FF0000);    /* vermelho 2x */
            CHECK_EQ_U32(pes_px(s, 159, 119), 0x00FF0000);
            CHECK_EQ_U32(pes_px(s, 160, 40), 0x00FFFFFF);
            CHECK_EQ_U32(pes_px(s, 160, 120), 0x000000FF);  /* azul 2x */
            CHECK_EQ_U32(pes_px(s, 259, 179), 0x000000FF);
            CHECK_EQ_U32(pes_px(s, 260, 120), 0x00FFFFFF);
            /* recorte BitBlt (280,0,40,30) do bitmap 160x120 */
            CHECK_EQ_U32(pes_px(s, 285, 5), 0x00FFFFFF);
            CHECK_EQ_U32(pes_px(s, 300, 25), 0x00FF0000);   /* vermelho no recorte */
            /* acentos SetPixel (COLORREF verde 0x0000FF00) */
            CHECK_EQ_U32(pes_px(s, 5, 5), 0x0000FF00);
            CHECK_EQ_U32(pes_px(s, 314, 234), 0x0000FF00);
        }
        CHECK(pes_log_has(log, "[GDI] StretchBlt"));
        CHECK(pes_log_has(log, "[GDI] BitBlt"));
        CHECK(pes_log_has(log, "[GDI] surface updated"));
        CHECK(pes_log_has(log, "[PROCESS] exit code 0 after"));
        pr_peproc_destroy(p);
        free(data);
        pr_log_destroy(log);
    }

    /* ============ PE 9: Kernel32 + GDI + MessageBox + ambiente ============ */
    {
        pr_log* log = pr_log_create(512);
        void* data = NULL;
        size_t len = 0;
        CHECK(pr_winhello_build(9, &data, &len) == PR_OK);
        pr_peproc* p = NULL;
        CHECK(pr_peproc_create(data, len, log, &p) == PR_OK);
        /* processo TEM ambiente, cwd e linha de comando (FASE 2) */
        pr_win32_ctx* w = pr_peproc_win32(p);
        CHECK(w != NULL);
        CHECK(pr_win32_env_set(w, "PORTICO_TEST", "ok") == PR_OK);
        CHECK(pr_win32_set_cwd(w, "C:\\portico") == PR_OK);
        CHECK(pr_win32_set_cmdline(w, "pe9.exe --fase5") == PR_OK);
        CHECK(pr_peproc_prepare(p) == PR_OK);
        uint64_t total = 0;
        pr_status st = pes_run(p, &total);
        pes_fail_report(p, st);
        CHECK(st == PR_OK);
        CHECK(pr_peproc_state(p) == PR_PEPROC_STOP_EXIT);
        CHECK_EQ_U32(pr_peproc_exit_code(p), 0);   /* 6 checagens internas */

        pr_surf* s = pr_peproc_surface(p);
        CHECK(s != NULL);
        if (s) {
            CHECK_EQ_U32(pes_px(s, 200, 200), 0x00FFFFFF);  /* PatBlt branco */
            CHECK_EQ_U32(pes_px(s, 10, 10), 0x00FF0000);    /* SetPixel vermelho */
        }
        CHECK(pes_log_has(log, "[WIN32] API kernel32.dll!GetModuleHandleA"));
        CHECK(pes_log_has(log, "[WIN32] API kernel32.dll!GetProcAddress"));
        CHECK(pes_log_has(log, "[WIN32] API kernel32.dll!GetCommandLineA"));
        CHECK(pes_log_has(log, "[WIN32] API kernel32.dll!GetEnvironmentVariableA"));
        CHECK(pes_log_has(log, "[WIN32] API kernel32.dll!GetSystemInfo"));
        CHECK(pes_log_has(log, "[WIN32] API user32.dll!MessageBoxA"));
        CHECK(pes_log_has(log, "sem UI"));
        CHECK(pes_log_has(log, "[PROCESS] exit code 0 after"));
        pr_peproc_destroy(p);
        free(data);
        pr_log_destroy(log);
    }

    /* ============ PE 10: CRT — startup + mem/string + SSE + retorno ============
     * mainCRTStartup -> main (call/ret) -> fn_memcpy(REP MOVSB),
     * fn_memset(REP STOSB), fn_strlen(REPNE SCASB), fn_memcmp(REPE CMPSB),
     * SSE (PXOR/MOVAPS/MOVUPS/MOVD/MOVQ), checksum com LODSB e
     * ExitProcess(eax) com eax = nº de FALHAS internas (0 = tudo certo). */
    {
        pr_log* log = pr_log_create(512);
        void* data = NULL;
        size_t len = 0;
        CHECK(pr_winhello_build(10, &data, &len) == PR_OK);
        pr_peproc* p = NULL;
        CHECK(pr_peproc_create(data, len, log, &p) == PR_OK);
        CHECK(pr_peproc_prepare(p) == PR_OK);
        uint64_t total = 0;
        pr_status st = pes_run(p, &total);
        pes_fail_report(p, st);
        CHECK(st == PR_OK);
        CHECK(pr_peproc_state(p) == PR_PEPROC_STOP_EXIT);
        CHECK_EQ_U32(pr_peproc_exit_code(p), 0);  /* 11 checagens internas */
        CHECK(total > 30);                        /* call/ret das fns CRT */
        CHECK(pes_log_has(log, "[PROCESS] exit code 0 after"));
        pr_peproc_destroy(p);
        free(data);
        pr_log_destroy(log);
    }
}
