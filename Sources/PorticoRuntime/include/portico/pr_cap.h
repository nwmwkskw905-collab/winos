/* Portico Runtime — sondagem de capacidades do ambiente de execução.
 * Detecta em tempo real o que a plataforma permite; nunca simula capacidades. */
#ifndef PORTICO_PR_CAP_H
#define PORTICO_PR_CAP_H

#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pr_cap_info {
    int  jit_available;       /* 1 somente se gerar+executar código for permitido */
    int  exec_mem_mappable;   /* mmap PROT_EXEC anônimo aceito pelo kernel */
    int  write_xor_execute;   /* W^X imposto pela plataforma */
    int  is_ios;              /* compilado para iOS (device) */
    int  is_simulator;
    long page_size;
    uint64_t total_ram;
    char cpu_brand[64];
    char note[128];           /* explicação legível da limitação principal */
} pr_cap_info;

void pr_cap_probe(pr_cap_info* out);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_CAP_H */
