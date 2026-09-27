/* hello_gl8.c — GRUPO 15: PE de descoberta do próximo bloqueador real.
 *
 * Frame 3D natural combinando SOMENTE recursos já certificados do runtime
 * (catálogo OPENGL32 auditado): glGetString/glGetIntegerv, viewport, frustum,
 * matrizes/transformações, depth (glClearDepth/glClear(GL_DEPTH_BUFFER_BIT)/
 * glEnable(GL_DEPTH_TEST)), iluminação+materiais+normais, textura com vertex
 * arrays + glDrawElements, color array + glDrawArrays, blending de overlay,
 * SwapBuffers — e o ciclo canônico de vertex arrays (enable -> draw -> disable
 * -> re-layout), padrão documentado de qualquer renderer fix-function.
 *
 * Geometria determinística (glFrustum(-1.6,1.6,-1.2,1.2,1,20), vp 320x240;
 * x_ndc = x_olho/(1.6*|z|), y_ndc = y_olho/(1.2*|z|)):
 *   A (z_olho=-2, IMEDIATO, iluminado, material VERMELHO): x_gl 80..200,
 *     y_gl 90..150. Desenhado PRIMEIRO (perto).
 *   B (z_olho=-4, ARRANJOS+textura 2x1 branco|azul + glDrawElements):
 *     x_gl 160..280, y_gl 90..150. Desenhado DEPOIS (longe) -> na sobreposição
 *     (x_gl 160..200) o DEPTH real deve preservar A (sem depth, B sobrescreve).
 *   C (z_olho=-3, color array + glDrawArrays, AMARELO): x_gl 200..280,
 *     y_gl 168..216 (triângulo).
 *   Overlay (blend SRC_ALPHA/ONE_MINUS_SRC_ALPHA, amarelo 50%): canto inferior
 *     esquerdo x_gl 20..70, y_gl 20..70 -> sobre preto = (128,128,0).
 *
 * Uso OBSERVÁVEL: asserts de pixel derivados da geometria (nada de "não está
 * preto"); o assert da sobreposição prova o z-buffer; saída 20..28 por falha;
 * exit 42 = tudo certo.
 */
#include <windows.h>
#include <GL/gl.h>

#define WIN_W 320
#define WIN_H 240

/* textura 2x1 de B: texel 0 branco, texel 1 azul */
static unsigned char tex2x1[2 * 1 * 3] = {
    255, 255, 255,   0, 0, 255
};

/* B: quad x_olho 0..4.8, y_olho -1.2..1.2 em z=-4 (x_gl 160..280, y 90..150) */
static const float b_verts[4][3] = {
    { 0.0f, -1.2f, 0.0f }, { 4.8f, -1.2f, 0.0f },
    { 4.8f,  1.2f, 0.0f }, { 0.0f,  1.2f, 0.0f }
};
static const float b_norms[4][3] = {
    { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }, { 0, 0, 1 }
};
static const float b_uvs[4][2] = {
    { 0.0f, 0.5f }, { 1.0f, 0.5f }, { 1.0f, 0.5f }, { 0.0f, 0.5f }
};
static const unsigned short b_idx[6] = { 0, 1, 2, 0, 2, 3 }; /* subtipo certificado (hello_gl3) */

/* C: triângulo x_olho 1.2..3.6, y_olho 1.44..2.88 em z=-3 (x_gl 200..280,
 * y_gl 168..216) */
static const float c_verts[3][3] = {
    { 1.2f, 1.44f, 0.0f }, { 3.6f, 1.44f, 0.0f }, { 1.2f, 2.88f, 0.0f }
};
static const float c_cols[3][3] = {
    { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }
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
    wc.lpszClassName = "PorticoGL8";
    if (!RegisterClassA(&wc)) return 1;

    HWND hwnd = CreateWindowExA(0, "PorticoGL8", "Portico GL8",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, WIN_W, WIN_H,
                                NULL, NULL, wc.hInstance, NULL);
    if (!hwnd) return 2;
    HDC hdc = GetDC(hwnd);
    if (!hdc) return 3;
    HGLRC rc = wglCreateContext(hdc);
    if (!rc) return 4;
    if (!wglMakeCurrent(hdc, rc)) return 5;

    /* ---- identidade + estado inicial (recursos certificados) ---- */
    if (glGetString(GL_VERSION) == NULL) return 21;
    glViewport(0, 0, WIN_W, WIN_H);
    {
        GLint vp[4] = {-7, -7, -7, -7};
        glGetIntegerv(GL_VIEWPORT, vp);
        if (vp[0] != 0 || vp[1] != 0 || vp[2] != WIN_W || vp[3] != WIN_H)
            return 20;
    }

    glClearDepth(1.0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.6, 1.6, -1.2, 1.2, 1.0, 20.0);
    glMatrixMode(GL_MODELVIEW);

    /* luz0 branca difusa + sem especular (comportamento real, não GPU) */
    {
        static const float z[4] = { 0, 0, 0, 1 };
        static const float wd[4] = { 1, 1, 1, 1 };
        static const float pos[4] = { 0, 0, 1, 0 };
        glLightfv(GL_LIGHT0, GL_AMBIENT, z);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, wd);
        glLightfv(GL_LIGHT0, GL_SPECULAR, z);
        glLightfv(GL_LIGHT0, GL_POSITION, pos);
    }

    /* ---- objeto A (perto, z=-2): imediato, iluminado, VERMELHO ---- */
    {
        static const float mamb[4] = { 0, 0, 0, 1 };
        static const float mdif[4] = { 1, 0, 0, 1 };
        glMaterialfv(GL_FRONT, GL_AMBIENT, mamb);
        glMaterialfv(GL_FRONT, GL_DIFFUSE, mdif);
        glEnable(GL_LIGHTING);
        glEnable(GL_LIGHT0);
    }
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -2.0f);
    glBegin(GL_TRIANGLES);
    glNormal3f(0.0f, 0.0f, 1.0f);
    glVertex3f(-1.6f, -0.6f, 0.0f);
    glVertex3f( 0.8f, -0.6f, 0.0f);
    glVertex3f( 0.8f,  0.6f, 0.0f);
    glNormal3f(0.0f, 0.0f, 1.0f);
    glVertex3f(-1.6f, -0.6f, 0.0f);
    glVertex3f( 0.8f,  0.6f, 0.0f);
    glVertex3f(-1.6f,  0.6f, 0.0f);
    glEnd();

    /* ---- objeto B (longe, z=-4): arranjos + textura + DrawElements ---- */
    glDisable(GL_LIGHTING);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -4.0f);
    {
        GLuint tn = 0;
        glGenTextures(1, &tn);
        glBindTexture(GL_TEXTURE_2D, tn);
        glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 1, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, tex2x1);
        glEnable(GL_TEXTURE_2D);
    }
    glColor3f(1.0f, 1.0f, 1.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, b_verts);
    glNormalPointer(GL_FLOAT, 0, b_norms);
    glTexCoordPointer(2, GL_FLOAT, 0, b_uvs);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, b_idx);

    /* ---- objeto C: re-layout de arranjos (ciclo canônico enable/draw/
     * disable) + color array + glDrawArrays ---- */
    /* Os disables do ciclo canônico são OBSERVÁVEIS: a textura permanece
     * ativa e o TEXCOORD_ARRAY desligado faz C usar o uv corrente (0,0) =
     * texel BRANCO (amarelo x branco = amarelo). Se o disable fosse no-op,
     * o array antigo (uv até 1.0) amostraria o texel AZUL e o assert (25)
     * veria amarelo x azul = preto. */
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glLoadIdentity();
    glTranslatef(0.0f, 0.0f, -3.0f);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, c_verts);
    glColorPointer(3, GL_FLOAT, 0, c_cols);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    /* ---- overlay: HUD em pixels (ortho canônico) + blend amarelo 50%
     * sobre preto = (128,128,0); x_gl/y_gl 20..70 ---- */
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 320.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBegin(GL_TRIANGLES);
    glColor4f(1.0f, 1.0f, 0.0f, 0.5f);
    glVertex3f(20.0f, 20.0f, 0.0f);
    glVertex3f(70.0f, 20.0f, 0.0f);
    glVertex3f(70.0f, 70.0f, 0.0f);
    glColor4f(1.0f, 1.0f, 0.0f, 0.5f);
    glVertex3f(20.0f, 20.0f, 0.0f);
    glVertex3f(70.0f, 70.0f, 0.0f);
    glVertex3f(20.0f, 70.0f, 0.0f);
    glEnd();
    glDisable(GL_BLEND);

    /* ---- asserts observáveis (posições derivadas da geometria acima) ---- */
    unsigned char r, g, b;
    if (!read_pixel(120, 120, &r, &g, &b)) return 29;
    if (r != 255 || g != 0 || b != 0) return 22;      /* A iluminado vermelho */
    if (!read_pixel(180, 120, &r, &g, &b)) return 29;
    if (r != 255 || g != 0 || b != 0) return 23;      /* sobreposição: depth
                                                       * preserva A (perto) */
    if (!read_pixel(240, 120, &r, &g, &b)) return 29;
    if (r != 0 || g != 0 || b != 255) return 24;      /* B: textura azul */
    if (!read_pixel(225, 185, &r, &g, &b)) return 29;
    if (r != 255 || g != 255 || b != 0) return 25;    /* C: color array amarelo */
    if (!read_pixel(45, 45, &r, &g, &b)) return 29;
    /* overlay: blend 50% = (128,128,0) com tolerância de arredondamento
     * f2b/bary ±1 (o blend certificado G11 produz 127 ou 128 conforme a soma
     * baricêntrica em float; nunca 0 nem 255) */
    if (r < 127 || r > 128 || g < 127 || g > 128 || b != 0) return 26;
    if (!read_pixel(300, 200, &r, &g, &b)) return 29;
    if (r != 0 || g != 0 || b != 0) return 27;        /* fundo intacto */
    if (glGetError() != GL_NO_ERROR) return 28;

    glFinish();
    if (!SwapBuffers(hdc)) return 12;
    return 42;
}
