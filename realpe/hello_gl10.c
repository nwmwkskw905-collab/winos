/* hello_gl10.c — PE Windows REAL (MinGW-w64), GRUPO 17: pequeno programa de
 * cena em 2 fases com ciclo completo de objetos de textura.
 *
 * A cena é um quadro de jogo simples e determinístico (320x240):
 *   Fase 1: caixa iluminada (malha: vertex+normal arrays + glDrawElements
 *     GL_UNSIGNED_INT), piso texturizado (vertex+texcoord arrays +
 *     glDrawElements GL_UNSIGNED_SHORT, textura 2x1 azul|branco), sombra
 *     translucida (blend), HUD (ortho + blend) e marcador de fase (color
 *     array + glDrawArrays). O piso e' desenhado DEPOIS da caixa (mais
 *     longe): na sobreposicao o depth real preserva a caixa.
 *   Transicao de fase: a textura da fase 1 e' LIBERADA (ciclo de vida real
 *     de objetos de textura) antes de carregar a da fase 2 — como em
 *     qualquer troca de fase que descarrega recursos antigos.
 *   Fase 2: o piso residual aparece com a cor-base (o recurso foi liberado),
 *     a nova textura (2x1 verde|amarelo) entra no piso novo, a caixa e o HUD
 *     seguem, e o marcador muda de cor pela color array (fase 2 = ciano).
 *
 * Geometria deterministica (glFrustum(-1.6,1.6,-1.2,1.2,1,20), vp 320x240):
 *   x_ndc = x_olho/(1.6*|z|), y_ndc = y_olho/(1.2*|z|); x_gl=(x_ndc+1)*160,
 *   y_gl=(y_ndc+1)*120. Cada assert de pixel deriva dessas contas.
 * glReadPixels por assert (y para CIMA); 2x SwapBuffers; exit 42 no caminho
 * correto; exits 20..22 contexto, 30..37 fase 1, 50..57 fase 2.
 */
#include <windows.h>
#include <GL/gl.h>

#define WIN_W 320
#define WIN_H 240

/* textura da fase 1 (2x1): texel 0 azul, texel 1 branco */
static unsigned char tex1_img[6] = { 0, 0, 255, 255, 255, 255 };
/* textura da fase 2 (2x1): texel 0 verde, texel 1 amarelo */
static unsigned char tex2_img[6] = { 0, 255, 0, 255, 255, 0 };

/* malha da caixa (4 vertices, 2 triangulos, GL_UNSIGNED_INT) — z=-2,
 * x_gl 80..160, y_gl 90..150 */
static const float box_verts[12] = {
    -1.6f, -0.6f, -2.0f,   0.0f, -0.6f, -2.0f,
     0.0f,  0.6f, -2.0f,  -1.6f,  0.6f, -2.0f
};
static const float box_norms[12] = {
    0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1
};
static const unsigned int box_idx[6] = { 0, 1, 2, 0, 2, 3 };

/* piso da fase 1 / residual (z=-4): x_gl 100..220, y_gl 70..130 */
static const float floor_verts[12] = {
    -2.4f, -2.0f, -4.0f,   2.4f, -2.0f, -4.0f,
     2.4f,  0.4f, -4.0f,  -2.4f,  0.4f, -4.0f
};
static const float floor_uvs[8] = { 0, 0,  1, 0,  1, 1,  0, 1 };
static const unsigned short floor_idx[6] = { 0, 1, 2, 0, 2, 3 };

/* piso novo da fase 2 (z=-3): x_gl 230..310, y_gl 70..130 */
static const float floor2_verts[12] = {
     2.1f, -1.5f, -3.0f,   4.5f, -1.5f, -3.0f,
     4.5f,  0.3f, -3.0f,   2.1f,  0.3f, -3.0f
};
static const float floor2_uvs[8] = { 0, 0,  1, 0,  1, 1,  0, 1 };
static const unsigned short floor2_idx[6] = { 0, 1, 2, 0, 2, 3 };

/* marcador de fase no HUD (color array + glDrawArrays): x_gl 250..300,
 * y_gl 20..40 */
static const float mark_verts[18] = {
    250, 20, 0,  300, 20, 0,  300, 40, 0,
    250, 20, 0,  300, 40, 0,  250, 40, 0
};
static float mark_cols[18];   /* preenchido por fase (verde -> ciano) */

static void set_mark(float r, float g, float b) {
    for (int i = 0; i < 6; i++) {
        mark_cols[i * 3 + 0] = r;
        mark_cols[i * 3 + 1] = g;
        mark_cols[i * 3 + 2] = b;
    }
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

static void draw_box(void) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, box_verts);
    glNormalPointer(GL_FLOAT, 0, box_norms);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, box_idx);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_floor_mesh(const float* verts, const float* uvs,
                            const unsigned short* idx) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, verts);
    glTexCoordPointer(2, GL_FLOAT, 0, uvs);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, idx);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_marker(void) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, mark_verts);
    glColorPointer(3, GL_FLOAT, 0, mark_cols);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void hud_begin(void) {
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, 320.0, 0.0, 240.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();
}

static void hud_end(void) {
    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
}

static void draw_shadow(void) {
    /* sombra translucida (blend 50% preto) em z=-2.5: x_gl 130..170,
     * y_gl 72..88 */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
    glBegin(GL_TRIANGLES);
    glVertex3f(-0.75f, -1.2f, -2.5f);
    glVertex3f( 0.25f, -1.2f, -2.5f);
    glVertex3f( 0.25f, -0.8f, -2.5f);
    glVertex3f(-0.75f, -1.2f, -2.5f);
    glVertex3f( 0.25f, -0.8f, -2.5f);
    glVertex3f(-0.75f, -0.8f, -2.5f);
    glEnd();
    glDisable(GL_BLEND);
}

static void draw_hud(void) {
    hud_begin();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1.0f, 1.0f, 0.0f, 0.5f);
    glBegin(GL_TRIANGLES);
    glVertex3f(20.0f, 20.0f, 0.0f);
    glVertex3f(70.0f, 20.0f, 0.0f);
    glVertex3f(70.0f, 70.0f, 0.0f);
    glVertex3f(20.0f, 20.0f, 0.0f);
    glVertex3f(70.0f, 70.0f, 0.0f);
    glVertex3f(20.0f, 70.0f, 0.0f);
    glEnd();
    glDisable(GL_BLEND);
    draw_marker();
    hud_end();
}

static void draw_box_lit(void) {
    static const float mamb[4] = { 0, 0, 0, 1 };
    static const float mdif[4] = { 1, 0, 0, 1 };
    glMaterialfv(GL_FRONT, GL_AMBIENT, mamb);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, mdif);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    draw_box();
    glDisable(GL_LIGHTING);
}

int main(void) {
    /* contexto */
    HDC dc = GetDC(NULL);
    if (!dc) return 20;
    HGLRC rc = wglCreateContext(dc);
    if (!rc) return 21;
    if (!wglMakeCurrent(dc, rc)) return 22;

    /* viewport + projecao + estado base */
    glViewport(0, 0, WIN_W, WIN_H);
    glClearDepth(1.0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.6, 1.6, -1.2, 1.2, 1.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* luz0: difusa branca, sem especular (mesmo quadro comprovado do G15) */
    {
        static const float z[4] = { 0, 0, 0, 1 };
        static const float wd[4] = { 1, 1, 1, 1 };
        static const float pos[4] = { 0, 0, 1, 0 };
        glLightfv(GL_LIGHT0, GL_AMBIENT, z);
        glLightfv(GL_LIGHT0, GL_DIFFUSE, wd);
        glLightfv(GL_LIGHT0, GL_SPECULAR, z);
        glLightfv(GL_LIGHT0, GL_POSITION, pos);
    }

    GLuint tex1 = 0, tex2 = 0;

    /* ================= FASE 1 ================= */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    set_mark(0.0f, 1.0f, 0.0f);           /* marcador verde = fase 1 */

    /* caixa iluminada (perto) — primeiro */
    glLoadIdentity();
    draw_box_lit();

    /* piso texturizado (longe) — depois: o depth preserva a caixa */
    glGenTextures(1, &tex1);
    glBindTexture(GL_TEXTURE_2D, tex1);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 1, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 tex1_img);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0f, 1.0f, 1.0f);
    draw_floor_mesh(floor_verts, floor_uvs, floor_idx);
    glDisable(GL_TEXTURE_2D);

    draw_shadow();
    draw_hud();
    glFinish();
    if (glGetError() != GL_NO_ERROR) return 30;

    if (!px_is(130, 110, 255, 0, 0)) return 31;      /* caixa + depth */
    if (!px_is(215, 100, 255, 255, 255)) return 32;  /* piso texel branco */
    if (!px_is(115, 80, 0, 0, 255)) return 33;       /* piso texel azul */
    {   /* sombra: blend 50% preto sobre azul = (0,0,128±1) */
        unsigned char r, g, b;
        read_px(150, 80, &r, &g, &b);
        if (r != 0 || g != 0 || (b != 127 && b != 128)) return 34;
    }
    {   /* HUD: amarelo 50% sobre preto = (128±1,128±1,0) */
        unsigned char r, g, b;
        read_px(45, 45, &r, &g, &b);
        if ((r != 127 && r != 128) || (g != 127 && g != 128) || b != 0)
            return 35;
    }
    if (!px_is(275, 30, 0, 255, 0)) return 36;       /* marcador (c. array) */
    if (!px_is(300, 200, 0, 0, 0)) return 37;        /* fundo */

    SwapBuffers(dc);

    /* ============ TRANSICAO DE FASE ============
     * descarrega os recursos da fase 1 antes de carregar a fase 2 */
    glDeleteTextures(1, &tex1);

    /* ================= FASE 2 ================= */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    set_mark(0.0f, 1.0f, 1.0f);           /* marcador ciano = fase 2 */

    /* piso residual: a textura da fase 1 foi LIBERADA — desenhado com a
     * cor-base (sem objeto de textura) */
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0f, 1.0f, 1.0f);
    draw_floor_mesh(floor_verts, floor_uvs, floor_idx);

    /* carrega os recursos da fase 2 */
    glGenTextures(1, &tex2);
    glBindTexture(GL_TEXTURE_2D, tex2);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 1, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 tex2_img);

    /* piso novo da fase 2 (textura nova: verde|amarelo) */
    draw_floor_mesh(floor2_verts, floor2_uvs, floor2_idx);
    glDisable(GL_TEXTURE_2D);

    glLoadIdentity();
    draw_box_lit();
    draw_hud();
    glFinish();
    if (glGetError() != GL_NO_ERROR) return 50;

    if (!px_is(115, 80, 255, 255, 255)) return 51;   /* residual sem textura */
    if (!px_is(245, 80, 0, 255, 0)) return 52;       /* tex2: verde */
    if (!px_is(300, 110, 255, 255, 0)) return 53;    /* tex2: amarelo */
    if (!px_is(130, 110, 255, 0, 0)) return 54;      /* caixa iluminada */
    {
        unsigned char r, g, b;
        read_px(45, 45, &r, &g, &b);
        if ((r != 127 && r != 128) || (g != 127 && g != 128) || b != 0)
            return 55;
    }
    if (!px_is(275, 30, 0, 255, 255)) return 56;     /* marcador fase 2 */
    if (!px_is(300, 200, 0, 0, 0)) return 57;        /* fundo */

    SwapBuffers(dc);

    /* teardown: libera a textura da fase 2 */
    glDeleteTextures(1, &tex2);
    return 42;
}
