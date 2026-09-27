/* pr_gl.h — subconjunto REAL do OpenGL 1.1 por software (GRUPO 8).
 *
 * SOMENTE as 15 funções opengl32 + SwapBuffers exercitadas pelo PE de
 * controle real (realpe/hello_gl.c), com rasterização/z-buffer reais.
 * Todo o restante do OpenGL permanece NÃO implementado (EXECUTION
 * STOPPED honesto). Sem cópia de código proprietário: implementação
 * própria a partir da especificação pública do GL 1.1 (ABI/constantes). */
#ifndef PORTICO_PR_GL_H
#define PORTICO_PR_GL_H

#include "pr_types.h"

struct pr_win32_ctx;
struct pr_surf;
typedef struct pr_gl_state pr_gl_state;

/* ciclo de vida (1 estado por pr_win32_ctx; liberado no destroy) */
pr_gl_state* pr_gl_new(struct pr_win32_ctx* owner);
void         pr_gl_release(struct pr_win32_ctx* owner);

/* wgl (contexto atual por estado — thread única documentada) */
uint32_t pr_gl_wgl_create(pr_gl_state* s, uint32_t hdc);
int      pr_gl_wgl_make_current(pr_gl_state* s, uint32_t hdc, uint32_t hglrc);
int      pr_gl_wgl_delete(pr_gl_state* s, uint32_t hglrc);

/* apresentação: copia o framebuffer (flip vertical) para a superfície
 * XRGB8888 do caminho GDI→SurfaceBridge→BGRA8→Metal (sem 2º renderer) */
int pr_gl_swap_buffers(pr_gl_state* s, struct pr_surf* target);

/* GL 1.1 (subconjunto exercitado pelo PE) */
void     pr_gl_viewport(pr_gl_state* s, int x, int y, int w, int h);
void     pr_gl_clear_color(pr_gl_state* s, float r, float g, float b, float a);
void     pr_gl_clear_depth(pr_gl_state* s, double d);
void     pr_gl_enable(pr_gl_state* s, unsigned cap);
void     pr_gl_depth_func(pr_gl_state* s, unsigned func);
void     pr_gl_depth_mask(pr_gl_state* s, int enabled);
void     pr_gl_shade_model(pr_gl_state* s, unsigned mode);
void     pr_gl_clear(pr_gl_state* s, unsigned mask);
void     pr_gl_begin(pr_gl_state* s, unsigned mode);
void     pr_gl_color3f(pr_gl_state* s, float r, float g, float b);
void     pr_gl_vertex3f(pr_gl_state* s, float x, float y, float z);
void     pr_gl_end(pr_gl_state* s);
void     pr_gl_finish(pr_gl_state* s);

/* ---- GRUPO 9: matrizes + vertex arrays (subconjunto do 2º PE) ---- */
void pr_gl_matrix_mode(pr_gl_state* s, unsigned mode);
void pr_gl_load_identity(pr_gl_state* s);
void pr_gl_ortho(pr_gl_state* s, double l, double r, double b, double t,
                 double n, double f);
void pr_gl_translatef(pr_gl_state* s, float x, float y, float z);
void pr_gl_rotatef(pr_gl_state* s, float angle_deg, float x, float y, float z);
void pr_gl_scalef(pr_gl_state* s, float x, float y, float z);
void pr_gl_enable_client_state(pr_gl_state* s, unsigned array);
void pr_gl_disable_client_state(pr_gl_state* s, unsigned array);
void pr_gl_vertex_pointer(pr_gl_state* s, int size, unsigned type, int stride,
                          uint64_t addr);
void pr_gl_color_pointer(pr_gl_state* s, int size, unsigned type, int stride,
                         uint64_t addr);
/* resolve: traduz endereço de convidado → host (validado) ou NULL */
typedef void* (*pr_gl_ptr_fn)(void* ud, uint64_t addr, size_t bytes);
void pr_gl_draw_elements(pr_gl_state* s, unsigned mode, int count,
                         unsigned type, uint64_t idx_addr,
                         pr_gl_ptr_fn resolve, void* ud);

/* ---- GRUPO 10: matrix stack + glFrustum + glDrawArrays + texturas ----
 * Subconjunto real: TEXTURE_2D level 0, GL_RGB/GL_UNSIGNED_BYTE,
 * GL_NEAREST + GL_REPEAT, MODULATE; pilhas MV 32 / PROJ 4. */
#define GL10_MAX_TEX 64   /* objetos 0..63 (0 = textura default) */
void pr_gl_push_matrix(pr_gl_state* s);
void pr_gl_pop_matrix(pr_gl_state* s);
void pr_gl_frustum(pr_gl_state* s, double l, double r, double b,
                   double t, double znear, double zfar);
void pr_gl_gen_textures(pr_gl_state* s, int n, uint32_t* names);
void pr_gl_delete_textures(pr_gl_state* s, int n, const uint32_t* names);
void pr_gl_bind_texture(pr_gl_state* s, unsigned target, uint32_t name);
void pr_gl_tex_parameteri(pr_gl_state* s, unsigned target, unsigned pname,
                          int param);
void pr_gl_tex_image2d(pr_gl_state* s, unsigned target, int level,
                       int internalformat, int width, int height,
                       int border, unsigned format, unsigned type,
                       uint64_t pixels_addr, pr_gl_ptr_fn resolve, void* ud);
void pr_gl_tex_coord_pointer(pr_gl_state* s, int size, unsigned type,
                             int stride, uint64_t addr);
void pr_gl_disable(pr_gl_state* s, unsigned cap);
void pr_gl_draw_arrays(pr_gl_state* s, unsigned mode, int first, int count,
                       pr_gl_ptr_fn resolve, void* ud);

/* ---- GRUPO 11: iluminação fixed-function + blending/alpha ----
 * Subconjunto real: 8 luzes (spot: fora), material GL_FRONT, Gouraud por
 * vértice (emissão+ambiente+difusa+especular Blinn), blend com fatores
 * GL_ZERO/GL_ONE/GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA. */
void pr_gl_normal3f(pr_gl_state* s, float x, float y, float z);
void pr_gl_tex_coord2f(pr_gl_state* s, float u, float v);
void pr_gl_normal_pointer(pr_gl_state* s, unsigned type, int stride,
                          uint64_t addr);
void pr_gl_color4f(pr_gl_state* s, float r, float g, float b, float a);
void pr_gl_light_fv(pr_gl_state* s, unsigned light, unsigned pname,
                    const float* v);
void pr_gl_material_fv(pr_gl_state* s, unsigned face, unsigned pname,
                       const float* v);
void pr_gl_blend_func(pr_gl_state* s, unsigned sfactor, unsigned dfactor);
unsigned pr_gl_get_error(pr_gl_state* s);
const char* pr_gl_get_string(pr_gl_state* s, unsigned name);
void pr_gl_get_integer_v(pr_gl_state* s, unsigned name, int* out);
void     pr_gl_read_pixels(pr_gl_state* s, int x, int y, int w, int h,
                           unsigned fmt, unsigned type, void* out);

#endif /* PORTICO_PR_GL_H */
