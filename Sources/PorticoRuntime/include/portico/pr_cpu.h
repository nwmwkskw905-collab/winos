/* Portico Runtime — backend de execução de CPU.
 *
 * HONESTIDADE DE CAPACIDADES:
 *  - Este módulo é um INTERPRETADOR real de um subconjunto do conjunto de
 *    instruções x86 (IA-32, modo flat 32 bits). Ele executa código de verdade,
 *    instrução a instrução, sem JIT.
 *  - NÃO executa software Windows comercial: falta a camada Win32 (Win32/NT),
 *    que precisa de um backend externo (ex.: classe Wine) — ver docs/BACKEND_INTEGRATION.md.
 *  - JIT (geração de código em tempo de execução) NÃO é utilizado: o iOS proíbe
 *    a execução de código gerado por apps de terceiros (codesigning/W^X).
 *    pr_cap.h sonda isso em tempo real.
 *
 * Subconjunto implementado (IA-32 flat): MOV, XCHG, LEA, ALU (ADD/OR/ADC/SBB/
 * AND/SUB/XOR/CMP, TEST), INC/DEC, NOT/NEG, MUL/IMUL/DIV/IDIV, shifts (SHL/SHR/
 * SAR/ROL/ROR), PUSH/POP, PUSHFD/POPFD, CALL/RET/JMP/Jcc/SETcc/LOOP/JECXZ,
 * MOVZX/MOVSX, CWDE/CDQ, INT/INT3/HLT, NOP, CPUID (básico), NOP multibyte (0F1F).
 * Prefixos de segmento são aceitos e ignorados (memória flat). Operandos 16-bit
 * (prefixo 0x66) e endereçamento 16-bit (0x67) reportam PR_ERR_UNSUPPORTED. */
#ifndef PORTICO_PR_CPU_H
#define PORTICO_PR_CPU_H

#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pr_cpu pr_cpu;

enum {
    PR_REG_EAX = 0, PR_REG_ECX = 1, PR_REG_EDX = 2, PR_REG_EBX = 3,
    PR_REG_ESP = 4, PR_REG_EBP = 5, PR_REG_ESI = 6, PR_REG_EDI = 7
};

enum {
    PR_EFLAGS_CF = 1u << 0,
    PR_EFLAGS_PF = 1u << 2,
    PR_EFLAGS_ZF = 1u << 6,
    PR_EFLAGS_SF = 1u << 7,
    PR_EFLAGS_OF = 1u << 11
};

/* MMIO: janela de registradores mapeada em memória (I/O de plataforma). */
typedef uint32_t (*pr_cpu_mmio_read_fn)(void* ud, uint32_t off);
typedef void     (*pr_cpu_mmio_write_fn)(void* ud, uint32_t off, uint32_t value);

/* Trap de software (instrução INT n). Retorna 0 para continuar; !=0 interrompe run. */
typedef int (*pr_cpu_trap_fn)(void* ud, pr_cpu* cpu, uint8_t vector);

/* Guard de acesso à memória (permissões de região / W^X). Retorna 1 = permitido.
 * Sem guard, qualquer endereço dentro do tamanho da memória flat é aceito. */
enum { PR_CPU_ACC_R = 1, PR_CPU_ACC_W = 2, PR_CPU_ACC_X = 4 };
typedef int (*pr_cpu_guard_fn)(void* ud, uint32_t addr, size_t len, int acc);

pr_cpu* pr_cpu_create(size_t mem_size); /* memória flat; mínimo 1 MiB */
void    pr_cpu_destroy(pr_cpu* cpu);
void    pr_cpu_reset(pr_cpu* cpu);

uint32_t pr_cpu_reg(const pr_cpu* cpu, int idx);
void     pr_cpu_set_reg(pr_cpu* cpu, int idx, uint32_t v);
uint32_t pr_cpu_eip(const pr_cpu* cpu);
void     pr_cpu_set_eip(pr_cpu* cpu, uint32_t v);
uint32_t pr_cpu_eflags(const pr_cpu* cpu);
void     pr_cpu_set_eflags(pr_cpu* cpu, uint32_t v);
int      pr_cpu_halted(const pr_cpu* cpu);
uint64_t pr_cpu_insns(const pr_cpu* cpu);

/* Memória: carga/leitura direta (fora do convidado). */
pr_status pr_cpu_load(pr_cpu* cpu, uint32_t addr, const void* data, size_t len);
pr_status pr_cpu_read_mem(const pr_cpu* cpu, uint32_t addr, void* out, size_t len);
uint32_t  pr_cpu_mem_size(const pr_cpu* cpu);
/* Ponteiro para a memória física do convidado (backing do pr_vm). */
uint8_t*  pr_cpu_mem(pr_cpu* cpu);

/* Configura janela MMIO (até 2). off = endereço - base. */
pr_status pr_cpu_set_mmio(pr_cpu* cpu, int slot, uint32_t base, uint32_t size,
                          pr_cpu_mmio_read_fn rd, pr_cpu_mmio_write_fn wr, void* ud);
void      pr_cpu_set_trap(pr_cpu* cpu, pr_cpu_trap_fn fn, void* ud);
void      pr_cpu_set_guard(pr_cpu* cpu, pr_cpu_guard_fn fn, void* ud);

/* Executa 1 instrução. */
pr_status pr_cpu_step(pr_cpu* cpu);

/* Solicita que o laço de pr_cpu_run retorne ao fim da instrução atual
 * (usado pelo host para encerrar o frame no PRESENT/HALT do convidado). */
void pr_cpu_break(pr_cpu* cpu);

/* Executa até max_instructions, HLT, trap interrompendo ou falta.
 * *executed recebe o nº real de instruções. PR_ERR_FAULT com pr_cpu_fault_info. */
typedef struct pr_cpu_fault {
    uint32_t eip;
    uint8_t  opcode;
    char     reason[64];
} pr_cpu_fault;
pr_status pr_cpu_run(pr_cpu* cpu, uint64_t max_instructions, uint64_t* executed);
const pr_cpu_fault* pr_cpu_last_fault(const pr_cpu* cpu);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_CPU_H */
