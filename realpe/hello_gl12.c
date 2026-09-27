/* hello_gl12.c — PE Windows REAL (MinGW-w64), GRUPO 21: galeria fase 3.
 * Cenário de DESCOBERTA: avança além do subconjunto exercitado pelo
 * hello_gl11 — (a) processamento procedural de textura com switch e fusão
 * inteira de pixels (codegen real: tabela de saltos/SSE empacotado se o
 * compilador emitir), (b) prisma facetado com glShadeModel(GL_FLAT) sobre
 * color array (cor do último vértice por triângulo), (c) cortina translúcida
 * com glDepthMask(GL_FALSE) — o marcador ATRÁS da cortina deve aparecer,
 * provando que a cortina não escreveu profundidade.
 *
 * Geometria deterministica (glFrustum(-1.6,1.6,-1.2,1.2,1,20), vp 320x240):
 *   x_gl=(x/(1.6|z|)+1)*160, y_gl=(y/(1.2|z|)+1)*120.
 *   assoalho z=-4 solido (0.2,0.2,0.2): x_gl 80..240, y_gl 80..160;
 *   painel z=-2: x_gl 120..200, y_gl 95..145 (diagonal v0-v2 divide os
 *     dois triangulos; FLAT: {0,1,2}=v2 azul, {0,2,3}=v3 amarelo);
 *   cortina z=-1.5 50% ciano: x_gl 125..155, y_gl 100..140 (depth mask off);
 *   marcador z=-1.75 (entre cortina e painel) branco: x_gl 135..145,
 *     y_gl 110..130 — so' aparece se a cortina nao gravar profundidade;
 *   painel de textura procedural em orto: x_gl 220..284, y_gl 170..234
 *     (8x8 texels de 8 px; centro do texel (i,j) = (224+8i, 174+8j)).
 * glReadPixels por assert; exit 42 no caminho correto; 20..22 contexto;
 * 30..39 sondas.
 */
#include <windows.h>
#include <GL/gl.h>

#define WIN_W 320
#define WIN_H 240

static unsigned char tex[8 * 8 * 3];

static void gen_tex(void) {
    static const unsigned char pal[4][2] = { { 0, 255 }, { 255, 0 },
                                             { 128, 128 }, { 64, 192 } };
    unsigned char tmp[8 * 8 * 3];
    unsigned sum = 0;
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            int mode = (y >= 4 ? 2 : 0) + (x >= 4 ? 1 : 0);
            unsigned char v;
            switch (mode) {          /* despacho real por modo */
            case 0:  v = (unsigned char)(((x ^ y) & 1) ? 255 : 0); break;
            case 1:  v = (unsigned char)(x * 36); break;
            case 2:  v = (unsigned char)((x + y) * 18); break;
            default: v = (unsigned char)(255 - x * 32); break;
            }
            int i = (y * 8 + x) * 3;
            tmp[i + 0] = v;
            tmp[i + 1] = pal[mode][0];
            tmp[i + 2] = pal[mode][1];
            sum += v;
        }
    }
    /* fusao inteira de pixels (media de bytes) — codigo real de imagem */
    for (int i = 0; i < 8 * 8 * 3; i++) tex[i] = (unsigned char)((tmp[i] + 64) >> 1);
    (void)sum;
}

static void read_px(int x, int y, unsigned char* r, unsigned char* g,
                    unsigned char* b) {
    unsigned char px[4];
    glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    *r = px[0]; *g = px[1]; *b = px[2];
}

static int px_is(int x, int y, int R, int G, int B) {
    unsigned char r, g, b;
    read_px(x, y, &r, &g, &b);
    return r == R && g == G && b == B;
}

int main(void) {
    /* (a) processamento procedural ANTES de qualquer GL */
    gen_tex();

    /* contexto */
    HDC dc = GetDC(NULL);
    if (!dc) return 20;
    HGLRC rc = wglCreateContext(dc);
    if (!rc) return 21;
    if (!wglMakeCurrent(dc, rc)) return 22;

    glViewport(0, 0, WIN_W, WIN_H);
    glClearDepth(1.0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.6, 1.6, -1.2, 1.2, 1.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* (b) prisma facetado: cor solida por triangulo (ultimo vertice) */
    glShadeModel(GL_FLAT);

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    /* assoalho solido cinza-escuro */
    {
        static const float fv[12] = {
            -3.2f, -1.6f, -4.0f,   3.2f, -1.6f, -4.0f,
             3.2f,  1.6f, -4.0f,  -3.2f,  1.6f, -4.0f
        };
        static const unsigned int idx[6] = { 0, 1, 2, 0, 2, 3 };
        glColor3f(0.2f, 0.2f, 0.2f);
        glEnableClientState(GL_VERTEX_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, fv);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, idx);
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    /* painel facetado (color array): v2 azul, v3 amarelo sob GL_FLAT */
    {
        static const float pv[12] = {
            -0.8f, -0.5f, -2.0f,   0.8f, -0.5f, -2.0f,
             0.8f,  0.5f, -2.0f,  -0.8f,  0.5f, -2.0f
        };
        static const float pc[12] = {
            1, 0, 0,   0, 1, 0,   0, 0, 1,   1, 1, 0
        };
        static const unsigned int idx[6] = { 0, 1, 2, 0, 2, 3 };
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glVertexPointer(3, GL_FLOAT, 0, pv);
        glColorPointer(3, GL_FLOAT, 0, pc);
        glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, idx);
        glDisableClientState(GL_COLOR_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
    }

    /* (c) cortina translucida SEM gravar profundidade */
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 1.0f, 1.0f, 0.5f);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.525f, -0.3f, -1.5f);
    glVertex3f(-0.075f, -0.3f, -1.5f);
    glVertex3f(-0.075f,  0.3f, -1.5f);
    glVertex3f(-0.525f, -0.3f, -1.5f);
    glVertex3f(-0.075f,  0.3f, -1.5f);
    glVertex3f(-0.525f,  0.3f, -1.5f);
    glEnd();
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);

    /* marcador ATRAS da cortina (z=-1.75): so' aparece com depth mask off */
    glColor3f(1.0f, 1.0f, 1.0f);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.4375f, -0.175f, -1.75f);
    glVertex3f(-0.2625f, -0.175f, -1.75f);
    glVertex3f(-0.2625f,  0.175f, -1.75f);
    glVertex3f(-0.4375f, -0.175f, -1.75f);
    glVertex3f(-0.2625f,  0.175f, -1.75f);
    glVertex3f(-0.4375f,  0.175f, -1.75f);
    glEnd();

    /* painel de textura procedural em coordenadas de tela (orto) */
    {
        static const float quad[16] = {
            220, 170,  0, 0,   284, 170,  1, 0,
            284, 234,  1, 1,   220, 234,  0, 1
        };
        glMatrixMode(GL_PROJECTION);
        glPushMatrix();
        glLoadIdentity();
        glOrtho(0.0, 320.0, 0.0, 240.0, -1.0, 1.0);
        glMatrixMode(GL_MODELVIEW);
        glPushMatrix();
        glLoadIdentity();

        GLuint t = 0;
        glGenTextures(1, &t);
        glBindTexture(GL_TEXTURE_2D, t);
        glTexImage2D(GL_TEXTURE_2D, 0, 3, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE,
                     tex);
        glEnable(GL_TEXTURE_2D);
        glColor3f(1.0f, 1.0f, 1.0f);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(3, GL_FLOAT, 16, quad);
        glTexCoordPointer(2, GL_FLOAT, 16, quad + 2);
        {
            static const unsigned int qidx[6] = { 0, 1, 2, 0, 2, 3 };
            glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, qidx);
        }
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        glDisable(GL_TEXTURE_2D);
        glDeleteTextures(1, &t);

        glPopMatrix();
        glMatrixMode(GL_PROJECTION);
        glPopMatrix();
        glMatrixMode(GL_MODELVIEW);
    }

    glFinish();
    if (glGetError() != GL_NO_ERROR) return 25;

    if (!px_is(100, 110, 51, 51, 51)) return 30;        /* assoalho solido */
    if (!px_is(185, 110, 0, 0, 255)) return 31;         /* faceta azul (flat) */
    if (!px_is(170, 140, 255, 255, 0)) return 32;       /* faceta amarela visivel fora da cortina */
    {   /* cortina 50% ciano sobre amarelo = (128±1, 255, 128±1) */
        unsigned char r, g, b;
        read_px(130, 125, &r, &g, &b);
        if ((r != 127 && r != 128) || g != 255 || (b != 127 && b != 128))
            return 33;
    }
    if (!px_is(140, 120, 255, 255, 255)) return 34;     /* marcador atrás */
    if (!px_is(224, 174, 32, 32, 159)) return 35;       /* texel (0,0) */
    if (!px_is(256, 190, 104, 159, 32)) return 36;      /* texel (4,2) */
    if (!px_is(240, 214, 95, 96, 96)) return 37;        /* texel (2,5) */
    if (!px_is(280, 230, 47, 64, 128)) return 38;       /* texel (7,7) */
    if (!px_is(300, 220, 0, 0, 0)) return 39;           /* fundo */

    (void)wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(NULL, dc);
    return 42;
}
