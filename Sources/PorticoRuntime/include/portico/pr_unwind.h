/* Portico Runtime — SEH/unwind x64 mínimo: .pdata (RUNTIME_FUNCTION) e
 * .xdata (UNWIND_INFO). Carrega e interpreta o subconjunto exigido pelos PEs
 * reais: índice de funções, lookup por RVA e decodificação dos unwind codes.
 * NÃO há dispatch de exceção aqui (__C_specific_handler continua honesto). */
#ifndef PORTICO_PR_UNWIND_H
#define PORTICO_PR_UNWIND_H

#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PR_UNWIND_MAX_CODES 40

/* Uma entrada RUNTIME_FUNCTION da .pdata (12 bytes no arquivo). */
typedef struct pr_unwind_entry {
    uint32_t begin_rva;
    uint32_t end_rva;
    uint32_t unwind_rva;
} pr_unwind_entry;

/* Um unwind code decodificado (UWOP_*). */
typedef struct pr_unwind_code {
    uint8_t prolog_off;   /* offset dentro do prolog */
    uint8_t op;           /* UWOP 0..8 */
    uint8_t info;         /* OpInfo */
    uint16_t extra[2];    /* slots extras (offset/tamanho) */
    uint8_t extras;       /* quantos slots extras este code consumiu */
} pr_unwind_code;

/* UNWIND_INFO decodificado (.xdata). */
typedef struct pr_unwind_info {
    uint8_t version;              /* 1 ou 2 */
    uint8_t flags;                /* UNW_FLAG_EHANDLER/UHANDLER/CHAININFO */
    uint8_t prolog_size;
    uint8_t frame_reg;            /* 0 = sem frame pointer */
    uint32_t frame_offset;        /* offset do frame reg (OpInfo * 16) */
    pr_unwind_code codes[PR_UNWIND_MAX_CODES];
    size_t ncodes;
    uint32_t handler_rva;         /* flags EHANDLER/UHANDLER */
    pr_unwind_entry chained;      /* flags CHAININFO */
    int has_handler;
    int is_chained;
} pr_unwind_info;

/* UWOP (x64) */
#define PR_UWOP_PUSH_NONVOL      0
#define PR_UWOP_ALLOC_LARGE      1
#define PR_UWOP_ALLOC_SMALL      2
#define PR_UWOP_SET_FPREG        3
#define PR_UWOP_SAVE_NONVOL      4
#define PR_UWOP_SAVE_NONVOL_FAR  5
#define PR_UWOP_SAVE_XMM128      6
#define PR_UWOP_SAVE_XMM128_FAR  7
#define PR_UWOP_PUSH_MACHFRAME   8

#define PR_UNW_FLAG_EHANDLER  0x01
#define PR_UNW_FLAG_UHANDLER  0x02
#define PR_UNW_FLAG_CHAININFO 0x04

/* Índice da tabela RUNTIME_FUNCTION na imagem MAPPADA (RVA == offset).
 * Para na entrada zerada (terminador); entrada inválida = PR_ERR_FORMAT. */
pr_status pr_unwind_index(const uint8_t* image, size_t image_size,
                          uint32_t exc_rva, uint32_t exc_size,
                          pr_unwind_entry* out, size_t cap, size_t* count);

/* Interpreta o UNWIND_INFO em unwind_rva. Versão fora de 1/2 =
 * PR_ERR_UNSUPPORTED (honesto). */
pr_status pr_unwind_parse(const uint8_t* image, size_t image_size,
                          uint32_t unwind_rva, pr_unwind_info* out);

/* Busca a função que contém rva; -1 se nenhuma. */
int pr_unwind_lookup(const pr_unwind_entry* tab, size_t n, uint32_t rva);

/* "Desfaz" o prolog: tamanho do frame (pushes + allocs) e máscara dos
 * registradores callee-saved empurrados/salvos (bit r = R8..R15 etc.).
 * Opcode inesperado = PR_ERR_UNSUPPORTED. */
pr_status pr_unwind_compute(const pr_unwind_info* ui,
                            uint64_t* frame_bytes, uint32_t* saved_mask);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_UNWIND_H */
