#include "pt_util.h"
#include "portico/pr_cpu.h"
#include "portico/pr_asm.h"

#include <stdlib.h>

static int trap_hits = 0;
static int trap_fn(void* ud, pr_cpu* cpu, uint8_t vec) {
    (void)ud;
    (void)cpu;
    trap_hits++;
    return (vec == 0x80) ? 0 : 1;
}

static uint32_t mmio_val = 0;
static uint32_t mmio_rd(void* ud, uint32_t off) {
    (void)ud;
    (void)off;
    return mmio_val;
}
static void mmio_wr(void* ud, uint32_t off, uint32_t v) {
    (void)ud;
    (void)off;
    mmio_val = v;
}

static void run_buf(pr_cpu* cpu, pr_asm* a) {
    pr_cpu_reset(cpu);
    pr_cpu_load(cpu, 0x1000, a->buf, a->len);
    pr_cpu_set_eip(cpu, 0x1000);
    pr_cpu_set_reg(cpu, PR_REG_ESP, 0x80000);
    uint64_t n = 0;
    pr_status st = pr_cpu_run(cpu, 100000, &n);
    CHECK(st == PR_OK);
}

void test_cpu(void) {
    pr_cpu* cpu = pr_cpu_create(1 << 20);
    CHECK(cpu != NULL);
    pr_asm a;
    pr_asm_init(&a, 1024);

    /* 1) aritmética básica */
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 5);
    pr_asm_mov_r_imm(&a, PR_REG_ECX, 3);
    pr_asm_add_r_r(&a, PR_REG_EAX, PR_REG_ECX);
    pr_asm_hlt(&a);
    run_buf(cpu, &a);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EAX), 8);
    CHECK_EQ_U32(pr_cpu_halted(cpu), 1);

    /* 2) flags ZF em SUB igual */
    pr_asm_free(&a);
    pr_asm_init(&a, 1024);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 7);
    pr_asm_sub_r_imm(&a, PR_REG_EAX, 7);
    pr_asm_hlt(&a);
    run_buf(cpu, &a);
    CHECK((pr_cpu_eflags(cpu) & PR_EFLAGS_ZF) != 0);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EAX), 0);

    /* 3) laço: soma 1..10 = 55 */
    pr_asm_free(&a);
    pr_asm_init(&a, 1024);
    pr_asm_mov_r_imm(&a, PR_REG_ECX, 10);
    pr_asm_xor_r_r(&a, PR_REG_EAX, PR_REG_EAX);
    size_t top = pr_asm_pos(&a);
    pr_asm_add_r_r(&a, PR_REG_EAX, PR_REG_ECX);
    pr_asm_dec_r(&a, PR_REG_ECX);
    size_t jnz = pr_asm_jne_rel32(&a);
    pr_asm_patch_rel32(&a, jnz, top);
    pr_asm_hlt(&a);
    run_buf(cpu, &a);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EAX), 55);

    /* 4) CALL/RET + pilha */
    pr_asm_free(&a);
    pr_asm_init(&a, 1024);
    size_t call = pr_asm_call32(&a);
    pr_asm_hlt(&a);
    size_t fn = pr_asm_pos(&a);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 42);
    pr_asm_push_r(&a, PR_REG_EAX);
    pr_asm_pop_r(&a, PR_REG_EDX);
    pr_asm_ret(&a);
    pr_asm_patch_rel32(&a, call, fn);
    run_buf(cpu, &a);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EAX), 42);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EDX), 42);

    /* 5) MUL/IMUL/DIV */
    pr_asm_free(&a);
    pr_asm_init(&a, 1024);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 1000000);
    pr_asm_mov_r_imm(&a, PR_REG_ECX, 3);
    pr_asm_u8(&a, 0xF7); pr_asm_u8(&a, 0xE1); /* MUL ECX */
    pr_asm_mov_r_r(&a, PR_REG_EBX, PR_REG_EAX); /* ebx = low */
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 7);
    pr_asm_mov_r_imm(&a, PR_REG_EDX, 0);
    pr_asm_mov_r_imm(&a, PR_REG_ECX, 3);
    pr_asm_u8(&a, 0xF7); pr_asm_u8(&a, 0xF1); /* DIV ECX */
    pr_asm_hlt(&a);
    run_buf(cpu, &a);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EBX), 3000000);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EAX), 2); /* 7/3 */
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EDX), 1); /* resto */

    /* 6) Jcc: CMP + JL/JG */
    pr_asm_free(&a);
    pr_asm_init(&a, 1024);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 3);
    pr_asm_cmp_r_imm(&a, PR_REG_EAX, 10);
    size_t jl = pr_asm_jl_rel32(&a);
    pr_asm_mov_r_imm(&a, PR_REG_EBX, 0xBAD);
    pr_asm_patch_rel32(&a, jl, pr_asm_pos(&a));
    pr_asm_mov_r_imm(&a, PR_REG_EBX, 1);
    pr_asm_hlt(&a);
    run_buf(cpu, &a);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EBX), 1);

    /* 7) memória: MOV r/m ↔ r, LEA */
    pr_asm_free(&a);
    pr_asm_init(&a, 1024);
    pr_asm_lea(&a, PR_REG_EAX, PR_REG_ESI, 0x2000);
    pr_asm_mov_r_imm(&a, PR_REG_ECX, 0x12345678);
    pr_asm_mov_m_r(&a, PR_REG_EAX, 0, PR_REG_ECX);
    pr_asm_mov_r_m(&a, PR_REG_EDX, PR_REG_EAX, 0);
    pr_asm_hlt(&a);
    run_buf(cpu, &a);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EAX), 0x2000);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EDX), 0x12345678);

    /* 8) MMIO */
    pr_cpu_set_mmio(cpu, 0, 0xF0000000u, 0x1000, mmio_rd, mmio_wr, NULL);
    pr_asm_free(&a);
    pr_asm_init(&a, 1024);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 0xF0000000u);
    pr_asm_mov_r_imm(&a, PR_REG_ECX, 0xABCD);
    pr_asm_mov_m_r(&a, PR_REG_EAX, 0, PR_REG_ECX);
    pr_asm_mov_r_imm(&a, PR_REG_EDX, 0xF0000000u);
    pr_asm_mov_r_m(&a, PR_REG_EBX, PR_REG_EDX, 0);
    pr_asm_hlt(&a);
    run_buf(cpu, &a);
    CHECK_EQ_U32(mmio_val, 0xABCD);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EBX), 0xABCD);

    /* 9) MOVZX/MOVSX/SETcc/BSWAP */
    pr_asm_free(&a);
    pr_asm_init(&a, 1024);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 0xFFFFFF80u);
    pr_asm_u8(&a, 0x0F); pr_asm_u8(&a, 0xBE); pr_asm_u8(&a, 0xC8); /* MOVSX ECX, AL */
    pr_asm_u8(&a, 0x0F); pr_asm_u8(&a, 0xB6); pr_asm_u8(&a, 0xD0); /* MOVZX EDX, AL */
    pr_asm_cmp_r_imm(&a, PR_REG_EAX, 0);
    pr_asm_u8(&a, 0x0F); pr_asm_u8(&a, 0x9C); pr_asm_u8(&a, 0xC3); /* SETL BL */
    pr_asm_mov_r_imm(&a, PR_REG_EDI, 0x11223344);
    pr_asm_u8(&a, 0x0F); pr_asm_u8(&a, 0xCF); /* BSWAP EDI */
    pr_asm_hlt(&a);
    run_buf(cpu, &a);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_ECX), 0xFFFFFF80u); /* sinal estendido */
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EDX), 0x80u);       /* zero estendido */
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EBX), 1);           /* SETL verdadeiro */
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EDI), 0x44332211u);

    /* 10) INT com trap (0x80 não interrompe) */
    pr_cpu_set_trap(cpu, trap_fn, NULL);
    trap_hits = 0;
    pr_asm_free(&a);
    pr_asm_init(&a, 1024);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 1);
    pr_asm_int(&a, 0x80);
    pr_asm_add_r_imm(&a, PR_REG_EAX, 1);
    pr_asm_hlt(&a);
    run_buf(cpu, &a);
    CHECK_EQ_U32((unsigned)trap_hits, 1);
    CHECK_EQ_U32(pr_cpu_reg(cpu, PR_REG_EAX), 2);
    pr_cpu_set_trap(cpu, NULL, NULL);

    /* 11) opcode fora do subconjunto → PR_ERR_FAULT honesto */
    pr_asm_free(&a);
    pr_asm_init(&a, 64);
    pr_asm_u8(&a, 0x66); /* prefixo 16-bit: explicitamente não suportado */
    pr_asm_nop(&a);
    pr_asm_hlt(&a);
    pr_cpu_reset(cpu);
    pr_cpu_load(cpu, 0x1000, a.buf, a.len);
    pr_cpu_set_eip(cpu, 0x1000);
    uint64_t n = 0;
    pr_status st = pr_cpu_run(cpu, 10, &n);
    CHECK(st == PR_ERR_FAULT);
    const pr_cpu_fault* f = pr_cpu_last_fault(cpu);
    CHECK(f != NULL);
    CHECK(strstr(f->reason, "16-bit") != NULL);

    /* 12) divisão por zero → falta */
    pr_asm_free(&a);
    pr_asm_init(&a, 64);
    pr_asm_mov_r_imm(&a, PR_REG_EAX, 10);
    pr_asm_xor_r_r(&a, PR_REG_EDX, PR_REG_EDX);
    pr_asm_mov_r_imm(&a, PR_REG_ECX, 0);
    pr_asm_u8(&a, 0xF7); pr_asm_u8(&a, 0xF1); /* DIV ECX */
    pr_asm_hlt(&a);
    pr_cpu_reset(cpu);
    pr_cpu_load(cpu, 0x1000, a.buf, a.len);
    pr_cpu_set_eip(cpu, 0x1000);
    st = pr_cpu_run(cpu, 10, &n);
    CHECK(st == PR_ERR_FAULT);
    CHECK(strstr(pr_cpu_last_fault(cpu)->reason, "zero") != NULL);

    pr_asm_free(&a);
    pr_cpu_destroy(cpu);
}
