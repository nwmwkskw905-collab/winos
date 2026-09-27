/* hello_gl9.c — PE Windows REAL (MinGW-w64), GRUPO 16: prova observável de
 * glDrawElements com GL_UNSIGNED_INT (índices de 32 bits little-endian).
 *
 * Vertex buffer grande (66003 vértices float3, heap) onde:
 *   verts[0..2]     = triângulo de CONTROLE (verde) — desenhado com
 *                     GL_UNSIGNED_SHORT (caminho certificado preservado);
 *   verts[464..466] = triângulo ALVO DO TRUNCAMENTO (canto inferior) — só
 *                     apareceria se o runtime truncasse 66000->uint16 (464);
 *   verts[66000..2] = triângulo GRANDE (centro, amarelo) — os três índices
 *                     passam de 65535 e referenciam estes vértices REAIS.
 * Se houvesse truncamento uint32→uint16: o centro ficaria PRETO e o canto
 * inferior esquerdo ganharia um triângulo amarelo — os asserts 23/24 pegam.
 * Contexto + viewport + matrizes + arrays + glGetError + glReadPixels +
 * SwapBuffers; exits 20..29 por falha; exit 42 = tudo certo. */
#include <windows.h>
#include <GL/gl.h>
#include <stdlib.h>

#define NV        66003
#define IDX_BIG0  66000u
#define IDX_TRUNC 464u        /* 66000 - 65536: onde o truncamento cairia */

static void read_px(int x, int y, unsigned char* r, unsigned char* g,
                    unsigned char* b) {
    unsigned char px[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    *r = px[0]; *g = px[1]; *b = px[2];
}

int main(void) {
    /* contexto */
    HDC dc = GetDC(NULL);
    if (!dc) return 20;
    HGLRC rc = wglCreateContext(dc);
    if (!rc) return 21;
    if (!wglMakeCurrent(dc, rc)) return 22;

    /* viewport + matrizes (ortho 1:1 pixel) */
    glViewport(0, 0, 320, 240);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, 320.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* vertex buffer grande no heap do convidado */
    {
        float* verts = (float*)malloc((size_t)NV * 3u * sizeof(float));
        if (!verts) return 23;
        for (int i = 0; i < NV * 3; i++) verts[i] = -9.0f;
        /* controle (verde, curto) */
        verts[0 * 3 + 0] = 20.0f;  verts[0 * 3 + 1] = 200.0f;
        verts[1 * 3 + 0] = 60.0f;  verts[1 * 3 + 1] = 200.0f;
        verts[2 * 3 + 0] = 20.0f;  verts[2 * 3 + 1] = 235.0f;
        /* alvo do truncamento (só aparece com truncamento errado) */
        verts[IDX_TRUNC * 3 + 0] = 20.0f;  verts[IDX_TRUNC * 3 + 1] = 20.0f;
        verts[(IDX_TRUNC + 1) * 3 + 0] = 60.0f;
        verts[(IDX_TRUNC + 1) * 3 + 1] = 20.0f;
        verts[(IDX_TRUNC + 2) * 3 + 0] = 20.0f;
        verts[(IDX_TRUNC + 2) * 3 + 1] = 55.0f;
        /* triângulo grande (amarelo) — só com índices 32-bit reais */
        verts[IDX_BIG0 * 3 + 0] = 120.0f; verts[IDX_BIG0 * 3 + 1] = 90.0f;
        verts[(IDX_BIG0 + 1) * 3 + 0] = 240.0f;
        verts[(IDX_BIG0 + 1) * 3 + 1] = 90.0f;
        verts[(IDX_BIG0 + 2) * 3 + 0] = 120.0f;
        verts[(IDX_BIG0 + 2) * 3 + 1] = 200.0f;

        /* arrays */
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, verts);

        /* 1) controle: GL_UNSIGNED_SHORT (caminho certificado) */
        {
            static const unsigned short idx16[3] = { 0, 1, 2 };
            glColor3f(0.0f, 1.0f, 0.0f);
            glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx16);
            if (glGetError() != GL_NO_ERROR) return 24;
        }
        /* 2) GL_UNSIGNED_INT com 3 índices > 65535 */
        {
            static const unsigned int idx32[3] = {
                IDX_BIG0, IDX_BIG0 + 1u, IDX_BIG0 + 2u
            };
            glColor3f(1.0f, 1.0f, 0.0f);
            glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, idx32);
            if (glGetError() != GL_NO_ERROR) return 25;
        }
        glFinish();

        /* validação REAL de pixels */
        unsigned char r, g, b;
        read_px(30, 210, &r, &g, &b);
        if (r != 0 || g != 255 || b != 0) { free(verts); return 26; }
        read_px(150, 120, &r, &g, &b);            /* centro: amarelo */
        if (r != 255 || g != 255 || b != 0) { free(verts); return 27; }
        read_px(30, 35, &r, &g, &b);              /* truncamento: PRETO */
        if (r != 0 || g != 0 || b != 0) { free(verts); return 28; }
        read_px(300, 30, &r, &g, &b);             /* fundo intacto */
        if (r != 0 || g != 0 || b != 0) { free(verts); return 29; }

        SwapBuffers(dc);
        free(verts);
    }
    return 42;
}
