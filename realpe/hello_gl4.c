/* hello_gl4.c — GRUPO 11: QUARTO PE 3D REAL (mais complexo que hello_gl3).
 *
 * Novas categorias em PE real (as 2 clássicas ausentes em G8–G10):
 *   1) ILUMINAÇÃO fixed-function: glLightfv + glMaterialfv + glNormal3f
 *      + glNormalPointer + GL_LIGHTING/GL_LIGHT0 (Gouraud por vértice)
 *   2) BLENDING/ALPHA: glColor4f + glBlendFunc + GL_BLEND
 *      (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA — fórmula real)
 * Reutiliza: perspectiva (glFrustum), matrix stack, vertex arrays,
 * glDrawElements, z-buffer, glReadPixels, SwapBuffers.
 *
 * Cena determinística (derivação fechada; coords GL, janela 320x240):
 *   Luz 0 direcional (0,0,1), difusa VERMELHA (1,0,0), ambiente/especular 0;
 *   material: difusa branca, ambiente 0. Cor = N·L · (1,0,0).
 *   - Quad ESQ (imediato, N=(0,0,1))        -> N·L=1   -> (255,  0,  0)
 *     px x 60..153.3, y 53.3..186.7
 *   - Quad DIR (arrays, N=(0.6,0,0.8))      -> N·L=0.8 -> (204,  0,  0)
 *     px x 166.7..260
 *   - Overlay BLEND frente (z=+0.1, azul a=0.25) sobre o ESQ -> (191,0,64)
 *   - Overlay BLEND trás  (z=-0.1, verde a=0.25) sobre o DIR -> z-buffer
 *     REJEITA (0.8602 > 0.8547) e o pixel fica (204,0,0) = prova z+blend
 *   - Fundo preto. Asserts com margem >=23px de qualquer borda.
 * Errado -> exit != 42. Certo -> 42.
 */
#include <windows.h>
#include <GL/gl.h>

#define WIN_W 320
#define WIN_H 240

/* quad direito: arrays posição + normal (N=(0.6,0,0.8) constante) */
static const float rverts[4][3] = {
    { 0.05f, -0.5f, 0.0f}, { 0.75f, -0.5f, 0.0f},
    { 0.75f,  0.5f, 0.0f}, { 0.05f,  0.5f, 0.0f}
};
static const float rnorms[4][3] = {
    {0.6f, 0.0f, 0.8f}, {0.6f, 0.0f, 0.8f},
    {0.6f, 0.0f, 0.8f}, {0.6f, 0.0f, 0.8f}
};
static const unsigned short ridx[6] = { 0, 1, 2, 0, 2, 3 };

static int read_pixel(int x, int y, unsigned char* r, unsigned char* g,
                      unsigned char* b) {
    unsigned char px[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    *r = px[0]; *g = px[1]; *b = px[2];
    return glGetError() == GL_NO_ERROR;
}

/* 2 triângulos de um quad (modo imediato) */
static void quad2(float x0, float y0, float x1, float y1, float z) {
    glVertex3f(x0, y0, z); glVertex3f(x1, y0, z); glVertex3f(x1, y1, z);
    glVertex3f(x0, y0, z); glVertex3f(x1, y1, z); glVertex3f(x0, y1, z);
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
    wc.lpszClassName = "PorticoGL4";
    if (!RegisterClassA(&wc)) return 1;

    HWND hwnd = CreateWindowExA(0, "PorticoGL4", "Portico GL4",
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

    /* ---- câmera (mesma projeção certificada do hello_gl3) ---- */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glFrustum(-0.2, 0.2, -0.15, 0.15, 0.5, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -3.0f);

    /* ---- ILUMINAÇÃO: luz 0 direcional difusa VERMELHA ---- */
    static const float amb[4]  = {0, 0, 0, 1};
    static const float dif[4]  = {1, 0, 0, 1};
    static const float spec[4] = {0, 0, 0, 1};
    static const float pos[4]  = {0, 0, 1, 0};   /* direcional (w=0) */
    static const float mamb[4] = {0, 0, 0, 1};
    static const float mdif[4] = {1, 1, 1, 1};
    glLightfv(GL_LIGHT0, GL_AMBIENT, amb);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    glLightfv(GL_LIGHT0, GL_SPECULAR, spec);
    glLightfv(GL_LIGHT0, GL_POSITION, pos);
    glMaterialfv(GL_FRONT, GL_AMBIENT, mamb);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, mdif);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    if (glGetError() != GL_NO_ERROR) return 6;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* ---- quad ESQ (imediato, N=(0,0,1)) -> (255,0,0) ---- */
    glBegin(GL_TRIANGLES);
    glNormal3f(0.0f, 0.0f, 1.0f);
    quad2(-0.75f, -0.5f, -0.05f, 0.5f, 0.0f);
    glEnd();

    /* ---- quad DIR (arrays + normal pointer, N=(0.6,0,0.8)) -> (204,0,0) ---- */
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, rverts);
    glNormalPointer(GL_FLOAT, 0, rnorms);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, ridx);
    if (glGetError() != GL_NO_ERROR) return 7;

    /* ---- BLENDING real sobre as duas regiões ---- */
    glDisable(GL_LIGHTING);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    /* overlay TRÁS (z=-0.1): z-buffer REJEITA sobre o quad direito */
    glBegin(GL_TRIANGLES);
    glColor4f(0.0f, 1.0f, 0.0f, 0.25f);
    quad2(0.2f, 0.05f, 0.6f, 0.45f, -0.1f);
    /* overlay FRENTE (z=+0.1): passa no z-test e mistura (128,0,128) */
    glColor4f(0.0f, 0.0f, 1.0f, 0.25f);
    quad2(-0.6f, 0.05f, -0.2f, 0.45f, 0.1f);
    glEnd();
    glDisable(GL_BLEND);
    glFinish();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    if (glGetError() != GL_NO_ERROR) return 8;

    /* ---- validação objetiva (pixels derivados; margem >=23px) ---- */
    unsigned char r, g, b;
    if (!read_pixel(105, 85, &r, &g, &b)) return 11;
    if (r != 255 || g != 0 || b != 0) return 20;           /* N·L=1 */
    if (!read_pixel(215, 85, &r, &g, &b)) return 11;
    if (r != 204 || g != 0 || b != 0) return 21;           /* N·L=0.8 */
    if (!read_pixel(105, 150, &r, &g, &b)) return 11;
    if (r != 191 || g != 0 || b != 64) return 22;          /* blend a=0.25 */
    if (!read_pixel(215, 150, &r, &g, &b)) return 11;
    if (r != 204 || g != 0 || b != 0) return 23;           /* z rejeitou */
    if (!read_pixel(160, 30, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 0) return 24;             /* fundo */
    if (!read_pixel(8, 8, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 0) return 25;             /* fundo */

    if (!SwapBuffers(hdc)) return 12;
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return 42;
}
