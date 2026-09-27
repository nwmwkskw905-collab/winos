/* Portico Runtime — memória virtual do processo Windows (regiões e proteções).
 *
 * Camada REAL de endereçamento do convidado: regiões mapeadas com permissões
 * R/W/X, leitura/escrita/execução validadas e tradução vaddr→host. Usada pelo
 * processo PE (pr_peproc) e pelo guard de acesso do interpretador (pr_cpu).
 * Respeita o sandbox do iOS: a memória é heap comum do app — sem contornar
 * proteções do sistema (W^X sonda em pr_cap). */
#ifndef PORTICO_PR_VM_H
#define PORTICO_PR_VM_H

#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PR_VM_PROT_NONE = 0,
    PR_VM_PROT_R = 1,
    PR_VM_PROT_W = 2,
    PR_VM_PROT_X = 4
};

#define PR_VM_MAX_REGIONS 64
#define PR_VM_TAG_MAX 16

typedef struct pr_vm pr_vm;

typedef struct pr_vm_region {
    uint32_t base;
    uint32_t size;
    int      prot;             /* bitmask R/W/X */
    char     tag[PR_VM_TAG_MAX];
} pr_vm_region;

/* Cria espaço de endereçamento de `space` bytes (endereço = offset no backing;
 * limite de 256 MiB; toda memória nasce zerada). */
pr_vm* pr_vm_create(size_t space);
/* Como pr_vm_create, mas sobre um backing já alocado (memória física da CPU).
 * O backing deve ter space bytes e viver mais que o pr_vm. */
pr_vm* pr_vm_create_on(size_t space, void* backing);
/* Base do backing (memória física emprestada) e tamanho — p/ 2º contexto de CPU.
 * G42: necessário p/ threads guest reais. Somente leitura do estado. */
uint8_t* pr_vm_backing(pr_vm* vm, uint32_t* out_space);
void   pr_vm_destroy(pr_vm* vm);
size_t pr_vm_space(const pr_vm* vm);

/* Mapeia [addr, addr+len) com `prot` (sem sobrepor regiões). out_host opcional
 * recebe o ponteiro host da região. PR_ERR_STATE se sobrepor; PR_ERR_RANGE se
 * fora do espaço. */
pr_status pr_vm_map(pr_vm* vm, uint32_t addr, size_t len, int prot,
                    const char* tag, void** out_host);
pr_status pr_vm_unmap(pr_vm* vm, uint32_t addr, size_t len);
/* Altera proteção de uma região mapeada (endereço e tamanho devem cobrir uma
 * ou mais regiões completas). */
pr_status pr_vm_protect(pr_vm* vm, uint32_t addr, size_t len, int prot);

/* Aloca `len` bytes alinhados em região livre (usa o espaço do fim p/ baixo).
 * out_addr recebe o endereço do convidado. */
pr_status pr_vm_alloc(pr_vm* vm, size_t len, int prot, const char* tag,
                      uint32_t* out_addr, void** out_host);

/* Traduz vaddr+len para ponteiro host exigindo acesso `acc` (PR_VM_PROT_*).
 * NULL se não mapeado ou protegido. */
void* pr_vm_translate(pr_vm* vm, uint32_t addr, size_t len, int acc);

/* Leitura/escrita validadas (exigem R/W). Copiam de/para o backing. */
pr_status pr_vm_read(const pr_vm* vm, uint32_t addr, void* out, size_t len);
pr_status pr_vm_write(pr_vm* vm, uint32_t addr, const void* in, size_t len);

/* Consulta de regiões (retorna nº copiado; out_count = total). */
/* Grava sem checar proteção (somente LOAD: IAT/thunks antes das proteções
 * finais). Limites e mapeamento continuam valendo; uso indevido é erro. */
pr_status pr_vm_loader_write(pr_vm* vm, uint32_t addr, const void* in, size_t len);

/* Busca um vão livre de `span` bytes (alinhado a página), do fim para o
 * baixo, SEM mapear. PR_ERR_RANGE se não couber. */
pr_status pr_vm_find_gap(pr_vm* vm, uint32_t span, uint32_t* out_base);

size_t pr_vm_regions(const pr_vm* vm, pr_vm_region* out, size_t max);

/* Verificação de acesso (1 = permitido; registra falha p/ diagnóstico). */
int  pr_vm_check(pr_vm* vm, uint32_t addr, size_t len, int acc);
/* Último acesso negado (p/ relatório EXECUTION STOPPED). */
int  pr_vm_last_denied(const pr_vm* vm, uint32_t* out_addr, size_t* out_len,
                       int* out_acc);
uint32_t pr_vm_denied_count(const pr_vm* vm);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_VM_H */
