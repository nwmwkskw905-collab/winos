/* hello_gl7.c — GRUPO 14: PE de descoberta/validação de glGetIntegerv.
 *
 * Inicialização 3D realista: contexto GL -> viewport -> consultas de capacidade
 * -> textura -> luz -> matrizes. Única API nova: glGetIntegerv (5 pnames).
 *
 * pnames consultados e valores esperados (capacidades REAIS do runtime,
 * confirmadas no código — nada inventado):
 *   GL_VIEWPORT (0x0BA2)                 -> (0, 0, 320, 240) após glViewport
 *   GL_MAX_TEXTURE_SIZE (0x0D33)         -> 1024  (tex_image2d: w>1024 -> 0x501)
 *   GL_MAX_LIGHTS (0x0D31)               -> 8     (lights[8], GL_LIGHT0..7)
 *   GL_MAX_MODELVIEW_STACK_DEPTH (0x0D36)-> 32    (mv_stack[32], 33o push 0x503)
 *   GL_MAX_PROJECTION_STACK_DEPTH(0x0D38)-> 4     (pj_stack[4],  5o push 0x503)
 *
 * Uso OBSERVÁVEL de cada valor (anti-fraude):
 *   vp    -> pontos de assert DERIVADOS de vp (centro/quartos);
 *   maxt  -> textura real de maxt colunas (2 linhas: azul/verde) desenhada
 *            + PROVA DE LIMITE: maxt ok, maxt+1 -> GL_INVALID_VALUE;
 *   maxl  -> liga a luz (maxl-1)=L7 com difusa AZUL -> pixel (0,0,255);
 *            se maxl mentir para cima -> GL_LIGHT8 -> 0x500 -> falha;
 *   mvd   -> push mvd vezes OK; push mvd+1 -> GL_STACK_OVERFLOW (0x503);
 *            pop extra -> GL_STACK_UNDERFLOW (0x504);
 *   pjd   -> idem na pilha PROJECTION.
 * Errado -> exit != 42. Certo -> 42.
 */
#include <windows.h>
#include <GL/gl.h>

#define WIN_W 320
#define WIN_H 240

static unsigned char tex2[1024 * 2 * 3];   /* 2 linhas: azul (v=0), verde (v=1) */
static unsigned char tex1[1024 * 3];       /* 1 linha p/ prova de limite */

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
    wc.lpszClassName = "PorticoGL7";
    if (!RegisterClassA(&wc)) return 1;

    HWND hwnd = CreateWindowExA(0, "PorticoGL7", "Portico GL7",
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

    /* ---- consultas (RECURSO NOVO: glGetIntegerv) ---- */
    GLint vp[4] = {-1, -1, -1, -1};
    GLint maxt = -1, maxl = -1, mvd = -1, pjd = -1;
    glGetIntegerv(GL_VIEWPORT, vp);
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxt);
    glGetIntegerv(GL_MAX_LIGHTS, &maxl);
    glGetIntegerv(GL_MAX_MODELVIEW_STACK_DEPTH, &mvd);
    glGetIntegerv(GL_MAX_PROJECTION_STACK_DEPTH, &pjd);
    if (glGetError() != GL_NO_ERROR) return 25;

    /* ---- validação dos valores (capacidades reais) ---- */
    if (vp[0] != 0 || vp[1] != 0 || vp[2] != WIN_W || vp[3] != WIN_H) return 20;
    if (maxt != 1024) return 21;
    if (maxl != 8) return 22;
    if (mvd != 32) return 23;
    if (pjd != 4) return 24;

    /* pontos de assert DERIVADOS do viewport consultado */
    int cx = vp[0] + vp[2] / 2;          /* 160 */
    int q1 = vp[1] + vp[3] / 4;          /*  60 */
    int q3 = vp[1] + 3 * vp[3] / 4;      /* 180 */

    /* ---- cena 1: textura real com maxt colunas x 2 linhas ---- */
    for (int i = 0; i < 1024 * 3; i += 3) {
        tex2[i] = 0; tex2[i + 1] = 0; tex2[i + 2] = 255;        /* linha 0 azul */
        tex2[1024 * 3 + i] = 0; tex2[1024 * 3 + i + 1] = 255;   /* linha 1 verde */
        tex2[1024 * 3 + i + 2] = 0;
    }
    GLuint tn = 0;
    glGenTextures(1, &tn);
    glBindTexture(GL_TEXTURE_2D, tn);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, maxt, 2, 0, GL_RGB,
                 GL_UNSIGNED_BYTE, tex2);
    glEnable(GL_TEXTURE_2D);
    if (glGetError() != GL_NO_ERROR) return 26;

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1.0f, 1.0f, 1.0f);   /* MODULATE branco -> cor = textura */

    glBegin(GL_TRIANGLES);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
    glTexCoord2f(1.0f, 0.0f); glVertex3f( 1.0f, -1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
    glTexCoord2f(0.0f, 0.0f); glVertex3f(-1.0f, -1.0f, 0.0f);
    glTexCoord2f(1.0f, 1.0f); glVertex3f( 1.0f,  1.0f, 0.0f);
    glTexCoord2f(0.0f, 1.0f); glVertex3f(-1.0f,  1.0f, 0.0f);
    glEnd();

    unsigned char r, g, b;
    if (!read_pixel(cx, q1, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 255) return 28;       /* v=0.25 -> linha 0 */
    if (!read_pixel(cx, q3, &r, &g, &b)) return 11;
    if (r != 0 || g != 255 || b != 0) return 29;       /* v=0.75 -> linha 1 */

    /* ---- cena 2: luz (maxl-1)=L7 difusa AZUL -> pixel (0,0,255) ---- */
    glDisable(GL_TEXTURE_2D);
    glClear(GL_COLOR_BUFFER_BIT);
    {
        GLenum L7 = GL_LIGHT0 + (GLuint)(maxl - 1);
        static const float z[4] = {0, 0, 0, 1};
        static const float az[4] = {0, 0, 1, 1};
        static const float pos[4] = {0, 0, 1, 0};
        static const float mamb[4] = {0, 0, 0, 1};
        static const float mdif[4] = {1, 1, 1, 1};
        glLightfv(L7, GL_AMBIENT, z);
        glLightfv(L7, GL_DIFFUSE, az);
        glLightfv(L7, GL_SPECULAR, z);
        glLightfv(L7, GL_POSITION, pos);
        glMaterialfv(GL_FRONT, GL_AMBIENT, mamb);
        glMaterialfv(GL_FRONT, GL_DIFFUSE, mdif);
        glEnable(GL_LIGHTING);
        glEnable(L7);
        glColor3f(1.0f, 1.0f, 1.0f);
        glBegin(GL_TRIANGLES);
        glNormal3f(0.0f, 0.0f, 1.0f);
        glVertex3f(-0.5f, -0.5f, 0.0f);
        glVertex3f( 0.5f, -0.5f, 0.0f);
        glVertex3f( 0.5f,  0.5f, 0.0f);
        glNormal3f(0.0f, 0.0f, 1.0f);
        glVertex3f(-0.5f, -0.5f, 0.0f);
        glVertex3f( 0.5f,  0.5f, 0.0f);
        glVertex3f(-0.5f,  0.5f, 0.0f);
        glEnd();
    }
    if (!read_pixel(cx, vp[1] + vp[3] / 2, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 255) return 30;       /* centro = L7 azul */
    if (!read_pixel(20, 20, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 0) return 31;         /* fora do quad */

    /* ---- prova das pilhas: mvd/pjd = capacidade real (overflow/underflow) */
    glDisable(GL_LIGHTING);
    glMatrixMode(GL_MODELVIEW);
    for (int i = 0; i < mvd; i++) glPushMatrix();      /* 32 pushes: ok */
    if (glGetError() != GL_NO_ERROR) return 32;
    glPushMatrix();                                    /* 33o: overflow */
    if (glGetError() != GL_STACK_OVERFLOW) return 33;
    for (int i = 0; i < mvd; i++) glPopMatrix();
    glPopMatrix();                                     /* extra: underflow */
    if (glGetError() != GL_STACK_UNDERFLOW) return 34;

    glMatrixMode(GL_PROJECTION);
    for (int i = 0; i < pjd; i++) glPushMatrix();      /* 4 pushes: ok */
    if (glGetError() != GL_NO_ERROR) return 35;
    glPushMatrix();                                    /* 5o: overflow */
    if (glGetError() != GL_STACK_OVERFLOW) return 36;
    for (int i = 0; i < pjd; i++) glPopMatrix();
    glPopMatrix();
    if (glGetError() != GL_STACK_UNDERFLOW) return 37;

    /* ---- prova do limite de textura: maxt ok, maxt+1 -> INVALID_VALUE ---- */
    {
        GLuint t2n = 0;
        glGenTextures(1, &t2n);
        glBindTexture(GL_TEXTURE_2D, t2n);
        for (int i = 0; i < 1024 * 3; i += 3) { tex1[i] = 8; tex1[i+1] = 8; tex1[i+2] = 8; }
        glTexImage2D(GL_TEXTURE_2D, 0, 3, maxt, 1, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, tex1);          /* 1024x1: ok */
        if (glGetError() != GL_NO_ERROR) return 38;
        glTexImage2D(GL_TEXTURE_2D, 0, 3, maxt + 1, 1, 0, GL_RGB,
                     GL_UNSIGNED_BYTE, tex1);          /* 1025x1: erro */
        if (glGetError() != GL_INVALID_VALUE) return 39;
    }

    if (!SwapBuffers(hdc)) return 12;
    return 42;
}
