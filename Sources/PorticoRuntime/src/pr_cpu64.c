/* pr_cpu64.c — interpretador x86-64 progressivo (ver pr_cpu64.h).
 *
 * FASE 1: RFLAGS reais (CF/PF/ZF/SF/OF), ALU completa 8/16/32/64-bit,
 * CMP/TEST, INC/DEC, shifts/rotates, LEA, MOVZX/MOVSX/MOVSXD, XCHG,
 * JMP/Jcc/SETcc (16 condições), CALL rel32/indireto, PUSH/POP imm/rm,
 * LEAVE/ENTER(n,0), MUL/IMUL/DIV/IDIV, CBW/CQO, flag-ops, endbr64/NOPs.
 * Endereçamento [base+index*scale+disp] e RIP-rel (relativo ao fim da
 * instrução, inclusive com imediato após o ModRM).
 * FASE CRT: string ops (MOVS/STOS/LODS/SCAS/CMPS com REP/REPE/REPNE e DF)
 * e SSE mínimo (MOVUPS/MOVAPS/MOVDQA/MOVDQU/MOVD/MOVQ/PXOR/XORPS/XORPD).
 *
 * Não suportado → fault honesto: opcode, RIP, bytes da instrução, endereço
 * e motivo ("fora do subconjunto x64"/especifico). Sempre 1 instrução por
 * passo (REP conta como 1; registros atualizados por iteração). Ainda fora:
 * FPU/x87, aritmética SSE/shuffles/MOVSS/MOVSD, INS/OUTS, AVX.
 */
#include "portico/pr_cpu64.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct pr_cpu64 {
    uint8_t* mem;
    uint32_t mem_size;
    uint64_t r[16];
    uint8_t  xmm[16][16];     /* XMM0..15 (SSE minimo) */
    uint64_t rip;
    uint64_t rflags;          /* apenas CF/PF/ZF/SF/OF (+DF bit 10) */
    int halted;
    pr_cpu64_guard_fn guard; void* guard_ud;
    uint64_t fs_base, gs_base;  /* bases flat (TEB via FS/GS no x64 Windows) */
    uint64_t seg_base;          /* base ativa na instrução corrente */
    /* x87 mínimo: apenas ESTADO de controle (CRT faz FNINIT/FLDCW/FNSTCW).
     * A aritmética x87 fica FORA do subconjunto (fault honesto sob demanda). */
    uint16_t fpu_cw;            /* control word (inicial 0x037F) */
    uint16_t fpu_sw;            /* status word */
    uint16_t fpu_tw;            /* tag word (0xFFFF = todas vazias) */
    long double fst[8];         /* pilha x87 st(0)..st(7) (80-bit) */
    int fpu_top;                /* topo da pilha (sw bits 11-13) */
    uint64_t tsc;               /* contador determinístico p/ RDTSC (+8/insn) */
    pr_cpu64_trap_fn  trap;  void* trap_ud;
    pr_cpu64_fault fault;
    int has_fault;
};

/* ---------------- memória (guard R/W/X) ---------------- */

static int mem_check(pr_cpu64* c, uint64_t addr, size_t len, int acc) {
    if (len == 0) return 1;
    if (addr > (uint64_t)c->mem_size || len > (size_t)(c->mem_size - addr)) {
        snprintf(c->fault.reason, sizeof(c->fault.reason),
                 "endereco fora do espaco de memoria (0x%llx + %u)",
                 (unsigned long long)addr, (unsigned)len);
        return 0;
    }
    if (c->guard && !c->guard(c->guard_ud, addr, len, acc)) {
        snprintf(c->fault.reason, sizeof(c->fault.reason),
                 "acesso negado pelo guard de paginas (acc=%d)", acc);
        return 0;
    }
    return 1;
}

static uint64_t get_le(const uint8_t* p, int w) {
    switch (w) {
        case 1: return p[0];
        case 2: return (uint64_t)p[0] | ((uint64_t)p[1] << 8);
        case 4: return (uint64_t)p[0] | ((uint64_t)p[1] << 8) |
                       ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 24);
        default: {
            uint64_t v = 0;
            for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
            return v;
        }
    }
}

static void set_le(uint8_t* p, int w, uint64_t v) {
    for (int i = 0; i < w; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static int mem_read(pr_cpu64* c, uint64_t addr, int w, uint64_t* out) {
    if (!mem_check(c, addr, (size_t)w, PR_CPU64_ACC_R)) return 0;
    *out = get_le(c->mem + addr, w);
    return 1;
}

static int mem_write(pr_cpu64* c, uint64_t addr, int w, uint64_t v) {
    if (!mem_check(c, addr, (size_t)w, PR_CPU64_ACC_W)) return 0;
    set_le(c->mem + addr, w, v);
    return 1;
}

/* ---------------- registradores (incl. bytes altos AH..BH) ---------------- */

static uint64_t reg_mask(int w) {
    switch (w) { case 1: return 0xFFull; case 2: return 0xFFFFull;
                 case 4: return 0xFFFFFFFFull; default: return ~0ull; }
}
static int width_bits(int w) { return w * 8; }

static uint64_t reg_get(const pr_cpu64* c, int idx, int w, int hi8, int no_rex) {
    uint64_t v = c->r[idx & 15];
    if (w == 1) {
        if (hi8 && no_rex) return (c->r[idx & 3] >> 8) & 0xFF;   /* AH CH DH BH */
        return v & 0xFF;
    }
    return v & reg_mask(w);
}

static void reg_set(pr_cpu64* c, int idx, int w, uint64_t v, int hi8, int no_rex) {
    idx &= 15;
    switch (w) {
        case 1:
            if (hi8 && no_rex) {
                /* AH/CH/DH/BH = bits 8..15 de RAX/RCX/RDX/RBX (índice 0..3) */
                int t = idx & 3;
                c->r[t] = (c->r[t] & ~0xFF00ull) | ((v & 0xFF) << 8);
            } else {
                c->r[idx] = (c->r[idx] & ~0xFFull) | (v & 0xFF);
            }
            break;
        case 2:
            c->r[idx] = (c->r[idx] & ~0xFFFFull) | (v & 0xFFFFull);
            break;
        case 4:
            c->r[idx] = v & 0xFFFFFFFFull;            /* zero-extend p/ 64 */
            break;
        default:
            c->r[idx] = v;
            break;
    }
}

/* ---------------- flags ---------------- */

#define BIT_CF PR_R64_CF
#define BIT_PF PR_R64_PF
#define BIT_ZF PR_R64_ZF
#define BIT_SF PR_R64_SF
#define BIT_OF PR_R64_OF

static uint64_t pf_of(uint64_t res) {
    uint8_t b = (uint8_t)(res & 0xFF);
    b ^= (uint8_t)(b >> 4); b &= 0x0F;
    /* PF = paridade par do byte baixo (0x02 tem 1 bit → PF=0... usa LUT) */
    static const uint8_t parity[16] = {1,0,0,1,0,1,1,0,0,1,1,0,1,0,0,1};
    return parity[b & 0x0F] ? BIT_PF : 0;
}

static void flags_logic(pr_cpu64* c, uint64_t res, int w) {
    uint64_t f = pf_of(res);
    if ((res & reg_mask(w)) == 0) f |= BIT_ZF;
    if (res & (1ull << (width_bits(w) - 1))) f |= BIT_SF;
    c->rflags = (c->rflags & ~(BIT_CF | BIT_OF | BIT_ZF | BIT_SF | BIT_PF)) | f;
}

static void flags_add(pr_cpu64* c, uint64_t a, uint64_t b, uint64_t res,
                      uint64_t cin, int w) {
    int bits = width_bits(w);
    uint64_t m = reg_mask(w);
    a &= m; b &= m; res &= m;
    uint64_t f = pf_of(res);
    uint64_t raw = a + b + cin;
    if (raw > m) f |= BIT_CF;
    if (res == 0) f |= BIT_ZF;
    if (res & (1ull << (bits - 1))) f |= BIT_SF;
    /* OF: sinais iguais de a,b e diferente de res */
    if ((~(a ^ b) & (a ^ res)) & (1ull << (bits - 1))) f |= BIT_OF;
    c->rflags = (c->rflags & ~(BIT_CF | BIT_OF | BIT_ZF | BIT_SF | BIT_PF)) | f;
}

static void flags_sub(pr_cpu64* c, uint64_t a, uint64_t b, uint64_t res,
                      uint64_t bin, int w) {
    int bits = width_bits(w);
    uint64_t m = reg_mask(w);
    a &= m; b &= m; res &= m;
    uint64_t f = pf_of(res);
    if (a < b + bin) f |= BIT_CF;
    if (res == 0) f |= BIT_ZF;
    if (res & (1ull << (bits - 1))) f |= BIT_SF;
    /* OF: sinais diferentes de a,b e de a diferente de res */
    if (((a ^ b) & (a ^ res)) & (1ull << (bits - 1))) f |= BIT_OF;
    c->rflags = (c->rflags & ~(BIT_CF | BIT_OF | BIT_ZF | BIT_SF | BIT_PF)) | f;
}

/* pilha x87 mínima (80-bit): push/pop/peek — ops exigidas pelo %f do MinGW */
static void fpu_push(pr_cpu64* c, long double v) {
    c->fpu_top = (c->fpu_top - 1) & 7;
    c->fst[c->fpu_top] = v;
    c->fpu_sw = (uint16_t)((c->fpu_sw & ~0x3800u) | ((c->fpu_top & 7) << 11));
    c->fpu_tw = (uint16_t)(c->fpu_tw & ~(3u << (c->fpu_top * 2)));
}
static long double fpu_pop(pr_cpu64* c) {
    long double v = c->fst[c->fpu_top];
    c->fpu_tw = (uint16_t)(c->fpu_tw | (3u << (c->fpu_top * 2)));
    c->fpu_top = (c->fpu_top + 1) & 7;
    c->fpu_sw = (uint16_t)((c->fpu_sw & ~0x3800u) | ((c->fpu_top & 7) << 11));
    return v;
}
static long double fpu_peek(pr_cpu64* c, int i) {
    return c->fst[(c->fpu_top + (i & 7)) & 7];
}
static void fpu_push80(pr_cpu64* c, const uint8_t b[10]) {
    long double v = 0;
    memcpy(&v, b, 10);   /* x87 80-bit no little-endian x64 */
    fpu_push(c, v);
}
static void fpu_pop80(pr_cpu64* c, uint8_t b[10]) {
    long double v = fpu_pop(c);
    memcpy(b, &v, 10);
}

static int cond_true(const pr_cpu64* c, int cc) {
    uint64_t f = c->rflags;
    int cf = !!(f & BIT_CF), zf = !!(f & BIT_ZF), sf = !!(f & BIT_SF);
    int of = !!(f & BIT_OF), pf = !!(f & BIT_PF);
    switch (cc & 15) {
        case 0:  return of;
        case 1:  return !of;
        case 2:  return cf;
        case 3:  return !cf;
        case 4:  return zf;
        case 5:  return !zf;
        case 6:  return cf || zf;
        case 7:  return !cf && !zf;
        case 8:  return sf;
        case 9:  return !sf;
        case 10: return pf;
        case 11: return !pf;
        case 12: return sf != of;
        case 13: return sf == of;
        case 14: return zf || (sf != of);
        default: return !zf && (sf == of);
    }
}

/* ---------------- fetch + ModRM ---------------- */

static int fetch(pr_cpu64* c, uint64_t* rip, uint8_t* b) {
    if (!mem_check(c, *rip, 1, PR_CPU64_ACC_X)) return 0;
    *b = c->mem[*rip];
    (*rip)++;
    return 1;
}

static int fetch_n(pr_cpu64* c, uint64_t* rip, void* out, int n) {
    if (!mem_check(c, *rip, (size_t)n, PR_CPU64_ACC_X)) return 0;
    memcpy(out, c->mem + *rip, (size_t)n);
    *rip += (uint64_t)n;
    return 1;
}

typedef struct {
    int is_reg;
    int reg;            /* índice quando is_reg */
    int hi8, no_rex;    /* forma byte-alto */
    /* memória: componentes resolvidos no ponto de uso (RIP-rel correto) */
    int base, idx, scale, have_idx, rip_rel;
    int64_t disp;
} rm_t;

/* Decodifica ModRM (+SIB+disp). Avança c->rip até depois do ModRM. */
static int decode_rm(pr_cpu64* c, uint64_t* rip, uint8_t rex, int w,
                     rm_t* o, int* regf) {
    uint8_t modrm;
    if (!fetch(c, rip, &modrm)) return 0;
    int mod = (modrm >> 6) & 3;
    int rf  = (modrm >> 3) & 7;
    int rm  = modrm & 7;
    *regf = rf | ((rex & 0x4) ? 8 : 0);
    o->is_reg = 0; o->hi8 = 0; o->no_rex = (rex == 0);
    o->base = -1; o->idx = -1; o->scale = 1; o->have_idx = 0;
    o->rip_rel = 0; o->disp = 0; o->reg = 0;

    if (mod == 3) {
        o->is_reg = 1;
        o->reg = rm | ((rex & 0x1) ? 8 : 0);
        if (w == 1 && o->reg >= 4 && o->no_rex) o->hi8 = 1;
        return 1;
    }

    if (rm == 4) {                                  /* SIB */
        uint8_t sib;
        if (!fetch(c, rip, &sib)) return 0;
        int sc = (sib >> 6) & 3, ix = (sib >> 3) & 7, bs = sib & 7;
        o->scale = 1 << sc;
        if (ix != 4 || (rex & 0x2)) {               /* index=100 sem REX.X = none */
            o->idx = ix | ((rex & 0x2) ? 8 : 0);
            o->have_idx = 1;
        }
        if (bs == 5 && mod == 0) {                  /* base=101 mod=00 → disp32 */
            int32_t d;
            if (!fetch_n(c, rip, &d, 4)) return 0;
            o->disp = d;
        } else {
            o->base = bs | ((rex & 0x1) ? 8 : 0);
        }
    } else if (rm == 5 && mod == 0) {               /* RIP+disp32 */
        int32_t d;
        if (!fetch_n(c, rip, &d, 4)) return 0;
        o->rip_rel = 1;
        o->disp = d;
    } else {
        o->base = rm | ((rex & 0x1) ? 8 : 0);
    }

    if (mod == 1) {
        int8_t d;
        if (!fetch_n(c, rip, &d, 1)) return 0;
        o->disp += d;
    } else if (mod == 2) {
        int32_t d;
        if (!fetch_n(c, rip, &d, 4)) return 0;
        o->disp += d;
    }
    return 1;
}

/* Endereço efetivo (chamado APÓS imediatos — RIP-rel usa fim da instrução). */
static uint64_t ea_of(const pr_cpu64* c, const rm_t* o) {
    if (o->rip_rel) return c->seg_base + c->rip + (uint64_t)o->disp;
    uint64_t a = (o->base >= 0) ? c->r[o->base & 15] : 0;
    if (o->have_idx) a += c->r[o->idx & 15] * (uint64_t)o->scale;
    return c->seg_base + a + (uint64_t)o->disp;
}

static int rm_read(pr_cpu64* c, const rm_t* o, int w, uint64_t* out) {
    if (o->is_reg) {
        *out = reg_get(c, o->reg, w, o->hi8, o->no_rex);
        return 1;
    }
    return mem_read(c, ea_of(c, o), w, out);
}

static int rm_write(pr_cpu64* c, const rm_t* o, int w, uint64_t v) {
    if (o->is_reg) {
        reg_set(c, o->reg, w, v, o->hi8, o->no_rex);
        return 1;
    }
    return mem_write(c, ea_of(c, o), w, v);
}

/* ---------------- memoria 128-bit (SSE) ---------------- */

static int mem_read16(pr_cpu64* c, uint64_t addr, uint8_t* out) {
    if (!mem_check(c, addr, 16, PR_CPU64_ACC_R)) return 0;
    memcpy(out, c->mem + addr, 16);
    return 1;
}
static int mem_write16(pr_cpu64* c, uint64_t addr, const uint8_t* in) {
    if (!mem_check(c, addr, 16, PR_CPU64_ACC_W)) return 0;
    memcpy(c->mem + addr, in, 16);
    return 1;
}

/* ---------------- ALU ---------------- */
/* op: 0 add, 1 or, 2 adc, 3 sbb, 4 and, 5 sub, 6 xor, 7 cmp. */
static int alu_exec(pr_cpu64* c, int op, uint64_t a, uint64_t b, int w,
                    int writeback, rm_t* o, uint64_t* outv) {
    uint64_t m = reg_mask(w);
    a &= m; b &= m;
    uint64_t res;
    switch (op) {
        case 0: case 2: {
            uint64_t cin = (op == 2 && (c->rflags & BIT_CF)) ? 1 : 0;
            res = (a + b + cin) & m;
            flags_add(c, a, b, res, cin, w);
            break;
        }
        case 3: {
            uint64_t bin = (c->rflags & BIT_CF) ? 1 : 0;
            res = (a - b - bin) & m;
            flags_sub(c, a, b, res, bin, w);
            break;
        }
        case 5: case 7: {
            res = (a - b) & m;
            flags_sub(c, a, b, res, 0, w);
            break;
        }
        case 1: res = a | b; flags_logic(c, res, w); break;
        case 4: res = a & b; flags_logic(c, res, w); break;
        default: res = a ^ b; flags_logic(c, res, w); break;
    }
    if (writeback && op != 7) return rm_write(c, o, w, res);
    if (outv) *outv = res;
    return 1;
}

/* shifts/rotates: 0 ROL 1 ROR 2 RCL 3 RCR 4 SHL 5 SHR 6 SAL=SHL 7 SAR */
static int shift_exec(pr_cpu64* c, int subop, rm_t* o, int w, uint64_t count) {
    uint64_t val;
    if (!rm_read(c, o, w, &val)) return 0;
    int bits = width_bits(w);
    uint64_t m = reg_mask(w);
    val &= m;
    unsigned cnt = (unsigned)(count & (w == 8 ? 0x3F : 0x1F));
    if (cnt == 0) return 1;                          /* flags intactos */
    int cf = (c->rflags & BIT_CF) ? 1 : 0;
    uint64_t signbit = 1ull << (bits - 1);
    uint64_t res = val;
    unsigned i;
    switch (subop) {
        case 0:                                        /* ROL */
            for (i = 0; i < cnt; i++) {
                cf = (int)((res >> (bits - 1)) & 1);
                res = ((res << 1) | (uint64_t)cf) & m;
            }
            c->rflags = (c->rflags & ~BIT_CF) | (cf ? BIT_CF : 0);
            if (cnt == 1) c->rflags = (c->rflags & ~BIT_OF) |
                ((((int)((res >> (bits - 1)) & 1)) ^ cf) ? BIT_OF : 0);
            return rm_write(c, o, w, res);
        case 1:                                        /* ROR */
            for (i = 0; i < cnt; i++) {
                cf = (int)(res & 1);
                res = (res >> 1) | ((uint64_t)cf << (bits - 1));
            }
            c->rflags = (c->rflags & ~BIT_CF) | (cf ? BIT_CF : 0);
            if (cnt == 1) {
                uint64_t b2 = (res >> (bits - 1)) & 1, b1 = (res >> (bits - 2)) & 1;
                c->rflags = (c->rflags & ~BIT_OF) | ((b2 ^ b1) ? BIT_OF : 0);
            }
            return rm_write(c, o, w, res);
        case 2:                                        /* RCL */
            for (i = 0; i < cnt; i++) {
                int ncf = (int)((res >> (bits - 1)) & 1);
                res = ((res << 1) | (uint64_t)cf) & m;
                cf = ncf;
            }
            c->rflags = (c->rflags & ~BIT_CF) | (cf ? BIT_CF : 0);
            if (cnt == 1) c->rflags = (c->rflags & ~BIT_OF) |
                ((((int)((res >> (bits - 1)) & 1)) ^ cf) ? BIT_OF : 0);
            return rm_write(c, o, w, res);
        case 3:                                        /* RCR */
            for (i = 0; i < cnt; i++) {
                int ncf = (int)(res & 1);
                res = (res >> 1) | ((uint64_t)cf << (bits - 1));
                cf = ncf;
            }
            c->rflags = (c->rflags & ~BIT_CF) | (cf ? BIT_CF : 0);
            if (cnt == 1) {
                uint64_t b2 = (res >> (bits - 1)) & 1, b1 = (res >> (bits - 2)) & 1;
                c->rflags = (c->rflags & ~BIT_OF) | ((b2 ^ b1) ? BIT_OF : 0);
            }
            return rm_write(c, o, w, res);
        case 4: case 6:                                /* SHL */
            for (i = 0; i < cnt; i++) {
                cf = (int)((res >> (bits - 1)) & 1);
                res = (res << 1) & m;
            }
            c->rflags = (c->rflags & ~BIT_CF) | (cf ? BIT_CF : 0);
            if (cnt == 1) c->rflags = (c->rflags & ~BIT_OF) |
                ((((int)((res >> (bits - 1)) & 1)) ^ cf) ? BIT_OF : 0);
            {
                uint64_t f = pf_of(res);
                if (res == 0) f |= BIT_ZF;
                if (res & signbit) f |= BIT_SF;
                c->rflags = (c->rflags & ~(BIT_ZF | BIT_SF | BIT_PF)) | f;
            }
            return rm_write(c, o, w, res);
        case 5:                                        /* SHR */
            for (i = 0; i < cnt; i++) {
                cf = (int)(res & 1);
                res >>= 1;
            }
            c->rflags = (c->rflags & ~BIT_CF) | (cf ? BIT_CF : 0);
            if (cnt == 1) c->rflags = (c->rflags & ~BIT_OF) | (val & signbit ? BIT_OF : 0);
            {
                uint64_t f = pf_of(res);
                if (res == 0) f |= BIT_ZF;
                if (res & signbit) f |= BIT_SF;
                c->rflags = (c->rflags & ~(BIT_ZF | BIT_SF | BIT_PF)) | f;
            }
            return rm_write(c, o, w, res);
        default:                                       /* SAR */
            for (i = 0; i < cnt; i++) {
                cf = (int)(res & 1);
                res = (res >> 1) | (res & signbit);
            }
            c->rflags = (c->rflags & ~BIT_CF) | (cf ? BIT_CF : 0);
            if (cnt == 1) c->rflags &= ~BIT_OF;
            {
                uint64_t f = pf_of(res);
                if (res == 0) f |= BIT_ZF;
                if (res & signbit) f |= BIT_SF;
                c->rflags = (c->rflags & ~(BIT_ZF | BIT_SF | BIT_PF)) | f;
            }
            return rm_write(c, o, w, res);
    }
}

static int64_t sext(uint64_t v, int w) {
    switch (w) {
        case 1: return (int8_t)(uint8_t)v;
        case 2: return (int16_t)(uint16_t)v;
        case 4: return (int32_t)(uint32_t)v;
        default: return (int64_t)v;
    }
}

/* ---------------- pilha ---------------- */

static int push64(pr_cpu64* c, uint64_t v) {
    c->r[4] -= 8;
    return mem_write(c, c->r[4], 8, v);
}
static int pop64(pr_cpu64* c, uint64_t* out) {
    if (!mem_read(c, c->r[4], 8, out)) return 0;
    c->r[4] += 8;
    return 1;
}

/* ---------------- fault ---------------- */

static int do_fault(pr_cpu64* c, uint64_t rip0, uint8_t op, uint8_t rex,
                    uint32_t addr, const char* msg) {
    char keep[80];
    if (msg && msg != c->fault.reason) snprintf(keep, sizeof(keep), "%s", msg);
    else snprintf(keep, sizeof(keep), "%s", c->fault.reason);
    if (!keep[0]) snprintf(keep, sizeof(keep), "parada do trap de software");
    /* se o trap escreveu em reason via ponteiro externo, motivo default acima
     * cobre; se escreveu no proprio c->fault.reason, msg==NULL preserva. */
    c->fault.rip = rip0;
    c->fault.opcode = op;
    c->fault.rex = rex;
    c->fault.addr = addr;
    snprintf(c->fault.reason, sizeof(c->fault.reason), "%s", keep);
    c->fault.nbytes = 0;
    if (rip0 < c->mem_size) {
        size_t n = c->mem_size - (size_t)rip0;
        if (n > sizeof(c->fault.bytes)) n = sizeof(c->fault.bytes);
        memcpy(c->fault.bytes, c->mem + rip0, n);
        c->fault.nbytes = (uint8_t)n;
    }
    c->has_fault = 1;
    return 0;
}

/* ---------------- passo único ---------------- */

static int step_one(pr_cpu64* c) {
    c->tsc += 8;               /* tempo virtual determinístico (sem info do host) */
    uint64_t rip = c->rip;
    uint64_t rip0 = rip;
    uint8_t rex = 0, op;
    int p66 = 0, p67 = 0, pfs = 0, pgs = 0, prep = 0, plock = 0, pwait = 0;
    c->has_fault = 0;

    /* prefixos + REX */
    for (;;) {
        if (!fetch(c, &rip, &op)) {
            /* mem_check já preencheu reason (negado/fora do espaco) */
            return do_fault(c, rip0, rex ? rex : 0, rex, (uint32_t)rip0, NULL);
        }
        if (op == 0x66) { p66 = 1; continue; }
        if (op == 0x67) { p67 = 1; continue; }
        if (op == 0xF0) { plock = 1; continue; }   /* LOCK: VM mono-core, sem efeito */
        if (op == 0x9B) { pwait = 1; continue; }   /* WAIT/FINIT combinado */
        if (op == 0xF2 || op == 0xF3) { prep = op; continue; }
        if (op == 0x2E || op == 0x3E || op == 0x26 || op == 0x36) continue; /* no-op flat */
        if (op == 0x64) { pfs = 1; pgs = 0; continue; }   /* último vence (x86) */
        if (op == 0x65) { pgs = 1; pfs = 0; continue; }
        if (op >= 0x40 && op <= 0x4F) { rex = op; continue; }  /* REX: prefixo final */
        break;
    }
    if (p67) return do_fault(c, rip0, op, rex, 0, "prefixo 67h (addr32) fora do subconjunto x64");
    /* FS/GS: modelo flat com base configurável (TEB). Sem base configurada
     * os acessos caem em VA baixa e falham com diagnóstico honesto. */
    c->seg_base = pgs ? c->gs_base : (pfs ? c->fs_base : 0);

    int w = (rex & 0x8) ? 8 : (p66 ? 2 : 4);          /* operand size padrão */
    int no_rex = (rex == 0);
    rm_t o;
    int regf;

#define FAULT(msg) return do_fault(c, rip0, op, rex, 0, (msg))
#define FAULT_ADDR(addr, msg) return do_fault(c, rip0, op, rex, (uint32_t)(addr), (msg))

    /* ---------- um byte + rd (PUSH/POP/XCHG/INC-OBsoleto) ---------- */
    if (op >= 0x50 && op <= 0x57) {                    /* PUSH r64 */
        int r = (op - 0x50) | ((rex & 0x1) ? 8 : 0);
        if (!push64(c, c->r[r])) FAULT_ADDR(c->r[4], "push fora da pilha");
        c->rip = rip;
        return 1;
    }
    if (op >= 0x58 && op <= 0x5F) {                    /* POP r64 */
        int r = (op - 0x58) | ((rex & 0x1) ? 8 : 0);
        uint64_t v;
        if (!pop64(c, &v)) FAULT_ADDR(c->r[4], "pop fora da pilha");
        c->r[r] = v;
        c->rip = rip;
        return 1;
    }
    if (op >= 0x90 && op <= 0x97) {                    /* XCHG rAX, r (90=NOP) */
        int r = (op - 0x90) | ((rex & 0x1) ? 8 : 0);
        if (r == 0 && !p66) { c->rip = rip; return 1; } /* NOP */
        uint64_t a = reg_get(c, 0, w, 0, no_rex);
        uint64_t b = reg_get(c, r, w, 0, no_rex);
        reg_set(c, 0, w, b, 0, no_rex);
        reg_set(c, r, w, a, 0, no_rex);
        c->rip = rip;
        return 1;
    }
    if (op >= 0xB0 && op <= 0xB7) {                    /* MOV r8, imm8 */
        int r = (op - 0xB0) | ((rex & 0x1) ? 8 : 0);
        uint8_t imm;
        if (!fetch(c, &rip, &imm)) FAULT("fetch de imediato negado");
        int hi8 = (r < 8 && r >= 4 && no_rex);
        reg_set(c, r, 1, imm, hi8, no_rex);
        c->rip = rip;
        return 1;
    }
    if (op >= 0xB8 && op <= 0xBF) {                    /* MOV r, imm16/32/64 */
        int r = (op - 0xB8) | ((rex & 0x1) ? 8 : 0);
        if (rex & 0x8) {
            uint64_t imm;
            if (!fetch_n(c, &rip, &imm, 8)) FAULT("fetch de imediato negado");
            c->r[r] = imm;
        } else if (p66) {
            uint16_t imm;
            if (!fetch_n(c, &rip, &imm, 2)) FAULT("fetch de imediato negado");
            reg_set(c, r, 2, imm, 0, no_rex);
        } else {
            uint32_t imm;
            if (!fetch_n(c, &rip, &imm, 4)) FAULT("fetch de imediato negado");
            c->r[r] = imm;                             /* zero-extend */
        }
        c->rip = rip;
        return 1;
    }

    /* ---------- ALU por família 0x00..0x3D (op*8 + form 0..5) ---------- */
    if (op <= 0x3D && (op & 7) <= 5) {
        int aluop = (int)((op >> 3) & 7);
        int form = op & 7;
        if (form <= 5) {
            switch (form) {
                case 0: case 1: {                      /* r/m, r */
                    int w8 = (form == 0) ? 1 : w;
                    if (!decode_rm(c, &rip, rex, w8, &o, &regf)) FAULT("ModRM negado");
                    uint64_t a, b = reg_get(c, regf, w8, (w8 == 1 && regf >= 4 && no_rex), no_rex);
                    c->rip = rip;
                    if (!rm_read(c, &o, w8, &a)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                    if (!alu_exec(c, aluop, a, b, w8, 1, &o, NULL))
                        FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
                    c->rip = rip;
                    return 1;
                }
                case 2: case 3: {                      /* r, r/m */
                    int w8 = (form == 2) ? 1 : w;
                    if (!decode_rm(c, &rip, rex, w8, &o, &regf)) FAULT("ModRM negado");
                    int hi8 = (w8 == 1 && regf >= 4 && no_rex);
                    c->rip = rip;
                    uint64_t a = reg_get(c, regf, w8, hi8, no_rex), b;
                    if (!rm_read(c, &o, w8, &b)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                    uint64_t res;
                    if (!alu_exec(c, aluop, a, b, w8, 0, NULL, &res)) return 0;
                    if (aluop != 7) {
                        reg_set(c, regf, w8, res, hi8, no_rex);
                    }
                    c->rip = rip;
                    return 1;
                }
                case 4: {                              /* AL, imm8 */
                    uint8_t imm;
                    if (!fetch(c, &rip, &imm)) FAULT("fetch de imediato negado");
                    rm_t regal; memset(&regal, 0, sizeof(regal));
                    regal.is_reg = 1; regal.reg = 0;
                    c->rip = rip;
                    uint64_t a = reg_get(c, 0, 1, 0, no_rex);
                    if (!alu_exec(c, aluop, a, imm, 1, 1, &regal, NULL)) return 0;
                    c->rip = rip;
                    return 1;
                }
                default: {                             /* eAX, imm16/32 */
                    uint64_t imm = 0;
                    if (p66) { uint16_t v; if (!fetch_n(c, &rip, &v, 2)) FAULT("fetch negado"); imm = v; }
                    else { uint32_t v; if (!fetch_n(c, &rip, &v, 4)) FAULT("fetch negado"); imm = v; }
                    rm_t regax; memset(&regax, 0, sizeof(regax));
                    regax.is_reg = 1; regax.reg = 0;
                    c->rip = rip;
                    uint64_t a = reg_get(c, 0, w, 0, no_rex);
                    if (!alu_exec(c, aluop, a, imm, w, 1, &regax, NULL)) return 0;
                    c->rip = rip;
                    return 1;
                }
            }
        }
    }

    switch (op) {
        case 0x63: {                                   /* MOVSXD r, r/m32 */
            if (!decode_rm(c, &rip, rex, 4, &o, &regf)) FAULT("ModRM negado");
            c->rip = rip;
            uint64_t v;
            if (!rm_read(c, &o, 4, &v)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
            int dw = (rex & 0x8) ? 8 : 4;
            reg_set(c, regf, dw, (uint64_t)sext(v, 4), 0, no_rex);
            c->rip = rip;
            return 1;
        }
        case 0x68: {                                   /* PUSH imm32 */
            int32_t imm;
            if (!fetch_n(c, &rip, &imm, 4)) FAULT("fetch de imediato negado");
            if (!push64(c, (uint64_t)(int64_t)imm)) FAULT_ADDR(c->r[4], "push fora da pilha");
            c->rip = rip;
            return 1;
        }
        case 0x69: case 0x6B: {                        /* IMUL r, r/m, imm32/imm8 */
            if (!decode_rm(c, &rip, rex, w, &o, &regf)) FAULT("ModRM negado");
            int64_t imm;
            if (op == 0x6B) { int8_t v; if (!fetch_n(c, &rip, &v, 1)) FAULT("fetch negado"); imm = v; }
            else if (p66) { int16_t v; if (!fetch_n(c, &rip, &v, 2)) FAULT("fetch negado"); imm = v; }
            else { int32_t v; if (!fetch_n(c, &rip, &v, 4)) FAULT("fetch negado"); imm = v; }
            c->rip = rip;
            uint64_t b;
            if (!rm_read(c, &o, w, &b)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
            int64_t prod = sext(b, w) * imm;
            reg_set(c, regf, w, (uint64_t)prod, 0, no_rex);
            /* CF/OF = truncamento */
            int64_t trunc = sext((uint64_t)prod, w);
            int ov = (trunc != prod);
            c->rflags = (c->rflags & ~(BIT_CF | BIT_OF)) | (ov ? (BIT_CF | BIT_OF) : 0);
            c->rip = rip;
            return 1;
        }
        case 0x6A: {                                   /* PUSH imm8 */
            int8_t imm;
            if (!fetch_n(c, &rip, &imm, 1)) FAULT("fetch de imediato negado");
            if (!push64(c, (uint64_t)(int64_t)imm)) FAULT_ADDR(c->r[4], "push fora da pilha");
            c->rip = rip;
            return 1;
        }
        case 0x80: case 0x81: case 0x83: {             /* ALU grupo 1 */
            int w8 = (op == 0x80) ? 1 : w;
            if (!decode_rm(c, &rip, rex, w8, &o, &regf)) FAULT("ModRM negado");
            uint64_t imm;
            if (op == 0x83) { int8_t v; if (!fetch_n(c, &rip, &v, 1)) FAULT("fetch negado"); imm = (uint64_t)sext((uint64_t)(uint8_t)v, 1); }
            else if (op == 0x80) { uint8_t v; if (!fetch_n(c, &rip, &v, 1)) FAULT("fetch negado"); imm = v; }
            else if (p66) { uint16_t v; if (!fetch_n(c, &rip, &v, 2)) FAULT("fetch negado"); imm = v; }
            else { uint32_t v; if (!fetch_n(c, &rip, &v, 4)) FAULT("fetch negado"); imm = v; }
            c->rip = rip;
            uint64_t a;
            if (!rm_read(c, &o, w8, &a)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
            if (!alu_exec(c, regf, a, imm, w8, 1, &o, NULL))
                FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
            c->rip = rip;
            return 1;
        }
        case 0x84: case 0x85: {                        /* TEST r/m, r */
            int w8 = (op == 0x84) ? 1 : w;
            if (!decode_rm(c, &rip, rex, w8, &o, &regf)) FAULT("ModRM negado");
            c->rip = rip;
            uint64_t a, b = reg_get(c, regf, w8, (w8 == 1 && regf >= 4 && no_rex), no_rex);
            if (!rm_read(c, &o, w8, &a)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
            flags_logic(c, a & b, w8);
            c->rip = rip;
            return 1;
        }
        case 0x86: case 0x87: {                        /* XCHG r/m, r */
            int w8 = (op == 0x86) ? 1 : w;
            if (!decode_rm(c, &rip, rex, w8, &o, &regf)) FAULT("ModRM negado");
            c->rip = rip;
            uint64_t a, b = reg_get(c, regf, w8, (w8 == 1 && regf >= 4 && no_rex), no_rex);
            if (!rm_read(c, &o, w8, &a)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
            if (!rm_write(c, &o, w8, b)) FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
            reg_set(c, regf, w8, a, (w8 == 1 && regf >= 4 && no_rex), no_rex);
            c->rip = rip;
            return 1;
        }
        case 0x88: case 0x89:                          /* MOV r/m, r */
        case 0x8A: case 0x8B: {                        /* MOV r, r/m */
            int w8 = (op == 0x88 || op == 0x8A) ? 1 : w;
            int to_reg = (op >= 0x8A);
            if (!decode_rm(c, &rip, rex, w8, &o, &regf)) FAULT("ModRM negado");
            c->rip = rip;
            int hi8 = (w8 == 1 && regf >= 4 && no_rex);
            if (to_reg) {
                uint64_t v;
                if (!rm_read(c, &o, w8, &v)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                reg_set(c, regf, w8, v, hi8, no_rex);
            } else {
                uint64_t v = reg_get(c, regf, w8, hi8, no_rex);
                if (!rm_write(c, &o, w8, v)) FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
            }
            c->rip = rip;
            return 1;
        }
        case 0x8C: case 0x8E:                          /* MOV Sreg (flat: simpl.) */
            FAULT("mov com segment register fora do subconjunto x64");
        case 0x8D: {                                   /* LEA r, m */
            if (!decode_rm(c, &rip, rex, w, &o, &regf)) FAULT("ModRM negado");
            if (o.is_reg) FAULT("LEA com operandos registrador");
            c->rip = rip;
            reg_set(c, regf, w, ea_of(c, &o), 0, no_rex);
            c->rip = rip;
            return 1;
        }
        case 0x8F: {                                   /* POP r/m (/0) */
            if (!decode_rm(c, &rip, rex, w, &o, &regf)) FAULT("ModRM negado");
            if (regf != 0) FAULT("pop grupo com / != 0");
            uint64_t v;
            if (!pop64(c, &v)) FAULT_ADDR(c->r[4], "pop fora da pilha");
            c->rip = rip;
            if (!rm_write(c, &o, w, v)) FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
            c->rip = rip;
            return 1;
        }
        case 0x98: {                                   /* CBW/CWDE/CDQE */
            if (rex & 0x8) c->r[0] = (uint64_t)sext(c->r[0], 4);
            else if (p66) reg_set(c, 0, 2, (uint64_t)sext(reg_get(c, 0, 1, 0, no_rex), 1), 0, no_rex);
            else c->r[0] = (uint64_t)(uint32_t)(int32_t)(int16_t)(uint16_t)c->r[0];
            c->rip = rip;
            return 1;
        }
        case 0x99: {                                   /* CWD/CDQ/CQO */
            if (rex & 0x8) c->r[2] = (c->r[0] & (1ull << 63)) ? ~0ull : 0;
            else if (p66) reg_set(c, 2, 2, (reg_get(c, 0, 2, 0, no_rex) & 0x8000) ? 0xFFFF : 0, 0, no_rex);
            else c->r[2] = (c->r[0] & 0x80000000ull) ? 0xFFFFFFFFull : 0;
            c->rip = rip;
            return 1;
        }
        case 0x9C: case 0x9D:
            FAULT("pushf/popf fora do subconjunto x64");
        case 0xA4: case 0xA5:                          /* MOVSB/MOVS */
        case 0xA6: case 0xA7:                          /* CMPSB/CMPS */
        case 0xAA: case 0xAB:                          /* STOSB/STOS */
        case 0xAC: case 0xAD:                          /* LODSB/LODS */
        case 0xAE: case 0xAF: {                        /* SCASB/SCAS */
            /* FASE 1+2: string ops x64.
             * MOVS (A4/A5), STOS (AA/AB), LODS (AC/AD), SCAS (AE/AF),
             * CMPS (A6/A7). F3=REP (MOVS/STOS/LODS) ou REPE (CMPS/SCAS);
             * F2=REPNE (CMPS/SCAS; nas demais = REP por contagem). Sem
             * prefixo = 1 iteracao. RCX=0 com REP* = no-op completo (regs
             * e flags intactos). DF (bit 10) define direcao de RSI/RDI.
             * w=1 no sufixo B; senao REX.W=8, 66=2, padrao=4.
             * CMPS computa [RSI]-[RDI]; SCAS computa RAX-[RDI] (como CMP).
             * Em REPE/REPNE a parada antecipada mantem RCX = restante. */
            int kind;                                  /* 0 movs 1 stos 2 lods 3 scas 4 cmps */
            switch (op & 0xFE) {
                case 0xA4: kind = 0; break;
                case 0xAA: kind = 1; break;
                case 0xAC: kind = 2; break;
                case 0xAE: kind = 3; break;
                default:   kind = 4; break;
            }
            int sw = (op & 1) ? ((rex & 0x8) ? 8 : (p66 ? 2 : 4)) : 1;
            int64_t dd = (c->rflags & (1ull << 10)) ? -(int64_t)sw : (int64_t)sw;
            int repn = (prep == 0xF3) ? 1 : (prep == 0xF2) ? -1 : 0;
            uint64_t left = (!prep) ? 1 : c->r[1];
            if (prep && left == 0) { c->rip = rip; return 1; }   /* RCX=0 */
            while (left > 0) {
                uint64_t a = 0, b = 0;
                switch (kind) {
                    case 0:                            /* MOVS [RDI] <- [RSI] */
                        if (!mem_read(c, c->r[6], sw, &a))
                            FAULT_ADDR(c->r[6], "movs: leitura negada ou fora do espaco");
                        if (!mem_write(c, c->r[7], sw, a))
                            FAULT_ADDR(c->r[7], "movs: escrita negada ou fora do espaco");
                        c->r[6] += (uint64_t)dd; c->r[7] += (uint64_t)dd;
                        break;
                    case 1:                            /* STOS [RDI] <- eAX */
                        if (!mem_write(c, c->r[7], sw, reg_get(c, 0, sw, 0, no_rex)))
                            FAULT_ADDR(c->r[7], "stos: escrita negada ou fora do espaco");
                        c->r[7] += (uint64_t)dd;
                        break;
                    case 2:                            /* LODS eAX <- [RSI] */
                        if (!mem_read(c, c->r[6], sw, &a))
                            FAULT_ADDR(c->r[6], "lods: leitura negada ou fora do espaco");
                        reg_set(c, 0, sw, a, 0, no_rex);
                        c->r[6] += (uint64_t)dd;
                        break;
                    case 3: {                          /* SCAS eAX - [RDI] */
                        uint64_t acc = reg_get(c, 0, sw, 0, no_rex);
                        if (!mem_read(c, c->r[7], sw, &a))
                            FAULT_ADDR(c->r[7], "scas: leitura negada ou fora do espaco");
                        flags_sub(c, acc, a, (acc - a) & reg_mask(sw), 0, sw);
                        c->r[7] += (uint64_t)dd;
                        break;
                    }
                    default:                           /* CMPS [RSI] - [RDI] */
                        if (!mem_read(c, c->r[6], sw, &a))
                            FAULT_ADDR(c->r[6], "cmps: leitura negada ou fora do espaco");
                        if (!mem_read(c, c->r[7], sw, &b))
                            FAULT_ADDR(c->r[7], "cmps: leitura negada ou fora do espaco");
                        flags_sub(c, a, b, (a - b) & reg_mask(sw), 0, sw);
                        c->r[6] += (uint64_t)dd; c->r[7] += (uint64_t)dd;
                        break;
                }
                left--;
                if (prep) c->r[1] = left;
                if (prep && (kind == 3 || kind == 4)) { /* REPE/REPNE param em ZF */
                    if (repn == 1 && !(c->rflags & BIT_ZF)) break;
                    if (repn == -1 && (c->rflags & BIT_ZF)) break;
                }
            }
            if (prep) c->r[1] = left;
            c->rip = rip;
            return 1;
        }
        case 0xA8: case 0xA9: {                        /* TEST eAX, imm */
            int w8 = (op == 0xA8) ? 1 : w;
            uint64_t imm;
            if (w8 == 1) { uint8_t v; if (!fetch_n(c, &rip, &v, 1)) FAULT("fetch negado"); imm = v; }
            else if (p66) { uint16_t v; if (!fetch_n(c, &rip, &v, 2)) FAULT("fetch negado"); imm = v; }
            else { uint32_t v; if (!fetch_n(c, &rip, &v, 4)) FAULT("fetch negado"); imm = v; }
            flags_logic(c, reg_get(c, 0, w8, 0, no_rex) & imm, w8);
            c->rip = rip;
            return 1;
        }
        case 0xC2: {                                   /* RET imm16: pop + ajuste */
            uint16_t popb;
            if (!fetch_n(c, &rip, &popb, 2)) FAULT("fetch de imediato negado");
            uint64_t v;
            if (!pop64(c, &v)) FAULT_ADDR(c->r[4], "ret: pop fora da pilha");
            c->r[4] += popb;
            c->rip = v;
            return 1;
        }
        case 0xC3: {                                   /* RET */
            uint64_t v;
            if (!pop64(c, &v)) FAULT_ADDR(c->r[4], "ret: pop fora da pilha");
            c->rip = v;
            return 1;
        }
        case 0xCA: case 0xCB:
            FAULT("ret far fora do subconjunto x64");
        case 0xC0: case 0xC1: {                        /* shift grupo 2, imm8 */
            int w8 = (op == 0xC0) ? 1 : w;
            if (!decode_rm(c, &rip, rex, w8, &o, &regf)) FAULT("ModRM negado");
            uint8_t cnt;
            if (!fetch_n(c, &rip, &cnt, 1)) FAULT("fetch negado");
            c->rip = rip;
            return shift_exec(c, regf, &o, w8, cnt);
        }
        case 0xC6: case 0xC7: {                        /* MOV r/m, imm (/0) */
            int w8 = (op == 0xC6) ? 1 : w;
            if (!decode_rm(c, &rip, rex, w8, &o, &regf)) FAULT("ModRM negado");
            if (regf != 0) FAULT("C6/C7 com / != 0");
            uint64_t imm;
            if (w8 == 1) { uint8_t v; if (!fetch_n(c, &rip, &v, 1)) FAULT("fetch negado"); imm = v; }
            else if (p66) { uint16_t v; if (!fetch_n(c, &rip, &v, 2)) FAULT("fetch negado"); imm = v; }
            else if (rex & 0x8) { int32_t v; if (!fetch_n(c, &rip, &v, 4)) FAULT("fetch negado"); imm = (uint64_t)(int64_t)v; }
            else { uint32_t v; if (!fetch_n(c, &rip, &v, 4)) FAULT("fetch negado"); imm = v; }
            c->rip = rip;
            if (!rm_write(c, &o, w8, imm)) FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
            c->rip = rip;
            return 1;
        }
        case 0xC8: {                                   /* ENTER imm16, 0 */
            uint16_t size; uint8_t lvl;
            if (!fetch_n(c, &rip, &size, 2)) FAULT("fetch negado");
            if (!fetch_n(c, &rip, &lvl, 1)) FAULT("fetch negado");
            if (lvl != 0) FAULT("ENTER com nivel != 0 fora do subconjunto x64");
            if (!push64(c, c->r[5])) FAULT_ADDR(c->r[4], "push fora da pilha");
            c->r[5] = c->r[4];
            c->r[4] -= size;
            c->rip = rip;
            return 1;
        }
        case 0xC9:                                     /* LEAVE */
            c->r[4] = c->r[5];
            { uint64_t v; if (!pop64(c, &v)) FAULT_ADDR(c->r[4], "leave: pop fora da pilha"); c->r[5] = v; }
            c->rip = rip;
            return 1;
        case 0xCC:
            FAULT("INT3 (breakpoint) nao suportado");
        case 0xCD: {                                   /* INT ib → trap */
            uint8_t vec;
            if (!fetch(c, &rip, &vec)) FAULT("fetch negado");
            c->rip = rip;
            if (!c->trap) FAULT("INT sem trap registrado");
            int r = c->trap(c->trap_ud, c, vec);
            if (r < 0) {
                return do_fault(c, rip0, op, rex, 0, NULL);
            }
            if (r > 0) {
                /* o trap pediu PARADA (ExitProcess/exit ou EXECUTION STOPPED):
                 * sem isto a execução continuaria após o ponto de parada */
                c->halted = 1;
            }
            return 1;
        }
        case 0xD0: case 0xD1: case 0xD2: case 0xD3: {  /* shift por 1/CL */
            int w8 = (op == 0xD0 || op == 0xD2) ? 1 : w;
            if (!decode_rm(c, &rip, rex, w8, &o, &regf)) FAULT("ModRM negado");
            c->rip = rip;
            uint64_t cnt = (op == 0xD2 || op == 0xD3) ? (reg_get(c, 1, 1, 0, no_rex)) : 1;
            return shift_exec(c, regf, &o, w8, cnt);
        }
        case 0xD6:
            FAULT("SALC fora do subconjunto x64");
        case 0xE8: {                                   /* CALL rel32 */
            int32_t d;
            if (!fetch_n(c, &rip, &d, 4)) FAULT("fetch negado");
            if (!push64(c, rip)) FAULT_ADDR(c->r[4], "call: push fora da pilha");
            c->rip = (uint64_t)((int64_t)rip + d);
            return 1;
        }
        case 0xE9: {                                   /* JMP rel32 */
            int32_t d;
            if (!fetch_n(c, &rip, &d, 4)) FAULT("fetch negado");
            c->rip = (uint64_t)((int64_t)rip + d);
            return 1;
        }
        case 0xEB: {                                   /* JMP rel8 */
            int8_t d;
            if (!fetch_n(c, &rip, &d, 1)) FAULT("fetch negado");
            c->rip = (uint64_t)((int64_t)rip + d);
            return 1;
        }
        case 0xF4:                                     /* HLT */
            c->halted = 1;
            c->rip = rip;
            return 1;
        case 0xF5:                                     /* CMC */
            c->rflags ^= BIT_CF;
            c->rip = rip;
            return 1;
        case 0xF6: case 0xF7: {                        /* grupo 3 */
            int w8 = (op == 0xF6) ? 1 : w;
            if (!decode_rm(c, &rip, rex, w8, &o, &regf)) FAULT("ModRM negado");
            switch (regf) {
                case 0: {                              /* TEST r/m, imm */
                    uint64_t imm;
                    if (w8 == 1) { uint8_t v; if (!fetch_n(c, &rip, &v, 1)) FAULT("fetch negado"); imm = v; }
                    else if (p66) { uint16_t v; if (!fetch_n(c, &rip, &v, 2)) FAULT("fetch negado"); imm = v; }
                    else { uint32_t v; if (!fetch_n(c, &rip, &v, 4)) FAULT("fetch negado"); imm = v; }
                    c->rip = rip;
                    uint64_t a;
                    if (!rm_read(c, &o, w8, &a)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                    flags_logic(c, a & imm, w8);
                    c->rip = rip;
                    return 1;
                }
                case 2: case 3: {                      /* NOT / NEG */
                    c->rip = rip;
                    uint64_t a;
                    if (!rm_read(c, &o, w8, &a)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                    if (regf == 2) {
                        if (!rm_write(c, &o, w8, ~a)) FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
                    } else {
                        uint64_t res = (~a + 1) & reg_mask(w8);
                        flags_sub(c, 0, a, res, 0, w8);
                        if (a != 0) c->rflags |= BIT_CF; else c->rflags &= ~BIT_CF;
                        if (!rm_write(c, &o, w8, res)) FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
                    }
                    c->rip = rip;
                    return 1;
                }
                case 4: case 5: {                      /* MUL / IMUL unário */
                    c->rip = rip;
                    uint64_t b;
                    if (!rm_read(c, &o, w8, &b)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                    uint64_t a = reg_get(c, 0, w8, 0, no_rex);
                    int ov = 0;
                    if (w8 == 1) {
                        if (regf == 4) { uint16_t p = (uint16_t)((uint8_t)a * (uint8_t)b); reg_set(c, 0, 2, p, 0, no_rex); ov = (p >> 8) != 0; }
                        else { int16_t p = (int16_t)((int8_t)a * (int8_t)b); reg_set(c, 0, 2, (uint64_t)p, 0, no_rex); ov = p != (int8_t)p; }
                    } else if (w8 == 2) {
                        if (regf == 4) { uint32_t p = (uint32_t)(uint16_t)a * (uint16_t)b; reg_set(c, 0, 2, p, 0, no_rex); reg_set(c, 2, 2, p >> 16, 0, no_rex); ov = (p >> 16) != 0; }
                        else { int32_t p = (int32_t)(int16_t)a * (int16_t)b; reg_set(c, 0, 2, (uint64_t)p, 0, no_rex); reg_set(c, 2, 2, (uint64_t)p >> 16, 0, no_rex); ov = p != (int16_t)p; }
                    } else if (w8 == 4) {
                        if (regf == 4) { uint64_t p = (uint64_t)(uint32_t)a * (uint32_t)b; reg_set(c, 0, 4, p, 0, no_rex); reg_set(c, 2, 4, p >> 32, 0, no_rex); ov = (p >> 32) != 0; }
                        else { int64_t p = (int64_t)(int32_t)a * (int32_t)b; reg_set(c, 0, 4, (uint64_t)p, 0, no_rex); reg_set(c, 2, 4, (uint64_t)p >> 32, 0, no_rex); ov = p != (int32_t)p; }
                    } else {
                        if (regf == 4) {
                            unsigned __int128 p = (unsigned __int128)a * b;
                            c->r[0] = (uint64_t)p; c->r[2] = (uint64_t)(p >> 64);
                            ov = c->r[2] != 0;
                        } else {
                            __int128 p = (__int128)(int64_t)a * (int64_t)b;
                            c->r[0] = (uint64_t)p; c->r[2] = (uint64_t)(p >> 64);
                            ov = (int64_t)p != p;
                        }
                    }
                    c->rflags = (c->rflags & ~(BIT_CF | BIT_OF)) | (ov ? (BIT_CF | BIT_OF) : 0);
                    c->rip = rip;
                    return 1;
                }
                case 6: case 7: {                      /* DIV / IDIV */
                    c->rip = rip;
                    uint64_t b;
                    if (!rm_read(c, &o, w8, &b)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                    if (b == 0) FAULT("divisao por zero");
                    if (w8 == 1) {
                        uint16_t num = (uint16_t)reg_get(c, 0, 2, 0, no_rex);
                        if (regf == 6) {
                            uint16_t q = num / (uint8_t)b, r = num % (uint8_t)b;
                            if (q > 0xFF) FAULT("estouro de divisao");
                            reg_set(c, 0, 1, q, 0, no_rex);
                            reg_set(c, 0, 1, r, 1, 0);   /* AH = resto */
                            /* remonta AX: AL=q AH=r */
                            c->r[0] = (c->r[0] & ~0xFFFFull) | ((uint64_t)r << 8) | (q & 0xFF);
                        } else {
                            int16_t num_s = (int16_t)num;
                            int8_t den = (int8_t)b;
                            int16_t q = (int16_t)(num_s / den), r = (int16_t)(num_s % den);
                            if (q != (int8_t)q) FAULT("estouro de divisao");
                            c->r[0] = (c->r[0] & ~0xFFFFull) | ((uint64_t)(uint16_t)r << 8) | (uint8_t)q;
                        }
                    } else if (w8 == 2) {
                        uint32_t num = ((uint32_t)reg_get(c, 2, 2, 0, no_rex) << 16) | reg_get(c, 0, 2, 0, no_rex);
                        if (regf == 6) {
                            uint32_t q = num / (uint16_t)b, r = num % (uint16_t)b;
                            if (q > 0xFFFF) FAULT("estouro de divisao");
                            reg_set(c, 0, 2, q, 0, no_rex);
                            reg_set(c, 2, 2, r, 0, no_rex);
                        } else {
                            int32_t num_s = (int32_t)num;
                            int16_t den = (int16_t)b;
                            int32_t q = num_s / den, r = num_s % den;
                            if (q != (int16_t)q) FAULT("estouro de divisao");
                            reg_set(c, 0, 2, (uint64_t)q, 0, no_rex);
                            reg_set(c, 2, 2, (uint64_t)r, 0, no_rex);
                        }
                    } else if (w8 == 4) {
                        uint64_t num = ((uint64_t)reg_get(c, 2, 4, 0, no_rex) << 32) | reg_get(c, 0, 4, 0, no_rex);
                        if (regf == 6) {
                            uint64_t q = num / (uint32_t)b, r = num % (uint32_t)b;
                            if (q > 0xFFFFFFFFull) FAULT("estouro de divisao");
                            reg_set(c, 0, 4, q, 0, no_rex);
                            reg_set(c, 2, 4, r, 0, no_rex);
                        } else {
                            int64_t num_s = (int64_t)num;
                            int32_t den = (int32_t)b;
                            int64_t q = num_s / den, r = num_s % den;
                            if (q != (int32_t)q) FAULT("estouro de divisao");
                            reg_set(c, 0, 4, (uint64_t)q, 0, no_rex);
                            reg_set(c, 2, 4, (uint64_t)r, 0, no_rex);
                        }
                    } else {
                        if (regf == 6) {
                            /* DIV 64: dividendo RDX:RAX (128-bit sem sinal) */
                            unsigned __int128 num = ((unsigned __int128)c->r[2] << 64) | c->r[0];
                            unsigned __int128 q = num / b, r = num % b;
                            if (q >> 64) FAULT("estouro de divisao");
                            c->r[0] = (uint64_t)q; c->r[2] = (uint64_t)r;
                        } else {
                            /* IDIV 64: dividendo RDX:RAX (128-bit sinalizado) */
                            __int128 num = ((__int128)(int64_t)c->r[2] << 64) | c->r[0];
                            int64_t den = (int64_t)b;
                            __int128 q = num / den, r = num % den;
                            if (q != (int64_t)q) FAULT("estouro de divisao");
                            c->r[0] = (uint64_t)q; c->r[2] = (uint64_t)r;
                        }
                    }
                    c->rip = rip;
                    return 1;
                }
                default:
                    FAULT("grupo F6/F7 com subcodigo nao suportado");
            }
        }
        case 0xFE: case 0xFF: {                        /* INC/DEC/CALL/JMP/PUSH grupo 4/5 */
            int w8 = (op == 0xFE) ? 1 : w;
            if (!decode_rm(c, &rip, rex, w8, &o, &regf)) FAULT("ModRM negado");
            c->rip = rip;
            switch (regf) {
                case 0: case 1: {                      /* INC / DEC (CF intacto) */
                    uint64_t a;
                    if (!rm_read(c, &o, w8, &a)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                    uint64_t cf = c->rflags & BIT_CF;
                    uint64_t res = (regf == 0) ? (a + 1) : (a - 1);
                    if (regf == 0) flags_add(c, a, 1, res, 0, w8);
                    else flags_sub(c, a, 1, res, 0, w8);
                    c->rflags = (c->rflags & ~BIT_CF) | cf;
                    if (!rm_write(c, &o, w8, res)) FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
                    c->rip = rip;
                    return 1;
                }
                case 2: {                              /* CALL r/m64 */
                    uint64_t t;
                    if (!rm_read(c, &o, 8, &t)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                    if (!push64(c, rip)) FAULT_ADDR(c->r[4], "call: push fora da pilha");
                    c->rip = t;
                    return 1;
                }
                case 4: {                              /* JMP r/m64 */
                    uint64_t t;
                    if (!rm_read(c, &o, 8, &t)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                    c->rip = t;
                    return 1;
                }
                case 6: {                              /* PUSH r/m */
                    uint64_t v;
                    if (!rm_read(c, &o, w8 == 1 ? 8 : w, &v)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                    if (!push64(c, v)) FAULT_ADDR(c->r[4], "push fora da pilha");
                    c->rip = rip;
                    return 1;
                }
                default:
                    FAULT("grupo FF com subcodigo nao suportado (call/jmp far)");
            }
        }
        case 0x9E: case 0x9F:                          /* SAHF/LAHF */
            FAULT("sahf/lahf fora do subconjunto x64");
        case 0xF8: c->rflags &= ~BIT_CF; c->rip = rip; return 1;   /* CLC */
        case 0xF9: c->rflags |=  BIT_CF; c->rip = rip; return 1;   /* STC */
        case 0xFC: c->rflags &= ~(1ull << 10); c->rip = rip; return 1; /* CLD */
        case 0xFD: c->rflags |=  (1ull << 10); c->rip = rip; return 1; /* STD */
        case 0xFA: case 0xFB:
            FAULT("cli/sti fora do subconjunto x64");
        case 0xD9: {                                   /* x87: FLD/FSTP m32, FLDCW/FNSTCW, FSQRT */
            /* pilha x87 real (8 regs 80-bit) — ops exigidas pelo formatter %f
             * do MinGW (fldl/fldt/fstpl/fstpt/fsqrt); resto = fault honesto */
            if (!decode_rm(c, &rip, rex, 2, &o, &regf)) FAULT("ModRM negado");
            c->rip = rip;
            if (o.is_reg && regf == 7 && o.reg == 2) {  /* FSQRT (D9 FA) */
                c->fst[c->fpu_top] = __builtin_sqrtl(c->fst[c->fpu_top]);
                c->rip = rip;
                return 1;
            }
            if (o.is_reg) FAULT("x87: operacao em registrador fora do subconjunto");
            if (regf == 7) {                             /* FNSTCW m16 */
                uint64_t cw = c->fpu_cw;
                if (!mem_write(c, ea_of(c, &o), 2, cw))
                    FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
                c->rip = rip;
                return 1;
            }
            if (regf == 5) {                             /* FLDCW m16 */
                uint64_t cw;
                if (!mem_read(c, ea_of(c, &o), 2, &cw))
                    FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                c->fpu_cw = (uint16_t)cw;
                c->rip = rip;
                return 1;
            }
            if (regf == 0) {                             /* FLD m32 */
                uint64_t v;
                if (!mem_read(c, ea_of(c, &o), 4, &v))
                    FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                float f;
                memcpy(&f, &v, 4);
                fpu_push(c, (long double)f);
                c->rip = rip;
                return 1;
            }
            if (regf == 3) {                             /* FSTP m32 */
                long double v = fpu_pop(c);
                float f = (float)v;
                uint64_t w;
                memcpy(&w, &f, 4);
                if (!mem_write(c, ea_of(c, &o), 4, w))
                    FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
                c->rip = rip;
                return 1;
            }
            FAULT("x87: operacao fora do subconjunto (so load/store/sqrt)");
        }
        case 0xDB: {                                   /* x87: FNINIT/FNCLEX/FLD m80/FSTP m80 */
            uint8_t mb;
            if (!fetch(c, &rip, &mb)) FAULT("fetch negado");
            if (mb == 0xE3) {                            /* FNINIT (9B DB E3 = FINIT) */
                (void)pwait;
                c->fpu_cw = 0x037F; c->fpu_sw = 0; c->fpu_tw = 0xFFFF;
                c->fpu_top = 0;
                c->rip = rip;
                return 1;
            }
            if (mb == 0xE2) {                            /* FNCLEX */
                c->fpu_sw &= (uint16_t)~0x80FFu;
                c->rip = rip;
                return 1;
            }
            /* mem forms: FLD m80 (/5) e FSTP m80 (/7) */
            {
                int mmod = (mb >> 6) & 3, mreg = (mb >> 3) & 7, mrm = mb & 7;
                (void)mrm;
                if (mmod != 3 && mreg == 5) {            /* FLD m80 */
                    uint8_t b[10];
                    uint64_t ea;
                    rip--;   /* devolve o ModRM para o decode_rm */
                    if (!decode_rm(c, &rip, rex, 10, &o, &regf)) FAULT("ModRM negado");
                    c->rip = rip;
                    ea = ea_of(c, &o);
                    for (int k = 0; k < 10; k += 2) {
                        uint64_t w;
                        if (!mem_read(c, ea + (uint64_t)k, 2, &w))
                            FAULT_ADDR(ea, "leitura fora do espaco ou negada");
                        b[k] = (uint8_t)w;
                        b[k + 1] = (uint8_t)(w >> 8);
                    }
                    fpu_push80(c, b);
                    return 1;
                }
                if (mmod != 3 && mreg == 7) {            /* FSTP m80 */
                    rip--;   /* devolve o ModRM para o decode_rm */
                    if (!decode_rm(c, &rip, rex, 10, &o, &regf)) FAULT("ModRM negado");
                    c->rip = rip;
                    uint8_t b[10];
                    fpu_pop80(c, b);
                    uint64_t ea = ea_of(c, &o);
                    for (int k = 0; k < 10; k += 2) {
                        uint64_t w = (uint64_t)b[k] | ((uint64_t)b[k + 1] << 8);
                        if (!mem_write(c, ea + (uint64_t)k, 2, w))
                            FAULT_ADDR(ea, "escrita fora do espaco ou negada");
                    }
                    return 1;
                }
            }
            FAULT("x87: operacao fora do subconjunto (so load/store/sqrt)");
        }
        case 0xDD: {                                   /* x87: FLD/FST/FSTP m64, FNSTSW */
            if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
            c->rip = rip;
            if (o.is_reg) FAULT("x87: operacao em registrador fora do subconjunto");
            if (regf == 7) {                             /* FNSTSW m64 (16 bits) */
                uint64_t sw = c->fpu_sw;
                if (!mem_write(c, ea_of(c, &o), 2, sw))
                    FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
                c->rip = rip;
                return 1;
            }
            if (regf == 0) {                             /* FLD m64 (fldl) */
                uint64_t v;
                if (!mem_read(c, ea_of(c, &o), 8, &v))
                    FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                double d;
                memcpy(&d, &v, 8);
                fpu_push(c, (long double)d);
                c->rip = rip;
                return 1;
            }
            if (regf == 2) {                             /* FST m64 */
                long double v = fpu_peek(c, 0);
                double d = (double)v;
                uint64_t w;
                memcpy(&w, &d, 8);
                if (!mem_write(c, ea_of(c, &o), 8, w))
                    FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
                c->rip = rip;
                return 1;
            }
            if (regf == 3) {                             /* FSTP m64 (fstpl) */
                long double v = fpu_pop(c);
                double d = (double)v;
                uint64_t w;
                memcpy(&w, &d, 8);
                if (!mem_write(c, ea_of(c, &o), 8, w))
                    FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
                c->rip = rip;
                return 1;
            }
            FAULT("x87: operacao fora do subconjunto (so load/store/sqrt)");
        }
        case 0xDF: {                                   /* x87: FNSTSW AX */
            uint8_t mb;
            if (!fetch(c, &rip, &mb)) FAULT("fetch negado");
            if (mb == 0xE0) {                            /* FNSTSW AX */
                c->r[0] = (c->r[0] & ~0xFFFFull) | c->fpu_sw;
                c->rip = rip;
                return 1;
            }
            FAULT("x87: operacao fora do subconjunto (so load/store/sqrt)");
        }
        case 0x0F: {                                   /* escapes de 2 bytes */
            uint8_t op2;
            if (!fetch(c, &rip, &op2)) FAULT("fetch negado");
            /* endbr64/endbr32: F3 0F 1E FA/CF = NOP */
            if (op2 == 0x1E && prep == 0xF3) {
                uint8_t mb;
                if (!fetch(c, &rip, &mb)) FAULT("fetch negado");
                c->rip = rip;
                return 1;
            }
            /* NOP multi-byte: 0F 1F /0 */
            if (op2 == 0x1F) {
                if (!decode_rm(c, &rip, rex, w, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                return 1;
            }
            if (op2 >= 0x80 && op2 <= 0x8F) {          /* Jcc rel32 */
                int32_t d;
                if (!fetch_n(c, &rip, &d, 4)) FAULT("fetch negado");
                if (cond_true(c, op2 & 15)) c->rip = (uint64_t)((int64_t)rip + d);
                else c->rip = rip;
                return 1;
            }
            if (op2 >= 0x90 && op2 <= 0x9F) {          /* SETcc r/m8 */
                if (!decode_rm(c, &rip, rex, 1, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                if (!rm_write(c, &o, 1, cond_true(c, op2 & 15) ? 1 : 0))
                    FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
                c->rip = rip;
                return 1;
            }
            if (op2 >= 0x40 && op2 <= 0x4F) {          /* CMOVcc r, r/m */
                if (!decode_rm(c, &rip, rex, w, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint64_t v;
                if (!rm_read(c, &o, w, &v)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                if (cond_true(c, op2 & 15))
                    reg_set(c, regf, w, v, 0, no_rex);
                return 1;
            }
            if (op2 == 0xBC || op2 == 0xBD) {          /* BSF/TZCNT (BC) e BSR (BD) */
                if (!decode_rm(c, &rip, rex, w, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint64_t v;
                if (!rm_read(c, &o, w, &v)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                v &= reg_mask(w);
                int bits = width_bits(w);
                uint64_t f = c->rflags & ~(BIT_CF | BIT_ZF | BIT_OF | BIT_SF | BIT_PF);
                if (prep == 0xF3 && op2 == 0xBC) {       /* TZCNT: conta zeros à direita */
                    uint64_t n = 0;
                    while (n < (uint64_t)bits && ((v >> n) & 1) == 0) n++;
                    if (v == 0) f |= BIT_CF | BIT_ZF;
                    else if (n == 0) f |= BIT_ZF;
                    reg_set(c, regf, w, n, 0, no_rex);
                } else if (op2 == 0xBC) {                /* BSF */
                    if (v == 0) {
                        f |= BIT_ZF;                     /* destino indefinido: zera */
                        reg_set(c, regf, w, 0, 0, no_rex);
                    } else {
                        uint64_t n = 0;
                        while (((v >> n) & 1) == 0) n++;
                        reg_set(c, regf, w, n, 0, no_rex);
                    }
                } else {                                 /* BSR */
                    if (v == 0) {
                        f |= BIT_ZF;
                        reg_set(c, regf, w, 0, 0, no_rex);
                    } else {
                        int n = bits - 1;
                        while (((v >> n) & 1) == 0) n--;
                        reg_set(c, regf, w, (uint64_t)n, 0, no_rex);
                    }
                }
                c->rflags = f | 0x202;
                return 1;
            }
            if (op2 == 0xB0 || op2 == 0xB1) {          /* CMPXCHG r/m, r */
                (void)plock;
                int cw = (op2 == 0xB0) ? 1 : w;
                if (!decode_rm(c, &rip, rex, cw, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint64_t dst;
                if (!rm_read(c, &o, cw, &dst)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                uint64_t acc = reg_get(c, 0, cw, 0, no_rex);
                uint64_t res = (dst - acc) & reg_mask(cw);
                flags_sub(c, dst, acc, res, 0, cw);      /* como CMP dst, acc */
                if (res == 0) {
                    /* acumulador == destino: grava o registrador-fonte */
                    if (!rm_write(c, &o, cw, reg_get(c, regf, cw, 0, no_rex)))
                        FAULT_ADDR(ea_of(c, &o), "escrita fora do espaco ou negada");
                } else {
                    reg_set(c, 0, cw, dst, 0, no_rex);   /* devolve destino em AL/AX/EAX/RAX */
                }
                c->rip = rip;
                return 1;
            }
            if (op2 == 0xAF) {                         /* IMUL r, r/m */
                if (!decode_rm(c, &rip, rex, w, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint64_t b;
                if (!rm_read(c, &o, w, &b)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                int64_t prod = sext(reg_get(c, regf, w, 0, no_rex), w) * sext(b, w);
                reg_set(c, regf, w, (uint64_t)prod, 0, no_rex);
                int64_t trunc = sext((uint64_t)prod, w);
                int ov = (trunc != prod);
                c->rflags = (c->rflags & ~(BIT_CF | BIT_OF)) | (ov ? (BIT_CF | BIT_OF) : 0);
                c->rip = rip;
                return 1;
            }
            if (op2 == 0xB6 || op2 == 0xB7 ||          /* MOVZX */
                op2 == 0xBE || op2 == 0xBF) {          /* MOVSX */
                int srcw = (op2 == 0xB6 || op2 == 0xBE) ? 1 : 2;
                int sign = (op2 >= 0xBE);
                if (!decode_rm(c, &rip, rex, srcw, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint64_t v;
                if (!rm_read(c, &o, srcw, &v)) FAULT_ADDR(ea_of(c, &o), "leitura fora do espaco ou negada");
                int dw = (rex & 0x8) ? 8 : (p66 ? 2 : 4);
                uint64_t outv = sign ? (uint64_t)sext(v, srcw) : (v & reg_mask(srcw));
                reg_set(c, regf, dw, outv, 0, no_rex);
                c->rip = rip;
                return 1;
            }
            /* ---------- GRUPO 3: SSE2 real (escalar/convert/compare/unpack) ------
             * Exigido por PEs MinGW reais (-O2): movsd/movss, add/sub/mul/div/sqrt
             * escalares, cvtsi2sd/cvtsi2ss/cvtss2sd/cvttsd2si, comisd/ucomisd,
             * punpckldq. Packed aritmético 66 (addpd...) continua FORA do subconjunto
             * (fault honesto) até um PE real exigir. */
            if ((op2 == 0x51 || op2 == 0x58 || op2 == 0x59 ||
                 op2 == 0x5C || op2 == 0x5E) && (prep == 0xF2 || prep == 0xF3)) {
                int dbl = (prep == 0xF2);               /* F2=sd F3=ss */
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint8_t s[16];
                if (o.is_reg) memcpy(s, c->xmm[o.reg & 15], 16);
                else if (!mem_read16(c, ea_of(c, &o), s))
                    FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                if (dbl) {
                    double a, b, r = 0;
                    memcpy(&a, c->xmm[regf], 8);
                    memcpy(&b, s, 8);
                    switch (op2) {
                    case 0x51: r = __builtin_sqrt(b); break;   /* SQRTSD: sqrt(src) */
                    case 0x58: r = a + b; break;               /* ADDSD */
                    case 0x59: r = a * b; break;               /* MULSD */
                    case 0x5C: r = a - b; break;               /* SUBSD */
                    case 0x5E: r = a / b; break;               /* DIVSD */
                    }
                    memcpy(c->xmm[regf], &r, 8);
                } else {
                    float a, b, r = 0;
                    memcpy(&a, c->xmm[regf], 4);
                    memcpy(&b, s, 4);
                    switch (op2) {
                    case 0x51: r = __builtin_sqrtf(b); break;  /* SQRTSS */
                    case 0x58: r = a + b; break;               /* ADDSS */
                    case 0x59: r = a * b; break;               /* MULSS */
                    case 0x5C: r = a - b; break;               /* SUBSS */
                    case 0x5E: r = a / b; break;               /* DIVSS */
                    }
                    memcpy(c->xmm[regf], &r, 4);
                }
                return 1;
            }
            if (op2 == 0x2A && prep == 0xF2) {          /* CVTSI2SD xmm, r/m32-64 */
                int wv = (rex & 0x8) ? 8 : 4;
                if (!decode_rm(c, &rip, rex, wv, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint64_t v;
                if (!rm_read(c, &o, wv, &v)) FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                double d = (wv == 8) ? (double)(int64_t)v : (double)(int32_t)(uint32_t)v;
                memcpy(c->xmm[regf], &d, 8);            /* baixo 64; alto preservado */
                return 1;
            }
            if (op2 == 0x2A && prep == 0xF3) {          /* CVTSI2SS xmm, r/m32-64 */
                int wv = (rex & 0x8) ? 8 : 4;
                if (!decode_rm(c, &rip, rex, wv, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint64_t v;
                if (!rm_read(c, &o, wv, &v)) FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                float f = (wv == 8) ? (float)(int64_t)v : (float)(int32_t)(uint32_t)v;
                memcpy(c->xmm[regf], &f, 4);            /* baixo 32; alto 127:32
                                                         * PRESERVADO (Intel SDM) */
                return 1;
            }
            if (op2 == 0x5A && prep == 0xF3) {          /* CVTSS2SD xmm, xmm/m32 */
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint8_t s[16];
                if (o.is_reg) memcpy(s, c->xmm[o.reg & 15], 16);
                else if (!mem_read16(c, ea_of(c, &o), s))
                    FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                float f;
                double d;
                memcpy(&f, s, 4);
                d = (double)f;
                memcpy(c->xmm[regf], &d, 8);
                return 1;
            }
            if (op2 == 0x5A && prep == 0xF2) {          /* CVTSD2SS xmm, xmm/m64 */
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint8_t s[16];
                if (o.is_reg) memcpy(s, c->xmm[o.reg & 15], 16);
                else if (!mem_read16(c, ea_of(c, &o), s))
                    FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                double d;
                float f;
                memcpy(&d, s, 8);
                f = (float)d;
                memcpy(c->xmm[regf], &f, 4);
                return 1;
            }
            if ((op2 == 0x2C || op2 == 0x2D) && (prep == 0xF2 || prep == 0xF3)) {
                /* CVTTSD2SI/CVTSD2SI (F2) e CVTTSS2SI/CVTSS2SI (F3) */
                int dbl = (prep == 0xF2);
                int wv = (rex & 0x8) ? 8 : 4;
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint8_t s[16];
                if (o.is_reg) memcpy(s, c->xmm[o.reg & 15], 16);
                else if (!mem_read16(c, ea_of(c, &o), s))
                    FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                double d;
                if (dbl) memcpy(&d, s, 8);
                else { float f; memcpy(&f, s, 4); d = (double)f; }
                uint64_t res;
                if (op2 == 0x2C) {                      /* truncate */
                    if (wv == 8) res = (uint64_t)(int64_t)d;
                    else res = (uint64_t)(int32_t)d;
                } else {                                /* round nearest */
                    long long q = (long long)(d + (d >= 0 ? 0.5 : -0.5));
                    res = (wv == 8) ? (uint64_t)q : (uint64_t)(int32_t)q;
                }
                reg_set(c, regf, wv, res, 0, no_rex);
                return 1;
            }
            if (op2 == 0x6C && p66 && !prep) {         /* PUNPCKLQDQ xmm, xmm/m128 */
                /* G47: gap OBSERVADO por hello_wait_multiple.exe (EXECUTION STOPPED
                 * em 66 0F 6C). Intel SDM: DEST[63:0] <- SRC1[63:0];
                 * DEST[127:64] <- SRC2[63:0]. Forma legada SSE (sem VEX). */
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint8_t s[16];
                if (o.is_reg) memcpy(s, c->xmm[o.reg & 15], 16);
                else if (!mem_read16(c, ea_of(c, &o), s))
                    FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                memcpy(c->xmm[regf] + 8, s, 8);        /* [127:64] <- src[63:0];
                                                        * [63:0] preservado */
                return 1;
            }
            /* G88: packed FP SSE — gap OBSERVADO por sse.exe / gl6.exe / hello_gl*.
             * PS = 0F xx sem prefixo (4x float32); PD = 66 0F xx (2x float64).
             * Implementado fiel ao Intel SDM: ADDPS/PD 0x58, MULPS/PD 0x59,
             * SUBPS/PD 0x5C, MINPS/PD 0x5D, DIVPS/PD 0x5E, MAXPS/PD 0x5F,
             * SQRTPS/PD 0x51, ANDPS/PD 0x54, ANDNPS/PD 0x55, ORPS/PD 0x56.
             * XORPS/PD já em 0x57. Forma reg-reg e reg-mem128. */
            if ((op2 == 0x51 || op2 == 0x54 || op2 == 0x55 || op2 == 0x56 ||
                 op2 == 0x58 || op2 == 0x59 || op2 == 0x5C || op2 == 0x5D ||
                 op2 == 0x5E || op2 == 0x5F) && !prep) {
                int is_pd = p66 ? 1 : 0;               /* 0=PS, 1=PD */
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint8_t sb[16];
                if (o.is_reg) memcpy(sb, c->xmm[o.reg & 15], 16);
                else if (!mem_read16(c, ea_of(c, &o), sb))
                    FAULT_ADDR(ea_of(c, &o), "sse packed: leitura negada");
                if (is_pd) {
                    double da[2], db[2], dr[2];
                    memcpy(da, c->xmm[regf], 16);
                    memcpy(db, sb, 16);
                    for (int i=0;i<2;i++) {
                        switch (op2) {
                        case 0x51: dr[i]=__builtin_sqrt(db[i]); break;
                        case 0x58: dr[i]=da[i]+db[i]; break;
                        case 0x59: dr[i]=da[i]*db[i]; break;
                        case 0x5C: dr[i]=da[i]-db[i]; break;
                        case 0x5D: dr[i]= da[i]<db[i]?da[i]:db[i]; break;
                        case 0x5E: dr[i]=da[i]/db[i]; break;
                        case 0x5F: dr[i]= da[i]>db[i]?da[i]:db[i]; break;
                        default: dr[i]=0; break;
                        }
                    }
                    if (op2==0x54 || op2==0x55 || op2==0x56) {
                        uint64_t a64[2], b64[2], r64[2];
                        memcpy(a64, c->xmm[regf], 16);
                        memcpy(b64, sb, 16);
                        for (int i=0;i<2;i++) {
                            if (op2==0x54) r64[i]=a64[i]&b64[i];
                            else if (op2==0x55) r64[i]=(~a64[i])&b64[i];
                            else r64[i]=a64[i]|b64[i];
                        }
                        memcpy(c->xmm[regf], r64, 16);
                    } else {
                        memcpy(c->xmm[regf], dr, 16);
                    }
                } else {
                    float fa[4], fb[4], fr[4];
                    memcpy(fa, c->xmm[regf], 16);
                    memcpy(fb, sb, 16);
                    for (int i=0;i<4;i++) {
                        switch (op2) {
                        case 0x51: fr[i]=__builtin_sqrtf(fb[i]); break;
                        case 0x58: fr[i]=fa[i]+fb[i]; break;
                        case 0x59: fr[i]=fa[i]*fb[i]; break;
                        case 0x5C: fr[i]=fa[i]-fb[i]; break;
                        case 0x5D: fr[i]= fa[i]<fb[i]?fa[i]:fb[i]; break;
                        case 0x5E: fr[i]=fa[i]/fb[i]; break;
                        case 0x5F: fr[i]= fa[i]>fb[i]?fa[i]:fb[i]; break;
                        default: fr[i]=0; break;
                        }
                    }
                    if (op2==0x54 || op2==0x55 || op2==0x56) {
                        uint32_t a32[4], b32[4], r32[4];
                        memcpy(a32, c->xmm[regf], 16);
                        memcpy(b32, sb, 16);
                        for (int i=0;i<4;i++) {
                            if (op2==0x54) r32[i]=a32[i]&b32[i];
                            else if (op2==0x55) r32[i]=(~a32[i])&b32[i];
                            else r32[i]=a32[i]|b32[i];
                        }
                        memcpy(c->xmm[regf], r32, 16);
                    } else {
                        memcpy(c->xmm[regf], fr, 16);
                    }
                }
                return 1;
            }
            if ((op2 == 0x2E || op2 == 0x2F) && (prep == 0xF2 || prep == 0xF3 || p66 || (!prep && !p66))) {
                /* COMIS[U]SD (66/F2 0F 2E/2F) e COMIS[U]SS (0F/F3 0F 2E/2F) */
                int dbl = (prep == 0xF2) || (p66 && !prep);
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint8_t s[16];
                if (o.is_reg) memcpy(s, c->xmm[o.reg & 15], 16);
                else if (!mem_read16(c, ea_of(c, &o), s))
                    FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                int unordered = 0, less = 0, equal = 0;
                if (dbl) {
                    double a, b;
                    memcpy(&a, c->xmm[regf], 8);
                    memcpy(&b, s, 8);
                    unordered = __builtin_isnan(a) || __builtin_isnan(b);
                    less = a < b;
                    equal = a == b;
                } else {
                    float a, b;
                    memcpy(&a, c->xmm[regf], 4);
                    memcpy(&b, s, 4);
                    unordered = __builtin_isnan(a) || __builtin_isnan(b);
                    less = a < b;
                    equal = a == b;
                }
                uint64_t f = c->rflags & ~(BIT_CF | BIT_ZF | BIT_PF | BIT_OF | BIT_SF);
                if (unordered) f |= BIT_CF | BIT_ZF | BIT_PF;
                else {
                    if (less) f |= BIT_CF;
                    if (equal) f |= BIT_ZF;
                }
                c->rflags = f | 0x202;
                return 1;
            }
            if (op2 == 0x13) {                         /* MOVLPS/MOVLPD store */
                /* GRUPO 19: MOVLPS (0F 13) e MOVLPD (66 0F 13) — forma store:
                 * grava os 64 bits BAIXOS do registrador XMM em m64, no
                 * endereço efetivo do convidado, exatamente 8 bytes
                 * little-endian. Mesmas duas codificações (com/sem 66): o
                 * store é idêntico; a forma load (0F 12) continua fora do
                 * subconjunto. Só forma memória (como no x64). */
                if (prep) FAULT("movlps/movlpd com prefixo invalido");
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                if (o.is_reg) FAULT("movlps/movlpd exige operando de memoria");
                if (!mem_write(c, ea_of(c, &o), 8, get_le(c->xmm[regf], 8)))
                    FAULT_ADDR(ea_of(c, &o), "sse: escrita negada ou fora do espaco");
                return 1;
            }
            if (op2 == 0x14) {                         /* UNPCKLPD (66) / UNPCKLPS */
                if (!p66 && !(!prep && !p66)) FAULT("unpckl so com 66 (pd) ou nu (ps)");
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint8_t s[16], d[16];
                if (o.is_reg) memcpy(s, c->xmm[o.reg & 15], 16);
                else if (!mem_read16(c, ea_of(c, &o), s))
                    FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                memcpy(d, c->xmm[regf], 16);
                if (p66) {                             /* UNPCKLPD: dst[127:64]=src[63:0] */
                    memcpy(c->xmm[regf] + 8, s, 8);
                } else {                               /* UNPCKLPS: 4 words interleave */
                    memcpy(c->xmm[regf] + 4, s, 4);
                    memcpy(c->xmm[regf] + 8, d + 8, 4);
                    memcpy(c->xmm[regf] + 12, s + 8, 4);
                }
                return 1;
            }
            if (op2 == 0x62 && p66 && !prep) {         /* PUNPCKLDQ xmm, xmm/m128 */
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                uint8_t s[16], d[16];
                if (o.is_reg) memcpy(s, c->xmm[o.reg & 15], 16);
                else if (!mem_read16(c, ea_of(c, &o), s))
                    FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                memcpy(d, c->xmm[regf], 16);
                memcpy(c->xmm[regf] + 0, d + 0, 4);
                memcpy(c->xmm[regf] + 4, s + 0, 4);
                memcpy(c->xmm[regf] + 8, d + 4, 4);
                memcpy(c->xmm[regf] + 12, s + 4, 4);
                return 1;
            }
            if (op2 == 0x70) {                         /* PSHUFD xmm, xmm/m128, imm8 */
                if (!p66 || prep)
                    FAULT("shuffle SSE fora do subconjunto (so pshufd 66 0F 70)");
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                uint8_t imm;
                if (!fetch(c, &rip, &imm)) FAULT("fetch negado");
                c->rip = rip;
                uint8_t s[16], d[16];
                if (o.is_reg) memcpy(s, c->xmm[o.reg & 15], 16);
                else if (!mem_read16(c, ea_of(c, &o), s))
                    FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                for (int i = 0; i < 4; i++)
                    memcpy(d + 4 * i, s + 4 * ((imm >> (2 * i)) & 3), 4);
                memcpy(c->xmm[regf], d, 16);
                return 1;
            }
            if (op2 == 0x71 && p66 && !prep) {     /* GRUPO 23: PSRLW xmm, imm8 */
                /* 66 0F 71 /2 ib — packed shift right logical word: o XMM de
                 * destino fica no r/m (mod=11) e o /2 (reg field) identifica
                 * PSRLW; 8 lanes unsigned de 16 bits (little-endian), cada uma
                 * deslocada logicamente >> imm8; imm8 >= 16 zera a lane
                 * (Intel SDM). SOMENTE /2: /4 (PSRAW) e /6 (PSLLW) do mesmo
                 * grupo 0F 71 continuam fora do subconjunto (fault honesto).
                 * Nao altera RFLAGS, MXCSR nem outros registradores (SDM). */
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                uint8_t imm;
                if (!fetch(c, &rip, &imm)) FAULT("fetch negado");
                if (regf != 2) FAULT("0F 71 somente /2 (psrlw) no subconjunto x64");
                if (!o.is_reg) FAULT("psrlw imm8 exige operando registrador xmm");
                c->rip = rip;
                unsigned sh = imm;
                uint8_t d[16];
                memcpy(d, c->xmm[o.reg & 15], 16);
                for (int i = 0; i < 8; i++) {
                    unsigned lane = (unsigned)d[2 * i]
                                  | ((unsigned)d[2 * i + 1] << 8);
                    lane = (sh >= 16) ? 0u : (lane >> sh);
                    d[2 * i] = (uint8_t)(lane & 0xFF);
                    d[2 * i + 1] = (uint8_t)((lane >> 8) & 0xFF);
                }
                memcpy(c->xmm[o.reg & 15], d, 16);
                return 1;
            }
            if (op2 == 0xDB && p66 && !prep) {     /* GRUPO 24: PAND xmm, xmm */
                /* 66 0F DB /r — packed bitwise AND de 128 bits: destino (reg)
                 * &= fonte (r/m). SOMENTE a forma registrador-registrador
                 * (mod=11) observada no hello_gl12 (66 0F DB C2 =
                 * pand %xmm2,%xmm0 em gen_tex); a forma com memoria e as
                 * demais instrucoes do nucleo packed (PADDB 0F FC, PANDN
                 * 0F DF, POR 0F EB...) continuam fora do subconjunto (fault
                 * honesto). Byte a byte sobre o c->xmm[16][16] existente;
                 * nao altera RFLAGS, MXCSR nem outros registradores (SDM). */
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                if (!o.is_reg) FAULT("pand exige forma registrador-registrador (xmm, xmm)");
                c->rip = rip;
                for (int i2 = 0; i2 < 16; i2++)
                    c->xmm[regf][i2] = (uint8_t)(c->xmm[regf][i2]
                                               & c->xmm[o.reg & 15][i2]);
                return 1;
            }
            if (op2 == 0xFC && p66 && !prep) {     /* GRUPO 25: PADDB xmm, xmm */
                /* 66 0F FC /r — packed byte add: 16 lanes de 8 bits com
                 * adicao inteira MODULAR (destino += fonte, truncada em 8
                 * bits: 0xFF+0x01=0x00; NUNCA ha carry para a lane vizinha).
                 * SOMENTE a forma registrador-registrador (mod=11) observada
                 * no hello_gl12 (66 0F FC C1 = paddb %xmm1,%xmm0 em gen_tex);
                 * a forma com memoria e as demais packed (PADDW/PADDD/PADDQ/
                 * PSUBB/PSUBW/..., PANDN, POR) continuam fora do subconjunto
                 * (fault honesto). Byte a byte sobre o c->xmm[16][16]
                 * existente; nao altera RFLAGS (sem CF/OF), MXCSR nem outros
                 * registradores (SDM). */
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                if (!o.is_reg) FAULT("paddb exige forma registrador-registrador (xmm, xmm)");
                c->rip = rip;
                for (int i2 = 0; i2 < 16; i2++)
                    c->xmm[regf][i2] = (uint8_t)(c->xmm[regf][i2]
                                               + c->xmm[o.reg & 15][i2]);
                return 1;
            }
            /* ---------- FASE 3: SSE minimo (XMM 128-bit) ----------
             * MOVUPS (0F 10/11 sem prefixo), MOVAPS (0F 28/29 sem prefixo,
             * alinhamento 16 obrigatorio), MOVDQA (66 0F 6F/7F alinhado),
             * MOVDQU (F3 0F 6F/7F), MOVD/MOVQ gpr->xmm (66 0F 6E [,REX.W]),
             * MOVD/MOVQ xmm->gpr (66 0F 7E [,REX.W]),
             * MOVQ xmm<-xmm/m64 (F3 0F 7E), MOVQ xmm/m64<-xmm (66 0F D6),
             * PXOR (66 0F EF), XORPS (0F 57), XORPD (66 0F 57),
             * PCMPEQD (66 0F 76 — G14: exigido por hello_gl7 do GCC real).
             * FORA do subconjunto (fault honesto): demais aritmetica packed,
             * shuffles, conversoes, MOVSS/MOVSD, MOVUPD/MOVAPD, SSE2+. */
            if (op2 == 0x10 || op2 == 0x11 || op2 == 0x28 || op2 == 0x29 ||
                op2 == 0x57 || op2 == 0x6E || op2 == 0x6F || op2 == 0x76 ||
                op2 == 0x7E || op2 == 0x7F || op2 == 0xD6 || op2 == 0xEF) {
                int algn = 0;
                uint8_t u[16];
                /* validacao de prefixo por opcode (so o subconjunto acima) */
                if (op2 == 0x10 || op2 == 0x11) {
                    /* MOVUPS (00) / MOVUPD (66) / MOVSS (F3) / MOVSD (F2) */
                    if (prep == 0xF2) { /* MOVSD m64/xmm */ }
                    else if (prep == 0xF3) { /* MOVSS m32/xmm */ }
                    else if (p66 && !prep) { /* MOVUPD */ }
                    else if (prep || p66) FAULT("prefixo invalido para movu/s movsd/movss");
                } else if (op2 == 0x28 || op2 == 0x29) {
                    if (prep) FAULT("movaps com prefixo fora do subconjunto");
                    if (p66)  FAULT("movapd SSE fora do subconjunto");
                    algn = 1;
                } else if (op2 == 0x57) {
                    if (prep) FAULT("prefixo invalido para xorps/xorpd");
                } else if (op2 == 0x76) {
                    if (prep) FAULT("prefixo invalido para pcmpeqd");
                    if (!p66) FAULT("pcmpeqd exige prefixo 66");
                } else if (op2 == 0x6E || op2 == 0xD6) {
                    if (prep) FAULT("prefixo invalido para movd/movq");
                    if (op2 == 0x6E && !p66) FAULT("movd/movq gpr->xmm exige prefixo 66");
                    if (op2 == 0xD6 && !p66) FAULT("movq m64<-xmm exige prefixo 66");
                } else if (op2 == 0x6F || op2 == 0x7F) {
                    if (prep == 0xF3) algn = 0;                /* MOVDQU */
                    else if (!prep && p66) algn = 1;           /* MOVDQA */
                    else FAULT("movdqa(66)/movdqu(F3) exigem o prefixo correto");
                } else { /* 0x7E: F3 movq load / 66 movd-q store */
                    if (prep != 0xF3 && !(p66 && !prep))
                        FAULT("7E exige F3 (movq load) ou 66 (movd/movq store)");
                }

                if (op2 == 0x6E) {                     /* MOVD/MOVQ gpr->xmm */
                    int wv = (rex & 0x8) ? 8 : 4;
                    if (!decode_rm(c, &rip, rex, wv, &o, &regf)) FAULT("ModRM negado");
                    c->rip = rip;
                    uint64_t v;
                    if (!rm_read(c, &o, wv, &v)) FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                    memset(c->xmm[regf], 0, 16);
                    set_le(c->xmm[regf], wv, v);
                    return 1;
                }
                if (op2 == 0x7E && p66 && !prep) {     /* MOVD/MOVQ xmm->gpr/m */
                    int wv = (rex & 0x8) ? 8 : 4;
                    if (!decode_rm(c, &rip, rex, wv, &o, &regf)) FAULT("ModRM negado");
                    c->rip = rip;
                    uint64_t v = get_le(c->xmm[regf], wv);
                    if (!rm_write(c, &o, wv, v)) FAULT_ADDR(ea_of(c, &o), "sse: escrita negada ou fora do espaco");
                    return 1;
                }
                if (op2 == 0x7E && prep == 0xF3) {     /* MOVQ xmm <- xmm/m64 */
                    if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                    c->rip = rip;
                    memset(c->xmm[regf], 0, 16);
                    if (o.is_reg) memcpy(c->xmm[regf], c->xmm[o.reg & 15], 8);
                    else {
                        uint64_t v;
                        if (!mem_read(c, ea_of(c, &o), 8, &v))
                            FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                        set_le(c->xmm[regf], 8, v);
                    }
                    return 1;
                }
                if (op2 == 0xD6) {                     /* MOVQ xmm_dest(reg), xmm/m64_src */
                    if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                    c->rip = rip;
                    if (o.is_reg) {
                        /* reg-form: dest = regf, src = rm (Intel MOVQ xmm1, xmm2) */
                        memcpy(c->xmm[regf], c->xmm[o.reg & 15], 8);
                        memset(c->xmm[regf] + 8, 0, 8);
                    } else if (!mem_write(c, ea_of(c, &o), 8, get_le(c->xmm[regf], 8))) {
                        FAULT_ADDR(ea_of(c, &o), "sse: escrita negada ou fora do espaco");
                    }
                    return 1;
                }
                /* load/store 128-bit e ALU xor */
                if (!decode_rm(c, &rip, rex, 8, &o, &regf)) FAULT("ModRM negado");
                c->rip = rip;
                if (op2 == 0x57 || op2 == 0xEF) {      /* XORPS/XORPD/PXOR */
                    if (o.is_reg) memcpy(u, c->xmm[o.reg & 15], 16);
                    else if (!mem_read16(c, ea_of(c, &o), u))
                        FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                    for (int i = 0; i < 16; i++) c->xmm[regf][i] ^= u[i];
                    return 1;
                }
                if (op2 == 0x76) {                     /* PCMPEQD real */
                    if (o.is_reg) memcpy(u, c->xmm[o.reg & 15], 16);
                    else if (!mem_read16(c, ea_of(c, &o), u))
                        FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                    for (int i = 0; i < 4; i++) {
                        uint32_t aa = (uint32_t)get_le(c->xmm[regf] + i * 4, 4);
                        uint32_t bb = (uint32_t)get_le(u + i * 4, 4);
                        set_le(c->xmm[regf] + i * 4, 4,
                               (uint64_t)(aa == bb ? 0xFFFFFFFFu : 0u));
                    }
                    return 1;
                }
                if (op2 == 0x10 || op2 == 0x28 || op2 == 0x6F) {    /* load */
                    int sc = (op2 == 0x10) ? ((prep == 0xF2) ? 8 : (prep == 0xF3) ? 4 : 0) : 0;
                    if (sc) {                          /* MOVSD/MOVSS load: zera o resto */
                        uint8_t s[16];
                        memset(s, 0, 16);
                        if (o.is_reg) {
                            memcpy(s, c->xmm[o.reg & 15], (size_t)sc);
                        } else {
                            uint64_t v = 0;
                            if (!mem_read(c, ea_of(c, &o), sc, &v))
                                FAULT_ADDR(ea_of(c, &o), "sse: leitura negada ou fora do espaco");
                            memcpy(s, &v, (size_t)sc);
                        }
                        memcpy(c->xmm[regf], s, 16);
                        return 1;
                    }
                    if (o.is_reg) memcpy(c->xmm[regf], c->xmm[o.reg & 15], 16);
                    else {
                        uint64_t ea = ea_of(c, &o);
                        if (algn && (ea & 15))
                            FAULT_ADDR(ea, "movaps/movdqa exige alinhamento 16");
                        if (!mem_read16(c, ea, c->xmm[regf]))
                            FAULT_ADDR(ea, "sse: leitura negada ou fora do espaco");
                    }
                    return 1;
                }
                /* store (0x11/0x29/0x7F) */
                {
                    int sc = (op2 == 0x11) ? ((prep == 0xF2) ? 8 : (prep == 0xF3) ? 4 : 0) : 0;
                    if (sc) {                          /* MOVSD/MOVSS store: só o baixo */
                        uint64_t v = 0;
                        memcpy(&v, c->xmm[regf], (size_t)sc);
                        if (o.is_reg) memcpy(c->xmm[o.reg & 15], &v, (size_t)sc);
                        else if (!mem_write(c, ea_of(c, &o), sc, v))
                            FAULT_ADDR(ea_of(c, &o), "sse: escrita negada ou fora do espaco");
                        return 1;
                    }
                    if (o.is_reg) memcpy(c->xmm[o.reg & 15], c->xmm[regf], 16);
                    else {
                        uint64_t ea = ea_of(c, &o);
                        if (algn && (ea & 15))
                            FAULT_ADDR(ea, "movaps/movdqa exige alinhamento 16");
                        if (!mem_write16(c, ea, c->xmm[regf]))
                            FAULT_ADDR(ea, "sse: escrita negada ou fora do espaco");
                    }
                    return 1;
                }
            }
            if (op2 == 0xA2) {                         /* CPUID (virtual determinístico) */
                uint32_t leaf = (uint32_t)c->r[0];
                uint32_t eax = 0, ebx = 0, ecx = 0, edx = 0;
                if (leaf == 0) {
                    /* maior folha suportada + vendor "PORTICO_VRTX" (12 chars) */
                    eax = 1;
                    ebx = (uint32_t)('P' | ('O' << 8) | ('R' << 16) | ('T' << 24));
                    edx = (uint32_t)('I' | ('C' << 8) | ('O' << 16) | ('_' << 24));
                    ecx = (uint32_t)('V' | ('R' << 8) | ('T' << 16) | ('X' << 24));
                } else if (leaf == 1) {
                    /* família 6 modelo 15 passo 0 — identidade virtual fixa */
                    eax = 0x000006F0u;
                    ebx = 0;
                    ecx = 0;
                    /* só o que NÓS emulamos de verdade: TSC(4) CMOV(15) SSE(25) SSE2(26) */
                    edx = (1u << 4) | (1u << 15) | (1u << 25) | (1u << 26);
                }
                /* demais folhas e subfolhas: zeros (CPU virtual documentado;
                 * NUNCA expõe informações do host) */
                reg_set(c, 0, 4, eax, 0, no_rex);
                reg_set(c, 3, 4, ebx, 0, no_rex);
                reg_set(c, 1, 4, ecx, 0, no_rex);
                reg_set(c, 2, 4, edx, 0, no_rex);
                c->rip = rip;
                return 1;
            }
            if (op2 == 0x31) {                         /* RDTSC (determinístico) */
                reg_set(c, 0, 4, (uint32_t)(c->tsc & 0xFFFFFFFFu), 0, no_rex);
                reg_set(c, 2, 4, (uint32_t)(c->tsc >> 32), 0, no_rex);
                c->rip = rip;
                return 1;
            }
            if (op2 == 0x0B || op2 == 0xB9) {
                FAULT("instrucao 0F (ud1/ud2) fora do subconjunto x64");
            }
            {
                char msg[80];
                snprintf(msg, sizeof(msg),
                         "instrucao fora do subconjunto x64 (opcode 0F %02X)", op2);
                return do_fault(c, rip0, op, rex, 0, msg);
            }
        }
        case 0x27: case 0x2F: case 0x37: case 0x3F:
            FAULT("bcd (daa/das/aaa) fora do subconjunto x64");
        case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75:
        case 0x76: case 0x77: case 0x78: case 0x79: case 0x7A: case 0x7B:
        case 0x7C: case 0x7D: case 0x7E: case 0x7F: {  /* Jcc rel8 */
            int8_t d;
            if (!fetch_n(c, &rip, &d, 1)) FAULT("fetch negado");
            if (cond_true(c, op & 15)) c->rip = (uint64_t)((int64_t)rip + d);
            else c->rip = rip;
            return 1;
        }
        case 0xE0: case 0xE1: case 0xE2: case 0xE3:
            FAULT("loops/jecxz fora do subconjunto x64");
        default:
            break;
    }

    {
        char msg[80];
        snprintf(msg, sizeof(msg), "instrucao fora do subconjunto x64 (opcode registrado)");
        return do_fault(c, rip0, op, rex, 0, msg);
    }
#undef FAULT
#undef FAULT_ADDR
}

/* ---------------- API pública ---------------- */

pr_cpu64* pr_cpu64_create_on(uint8_t* mem, uint32_t mem_size) {
    if (!mem) return NULL;
    pr_cpu64* c = (pr_cpu64*)calloc(1, sizeof(pr_cpu64));
    if (!c) return NULL;
    c->mem = mem;
    c->mem_size = mem_size;
    c->rflags = 0x202;                                /* bit 1 sempre 1 */
    return c;
}

void pr_cpu64_destroy(pr_cpu64* c) { free(c); }

void pr_cpu64_set_rip(pr_cpu64* c, uint64_t rip) { if (c) c->rip = rip; }
uint64_t pr_cpu64_rip(const pr_cpu64* c) { return c ? c->rip : 0; }
uint64_t pr_cpu64_reg(const pr_cpu64* c, int idx) { return (c && idx >= 0 && idx < 16) ? c->r[idx] : 0; }
void pr_cpu64_set_reg(pr_cpu64* c, int idx, uint64_t v) {
    if (c && idx >= 0 && idx < 16) c->r[idx] = v;
}

void pr_cpu64_xmm(const pr_cpu64* c, int idx, uint8_t out[16]) {
    if (!c || !out) return;
    memcpy(out, c->xmm[idx & 15], 16);
}

void pr_cpu64_set_xmm(pr_cpu64* c, int idx, const void* v) {
    if (!c || !v) return;
    memcpy(c->xmm[idx & 15], v, 16);
}
uint64_t pr_cpu64_rflags(const pr_cpu64* c) { return c ? c->rflags : 0; }
void pr_cpu64_set_rflags(pr_cpu64* c, uint64_t v) { if (c) c->rflags = v | 0x202; }

void pr_cpu64_set_fs_base(pr_cpu64* c, uint64_t base) { if (c) c->fs_base = base; }
void pr_cpu64_set_gs_base(pr_cpu64* c, uint64_t base) { if (c) c->gs_base = base; }

void pr_cpu64_set_guard(pr_cpu64* c, pr_cpu64_guard_fn fn, void* ud) {
    if (c) { c->guard = fn; c->guard_ud = ud; }
}
void pr_cpu64_set_trap(pr_cpu64* c, pr_cpu64_trap_fn fn, void* ud) {
    if (c) { c->trap = fn; c->trap_ud = ud; }
}

pr_status pr_cpu64_run(pr_cpu64* c, uint64_t budget, uint64_t* executed) {
    if (executed) *executed = 0;
    if (!c) return PR_ERR_INVALID;
    c->halted = 0;
    c->has_fault = 0;
    memset(&c->fault, 0, sizeof(c->fault));
    uint64_t n = 0;
    while (n < budget && !c->halted) {
        if (!step_one(c)) {
            if (executed) *executed = n;
            return PR_ERR_FAULT;
        }
        n++;
    }
    if (executed) *executed = n;
    return PR_OK;
}

int pr_cpu64_halted(const pr_cpu64* c) { return c && c->halted; }

const pr_cpu64_fault* pr_cpu64_last_fault(const pr_cpu64* c) {
    return (c && c->has_fault) ? &c->fault : NULL;
}
