/* Testes da expansao CRT do pr_cpu64: string ops (REP MOVS/STOS/LODS/SCAS/CMPS
 * com DF/RCX/tamanhos), SSE minimo (MOVUPS/MOVAPS/MOVDQA/MOVDQU/MOVD/MOVQ/
 * PXOR/XORPS) e comportamentos exigidos por compiladores reais (rep ret).
 * Todos os bytes sao inspecionaveis; fault honesto com opcode/RIP/bytes. */
#include "pt_util.h"

#include "portico/pr_cpu64.h"

#include <string.h>

static int ex_guard_all(void* ud, uint64_t addr, size_t len, int acc) {
    (void)ud; (void)addr; (void)len; (void)acc;
    return 1;
}

/* GRUPO 19: nega escrita em ex_g19_deny_at (classe: acesso negado) */
static uint64_t ex_g19_deny_at;
static int ex_g19_guard(void* ud, uint64_t addr, size_t len, int acc) {
    (void)ud; (void)len;
    if (acc == PR_CPU64_ACC_W && addr == ex_g19_deny_at) return 0;
    return 1;
}

void test_cpu64ext(void) {
    static uint8_t mem[8192];
    pr_cpu64* c = pr_cpu64_create_on(mem, (uint32_t)sizeof(mem));
    CHECK(c != NULL);
    pr_cpu64_set_guard(c, ex_guard_all, NULL);
    uint64_t n = 0;

    /* ================= FASE 1: REP MOVS/STOS ================= */

    /* -- REP MOVSB: 8 bytes, DF=0, regs atualizados, RCX=0, 1 instrucao -- */
    {
        static const uint8_t prog[] = { 0xF3, 0xA4, 0xF4 };   /* rep movsb; hlt */
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x100, "\x11\x22\x33\x44\x55\x66\x77\x88", 8);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x100);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x300);
        pr_cpu64_set_reg(c, PR_R64_RCX, 8);
        pr_cpu64_set_rflags(c, 0x202);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);                              /* rep + hlt = 2 passos */
        CHECK(memcmp(mem + 0x300, mem + 0x100, 8) == 0);
        CHECK(pr_cpu64_reg(c, PR_R64_RSI) == 0x108);
        CHECK(pr_cpu64_reg(c, PR_R64_RDI) == 0x308);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == 0);
    }

    /* -- REP MOVSW/MOVSD/MOVSQ: tamanho do elemento correto -- */
    {
        /* movsw rcx=2 | mov ecx,2 | movsd | mov ecx,2 | movsq | hlt
         * (REP consome RCX entao recarregamos entre cada um) */
        static const uint8_t prog[] = { 0x66, 0xF3, 0xA5,
                                        0xB9, 0x02, 0x00, 0x00, 0x00,
                                        0xF3, 0xA5,
                                        0xB9, 0x02, 0x00, 0x00, 0x00,
                                        0xF3, 0x48, 0xA5, 0xF4 };
        memset(mem, 0, sizeof(mem));
        for (int i = 0; i < 48; i++) mem[0x100 + i] = (uint8_t)(0x60 + i);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x100);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x300);
        pr_cpu64_set_reg(c, PR_R64_RCX, 2);
        pr_cpu64_set_rflags(c, 0x202);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 6);
        /* movsw: 2 elem x 2B = 4B; movsd: 2 x 4B = 8B; movsq: 2 x 8B = 16B */
        CHECK(pr_cpu64_reg(c, PR_R64_RSI) == 0x100 + 4 + 8 + 16);
        CHECK(pr_cpu64_reg(c, PR_R64_RDI) == 0x300 + 4 + 8 + 16);
        CHECK(memcmp(mem + 0x300, mem + 0x100, 28) == 0);
    }

    /* -- DF=1: REP MOVSB copia para tras e decrementa RSI/RDI -- */
    {
        static const uint8_t prog[] = { 0xFD, 0xF3, 0xA4, 0xF4 }; /* std; rep movsb */
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x100, "\xA1\xA2\xA3\xA4", 4);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x103);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x303);
        pr_cpu64_set_reg(c, PR_R64_RCX, 4);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(memcmp(mem + 0x300, mem + 0x100, 4) == 0);
        CHECK(pr_cpu64_reg(c, PR_R64_RSI) == 0x0FF);
        CHECK(pr_cpu64_reg(c, PR_R64_RDI) == 0x2FF);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == 0);
    }

    /* -- RCX=0: REP MOVSB/STOSB sao no-op completos -- */
    {
        static const uint8_t prog[] = { 0xF3, 0xA4, 0xF3, 0xAA, 0xF4 };
        memset(mem, 0, sizeof(mem));
        memset(mem + 0x300, 0xCC, 16);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x100);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x300);
        pr_cpu64_set_reg(c, PR_R64_RAX, 0xAB);
        pr_cpu64_set_reg(c, PR_R64_RCX, 0);
        pr_cpu64_set_rflags(c, 0x202);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(mem[0x300] == 0xCC);                  /* nada copiado/gravado */
        CHECK(pr_cpu64_reg(c, PR_R64_RSI) == 0x100);
        CHECK(pr_cpu64_reg(c, PR_R64_RDI) == 0x300);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == 0);
    }

    /* -- MOVSB sem prefixo = 1 elemento (RCX intacto) -- */
    {
        static const uint8_t prog[] = { 0xA4, 0xF4 };
        memset(mem, 0, sizeof(mem));
        mem[0x100] = 0x5A;
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x100);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x300);
        pr_cpu64_set_reg(c, PR_R64_RCX, 99);
        pr_cpu64_set_rflags(c, 0x202);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(mem[0x300] == 0x5A);
        CHECK(mem[0x301] == 0x00);
        CHECK(pr_cpu64_reg(c, PR_R64_RSI) == 0x101);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == 99);
    }

    /* -- REP STOSB/STOSW/STOSD/STOSQ: padroes + avanco -- */
    {
        /* stosb | mov ecx,2 | stosw | mov ecx,2 | stosd | mov ecx,2 | stosq */
        static const uint8_t prog[] = { 0xF3, 0xAA,
                                        0xB9, 0x02, 0x00, 0x00, 0x00, 0x66, 0xF3, 0xAB,
                                        0xB9, 0x02, 0x00, 0x00, 0x00, 0xF3, 0xAB,
                                        0xB9, 0x02, 0x00, 0x00, 0x00, 0xF3, 0x48, 0xAB,
                                        0xF4 };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x300);
        pr_cpu64_set_reg(c, PR_R64_RAX, 0x0102030405060708ull);
        pr_cpu64_set_reg(c, PR_R64_RCX, 2);
        pr_cpu64_set_rflags(c, 0x202);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 8);
        /* stosb x2: 08 08 | stosw x2 (ax=0x0708): 08 07 08 07
         * stosd x2 (eax=0x05060708) | stosq x2 (rax imm64 LE) */
        CHECK(mem[0x300] == 0x08);
        CHECK(mem[0x301] == 0x08);
        CHECK(mem[0x302] == 0x08);
        CHECK(mem[0x303] == 0x07);
        CHECK(mem[0x306] == 0x08);
        CHECK(mem[0x309] == 0x05);
        CHECK(mem[0x30D] == 0x05);
        CHECK(mem[0x30E] == 0x08);
        CHECK(mem[0x314] == 0x02);
        CHECK(mem[0x315] == 0x01);
        CHECK(pr_cpu64_reg(c, PR_R64_RDI) == 0x300 + 2 + 4 + 8 + 16);
    }

    /* -- STOS com DF=1: RDI anda para tras -- */
    {
        static const uint8_t prog[] = { 0xFD, 0xF3, 0xAB, 0xF4 };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x307);
        pr_cpu64_set_reg(c, PR_R64_RAX, 0x11223344);
        pr_cpu64_set_reg(c, PR_R64_RCX, 2);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        /* stosd com DF=1 a partir de 0x307: primeiro [0x307..0x30A],
         * depois rdi=0x303: [0x303..0x306] */
        CHECK(mem[0x307] == 0x44);
        CHECK(mem[0x303] == 0x44);
        CHECK(pr_cpu64_reg(c, PR_R64_RDI) == 0x2FF);
    }

    /* ================= FASE 2: LODS/SCAS/CMPS + rep ret ================= */

    /* -- REP LODSB: AL = ultimo byte lido, RSI avanca -- */
    {
        static const uint8_t prog[] = { 0xF3, 0xAC, 0xF4 };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x100, "\x01\x02\x03\x04", 4);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x100);
        pr_cpu64_set_reg(c, PR_R64_RCX, 3);
        pr_cpu64_set_rflags(c, 0x202);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK((pr_cpu64_reg(c, PR_R64_RAX) & 0xFF) == 0x03);
        CHECK(pr_cpu64_reg(c, PR_R64_RSI) == 0x103);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == 0);
    }

    /* -- REPNE SCASB (strlen classico): RCX=-1 -> RCX=-(len+2), ZF=1 -- */
    {
        static const uint8_t prog[] = { 0xF2, 0xAE, 0xF4 };  /* repne scasb */
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x100, "abc", 4);                       /* a b c \0 */
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RAX, 0);                  /* AL = 0 */
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x100);
        pr_cpu64_set_reg(c, PR_R64_RCX, ~0ull);
        pr_cpu64_set_rflags(c, 0x202);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(pr_cpu64_reg(c, PR_R64_RDI) == 0x104);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == ~0ull - 4);
        CHECK(pr_cpu64_rflags(c) & PR_R64_ZF);               /* achou o NUL */
    }

    /* -- REPE CMPSB: iguais (ZF=1, RCX=0) e diferentes (parada cedo) -- */
    {
        static const uint8_t prog[] = { 0xF3, 0xA6, 0xF4 };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x100, "abcd", 4);
        memcpy(mem + 0x180, "abcd", 4);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x100);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x180);
        pr_cpu64_set_reg(c, PR_R64_RCX, 4);
        pr_cpu64_set_rflags(c, 0x202);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == 0);
        CHECK(pr_cpu64_rflags(c) & PR_R64_ZF);
        /* agora diferente no 2o byte */
        mem[0x181] = 'X';
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x100);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x180);
        pr_cpu64_set_reg(c, PR_R64_RCX, 4);
        pr_cpu64_set_rflags(c, 0x202);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == 2);             /* restante */
        CHECK(!(pr_cpu64_rflags(c) & PR_R64_ZF));
    }

    /* -- REPNE CMPSB: para quando ZF=1 (byte igual) -- */
    {
        static const uint8_t prog[] = { 0xF2, 0xA6, 0xF4 };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x100, "aZcd", 4);
        memcpy(mem + 0x180, "aYcd", 4);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x100);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x180);
        pr_cpu64_set_reg(c, PR_R64_RCX, 4);
        pr_cpu64_set_rflags(c, 0x202);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == 3);             /* parou no 1o */
        CHECK(pr_cpu64_rflags(c) & PR_R64_ZF);
    }

    /* -- rep ret (F3 C3): RET real com prefixo inofensivo (MSVC/MinGW) -- */
    {
        /* 0x200: call 0x208 ; hlt | 0x208: mov eax,42 ; rep ret */
        static const uint8_t prog[] = {
            0xE8, 0x03, 0x00, 0x00, 0x00,   /* call +3 -> 0x208 */
            0xF4,
            0x90, 0x90,
            0xB8, 0x2A, 0x00, 0x00, 0x00,   /* mov eax,42 */
            0xF3, 0xC3,                     /* rep ret */
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSP, 0x800);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(pr_cpu64_reg(c, PR_R64_RAX) == 42);
        CHECK(pr_cpu64_halted(c));
    }

    /* ================= FASE 3: SSE minimo ================= */

    /* -- PXOR + MOVDQU store: 16 bytes zerados (memset fast-path) -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0xEF, 0xC0,         /* pxor xmm0,xmm0 */
            0xF3, 0x0F, 0x7F, 0x07,         /* movdqu [rdi],xmm0 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memset(mem + 0x300, 0x55, 16);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x300);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        for (int i = 0; i < 16; i++) CHECK(mem[0x300 + i] == 0);
    }

    /* -- MOVUPS load + XORPS consigo + MOVUPS store: zera destino -- */
    {
        static const uint8_t prog[] = {
            0x0F, 0x10, 0x06,               /* movups xmm0,[rsi] */
            0x0F, 0x57, 0xC0,               /* xorps xmm0,xmm0 */
            0x0F, 0x11, 0x07,               /* movups [rdi],xmm0 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        for (int i = 0; i < 16; i++) mem[0x400 + i] = (uint8_t)(i + 1);
        memset(mem + 0x300, 0xFF, 16);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x400);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x300);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(mem[0x300] == 0);
        CHECK(mem[0x30F] == 0);
    }

    /* -- MOVAPS alinhado (load+store) copia 16 bytes -- */
    {
        static const uint8_t prog[] = {
            0x0F, 0x28, 0x06,               /* movaps xmm0,[rsi] */
            0x0F, 0x29, 0x07,               /* movaps [rdi],xmm0 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        for (int i = 0; i < 16; i++) mem[0x310 + i] = (uint8_t)(0x80 + i);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x310);   /* 16-aligned */
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x390);   /* 16-aligned */
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(memcmp(mem + 0x390, mem + 0x310, 16) == 0);
    }

    /* -- MOVAPS nao-alinhado = fault honesto (opcode/RIP/bytes) -- */
    {
        static const uint8_t prog[] = { 0x0F, 0x29, 0x07, 0xF4 }; /* movaps [rdi],xmm0 */
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x394);   /* NAO alinhado */
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        const pr_cpu64_fault* f = pr_cpu64_last_fault(c);
        CHECK(f != NULL);
        CHECK(strstr(f->reason, "alinhamento") != NULL);
        CHECK(f->rip == 0x200);
        CHECK(f->opcode == 0x0F);
        CHECK(f->nbytes >= 2);
    }

    /* -- MOVDQU nao-alinhado funciona (mesmo endereco do caso acima) -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0xEF, 0xC0,         /* pxor xmm0,xmm0 */
            0xF3, 0x0F, 0x7F, 0x07,         /* movdqu [rdi],xmm0 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memset(mem + 0x394, 0x77, 16);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x394);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(mem[0x394] == 0);
        CHECK(mem[0x3A3] == 0);
    }

    /* -- MOVDQA alinhado: load/store + fault quando desalinhado -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0x6F, 0x06,         /* movdqa xmm0,[rsi] */
            0x66, 0x0F, 0x7F, 0x07,         /* movdqa [rdi],xmm0 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        for (int i = 0; i < 16; i++) mem[0x310 + i] = (uint8_t)(i + 1);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x310);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x3A0);   /* alinhado */
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(memcmp(mem + 0x3A0, mem + 0x310, 16) == 0);
        /* store desalinhado */
        pr_cpu64_set_rip(c, 0x200 + 4);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x3A2);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        CHECK(strstr(pr_cpu64_last_fault(c)->reason, "alinhamento") != NULL);
    }

    /* -- MOVD gpr->xmm + MOVQ m64<-xmm: low 32 zero-extended em 8 bytes -- */
    {
        static const uint8_t prog[] = {
            0xB8, 0x44, 0x33, 0x22, 0x11,   /* mov eax, 0x11223344 */
            0x66, 0x0F, 0x6E, 0xC0,         /* movd xmm0, eax */
            0x66, 0x0F, 0xD6, 0x07,         /* movq [rdi], xmm0 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x300);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(mem[0x300] == 0x44);
        CHECK(mem[0x303] == 0x11);
        CHECK(mem[0x304] == 0x00);          /* zero-extend */
        CHECK(mem[0x307] == 0x00);
    }

    /* -- MOVQ gpr64->xmm + MOVDQU store: low 8 = imm64, high 8 = 0 -- */
    {
        static const uint8_t prog[] = {
            0x48, 0xB8, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
            0x48, 0x66, 0x0F, 0x6E, 0xC0,   /* movq xmm0, rax */
            0xF3, 0x0F, 0x7F, 0x07,         /* movdqu [rdi], xmm0 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x300);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(mem[0x300] == 0x08);
        CHECK(mem[0x307] == 0x01);
        CHECK(mem[0x308] == 0x00);          /* alta zerada */
        CHECK(mem[0x30F] == 0x00);
    }

    /* -- MOVQ load (F3 0F 7E): copia low 8 e zera high 8 -- */
    {
        static const uint8_t prog[] = {
            0xF3, 0x0F, 0x7E, 0x06,         /* movq xmm0, [rsi] */
            0xF3, 0x0F, 0x7F, 0x07,         /* movdqu [rdi], xmm0 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x300, "\x10\x32\x54\x76\x98\xBA\xDC\xFE", 8);
        memset(mem + 0x308, 0xEE, 8);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x300);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x380);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(memcmp(mem + 0x380, mem + 0x300, 8) == 0);
        CHECK(mem[0x388] == 0x00);
    }

    /* -- MOVQ/MOVD xmm->gpr: rax/edx recebem low 64/32 -- */
    {
        static const uint8_t prog[] = {
            0xF3, 0x0F, 0x7E, 0x06,         /* movq xmm0, [rsi] */
            0x48, 0x66, 0x0F, 0x7E, 0xC0,   /* movq rax, xmm0 */
            0x66, 0x0F, 0x7E, 0xC2,         /* movd edx, xmm0 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x300, "\x88\x77\x66\x55\x44\x33\x22\x11", 8);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x300);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(pr_cpu64_reg(c, PR_R64_RAX) == 0x1122334455667788ull);
        CHECK(pr_cpu64_reg(c, PR_R64_RDX) == 0x55667788u);   /* zero-ext */
    }

    /* -- MOVQ reg-form (66 0F D6 C1): low 8 copia, high zera -- */
    {
        static const uint8_t prog[] = {
            0xF3, 0x0F, 0x7E, 0x06,         /* movq xmm0, [rsi] */
            0x66, 0x0F, 0xD6, 0xC8,         /* movq xmm1, xmm0 (low 8) */
            0xF3, 0x0F, 0x7F, 0x0F,         /* movdqu [rdi], xmm1 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x300, "\xCD\xAB\x90\x78\x56\x34\x12\xEF", 8);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x300);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x380);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(memcmp(mem + 0x380, mem + 0x300, 8) == 0);
        CHECK(mem[0x388] == 0x00);
    }

    /* -- XORPD (66 0F 57) tambem zera 128 bits -- */
    {
        static const uint8_t prog[] = {
            0x0F, 0x10, 0x06,               /* movups xmm0,[rsi] */
            0x66, 0x0F, 0x57, 0xC0,         /* xorpd xmm0,xmm0 */
            0xF3, 0x0F, 0x7F, 0x07,         /* movdqu [rdi],xmm0 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memset(mem + 0x400, 0xA5, 16);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSI, 0x400);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x380);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(mem[0x380] == 0);
        CHECK(mem[0x38F] == 0);
    }

    /* -- G88: instrucao SSE packed agora IMPLEMENTADA (ADDPS etc)
     *    Antes era fault; agora testamos ADDPS/ADDPD reais -- */
    {
        /* ADDPS xmm0,xmm1: 4x float32 */
        static const uint8_t prog[] = {
            0x0F, 0x58, 0xC1,               /* addps xmm0,xmm1 */
            0xF4,
        };
        float a[4]={1.0f,2.0f,3.0f,4.0f};
        float b[4]={10.0f,20.0f,30.0f,40.0f};
        float exp[4]={11.0f,22.0f,33.0f,44.0f};
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, a);
        pr_cpu64_set_xmm(c, 1, b);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        uint8_t got[16]; pr_cpu64_xmm(c, 0, got);
        float gotf[4]; memcpy(gotf, got, 16);
        CHECK(gotf[0]==exp[0] && gotf[1]==exp[1] && gotf[2]==exp[2] && gotf[3]==exp[3]);
    }
    {
        /* ADDPD xmm0,xmm1: 2x float64 (66 prefix) */
        static const uint8_t prog[] = {
            0x66, 0x0F, 0x58, 0xC1,
            0xF4,
        };
        double a[2]={1.5,2.5};
        double b[2]={10.0,20.0};
        double exp[2]={11.5,22.5};
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, a);
        pr_cpu64_set_xmm(c, 1, b);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        uint8_t got[16]; pr_cpu64_xmm(c, 0, got);
        double gotd[2]; memcpy(gotd, got, 16);
        CHECK(gotd[0]==exp[0] && gotd[1]==exp[1]);
    }
    {
        /* SUBPS + MULPS + DIVPS + SQRTPS */
        static const uint8_t prog_sub[] = { 0x0F, 0x5C, 0xC1, 0xF4 }; /* subps */
        static const uint8_t prog_mul[] = { 0x0F, 0x59, 0xC1, 0xF4 }; /* mulps */
        static const uint8_t prog_div[] = { 0x0F, 0x5E, 0xC1, 0xF4 }; /* divps */
        static const uint8_t prog_sqrt[] = { 0x0F, 0x51, 0xC1, 0xF4 }; /* sqrtps */
        float a[4]={10,20,30,40};
        float b[4]={1,2,3,4};
        float exp_sub[4]={9,18,27,36};
        float exp_mul[4]={10,40,90,160};
        float exp_div[4]={10,10,10,10};
        float exp_sqrt[4]={2,3,4,5};
        float src_sqrt[4]={4,9,16,25};
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog_sub, sizeof(prog_sub));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, a); pr_cpu64_set_xmm(c, 1, b);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        uint8_t got[16]; pr_cpu64_xmm(c, 0, got);
        float gf[4]; memcpy(gf, got, 16);
        CHECK(gf[0]==exp_sub[0] && gf[1]==exp_sub[1]);
        memcpy(mem + 0x200, prog_mul, sizeof(prog_mul));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, a); pr_cpu64_set_xmm(c, 1, b);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        pr_cpu64_xmm(c, 0, got); memcpy(gf, got, 16);
        CHECK(gf[0]==exp_mul[0] && gf[1]==exp_mul[1]);
        memcpy(mem + 0x200, prog_div, sizeof(prog_div));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, a); pr_cpu64_set_xmm(c, 1, b);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        pr_cpu64_xmm(c, 0, got); memcpy(gf, got, 16);
        CHECK(gf[0]==exp_div[0]);
        memcpy(mem + 0x200, prog_sqrt, sizeof(prog_sqrt));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 1, src_sqrt);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        pr_cpu64_xmm(c, 0, got); memcpy(gf, got, 16);
        CHECK(gf[0]==exp_sqrt[0] && gf[1]==exp_sqrt[1] && gf[2]==exp_sqrt[2] && gf[3]==exp_sqrt[3]);
    }
    {
        /* ANDPS / ORPS */
        static const uint8_t prog_and[] = { 0x0F, 0x54, 0xC1, 0xF4 };
        static const uint8_t prog_or[]  = { 0x0F, 0x56, 0xC1, 0xF4 };
        uint8_t a[16]={0xFF,0x0F,0xF0,0x00, 0xAA,0x55,0xCC,0x33, 0xFF,0xFF,0x00,0x00, 0x12,0x34,0x56,0x78};
        uint8_t b[16]={0x0F,0xFF,0x0F,0xFF, 0x55,0xAA,0x33,0xCC, 0x00,0xFF,0xFF,0x00, 0xFF,0xFF,0xFF,0xFF};
        uint8_t exp_and[16]={0x0F,0x0F,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0xFF,0x00,0x00, 0x12,0x34,0x56,0x78};
        uint8_t exp_or[16]={0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0xFF, 0xFF,0xFF,0xFF,0x00, 0xFF,0xFF,0xFF,0xFF};
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog_and, sizeof(prog_and));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, a); pr_cpu64_set_xmm(c, 1, b);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        uint8_t got[16]; pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, exp_and, 16)==0);
        memcpy(mem + 0x200, prog_or, sizeof(prog_or));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, a); pr_cpu64_set_xmm(c, 1, b);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, exp_or, 16)==0);
    }
    {
        /* ainda fora do subconjunto: ANDPS com prefixo invalido F3 deve fault */
        static const uint8_t prog[] = { 0xF3, 0x0F, 0x54, 0xC1, 0xF4 };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
    }

    /* -- CVTSI2SS (F3 [REX.W] 0F 2A): int->float32; alto 127:32 PRESERVADO
     * (Intel SDM) — exigido por hello_gl6.exe ((float)strlen/255) -- */
    {
        static const uint8_t prog[] = {
            0x48, 0xC7, 0xC0, 0x18, 0x00, 0x00, 0x00,  /* mov rax, 24 */
            0xB9, 0xE8, 0x03, 0x00, 0x00,              /* mov ecx, 1000 */
            0x48, 0xC7, 0xC6, 0x00, 0x01, 0x00, 0x00,  /* mov rsi, 0x100 */
            0x48, 0xC7, 0xC7, 0x00, 0x03, 0x00, 0x00,  /* mov rdi, 0x300 */
            0x48, 0xC7, 0xC2, 0x00, 0x04, 0x00, 0x00,  /* mov rdx, 0x400 */
            0xF3, 0x0F, 0x6F, 0x06,                    /* movdqu xmm0, [rsi] */
            0xF3, 0x48, 0x0F, 0x2A, 0xC0,              /* cvtsi2ss xmm0, rax */
            0xF3, 0x0F, 0x2A, 0xC9,                    /* cvtsi2ss xmm1, ecx */
            0xF3, 0x0F, 0x7F, 0x07,                    /* movdqu [rdi], xmm0 */
            0xF3, 0x0F, 0x7F, 0x0A,                    /* movdqu [rdx], xmm1 */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        for (int i = 0; i < 16; i++) mem[0x100 + i] = (uint8_t)(0xA0 + i);
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        /* baixo 32 = (float)24 = 24.0f = 0x41C00000 */
        uint32_t lo;
        memcpy(&lo, mem + 0x300, 4);
        CHECK(lo == 0x41C00000u);
        /* alto 127:32 PRESERVADO byte a byte */
        CHECK(mem[0x304] == 0xA4 && mem[0x305] == 0xA5);
        CHECK(mem[0x30E] == 0xAE && mem[0x30F] == 0xAF);
        /* forma 32 bits: (float)1000 = 1000.0f = 0x447A0000 */
        memcpy(&lo, mem + 0x400, 4);
        CHECK(lo == 0x447A0000u);
    }

    /* ================= GRUPO 19: MOVLPS/MOVLPD store (0F 13) =============
     * Forma store: grava os 64 bits BAIXOS do XMM em m64 (endereço efetivo
     * do convidado, 8 bytes, little-endian). Forma load (0F 12) continua
     * fora do subconjunto. */

    /* -- 1) store correto + little-endian + endereço válido: a sequência
     *    EXATA observada no PE: movlps [rsp+0x20], xmm1 (0F 13 4C 24 20) --
     */
    {
        static const uint8_t prog[] = {
            0x0F, 0x13, 0x4C, 0x24, 0x20,   /* movlps [rsp+0x20], xmm1 */
            0xF4,
        };
        static const uint8_t pat[16] = {
            0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11,
            0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7
        };
        uint8_t before_xmm[16], other_xmm[16];
        memset(mem, 0, sizeof(mem));
        memset(mem + 0x428, 0x5A, 8);          /* sentinel: NAO pode mudar */
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSP, 0x400);
        pr_cpu64_set_reg(c, PR_R64_RAX, 0x1111222233334444ull);
        pr_cpu64_set_reg(c, PR_R64_RCX, 0x5555666677778888ull);
        pr_cpu64_set_xmm(c, 1, pat);
        pr_cpu64_xmm(c, 1, before_xmm);
        pr_cpu64_set_xmm(c, 2, pat);
        pr_cpu64_xmm(c, 2, other_xmm);
        pr_cpu64_set_rflags(c, 0x202 | 0x1 | 0x40);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);                            /* movlps + hlt */
        /* 64 bits baixos gravados byte a byte (little-endian = cópia) */
        CHECK(memcmp(mem + 0x420, pat, 8) == 0);
        /* exatamente 8 bytes: os 8 seguintes intactos */
        for (int i = 0; i < 8; i++) CHECK(mem[0x428 + i] == 0x5A);
        /* preservação: XMM fonte NÃO muda (nem os 64 bits superiores) */
        uint8_t after_xmm[16];
        pr_cpu64_xmm(c, 1, after_xmm);
        CHECK(memcmp(after_xmm, before_xmm, 16) == 0);
        CHECK(memcmp(after_xmm + 8, pat + 8, 8) == 0);
        /* ausência de corrupção: outros XMM e GPRs/RFLAGS intactos */
        pr_cpu64_xmm(c, 2, after_xmm);
        CHECK(memcmp(after_xmm, other_xmm, 16) == 0);
        CHECK(pr_cpu64_reg(c, PR_R64_RAX) == 0x1111222233334444ull);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == 0x5555666677778888ull);
        CHECK(pr_cpu64_reg(c, PR_R64_RSP) == 0x400);
        CHECK(pr_cpu64_rflags(c) == (0x202 | 0x1 | 0x40));
    }

    /* -- 2) registradores XMM relevantes: xmm0, xmm9 (REX.R) e xmm15 --
     *    + 3 bit patterns distintos (zeros, todos 1s, floats do PE) */
    {
        static const struct { uint8_t p[16]; } pats[3] = {
            {{ 0 }},
            {{ 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
               0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF }},
            /* mdif do PE: {0.5f, 1.0f, r, g} = 3F000000 3F800000 ... */
            {{ 0x00,0x00,0x00,0x3F, 0x00,0x00,0x80,0x3F,
               0x00,0x00,0x80,0x3E, 0x00,0x00,0x00,0x3F }}
        };
        static const uint8_t movlps_xmm0[4]  = { 0x0F, 0x13, 0x07, 0xF4 }; /* [rdi],xmm0 */
        static const uint8_t movlps_xmm9[5]  = { 0x44, 0x0F, 0x13, 0x4C, 0x24 }; /* [rsp+d8],xmm9 */
        static const uint8_t movlps_xmm15[5] = { 0x44, 0x0F, 0x13, 0x7C, 0x24 }; /* [rsp+d8],xmm15 */
        for (int t = 0; t < 3; t++) {
            /* xmm0 */
            memset(mem, 0, sizeof(mem));
            memcpy(mem + 0x200, movlps_xmm0, sizeof(movlps_xmm0));
            pr_cpu64_set_rip(c, 0x200);
            pr_cpu64_set_reg(c, PR_R64_RDI, 0x500);
            pr_cpu64_set_xmm(c, 0, pats[t].p);
            CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
            CHECK(memcmp(mem + 0x500, pats[t].p, 8) == 0);
            /* xmm9 com disp8 */
            memset(mem, 0, sizeof(mem));
            memcpy(mem + 0x200, movlps_xmm9, sizeof(movlps_xmm9));
            mem[0x205] = 0x20;                    /* disp8 */
            mem[0x206] = 0xF4;
            pr_cpu64_set_rip(c, 0x200);
            pr_cpu64_set_reg(c, PR_R64_RSP, 0x400);
            pr_cpu64_set_xmm(c, 9, pats[t].p);
            CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
            CHECK(memcmp(mem + 0x420, pats[t].p, 8) == 0);
            /* xmm15 */
            memset(mem, 0, sizeof(mem));
            memcpy(mem + 0x200, movlps_xmm15, sizeof(movlps_xmm15));
            mem[0x205] = 0x20;
            mem[0x206] = 0xF4;
            pr_cpu64_set_rip(c, 0x200);
            pr_cpu64_set_reg(c, PR_R64_RSP, 0x400);
            pr_cpu64_set_xmm(c, 15, pats[t].p);
            CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
            CHECK(memcmp(mem + 0x420, pats[t].p, 8) == 0);
        }
    }

    /* -- 3) MOVLPD (66 0F 13): mesmo store dos 64 bits baixos -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0x13, 0x4C, 0x24, 0x20, /* movlpd [rsp+0x20], xmm1 */
            0xF4,
        };
        static const uint8_t pat[16] = {
            0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x80, 0x3F,
            0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RSP, 0x400);
        pr_cpu64_set_xmm(c, 1, pat);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(memcmp(mem + 0x420, pat, 8) == 0);
    }

    /* -- 4) memória exatamente suficiente: store no fim do espaço (8 de 8) -- */
    {
        static const uint8_t prog[] = {
            0x0F, 0x13, 0x07, 0xF4,               /* movlps [rdi], xmm0 */
        };
        static const uint8_t pat[16] = {
            0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
            0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99, 0x99
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDI, 8192 - 8);
        pr_cpu64_set_xmm(c, 0, pat);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(memcmp(mem + 8192 - 8, pat, 8) == 0);
    }

    /* -- 5) endereço inválido (além do espaço): fault honesto com addr -- */
    {
        static const uint8_t prog[] = {
            0x0F, 0x13, 0x07, 0xF4,               /* movlps [rdi], xmm0 */
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDI, 8192);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        const pr_cpu64_fault* f = pr_cpu64_last_fault(c);
        CHECK(strstr(f->reason, "fora do espaco") != NULL);
        CHECK(f->addr == 8192);
        CHECK(f->bytes[0] == 0x0F && f->bytes[1] == 0x13);
    }

    /* -- 6) atravessa o limite da memória (4 de 8 bytes dentro): fault -- */
    {
        static const uint8_t prog[] = {
            0x0F, 0x13, 0x07, 0xF4,               /* movlps [rdi], xmm0 */
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDI, 8192 - 4);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        const pr_cpu64_fault* f = pr_cpu64_last_fault(c);
        CHECK(strstr(f->reason, "fora do espaco") != NULL);
    }

    /* -- 7) escrita negada pelo guard de páginas: fault honesto -- */
    {
        static const uint8_t prog[] = {
            0x0F, 0x13, 0x07, 0xF4,               /* movlps [rdi], xmm0 */
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        ex_g19_deny_at = 0x600;
        pr_cpu64_set_guard(c, ex_g19_guard, NULL);
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x600);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        const pr_cpu64_fault* f = pr_cpu64_last_fault(c);
        /* diagnóstico do caminho de escrita (FAULT_ADDR): escrita negada */
        CHECK(strstr(f->reason, "escrita negada") != NULL);
        CHECK(f->addr == 0x600);
        pr_cpu64_set_guard(c, ex_guard_all, NULL);
    }

    /* -- 8) forma registrador (0F 13 C1) e' encodificação inválida: fault -- */
    {
        static const uint8_t prog[] = {
            0x0F, 0x13, 0xC1,                     /* movlps xmm1?? (mod=11) */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        const pr_cpu64_fault* f = pr_cpu64_last_fault(c);
        CHECK(strstr(f->reason, "memoria") != NULL);
    }

    /* -- 9) prefixo inválido (F3 0F 13) continua fora do subconjunto -- */
    {
        static const uint8_t prog[] = {
            0xF3, 0x0F, 0x13, 0x07,
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDI, 0x500);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        const pr_cpu64_fault* f = pr_cpu64_last_fault(c);
        CHECK(strstr(f->reason, "prefixo") != NULL);
    }


    /* ================= GRUPO 23: PSRLW xmm, imm8 (66 0F 71 /2 ib) =========
     * Descoberto por execucao de hello_gl12.exe (STOP "instrucao fora do
     * subconjunto x64 (opcode 0F 71)" em gen_tex, VA 0x1400029c6). Packed
     * shift right LOGICAL word: 8 lanes unsigned de 16 bits, cada uma >> imm8;
     * imm8 >= 16 zera a lane. Nao mexe em flags, MXCSR, outros XMM nem GPRs. */

    /* -- 1) encodacao EXATA do hello_gl12: psrlw $1, %xmm0 (66 0F 71 D0 01);
     *    lanes multiplas + valores 0x0000/0x0001/0xFFFF/intermediarios -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0x71, 0xD0, 0x01,   /* psrlw $1, %xmm0 */
            0xF4,
        };
        /* lanes LE: [0]=0xFFFF [1]=0x0000 [2]=0x0001 [3]=0x8000
         *           [4]=0x1234 [5]=0x7FFF [6]=0x0003 [7]=0xFFFE */
        static const uint8_t inp[16] = {
            0xFF,0xFF,  0x00,0x00,  0x01,0x00,  0x00,0x80,
            0x34,0x12,  0xFF,0x7F,  0x03,0x00,  0xFE,0xFF,
        };
        /* >>1 logico: FFFF->7FFF 0000->0000 0001->0000 8000->4000
         *            1234->091A 7FFF->3FFF 0003->0001 FFFE->7FFF */
        static const uint8_t exp[16] = {
            0xFF,0x7F,  0x00,0x00,  0x00,0x00,  0x00,0x40,
            0x1A,0x09,  0xFF,0x3F,  0x01,0x00,  0xFF,0x7F,
        };
        uint8_t got[16], save1[16], other[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RAX, 0x1111222233334444ull);
        pr_cpu64_set_reg(c, PR_R64_RCX, 0x5555666677778888ull);
        pr_cpu64_set_xmm(c, 0, inp);
        pr_cpu64_set_xmm(c, 1, inp);
        pr_cpu64_xmm(c, 1, save1);
        pr_cpu64_set_xmm(c, 7, inp);
        pr_cpu64_xmm(c, 7, other);
        pr_cpu64_set_rflags(c, 0x202 | 0x1 | 0x40 | 0x80);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);                          /* psrlw + hlt: SÓ o necessario */
        pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, exp, 16) == 0);       /* 8 lanes, shift 1 */
        /* estado indevido: demais XMM, GPRs e RFLAGS intactos */
        pr_cpu64_xmm(c, 1, got);
        CHECK(memcmp(got, save1, 16) == 0);
        pr_cpu64_xmm(c, 7, got);
        CHECK(memcmp(got, other, 16) == 0);
        CHECK(pr_cpu64_reg(c, PR_R64_RAX) == 0x1111222233334444ull);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == 0x5555666677778888ull);
        CHECK(pr_cpu64_rflags(c) == (0x202 | 0x1 | 0x40 | 0x80));
    }

    /* -- 2) shift 0 = identidade exata (todas as lanes preservadas) -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0x71, 0xD0, 0x00,   /* psrlw $0, %xmm0 */
            0xF4,
        };
        static const uint8_t inp[16] = {
            0xFF,0xFF,  0x00,0x00,  0x01,0x00,  0x00,0x80,
            0x34,0x12,  0xFF,0x7F,  0x03,0x00,  0xFE,0xFF,
        };
        uint8_t got[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, inp);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);
        pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, inp, 16) == 0);
    }

    /* -- 3) shift intermediario ($4) + 0x0001/0xFFFF -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0x71, 0xD3, 0x04,   /* psrlw $4, %xmm3 */
            0xF4,
        };
        static const uint8_t inp[16] = {
            0x11,0x11,  0xFF,0xFF,  0x01,0x00,  0xFF,0x0F,
            0x00,0x10,  0x88,0x88,  0x09,0x00,  0x00,0xF0,
        };
        /* >>4: 1111->0111 FFFF->0FFF 0001->0000 0FFF->00FF
         *     1000->0100 8888->0888 0009->0000 F000->0F00 */
        static const uint8_t exp[16] = {
            0x11,0x01,  0xFF,0x0F,  0x00,0x00,  0xFF,0x00,
            0x00,0x01,  0x88,0x08,  0x00,0x00,  0x00,0x0F,
        };
        uint8_t got[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 3, inp);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);
        pr_cpu64_xmm(c, 3, got);
        CHECK(memcmp(got, exp, 16) == 0);
    }

    /* -- 4) shift 15 (maximo sem zerar): 0xFFFF->0x0001, 0x8000->0x0001,
     *    0x0001->0x0000, 0xC000->0x0001 -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0x71, 0xD2, 0x0F,   /* psrlw $15, %xmm2 */
            0xF4,
        };
        static const uint8_t inp[16] = {
            0xFF,0xFF,  0x00,0x80,  0x01,0x00,  0x00,0xC0,
            0x00,0x00,  0xFF,0xFF,  0x00,0x40,  0xFF,0x7F,
        };
        static const uint8_t exp[16] = {
            0x01,0x00,  0x01,0x00,  0x00,0x00,  0x01,0x00,
            0x00,0x00,  0x01,0x00,  0x00,0x00,  0x00,0x00,
        };
        uint8_t got[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 2, inp);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);
        pr_cpu64_xmm(c, 2, got);
        CHECK(memcmp(got, exp, 16) == 0);
    }

    /* -- 5) shift >= 16 zera todas as lanes ($16 e $255; SDM) -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0x71, 0xD0, 0x10,   /* psrlw $16, %xmm0 */
            0x66, 0x0F, 0x71, 0xD4, 0xFF,   /* psrlw $255, %xmm4 */
            0xF4,
        };
        static const uint8_t inp[16] = {
            0xFF,0xFF,  0x00,0x00,  0x01,0x00,  0x00,0x80,
            0x34,0x12,  0xFF,0x7F,  0x03,0x00,  0xFE,0xFF,
        };
        static const uint8_t zeros[16] = { 0 };
        uint8_t got[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, inp);
        pr_cpu64_set_xmm(c, 4, inp);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 3);                          /* 2x psrlw + hlt */
        pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, zeros, 16) == 0);
        pr_cpu64_xmm(c, 4, got);
        CHECK(memcmp(got, zeros, 16) == 0);
    }

    /* -- 6) XMM alto via REX.B: psrlw $3, %xmm11 (66 41 0F 71 D3 03) -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x41, 0x0F, 0x71, 0xD3, 0x03,
            0xF4,
        };
        static const uint8_t inp[16] = {
            0xFF,0xFF,  0x08,0x08,  0x00,0x01,  0x00,0x00,
            0x55,0x55,  0xAA,0xAA,  0x03,0x00,  0xFF,0x00,
        };
        /* >>3: FFFF->1FFF 0808->0101 0100->0020 0000->0000
         *     5555->0AAA AAAA->1555 0003->0000 00FF->001F */
        static const uint8_t exp[16] = {
            0xFF,0x1F,  0x01,0x01,  0x20,0x00,  0x00,0x00,
            0xAA,0x0A,  0x55,0x15,  0x00,0x00,  0x1F,0x00,
        };
        uint8_t got[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 11, inp);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);
        pr_cpu64_xmm(c, 11, got);
        CHECK(memcmp(got, exp, 16) == 0);
    }

    /* -- 7) SOMENTE /2: /4 (psraw) e /6 (psllw) continuam FORA (fault
     *    honesto) — o grupo 0F 71 nao virou suporte generico -- */
    {
        static const uint8_t psraw[5] = { 0x66, 0x0F, 0x71, 0xE0, 0x01 };
        static const uint8_t psllw[5] = { 0x66, 0x0F, 0x71, 0xF0, 0x01 };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, psraw, sizeof(psraw));
        mem[0x205] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        CHECK(strstr(pr_cpu64_last_fault(c)->reason, "/2") != NULL);
        memcpy(mem + 0x200, psllw, sizeof(psllw));
        mem[0x205] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        CHECK(strstr(pr_cpu64_last_fault(c)->reason, "/2") != NULL);
    }

    /* -- 8) forma memoria invalida (66 0F 71 /2 com mod!=11): fault -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0x71, 0x10, 0x01,   /* psrlw $1, [rax]?? */
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RAX, 0x300);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        CHECK(strstr(pr_cpu64_last_fault(c)->reason, "registrador") != NULL);
    }

    /* -- 9) sem o prefixo 66 (0F 71 D0 01) continua fora do subconjunto -- */
    {
        static const uint8_t prog[] = {
            0x0F, 0x71, 0xD0, 0x01,
            0xF4,
        };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
    }

    /* ================= GRUPO 24: PAND xmm, xmm (66 0F DB /r) ================
     * Descoberto por execucao de hello_gl12.exe (STOP "instrucao fora do
     * subconjunto x64 (opcode 0F DB)" em gen_tex, VA 0x1400029cb). Packed
     * bitwise AND de 128 bits: destino (ModRM.reg) &= fonte (r/m), byte a
     * byte. SOMENTE forma registrador-registrador (mod=11). PADDB (0F FC) e
     * forma memoria continuam fora do subconjunto. Nao mexe em flags, MXCSR,
     * outros XMM nem GPRs. */

    /* -- 1) encoding EXATO do hello_gl12: pand %xmm2,%xmm0 (66 0F DB C2);
     *    AND 128-bit completo nos 16 bytes (zero/all-ones/alternado/misto);
     *    fonte, demais XMM, GPRs e RFLAGS intactos; n==2 -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0xDB, 0xC2,   /* pand %xmm2,%xmm0 */
            0xF4,
        };
        static const uint8_t dst[16] = {
            0x00,0xFF,0x55,0xAA,  0xF0,0x0F,0x12,0x34,
            0x80,0x01,0xFE,0x7F,  0xC3,0x3C,0x99,0x66,
        };
        static const uint8_t src1[16] = {
            0xFF,0xFF,0x55,0x55,  0x0F,0xF0,0x34,0x12,
            0x01,0x80,0x7F,0xFE,  0x3C,0xC3,0x66,0x99,
        };
        static const uint8_t exp[16] = {
            0x00,0xFF,0x55,0x00,  0x00,0x00,0x10,0x10,
            0x00,0x00,0x7E,0x7E,  0x00,0x00,0x00,0x00,
        };
        uint8_t got[16], save2[16], other[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RAX, 0x1111222233334444ull);
        pr_cpu64_set_reg(c, PR_R64_RCX, 0x5555666677778888ull);
        pr_cpu64_set_xmm(c, 0, dst);
        pr_cpu64_set_xmm(c, 2, src1);
        pr_cpu64_xmm(c, 2, save2);
        pr_cpu64_set_xmm(c, 7, src1);
        pr_cpu64_xmm(c, 7, other);
        pr_cpu64_set_rflags(c, 0x202 | 0x1 | 0x40 | 0x80);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);                          /* pand + hlt: SÓ o necessario */
        pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, exp, 16) == 0);       /* destino = dst AND src */
        pr_cpu64_xmm(c, 2, got);
        CHECK(memcmp(got, save2, 16) == 0);     /* fonte intacta */
        pr_cpu64_xmm(c, 7, got);
        CHECK(memcmp(got, other, 16) == 0);     /* demais XMM intactos */
        CHECK(pr_cpu64_reg(c, PR_R64_RAX) == 0x1111222233334444ull);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == 0x5555666677778888ull);
        CHECK(pr_cpu64_rflags(c) == (0x202 | 0x1 | 0x40 | 0x80));
    }

    /* -- 2) valores zero e all-ones nos dois sentidos -> zero -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0xDB, 0xC2,   /* pand %xmm2,%xmm0 (zeros &= ones) */
            0x66, 0x0F, 0xDB, 0xCB,   /* pand %xmm3,%xmm1 (ones &= zeros) */
            0xF4,
        };
        static const uint8_t zeros[16] = { 0 };
        static const uint8_t ones[16] = {
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        };
        uint8_t got[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, zeros);
        pr_cpu64_set_xmm(c, 2, ones);
        pr_cpu64_set_xmm(c, 1, ones);
        pr_cpu64_set_xmm(c, 3, zeros);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 3);                          /* 2x pand + hlt */
        pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, zeros, 16) == 0);
        pr_cpu64_xmm(c, 1, got);
        CHECK(memcmp(got, zeros, 16) == 0);
    }

    /* -- 3) padroes alternados: AA&55=00, AA&AA=AA, FF&55=55 -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0xDB, 0xC2,   /* pand %xmm2,%xmm0 */
            0x66, 0x0F, 0xDB, 0xCB,   /* pand %xmm3,%xmm1 */
            0x66, 0x0F, 0xDB, 0xEC,   /* pand %xmm4,%xmm5 */
            0xF4,
        };
        static const uint8_t AA[16] = {
            0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
            0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
        };
        static const uint8_t F5[16] = {
            0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
            0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
        };
        static const uint8_t FF[16] = {
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        };
        static const uint8_t z00[16] = { 0 };
        uint8_t got[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, AA);
        pr_cpu64_set_xmm(c, 2, F5);
        pr_cpu64_set_xmm(c, 1, AA);
        pr_cpu64_set_xmm(c, 3, AA);
        pr_cpu64_set_xmm(c, 5, FF);
        pr_cpu64_set_xmm(c, 4, F5);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 4);                          /* 3x pand + hlt */
        pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, z00, 16) == 0);       /* AA & 55 = 00 */
        pr_cpu64_xmm(c, 1, got);
        CHECK(memcmp(got, AA, 16) == 0);        /* AA & AA = AA */
        pr_cpu64_xmm(c, 5, got);
        CHECK(memcmp(got, F5, 16) == 0);        /* FF & 55 = 55 */
    }

    /* -- 4) XMMs altos via REX.R+REX.B: pand %xmm11,%xmm8 (66 45 0F DB C3) -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x45, 0x0F, 0xDB, 0xC3,
            0xF4,
        };
        static const uint8_t dst[16] = {
            0xFF,0x0F,0xF0,0x33,  0x00,0xFF,0x80,0x01,
            0xAA,0x55,0xC3,0x3C,  0x7E,0x81,0x12,0x34,
        };
        static const uint8_t src1[16] = {
            0xFF,0xFF,0xFF,0xFF,  0xFF,0x0F,0xFF,0xFF,
            0x0F,0xFF,0xFF,0x00,  0xFF,0xFF,0xFF,0xFF,
        };
        static const uint8_t exp[16] = {
            0xFF,0x0F,0xF0,0x33,  0x00,0x0F,0x80,0x01,
            0x0A,0x55,0xC3,0x00,  0x7E,0x81,0x12,0x34,
        };
        uint8_t got[16], save11[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 8, dst);
        pr_cpu64_set_xmm(c, 11, src1);
        pr_cpu64_xmm(c, 11, save11);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);
        pr_cpu64_xmm(c, 8, got);
        CHECK(memcmp(got, exp, 16) == 0);
        pr_cpu64_xmm(c, 11, got);
        CHECK(memcmp(got, save11, 16) == 0);    /* fonte intacta */
    }

    /* -- 5) destino == fonte (pand %xmm0,%xmm0 = 66 0F DB C0): valor
     *    preservado (AND idempotente), sem corrupcao de alias -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0xDB, 0xC0,
            0xF4,
        };
        static const uint8_t inp[16] = {
            0x37,0x91,0x00,0xFF,  0xAA,0x55,0x80,0x01,
            0xFE,0x7F,0x12,0x34,  0xC3,0x3C,0x99,0x66,
        };
        uint8_t got[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, inp);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);
        pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, inp, 16) == 0);
    }

    /* -- 6) formas NAO suportadas: memoria (mod!=11), sem prefixo 66 e
     *    irma packed nao implementada PADDW (66 0F FD C1) continuam fora —
     *    fault honesto; nada de implementacao antecipada. (PADDB saiu deste
     *    bloco no G25: e' o blocker implementado naquele grupo; a familia
     *    toda continua coberta pelas negativas do G25.) -- */
    {
        static const uint8_t pand_mem[4] = { 0x66, 0x0F, 0xDB, 0x02 };
        static const uint8_t pand_nop66[3] = { 0x0F, 0xDB, 0xC2 };
        static const uint8_t paddw[4] = { 0x66, 0x0F, 0xFD, 0xC1 };
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, pand_mem, sizeof(pand_mem));
        mem[0x204] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RDX, 0x300);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        CHECK(strstr(pr_cpu64_last_fault(c)->reason, "registrador") != NULL);
        memcpy(mem + 0x200, pand_nop66, sizeof(pand_nop66));
        mem[0x203] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        memcpy(mem + 0x200, paddw, sizeof(paddw));
        mem[0x204] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
    }

    /* ================= GRUPO 25: PADDB xmm, xmm (66 0F FC /r) ===============
     * Descoberto por execucao de hello_gl12.exe (STOP "instrucao fora do
     * subconjunto x64 (opcode 0F FC)" em gen_tex, VA 0x1400029cf). Packed
     * byte add MODULAR de 8 bits: 16 lanes independentes, destino += fonte,
     * truncado em 8 bits (0xFF+0x01=0x00), SEM carry entre lanes. SOMENTE
     * forma registrador-registrador (mod=11). PADDW/PADDD/PADDQ/PSUBB/PANDN/
     * POR e forma memoria continuam fora do subconjunto. Nao mexe em flags,
     * MXCSR, outros XMM nem GPRs. */

    /* -- 1) encoding EXATO do hello_gl12: paddb %xmm1,%xmm0 (66 0F FC C1);
     *    soma SEM overflow (0x10+0x20=0x30) nas 16 lanes;
     *    fonte, demais XMM, GPRs e RFLAGS intactos; n==2 -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0xFC, 0xC1,   /* paddb %xmm1,%xmm0 */
            0xF4,
        };
        static const uint8_t dst[16] = {
            0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
            0x10,0x10,0x10,0x10,0x10,0x10,0x10,0x10,
        };
        static const uint8_t src1[16] = {
            0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
            0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
        };
        static const uint8_t exp[16] = {
            0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,
            0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,
        };
        uint8_t got[16], save1[16], other[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_reg(c, PR_R64_RAX, 0x1111222233334444ull);
        pr_cpu64_set_reg(c, PR_R64_RCX, 0x5555666677778888ull);
        pr_cpu64_set_xmm(c, 0, dst);
        pr_cpu64_set_xmm(c, 1, src1);
        pr_cpu64_xmm(c, 1, save1);
        pr_cpu64_set_xmm(c, 7, src1);
        pr_cpu64_xmm(c, 7, other);
        pr_cpu64_set_rflags(c, 0x202 | 0x1 | 0x40 | 0x80);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);                          /* paddb + hlt: SÓ o necessario */
        pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, exp, 16) == 0);       /* 0x10+0x20=0x30 nas 16 lanes */
        pr_cpu64_xmm(c, 1, got);
        CHECK(memcmp(got, save1, 16) == 0);     /* fonte intacta */
        pr_cpu64_xmm(c, 7, got);
        CHECK(memcmp(got, other, 16) == 0);     /* demais XMM intactos */
        CHECK(pr_cpu64_reg(c, PR_R64_RAX) == 0x1111222233334444ull);
        CHECK(pr_cpu64_reg(c, PR_R64_RCX) == 0x5555666677778888ull);
        CHECK(pr_cpu64_rflags(c) == (0x202 | 0x1 | 0x40 | 0x80));
    }

    /* -- 2) overflow modular (0xFF+0x01=0x00) + independencia de lanes /
     *    SEM carry entre bytes: wraps em lanes 0..2 nao alteram a lane 3
     *    (0xF0+0x20=0x10) nem as seguintes -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0xFC, 0xC1,   /* paddb %xmm1,%xmm0 */
            0xF4,
        };
        static const uint8_t dst[16] = {
            0xFF,0x80,0xFE,0xF0,  0x01,0x02,0x03,0x04,
            0x05,0x06,0x07,0x08,  0x09,0x0A,0x0B,0x0C,
        };
        static const uint8_t src1[16] = {
            0x01,0x80,0x03,0x20,  0x00,0x00,0x00,0x00,
            0x00,0x00,0x00,0x00,  0x00,0x00,0x00,0x00,
        };
        /* wraps: FF+01=00 80+80=00 FE+03=01 F0+20=10 (0x110 truncado);
         * lanes 4..15 preservadas (nenhum carry vazou) */
        static const uint8_t exp[16] = {
            0x00,0x00,0x01,0x10,  0x01,0x02,0x03,0x04,
            0x05,0x06,0x07,0x08,  0x09,0x0A,0x0B,0x0C,
        };
        uint8_t got[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, dst);
        pr_cpu64_set_xmm(c, 1, src1);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);
        pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, exp, 16) == 0);
    }

    /* -- 3) valores diversos em todas as 16 lanes (pares distintos, variasos
     *    wraps isolados comprovando independencia byte a byte) -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0xFC, 0xC1,   /* paddb %xmm1,%xmm0 */
            0xF4,
        };
        static const uint8_t dst[16] = {
            0x10,0x00,0xFF,0x7F,  0x80,0x12,0x34,0x56,
            0x78,0x9A,0xBC,0xDE,  0x01,0x0F,0xF0,0x55,
        };
        static const uint8_t src1[16] = {
            0x20,0xFF,0x01,0x80,  0x80,0x34,0x12,0x65,
            0x87,0x66,0x44,0x22,  0x02,0x01,0x10,0x0F,
        };
        /* 30 FF 00(=FF+01) FF(=7F+80) 00(=80+80) 46 46 BB FF
         * 00(=9A+66) 00(=BC+44) 00(=DE+22) 03 10 00(=F0+10) 64 */
        static const uint8_t exp[16] = {
            0x30,0xFF,0x00,0xFF,  0x00,0x46,0x46,0xBB,
            0xFF,0x00,0x00,0x00,  0x03,0x10,0x00,0x64,
        };
        uint8_t got[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, dst);
        pr_cpu64_set_xmm(c, 1, src1);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);
        pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, exp, 16) == 0);
    }

    /* -- 4) padroes + diferentes XMMs + REX.R/REX.B:
     *    00+00=00 FF+00=FF 00+FF=FF AA+55=FF 55+AA=FF FF+FF=FE -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0xFC, 0xC1,       /* paddb %xmm1,%xmm0   */
            0x66, 0x0F, 0xFC, 0xD3,       /* paddb %xmm3,%xmm2   */
            0x66, 0x0F, 0xFC, 0xE5,       /* paddb %xmm5,%xmm4   */
            0x66, 0x0F, 0xFC, 0xF7,       /* paddb %xmm7,%xmm6   */
            0x66, 0x45, 0x0F, 0xFC, 0xC1, /* paddb %xmm9,%xmm8   */
            0x66, 0x45, 0x0F, 0xFC, 0xD3, /* paddb %xmm11,%xmm10 */
            0xF4,
        };
        static const uint8_t p00[16] = { 0 };
        static const uint8_t pFF[16] = {
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
            0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,
        };
        static const uint8_t pAA[16] = {
            0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
            0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,0xAA,
        };
        static const uint8_t p55[16] = {
            0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
            0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,
        };
        static const uint8_t pFE[16] = {
            0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,
            0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,
        };
        uint8_t got[16], save11[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, p00);  pr_cpu64_set_xmm(c, 1, p00);
        pr_cpu64_set_xmm(c, 2, pFF);  pr_cpu64_set_xmm(c, 3, p00);
        pr_cpu64_set_xmm(c, 4, p00);  pr_cpu64_set_xmm(c, 5, pFF);
        pr_cpu64_set_xmm(c, 6, pAA);  pr_cpu64_set_xmm(c, 7, p55);
        pr_cpu64_set_xmm(c, 8, p55);  pr_cpu64_set_xmm(c, 9, pAA);
        pr_cpu64_set_xmm(c, 10, pFF); pr_cpu64_set_xmm(c, 11, pFF);
        pr_cpu64_xmm(c, 11, save11);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 7);                          /* 6x paddb + hlt */
        pr_cpu64_xmm(c, 0, got); CHECK(memcmp(got, p00, 16) == 0); /* 00+00 */
        pr_cpu64_xmm(c, 2, got); CHECK(memcmp(got, pFF, 16) == 0); /* FF+00 */
        pr_cpu64_xmm(c, 4, got); CHECK(memcmp(got, pFF, 16) == 0); /* 00+FF */
        pr_cpu64_xmm(c, 6, got); CHECK(memcmp(got, pFF, 16) == 0); /* AA+55 */
        pr_cpu64_xmm(c, 8, got); CHECK(memcmp(got, pFF, 16) == 0); /* 55+AA */
        pr_cpu64_xmm(c, 10, got); CHECK(memcmp(got, pFE, 16) == 0);/* FF+FF */
        pr_cpu64_xmm(c, 11, got);
        CHECK(memcmp(got, save11, 16) == 0);    /* fonte intacta */
    }

    /* -- 5) destino == fonte (paddb %xmm0,%xmm0 = 66 0F FC C0): dobra modular
     *    por lane (80+80=00, FF+FF=FE, AA+AA=54), alias seguro -- */
    {
        static const uint8_t prog[] = {
            0x66, 0x0F, 0xFC, 0xC0,
            0xF4,
        };
        static const uint8_t inp[16] = {
            0x10,0x80,0xFF,0x00,  0x0F,0xF1,0x55,0xAA,
            0x01,0x02,0x03,0x04,  0x81,0x82,0x83,0x84,
        };
        /* x+x mod 256: 20 00 FE 00  1E E2 AA 54  02 04 06 08  02 04 06 08 */
        static const uint8_t exp[16] = {
            0x20,0x00,0xFE,0x00,  0x1E,0xE2,0xAA,0x54,
            0x02,0x04,0x06,0x08,  0x02,0x04,0x06,0x08,
        };
        uint8_t got[16];
        memset(mem, 0, sizeof(mem));
        memcpy(mem + 0x200, prog, sizeof(prog));
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_xmm(c, 0, inp);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK(n == 2);
        pr_cpu64_xmm(c, 0, got);
        CHECK(memcmp(got, exp, 16) == 0);
    }

    /* -- 6) NADA de implementacao antecipada: PADDW/PADDD/PADDQ/PSUBB/PANDN/
     *    POR, forma memoria e ausencia do prefixo 66 continuam com fault
     *    honesto -- */
    {
        static const uint8_t paddw[4]  = { 0x66, 0x0F, 0xFD, 0xC1 };
        static const uint8_t paddd[4]  = { 0x66, 0x0F, 0xFE, 0xC1 };
        static const uint8_t paddq[4]  = { 0x66, 0x0F, 0xD4, 0xC1 };
        static const uint8_t psubb[4]  = { 0x66, 0x0F, 0xF8, 0xC1 };
        static const uint8_t pandn[4]  = { 0x66, 0x0F, 0xDF, 0xC1 };
        static const uint8_t por[4]    = { 0x66, 0x0F, 0xEB, 0xC1 };
        static const uint8_t paddb_mem[4] = { 0x66, 0x0F, 0xFC, 0x02 };
        static const uint8_t paddb_nop66[3] = { 0x0F, 0xFC, 0xC1 };
        memset(mem, 0, sizeof(mem));
        pr_cpu64_set_reg(c, PR_R64_RDX, 0x300);
        memcpy(mem + 0x200, paddw, 4); mem[0x204] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        memcpy(mem + 0x200, paddd, 4); mem[0x204] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        memcpy(mem + 0x200, paddq, 4); mem[0x204] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        memcpy(mem + 0x200, psubb, 4); mem[0x204] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        memcpy(mem + 0x200, pandn, 4); mem[0x204] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        memcpy(mem + 0x200, por, 4); mem[0x204] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        memcpy(mem + 0x200, paddb_mem, 4); mem[0x204] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
        CHECK(strstr(pr_cpu64_last_fault(c)->reason, "registrador") != NULL);
        memcpy(mem + 0x200, paddb_nop66, 3); mem[0x203] = 0xF4;
        pr_cpu64_set_rip(c, 0x200);
        CHECK(pr_cpu64_run(c, 100, &n) == PR_ERR_FAULT);
    }

    pr_cpu64_destroy(c);
}
