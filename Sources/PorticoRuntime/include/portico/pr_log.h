/* Portico Runtime — log em ring buffer thread-safe. */
#ifndef PORTICO_PR_LOG_H
#define PORTICO_PR_LOG_H

#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pr_log_level {
    PR_LOG_DEBUG = 0,
    PR_LOG_INFO  = 1,
    PR_LOG_WARN  = 2,
    PR_LOG_ERROR = 3
} pr_log_level;

#define PR_LOG_MSG_MAX 192
#define PR_LOG_CAT_MAX 24

typedef struct pr_log_entry {
    uint64_t   ts_ms;
    pr_log_level level;
    char       cat[PR_LOG_CAT_MAX];
    char       msg[PR_LOG_MSG_MAX];
} pr_log_entry;

typedef struct pr_log pr_log;

pr_log*  pr_log_create(size_t capacity); /* nº de entradas retidas; 0 => 2048 */
void     pr_log_destroy(pr_log* log);
void     pr_log_write(pr_log* log, pr_log_level level, const char* cat,
                      const char* fmt, ...);
/* Lê entradas a partir de `offset` lógico (contador monotônico). Retorna nº lidas. */
size_t   pr_log_read(const pr_log* log, pr_log_entry* out, size_t max, uint64_t offset);
uint64_t pr_log_count(const pr_log* log); /* total já escrito (monotônico) */
void     pr_log_clear(pr_log* log);
size_t   pr_log_capacity(const pr_log* log);

const char* pr_log_level_str(pr_log_level level);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_LOG_H */
