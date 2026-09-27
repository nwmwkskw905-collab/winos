/* pr_cpu64.h — interpretador x86-64 (LONG MODE) progressivo.
 *
 * Subconjunto implementado (Fase 1 expandida): prologos/epilogos comuns
 * (push/pop, mov rbp/rsp, sub/add rsp, leave, call/ret), movs 8/16/32/64-bit
 * (reg/mem/immediatos, movzx/movsx/movsxd), ALU completa (add/or/adc/sbb/
 * and/sub/xor/cmp/test/not/neg/inc/dec/mul/imul/div/idiv) com RFLAGS reais
 * (CF/PF/ZF/SF/OF), shifts/rotates (shl/shr/sar/rol/ror/rcl/rcr por 1/imm/CL),
 * LEA, XCHG, JMP/Jcc/SETcc (todos os códigos de condição), call/jmp/push/pop
 * indiretos, endbr64/multi-byte NOP, CBW/CWDE/CDQE, CWD/CDQ/CQO, INT, HLT,
 * flag-ops (CLC/STC/CMC/CLD/STD), endereçamento [base+index*scale+disp] e
 * RIP-relative. Prefixo 66 = 16-bit. Prefixos CS/DS/ES/SS são no-op (modelo
 * flat); FS/GS (TEB) são recusados com diagnóstico.
 *
 * NÃO implementados (recusa honesta com opcode/RIP/bytes/endereço): FPU/x87,
 * SSE/AVX, string ops (movs/stos/rep), 16-bit real-mode, far call/jmp/ret,
 * DAA/DAS/AAA, TEB/PEB (FS/GS), interrupções além de INT genérico → trap.
 *
 * Flags (RFLAGS) NÃO são exportadas para pushf/popf ainda; JCXZ não existe.
 * Instrução fora do subconjunto → fault com opcode, RIP, bytes da instrução,
 * endereço de memória envolvido e motivo — NUNCA se finge suporte.
 *
 * Espaço de memória emprestado (pr_vm do processo); acessos passam por guard
 * de páginas (PROT_R/W/X) como em pr_cpu.
 */
#ifndef PR_CPU64_H
#define PR_CPU64_H

#include <stdint.h>
#include <stddef.h>
#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pr_cpu64 pr_cpu64;

/* Índices de registrador (numeração de hardware):
 * 0 RAX, 1 RCX, 2 RDX, 3 RBX, 4 RSP, 5 RBP, 6 RSI, 7 RDI, 8..15 R8..R15. */
#define PR_R64_RAX 0
#define PR_R64_RCX 1
#define PR_R64_RDX 2
#define PR_R64_RBX 3
#define PR_R64_RSP 4
#define PR_R64_RBP 5
#define PR_R64_RSI 6
#define PR_R64_RDI 7

/* Tipos de acesso do guard (mesmos valores de pr_cpu: R=1, W=2, X=4). */
#define PR_CPU64_ACC_R 1
#define PR_CPU64_ACC_W 2
#define PR_CPU64_ACC_X 4

/* Bits de RFLAGS suportados (máscara visível via pr_cpu64_rflags). */
#define PR_R64_CF 0x001ull
#define PR_R64_PF 0x004ull
#define PR_R64_ZF 0x040ull
#define PR_R64_SF 0x080ull
#define PR_R64_OF 0x800ull

/* Diagnóstico de parada: opcode, RIP, bytes da instrução e endereço. */
typedef struct pr_cpu64_fault {
    uint64_t rip;       /* RIP da instrução que falhou */
    uint8_t  opcode;    /* primeiro byte do opcode */
    uint8_t  rex;       /* prefixo REX (0 se ausente) */
    uint8_t  bytes[8];  /* bytes da instrução (até 8, a partir do RIP) */
    uint8_t  nbytes;    /* quantos bytes capturados */
    uint32_t addr;      /* endereço de memória do acesso (fault de acesso) */
    char     reason[80];
} pr_cpu64_fault;

/* Trap de software (INT n): retorna 0 p/ continuar após o handler,
 * <0 para fault (parada com erro). A parada LIMPA do run é via HLT após o
 * handler (pr_cpu64_run retorna PR_OK em halted) — >0 é tratado como 0. */
typedef int (*pr_cpu64_trap_fn)(void* ud, pr_cpu64* c, uint8_t vector);
/* Guard de memória (mesma convenção de pr_cpu): !=0 = acesso permitido,
 * 0 = negado (o acesso vira fault com endereço/reason). */
typedef int (*pr_cpu64_guard_fn)(void* ud, uint64_t addr, size_t len, int acc);

/* Cria uma CPU sobre um buffer emprestado (não é liberado no destroy). */
pr_cpu64* pr_cpu64_create_on(uint8_t* mem, uint32_t mem_size);
void      pr_cpu64_destroy(pr_cpu64* c);

void     pr_cpu64_set_rip(pr_cpu64* c, uint64_t rip);
uint64_t pr_cpu64_rip(const pr_cpu64* c);
uint64_t pr_cpu64_reg(const pr_cpu64* c, int idx);
void     pr_cpu64_set_reg(pr_cpu64* c, int idx, uint64_t v);
/* XMM0..15 (128 bits) — GRUPO 8: args float/double (ABI x64) nos traps */
void     pr_cpu64_xmm(const pr_cpu64* c, int idx, uint8_t out[16]);
void     pr_cpu64_set_xmm(pr_cpu64* c, int idx, const void* v);
uint64_t pr_cpu64_rflags(const pr_cpu64* c);
void     pr_cpu64_set_rflags(pr_cpu64* c, uint64_t v);
/* FS/GS: base flat usada pelo convidado para TEB/PEB (x64 Windows) */
void     pr_cpu64_set_fs_base(pr_cpu64* c, uint64_t base);
void     pr_cpu64_set_gs_base(pr_cpu64* c, uint64_t base);

void pr_cpu64_set_guard(pr_cpu64* c, pr_cpu64_guard_fn fn, void* ud);
void pr_cpu64_set_trap(pr_cpu64* c, pr_cpu64_trap_fn fn, void* ud);

/* Executa até budget de instruções, HALT, trap que pede parada ou fault.
 * executed recebe o nº real de instruções. PR_OK (halt/budget) ou
 * PR_ERR_FAULT (pr_cpu64_last_fault com opcode/RIP/bytes/endereço). */
pr_status pr_cpu64_run(pr_cpu64* c, uint64_t budget, uint64_t* executed);

int pr_cpu64_halted(const pr_cpu64* c);
const pr_cpu64_fault* pr_cpu64_last_fault(const pr_cpu64* c);

#ifdef __cplusplus
}
#endif
#endif /* PR_CPU64_H */
