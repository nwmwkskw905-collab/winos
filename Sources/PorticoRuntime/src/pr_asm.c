/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_asm.h"

#include <stdlib.h>
#include <string.h>

static pr_status need(pr_asm* a, size_t n) {
    if (a->len + n > a->cap) {
        size_t cap = a->cap ? a->cap : 256;
        while (cap < a->len + n) cap *= 2;
        uint8_t* nb = (uint8_t*)realloc(a->buf, cap);
        if (!nb) return PR_ERR_NOMEM;
        a->buf = nb;
        a->cap = cap;
    }
    return PR_OK;
}

pr_status pr_asm_init(pr_asm* a, size_t cap) {
    if (!a) return PR_ERR_INVALID;
    a->len = 0;
    a->cap = cap ? cap : 256;
    a->buf = (uint8_t*)malloc(a->cap);
    return a->buf ? PR_OK : PR_ERR_NOMEM;
}

void pr_asm_free(pr_asm* a) {
    if (!a) return;
    free(a->buf);
    a->buf = NULL;
    a->len = a->cap = 0;
}

pr_status pr_asm_u8(pr_asm* a, uint8_t v) {
    pr_status st = need(a, 1);
    if (st != PR_OK) return st;
    a->buf[a->len++] = v;
    return PR_OK;
}

pr_status pr_asm_u16(pr_asm* a, uint16_t v) {
    pr_status st = need(a, 2);
    if (st != PR_OK) return st;
    a->buf[a->len++] = (uint8_t)(v & 0xFF);
    a->buf[a->len++] = (uint8_t)((v >> 8) & 0xFF);
    return PR_OK;
}

pr_status pr_asm_u32(pr_asm* a, uint32_t v) {
    pr_status st = need(a, 4);
    if (st != PR_OK) return st;
    a->buf[a->len++] = (uint8_t)(v & 0xFF);
    a->buf[a->len++] = (uint8_t)((v >> 8) & 0xFF);
    a->buf[a->len++] = (uint8_t)((v >> 16) & 0xFF);
    a->buf[a->len++] = (uint8_t)((v >> 24) & 0xFF);
    return PR_OK;
}

void pr_asm_patch_u32(pr_asm* a, size_t at, uint32_t v) {
    if (!a || at + 4 > a->len) return;
    a->buf[at] = (uint8_t)(v & 0xFF);
    a->buf[at + 1] = (uint8_t)((v >> 8) & 0xFF);
    a->buf[at + 2] = (uint8_t)((v >> 16) & 0xFF);
    a->buf[at + 3] = (uint8_t)((v >> 24) & 0xFF);
}

size_t pr_asm_pos(const pr_asm* a) { return a ? a->len : 0; }

/* mod=3 (registradores) */
static void modrm_rr(pr_asm* a, int reg, int rm) {
    pr_asm_u8(a, (uint8_t)(0xC0 | ((reg & 7) << 3) | (rm & 7)));
}

/* mod=2 (base + disp32); base==4 exige SIB. */
static void modrm_bd(pr_asm* a, int reg, int base, int32_t disp) {
    pr_asm_u8(a, (uint8_t)(0x80 | ((reg & 7) << 3) | (base & 7)));
    if ((base & 7) == 4) pr_asm_u8(a, 0x24); /* SIB: scale=0, index=none, base=ESP */
    pr_asm_u32(a, (uint32_t)disp);
}

void pr_asm_mov_r_imm(pr_asm* a, int rd, uint32_t imm) {
    pr_asm_u8(a, (uint8_t)(0xB8 + (rd & 7)));
    pr_asm_u32(a, imm);
}

void pr_asm_mov_r_r(pr_asm* a, int rd, int rs) {
    pr_asm_u8(a, 0x89);
    modrm_rr(a, rs, rd);
}

void pr_asm_mov_r_m(pr_asm* a, int rd, int base, int32_t disp) {
    pr_asm_u8(a, 0x8B);
    modrm_bd(a, rd, base, disp);
}

void pr_asm_mov_m_r(pr_asm* a, int base, int32_t disp, int rs) {
    pr_asm_u8(a, 0x89);
    modrm_bd(a, rs, base, disp);
}

void pr_asm_mov_r_imm_m(pr_asm* a, int base, int32_t disp, uint32_t imm) {
    pr_asm_u8(a, 0xC7);
    modrm_bd(a, 0, base, disp);
    pr_asm_u32(a, imm);
}

void pr_asm_lea(pr_asm* a, int rd, int base, int32_t disp) {
    pr_asm_u8(a, 0x8D);
    modrm_bd(a, rd, base, disp);
}

static void alu_rr(pr_asm* a, uint8_t opcode, int rd, int rs) {
    pr_asm_u8(a, opcode);
    modrm_rr(a, rs, rd);
}

void pr_asm_add_r_r(pr_asm* a, int rd, int rs) { alu_rr(a, 0x01, rd, rs); }
void pr_asm_sub_r_r(pr_asm* a, int rd, int rs) { alu_rr(a, 0x29, rd, rs); }
void pr_asm_xor_r_r(pr_asm* a, int rd, int rs) { alu_rr(a, 0x31, rd, rs); }
void pr_asm_cmp_r_r(pr_asm* a, int rd, int rs) { alu_rr(a, 0x39, rd, rs); }
void pr_asm_test_r_r(pr_asm* a, int rd, int rs) { alu_rr(a, 0x85, rd, rs); }

static void alu_r_imm(pr_asm* a, int sub, int rd, uint32_t imm) {
    if (imm <= 0x7F || imm >= 0xFFFFFF80u) {
        pr_asm_u8(a, 0x83);
        modrm_rr(a, sub, rd);
        pr_asm_u8(a, (uint8_t)(imm & 0xFF));
    } else {
        pr_asm_u8(a, 0x81);
        modrm_rr(a, sub, rd);
        pr_asm_u32(a, imm);
    }
}

void pr_asm_add_r_imm(pr_asm* a, int rd, uint32_t imm) { alu_r_imm(a, 0, rd, imm); }
void pr_asm_and_r_imm(pr_asm* a, int rd, uint32_t imm) { alu_r_imm(a, 4, rd, imm); }
void pr_asm_or_r_imm(pr_asm* a, int rd, uint32_t imm)  { alu_r_imm(a, 1, rd, imm); }

void pr_asm_sub_r_imm(pr_asm* a, int rd, uint32_t imm) { alu_r_imm(a, 5, rd, imm); }

void pr_asm_cmp_r_imm(pr_asm* a, int rd, uint32_t imm) { alu_r_imm(a, 7, rd, imm); }

void pr_asm_inc_r(pr_asm* a, int rd) { pr_asm_u8(a, (uint8_t)(0x40 + (rd & 7))); }
void pr_asm_dec_r(pr_asm* a, int rd) { pr_asm_u8(a, (uint8_t)(0x48 + (rd & 7))); }

void pr_asm_push_r(pr_asm* a, int rs) { pr_asm_u8(a, (uint8_t)(0x50 + (rs & 7))); }
void pr_asm_pop_r(pr_asm* a, int rd)  { pr_asm_u8(a, (uint8_t)(0x58 + (rd & 7))); }

void pr_asm_call_rel(pr_asm* a, int32_t rel) {
    pr_asm_u8(a, 0xE8);
    pr_asm_u32(a, (uint32_t)rel);
}

size_t pr_asm_call32(pr_asm* a) {
    pr_asm_u8(a, 0xE8);
    size_t at = pr_asm_pos(a);
    pr_asm_u32(a, 0);
    return at;
}

void pr_asm_ret(pr_asm* a) { pr_asm_u8(a, 0xC3); }

size_t pr_asm_jmp_rel32(pr_asm* a) {
    pr_asm_u8(a, 0xE9);
    size_t at = pr_asm_pos(a);
    pr_asm_u32(a, 0);
    return at;
}

static size_t jcc_rel32(pr_asm* a, uint8_t cc_hi) {
    pr_asm_u8(a, 0x0F);
    pr_asm_u8(a, cc_hi);
    size_t at = pr_asm_pos(a);
    pr_asm_u32(a, 0);
    return at;
}

void pr_asm_patch_rel32(pr_asm* a, size_t rel_at, size_t target) {
    /* rel = target - end_of_rel_field (rel_at + 4) */
    int64_t rel = (int64_t)target - (int64_t)(rel_at + 4);
    pr_asm_patch_u32(a, rel_at, (uint32_t)rel);
}

size_t pr_asm_je_rel32(pr_asm* a)  { return jcc_rel32(a, 0x84); }
size_t pr_asm_jz_rel32(pr_asm* a)  { return jcc_rel32(a, 0x84); }
size_t pr_asm_jne_rel32(pr_asm* a) { return jcc_rel32(a, 0x85); }
size_t pr_asm_jl_rel32(pr_asm* a)  { return jcc_rel32(a, 0x8C); }
size_t pr_asm_jg_rel32(pr_asm* a)  { return jcc_rel32(a, 0x8F); }
size_t pr_asm_jge_rel32(pr_asm* a) { return jcc_rel32(a, 0x8D); }
size_t pr_asm_jle_rel32(pr_asm* a) { return jcc_rel32(a, 0x8E); }

void pr_asm_int(pr_asm* a, uint8_t vec) {
    pr_asm_u8(a, 0xCD);
    pr_asm_u8(a, vec);
}

void pr_asm_hlt(pr_asm* a) { pr_asm_u8(a, 0xF4); }
void pr_asm_nop(pr_asm* a) { pr_asm_u8(a, 0x90); }

static void shift1(pr_asm* a, int sub, int rd) {
    pr_asm_u8(a, 0xD1);
    modrm_rr(a, sub, rd);
}

void pr_asm_shl1(pr_asm* a, int rd) { shift1(a, 4, rd); }
void pr_asm_shr1(pr_asm* a, int rd) { shift1(a, 5, rd); }
void pr_asm_sar1(pr_asm* a, int rd) { shift1(a, 7, rd); }

void pr_asm_mov_r8_m8(pr_asm* a, int rd8, int base, int32_t disp) {
    pr_asm_u8(a, 0x8A);
    modrm_bd(a, rd8 & 3, base, disp);
}

void pr_asm_mov_m8_r8(pr_asm* a, int base, int32_t disp, int rs8) {
    pr_asm_u8(a, 0x88);
    modrm_bd(a, rs8 & 3, base, disp);
}

void pr_asm_test_r8_r8(pr_asm* a, int rd8, int rs8) {
    pr_asm_u8(a, 0x84);
    modrm_rr(a, rs8 & 3, rd8 & 3);
}

void pr_asm_note(pr_asm* a, const char* text) {
    (void)a;
    (void)text; /* marcador semântico para leitura humana do builder */
}
