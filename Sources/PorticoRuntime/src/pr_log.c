/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_log.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <pthread.h>

struct pr_log {
    pr_log_entry* entries;
    size_t cap;
    uint64_t total;   /* monotônico: total já escrito */
    pthread_mutex_t mu;
};

pr_log* pr_log_create(size_t capacity) {
    if (capacity == 0) capacity = 2048;
    pr_log* log = (pr_log*)calloc(1, sizeof(pr_log));
    if (!log) return NULL;
    log->entries = (pr_log_entry*)calloc(capacity, sizeof(pr_log_entry));
    if (!log->entries) { free(log); return NULL; }
    log->cap = capacity;
    pthread_mutex_init(&log->mu, NULL);
    return log;
}

void pr_log_destroy(pr_log* log) {
    if (!log) return;
    pthread_mutex_destroy(&log->mu);
    free(log->entries);
    free(log);
}

const char* pr_log_level_str(pr_log_level level) {
    switch (level) {
        case PR_LOG_DEBUG: return "DEBUG";
        case PR_LOG_INFO:  return "INFO";
        case PR_LOG_WARN:  return "WARNING";
        case PR_LOG_ERROR: return "ERROR";
        default: return "?";
    }
}

void pr_log_write(pr_log* log, pr_log_level level, const char* cat,
                  const char* fmt, ...) {
    if (!log || !fmt) return;
    pr_log_entry e;
    memset(&e, 0, sizeof(e));
    e.ts_ms = pr_now_ms();
    e.level = level;
    if (cat) {
        strncpy(e.cat, cat, PR_LOG_CAT_MAX - 1);
    } else {
        strncpy(e.cat, "geral", PR_LOG_CAT_MAX - 1);
    }
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(e.msg, PR_LOG_MSG_MAX, fmt, ap);
    va_end(ap);

    pthread_mutex_lock(&log->mu);
    size_t slot = (size_t)(log->total % log->cap);
    log->entries[slot] = e;
    log->total++;
    pthread_mutex_unlock(&log->mu);
}

size_t pr_log_read(const pr_log* log, pr_log_entry* out, size_t max, uint64_t offset) {
    if (!log || !out || max == 0) return 0;
    pthread_mutex_lock((pthread_mutex_t*)&log->mu);
    uint64_t total = log->total;
    uint64_t first = total > log->cap ? total - log->cap : 0;
    if (offset < first) offset = first;
    size_t n = 0;
    while (offset < total && n < max) {
        out[n++] = log->entries[(size_t)(offset % log->cap)];
        offset++;
    }
    pthread_mutex_unlock((pthread_mutex_t*)&log->mu);
    return n;
}

uint64_t pr_log_count(const pr_log* log) {
    if (!log) return 0;
    pthread_mutex_lock((pthread_mutex_t*)&log->mu);
    uint64_t t = log->total;
    pthread_mutex_unlock((pthread_mutex_t*)&log->mu);
    return t;
}

void pr_log_clear(pr_log* log) {
    if (!log) return;
    pthread_mutex_lock(&log->mu);
    memset(log->entries, 0, log->cap * sizeof(pr_log_entry));
    log->total = 0;
    pthread_mutex_unlock(&log->mu);
}

size_t pr_log_capacity(const pr_log* log) {
    return log ? log->cap : 0;
}
