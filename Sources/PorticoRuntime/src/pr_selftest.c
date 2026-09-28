/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

/* Self-Test PXP: payload IA-32 (interpretado) que exercita o pipeline completo
 * (entrada → CPU → comandos gráficos → áudio → logs → present).
 *
 * ESTE NÃO É UM EXECUTÁVEL WINDOWS. É um payload nativo do formato PXP usado
 * para validar honestamente o runtime (e como "jogo de diagnóstico" no app).
 *
 * Mapa MMIO (base 0xF0000000, janela 0x4000):
 *   +0x000 GFX_OP (w): 1=clear 2=viewport 3=scissor 4=draw_tri 5=present 6=filter
 *   +0x004..0x010 GFX_A0..A3 (w); +0x014 GFX_SUBMIT (w)
 *     clear: A0..A3 = r,g,b,a (0..255); viewport/scissor: Q16.16;
 *     draw: A0=vertex_count A1=first_vertex; present: A0=w A1=h
 *   +0x018 VERTEX_X (w Q16.16), +0x01C VERTEX_Y, +0x020..0x028 R,G,B (0..255)
 *   +0x02C VERTEX_SUBMIT (w)
 *   +0x030 TARGET_W (r), +0x034 TARGET_H (r)
 *   +0x100 INPUT_BTN (r), +0x104..0x110 AX0..AX3 (r Q16.16), +0x114/0x118 triggers
 *   +0x200 TONE_FREQ (w Q16.16 Hz), +0x204 TONE_VOL (w 0..255), +0x208 TONE_WAVE (w)
 *   +0x300 TIME_LO/HI (r ms), +0x308 FRAME_IDX (r)
 *   +0x30C LOG_DATA (w byte), +0x310 LOG_FLUSH (w), +0x314 SYS_HALT (w), +0x318 SYS_RAND (r)
 */
#include "portico/pr_host.h"
#include "portico/pr_cpu.h"
#include "portico/pr_asm.h"

#include <stdlib.h>
#include <string.h>

#define PXP_LOAD_ADDR   0x00010000u
#define MMIO_BASE       0xF0000000u

#define OFF_GFX_OP      0x000
#define OFF_GFX_A0      0x004
#define OFF_GFX_A1      0x008
#define OFF_GFX_A2      0x00C
#define OFF_GFX_A3      0x010
#define OFF_GFX_SUBMIT  0x014
#define OFF_VTX_X       0x018
#define OFF_VTX_Y       0x01C
#define OFF_VTX_R       0x020
#define OFF_VTX_G       0x024
#define OFF_VTX_B       0x028
#define OFF_VTX_SUB     0x02C
#define OFF_INPUT_BTN   0x100
#define OFF_INPUT_AX0   0x104
#define OFF_SYS_HALT    0x314
#define OFF_TONE_FREQ   0x200
#define OFF_LOG_DATA    0x30C
#define OFF_LOG_FLUSH   0x310

#define PXP_HEADER_SIZE 24u

static void emit_str(pr_asm* a, const char* s) {
    while (*s) pr_asm_u8(a, (uint8_t)*s++);
    pr_asm_u8(a, 0);
}

pr_status pr_selftest_payload_build(void** out_data, size_t* out_len) {
    if (!out_data || !out_len) return PR_ERR_INVALID;

    pr_asm a;
    if (pr_asm_init(&a, 8192) != PR_OK) return PR_ERR_NOMEM;

    /* ---- strings (offsets capturados em tempo de build) ---- */
    size_t off_tick = pr_asm_pos(&a);
    emit_str(&a, "selftest: frame tick");
    size_t off_bye = pr_asm_pos(&a);
    emit_str(&a, "selftest: encerrando (START)");
    size_t off_start = pr_asm_pos(&a);
    emit_str(&a, "selftest: payload PXP iniciado");
    while (pr_asm_pos(&a) % 4 != 0) pr_asm_u8(&a, 0x90);
    size_t off_code = pr_asm_pos(&a);

    /* ---- código ----
     * EBP = frame, EBX = x (Q16.16), EDI = base MMIO, ESI = ptr de string. */
    pr_asm_mov_r_imm(&a, PR_REG_EBP, 0);
    pr_asm_mov_r_imm(&a, PR_REG_EBX, 0x0B000000u); /* x = 176.0 */
    pr_asm_mov_r_imm(&a, PR_REG_EDI, MMIO_BASE);

    /* print(s_start) */
    pr_asm_mov_r_imm(&a, PR_REG_ESI, (uint32_t)(PXP_LOAD_ADDR + off_start));
    size_t call_start = pr_asm_call32(&a);

    size_t loop_top = pr_asm_pos(&a);

    /* x += axis0 >> 3 */
    pr_asm_mov_r_m(&a, PR_REG_EAX, PR_REG_EDI, OFF_INPUT_AX0);
    pr_asm_sar1(&a, PR_REG_EAX);
    pr_asm_sar1(&a, PR_REG_EAX);
    pr_asm_sar1(&a, PR_REG_EAX);
    pr_asm_add_r_r(&a, PR_REG_EBX, PR_REG_EAX);

    /* clamp x ∈ [0, 544.0] */
    pr_asm_cmp_r_imm(&a, PR_REG_EBX, 0);
    size_t j_ge0 = pr_asm_jge_rel32(&a);
    pr_asm_mov_r_imm(&a, PR_REG_EBX, 0);
    pr_asm_patch_rel32(&a, j_ge0, pr_asm_pos(&a));

    pr_asm_cmp_r_imm(&a, PR_REG_EBX, 0x02200000u); /* 544.0 */
    size_t j_lehi = pr_asm_jle_rel32(&a);
    pr_asm_mov_r_imm(&a, PR_REG_EBX, 0x02200000u);
    pr_asm_patch_rel32(&a, j_lehi, pr_asm_pos(&a));

    /* START (bit 6) → log + halt */
    pr_asm_mov_r_m(&a, PR_REG_ECX, PR_REG_EDI, OFF_INPUT_BTN);
    pr_asm_and_r_imm(&a, PR_REG_ECX, 0x40);
    size_t j_nohalt = pr_asm_jz_rel32(&a);
    pr_asm_mov_r_imm(&a, PR_REG_ESI, (uint32_t)(PXP_LOAD_ADDR + off_bye));
    size_t call_bye = pr_asm_call32(&a);
    pr_asm_mov_r_imm(&a, PR_REG_ECX, 1);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_SYS_HALT, PR_REG_ECX);
    pr_asm_hlt(&a);
    pr_asm_patch_rel32(&a, j_nohalt, pr_asm_pos(&a));

    /* cores: EAX=r=(3*frame)&0xFF, ECX=g=255-r, EDX=b=(frame>>2)&0x7F */
    pr_asm_mov_r_r(&a, PR_REG_EAX, PR_REG_EBP);
    pr_asm_add_r_r(&a, PR_REG_EAX, PR_REG_EAX);
    pr_asm_add_r_r(&a, PR_REG_EAX, PR_REG_EBP);
    pr_asm_and_r_imm(&a, PR_REG_EAX, 0xFF);
    pr_asm_mov_r_imm(&a, PR_REG_ECX, 0xFF);
    pr_asm_sub_r_r(&a, PR_REG_ECX, PR_REG_EAX);
    pr_asm_mov_r_r(&a, PR_REG_EDX, PR_REG_EBP);
    pr_asm_shr1(&a, PR_REG_EDX);
    pr_asm_shr1(&a, PR_REG_EDX);
    pr_asm_and_r_imm(&a, PR_REG_EDX, 0x7F);

    /* CLEAR */
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_GFX_OP, 1);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_GFX_A0, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_GFX_A1, PR_REG_ECX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_GFX_A2, PR_REG_EDX);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_GFX_A3, 255);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_GFX_SUBMIT, 0);

    /* quad em (ebx, 80.0), lado 96.0 → 6 vértices (2 triângulos) */
    pr_asm_mov_r_r(&a, PR_REG_ESI, PR_REG_EBX);
    pr_asm_add_r_imm(&a, PR_REG_ESI, 0x06000000u); /* x2 = x + 96.0 */

    /* macro de vértice espelhada em chamadas: v0..v5 */
    /* v0(x, 80.0) */
    pr_asm_push_r(&a, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_X, PR_REG_EBX);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 0x05000000u); /* 80.0 */
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_Y, PR_REG_EAX);
    pr_asm_pop_r(&a, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_R, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_G, PR_REG_ECX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_B, PR_REG_EDX);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_VTX_SUB, 0);
    /* v1(x2, 80.0) */
    pr_asm_push_r(&a, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_X, PR_REG_ESI);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 0x05000000u);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_Y, PR_REG_EAX);
    pr_asm_pop_r(&a, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_R, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_G, PR_REG_ECX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_B, PR_REG_EDX);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_VTX_SUB, 0);
    /* v2(x2, 176.0) */
    pr_asm_push_r(&a, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_X, PR_REG_ESI);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 0x0B000000u); /* 176.0 */
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_Y, PR_REG_EAX);
    pr_asm_pop_r(&a, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_R, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_G, PR_REG_ECX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_B, PR_REG_EDX);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_VTX_SUB, 0);
    /* v3(x, 80.0) */
    pr_asm_push_r(&a, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_X, PR_REG_EBX);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 0x05000000u);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_Y, PR_REG_EAX);
    pr_asm_pop_r(&a, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_R, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_G, PR_REG_ECX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_B, PR_REG_EDX);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_VTX_SUB, 0);
    /* v4(x2, 176.0) */
    pr_asm_push_r(&a, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_X, PR_REG_ESI);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 0x0B000000u);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_Y, PR_REG_EAX);
    pr_asm_pop_r(&a, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_R, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_G, PR_REG_ECX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_B, PR_REG_EDX);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_VTX_SUB, 0);
    /* v5(x, 176.0) */
    pr_asm_push_r(&a, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_X, PR_REG_EBX);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 0x0B000000u);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_Y, PR_REG_EAX);
    pr_asm_pop_r(&a, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_R, PR_REG_EAX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_G, PR_REG_ECX);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_VTX_B, PR_REG_EDX);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_VTX_SUB, 0);

    /* DRAW (6 vértices a partir de 0) */
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_GFX_OP, 4);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_GFX_A0, 6);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_GFX_A1, 0);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_GFX_SUBMIT, 0);

    /* PRESENT 640×360 */
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_GFX_OP, 5);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_GFX_A0, 640);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_GFX_A1, 360);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_GFX_SUBMIT, 0);

    /* áudio: freq = 220 Hz + nº dos botões (Q16.16) */
    pr_asm_mov_r_m(&a, PR_REG_EAX, PR_REG_EDI, OFF_INPUT_BTN);
    pr_asm_and_r_imm(&a, PR_REG_EAX, 0xFFFF);
    for (int i = 0; i < 16; i++) pr_asm_shl1(&a, PR_REG_EAX); /* <<16 → +btn Hz */
    pr_asm_add_r_imm(&a, PR_REG_EAX, 0x00DC0000u); /* +220.0 Hz */
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_TONE_FREQ, PR_REG_EAX);
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_TONE_FREQ + 4, 160); /* volume */
    pr_asm_mov_r_imm_m(&a, PR_REG_EDI, OFF_TONE_FREQ + 8, 0);    /* quadrada */

    /* log a cada 64 frames */
    pr_asm_mov_r_r(&a, PR_REG_EAX, PR_REG_EBP);
    pr_asm_and_r_imm(&a, PR_REG_EAX, 0x3F);
    size_t j_skip_log = pr_asm_jne_rel32(&a);
    pr_asm_mov_r_imm(&a, PR_REG_ESI, (uint32_t)(PXP_LOAD_ADDR + off_tick));
    size_t call_tick = pr_asm_call32(&a);
    pr_asm_patch_rel32(&a, j_skip_log, pr_asm_pos(&a));

    pr_asm_inc_r(&a, PR_REG_EBP);
    size_t jmp_loop = pr_asm_jmp_rel32(&a);
    pr_asm_patch_rel32(&a, jmp_loop, loop_top);

    /* ---- print: ESI = texto NUL; clobbers EAX ---- */
    size_t off_print = pr_asm_pos(&a);
    size_t p_loop = pr_asm_pos(&a);
    pr_asm_mov_r8_m8(&a, 0, PR_REG_ESI, 0); /* AL = [ESI] */
    pr_asm_test_r8_r8(&a, 0, 0);
    size_t j_pdone = pr_asm_jz_rel32(&a);
    pr_asm_mov_m8_r8(&a, PR_REG_EDI, OFF_LOG_DATA, 0);
    pr_asm_inc_r(&a, PR_REG_ESI);
    size_t jmp_ploop = pr_asm_jmp_rel32(&a);
    pr_asm_patch_rel32(&a, jmp_ploop, p_loop);
    pr_asm_patch_rel32(&a, j_pdone, pr_asm_pos(&a));
    pr_asm_mov_r_imm(&a, PR_REG_ECX, 1);
    pr_asm_mov_m_r(&a, PR_REG_EDI, OFF_LOG_FLUSH, PR_REG_ECX);
    pr_asm_ret(&a);

    pr_asm_patch_rel32(&a, call_start, off_print);
    pr_asm_patch_rel32(&a, call_bye, off_print);
    pr_asm_patch_rel32(&a, call_tick, off_print);

    /* ---- monta o arquivo PXP (cabeçalho + segmento) ---- */
    size_t seg = a.len;
    size_t total = PXP_HEADER_SIZE + seg;
    uint8_t* out = (uint8_t*)calloc(1, total);
    if (!out) {
        pr_asm_free(&a);
        return PR_ERR_NOMEM;
    }
    memcpy(out, "PXP0", 4);
    uint32_t version = 1;
    uint32_t load = PXP_LOAD_ADDR;
    uint32_t entry = (uint32_t)(PXP_LOAD_ADDR + off_code);
    uint32_t code_size = (uint32_t)seg;
    uint32_t bss_size = 0;
    memcpy(out + 4, &version, 4);
    memcpy(out + 8, &load, 4);
    memcpy(out + 12, &entry, 4);
    memcpy(out + 16, &code_size, 4);
    memcpy(out + 20, &bss_size, 4);
    memcpy(out + PXP_HEADER_SIZE, a.buf, seg);
    pr_asm_free(&a);

    *out_data = out;
    *out_len = total;
    return PR_OK;
}
