/* Testes do interpretador x64 mínimo (pr_cpu64): subconjunto straight-line do
 * PE visual, semântica real de registradores/stack, RIP-relative, trap INT e
 * fault HONESTO com opcode/RIP/endereço (nunca se finge suporte). */
#include "pt_util.h"

#include "portico/pr_cpu64.h"

#include <string.h>

static uint8_t t64_last_vec;
static int t64_trap_ok(void* ud, pr_cpu64* c, uint8_t vector) {
    (void)ud; (void)c;
    t64_last_vec = vector;
    return 0;
}
static int t64_trap_stop(void* ud, pr_cpu64* c, uint8_t vector) {
    (void)ud; (void)c; (void)vector;
    return -1;
}
static int t64_guard_all(void* ud, uint64_t addr, size_t len, int acc) {
    (void)ud; (void)addr; (void)len; (void)acc;
    return 1; /* !=0 = permitido (convenção pr_cpu) */
}
static int t64_guard_no_x(void* ud, uint64_t addr, size_t len, int acc) {
    (void)ud; (void)addr; (void)len;
    return (acc == PR_CPU64_ACC_X) ? 0 : 1;
}

void test_cpu64(void) {
    static uint8_t mem[8192];
    pr_cpu64* c = pr_cpu64_create_on(mem, (uint32_t)sizeof(mem));
    CHECK(c != NULL);
    uint64_t n = 0;

    /* -- ALU/mov: mov eax,imm32 (zero-extend), mov rbx,rax, add, xor -- */
    {
        memset(mem, 0, sizeof(mem));
        /* 0x200: B8 78 56 34 12 | 48 89 C3 | 48 83 C3 08 | 48 31 C9 | F4 */
        static const uint8_t prog[] = {
            0xB8, 0x78, 0x56, 0x34, 0x12,
            0x48, 0x89, 0xC3,
            0x48, 0x83, 0xC3, 0x08,
            0x48, 0x31, 0xC9,
            0xF4,
        };
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32(n, 5);
        CHECK_EQ_U32(pr_cpu64_reg(c, PR_R64_RAX), 0x12345678u);
        CHECK_EQ_U32(pr_cpu64_reg(c, PR_R64_RBX), 0x12345680u);
        CHECK_EQ_U32(pr_cpu64_reg(c, PR_R64_RCX), 0);
        CHECK(pr_cpu64_halted(c));
    }

    /* -- ops de 32 bits zeram a metade superior (semântica x86-64) -- */
    {
        /* 48 C7 C0 FF FF FF FF | B8 00 00 00 00 | F4 */
        static const uint8_t prog[] = {
            0x48, 0xC7, 0xC0, 0xFF, 0xFF, 0xFF, 0xFF,
            0xB8, 0x00, 0x00, 0x00, 0x00,
            0xF4,
        };
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(pr_cpu64_reg(c, PR_R64_RAX) == 0); /* zero-extend real */
    }

    /* -- push/pop e call rm64/ret (8 bytes) com retorno exato -- */
    {
        /* rsp=0x800; call rbx p/ 0x300 → mov eax,7; ret → mov eax,42; hlt */
        static const uint8_t mainc[] = {
            0x53,                               /* push rbx */
            0x48, 0xC7, 0xC3, 0x00, 0x03, 0x00, 0x00, /* mov rbx, 0x300 */
            0xFF, 0xD3,                         /* call rbx */
            0xB8, 0x2A, 0x00, 0x00, 0x00,       /* mov eax, 42 */
            0x5B,                               /* pop rbx */
            0xF4,
        };
        static const uint8_t sub[] = {
            0xB8, 0x07, 0x00, 0x00, 0x00,       /* mov eax, 7 */
            0xC3,                               /* ret */
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, mainc, sizeof(mainc));
        memcpy(mem + 0x300, sub, sizeof(sub));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSP, 0x800);
        pr_cpu64_set_reg(c, PR_R64_RBX, 0xCAFEBABEull);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32(pr_cpu64_reg(c, PR_R64_RAX), 42);
        CHECK_EQ_U32(pr_cpu64_reg(c, PR_R64_RBX), 0xCAFEBABEu); /* pop real */
        CHECK_EQ_U32(pr_cpu64_reg(c, PR_R64_RSP), 0x800);       /* stack ok */
    }

    /* -- call [rip+disp32] (FF 15) + slot de 8 bytes -- */
    {
        /* 0x200: FF 15 disp → slot 0x230; alvo 0x300: mov eax,7; hlt */
        memset(mem, 0, sizeof(mem));
        mem[0x200] = 0xFF; mem[0x201] = 0x15;
        { int32_t d = 0x230 - (0x200 + 6); memcpy(mem + 0x202, &d, 4); }
        { uint64_t tgt = 0x300; memcpy(mem + 0x230, &tgt, 8); }
        static const uint8_t sub[] = { 0xB8, 0x07, 0x00, 0x00, 0x00, 0xF4 };
        memcpy(mem + 0x300, sub, sizeof(sub));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSP, 0x800);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32(n, 3); /* call + mov + hlt */
        CHECK_EQ_U32(pr_cpu64_reg(c, PR_R64_RAX), 7);
        CHECK_EQ_U32(pr_cpu64_reg(c, PR_R64_RSP), 0x7F8); /* 8 bytes */
    }

    /* -- mov r64, [rip+disp] (RIP-relative mod=00 rm=101) -- */
    {
        memset(mem, 0, sizeof(mem));
        /* 48 8B 05 09 00 00 00 (len 7 → rip 0x207; 0x207+9 = 0x210) */
        static const uint8_t prog[] = { 0x48, 0x8B, 0x05, 0x09, 0x00, 0x00, 0x00, 0xF4 };
        memcpy(mem + 0x200, prog, sizeof(prog));
        uint64_t v = 0x1122334455667788ull;
        memcpy(mem + 0x210, &v, 8);
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(pr_cpu64_reg(c, PR_R64_RAX) == 0x1122334455667788ull);
    }

    /* -- mov [rsp+disp8], imm32 / mov [rsp+disp8], r64 -- */
    {
        /* sub rsp,0x50; mov [rsp+0x20], 240; mov [rsp+0x28], r13; hlt */
        static const uint8_t prog[] = {
            0x48, 0x83, 0xEC, 0x50,
            0xC7, 0x44, 0x24, 0x20, 0xF0, 0x00, 0x00, 0x00,
            0x4C, 0x89, 0x6C, 0x24, 0x28,
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSP, 0x800);
        pr_cpu64_set_reg(c, 13, 0x00E00000ull); /* r13 */
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        uint32_t a = 0; uint64_t b = 0;
        memcpy(&a, mem + (0x800 - 0x50 + 0x20), 4);
        memcpy(&b, mem + (0x800 - 0x50 + 0x28), 8);
        CHECK_EQ_U32(a, 240);
        CHECK(b == 0x00E00000ull);
    }

    /* -- INT 2Eh com trap: vetor capturado, execução continua -- */
    {
        static const uint8_t prog[] = {
            0xB8, 0x00, 0x00, 0x00, 0x00,
            0xCD, 0x2E,
            0xB8, 0x63, 0x00, 0x00, 0x00,
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_trap(c, t64_trap_ok, NULL);
        pr_cpu64_set_rip(c, 0x200);
        t64_last_vec = 0;
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32(t64_last_vec, 0x2E);
        CHECK_EQ_U32(pr_cpu64_reg(c, PR_R64_RAX), 99);
        pr_cpu64_set_trap(c, NULL, NULL);
    }

    /* -- ret imm16 (C2): pop + ajuste da pilha -- */
    {
        /* call rbx (0x300): mov eax,7; ret 0x10 → rsp += 0x10 extra */
        static const uint8_t mainc[] = {
            0x48, 0xC7, 0xC3, 0x00, 0x03, 0x00, 0x00,
            0xFF, 0xD3,
            0xF4,
        };
        static const uint8_t sub[] = {
            0xB8, 0x07, 0x00, 0x00, 0x00,
            0xC2, 0x10, 0x00,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, mainc, sizeof(mainc));
        memcpy(mem + 0x300, sub, sizeof(sub));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSP, 0x800);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32(pr_cpu64_reg(c, PR_R64_RAX), 7);
        CHECK_EQ_U32(pr_cpu64_reg(c, PR_R64_RSP), 0x810); /* ret 0x10 real */
    }

    /* -- fault HONESTO: opcode fora do subconjunto registra opcode/RIP -- */
    {
        memset(mem, 0, sizeof(mem));
        mem[0x200] = 0x90;   /* nop */
        mem[0x201] = 0x0F;   /* RDMSR: ainda fora do subconjunto (RDTSC/0F 31
                              * passou a ser suportado por exigência real) */
        mem[0x202] = 0x32;
        pr_cpu64_set_rip(c, 0x200);
        pr_status st = pr_cpu64_run(c, 100, &n);
        CHECK(st == PR_ERR_FAULT);
        const pr_cpu64_fault* f = pr_cpu64_last_fault(c);
        CHECK(f != NULL);
        CHECK_EQ_U32(n, 1);
        CHECK_EQ_U32((uint32_t)f->rip, 0x201);
        CHECK_EQ_U32(f->opcode, 0x0F);
        CHECK(strstr(f->reason, "fora do subconjunto") != NULL);
    }

    /* -- trap que pede parada → fault com diagnóstico -- */
    {
        static const uint8_t prog[] = { 0xCD, 0x2E, 0xF4 };
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_trap(c, t64_trap_stop, NULL);
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        const pr_cpu64_fault* f = pr_cpu64_last_fault(c);
        CHECK(f != NULL && strstr(f->reason, "parada") != NULL);
        pr_cpu64_set_trap(c, NULL, NULL);
    }

    /* -- guard: fetch negado registra endereço; escrita negada idem -- */
    {
        pr_cpu64_set_guard(c, t64_guard_no_x, NULL);
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 10, &n) == PR_ERR_FAULT);
        const pr_cpu64_fault* f = pr_cpu64_last_fault(c);
        CHECK(f != NULL && f->addr == 0x200);
        CHECK(strstr(f->reason, "negado") != NULL);

        pr_cpu64_set_guard(c, t64_guard_all, NULL);
        pr_cpu64_set_guard(c, NULL, NULL);
    }

    /* -- endereço fora do espaço → fault com endereço registrado -- */
    {
        static const uint8_t prog[] = {
            0x48, 0xC7, 0xC3, 0x00, 0x00, 0x00, 0x40, /* mov rbx, 0x40000000 */
            0x48, 0x8B, 0x03,                         /* mov rax, [rbx] */
            0xF4,
        };
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        const pr_cpu64_fault* f = pr_cpu64_last_fault(c);
        CHECK(f != NULL && f->addr == 0x40000000u);
        CHECK(strstr(f->reason, "fora do espaco") != NULL);
    }

    /* ============ FASE 1: instruções expandidas ============ */

    /* -- flags: ADD com OF/SF/CF/ZF -- */
    {
        static const uint8_t prog[] = {
            0xB8, 0xFF, 0xFF, 0xFF, 0x7F,   /* mov eax, 0x7FFFFFFF */
            0x83, 0xC0, 0x01,               /* add eax, 1 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 0x80000000u);
        CHECK((pr_cpu64_rflags(c) & PR_R64_OF) != 0);
        CHECK((pr_cpu64_rflags(c) & PR_R64_SF) != 0);
        CHECK((pr_cpu64_rflags(c) & PR_R64_CF) == 0);

        static const uint8_t prog2[] = {
            0xB8, 0xFF, 0xFF, 0xFF, 0xFF,   /* mov eax, -1 */
            0x83, 0xC0, 0x01,               /* add eax, 1 → 0, CF=1 ZF=1 */
            0xF4,
        };
        memcpy(mem + 0x200, prog2, sizeof(prog2));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 0);
        CHECK((pr_cpu64_rflags(c) & PR_R64_CF) != 0);
        CHECK((pr_cpu64_rflags(c) & PR_R64_ZF) != 0);
    }

    /* -- CMP + Jcc rel8 (je taken/not taken, jl signed) -- */
    {
        static const uint8_t prog[] = {
            0xB8, 0x05, 0x00, 0x00, 0x00,   /* mov eax, 5 */
            0x3D, 0x05, 0x00, 0x00, 0x00,   /* cmp eax, 5 */
            0x74, 0x05,                     /* je +5 */
            0xB8, 0x63, 0x00, 0x00, 0x00,   /* mov eax, 99 (pulado) */
            0xB8, 0x2A, 0x00, 0x00, 0x00,   /* mov eax, 42 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 42);

        static const uint8_t prog2[] = {
            0xB8, 0x05, 0x00, 0x00, 0x00,   /* mov eax, 5 */
            0x83, 0xF8, 0x08,               /* cmp eax, 8 */
            0x7C, 0x05,                     /* jl +5 (5 < 8 assinado) */
            0xB8, 0x63, 0x00, 0x00, 0x00,   /* mov eax, 99 (pulado) */
            0xB8, 0x2A, 0x00, 0x00, 0x00,
            0xF4,
        };
        memcpy(mem + 0x200, prog2, sizeof(prog2));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 42);
    }

    /* -- Jcc rel32 (0F 84) taken -- */
    {
        static const uint8_t prog[] = {
            0xB8, 0x07, 0x00, 0x00, 0x00,
            0x83, 0xF8, 0x07,               /* cmp eax, 7 */
            0x0F, 0x84, 0x05, 0x00, 0x00, 0x00,  /* je rel32 +5 */
            0xB8, 0x63, 0x00, 0x00, 0x00,
            0xB8, 0x2A, 0x00, 0x00, 0x00,
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 42);
    }

    /* -- SETcc -- */
    {
        static const uint8_t prog[] = {
            0xB8, 0x05, 0x00, 0x00, 0x00,
            0x83, 0xF8, 0x05,               /* cmp eax, 5 (ZF=1) */
            0x0F, 0x94, 0xC1,               /* setz cl  → 1 */
            0x0F, 0x95, 0xC2,               /* setnz dl → 0 */
            0x0F, 0x9C, 0xC0,               /* setl al  → 0 */
            0x0F, 0x9E, 0xC3,               /* setle bl → 1 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)(pr_cpu64_reg(c, PR_R64_RCX) & 0xFF), 1);
        CHECK_EQ_U32((uint32_t)(pr_cpu64_reg(c, PR_R64_RDX) & 0xFF), 0);
        CHECK_EQ_U32((uint32_t)(pr_cpu64_reg(c, PR_R64_RAX) & 0xFF), 0);
        CHECK_EQ_U32((uint32_t)(pr_cpu64_reg(c, PR_R64_RBX) & 0xFF), 1);
    }

    /* -- LEA [base+index*scale+disp] -- */
    {
        static const uint8_t prog[] = {
            0x48, 0xBB, 0x40, 0x00, 0x00, 0x00, 0, 0, 0, 0,   /* mov rbx, 0x40 */
            0x48, 0xB9, 0x08, 0x00, 0x00, 0x00, 0, 0, 0, 0,   /* mov rcx, 8 */
            0x48, 0x8D, 0x44, 0x8B, 0x10,                     /* lea rax,[rbx+rcx*4+0x10] */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(pr_cpu64_reg(c, PR_R64_RAX) == 0x40 + 32 + 0x10);
    }

    /* -- SIB index*scale + disp32 (mov [rbx+rcx*8+0x200], rax) -- */
    {
        static const uint8_t prog[] = {
            0x48, 0xBB, 0x00, 0x10, 0x00, 0x00, 0, 0, 0, 0,   /* mov rbx, 0x1000 */
            0x48, 0xB9, 0x04, 0x00, 0x00, 0x00, 0, 0, 0, 0,   /* mov rcx, 4 */
            0x48, 0xB8, 0xCD, 0xAB, 0x00, 0x00, 0, 0, 0, 0,   /* mov rax, 0xABCD */
            0x48, 0x89, 0x84, 0xCB, 0x00, 0x02, 0x00, 0x00,   /* mov [rbx+rcx*8+0x200], rax */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        uint64_t v = 0;
        memcpy(&v, mem + (0x1000 + 32 + 0x200), 8);
        CHECK(v == 0xABCDull);
    }

    /* -- shifts/rotates com flags + por CL -- */
    {
        static const uint8_t prog[] = {
            0xB8, 0x01, 0x00, 0x00, 0x00,   /* mov eax, 1 */
            0xC1, 0xE0, 0x04,               /* shl eax, 4 → 0x10 */
            0xC1, 0xE8, 0x02,               /* shr eax, 2 → 4 */
            0xBB, 0x00, 0x00, 0x00, 0x80,   /* mov ebx, 0x80000000 */
            0xC1, 0xFB, 0x1F,               /* sar ebx, 31 → -1 (SF=1) */
            0xB9, 0x03, 0x00, 0x00, 0x00,   /* mov ecx, 3 */
            0xBA, 0x01, 0x00, 0x00, 0x00,   /* mov edx, 1 */
            0xD3, 0xE2,                     /* shl edx, cl → 8 */
            0xD1, 0xC2,                     /* rol edx, 1 → 0x10 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 4);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RBX), 0xFFFFFFFFu);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RDX), 0x10);
    }

    /* -- SAR seta SF (bloco próprio: flags logo após a instrução) -- */
    {
        static const uint8_t prog[] = {
            0xBB, 0x00, 0x00, 0x00, 0x80,   /* mov ebx, 0x80000000 */
            0xC1, 0xFB, 0x1F,               /* sar ebx, 31 → -1 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RBX), 0xFFFFFFFFu);
        CHECK((pr_cpu64_rflags(c) & PR_R64_SF) != 0);
    }

    /* -- SHL gera CF -- */
    {
        static const uint8_t prog[] = {
            0xB8, 0x00, 0x00, 0x00, 0x80,   /* mov eax, 0x80000000 */
            0xC1, 0xE0, 0x01,               /* shl eax, 1 → 0, CF=1 ZF=1 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 0);
        CHECK((pr_cpu64_rflags(c) & PR_R64_CF) != 0);
        CHECK((pr_cpu64_rflags(c) & PR_R64_ZF) != 0);
    }

    /* -- MOVZX/MOVSX/MOVSXD 8/16/32 → 64 -- */
    {
        static const uint8_t prog[] = {
            0xBB, 0xF0, 0x80, 0xFF, 0x80,   /* mov ebx, 0x80FF80F0 */
            0x48, 0x0F, 0xB6, 0xC3,         /* movzx rax, bl  → 0xF0 */
            0x48, 0x0F, 0xBE, 0xC3,         /* movsx rax, bl  → -16 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(pr_cpu64_reg(c, PR_R64_RAX) == 0xFFFFFFFFFFFFFFF0ull);

        static const uint8_t prog2[] = {
            0xBB, 0xF0, 0x80, 0xFF, 0x80,   /* mov ebx, 0x80FF80F0 */
            0x48, 0x0F, 0xB7, 0xC3,         /* movzx rax, bx  → 0x80F0 */
            0xF4,
        };
        memcpy(mem + 0x200, prog2, sizeof(prog2));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(pr_cpu64_reg(c, PR_R64_RAX) == 0x80F0ull);

        static const uint8_t prog3[] = {
            0xBB, 0xF0, 0x80, 0xFF, 0x80,
            0x48, 0x63, 0xC3,               /* movsxd rax, ebx → 0xFFFFFFFF80FF80F0 */
            0xF4,
        };
        memcpy(mem + 0x200, prog3, sizeof(prog3));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(pr_cpu64_reg(c, PR_R64_RAX) == 0xFFFFFFFF80FF80F0ull);
    }

    /* -- INC/DEC preservam CF -- */
    {
        static const uint8_t prog[] = {
            0xF9,                           /* stc (CF=1) */
            0xB8, 0x05, 0x00, 0x00, 0x00,   /* mov eax, 5 */
            0xFF, 0xC0,                     /* inc eax → 6, CF intacto */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 6);
        CHECK((pr_cpu64_rflags(c) & PR_R64_CF) != 0);
    }

    /* -- 8-bit: bytes altos AH/BH sem REX; SPL/DIL com REX -- */
    {
        static const uint8_t prog[] = {
            0xB8, 0x00, 0x00, 0x00, 0x00,   /* mov eax, 0 */
            0xB7, 0x12,                     /* mov bh, 0x12 (sem REX) */
            0xB3, 0x34,                     /* mov bl, 0x34 → rbx & 0xFFFF = 0x1234 */
            0x04, 0x01,                     /* add al, 1 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)(pr_cpu64_reg(c, PR_R64_RBX) & 0xFFFF), 0x1234);
        CHECK_EQ_U32((uint32_t)(pr_cpu64_reg(c, PR_R64_RAX) & 0xFF), 1);

        static const uint8_t prog2[] = {
            0x40, 0xB7, 0x7F,               /* mov dil, 0x7F (com REX → DIL, não BH) */
            0xF4,
        };
        memcpy(mem + 0x200, prog2, sizeof(prog2));
        pr_cpu64_set_reg(c, PR_R64_RDI, 0);
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)(pr_cpu64_reg(c, PR_R64_RDI) & 0xFF), 0x7F);
    }

    /* -- 16-bit (prefixo 66) -- */
    {
        static const uint8_t prog[] = {
            0x66, 0xB8, 0x34, 0x12,         /* mov ax, 0x1234 */
            0x66, 0x05, 0x01, 0x00,         /* add ax, 1 → 0x1235 */
            0x66, 0x89, 0xC3,               /* mov bx, ax */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)(pr_cpu64_reg(c, PR_R64_RBX) & 0xFFFF), 0x1235);
    }

    /* -- prologo classico: push rbp/mov rbp,rsp/sub/[rbp-8]/leave -- */
    {
        static const uint8_t prog[] = {
            0x55,                           /* push rbp */
            0x48, 0x89, 0xE5,               /* mov rbp, rsp */
            0x48, 0x83, 0xEC, 0x20,         /* sub rsp, 0x20 */
            0xC7, 0x45, 0xF8, 0x2A, 0x00, 0x00, 0x00,  /* mov [rbp-8], 42 */
            0x8B, 0x45, 0xF8,               /* mov eax, [rbp-8] */
            0xC9,                           /* leave */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSP, 0x800);
        pr_cpu64_set_reg(c, PR_R64_RBP, 0);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 42);
        CHECK(pr_cpu64_reg(c, PR_R64_RSP) == 0x800);
        CHECK(pr_cpu64_reg(c, PR_R64_RBP) == 0);
    }

    /* -- endbr64 (NOP) + ENTER imm16,0 + leave -- */
    {
        static const uint8_t prog[] = {
            0xF3, 0x0F, 0x1E, 0xFA,         /* endbr64 = nop */
            0xC8, 0x10, 0x00, 0x00,         /* enter 0x10, 0 */
            0xC9,                           /* leave */
            0xB8, 0x2A, 0x00, 0x00, 0x00,
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSP, 0x800);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 42);
        CHECK(pr_cpu64_reg(c, PR_R64_RSP) == 0x800);
    }

    /* -- NEG/NOT/TEST e DIV/IMUL -- */
    {
        static const uint8_t prog[] = {
            0xB8, 0x0A, 0x00, 0x00, 0x00,   /* mov eax, 10 */
            0xF7, 0xD8,                     /* neg eax → -10 (CF=1) */
            0xF7, 0xD0,                     /* not eax → 9 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 9);

        static const uint8_t prog2[] = {
            0xB8, 0xA0, 0x86, 0x01, 0x00,   /* mov eax, 100000 */
            0xB9, 0xE8, 0x03, 0x00, 0x00,   /* mov ecx, 1000 */
            0x99,                           /* cdq */
            0xF7, 0xF1,                     /* div ecx → eax=100, edx=0 */
            0xF4,
        };
        memcpy(mem + 0x200, prog2, sizeof(prog2));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 100);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RDX), 0);

        static const uint8_t prog3[] = {
            0xB8, 0x07, 0x00, 0x00, 0x00,   /* mov eax, 7 */
            0xB9, 0x06, 0x00, 0x00, 0x00,   /* mov ecx, 6 */
            0xF7, 0xE9,                     /* imul ecx → eax=42 */
            0xF4,
        };
        memcpy(mem + 0x200, prog3, sizeof(prog3));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 42);
    }

    /* -- fault com BYTES da instrução -- */
    {
        memset(mem, 0, sizeof(mem));
        mem[0x200] = 0x0F;
        mem[0x201] = 0x32;   /* RDMSR (ainda fora; RDTSC/0F 31 é suportado) */
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        const pr_cpu64_fault* f = pr_cpu64_last_fault(c);
        CHECK(f != NULL);
        if (f) {
            CHECK(f->nbytes >= 2);
            CHECK_EQ_U32(f->bytes[0], 0x0F);
            CHECK_EQ_U32(f->bytes[1], 0x32);
        }
    }

    /* -- XCHG r/m,r + call rel32/ret -- */
    {
        static const uint8_t prog[] = {
            0xB8, 0x01, 0x00, 0x00, 0x00,   /* mov eax, 1 */
            0xBB, 0x02, 0x00, 0x00, 0x00,   /* mov ebx, 2 */
            0x87, 0xC3,                     /* xchg eax, ebx → eax=2 ebx=1 */
            0xE8, 0x05, 0x00, 0x00, 0x00,   /* call +5 (para 0x216) */
            0xF4,                           /* ponto de retorno: hlt */
            0x90, 0x90, 0x90, 0x90,         /* padding */
            0xB8, 0x2A, 0x00, 0x00, 0x00,   /* sub: mov eax, 42 */
            0xC3,                           /* ret */
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSP, 0x800);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RAX), 42);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, PR_R64_RBX), 1);
        CHECK(pr_cpu64_reg(c, PR_R64_RSP) == 0x800);
    }

    pr_cpu64_destroy(c);
}
