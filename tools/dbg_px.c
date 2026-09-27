/* dbg_px.c — diagnóstico: executa um PE e imprime pixels do framebuffer real
 * do contexto do processo (glReadPixels), mesmo após a saída (o frame da fase
 * em curso fica intacto quando o PE sai antes do SwapBuffers).
 * Uso: dbg_px <pe.exe> "x,y;x,y;..." */
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
        fclose(f); free(buf); return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "uso: dbg_px <pe.exe> \"x,y;x,y\"\n"); return 2; }
    size_t len = 0;
    unsigned char* data = load_file(argv[1], &len);
    if (!data) { fprintf(stderr, "abrir exe falhou\n"); return 2; }
    pr_log* log = pr_log_create(1024);
    pr_peproc* p = NULL;
    if (pr_peproc_create(data, len, log, &p) != PR_OK || !p) {
        fprintf(stderr, "criar proc falhou\n"); return 2;
    }
    free(data);
    /* G29: provisiona o prefixo/VFS do convidado (mesmo papel do host real).
     * Sem prefixo o VFS recusa por seguranca (fail-closed) — comportamento correto. */
    mkdir("build/win_fs", 0777);
    pr_peproc_set_fs_root(p, "build/win_fs");

    if (pr_peproc_prepare(p) != PR_OK) {
        fprintf(stderr, "prepare falhou: %s\n", pr_peproc_diagnostic(p));
        pr_peproc_destroy(p); return 2;
    }
    uint64_t n = 0;
    (void)pr_peproc_step(p, 50000000, &n);
    printf("exited=%d rc=%u\n", pr_peproc_exited(p),
           (unsigned)pr_peproc_exit_code(p));

    pr_win32_ctx* ctx = pr_peproc_win32(p);
    size_t sz = 0;
    unsigned char* scr = (unsigned char*)pr_win32_scratch(ctx, &sz);
    if (!scr || sz < 64) { fprintf(stderr, "scratch indisponivel\n"); return 2; }
    unsigned char* px = scr + 32;

    char spec[512];
    snprintf(spec, sizeof(spec), "%s", argv[2]);
    for (char* tok = strtok(spec, ";"); tok;
         tok = strtok(NULL, ";")) {
        int x = 0, y = 0;
        if (sscanf(tok, "%d,%d", &x, &y) != 2) continue;
        uint64_t a[7] = { (uint64_t)x, (uint64_t)y, 1, 1,
                          0x1908 /*GL_RGBA*/, 0x1401 /*GL_UNSIGNED_BYTE*/,
                          (uint64_t)(uintptr_t)px };
        uint64_t ret = 0;
        pr_win32_call(ctx, "opengl32.dll", "glReadPixels", a, 7, &ret);
        printf("px(%3d,%3d) = (%3u,%3u,%3u)\n", x, y, px[0], px[1], px[2]);
    }
    pr_peproc_destroy(p);
    pr_log_destroy(log);
    return 0;
}
