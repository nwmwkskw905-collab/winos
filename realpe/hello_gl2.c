/* hello_gl2.c — GRUPO 9: SEGUNDO PE 3D REAL (mais complexo que hello_gl.c).
 *
 * Categorias exercitadas (SOMENTE estas, determinadas por este PE):
 *   1) MATRIZES: glMatrixMode, glLoadIdentity, glOrtho, glTranslatef, glRotatef
 *   2) VERTEX ARRAYS: glEnableClientState, glVertexPointer, glColorPointer,
 *      glDrawElements (GL_TRIANGLES/UNSIGNED_SHORT)
 * Texturas NÃO são usadas (permanecem fora do subconjunto).
 *
 * Cena: cubo 3D (6 faces, cor chapada por face) girado Ry(30°)·Rx(25°) e
 * transladado (-0.2, +0.1), projeção ortográfica, z-buffer real. Os pixels
 * esperados foram DERIVADOS da geometria (ver test_gl2.c — margens >20px às
 * bordas; cores chapadas => interpolação exata), não gravados do runtime.
 */
#include <windows.h>
#include <GL/gl.h>

#define WIN_W 320
#define WIN_H 240

static const float verts[24][3] = {
    /* +Z frente (verde) */     {-0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},
    /* -Z trás (cinza) */       { 0.5f,-0.5f,-0.5f},{-0.5f,-0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},
    /* +X direita (azul-esc) */ { 0.5f,-0.5f, 0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f, 0.5f,-0.5f},{ 0.5f, 0.5f, 0.5f},
    /* -X esquerda (vermelho) */{-0.5f,-0.5f,-0.5f},{-0.5f,-0.5f, 0.5f},{-0.5f, 0.5f, 0.5f},{-0.5f, 0.5f,-0.5f},
    /* +Y topo (azul) */        {-0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f, 0.5f},{ 0.5f, 0.5f,-0.5f},{-0.5f, 0.5f,-0.5f},
    /* -Y base (marrom) */      {-0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f,-0.5f},{ 0.5f,-0.5f, 0.5f},{-0.5f,-0.5f, 0.5f},
};
static const float cols[24][3] = {
    {0,1,0},{0,1,0},{0,1,0},{0,1,0},           /* verde */
    {0.25f,0.25f,0.25f},{0.25f,0.25f,0.25f},{0.25f,0.25f,0.25f},{0.25f,0.25f,0.25f},
    {0,0,0.5f},{0,0,0.5f},{0,0,0.5f},{0,0,0.5f},
    {1,0,0},{1,0,0},{1,0,0},{1,0,0},           /* vermelho */
    {0,0,1},{0,0,1},{0,0,1},{0,0,1},           /* azul */
    {0.4f,0.2f,0.1f},{0.4f,0.2f,0.1f},{0.4f,0.2f,0.1f},{0.4f,0.2f,0.1f},
};
static const unsigned short idx[36] = {
    0,1,2, 0,2,3,        /* +Z */
    4,5,6, 4,6,7,        /* -Z */
    8,9,10, 8,10,11,     /* +X */
    12,13,14, 12,14,15,  /* -X */
    16,17,18, 16,18,19,  /* +Y */
    20,21,22, 20,22,23,  /* -Y */
};

static int read_pixel(int x, int y, unsigned char* r, unsigned char* g,
                      unsigned char* b) {
    unsigned char px[4];
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
    wc.lpszClassName = "PorticoGL2";
    if (!RegisterClassA(&wc)) return 1;

    HWND hwnd = CreateWindowExA(0, "PorticoGL2", "Portico GL2",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, WIN_W, WIN_H,
                                NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return 2;
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 3;
    HGLRC rc = wglCreateContext(hdc);
    if (!rc) return 4;
    if (!wglMakeCurrent(hdc, rc)) return 5;

    glViewport(0, 0, WIN_W, WIN_H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClearDepth(1.0);
    glEnable(GL_DEPTH_TEST);

    /* ---- MATRIZES: projeção ortográfica + modelview T·Ry·Rx ---- */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.6, 1.6, -1.2, 1.2, -10.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(-0.2f, 0.1f, 0.0f);
    glRotatef(30.0f, 0.0f, 1.0f, 0.0f);
    glRotatef(25.0f, 1.0f, 0.0f, 0.0f);
    if (glGetError() != GL_NO_ERROR) return 6;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* ---- VERTEX ARRAYS: cubo 24 vértices + 36 índices ---- */
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, verts);
    glColorPointer(3, GL_FLOAT, 0, cols);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_SHORT, idx);
    glFinish();
    if (glGetError() != GL_NO_ERROR) return 7;

    /* ---- validação objetiva (pixels derivados da geometria) ---- */
    unsigned char r, g, b;
    /* face frente (verde) — centro projetado ≈ (162.7, 108.9), margem 42px */
    if (!read_pixel(162, 108, &r, &g, &b)) return 8;
    if (r != 0 || g != 255 || b != 0) return 10;
    /* face esquerda (vermelho) — ≈ (96.7, 130.0), margem 27px */
    if (!read_pixel(96, 130, &r, &g, &b)) return 8;
    if (r != 255 || g != 0 || b != 0) return 11;
    /* face topo (azul) — ≈ (150.6, 175.3), margem 21px */
    if (!read_pixel(150, 175, &r, &g, &b)) return 8;
    if (r != 0 || g != 0 || b != 255) return 12;
    /* fora do cubo → cor de limpeza */
    if (!read_pixel(8, 8, &r, &g, &b)) return 8;
    if (r != 0 || g != 0 || b != 0) return 13;

    if (!SwapBuffers(hdc)) return 9;
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return 42;
}
