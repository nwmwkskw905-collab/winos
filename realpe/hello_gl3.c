/* hello_gl3.c — GRUPO 10: TERCEIRO PE 3D REAL (mais complexo que hello_gl2).
 *
 * Recursos exercitados (SOMENTE estes):
 *   1) MATRIX STACK: glPushMatrix/glPopMatrix (PROJECTION e MODELVIEW)
 *   2) PERSPECTIVA REAL: glFrustum
 *   3) VERTEX ARRAYS: glDrawElements (quad) + glDrawArrays (triângulo) +
 *      glTexCoordPointer
 *   4) TEXTURAS REAIS: glGenTextures/glBindTexture/glTexParameteri/
 *      glTexImage2D (2x2 embutida) + glEnable/glDisable(GL_TEXTURE_2D)
 *
 * Cena determinística: quad texturado 2x2 (verde/vermelho/azul/branco)
 * perpendicular à câmera a z=-3 + triângulo laranja girado Rz(20°) a z=-2.5
 * (mais perto — oclui a textura pelo z-buffer). Pixels esperados DERIVADOS
 * da geometria/projeção (centros de texel = cores exatas por GL_NEAREST;
 * margens >20px às bordas).
 */
#include <windows.h>
#include <GL/gl.h>

#define WIN_W 320
#define WIN_H 240

/* textura 2x2 embutida (ordem GL: linha v=0 primeiro):
 * (0,0)=vermelho (1,0)=verde (0,1)=azul (1,1)=branco */
static const unsigned char texels[2 * 2 * 3] = {
    255, 0, 0,    0, 255, 0,
    0, 0, 255,    255, 255, 255
};

/* quad texturado (4 vértices, UV 0..1, cor branca = GL_MODULATE) */
static const float qverts[4][3] = {
    {-0.75f, -0.75f, 0.0f}, { 0.75f, -0.75f, 0.0f},
    { 0.75f,  0.75f, 0.0f}, {-0.75f,  0.75f, 0.0f}
};
static const float quvs[4][2] = {
    {0, 0}, {1, 0}, {1, 1}, {0, 1}
};
static const float qcols[4][3] = {
    {1, 1, 1}, {1, 1, 1}, {1, 1, 1}, {1, 1, 1}
};
static const unsigned short qidx[6] = { 0, 1, 2, 0, 2, 3 };

/* triângulo laranja chapado (glDrawArrays) */
static const float tverts[3][3] = {
    {-0.55f, -0.45f, 0.0f}, { 0.55f, -0.45f, 0.0f}, { 0.0f, 0.55f, 0.0f}
};
static const float tcols[3][3] = {
    {1.0f, 0.5f, 0.0f}, {1.0f, 0.5f, 0.0f}, {1.0f, 0.5f, 0.0f}
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
    wc.lpszClassName = "PorticoGL3";
    if (!RegisterClassA(&wc)) return 1;

    HWND hwnd = CreateWindowExA(0, "PorticoGL3", "Portico GL3",
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

    /* ---- PROJECTION: pilha + perspectiva real ---- */
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();                 /* pilha da PROJECTION */
    glLoadIdentity();
    glFrustum(-0.2, 0.2, -0.15, 0.15, 0.5, 20.0);

    /* ---- MODELVIEW: câmera ---- */
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -3.0f);
    if (glGetError() != GL_NO_ERROR) return 6;

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* ---- TEXTURA REAL 2x2 ---- */
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) return 7;
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 2, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, texels);
    glEnable(GL_TEXTURE_2D);
    if (glGetError() != GL_NO_ERROR) return 8;

    /* ---- quad texturado via glDrawElements ---- */
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, qverts);
    glColorPointer(3, GL_FLOAT, 0, qcols);
    glTexCoordPointer(2, GL_FLOAT, 0, quvs);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, qidx);
    if (glGetError() != GL_NO_ERROR) return 9;

    /* ---- matrix stack MODELVIEW: triângulo girado à frente ---- */
    glPushMatrix();
    glTranslatef(0.0f, 0.0f, 0.5f);
    glRotatef(20.0f, 0.0f, 0.0f, 1.0f);
    glDisable(GL_TEXTURE_2D);
    glVertexPointer(3, GL_FLOAT, 0, tverts);
    glColorPointer(3, GL_FLOAT, 0, tcols);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glPopMatrix();
    glFinish();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();                  /* balanceia a pilha da PROJECTION */
    if (glGetError() != GL_NO_ERROR) return 10;

    /* ---- validação objetiva (derivada da geometria/textura; pontos
     * verificados fora do triângulo com margem >=24px de qualquer
     * borda/fronteira de texel/aresta do triângulo) ---- */
    unsigned char r, g, b;
    /* 4 regiões de texel do quad (GL_NEAREST = cor exata do texel) */
    if (!read_pixel(86, 90, &r, &g, &b)) return 11;
    if (r != 255 || g != 0 || b != 0) return 20;          /* texel (0,0) vermelho */
    if (!read_pixel(235, 45, &r, &g, &b)) return 11;
    if (r != 0 || g != 255 || b != 0) return 21;          /* texel (1,0) verde */
    if (!read_pixel(93, 181, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 255) return 22;          /* texel (0,1) azul */
    if (!read_pixel(219, 179, &r, &g, &b)) return 11;
    if (r != 255 || g != 255 || b != 255) return 23;      /* texel (1,1) branco */
    /* triângulo laranja OCLUI a textura (z=−2.5 < −3) — prova z-buffer
     * (abaixo dele a textura seria verde: u=0.53 v=0.41) */
    if (!read_pixel(166, 102, &r, &g, &b)) return 11;
    if (r != 255 || g != 128 || b != 0) return 24;
    /* fora do quad → cor de limpeza */
    if (!read_pixel(8, 8, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 0) return 25;

    if (!SwapBuffers(hdc)) return 12;
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return 42;
}
