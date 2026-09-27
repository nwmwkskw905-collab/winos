/* hello_input.c — PE Windows REAL (MinGW-w64): janela + WndProc que processa
 * teclado (WM_KEYDOWN/WM_CHAR), mouse (WM_LBUTTONDOWN/WM_MOUSEWHEEL) e
 * WM_TIMER, desenhando uma marca na superfície por evento e atualizando o
 * frame. Fonte de eventos determinística (injetada pelo driver de teste antes
 * da execução: tecla 'A', clique, wheel e timer 10 ms).
 * Quando as 5 marcas foram recebidas → PostQuitMessage(42). Verificação final
 * real dos pixels via GetPixel. Sai com 42. */
#include <windows.h>

#define WIN_W 320
#define WIN_H 240
#define TIMER_ID 7

static int g_key, g_char, g_click, g_wheel, g_tick;

static void mark(HWND h, int x, int y, COLORREF c) {
    HDC dc = GetDC(h);
    SetPixel(dc, x, y, c);
    ReleaseDC(h, dc);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT rc;
        GetClientRect(h, &rc);
        HBRUSH bg = CreateSolidBrush(RGB(16, 16, 32));
        FillRect(dc, &rc, bg);
        DeleteObject(bg);
        SetPixel(dc, 0, 0, RGB(255, 255, 255));   /* pronto */
        EndPaint(h, &ps);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == 'A') { g_key = 1; mark(h, 10, 10, RGB(255, 255, 0)); }
        return 0;
    case WM_KEYUP:
        return 0;
    case WM_CHAR:
        if (wp == 'a') { g_char = 1; mark(h, 12, 10, RGB(0, 255, 255)); }
        return 0;
    case WM_MOUSEMOVE:
        return 0;
    case WM_LBUTTONDOWN:
        g_click = 1;
        mark(h, 20, 20, RGB(255, 0, 0));
        return 0;
    case WM_MOUSEWHEEL:
        g_wheel = 1;
        mark(h, 30, 30, RGB(80, 80, 255));
        return 0;
    case WM_TIMER:
        if (wp == TIMER_ID) {
            g_tick = 1;
            mark(h, 40, 40, RGB(0, 200, 0));
            if (g_key && g_char && g_click && g_wheel && g_tick)
                PostQuitMessage(42);   /* roteiro completo */
        }
        return 0;
    case WM_DESTROY:
        KillTimer(h, TIMER_ID);
        return 0;
    default:
        return DefWindowProcA(h, m, wp, lp);
    }
}

int main(void) {
    WNDCLASSA wc;
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "PorticoInput";
    if (!RegisterClassA(&wc)) return 1;

    HWND hwnd = CreateWindowExA(0, "PorticoInput", "Portico Input",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, WIN_W, WIN_H,
                                NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return 2;
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);   /* fundo + pixel "pronto" */

    SetTimer(hwnd, TIMER_ID, 10, NULL);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);   /* WM_KEYDOWN 'A' → WM_CHAR 'a' */
        DispatchMessageA(&msg);
    }
    if (msg.wParam != 42) return (int)msg.wParam;   /* roteiro incompleto (9) */

    /* verificação REAL final dos pixels desenhados pelos eventos */
    HDC v = GetDC(hwnd);
    COLORREF c0 = GetPixel(v, 0, 0);      /* pronto (branco) */
    COLORREF c1 = GetPixel(v, 10, 10);    /* tecla (amarelo) */
    COLORREF c2 = GetPixel(v, 12, 10);    /* char (ciano) */
    COLORREF c3 = GetPixel(v, 20, 20);    /* clique (vermelho) */
    COLORREF c4 = GetPixel(v, 30, 30);    /* wheel (azul) */
    COLORREF c5 = GetPixel(v, 40, 40);    /* timer (verde) */
    COLORREF cb = GetPixel(v, 60, 60);    /* fundo intacto */
    ReleaseDC(hwnd, v);

    int ok = (c0 == RGB(255, 255, 255)) &&
             (c1 == RGB(255, 255, 0)) &&
             (c2 == RGB(0, 255, 255)) &&
             (c3 == RGB(255, 0, 0)) &&
             (c4 == RGB(80, 80, 255)) &&
             (c5 == RGB(0, 200, 0)) &&
             (cb == RGB(16, 16, 32));
    DestroyWindow(hwnd);
    return ok ? 42 : 5;
}
