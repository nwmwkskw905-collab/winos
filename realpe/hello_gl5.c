/* hello_gl5.c — GRUPO 12: PE de descoberta/validação do PRÓXIMO BLOQUEADOR REAL.
 *
 * AUDITORIA G12 (fonte de verdade = código):
 *   glDrawElements/glTranslatef/glRotatef já existem desde G9 (usados por
 *   hello_gl3/gl4). glScalef NÃO existe em lugar nenhum — última função de
 *   transformação básica (TRS) ausente. glGet* (GetString/GetIntegerv) também
 *   falta (só glGetError) — fica para o próximo grupo.
 *
 * RECURSO EXERCITADO (isolado): glScalef (escala de modelo, M = M·S).
 * Única API nova deste PE. Sem iluminação/blend/texturas — pura transformação.
 *
 * Cena determinística (derivação fechada; coords GL, janela 320x240):
 *   glOrtho(-1,1,-1,1,-1,1)  -> NDC = coords de objeto (sem perspectiva)
 *   MODELVIEW = I · S, S = glScalef(2.0, 0.5, 1.0)
 *   Quad base (-0.4..0.4)^2  -> escalado (-0.8..0.8) x (-0.2..0.2)
 *   px: x_gl=(x+1)*160 -> 32..288 ; y_gl=(y+1)*120 -> 96..144
 *   glColor3f(1,0,0) sem iluminação -> (255,0,0) exato. Fundo (0,0,0).
 *
 * Asserts (GL y bottom-up) e o que cada um detecta se glScalef for falsa:
 *   1 (160,120) VERM  centro            (ambos)
 *   2 (264,120) VERM  X interno 24px    [no-op: PRETO -> pega]
 *   3 ( 56,120) VERM  X interno 24px    [no-op: PRETO -> pega]
 *   4 (160,156) PRETO Y externo 12px    [no-op: VERM  -> pega]
 *   5 (160, 84) PRETO Y externo 12px    [no-op: VERM  -> pega]
 *   6 (312,120) PRETO X externo 24px    (limpeza)
 *   7 (160,130) VERM  Y interno 14px    (sanidade)
 *   8 (208,156) PRETO canto externo     [no-op: VERM  -> pega]
 * Também detecta escala uniforme errada (0.5,0.5 / 2,2) e eixos trocados.
 * Errado -> exit != 42. Certo -> 42.
 */
#include <windows.h>
#include <GL/gl.h>

#define WIN_W 320
#define WIN_H 240

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
    wc.lpszClassName = "PorticoGL5";
    if (!RegisterClassA(&wc)) return 1;

    HWND hwnd = CreateWindowExA(0, "PorticoGL5", "Portico GL5",
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

    /* ---- projeção ortográfica (transformação só no MODELVIEW) ---- */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    /* ---- RECURSO NOVO: escala real (2, 0.5, 1) ---- */
    glScalef(2.0f, 0.5f, 1.0f);
    if (glGetError() != GL_NO_ERROR) return 6;

    glClear(GL_COLOR_BUFFER_BIT);
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_TRIANGLES);
    quad2(-0.4f, -0.4f, 0.4f, 0.4f, 0.0f);
    glEnd();
    if (glGetError() != GL_NO_ERROR) return 7;

    unsigned char r, g, b;

    if (!read_pixel(160, 120, &r, &g, &b)) return 11;
    if (r != 255 || g != 0 || b != 0) return 20;

    if (!read_pixel(264, 120, &r, &g, &b)) return 11;
    if (r != 255 || g != 0 || b != 0) return 21;

    if (!read_pixel(56, 120, &r, &g, &b)) return 11;
    if (r != 255 || g != 0 || b != 0) return 22;

    if (!read_pixel(160, 156, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 0) return 23;

    if (!read_pixel(160, 84, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 0) return 24;

    if (!read_pixel(312, 120, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 0) return 25;

    if (!read_pixel(160, 130, &r, &g, &b)) return 11;
    if (r != 255 || g != 0 || b != 0) return 26;

    if (!read_pixel(208, 156, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 0) return 27;

    if (!SwapBuffers(hdc)) return 12;
    return 42;
}
