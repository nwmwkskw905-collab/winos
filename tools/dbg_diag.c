/* dbg_diag.c — roda um PE no runtime real e imprime diagnostico/exit code. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "portico/pr_peproc.h"
#include "portico/pr_win32.h"
#include "portico/pr_log.h"

static uint8_t* load(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f); free(buf); return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "uso: dbg_diag <pe.exe>\n"); return 1; }
    size_t ilen = 0;
    uint8_t* img = load(argv[1], &ilen);
    if (!img) { fprintf(stderr, "falha ao ler %s\n", argv[1]); return 1; }
    pr_log* log = pr_log_create(4096);
    pr_peproc* p = NULL;
    if (pr_peproc_create(img, ilen, log, &p) != PR_OK) {
        fprintf(stderr, "create falhou\n");
        return 1;
    }
    /* G29: provisiona o prefixo/VFS do convidado (mesmo papel do host real).
     * Sem prefixo o VFS recusa por seguranca (fail-closed) — comportamento correto. */
    mkdir("build/win_fs", 0777);
    pr_peproc_set_fs_root(p, "build/win_fs");

    if (pr_peproc_prepare(p) != PR_OK) {
        printf("prepare falhou\n%s\n", pr_peproc_diagnostic(p));
        return 0;
    }
    /* G53: variável de teste determinística (suporte existente pr_win32_env_set). */
    pr_win32_env_set(pr_peproc_win32(p), "G53_VAR", "abcdefghij0123456789");
    uint64_t exec = 0;
    int i = 0;
    while (i < 200000 && !pr_peproc_exited(p)) {
        if (pr_peproc_step(p, 10000, &exec) != PR_OK) {
            printf("step[%d] falhou\n%s\n", i, pr_peproc_diagnostic(p));
            return 0;
        }
        i++;
    }
    printf("exited=%d rc=%u steps=%d exec=%llu\n%s\n",
           pr_peproc_exited(p), pr_peproc_exit_code(p), i,
           (unsigned long long)exec, pr_peproc_diagnostic(p));
    return 0;
}
