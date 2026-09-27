/* test_input.c — GRUPO 7: teclado, mouse, touch, controle, timers, foco e o
 * pipeline completo do PE real hello_input.exe (roteiro determinístico).
 * Fonte de entrada SEPARADA (pr_win32_input_*) → mensagens Win32 → WndProc. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pt_util.h"
#include "portico/pr_win32.h"
#include "portico/pr_peproc.h"
#include "portico/pr_surf.h"

static uint8_t* pe_real_load(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

/* ---- WndProc do hospedeiro (reentrada) ---- */
static uint64_t test_wndproc(void* ud, uint64_t fn, const uint64_t args[4],
                             uint64_t* out_ret) {
    (void)ud; (void)fn;
    uint32_t msg = (uint32_t)args[1];
    if (out_ret) *out_ret = (msg == 0x000Fu ||   /* WM_PAINT */
                             msg == 0x0001u) ? 0 : 1;   /* WM_CREATE */
    return 1;
}

/* ---- TIMERPROC do hospedeiro (reentrada) ---- */
static uint64_t timer_proc_calls, timer_proc_id;
static uint64_t test_timerproc(void* ud, uint64_t fn, const uint64_t args[4],
                               uint64_t* out_ret) {
    (void)ud; (void)fn;
    timer_proc_calls++;
    timer_proc_id = args[2];
    if (out_ret) *out_ret = 0;
    return 1;
}

/* sequência canônica de criação (espelha test_gdiwin) */
static uint64_t create_window(pr_win32_ctx* ctx, uint8_t* scratch,
                              const char* cls) {
    uint8_t* wc = scratch;
    uint8_t* cname = scratch + 80;
    uint8_t* title = scratch + 96;
    memset(wc, 0, 72);
    memcpy(cname, cls, strlen(cls) + 1);
    memcpy(title, "t", 2);
    *(uint64_t*)(wc + 8) = 0x401000;                       /* wndproc fake */
    *(uint64_t*)(wc + 64) = (uint64_t)(uintptr_t)cname;    /* lpszClassName */
    uint64_t wp = 0;
    uint64_t args[12];
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)wc;
    CHECK(pr_win32_call(ctx, "user32.dll", "RegisterClassA", args, 1, &wp) == PR_OK);
    memset(args, 0, sizeof(args));
    args[0] = 0;
    args[1] = (uint64_t)(uintptr_t)cname;
    args[2] = (uint64_t)(uintptr_t)title;
    args[3] = 0x10CF0000ull;
    args[4] = 0; args[5] = 0; args[6] = 320; args[7] = 24;
    args[8] = 0; args[9] = 0; args[10] = 0; args[11] = 0;
    uint64_t hwnd = 0;
    CHECK(pr_win32_call(ctx, "user32.dll", "CreateWindowExA", args, 12, &hwnd) == PR_OK);
    CHECK(hwnd != 0);
    return hwnd;
}

static void drain(pr_win32_ctx* ctx, uint8_t* msgp, int* n) {
    uint64_t args[8], wp = 0;
    for (;;) {
        memset(msgp, 0, 48);
        memset(args, 0, sizeof(args));
        args[0] = (uint64_t)(uintptr_t)msgp;
        args[4] = 1;   /* PM_REMOVE */
        if (pr_win32_call(ctx, "user32.dll", "PeekMessageA", args, 5, &wp) != PR_OK || !wp)
            break;
        if (n) (*n)++;
    }
}

static void test_input_mensagens(void) {
    printf("-- test_input_mensagens\n");
    pr_win32_ctx* ctx = pr_win32_create(NULL);
    CHECK(ctx != NULL);
    pr_win32_set_guestcall(ctx, test_wndproc, NULL);

    size_t sz = 0;
    uint8_t* s = (uint8_t*)pr_win32_scratch(ctx, &sz);
    CHECK(s && sz >= 256);
    uint8_t* msgp = s + 128;

    uint64_t hwnd = create_window(ctx, s, "InCls");
    CHECK(pr_win32_focus_hwnd(ctx) == (uint32_t)hwnd);

    uint64_t args[8], wp = 0;
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "DispatchMessageA", args, 1, &wp) == PR_OK);

    /* ---- teclado: KEYDOWN 'A' + TranslateMessage → CHAR 'a' ---- */
    CHECK(pr_win32_input_key(ctx, W32_VK_A, 1) == PR_OK);
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "GetMessageA", args, 4, &wp) == PR_OK);
    CHECK(wp == 1);
    uint32_t mh = 0, mm = 0;
    uint64_t mwp = 0, mlp = 0;
    memcpy(&mh, msgp + 0, 4);
    memcpy(&mm, msgp + 8, 4);
    memcpy(&mwp, msgp + 16, 8);
    memcpy(&mlp, msgp + 24, 8);
    CHECK(mh == (uint32_t)hwnd);
    CHECK(mm == 0x100);            /* WM_KEYDOWN */
    CHECK(mwp == W32_VK_A);
    CHECK(mlp == 1);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "TranslateMessage", args, 1, &wp) == PR_OK);
    CHECK(wp == 1);
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "GetMessageA", args, 4, &wp) == PR_OK);
    memcpy(&mm, msgp + 8, 4);
    memcpy(&mwp, msgp + 16, 8);
    CHECK(mm == 0x102);            /* WM_CHAR */
    CHECK(mwp == 'a');
    /* GetKeyState: bit15 = pressionada */
    memset(args, 0, sizeof(args));
    args[0] = W32_VK_A;
    CHECK(pr_win32_call(ctx, "user32.dll", "GetKeyState", args, 1, &wp) == PR_OK);
    CHECK((int16_t)wp < 0);
    CHECK(pr_win32_input_key(ctx, W32_VK_A, 0) == PR_OK);
    CHECK(pr_win32_call(ctx, "user32.dll", "GetKeyState", args, 1, &wp) == PR_OK);
    CHECK((uint16_t)wp == 0);
    CHECK(pr_win32_call(ctx, "user32.dll", "GetAsyncKeyState", args, 1, &wp) == PR_OK);
    CHECK((uint16_t)wp == 0);
    drain(ctx, msgp, NULL);

    /* ---- mouse: coords + MK + wheel ---- */
    CHECK(pr_win32_input_mouse(ctx, PR_MOUSE_MOVE, 10, 20, 0) == PR_OK);
    CHECK(pr_win32_input_mouse(ctx, PR_MOUSE_LDOWN, 10, 20, 0) == PR_OK);
    CHECK(pr_win32_input_mouse(ctx, PR_MOUSE_WHEEL, 3, 4, 120) == PR_OK);
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "GetMessageA", args, 4, &wp) == PR_OK);
    memcpy(&mm, msgp + 8, 4);
    memcpy(&mlp, msgp + 24, 8);
    CHECK(mm == 0x200);            /* WM_MOUSEMOVE */
    CHECK(mlp == ((20ull << 16) | 10));
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "GetMessageA", args, 4, &wp) == PR_OK);
    memcpy(&mm, msgp + 8, 4);
    memcpy(&mwp, msgp + 16, 8);
    CHECK(mm == 0x201);            /* WM_LBUTTONDOWN */
    CHECK(mwp == 1);               /* MK_LBUTTON */
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "GetMessageA", args, 4, &wp) == PR_OK);
    memcpy(&mm, msgp + 8, 4);
    memcpy(&mwp, msgp + 16, 8);
    CHECK(mm == 0x20A);            /* WM_MOUSEWHEEL */
    CHECK(mwp == ((120ull << 16) | 1));
    CHECK(pr_win32_input_mouse(ctx, PR_MOUSE_LUP, 3, 4, 0) == PR_OK);
    drain(ctx, msgp, NULL);

    /* ---- touch: promoção p/ mouse (primário) + multi-touch rastreado ---- */
    CHECK(pr_win32_input_touch(ctx, 5, PR_TOUCH_BEGAN, 50, 60) == PR_OK);
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "GetMessageA", args, 4, &wp) == PR_OK);
    memcpy(&mm, msgp + 8, 4);
    memcpy(&mlp, msgp + 24, 8);
    CHECK(mm == 0x200 && mlp == ((60ull << 16) | 50));
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "GetMessageA", args, 4, &wp) == PR_OK);
    memcpy(&mm, msgp + 8, 4);
    CHECK(mm == 0x201);
    CHECK(pr_win32_input_touch(ctx, 9, PR_TOUCH_BEGAN, 70, 80) == PR_OK);
    CHECK(pr_win32_input_touch(ctx, 9, PR_TOUCH_MOVED, 71, 81) == PR_OK);
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "PeekMessageA", args, 5, &wp) == PR_OK);
    CHECK(wp == 0);                /* secundário: rastreado, sem mouse */
    CHECK(pr_win32_input_touch(ctx, 5, PR_TOUCH_MOVED, 55, 65) == PR_OK);
    CHECK(pr_win32_input_touch(ctx, 5, PR_TOUCH_ENDED, 55, 65) == PR_OK);
    CHECK(pr_win32_input_touch(ctx, 9, PR_TOUCH_ENDED, 71, 81) == PR_OK);
    drain(ctx, msgp, NULL);

    /* ---- controle: estado consultável (sem mensagens; XInput futuro) ---- */
    CHECK(pr_win32_input_controller(ctx, PR_CTRL_BUTTON, PR_BTN_A, 1.0f) == PR_OK);
    CHECK(pr_win32_input_controller(ctx, PR_CTRL_AXIS, PR_AXIS_MOVE_X, 0.5f) == PR_OK);
    CHECK(pr_win32_input_controller(ctx, PR_CTRL_TRIGGER, 1, 0.25f) == PR_OK);
    CHECK(pr_win32_input_controller(ctx, PR_CTRL_AXIS, PR_AXIS_CAM_Y, -2.0f) == PR_OK);
    CHECK(pr_win32_input_controller(ctx, PR_CTRL_BUTTON, 3, 1.0f) == PR_ERR_INVALID);
    const pr_input_state* pad = pr_win32_input_pad(ctx);
    CHECK(pad != NULL);
    CHECK(pad->buttons == PR_BTN_A);
    CHECK(pad->axes[PR_AXIS_MOVE_X] == 0.5f);
    CHECK(pad->trigger_rt == 0.25f);
    CHECK(pad->axes[PR_AXIS_CAM_Y] == -1.0f);

    /* ---- foco + PostMessage ---- */
    CHECK(pr_win32_call(ctx, "user32.dll", "GetFocus", NULL, 0, &wp) == PR_OK);
    CHECK(wp == hwnd);
    memset(args, 0, sizeof(args));
    args[0] = hwnd; args[1] = 0x0500; args[2] = 0xAA; args[3] = 0xBB;
    CHECK(pr_win32_call(ctx, "user32.dll", "PostMessageA", args, 4, &wp) == PR_OK);
    CHECK(wp == 1);
    args[0] = 0xDEAD;
    CHECK(pr_win32_call(ctx, "user32.dll", "PostMessageA", args, 4, &wp) == PR_OK);
    CHECK(wp == 0);
    CHECK(pr_win32_last_error(ctx) == 1400);
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "GetMessageW", args, 4, &wp) == PR_OK);
    memcpy(&mm, msgp + 8, 4);
    memcpy(&mwp, msgp + 16, 8);
    CHECK(mm == 0x500 && mwp == 0xAA);

    pr_win32_destroy(ctx);
}

static void test_input_timers(void) {
    printf("-- test_input_timers\n");
    pr_win32_ctx* ctx = pr_win32_create(NULL);
    CHECK(ctx != NULL);
    pr_win32_set_guestcall(ctx, test_timerproc, NULL);

    size_t sz = 0;
    uint8_t* s = (uint8_t*)pr_win32_scratch(ctx, &sz);
    CHECK(s && sz >= 256);
    uint8_t* msgp = s + 128;

    uint64_t hwnd = create_window(ctx, s, "TmCls");
    uint64_t args[8], wp = 0;

    /* SetTimer(hwnd, 7, 10, NULL) → WM_TIMER */
    memset(args, 0, sizeof(args));
    args[0] = hwnd; args[1] = 7; args[2] = 10; args[3] = 0;
    CHECK(pr_win32_call(ctx, "user32.dll", "SetTimer", args, 4, &wp) == PR_OK);
    CHECK(wp == 7);
    CHECK(pr_win32_advance_time(ctx, 5) == PR_OK);
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "PeekMessageA", args, 5, &wp) == PR_OK);
    CHECK(wp == 0);   /* 5ms < elapse */
    CHECK(pr_win32_advance_time(ctx, 10) == PR_OK);
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "GetMessageA", args, 4, &wp) == PR_OK);
    uint32_t mm = 0;
    uint64_t mwp = 0;
    memcpy(&mm, msgp + 8, 4);
    memcpy(&mwp, msgp + 16, 8);
    CHECK(mm == 0x113 && mwp == 7);
    /* coalesce: 2 elapses atrasados = 1 WM_TIMER */
    CHECK(pr_win32_advance_time(ctx, 25) == PR_OK);
    int nticks = 0;
    drain(ctx, msgp, &nticks);
    CHECK(nticks == 1);
    /* KillTimer → sem mais WM_TIMER */
    memset(args, 0, sizeof(args));
    args[0] = hwnd; args[1] = 7;
    CHECK(pr_win32_call(ctx, "user32.dll", "KillTimer", args, 2, &wp) == PR_OK);
    CHECK(wp == 1);
    CHECK(pr_win32_call(ctx, "user32.dll", "KillTimer", args, 2, &wp) == PR_OK);
    CHECK(wp == 0);
    CHECK(pr_win32_advance_time(ctx, 100) == PR_OK);
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "PeekMessageA", args, 5, &wp) == PR_OK);
    CHECK(wp == 0);

    /* TIMERPROC via reentrada */
    timer_proc_calls = 0;
    memset(args, 0, sizeof(args));
    args[0] = hwnd; args[1] = 9; args[2] = 4; args[3] = 0x401000;
    CHECK(pr_win32_call(ctx, "user32.dll", "SetTimer", args, 4, &wp) == PR_OK);
    CHECK(wp == 9);
    CHECK(pr_win32_advance_time(ctx, 8) == PR_OK);
    CHECK(timer_proc_calls == 1);
    CHECK(timer_proc_id == 9);
    memset(args, 0, sizeof(args));
    args[0] = hwnd; args[1] = 9;
    pr_win32_call(ctx, "user32.dll", "KillTimer", args, 2, &wp);

    /* SetTimer sem janela e sem TIMERPROC → erro real 87 */
    memset(args, 0, sizeof(args));
    args[1] = 3; args[2] = 10;
    CHECK(pr_win32_call(ctx, "user32.dll", "SetTimer", args, 4, &wp) == PR_OK);
    CHECK(wp == 0);
    CHECK(pr_win32_last_error(ctx) == 87);

    /* GetMessageA vazio, sem timers → STOP honesto */
    memset(msgp, 0, 48);
    memset(args, 0, sizeof(args));
    args[0] = (uint64_t)(uintptr_t)msgp;
    CHECK(pr_win32_call(ctx, "user32.dll", "GetMessageA", args, 4, &wp) == PR_ERR_UNSUPPORTED);

    pr_win32_destroy(ctx);
}

static void test_input_hello_pe(void) {
    printf("-- test_input_hello_pe (hello_input.exe)\n");
    size_t ilen = 0;
    size_t dlen = 0;
    uint8_t* img = pe_real_load("Tests/PorticoRuntimeTests/data/hello_input.exe", &ilen);
    uint8_t* dll = pe_real_load("Tests/PorticoRuntimeTests/data/hello_dll.dll", &dlen);
    CHECK(img != NULL && ilen > 1024);
    CHECK(dll != NULL);
    if (!img || !dll) { free(img); free(dll); return; }

    pr_log* log = pr_log_create(1024);
    pr_peproc* p = NULL;
    CHECK(pr_peproc_create(img, ilen, log, &p) == PR_OK);
    CHECK(p != NULL);
    CHECK(pr_peproc_provide_dll(p, "hello_dll.dll", dll, (uint32_t)dlen) == PR_OK);
    CHECK(pr_peproc_prepare(p) == PR_OK);
    pr_win32_ctx* ctx = pr_peproc_win32(p);
    CHECK(ctx != NULL);
    /* NÃO substituir o seam: o WndProc REAL do PE roda via proc_guest_call */

    /* fonte de eventos DETERMINÍSTICA: injetada ANTES da execução (parking) */
    CHECK(pr_win32_input_key(ctx, W32_VK_A, 1) == PR_OK);
    CHECK(pr_win32_input_key(ctx, W32_VK_A, 0) == PR_OK);
    CHECK(pr_win32_input_mouse(ctx, PR_MOUSE_MOVE, 50, 50, 0) == PR_OK);
    CHECK(pr_win32_input_mouse(ctx, PR_MOUSE_LDOWN, 50, 50, 0) == PR_OK);
    CHECK(pr_win32_input_mouse(ctx, PR_MOUSE_LUP, 50, 50, 0) == PR_OK);
    CHECK(pr_win32_input_mouse(ctx, PR_MOUSE_WHEEL, 50, 50, 120) == PR_OK);

    uint64_t exec = 0;
    int i = 0;
    while (i < 5000 && !pr_peproc_exited(p)) {
        if (pr_peproc_step(p, 10000, &exec) != PR_OK) break;
        i++;
    }
    CHECK(pr_peproc_exited(p));
    uint32_t rc = pr_peproc_exit_code(p);
    if (rc != 42)
        printf("[LOG-hello_input] %llu entradas de log\n",
               (unsigned long long)pr_log_count(log));
    CHECK(rc == 42);   /* roteiro completo: 5 marcas + verificação de pixels */

    pr_surf* sp = pr_win32_surface(ctx);
    CHECK(sp != NULL && pr_surf_width(sp) == 320);
    uint32_t* px = sp ? pr_surf_pixels(sp) : NULL;
    CHECK(px != NULL);
    if (px) {
        CHECK(px[0 * 320 + 0] == 0x00FFFFFFu);        /* pronto */
        CHECK(px[10 * 320 + 10] == 0x00FFFF00u);      /* tecla (amarelo) */
        CHECK(px[10 * 320 + 12] == 0x0000FFFFu);      /* char (ciano) */
        CHECK(px[20 * 320 + 20] == 0x00FF0000u);      /* clique (vermelho) */
        CHECK(px[30 * 320 + 30] == 0x005050FFu);      /* wheel (azul) */
        CHECK(px[40 * 320 + 40] == 0x0000C800u);      /* timer (verde) */
        CHECK(px[60 * 320 + 60] == 0x00101020u);      /* fundo intacto */
    }
    pr_peproc_destroy(p);
    free(img);
    free(dll);
}

void test_input(void) {
    test_input_mensagens();
    test_input_timers();
    test_input_hello_pe();
}
