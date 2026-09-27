/* hello_gl.c — PE de controle 3D REAL do GRUPO 8 (MinGW-w64 x86_64).
 * Exercita um SUBCONJUNTO REAL do OpenGL 1.1 (rasterização por software do
 * WinOS + z-buffer): dois triângulos 3D em clip-space com teste de
 * profundidade (oclusão real) e leitura objetiva do framebuffer via
 * glReadPixels. Apresenta com SwapBuffers e sai com 42.
 *
 * Sem matrix stack (glMatrixMode/glRotatef não exercitados — subconjunto
 * mínimo documentado). Sem inventar: só usa o que o relatório lista.
 */
#include <windows.h>
#include <GL/gl.h>

#define WIN_W 320
#define WIN_H 240

static int read_pixel(HDC hdc, int x, int y,
                      unsigned char* r, unsigned char* g, unsigned char* b) {
    unsigned char px[4];
    (void)hdc;
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    *r = px[0]; *g = px[1]; *b = px[2];
    return glGetError() == GL_NO_ERROR;
}

int main(void) {
    WNDCLASSA wc;
    wc.style = 0;
    wc.lpfnWndProc = DefWindowProcA;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hIcon = NULL;
    wc.hCursor = NULL;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = NULL;
    wc.lpszClassName = "PorticoGL";
    if (!RegisterClassA(&wc)) return 1;

    HWND hwnd = CreateWindowExA(0, "PorticoGL", "Portico GL",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, WIN_W, WIN_H,
                                NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return 2;
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 3;

    /* contexto OpenGL 1.1 real (software no WinOS) */
    HGLRC rc = wglCreateContext(hdc);
    if (!rc) return 4;
    if (!wglMakeCurrent(hdc, rc)) return 5;

    glViewport(0, 0, WIN_W, WIN_H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);   /* preto exato */
    glClearDepth(1.0);
    glEnable(GL_DEPTH_TEST);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (glGetError() != GL_NO_ERROR) return 6;

    /* triângulo TRÁS (z=+0.5 → profundidade 0.75): vermelho */
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f(-0.8f, -0.8f, 0.5f);
    glVertex3f( 0.8f, -0.8f, 0.5f);
    glVertex3f( 0.0f,  0.8f, 0.5f);
    glEnd();

    /* triângulo FRENTE (z=-0.5 → profundidade 0.25): verde — oclui o trás */
    glBegin(GL_TRIANGLES);
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex3f(-0.4f, -0.4f, -0.5f);
    glVertex3f( 0.4f, -0.4f, -0.5f);
    glVertex3f( 0.0f,  0.4f, -0.5f);
    glEnd();
    glFinish();
    if (glGetError() != GL_NO_ERROR) return 7;

    /* validação OBJETIVA do framebuffer (origem GL: canto inferior esq.) */
    unsigned char r, g, b;
    /* centro (0,0 NDC) = região sobreposta → FRENTE vence o z-test → verde */
    if (!read_pixel(hdc, 160, 120, &r, &g, &b)) return 8;
    if (r != 0 || g != 255 || b != 0) return 10;
    /* y=-2/3 NDC → só o trás cobre (frente vai até -0.4) → vermelho */
    if (!read_pixel(hdc, 160, 40, &r, &g, &b)) return 8;
    if (r != 255 || g != 0 || b != 0) return 11;
    /* fora dos dois triângulos → cor de limpeza preta */
    if (!read_pixel(hdc, 20, 20, &r, &g, &b)) return 8;
    if (r != 0 || g != 0 || b != 0) return 12;

    /* apresentação do frame no caminho já certificado (Surface→BGRA8→Metal) */
    if (!SwapBuffers(hdc)) return 9;

    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return 42;
}
