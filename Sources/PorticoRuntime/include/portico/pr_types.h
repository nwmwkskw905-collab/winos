/* Portico Runtime — tipos básicos e status. Licença: projeto próprio (ver LICENSES.md). */
#ifndef PORTICO_PR_TYPES_H
#define PORTICO_PR_TYPES_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pr_status {
    PR_OK            = 0,
    PR_ERR_INVALID   = 1, /* argumento inválido */
    PR_ERR_NOMEM     = 2, /* sem memória */
    PR_ERR_IO        = 3, /* erro de E/S */
    PR_ERR_FORMAT    = 4, /* formato inválido (ex.: não é PE/ZIP) */
    PR_ERR_UNSUPPORTED = 5, /* recurso conhecido mas não implementado */
    PR_ERR_STATE     = 6, /* chamada fora de estado */
    PR_ERR_FAULT     = 7, /* falta do convidado (código interpretado) */
    PR_ERR_RANGE     = 8  /* fora dos limites */
} pr_status;

const char* pr_status_str(pr_status s);

/* Tempo monotônico em milissegundos. */
uint64_t pr_now_ms(void);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_TYPES_H */
