#include "pt_util.h"
#include "portico/pr_log.h"

void test_log(void) {
    pr_log* log = pr_log_create(4);
    CHECK(log != NULL);
    CHECK_EQ_U32(pr_log_capacity(log), 4);

    pr_log_write(log, PR_LOG_INFO, "teste", "olá %d", 42);
    pr_log_write(log, PR_LOG_WARN, "geral", "cuidado");
    pr_log_write(log, PR_LOG_ERROR, "cpu", "falta grave");
    pr_log_write(log, PR_LOG_DEBUG, "gpu", "frame");

    pr_log_entry e[8];
    size_t n = pr_log_read(log, e, 8, 0);
    CHECK_EQ_U32(n, 4);
    CHECK_STR(e[0].msg, "olá 42");
    CHECK_STR(e[0].cat, "teste");
    CHECK(e[0].level == PR_LOG_INFO);
    CHECK_STR(e[2].msg, "falta grave");
    CHECK_STR(pr_log_level_str(e[2].level), "ERROR");
    CHECK_STR(pr_log_level_str(PR_LOG_WARN), "WARNING");

    /* wrap: capacity 4, escreve mais 2 → antigas descartadas */
    pr_log_write(log, PR_LOG_INFO, "x", "novo1");
    pr_log_write(log, PR_LOG_INFO, "x", "novo2");
    CHECK_EQ_U32((unsigned)pr_log_count(log), 6);
    n = pr_log_read(log, e, 8, 0);
    CHECK_EQ_U32(n, 4); /* só as 4 mais recentes retidas */
    /* offset 0 foi descartado; a leitura clampa para a mais antiga retida */
    CHECK_STR(e[0].msg, "falta grave");
    CHECK_STR(e[3].msg, "novo2");
    /* leitura por offset lógico */
    n = pr_log_read(log, e, 8, 5);
    CHECK_EQ_U32(n, 1);
    CHECK_STR(e[0].msg, "novo2");
    n = pr_log_read(log, e, 8, 99);
    CHECK_EQ_U32(n, 0);

    pr_log_clear(log);
    CHECK_EQ_U32((unsigned)pr_log_count(log), 0);
    n = pr_log_read(log, e, 8, 0);
    CHECK_EQ_U32(n, 0);
    pr_log_destroy(log);
}
