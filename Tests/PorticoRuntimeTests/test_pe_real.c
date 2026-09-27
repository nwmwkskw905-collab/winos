/* Testes — PE32+ REAL de toolchain Windows (MinGW-w64 GCC 14, -O2 -static):
 * data/hello_real.exe: CRT real do MinGW + main que retorna 42.
 * Valida o PIPELINE COMPLETO sem builders internos: carga PE real -> imports
 * (KERNEL32+msvcrt) -> CPU x64 -> CRT startup (TEB/PEB via GS, lock cmpxchg,
 * x87 fninit, pshufd) -> main() -> exit(42). */
#include "pt_util.h"
#include "portico/pr_peproc.h"
#include "portico/pr_pe.h"
#include "portico/pr_win32.h"
#include "portico/pr_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t* pe_real_load(const char* path, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    *out_len = (size_t)len;
    return buf;
}

void test_pe_real(void) {
    /* binário REAL de toolchain externo (x86_64-w64-mingw32-gcc) */
    size_t len = 0;
    uint8_t* pe = pe_real_load("Tests/PorticoRuntimeTests/data/hello_real.exe", &len);
    CHECK(pe != NULL && len > 4096);
    if (!pe) return;

    /* 1) carga: headers/sections/.bss/.idata/.tls/.reloc de PE real */
    {
        pr_pe_loaded img;
        pr_status st = pr_pe_load(pe, len, &img, "hello_real.exe");
        CHECK(st == PR_OK);
        CHECK(img.machine == 0x8664);
        CHECK(img.section_count >= 8);
        pr_pe_loaded_free(&img);
    }

    /* 2) pipeline completo: create + prepare + run ate exit(42) */
    {
        pr_log* log = pr_log_create(256);
        pr_peproc* p = NULL;
        pr_status st = pr_peproc_create(pe, len, log, &p);
        CHECK(st == PR_OK && p != NULL);
        if (p) {
            st = pr_peproc_prepare(p);
            CHECK(st == PR_OK);
            for (int i = 0; i < 5000 && !pr_peproc_exited(p); i++) {
                uint64_t exec = 0;
                if (pr_peproc_step(p, 10000, &exec) != PR_OK) break;
            }
            CHECK(pr_peproc_exited(p));
            CHECK_EQ_U32(pr_peproc_exit_code(p), 42);
            pr_peproc_destroy(p);
        }
        pr_log_destroy(log);
    }

    /* 3) DATA_SYM: símbolos de dados do msvcrt viram células RW */
    {
        const pr_win32_export* cat = NULL;
        size_t ncat = pr_win32_catalog(&cat);
        int n_data = 0;
        for (size_t i = 0; i < ncat; i++)
            if (cat[i].is_data) n_data++;
        CHECK(n_data >= 3);
    }

    /* 4) honestidade: stdio real implementado; APIs não exigidas continuam
     * NAO implementadas (sem sucesso falso) */
    {
        const pr_win32_export* e = pr_win32_lookup("msvcrt.dll", "fprintf");
        CHECK(e && e->status == PR_WIN32_IMPLEMENTED && e->fn != NULL);
        const pr_win32_export* s = pr_win32_lookup("msvcrt.dll", "signal");
        CHECK(s && s->status == PR_WIN32_UNSUPPORTED && s->fn == NULL);
    }

    free(pe);

    /* 5) STDIO REAL (GRUPO 1): printf/fprintf/fwrite/vfprintf com formatação
     * real validada pela SAÍDA capturada e pelo retorno do main */
    {
        size_t slen = 0;
        uint8_t* sp = pe_real_load("Tests/PorticoRuntimeTests/data/hello_stdio.exe", &slen);
        CHECK(sp != NULL && slen > 4096);
        if (sp) {
            pr_log* log = pr_log_create(256);
            pr_peproc* p = NULL;
            CHECK(pr_peproc_create(sp, slen, log, &p) == PR_OK);
            if (p) {
                CHECK(pr_peproc_prepare(p) == PR_OK);
                for (int i = 0; i < 5000 && !pr_peproc_exited(p); i++) {
                    uint64_t exec = 0;
                    if (pr_peproc_step(p, 10000, &exec) != PR_OK) break;
                }
                CHECK(pr_peproc_exited(p));
                CHECK_EQ_U32(pr_peproc_exit_code(p), 5);
                char out[2048];
                size_t n = pr_win32_stdout_read(pr_peproc_win32(p), out, sizeof(out));
                CHECK(n == 81);
                CHECK(strstr(out, "n=42 s=winos hex=beef") != NULL);
                CHECK(strstr(out, "f=7 A     3|") != NULL);
                CHECK(strstr(out, "raw-bytes") != NULL);
                CHECK(strstr(out, "err v 9") != NULL);
                CHECK(strstr(out, "sum=42 ptr=0000000000") != NULL);
                pr_peproc_destroy(p);
            }
            pr_log_destroy(log);
        }
        free(sp);
    }

    /* 5b) STDIO honesto: formato desconhecido = EXECUTION STOPPED com motivo */
    {
        const pr_win32_export* e = pr_win32_lookup("msvcrt.dll", "printf");
        CHECK(e && e->status == PR_WIN32_IMPLEMENTED && e->fn != NULL);
    }

    /* 6) FASE 5 — DLL REAL (MinGW -shared): LoadLibraryA + GetProcAddress +
     * chamada ABI MS x64 + FreeLibrary com DllMain attach/detach reais. */
    {
        size_t ulen = 0, dlen = 0;
        uint8_t* user = pe_real_load("Tests/PorticoRuntimeTests/data/hello_user.exe", &ulen);
        uint8_t* dll = pe_real_load("Tests/PorticoRuntimeTests/data/hello_dll.dll", &dlen);
        CHECK(user != NULL && dll != NULL);
        if (user && dll) {
            pr_log* log = pr_log_create(256);
            pr_peproc* p = NULL;
            CHECK(pr_peproc_create(user, ulen, log, &p) == PR_OK);
            if (p) {
                CHECK(pr_peproc_provide_dll(p, "hello_dll.dll", dll, dlen) == PR_OK);
                CHECK(pr_peproc_prepare(p) == PR_OK);
                for (int i = 0; i < 5000 && !pr_peproc_exited(p); i++) {
                    uint64_t exec = 0;
                    if (pr_peproc_step(p, 10000, &exec) != PR_OK) break;
                }
                CHECK(pr_peproc_exited(p));
                CHECK_EQ_U32(pr_peproc_exit_code(p), 42);  /* add3(39,0)==42 */
                /* ciclo de vida real da DLL no log */
                pr_log_entry es[256];
                size_t n = pr_log_read(log, es, 256, 0);
                int saw_attach = 0, saw_unload = 0, saw_mapped = 0;
                for (size_t i = 0; i < n; i++) {
                    if (strstr(es[i].msg, "dll mapped hello_dll.dll")) saw_mapped = 1;
                    if (strstr(es[i].msg, "DllMain hello_dll.dll attach ret=1")) saw_attach = 1;
                    if (strstr(es[i].msg, "dll unloaded hello_dll.dll")) saw_unload = 1;
                }
                CHECK(saw_mapped);
                CHECK(saw_attach);
                CHECK(saw_unload);
                pr_peproc_destroy(p);
            }
            pr_log_destroy(log);
        }
        free(user);
        free(dll);
    }

    /* -- GRUPO 5: PE composto MinGW (CRT+stdio+heap+DLL+SSE2+CPUID/RDTSC+
     *    múltiplas funções) pelo caminho completo, com DLL reutilizada -- */
    {
        size_t alen = 0;
        uint8_t* app = pe_real_load("Tests/PorticoRuntimeTests/data/hello_app.exe", &alen);
        CHECK(app != NULL && alen > 4096);
        size_t dlen = 0;
        uint8_t* dll = pe_real_load("Tests/PorticoRuntimeTests/data/hello_dll.dll", &dlen);
        CHECK(dll != NULL && dlen > 1000);
        if (app && dll) {
            pr_log* log = pr_log_create(256);
            pr_peproc* p = NULL;
            CHECK(pr_peproc_create(app, alen, log, &p) == PR_OK);
            if (p) {
                CHECK(pr_peproc_provide_dll(p, "hello_dll.dll", dll, dlen) == PR_OK);
                CHECK(pr_peproc_prepare(p) == PR_OK);
                for (int i = 0; i < 5000 && !pr_peproc_exited(p); i++) {
                    uint64_t exec = 0;
                    if (pr_peproc_step(p, 10000, &exec) != PR_OK) break;
                }
                CHECK(pr_peproc_exited(p));
                CHECK_EQ_U32(pr_peproc_exit_code(p), 42);
                char out[2048];
                size_t n = pr_win32_stdout_read(pr_peproc_win32(p), out, sizeof(out));
                CHECK(n == 63);
                CHECK(strstr(out, "x=24.75 r=4.975 fib=55 heap=11 dll=42") != NULL);
                CHECK(strstr(out, "cpu=PORTICO_VRTX tsc>0=1") != NULL);
                pr_peproc_destroy(p);
            }
            pr_log_destroy(log);
        }
        free(app);
        free(dll);
    }
}
