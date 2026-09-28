/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_types.h"

#include <time.h>
#include <string.h>

const char* pr_status_str(pr_status s) {
    switch (s) {
        case PR_OK: return "ok";
        case PR_ERR_INVALID: return "argumento inválido";
        case PR_ERR_NOMEM: return "sem memória";
        case PR_ERR_IO: return "erro de E/S";
        case PR_ERR_FORMAT: return "formato inválido";
        case PR_ERR_UNSUPPORTED: return "não suportado";
        case PR_ERR_STATE: return "estado inválido";
        case PR_ERR_FAULT: return "falta do convidado";
        case PR_ERR_RANGE: return "fora dos limites";
        default: return "status desconhecido";
    }
}

uint64_t pr_now_ms(void) {
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}
