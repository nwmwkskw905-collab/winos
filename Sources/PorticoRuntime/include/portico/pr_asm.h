/* Portico Runtime — montador mínimo (emissor de bytes) para payloads nativos PXP
 * e para os testes do interpretador. Não é um assembler completo; é um helper
 * de código que elimina mágicas de bytes nos testes e no self-test. */
#ifndef PORTICO_PR_ASM_H
#define PORTICO_PR_ASM_H

#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pr_asm {
    uint8_t* buf;
    size_t   len;
    size_t   cap;
} pr_asm;

pr_status pr_asm_init(pr_asm* a, size_t cap);
void      pr_asm_free(pr_asm* a);
pr_status pr_asm_u8(pr_asm* a, uint8_t v);
pr_status pr_asm_u16(pr_asm* a, uint16_t v);
pr_status pr_asm_u32(pr_asm* a, uint32_t v);
/* Patch de um u32 já emitido (para fixups de deslocamento). */
void      pr_asm_patch_u32(pr_asm* a, size_t at, uint32_t v);
size_t    pr_asm_pos(const pr_asm* a);

/* Instruções do subconjunto (32-bit flat). */
void pr_asm_mov_r_imm(pr_asm* a, int rd, uint32_t imm);          /* B8+rd */
void pr_asm_mov_r_r(pr_asm* a, int rd, int rs);                   /* 89 /r (rs→rd) */
void pr_asm_mov_r_m(pr_asm* a, int rd, int base, int32_t disp);  /* 8B /r */
void pr_asm_mov_m_r(pr_asm* a, int base, int32_t disp, int rs);  /* 89 /r */
void pr_asm_mov_r_imm_m(pr_asm* a, int base, int32_t disp, uint32_t imm); /* C7 /0 */
void pr_asm_lea(pr_asm* a, int rd, int base, int32_t disp);       /* 8D /r */
void pr_asm_add_r_r(pr_asm* a, int rd, int rs);
void pr_asm_add_r_imm(pr_asm* a, int rd, uint32_t imm);           /* 83 /0 ib ou 81 */
void pr_asm_sub_r_r(pr_asm* a, int rd, int rs);
void pr_asm_sub_r_imm(pr_asm* a, int rd, uint32_t imm);
void pr_asm_xor_r_r(pr_asm* a, int rd, int rs);
void pr_asm_cmp_r_r(pr_asm* a, int rd, int rs);
void pr_asm_cmp_r_imm(pr_asm* a, int rd, uint32_t imm);
void pr_asm_inc_r(pr_asm* a, int rd);
void pr_asm_dec_r(pr_asm* a, int rd);
void pr_asm_push_r(pr_asm* a, int rs);
void pr_asm_pop_r(pr_asm* a, int rd);
void pr_asm_call_rel(pr_asm* a, int32_t rel);   /* E8 cd com rel já conhecido */
size_t pr_asm_call32(pr_asm* a);                /* E8 + placeholder; retorna offset do rel */
void pr_asm_ret(pr_asm* a);
size_t pr_asm_jmp_rel32(pr_asm* a);             /* E9 + placeholder; retorna offset do rel */
size_t pr_asm_je_rel32(pr_asm* a);              /* 0F84 + placeholder; offset do rel */
size_t pr_asm_jne_rel32(pr_asm* a);             /* 0F85 */
size_t pr_asm_jl_rel32(pr_asm* a);              /* 0F8C */
size_t pr_asm_jg_rel32(pr_asm* a);              /* 0F8F */
/* Aponta ramificação rel32 (offset retornado pelos helpers) para `target` (offset no buffer). */
void pr_asm_patch_rel32(pr_asm* a, size_t rel_at, size_t target);
void pr_asm_int(pr_asm* a, uint8_t vec);        /* CD ib */
void pr_asm_hlt(pr_asm* a);                     /* F4 */
void pr_asm_nop(pr_asm* a);                     /* 90 */

/* Extensões usadas pelo payload de self-test e testes de CPU. */
void pr_asm_and_r_imm(pr_asm* a, int rd, uint32_t imm);   /* 83 /4 ib ou 81 /4 id */
void pr_asm_or_r_imm(pr_asm* a, int rd, uint32_t imm);    /* 83 /1 ib ou 81 /1 id */
void pr_asm_test_r_r(pr_asm* a, int rd, int rs);          /* 85 /r */
void pr_asm_shl1(pr_asm* a, int rd);                      /* D1 /4 */
void pr_asm_shr1(pr_asm* a, int rd);                      /* D1 /5 */
void pr_asm_sar1(pr_asm* a, int rd);                      /* D1 /7 */
void pr_asm_mov_r8_m8(pr_asm* a, int rd8, int base, int32_t disp); /* 8A /r (rd8: 0..3) */
void pr_asm_mov_m8_r8(pr_asm* a, int base, int32_t disp, int rs8);  /* 88 /r (rs8: 0..3) */
void pr_asm_test_r8_r8(pr_asm* a, int rd8, int rs8);      /* 84 /r (registradores 0..3) */
size_t pr_asm_jz_rel32(pr_asm* a);                        /* 0F84 (alias de JE) */
size_t pr_asm_jge_rel32(pr_asm* a);                       /* 0F8D */
size_t pr_asm_jle_rel32(pr_asm* a);                       /* 0F8E */

/* Comentário embutido no fluxo (vira entrada de log do montador p/ debug). */
void pr_asm_note(pr_asm* a, const char* text);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_ASM_H */
