/* dbg_input.c — driver da bateria de PEs (reconstruído após limpeza do build/).
 * Uso: dbg_input <pe.exe> [noinput]
 * Sem "noinput": injeta a fonte de eventos determinística ANTES da execução
 * (tecla 'A' down+up, clique, wheel, avanço de 10 ms p/ WM_TIMER) — roteiro do
 * hello_input.c. Com "noinput": nenhum evento (PEs que não precisam de input).
 * Saída: "exited=<0|1> rc=<n> log=<entradas>" ou motivo honesto de falha. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "portico/pr_win32.h"
#include "portico/pr_peproc.h"
#include "portico/pr_log.h"

static unsigned char* load_file(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char* buf = malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 2) { fprintf(stderr, "uso: dbg_input <pe.exe> [noinput]\n"); return 2; }
    int noinput = (argc > 2 && strcmp(argv[2], "noinput") == 0);

    size_t ilen = 0;
    unsigned char* img = load_file(argv[1], &ilen);
    if (!img) { printf("abrir exe falhou\n"); return 1; }

    pr_log* log = pr_log_create(4096);
    pr_peproc* p = NULL;
    if (pr_peproc_create(img, ilen, log, &p) != PR_OK || !p) {
        printf("create falhou\n");
        return 1;
    }
    /* DLL real do convidado (host registra bytes permitidos — FASE 6) */
    {
        char dllpath[512];
        const char* slash = strrchr(argv[1], '/');
        size_t dirlen = slash ? (size_t)(slash - argv[1] + 1) : 0;
        snprintf(dllpath, sizeof dllpath, "%.*s%s", (int)dirlen, argv[1],
                 "hello_dll.dll");
        size_t dlen = 0;
        unsigned char* dbuf = load_file(dllpath, &dlen);
        if (dbuf) {
            pr_peproc_provide_dll(p, "hello_dll.dll", dbuf, dlen);
            free(dbuf);
        }
    }
    /* G29: provisiona o prefixo/VFS do convidado (mesmo papel do host real).
     * Sem prefixo o VFS recusa por seguranca (fail-closed) — comportamento correto. */
    mkdir("build/win_fs", 0777);
    pr_peproc_set_fs_root(p, "build/win_fs");

    if (pr_peproc_prepare(p) != PR_OK) {
        const char* d = pr_peproc_diagnostic(p);
        printf("prepare falhou: %s\n", d ? d : "(sem diagnostico)");
        return 1;
    }

    if (!noinput) {
        pr_win32_ctx* w = pr_peproc_win32(p);
        if (w) {
            pr_win32_input_key(w, W32_VK_A, 1);          /* WM_KEYDOWN 'A' */
            pr_win32_input_key(w, W32_VK_A, 0);          /* WM_KEYUP */
            pr_win32_input_mouse(w, PR_MOUSE_MOVE, 20, 20, 0);
            pr_win32_input_mouse(w, PR_MOUSE_LDOWN, 20, 20, 0);
            pr_win32_input_mouse(w, PR_MOUSE_LUP, 20, 20, 0);
            pr_win32_input_mouse(w, PR_MOUSE_WHEEL, 30, 30, 120);
            pr_win32_advance_time(w, 10);                /* WM_TIMER 10 ms */
        }
    }

    uint64_t exec = 0;
    for (int i = 0; i < 200000 && !pr_peproc_exited(p); i++) {
        if (pr_peproc_step(p, 10000, &exec) != PR_OK) break;
    }
    printf("exited=%d rc=%u log=%llu\n", pr_peproc_exited(p),
           (unsigned)pr_peproc_exit_code(p),
           (unsigned long long)pr_log_count(log));
    pr_peproc_destroy(p);
    pr_log_destroy(log);
    free(img);
    return 0;
}
