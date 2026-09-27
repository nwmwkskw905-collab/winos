/* hello_gl11.c — PE Windows REAL (MinGW-w64), GRUPO 18: galeria em 2 fases,
 * um passo além do hello_gl10 — mais composição (parede, duas caixas, tapete,
 * sombras, HUD, marcador) e um TAPETE COPLANAR ao assoalho.
 *
 * O tapete e' um decal de verdade: e' literalmente o MESMO triangulo do
 * assoalho (mesmos vertices, mesma profundidade bit a bit) redesenhado com
 * cor solida, como um tapete deitado sobre a tábua. Com o teste de
 * profundidade padrao (GL_LESS) o fragmento de profundidade igual seria
 * descartado e o tapete nao apareceria — todo engine gráfico da epoca
 * configura glDepthFunc(GL_LEQUAL) para decals sobre superficies. O
 * glDepthFunc e' a configuracao natural de profundidade que esta cena
 * exige; nao ha chamada artificial: o tapete e' elemento da galeria.
 *
 * Fase 1 (montagem): parede de fundo (color array + glDrawElements
 *   GL_UNSIGNED_INT, gradiente amarelo->azul), assoalho texturizado
 *   (2x1 ciano|amarelo, vertex+texcoord arrays + GL_UNSIGNED_SHORT),
 *   tapete magenta coplanar (metade do assoalho), sombra translucida
 *   (blend 50%), caixa A iluminada verde (vertex+normal arrays + INT),
 *   caixa B iluminada azul (SHORT), HUD (ortho+blend) e marcador branco
 *   (color array + glDrawArrays).
 * Transicao: o revestimento do assoalho e' trocado com ciclo real de
 *   recursos (glDeleteTextures do tex1 -> glGenTextures do tex2), como em
 *   qualquer troca de fase que descarrega e recarrega recursos.
 * Fase 2 (reorganizacao): tapete muda para a OUTRA metade (azul), assoalho
 *   com o revestimento novo (2x1 amarelo|branco), caixa A reposicionada
 *   (vermelha) sobre a area do tapete (depth), caixa B verde, HUD e
 *   marcador de fase 2. Teardown com glDeleteTextures do tex2.
 *
 * Geometria deterministica (glFrustum(-1.6,1.6,-1.2,1.2,1,20), vp 320x240):
 *   x_gl=(x_olho/(1.6*|z|)+1)*160, y_gl=(y_olho/(1.2*|z|)+1)*120.
 *   assoalho z=-4: x_gl 80..240, y_gl 80..160 (diagonal A-C divide T1/T2);
 *   parede  z=-5: x_gl 40..280, y_gl 60..180;
 *   caixa A z=-2: x_gl 80..160, y_gl 90..150 (fase 2: 150..230, 95..155);
 *   caixa B z=-2.5: x_gl 220..300, y_gl 90..150;
 *   sombra z=-2.5: x_gl 100..200, y_gl 75..92; HUD 20..70 x 20..50;
 *   marcador 250..300 x 20..40.
 * glReadPixels por assert (y para CIMA); 2x SwapBuffers; exit 42 no caminho
 * correto; exits 20..22 contexto, 30 glGetError fase 1, 31..41 fase 1,
 * 50 glGetError fase 2, 51..61 fase 2.
 */
#include <windows.h>
#include <GL/gl.h>

#define WIN_W 320
#define WIN_H 240

/* revestimento do assoalho — fase 1 (2x1): ciano | amarelo */
static unsigned char tex1_img[6] = { 0, 255, 255, 255, 255, 0 };
/* revestimento do assoalho — fase 2 (2x1): amarelo | branco */
static unsigned char tex2_img[6] = { 255, 255, 0, 255, 255, 255 };

/* assoalho (z=-4): x_gl 80..240, y_gl 80..160.
 * T1 = {0,1,2} = metade inferior-direita (recebe o tapete da fase 1);
 * T2 = {0,2,3} = metade superior-esquerda (tapete da fase 2). */
static const float floor_verts[12] = {
    -3.2f, -1.6f, -4.0f,   3.2f, -1.6f, -4.0f,
     3.2f,  1.6f, -4.0f,  -3.2f,  1.6f, -4.0f
};
static const float floor_uvs[8] = { 0, 0,  1, 0,  1, 1,  0, 1 };
static const unsigned short floor_idx[6] = { 0, 1, 2, 0, 2, 3 };
/* o tapete reusa o MESMO triangulo (indices 0..2 = T1 na fase 1;
 * 0,2,3 = T2 na fase 2) — profundidade exatamente igual (decal coplanar) */
static const unsigned short rug_t1_idx[3] = { 0, 1, 2 };
static const unsigned short rug_t2_idx[3] = { 0, 2, 3 };

/* parede de fundo (z=-5): x_gl 40..280, y_gl 60..180; gradiente vertical
 * amarelo (base) -> azul (topo) via color array + GL_UNSIGNED_INT */
static const float wall_verts[12] = {
    -6.0f, -3.0f, -5.0f,   6.0f, -3.0f, -5.0f,
     6.0f,  3.0f, -5.0f,  -6.0f,  3.0f, -5.0f
};
static const float wall_cols[12] = {
    1, 1, 0,   1, 1, 0,   0, 0, 1,   0, 0, 1
};
static const unsigned int wall_idx[6] = { 0, 1, 2, 0, 2, 3 };

/* caixa A fase 1 (z=-2): x_gl 80..160, y_gl 90..150 (vertex+normal+INT) */
static const float boxA_verts[12] = {
    -1.6f, -0.6f, -2.0f,   0.0f, -0.6f, -2.0f,
     0.0f,  0.6f, -2.0f,  -1.6f,  0.6f, -2.0f
};
/* caixa A fase 2 (z=-2): x_gl 150..230, y_gl 95..155 */
static const float boxA2_verts[12] = {
    -0.2f, -0.5f, -2.0f,   1.4f, -0.5f, -2.0f,
     1.4f,  0.7f, -2.0f,  -0.2f,  0.7f, -2.0f
};
/* caixa B (z=-2.5): x_gl 220..300, y_gl 90..150 (vertex+normal+SHORT) */
static const float boxB_verts[12] = {
    1.5f, -0.75f, -2.5f,   3.5f, -0.75f, -2.5f,
    3.5f,  0.75f, -2.5f,   1.5f,  0.75f, -2.5f
};
static const float box_norms[12] = {
    0, 0, 1,  0, 0, 1,  0, 0, 1,  0, 0, 1
};
static const unsigned int   box_idx_i[6] = { 0, 1, 2, 0, 2, 3 };
static const unsigned short box_idx_s[6] = { 0, 1, 2, 0, 2, 3 };

/* marcador de fase (color array + glDrawArrays): x_gl 250..300, y_gl 20..40 */
static const float mark_verts[18] = {
    250, 20, 0,  300, 20, 0,  300, 40, 0,
    250, 20, 0,  300, 40, 0,  250, 40, 0
};
static float mark_cols[18];   /* preenchido por fase (branco -> vermelho) */

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

static void draw_wall(void) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, wall_verts);
    glColorPointer(3, GL_FLOAT, 0, wall_cols);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, wall_idx);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_floor_tex(void) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, floor_verts);
    glTexCoordPointer(2, GL_FLOAT, 0, floor_uvs);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, floor_idx);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_rug(const unsigned short* tri, float r, float g, float b) {
    /* decal coplanar: o MESMO triangulo do assoalho com cor solida */
    glColor3f(r, g, b);
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, floor_verts);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, tri);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_box(const float* verts, const unsigned int* idx) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, verts);
    glNormalPointer(GL_FLOAT, 0, box_norms);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, idx);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_box_s(const float* verts) {
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_NORMAL_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, verts);
    glNormalPointer(GL_FLOAT, 0, box_norms);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, box_idx_s);
    glDisableClientState(GL_NORMAL_ARRAY);
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
    /* sombra translucida (blend 50% preto) em z=-2.5: x_gl 100..200,
     * y_gl 75..92 */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.0f, 0.0f, 0.0f, 0.5f);
    glBegin(GL_TRIANGLES);
    glVertex3f(-1.5f, -1.125f, -2.5f);
    glVertex3f( 1.0f, -1.125f, -2.5f);
    glVertex3f( 1.0f, -0.7f, -2.5f);
    glVertex3f(-1.5f, -1.125f, -2.5f);
    glVertex3f( 1.0f, -0.7f, -2.5f);
    glVertex3f(-1.5f, -0.7f, -2.5f);
    glEnd();
    glDisable(GL_BLEND);
}

static void draw_hud(float r, float g, float b) {
    hud_begin();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, 0.5f);
    glBegin(GL_TRIANGLES);
    glVertex3f(20.0f, 20.0f, 0.0f);
    glVertex3f(70.0f, 20.0f, 0.0f);
    glVertex3f(70.0f, 50.0f, 0.0f);
    glVertex3f(20.0f, 20.0f, 0.0f);
    glVertex3f(70.0f, 50.0f, 0.0f);
    glVertex3f(20.0f, 50.0f, 0.0f);
    glEnd();
    glDisable(GL_BLEND);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, mark_verts);
    glColorPointer(3, GL_FLOAT, 0, mark_cols);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    hud_end();
}

static void draw_box_lit(const float* verts, const unsigned int* idx,
                         float mr, float mg, float mb) {
    static const float mamb[4] = { 0, 0, 0, 1 };
    float mdif[4] = { mr, mg, mb, 1 };
    glMaterialfv(GL_FRONT, GL_AMBIENT, mamb);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, mdif);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    draw_box(verts, idx);
    glDisable(GL_LIGHTING);
}

static void draw_box_lit_s(const float* verts,
                           float mr, float mg, float mb) {
    static const float mamb[4] = { 0, 0, 0, 1 };
    float mdif[4] = { mr, mg, mb, 1 };
    glMaterialfv(GL_FRONT, GL_AMBIENT, mamb);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, mdif);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    draw_box_s(verts);
    glDisable(GL_LIGHTING);
}

int main(void) {
    /* contexto */
    HDC dc = GetDC(NULL);
    if (!dc) return 20;
    HGLRC rc = wglCreateContext(dc);
    if (!rc) return 21;
    if (!wglMakeCurrent(dc, rc)) return 22;

    /* viewport + projecao + estado base de profundidade */
    glViewport(0, 0, WIN_W, WIN_H);
    glClearDepth(1.0);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    /* o tapete e' coplanar ao assoalho (mesma profundidade): o teste de
     * profundidade deve aceitar igualdade para o decal aparecer */
    glDepthFunc(GL_LEQUAL);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.6, 1.6, -1.2, 1.2, 1.0, 20.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* luz0: difusa branca, sem especular (quadro comprovado do G15) */
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

    /* ================= FASE 1 (montagem da galeria) ================= */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    set_mark(1.0f, 1.0f, 1.0f);           /* marcador branco = fase 1 */

    draw_wall();                          /* parede (longe) */

    glGenTextures(1, &tex1);
    glBindTexture(GL_TEXTURE_2D, tex1);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 1, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 tex1_img);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0f, 1.0f, 1.0f);
    draw_floor_tex();                     /* assoalho (ciano|amarelo) */
    glDisable(GL_TEXTURE_2D);

    draw_rug(rug_t1_idx, 1.0f, 0.0f, 1.0f);   /* TAPETE coplanar (magenta) */
    draw_shadow();
    draw_box_lit(boxA_verts, box_idx_i, 0.0f, 1.0f, 0.0f);   /* verde */
    draw_box_lit_s(boxB_verts, 0.0f, 0.0f, 1.0f);            /* azul */
    draw_hud(0.0f, 1.0f, 1.0f);           /* HUD ciano 50% */
    glFinish();
    if (glGetError() != GL_NO_ERROR) return 30;

    /* tapete (T1 redesenhado) cobre o assoalho na metade inferior-direita;
     * sonda (235,85): interior de T1 livre de occlusao (a antiga (220,100)
     * caia no 1o pixel da caixa B [220..300]x[90..150], desenhada depois e
     * mais perto — o tapete nunca aparecia la) */
    if (!px_is(235, 85, 255, 0, 255)) return 31;   /* tapete magenta */
    /* sonda (110,155): T2 livre (a antiga (100,140) caia dentro da caixa A
     * [80..160]x[90..150], verde, mais perto — nunca mostrava o assoalho) */
    if (!px_is(110, 155, 0, 255, 255)) return 32;   /* assoalho texel ciano */
    if (!px_is(165, 148, 255, 255, 0)) return 33;   /* assoalho texel amarelo */
    if (!px_is(130, 110, 0, 255, 0)) return 34;     /* caixa A verde + depth */
    if (!px_is(255, 120, 0, 0, 255)) return 35;     /* caixa B azul */
    {   /* HUD ciano 50% sobre preto = (0,128±1,128±1) */
        unsigned char r, g, b;
        read_px(45, 45, &r, &g, &b);
        if (r != 0 || (g != 127 && g != 128) || (b != 127 && b != 128))
            return 36;
    }
    {   /* sombra 50% preto sobre o tapete magenta = (128±1,0,128±1) */
        unsigned char r, g, b;
        read_px(150, 85, &r, &g, &b);
        if ((r != 127 && r != 128) || g != 0 ||
            (b != 127 && b != 128))
            return 37;
    }
    if (!px_is(275, 30, 255, 255, 255)) return 38;   /* marcador branco */
    /* parede: gradiente vertical amostrado no CENTRO do pixel (50.5,90.5):
     * t=(90.5-60)/120 -> f2b = (190,190,65). O valor antigo (191,191,64)
     * era o da amostra no canto inteiro — nao existe em pixel algum. */
    if (!px_is(50, 90, 190, 190, 65)) return 39;
    /* parede em (265,165), centro (265.5,165.5): t=105.5/120 ->
     * f2b = (31,31,224) (valor antigo (32,32,223) = amostra no canto) */
    if (!px_is(265, 165, 31, 31, 224)) return 40;
    if (!px_is(300, 200, 0, 0, 0)) return 41;        /* fundo */

    SwapBuffers(dc);

    /* ============ TRANSICAO DE FASE ============
     * troca de revestimento com ciclo real de recursos */
    glDeleteTextures(1, &tex1);

    /* ================= FASE 2 (reorganizacao) ================= */
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glLoadIdentity();
    set_mark(1.0f, 0.0f, 0.0f);           /* marcador vermelho = fase 2 */

    draw_wall();

    glGenTextures(1, &tex2);
    glBindTexture(GL_TEXTURE_2D, tex2);
    glTexImage2D(GL_TEXTURE_2D, 0, 3, 2, 1, 0, GL_RGB, GL_UNSIGNED_BYTE,
                 tex2_img);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1.0f, 1.0f, 1.0f);
    draw_floor_tex();                     /* assoalho (amarelo|branco) */
    glDisable(GL_TEXTURE_2D);

    draw_rug(rug_t2_idx, 0.0f, 0.0f, 1.0f);   /* TAPETE na outra metade (azul) */
    draw_shadow();
    draw_box_lit(boxA2_verts, box_idx_i, 1.0f, 0.0f, 0.0f);  /* vermelha */
    draw_box_lit_s(boxB_verts, 0.0f, 1.0f, 0.0f);            /* verde */
    draw_hud(1.0f, 1.0f, 0.0f);           /* HUD amarelo 50% */
    glFinish();
    if (glGetError() != GL_NO_ERROR) return 50;

    /* sonda (235,85): T1 livre (a antiga (220,100) caia no 1o pixel da
     * caixa B, verde na fase 2 — mesma causa do assert 31) */
    if (!px_is(235, 85, 255, 255, 255)) return 51;  /* assoalho texel branco */
    if (!px_is(100, 140, 0, 0, 255)) return 52;      /* tapete azul (T2) */
    /* sonda (140,145): T2 livre (a antiga (165,148) caia dentro da caixa
     * A' [150..230]x[95..155], vermelha, mais perto) */
    if (!px_is(140, 145, 0, 0, 255)) return 53;      /* tapete azul (T2) */
    if (!px_is(190, 125, 255, 0, 0)) return 54;      /* caixa A' vermelha */
    if (!px_is(255, 120, 0, 255, 0)) return 55;      /* caixa B verde */
    {   /* HUD amarelo 50% sobre preto = (128±1,128±1,0) */
        unsigned char r, g, b;
        read_px(45, 45, &r, &g, &b);
        if ((r != 127 && r != 128) || (g != 127 && g != 128) || b != 0)
            return 56;
    }
    {   /* sombra 50% preto sobre texel amarelo = (128±1,128±1,0) */
        unsigned char r, g, b;
        read_px(150, 85, &r, &g, &b);
        if ((r != 127 && r != 128) || (g != 127 && g != 128) || b != 0)
            return 57;
    }
    if (!px_is(275, 30, 255, 0, 0)) return 58;       /* marcador vermelho */
    if (!px_is(50, 90, 190, 190, 65)) return 59;     /* parede (estavel) */
    if (!px_is(265, 165, 31, 31, 224)) return 60;
    if (!px_is(300, 200, 0, 0, 0)) return 61;

    SwapBuffers(dc);

    /* teardown: libera o revestimento da fase 2 */
    glDeleteTextures(1, &tex2);

    (void)wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(NULL, dc);
    return 42;
}
