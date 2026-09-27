/* hello_gdi.c — PE Windows REAL (MinGW-w64): janela + fundo + retângulos +
 * pixels + atualização + finalização correta, pelo caminho completo do WinOS
 * (user32/gdi32 reais → superfície interna → estágio de submissão Metal).
 *
 * WndProc REAL do convidado (WM_PAINT canônico via BeginPaint/EndPaint).
 * Sai com 42 se o desenho foi verificado por GetPixel na superfície e o
 * ciclo de mensagens terminou com WM_QUIT (wParam=42 vindo do WM_DESTROY). */
#include <windows.h>

#define WIN_W 320
#define WIN_H 240

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        /* fundo */
        HBRUSH bg = CreateSolidBrush(RGB(20, 24, 64));
        FillRect(hdc, &rc, bg);
        DeleteObject(bg);
        /* retângulos */
        RECT r1 = { 24, 24, 120, 96 };
        HBRUSH red = CreateSolidBrush(RGB(200, 40, 40));
        FillRect(hdc, &r1, red);
        DeleteObject(red);
        RECT r2 = { 140, 40, 300, 160 };
        HBRUSH green = CreateSolidBrush(RGB(40, 180, 80));
        FillRect(hdc, &r2, green);
        DeleteObject(green);
        /* pixels */
        SetPixel(hdc, 10, 10, RGB(255, 255, 0));
        SetPixel(hdc, WIN_W - 10, WIN_H - 10, RGB(255, 0, 255));
        SetPixel(hdc, 150, 20, RGB(0, 255, 255));
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(42);   /* wParam do WM_QUIT = código de saída */
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
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
    wc.lpszClassName = "PorticoGdi";
    if (!RegisterClassA(&wc)) return 1;

    HWND hwnd = CreateWindowExA(0, "PorticoGdi", "Portico GDI",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, WIN_W, WIN_H,
                                NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return 2;

    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);   /* WM_ERASEBKGND + WM_PAINT síncronos → desenho */

    /* verificação REAL dos pixels desenhados (GetPixel sobre a janela) */
    HDC v = GetDC(hwnd);
    COLORREF c_bg  = GetPixel(v, 5, 200);     /* fundo */
    COLORREF c_r1  = GetPixel(v, 30, 30);     /* retângulo vermelho */
    COLORREF c_r2  = GetPixel(v, 150, 50);    /* retângulo verde */
    COLORREF c_px  = GetPixel(v, 10, 10);     /* pixel amarelo */
    COLORREF c_px2 = GetPixel(v, WIN_W - 10, WIN_H - 10); /* magenta */
    ReleaseDC(hwnd, v);

    int ok = (c_bg == RGB(20, 24, 64)) &&
             (c_r1 == RGB(200, 40, 40)) &&
             (c_r2 == RGB(40, 180, 80)) &&
             (c_px == RGB(255, 255, 0)) &&
             (c_px2 == RGB(255, 0, 255));
    if (!ok) return 3;

    /* atualiza mais uma vez (já desenhado) e finaliza corretamente */
    UpdateWindow(hwnd);
    DestroyWindow(hwnd);   /* WM_DESTROY → WndProc → PostQuitMessage(42) */

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    return (int)msg.wParam;   /* 42 (WM_QUIT.wParam) */
}
