/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

/* Interpretador IA-32 (modo flat, 32 bits) — subconjunto real de instruções.
 * Executa código de verdade, instrução a instrução. NÃO é um emulador de PC:
 * não há dispositivos, nem Win32; a porta de MMIO + traps dão acesso à plataforma.
 * Documentação do subconjunto: portico/pr_cpu.h. */
#include "portico/pr_cpu.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define CPU_MMIO_SLOTS 2

typedef struct mmio_slot {
    int used;
    uint32_t base, size;
    pr_cpu_mmio_read_fn rd;
    pr_cpu_mmio_write_fn wr;
    void* ud;
} mmio_slot;

struct pr_cpu {
    uint8_t* mem;
    uint32_t mem_size;
    uint32_t r[8];
    uint32_t eip;
    uint32_t eflags;
    int halted;
    uint64_t insns;
    pr_cpu_fault fault;
    mmio_slot mmio[CPU_MMIO_SLOTS];
    pr_cpu_trap_fn trap;
    void* trap_ud;
    pr_cpu_guard_fn guard;
    void* guard_ud;
    int trap_stop;
    int break_run;
};

/* ---- helpers ---- */

static pr_status fault(pr_cpu* c, uint32_t eip, uint8_t op, const char* why) {
    c->fault.eip = eip;
    c->fault.opcode = op;
    snprintf(c->fault.reason, sizeof(c->fault.reason), "%s", why);
    return PR_ERR_FAULT;
}

pr_cpu* pr_cpu_create(size_t mem_size) {
    if (mem_size < (1u << 20)) mem_size = 1u << 20;
    pr_cpu* c = (pr_cpu*)calloc(1, sizeof(pr_cpu));
    if (!c) return NULL;
    c->mem = (uint8_t*)calloc(1, mem_size);
    if (!c->mem) { free(c); return NULL; }
    c->mem_size = (uint32_t)mem_size;
    pr_cpu_reset(c);
    return c;
}

void pr_cpu_destroy(pr_cpu* c) {
    if (!c) return;
    free(c->mem);
    free(c);
}

void pr_cpu_reset(pr_cpu* c) {
    if (!c) return;
    memset(c->r, 0, sizeof(c->r));
    c->eip = 0;
    c->eflags = 0x202;
    c->halted = 0;
    c->insns = 0;
    memset(&c->fault, 0, sizeof(c->fault));
    c->trap_stop = 0;
}

uint32_t pr_cpu_reg(const pr_cpu* c, int idx) {
    return (c && idx >= 0 && idx < 8) ? c->r[idx] : 0;
}
void pr_cpu_set_reg(pr_cpu* c, int idx, uint32_t v) {
    if (c && idx >= 0 && idx < 8) c->r[idx] = v;
}
uint32_t pr_cpu_eip(const pr_cpu* c) { return c ? c->eip : 0; }
void pr_cpu_set_eip(pr_cpu* c, uint32_t v) { if (c) c->eip = v; }
uint32_t pr_cpu_eflags(const pr_cpu* c) { return c ? c->eflags : 0; }
void pr_cpu_set_eflags(pr_cpu* c, uint32_t v) { if (c) c->eflags = (v | 0x202) & ~0x1u; }
int pr_cpu_halted(const pr_cpu* c) { return c ? c->halted : 1; }
uint64_t pr_cpu_insns(const pr_cpu* c) { return c ? c->insns : 0; }
uint32_t pr_cpu_mem_size(const pr_cpu* c) { return c ? c->mem_size : 0; }
const pr_cpu_fault* pr_cpu_last_fault(const pr_cpu* c) { return c ? &c->fault : NULL; }

pr_status pr_cpu_set_mmio(pr_cpu* c, int slot, uint32_t base, uint32_t size,
                          pr_cpu_mmio_read_fn rd, pr_cpu_mmio_write_fn wr, void* ud) {
    if (!c || slot < 0 || slot >= CPU_MMIO_SLOTS || size == 0) return PR_ERR_INVALID;
    c->mmio[slot].used = 1;
    c->mmio[slot].base = base;
    c->mmio[slot].size = size;
    c->mmio[slot].rd = rd;
    c->mmio[slot].wr = wr;
    c->mmio[slot].ud = ud;
    return PR_OK;
}

void pr_cpu_set_trap(pr_cpu* c, pr_cpu_trap_fn fn, void* ud) {
    if (!c) return;
    c->trap = fn;
    c->trap_ud = ud;
}

void pr_cpu_set_guard(pr_cpu* c, pr_cpu_guard_fn fn, void* ud) {
    if (!c) return;
    c->guard = fn;
    c->guard_ud = ud;
}

pr_status pr_cpu_load(pr_cpu* c, uint32_t addr, const void* data, size_t len) {
    if (!c || (!data && len)) return PR_ERR_INVALID;
    if ((uint64_t)addr + len > c->mem_size) return PR_ERR_RANGE;
    memcpy(c->mem + addr, data, len);
    return PR_OK;
}

pr_status pr_cpu_read_mem(const pr_cpu* c, uint32_t addr, void* out, size_t len) {
    if (!c || !out) return PR_ERR_INVALID;
    if ((uint64_t)addr + len > c->mem_size) return PR_ERR_RANGE;
    memcpy(out, c->mem + addr, len);
    return PR_OK;
}

uint8_t* pr_cpu_mem(pr_cpu* c) {
    return c ? c->mem : NULL;
}

/* ---- acesso a memória (com MMIO) ---- */

static mmio_slot* find_mmio(pr_cpu* c, uint32_t addr) {
    for (int i = 0; i < CPU_MMIO_SLOTS; i++) {
        mmio_slot* s = &c->mmio[i];
        if (s->used && addr >= s->base && addr < s->base + s->size) return s;
    }
    return NULL;
}

static uint32_t mem_read32(pr_cpu* c, uint32_t addr, int* err) {
    *err = 0;
    mmio_slot* s = find_mmio(c, addr);
    if (s) {
        if (!s->rd) { *err = 1; return 0; }
        return s->rd(s->ud, addr - s->base);
    }
    if ((uint64_t)addr + 4 > c->mem_size) { *err = 1; return 0; }
    if (c->guard && !c->guard(c->guard_ud, addr, 4, PR_CPU_ACC_R)) { *err = 1; return 0; }
    const uint8_t* p = c->mem + addr;
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void mem_write32(pr_cpu* c, uint32_t addr, uint32_t v, int* err) {
    *err = 0;
    mmio_slot* s = find_mmio(c, addr);
    if (s) {
        if (!s->wr) { *err = 1; return; }
        s->wr(s->ud, addr - s->base, v);
        return;
    }
    if ((uint64_t)addr + 4 > c->mem_size) { *err = 1; return; }
    if (c->guard && !c->guard(c->guard_ud, addr, 4, PR_CPU_ACC_W)) { *err = 1; return; }
    uint8_t* p = c->mem + addr;
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint8_t mem_read8(pr_cpu* c, uint32_t addr, int* err) {
    *err = 0;
    mmio_slot* s = find_mmio(c, addr);
    if (s) {
        if (!s->rd) { *err = 1; return 0; }
        return (uint8_t)(s->rd(s->ud, addr - s->base) & 0xFF);
    }
    if ((uint64_t)addr + 1 > c->mem_size) { *err = 1; return 0; }
    if (c->guard && !c->guard(c->guard_ud, addr, 1, PR_CPU_ACC_R)) { *err = 1; return 0; }
    return c->mem[addr];
}

static void mem_write8(pr_cpu* c, uint32_t addr, uint8_t v, int* err) {
    *err = 0;
    mmio_slot* s = find_mmio(c, addr);
    if (s) {
        if (!s->wr) { *err = 1; return; }
        s->wr(s->ud, addr - s->base, (uint32_t)v);
        return;
    }
    if ((uint64_t)addr + 1 > c->mem_size) { *err = 1; return; }
    if (c->guard && !c->guard(c->guard_ud, addr, 1, PR_CPU_ACC_W)) { *err = 1; return; }
    c->mem[addr] = v;
}

static uint16_t mem_read16(pr_cpu* c, uint32_t addr, int* err) {
    uint8_t lo = mem_read8(c, addr, err);
    if (*err) return 0;
    uint8_t hi = mem_read8(c, addr + 1, err);
    return (uint16_t)(lo | (hi << 8));
}

/* ---- flags ---- */

static void set_flag(pr_cpu* c, uint32_t bit, int on) {
    if (on) c->eflags |= bit; else c->eflags &= ~bit;
}

static int parity8(uint32_t v) {
    v &= 0xFF;
    v ^= v >> 4;
    v &= 0xF;
    return ((0x6996 >> v) & 1) == 0; /* 1 = paridade par */
}

static void set_zsp(pr_cpu* c, uint32_t res, int width32) {
    set_flag(c, PR_EFLAGS_ZF, res == 0);
    set_flag(c, PR_EFLAGS_SF, (width32 ? (res >> 31) : ((res >> 7) & 1)) != 0);
    set_flag(c, PR_EFLAGS_PF, parity8(res));
}

/* ALU: which 0=ADD 1=OR 2=ADC 3=SBB 4=AND 5=SUB 6=XOR 7=CMP; w32: 1=32-bit, 0=8-bit */
static uint32_t alu_exec(pr_cpu* c, int which, uint32_t a, uint32_t b, int w32) {
    uint32_t res = 0;
    uint32_t mask = w32 ? 0xFFFFFFFFu : 0xFFu;
    uint32_t sign = w32 ? 0x80000000u : 0x80u;
    a &= mask;
    b &= mask;
    switch (which) {
        case 0: { /* ADD */
            res = (a + b) & mask;
            set_flag(c, PR_EFLAGS_CF, (uint64_t)a + (uint64_t)b > mask);
            set_flag(c, PR_EFLAGS_OF, ((~(a ^ b) & (a ^ res)) & sign) != 0);
            break;
        }
        case 2: { /* ADC */
            uint32_t cin = (c->eflags & PR_EFLAGS_CF) ? 1u : 0u;
            res = (a + b + cin) & mask;
            set_flag(c, PR_EFLAGS_CF, (uint64_t)a + (uint64_t)b + cin > mask);
            set_flag(c, PR_EFLAGS_OF, ((~(a ^ b) & (a ^ res)) & sign) != 0);
            break;
        }
        case 5: { /* SUB */
            res = (a - b) & mask;
            set_flag(c, PR_EFLAGS_CF, a < b);
            set_flag(c, PR_EFLAGS_OF, (((a ^ b) & (a ^ res)) & sign) != 0);
            break;
        }
        case 3: { /* SBB */
            uint32_t cin = (c->eflags & PR_EFLAGS_CF) ? 1u : 0u;
            uint64_t total = (uint64_t)b + cin;
            res = (uint32_t)((a - total) & mask);
            set_flag(c, PR_EFLAGS_CF, (uint64_t)a < total);
            set_flag(c, PR_EFLAGS_OF, (((a ^ (uint32_t)total) & (a ^ res)) & sign) != 0);
            break;
        }
        case 1: res = (a | b) & mask; set_flag(c, PR_EFLAGS_CF, 0); set_flag(c, PR_EFLAGS_OF, 0); break;
        case 4: res = (a & b) & mask; set_flag(c, PR_EFLAGS_CF, 0); set_flag(c, PR_EFLAGS_OF, 0); break;
        case 6: res = (a ^ b) & mask; set_flag(c, PR_EFLAGS_CF, 0); set_flag(c, PR_EFLAGS_OF, 0); break;
        case 7: { /* CMP = SUB sem gravar */
            res = (a - b) & mask;
            set_flag(c, PR_EFLAGS_CF, a < b);
            set_flag(c, PR_EFLAGS_OF, (((a ^ b) & (a ^ res)) & sign) != 0);
            break;
        }
    }
    set_zsp(c, res, w32);
    return res;
}

static int cond_holds(const pr_cpu* c, int cc) {
    int cf = (c->eflags & PR_EFLAGS_CF) != 0;
    int zf = (c->eflags & PR_EFLAGS_ZF) != 0;
    int sf = (c->eflags & PR_EFLAGS_SF) != 0;
    int of = (c->eflags & PR_EFLAGS_OF) != 0;
    switch (cc & 0xF) {
        case 0x0: return of;            /* O */
        case 0x1: return !of;           /* NO */
        case 0x2: return cf;            /* B/C/NAE */
        case 0x3: return !cf;           /* AE/NB/NC */
        case 0x4: return zf;            /* E/Z */
        case 0x5: return !zf;           /* NE/NZ */
        case 0x6: return cf || zf;      /* BE/NA */
        case 0x7: return !cf && !zf;    /* A/NBE */
        case 0x8: return sf;            /* S */
        case 0x9: return !sf;           /* NS */
        case 0xA: return (c->eflags & PR_EFLAGS_PF) != 0;  /* P/PE */
        case 0xB: return (c->eflags & PR_EFLAGS_PF) == 0;  /* NP/PO */
        case 0xC: return sf != of;      /* L/NGE */
        case 0xD: return sf == of;      /* GE/NL */
        case 0xE: return zf || sf != of;/* LE/NG */
        case 0xF: return !zf && sf == of; /* G/NLE */
    }
    return 0;
}

/* ---- registradores de 8 bits: 0..3 = AL..BL; 4..7 = AH..BH ---- */

static uint8_t get_r8(const pr_cpu* c, int idx) {
    idx &= 7;
    if (idx < 4) return (uint8_t)(c->r[idx] & 0xFF);
    return (uint8_t)((c->r[idx - 4] >> 8) & 0xFF);
}

static void set_r8(pr_cpu* c, int idx, uint8_t v) {
    idx &= 7;
    if (idx < 4) {
        c->r[idx] = (c->r[idx] & 0xFFFFFF00u) | v;
    } else {
        c->r[idx - 4] = (c->r[idx - 4] & 0xFFFF00FFu) | ((uint32_t)v << 8);
    }
}

/* ---- busca de bytes ---- */

typedef struct fetcher {
    pr_cpu* c;
    uint32_t ip;
    uint32_t start_eip;
    int err;
    uint8_t last_op;
} fetcher;

static uint8_t f8(fetcher* f) {
    if ((uint64_t)f->ip + 1 > f->c->mem_size) { f->err = 1; return 0; }
    if (f->c->guard && !f->c->guard(f->c->guard_ud, f->ip, 1, PR_CPU_ACC_X)) {
        f->err = 1; return 0;
    }
    return f->c->mem[f->ip++];
}

static uint32_t f32(fetcher* f) {
    uint32_t lo = f8(f);
    uint32_t b1 = f8(f);
    uint32_t b2 = f8(f);
    uint32_t b3 = f8(f);
    return lo | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

/* ---- ModR/M ---- */

typedef struct rm {
    int is_reg;
    int reg;       /* índice 0..7 */
    uint32_t addr; /* quando !is_reg */
    int bad;
} rm_t;

static void decode_rm(fetcher* f, uint8_t modrm, rm_t* out, int* reg_field) {
    int mod = (modrm >> 6) & 3;
    int reg = (modrm >> 3) & 7;
    int rmb = modrm & 7;
    *reg_field = reg;
    out->is_reg = 0;
    out->reg = rmb;
    out->addr = 0;
    out->bad = 0;
    if (mod == 3) {
        out->is_reg = 1;
        return;
    }
    uint32_t ea = 0;
    if (rmb == 4) {
        uint8_t sib = f8(f);
        int scale = 1 << ((sib >> 6) & 3);
        int index = (sib >> 3) & 7;
        int base = sib & 7;
        if (index != 4) ea += f->c->r[index] * (uint32_t)scale;
        if (mod == 0 && base == 5) {
            ea += f32(f);
        } else {
            ea += f->c->r[base];
        }
        if (mod == 1) ea += (uint32_t)(int32_t)(int8_t)f8(f);
        else if (mod == 2) ea += f32(f);
    } else if (mod == 0 && rmb == 5) {
        ea = f32(f);
    } else {
        ea = f->c->r[rmb];
        if (mod == 1) ea += (uint32_t)(int32_t)(int8_t)f8(f);
        else if (mod == 2) ea += f32(f);
    }
    out->addr = ea;
}

static uint32_t rm_read32(fetcher* f, const rm_t* m) {
    if (m->is_reg) return f->c->r[m->reg];
    int err = 0;
    uint32_t v = mem_read32(f->c, m->addr, &err);
    if (err) f->err = 1;
    return v;
}

static void rm_write32(fetcher* f, const rm_t* m, uint32_t v) {
    if (m->is_reg) { f->c->r[m->reg] = v; return; }
    int err = 0;
    mem_write32(f->c, m->addr, v, &err);
    if (err) f->err = 1;
}

static uint8_t rm_read8(fetcher* f, const rm_t* m) {
    if (m->is_reg) return get_r8(f->c, m->reg);
    int err = 0;
    uint8_t v = mem_read8(f->c, m->addr, &err);
    if (err) f->err = 1;
    return v;
}

static void rm_write8(fetcher* f, const rm_t* m, uint8_t v) {
    if (m->is_reg) { set_r8(f->c, m->reg, v); return; }
    int err = 0;
    mem_write8(f->c, m->addr, v, &err);
    if (err) f->err = 1;
}

static uint16_t rm_read16(fetcher* f, const rm_t* m) {
    if (m->is_reg) return (uint16_t)(f->c->r[m->reg] & 0xFFFF);
    int err = 0;
    uint16_t v = mem_read16(f->c, m->addr, &err);
    if (err) f->err = 1;
    return v;
}

static void push32(pr_cpu* c, uint32_t v, int* err) {
    c->r[PR_REG_ESP] -= 4;
    mem_write32(c, c->r[PR_REG_ESP], v, err);
}

static uint32_t pop32(pr_cpu* c, int* err) {
    uint32_t v = mem_read32(c, c->r[PR_REG_ESP], err);
    c->r[PR_REG_ESP] += 4;
    return v;
}

/* ---- execução de 1 instrução ---- */

static pr_status step_raw(pr_cpu* c);

pr_status pr_cpu_step(pr_cpu* c) {
    if (!c) return PR_ERR_INVALID;
    if (c->halted) return PR_ERR_STATE;
    return step_raw(c);
}

static pr_status step_raw(pr_cpu* c) {
    fetcher f;
    memset(&f, 0, sizeof(f));
    f.c = c;
    f.ip = c->eip;
    f.start_eip = c->eip;

    uint8_t op = f8(&f);
    if (f.err) return fault(c, f.start_eip, op, "busca fora da memória");

    /* Prefixos de segmento/rep ignorados (memória flat). */
    while (op == 0x26 || op == 0x2E || op == 0x36 || op == 0x3E ||
           op == 0x64 || op == 0x65 || op == 0xF2 || op == 0xF3) {
        op = f8(&f);
        if (f.err) return fault(c, f.start_eip, op, "busca fora da memória");
    }
    if (op == 0x66 || op == 0x67) {
        return fault(c, f.start_eip, op,
                     "operandos 16-bit (0x66/0x67) fora do subconjunto");
    }
    f.last_op = op;

    /* ALU geral: 0x00..0x3F (exceto escape 0x0F), formas r/m8,r8 | r/m32,r32 |
     * r8,r/m8 | r32,r/m32 | AL,imm8 | EAX,imm32 */
    if (op < 0x40 && op != 0x0F) {
        int which = (op >> 3) & 7;
        int form = op & 7;
        if (form == 4) { /* AL, imm8 */
            uint8_t imm = f8(&f);
            uint8_t res = (uint8_t)alu_exec(c, which, get_r8(c, 0), imm, 0);
            if (which != 7) set_r8(c, 0, res);
        } else if (form == 5) { /* EAX, imm32 */
            uint32_t imm = f32(&f);
            uint32_t res = alu_exec(c, which, c->r[0], imm, 1);
            if (which != 7) c->r[0] = res;
        } else if (form <= 3) {
            uint8_t modrm = f8(&f);
            rm_t m;
            int regf;
            decode_rm(&f, modrm, &m, &regf);
            if (f.err) return fault(c, f.start_eip, op, "ModR/M fora da memória");
            if (form == 0) { /* r/m8, r8 */
                uint8_t res = (uint8_t)alu_exec(c, which, rm_read8(&f, &m), get_r8(c, regf), 0);
                if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
                if (which != 7) rm_write8(&f, &m, res);
            } else if (form == 1) { /* r/m32, r32 */
                uint32_t res = alu_exec(c, which, rm_read32(&f, &m), c->r[regf], 1);
                if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
                if (which != 7) rm_write32(&f, &m, res);
            } else if (form == 2) { /* r8, r/m8 */
                uint8_t res = (uint8_t)alu_exec(c, which, get_r8(c, regf), rm_read8(&f, &m), 0);
                if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
                if (which != 7) set_r8(c, regf, res);
            } else { /* form == 3: r32, r/m32 */
                uint32_t res = alu_exec(c, which, c->r[regf], rm_read32(&f, &m), 1);
                if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
                if (which != 7) c->r[regf] = res;
            }
        } else {
            return fault(c, f.start_eip, op, "instrução BCD/AAA não suportada");
        }
        c->eip = f.ip;
        c->insns++;
        return PR_OK;
    }

    switch (op) {
        case 0x50: case 0x51: case 0x52: case 0x53:
        case 0x54: case 0x55: case 0x56: case 0x57: { /* PUSH r32 */
            int err = 0;
            push32(c, c->r[op - 0x50], &err);
            if (err) return fault(c, f.start_eip, op, "pilha fora da memória");
            break;
        }
        case 0x58: case 0x59: case 0x5A: case 0x5B:
        case 0x5C: case 0x5D: case 0x5E: case 0x5F: { /* POP r32 */
            int err = 0;
            uint32_t v = pop32(c, &err);
            if (err) return fault(c, f.start_eip, op, "pilha fora da memória");
            c->r[op - 0x58] = v;
            break;
        }
        case 0x40: case 0x41: case 0x42: case 0x43:
        case 0x44: case 0x45: case 0x46: case 0x47: { /* INC r32 */
            int r = op - 0x40;
            uint32_t old_cf = c->eflags & PR_EFLAGS_CF;
            uint32_t res = alu_exec(c, 0, c->r[r], 1, 1);
            c->r[r] = res;
            c->eflags = (c->eflags & ~PR_EFLAGS_CF) | old_cf;
            break;
        }
        case 0x48: case 0x49: case 0x4A: case 0x4B:
        case 0x4C: case 0x4D: case 0x4E: case 0x4F: { /* DEC r32 */
            int r = op - 0x48;
            uint32_t old_cf = c->eflags & PR_EFLAGS_CF;
            uint32_t res = alu_exec(c, 5, c->r[r], 1, 1);
            c->r[r] = res;
            c->eflags = (c->eflags & ~PR_EFLAGS_CF) | old_cf;
            break;
        }
        case 0x68: { /* PUSH imm32 */
            int err = 0;
            push32(c, f32(&f), &err);
            if (err) return fault(c, f.start_eip, op, "pilha fora da memória");
            break;
        }
        case 0x6A: { /* PUSH imm8 */
            int err = 0;
            int8_t v = (int8_t)f8(&f);
            push32(c, (uint32_t)(int32_t)v, &err);
            if (err) return fault(c, f.start_eip, op, "pilha fora da memória");
            break;
        }
        case 0x69: case 0x6B: { /* IMUL r32, r/m32, imm */
            uint8_t modrm = f8(&f);
            rm_t m;
            int regf;
            decode_rm(&f, modrm, &m, &regf);
            if (f.err) return fault(c, f.start_eip, op, "ModR/M fora da memória");
            int32_t src = (int32_t)rm_read32(&f, &m);
            int32_t imm = (op == 0x6B) ? (int32_t)(int8_t)f8(&f) : (int32_t)f32(&f);
            if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
            int64_t prod = (int64_t)src * imm;
            c->r[regf] = (uint32_t)(uint64_t)prod;
            int fits = (prod == (int64_t)(int32_t)prod);
            set_flag(c, PR_EFLAGS_CF, !fits);
            set_flag(c, PR_EFLAGS_OF, !fits);
            break;
        }
        case 0x84: case 0x85: { /* TEST r/m, r */
            uint8_t modrm = f8(&f);
            rm_t m;
            int regf;
            decode_rm(&f, modrm, &m, &regf);
            if (f.err) return fault(c, f.start_eip, op, "ModR/M fora da memória");
            if (op == 0x84) {
                (void)alu_exec(c, 4, rm_read8(&f, &m), get_r8(c, regf), 0);
            } else {
                (void)alu_exec(c, 4, rm_read32(&f, &m), c->r[regf], 1);
            }
            if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
            break;
        }
        case 0x88: case 0x89: case 0x8A: case 0x8B: { /* MOV */
            uint8_t modrm = f8(&f);
            rm_t m;
            int regf;
            decode_rm(&f, modrm, &m, &regf);
            if (f.err) return fault(c, f.start_eip, op, "ModR/M fora da memória");
            switch (op) {
                case 0x88: rm_write8(&f, &m, get_r8(c, regf)); break;
                case 0x89: rm_write32(&f, &m, c->r[regf]); break;
                case 0x8A: {
                    uint8_t v = rm_read8(&f, &m);
                    if (!f.err) set_r8(c, regf, v);
                    break;
                }
                case 0x8B: {
                    uint32_t v = rm_read32(&f, &m);
                    if (!f.err) c->r[regf] = v;
                    break;
                }
            }
            if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
            break;
        }
        case 0x8D: { /* LEA r32, m */
            uint8_t modrm = f8(&f);
            rm_t m;
            int regf;
            decode_rm(&f, modrm, &m, &regf);
            if (f.err || m.is_reg) return fault(c, f.start_eip, op, "LEA inválido");
            c->r[regf] = m.addr;
            break;
        }
        case 0x80: case 0x81: case 0x83: { /* grp1 r/m, imm */
            uint8_t modrm = f8(&f);
            rm_t m;
            int regf;
            decode_rm(&f, modrm, &m, &regf);
            if (f.err) return fault(c, f.start_eip, op, "ModR/M fora da memória");
            if (op == 0x80) {
                uint8_t imm = f8(&f);
                uint8_t a = rm_read8(&f, &m);
                uint8_t res = (uint8_t)alu_exec(c, regf, a, imm, 0);
                if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
                if (regf != 7) rm_write8(&f, &m, res);
            } else {
                uint32_t imm = (op == 0x83)
                    ? (uint32_t)(int32_t)(int8_t)f8(&f)
                    : f32(&f);
                uint32_t a = rm_read32(&f, &m);
                uint32_t res = alu_exec(c, regf, a, imm, 1);
                if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
                if (regf != 7) rm_write32(&f, &m, res);
            }
            if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
            break;
        }
        case 0x90: /* NOP */
            break;
        case 0x91: case 0x92: case 0x93: case 0x94:
        case 0x95: case 0x96: case 0x97: { /* XCHG EAX, r */
            int r = op - 0x90;
            uint32_t t = c->r[0];
            c->r[0] = c->r[r];
            c->r[r] = t;
            break;
        }
        case 0x98: /* CWDE */
            c->r[0] = (uint32_t)(int32_t)(int16_t)(c->r[0] & 0xFFFF);
            break;
        case 0x99: /* CDQ */
            c->r[PR_REG_EDX] = (c->r[0] & 0x80000000u) ? 0xFFFFFFFFu : 0;
            break;
        case 0x9C: { /* PUSHFD */
            int err = 0;
            push32(c, c->eflags | 0x202, &err);
            if (err) return fault(c, f.start_eip, op, "pilha fora da memória");
            break;
        }
        case 0x9D: { /* POPFD */
            int err = 0;
            uint32_t v = pop32(c, &err);
            if (err) return fault(c, f.start_eip, op, "pilha fora da memória");
            c->eflags = (v | 0x202) & ~0x1u;
            break;
        }
        case 0xA0: case 0xA1: case 0xA2: case 0xA3: { /* MOV AL/EAX <-> moffs */
            uint32_t off = f32(&f);
            int err = 0;
            if (op == 0xA0) { uint8_t v = mem_read8(c, off, &err); if (!err) set_r8(c, 0, v); }
            else if (op == 0xA1) { c->r[0] = mem_read32(c, off, &err); }
            else if (op == 0xA2) { mem_write8(c, off, get_r8(c, 0), &err); }
            else { mem_write32(c, off, c->r[0], &err); }
            if (err) return fault(c, f.start_eip, op, "acesso a memória inválido");
            break;
        }
        case 0xA8: { /* TEST AL, imm8 */
            uint8_t imm = f8(&f);
            (void)alu_exec(c, 4, get_r8(c, 0), imm, 0);
            break;
        }
        case 0xA9: { /* TEST EAX, imm32 */
            uint32_t imm = f32(&f);
            (void)alu_exec(c, 4, c->r[0], imm, 1);
            break;
        }
        case 0xB0: case 0xB1: case 0xB2: case 0xB3:
        case 0xB4: case 0xB5: case 0xB6: case 0xB7: { /* MOV r8, imm8 */
            uint8_t imm = f8(&f);
            set_r8(c, op - 0xB0, imm);
            break;
        }
        case 0xB8: case 0xB9: case 0xBA: case 0xBB:
        case 0xBC: case 0xBD: case 0xBE: case 0xBF: { /* MOV r32, imm32 */
            uint32_t imm = f32(&f);
            c->r[op - 0xB8] = imm;
            break;
        }
        case 0xC2: { /* RET imm16 */
            uint16_t n = (uint16_t)f8(&f);
            n |= (uint16_t)(f8(&f) << 8);
            int err = 0;
            uint32_t ret = pop32(c, &err);
            if (err) return fault(c, f.start_eip, op, "pilha fora da memória");
            c->r[PR_REG_ESP] += n;
            c->eip = ret;
            c->insns++;
            return PR_OK;
        }
        case 0xC3: { /* RET */
            int err = 0;
            uint32_t ret = pop32(c, &err);
            if (err) return fault(c, f.start_eip, op, "pilha fora da memória");
            c->eip = ret;
            c->insns++;
            return PR_OK;
        }
        case 0xC6: case 0xC7: { /* MOV r/m, imm */
            uint8_t modrm = f8(&f);
            rm_t m;
            int regf;
            decode_rm(&f, modrm, &m, &regf);
            if (f.err || regf != 0) return fault(c, f.start_eip, op, "MOV imm inválido");
            if (op == 0xC6) {
                uint8_t imm = f8(&f);
                rm_write8(&f, &m, imm);
            } else {
                uint32_t imm = f32(&f);
                rm_write32(&f, &m, imm);
            }
            if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
            break;
        }
        case 0xC9: { /* LEAVE */
            c->r[PR_REG_ESP] = c->r[PR_REG_EBP];
            int err = 0;
            c->r[PR_REG_EBP] = pop32(c, &err);
            if (err) return fault(c, f.start_eip, op, "pilha fora da memória");
            break;
        }
        case 0xCC: case 0xCD: { /* INT3 / INT imm8 */
            uint8_t vec = (op == 0xCC) ? 3 : f8(&f);
            if (!c->trap) return fault(c, f.start_eip, op, "INT sem tratador");
            c->eip = f.ip;
            c->insns++;
            int r = c->trap(c->trap_ud, c, vec);
            if (r != 0) c->trap_stop = 1;
            return PR_OK;
        }
        case 0xD0: case 0xD1: case 0xD2: case 0xD3: { /* shifts grp2 */
            uint8_t modrm = f8(&f);
            rm_t m;
            int regf;
            decode_rm(&f, modrm, &m, &regf);
            if (f.err) return fault(c, f.start_eip, op, "ModR/M fora da memória");
            int w32 = (op == 0xD1 || op == 0xD3);
            uint32_t count = (op == 0xD0 || op == 0xD1) ? 1u : (uint32_t)(c->r[PR_REG_ECX] & 0xFF);
            if (w32) {
                uint32_t v = rm_read32(&f, &m);
                uint32_t old = v;
                uint32_t res = v;
                count &= 0x1F;
                for (uint32_t i = 0; i < count; i++) {
                    switch (regf) {
                        case 0: /* ROL */
                            res = (res << 1) | (res >> 31);
                            set_flag(c, PR_EFLAGS_CF, res & 1);
                            break;
                        case 1: /* ROR */
                            res = (res >> 1) | ((res & 1) << 31);
                            set_flag(c, PR_EFLAGS_CF, (res >> 31) & 1);
                            break;
                        case 4: /* SHL */
                            set_flag(c, PR_EFLAGS_CF, (res >> 31) & 1);
                            res <<= 1;
                            break;
                        case 5: /* SHR */
                            set_flag(c, PR_EFLAGS_CF, res & 1);
                            res >>= 1;
                            break;
                        case 7: /* SAR */
                            set_flag(c, PR_EFLAGS_CF, res & 1);
                            res = (uint32_t)((int32_t)res >> 1);
                            break;
                        default:
                            return fault(c, f.start_eip, op, "shift não suportado (RCL/RCR)");
                    }
                }
                if (count == 1) {
                    set_flag(c, PR_EFLAGS_OF,
                             regf == 4 ? (((res >> 31) ^ (c->eflags & PR_EFLAGS_CF ? 1 : 0)) & 1)
                                       : 0);
                }
                if (count) set_zsp(c, res, 1);
                if (!f.err) rm_write32(&f, &m, res);
                (void)old;
            } else {
                uint8_t v = rm_read8(&f, &m);
                uint8_t res = v;
                count &= 0x1F;
                for (uint32_t i = 0; i < count; i++) {
                    switch (regf) {
                        case 0:
                            res = (uint8_t)((res << 1) | (res >> 7));
                            set_flag(c, PR_EFLAGS_CF, res & 1);
                            break;
                        case 1:
                            res = (uint8_t)((res >> 1) | ((res & 1) << 7));
                            set_flag(c, PR_EFLAGS_CF, (res >> 7) & 1);
                            break;
                        case 4:
                            set_flag(c, PR_EFLAGS_CF, (res >> 7) & 1);
                            res = (uint8_t)(res << 1);
                            break;
                        case 5:
                            set_flag(c, PR_EFLAGS_CF, res & 1);
                            res = (uint8_t)(res >> 1);
                            break;
                        case 7:
                            set_flag(c, PR_EFLAGS_CF, res & 1);
                            res = (uint8_t)((int8_t)res >> 1);
                            break;
                        default:
                            return fault(c, f.start_eip, op, "shift não suportado (RCL/RCR)");
                    }
                }
                if (count) set_zsp(c, res, 0);
                if (!f.err) rm_write8(&f, &m, res);
            }
            if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
            break;
        }
        case 0xE0: case 0xE1: case 0xE2: { /* LOOPNE/LOOPE/LOOP rel8 */
            int8_t rel = (int8_t)f8(&f);
            c->r[PR_REG_ECX]--;
            int zf = (c->eflags & PR_EFLAGS_ZF) != 0;
            int take = 0;
            if (op == 0xE2) take = (c->r[PR_REG_ECX] != 0);
            else if (op == 0xE1) take = (c->r[PR_REG_ECX] != 0 && zf);
            else take = (c->r[PR_REG_ECX] != 0 && !zf);
            if (take) f.ip = (uint32_t)(f.ip + (int32_t)rel);
            break;
        }
        case 0xE3: { /* JECXZ rel8 */
            int8_t rel = (int8_t)f8(&f);
            if (c->r[PR_REG_ECX] == 0) f.ip = (uint32_t)(f.ip + (int32_t)rel);
            break;
        }
        case 0x70: case 0x71: case 0x72: case 0x73:
        case 0x74: case 0x75: case 0x76: case 0x77:
        case 0x78: case 0x79: case 0x7A: case 0x7B:
        case 0x7C: case 0x7D: case 0x7E: case 0x7F: { /* Jcc rel8 */
            int8_t rel = (int8_t)f8(&f);
            if (cond_holds(c, op & 0xF)) f.ip = (uint32_t)(f.ip + (int32_t)rel);
            break;
        }
        case 0xE8: { /* CALL rel32 */
            int32_t rel = (int32_t)f32(&f);
            int err = 0;
            push32(c, f.ip, &err);
            if (err) return fault(c, f.start_eip, op, "pilha fora da memória");
            f.ip = (uint32_t)(f.ip + rel);
            break;
        }
        case 0xE9: { /* JMP rel32 */
            int32_t rel = (int32_t)f32(&f);
            f.ip = (uint32_t)(f.ip + rel);
            break;
        }
        case 0xEB: { /* JMP rel8 */
            int8_t rel = (int8_t)f8(&f);
            f.ip = (uint32_t)(f.ip + (int32_t)rel);
            break;
        }
        case 0xF4: /* HLT */
            c->halted = 1;
            c->eip = f.ip;
            c->insns++;
            return PR_OK;
        case 0xF5: /* CMC */
            c->eflags ^= PR_EFLAGS_CF;
            break;
        case 0xF6: case 0xF7: { /* grp3 TEST/NOT/NEG/MUL/IMUL/DIV/IDIV */
            uint8_t modrm = f8(&f);
            rm_t m;
            int regf;
            decode_rm(&f, modrm, &m, &regf);
            if (f.err) return fault(c, f.start_eip, op, "ModR/M fora da memória");
            int w32 = (op == 0xF7);
            if (!w32) {
                if (regf == 0) { /* TEST r/m8, imm8 */
                    uint8_t imm = f8(&f);
                    (void)alu_exec(c, 4, rm_read8(&f, &m), imm, 0);
                } else if (regf == 2) { /* NOT r/m8 */
                    uint8_t v = (uint8_t)~rm_read8(&f, &m);
                    rm_write8(&f, &m, v);
                } else if (regf == 3) { /* NEG r/m8 */
                    uint8_t a = rm_read8(&f, &m);
                    uint8_t res = (uint8_t)alu_exec(c, 5, 0, a, 0);
                    rm_write8(&f, &m, res);
                } else {
                    return fault(c, f.start_eip, op, "MUL/DIV 8-bit fora do subconjunto");
                }
            } else {
                switch (regf) {
                    case 0: { /* TEST r/m32, imm32 */
                        uint32_t imm = f32(&f);
                        (void)alu_exec(c, 4, rm_read32(&f, &m), imm, 1);
                        break;
                    }
                    case 2: { /* NOT r/m32 */
                        uint32_t v = ~rm_read32(&f, &m);
                        rm_write32(&f, &m, v);
                        break;
                    }
                    case 3: { /* NEG r/m32 */
                        uint32_t a = rm_read32(&f, &m);
                        uint32_t res = alu_exec(c, 5, 0, a, 1);
                        rm_write32(&f, &m, res);
                        break;
                    }
                    case 4: { /* MUL r/m32 */
                        uint64_t prod = (uint64_t)c->r[0] * rm_read32(&f, &m);
                        c->r[0] = (uint32_t)prod;
                        c->r[PR_REG_EDX] = (uint32_t)(prod >> 32);
                        set_flag(c, PR_EFLAGS_CF, c->r[PR_REG_EDX] != 0);
                        set_flag(c, PR_EFLAGS_OF, c->r[PR_REG_EDX] != 0);
                        break;
                    }
                    case 5: { /* IMUL r/m32 */
                        int64_t prod = (int64_t)(int32_t)c->r[0] * (int32_t)rm_read32(&f, &m);
                        c->r[0] = (uint32_t)prod;
                        c->r[PR_REG_EDX] = (uint32_t)((uint64_t)prod >> 32);
                        int fits = (prod == (int64_t)(int32_t)prod);
                        set_flag(c, PR_EFLAGS_CF, !fits);
                        set_flag(c, PR_EFLAGS_OF, !fits);
                        break;
                    }
                    case 6: { /* DIV r/m32 */
                        uint32_t d = rm_read32(&f, &m);
                        if (d == 0) return fault(c, f.start_eip, op, "divisão por zero");
                        uint64_t num = ((uint64_t)c->r[PR_REG_EDX] << 32) | c->r[0];
                        uint64_t q = num / d;
                        if (q > 0xFFFFFFFFu) return fault(c, f.start_eip, op, "overflow de DIV");
                        c->r[0] = (uint32_t)q;
                        c->r[PR_REG_EDX] = (uint32_t)(num % d);
                        break;
                    }
                    case 7: { /* IDIV r/m32 */
                        int32_t d = (int32_t)rm_read32(&f, &m);
                        if (d == 0) return fault(c, f.start_eip, op, "divisão por zero");
                        int64_t num = (int64_t)(((uint64_t)c->r[PR_REG_EDX] << 32) | c->r[0]);
                        int64_t q = num / d;
                        if (q > 2147483647LL || q < -2147483648LL) {
                            return fault(c, f.start_eip, op, "overflow de IDIV");
                        }
                        c->r[0] = (uint32_t)(int32_t)q;
                        c->r[PR_REG_EDX] = (uint32_t)(int32_t)(num % d);
                        break;
                    }
                }
            }
            if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
            break;
        }
        case 0xF8: c->eflags &= ~PR_EFLAGS_CF; break; /* CLC */
        case 0xF9: c->eflags |= PR_EFLAGS_CF; break;  /* STC */
        case 0xFA: case 0xFB: break;                  /* CLI/STI: sem interrupções */
        case 0xFC: case 0xFD: break;                  /* CLD/STD: sem ops de string */
        case 0xFE: { /* grp4 INC/DEC r/m8 */
            uint8_t modrm = f8(&f);
            rm_t m;
            int regf;
            decode_rm(&f, modrm, &m, &regf);
            if (f.err || regf > 1) return fault(c, f.start_eip, op, "grp4 inválido");
            uint32_t old_cf = c->eflags & PR_EFLAGS_CF;
            uint8_t a = rm_read8(&f, &m);
            uint8_t res = (uint8_t)alu_exec(c, regf == 0 ? 0 : 5, a, 1, 0);
            rm_write8(&f, &m, res);
            c->eflags = (c->eflags & ~PR_EFLAGS_CF) | old_cf;
            if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
            break;
        }
        case 0xFF: { /* grp5 INC/DEC/CALL/JMP/PUSH r/m32 */
            uint8_t modrm = f8(&f);
            rm_t m;
            int regf;
            decode_rm(&f, modrm, &m, &regf);
            if (f.err) return fault(c, f.start_eip, op, "ModR/M fora da memória");
            switch (regf) {
                case 0: case 1: {
                    uint32_t old_cf = c->eflags & PR_EFLAGS_CF;
                    uint32_t a = rm_read32(&f, &m);
                    uint32_t res = alu_exec(c, regf == 0 ? 0 : 5, a, 1, 1);
                    rm_write32(&f, &m, res);
                    c->eflags = (c->eflags & ~PR_EFLAGS_CF) | old_cf;
                    break;
                }
                case 2: { /* CALL r/m32 */
                    uint32_t t = rm_read32(&f, &m);
                    int err = 0;
                    push32(c, f.ip, &err);
                    if (err) return fault(c, f.start_eip, op, "pilha fora da memória");
                    f.ip = t;
                    break;
                }
                case 4: { /* JMP r/m32 */
                    uint32_t t = rm_read32(&f, &m);
                    f.ip = t;
                    break;
                }
                case 6: { /* PUSH r/m32 */
                    uint32_t v = rm_read32(&f, &m);
                    int err = 0;
                    push32(c, v, &err);
                    if (err) return fault(c, f.start_eip, op, "pilha fora da memória");
                    break;
                }
                default:
                    return fault(c, f.start_eip, op, "CALL far/inválido fora do subconjunto");
            }
            if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
            break;
        }
        case 0x0F: { /* mapas estendidos */
            uint8_t op2 = f8(&f);
            if (f.err) return fault(c, f.start_eip, op, "busca fora da memória");
            if (op2 >= 0x80 && op2 <= 0x8F) { /* Jcc rel32 */
                int32_t rel = (int32_t)f32(&f);
                if (cond_holds(c, op2 & 0xF)) f.ip = (uint32_t)(f.ip + rel);
                break;
            }
            if (op2 >= 0x90 && op2 <= 0x9F) { /* SETcc r/m8 */
                uint8_t modrm = f8(&f);
                rm_t m;
                int regf;
                decode_rm(&f, modrm, &m, &regf);
                (void)regf;
                if (f.err) return fault(c, f.start_eip, op, "ModR/M fora da memória");
                rm_write8(&f, &m, cond_holds(c, op2 & 0xF) ? 1 : 0);
                if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
                break;
            }
            switch (op2) {
                case 0x1F: { /* NOP r/m32 multibyte */
                    uint8_t modrm = f8(&f);
                    rm_t m;
                    int regf;
                    decode_rm(&f, modrm, &m, &regf);
                    (void)regf;
                    if (f.err) return fault(c, f.start_eip, op, "ModR/M fora da memória");
                    break;
                }
                case 0xA2: { /* CPUID básico */
                    uint32_t leaf = c->r[0];
                    if (leaf == 0) {
                        c->r[0] = 1;
                        c->r[3] = 0x74726F50u; /* "Port" little-endian parcial */
                        c->r[1] = 0x436F6369u; /* "iCoc" */
                        c->r[2] = 0x20555043u; /* "CPU " */
                    } else {
                        c->r[0] = 0x00000600u;
                        c->r[1] = 0;
                        c->r[2] = 0;
                        c->r[3] = 0;
                    }
                    break;
                }
                case 0xAF: { /* IMUL r32, r/m32 */
                    uint8_t modrm = f8(&f);
                    rm_t m;
                    int regf;
                    decode_rm(&f, modrm, &m, &regf);
                    if (f.err) return fault(c, f.start_eip, op, "ModR/M fora da memória");
                    int64_t prod = (int64_t)(int32_t)c->r[regf] * (int32_t)rm_read32(&f, &m);
                    c->r[regf] = (uint32_t)prod;
                    int fits = (prod == (int64_t)(int32_t)prod);
                    set_flag(c, PR_EFLAGS_CF, !fits);
                    set_flag(c, PR_EFLAGS_OF, !fits);
                    if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
                    break;
                }
                case 0xB6: case 0xB7: case 0xBE: case 0xBF: { /* MOVZX/MOVSX */
                    uint8_t modrm = f8(&f);
                    rm_t m;
                    int regf;
                    decode_rm(&f, modrm, &m, &regf);
                    if (f.err) return fault(c, f.start_eip, op, "ModR/M fora da memória");
                    if (op2 == 0xB6) {
                        uint32_t v = rm_read8(&f, &m);
                        c->r[regf] = v;
                    } else if (op2 == 0xB7) {
                        uint32_t v = rm_read16(&f, &m);
                        c->r[regf] = v;
                    } else if (op2 == 0xBE) {
                        c->r[regf] = (uint32_t)(int32_t)(int8_t)rm_read8(&f, &m);
                    } else {
                        c->r[regf] = (uint32_t)(int32_t)(int16_t)rm_read16(&f, &m);
                    }
                    if (f.err) return fault(c, f.start_eip, op, "acesso a memória inválido");
                    break;
                }
                case 0xC8: case 0xC9: case 0xCA: case 0xCB:
                case 0xCC: case 0xCD: case 0xCE: case 0xCF: { /* BSWAP r32 */
                    uint32_t v = c->r[op2 - 0xC8];
                    v = ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
                        ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
                    c->r[op2 - 0xC8] = v;
                    break;
                }
                default: {
                    char why[48];
                    snprintf(why, sizeof(why), "opcode 0F %02X fora do subconjunto", op2);
                    return fault(c, f.start_eip, op2, why);
                }
            }
            break;
        }
        default: {
            char why[48];
            snprintf(why, sizeof(why), "opcode %02X fora do subconjunto", op);
            return fault(c, f.start_eip, op, why);
        }
    }

    c->eip = f.ip;
    c->insns++;
    return PR_OK;
}

pr_status pr_cpu_run(pr_cpu* c, uint64_t max_instructions, uint64_t* executed) {
    if (!c) return PR_ERR_INVALID;
    uint64_t n = 0;
    pr_status st = PR_OK;
    c->trap_stop = 0;
    c->break_run = 0;
    while (n < max_instructions) {
        if (c->halted || c->trap_stop || c->break_run) break;
        st = pr_cpu_step(c);
        if (st != PR_OK) break;
        n++;
    }
    if (executed) *executed = n;
    return st;
}

void pr_cpu_break(pr_cpu* c) {
    if (c) c->break_run = 1;
}
