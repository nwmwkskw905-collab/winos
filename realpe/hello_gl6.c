/* hello_gl6.c — GRUPO 13: PE de descoberta/validação de glGetString.
 *
 * AUDITORIA G13: glGet* existentes = só glGetError. glGetString/glGetIntegerv
 * ausentes. Prioridade de investigação = glGetString (consulta clássica de
 * init de todo app GL: GL_VERSION). glGetIntegerv NÃO é importado por este PE
 * (remover UMA lacuna real por vez — fica para o próximo grupo).
 *
 * RECURSO EXERCITADO (isolado): glGetString com GL_VENDOR/GL_RENDERER/
 * GL_VERSION/GL_EXTENSIONS + erros reais (enum inválido -> 0x500; dentro de
 * glBegin -> 0x502 + NULL). Única API nova deste PE.
 *
 * Valores esperados (strings estáveis e coerentes com o backend REAL —
 * rasterizador por software do WinOS entregue ao Metal; NÃO há GPU Windows):
 *   GL_VENDOR     -> "Portico"
 *   GL_RENDERER   -> "Portico SoftRaster/Metal"   (24 bytes)
 *   GL_VERSION    -> "1.1 Portico Subset"         (subconjunto de 1.1)
 *   GL_EXTENSIONS -> ""                            (nenhuma extensão inventada)
 *
 * Uso OBSERVÁVEL do resultado (a cor do quad é função das strings):
 *   r = (GL_VENDOR == "Portico")            ? 255 : 0
 *   g = comprimento(GL_RENDERER)                     (24)
 *   b = (GL_EXTENSIONS[0] == 0)             ? 0 : 255
 * -> pixel esperado (255, 24, 0) = 0x00FF1800. Qualquer string errada/curta/
 *    NULL muda o pixel ou aborta antes com exit próprio (20..31).
 * Erro -> exit != 42. Certo -> 42.
 */
#include <windows.h>
#include <GL/gl.h>
#include <string.h>

#define WIN_W 320
#define WIN_H 240

static int read_pixel(int x, int y, unsigned char* r, unsigned char* g,
                      unsigned char* b) {
    unsigned char px[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    *r = px[0]; *g = px[1]; *b = px[2];
    return glGetError() == GL_NO_ERROR;
}

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
    wc.lpszClassName = "PorticoGL6";
    if (!RegisterClassA(&wc)) return 1;

    HWND hwnd = CreateWindowExA(0, "PorticoGL6", "Portico GL6",
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

    /* ---- consultas (o recurso novo) ---- */
    const GLubyte* vendor  = glGetString(GL_VENDOR);      /* 0x1F00 */
    const GLubyte* rend    = glGetString(GL_RENDERER);    /* 0x1F01 */
    const GLubyte* version = glGetString(GL_VERSION);     /* 0x1F02 */
    const GLubyte* exts    = glGetString(GL_EXTENSIONS);  /* 0x1F03 */

    if (!vendor)  return 20;
    if (!rend)    return 21;
    if (!version) return 22;
    if (!exts)    return 23;

    /* ---- validação objetiva do CONTEÚDO ---- */
    if (strcmp((const char*)vendor, "Portico") != 0) return 24;
    if (strncmp((const char*)version, "1.1", 3) != 0) return 25;
    if (strcmp((const char*)exts, "") != 0) return 26;
    if (strlen((const char*)rend) != 24) return 27;

    /* ---- erros GL reais: enum desconhecido ---- */
    const GLubyte* bad = glGetString(0x9999);
    if (bad != NULL) return 28;
    if (glGetError() != GL_INVALID_ENUM) return 29;

    /* ---- erros GL reais: dentro de glBegin -> INVALID_OPERATION ---- */
    glBegin(GL_TRIANGLES);
    bad = glGetString(GL_VERSION);
    glEnd();
    if (bad != NULL) return 30;
    if (glGetError() != GL_INVALID_OPERATION) return 31;

    /* ---- uso observável: a cor é função das strings consultadas ---- */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    {
        float r = (strcmp((const char*)vendor, "Portico") == 0) ? 1.0f : 0.0f;
        float g = (float)strlen((const char*)rend) / 255.0f;
        float b = (exts[0] == 0) ? 0.0f : 1.0f;
        glColor3f(r, g, b);
    }
    glBegin(GL_TRIANGLES);
    quad2(-0.5f, -0.5f, 0.5f, 0.5f, 0.0f);
    glEnd();
    if (glGetError() != GL_NO_ERROR) return 6;

    /* ---- pixels: centro/quadrado = (255, 24, 0); fora = (0,0,0) ---- */
    unsigned char r, g, b;
    if (!read_pixel(160, 120, &r, &g, &b)) return 11;
    if (r != 255 || g != 24 || b != 0) return 32;
    if (!read_pixel(200, 100, &r, &g, &b)) return 11;
    if (r != 255 || g != 24 || b != 0) return 33;
    if (!read_pixel(20, 20, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 0) return 34;
    if (!read_pixel(300, 220, &r, &g, &b)) return 11;
    if (r != 0 || g != 0 || b != 0) return 35;

    if (!SwapBuffers(hdc)) return 12;
    return 42;
}
