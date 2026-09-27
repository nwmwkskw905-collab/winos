/* Portico Runtime — PE de teste do projeto ("olá, Windows"): payloads PE32
 * mínimos e CONTROLADOS que provam a cadeia real
 * PE → CPU → memória → imports → Win32 → processo → saída.
 *
 * Variantes:
 *  0 = hello:  GetTickCount64() + ExitProcess(42)
 *  1 = gdi:    CreateCompatibleDC + CreateSolidBrush + SelectObject + PatBlt
 *              (retângulo vermelho na superfície) + ExitProcess(0)
 *  6 = visual: PE32+ x64 — bitmap GDI (fundo branco + retângulo vermelho +
 *              retângulo azul) + BitBlt SRCCOPY p/ a superfície do processo +
 *              ExitProcess(0). Primeiro marco visual do pipeline completo
 *              PE→CPU x64→Win32→GDI→Bitmap→BitBlt→GfxFrame→Metal. */
#ifndef PORTICO_PR_WINHELLO_H
#define PORTICO_PR_WINHELLO_H

#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Constrói o PE em buffer malloc (liberar com free). */
pr_status pr_winhello_build(int variant, void** out_data, size_t* out_len);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_WINHELLO_H */
