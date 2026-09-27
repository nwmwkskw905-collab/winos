/* test_dll.c — FASE 6: infraestrutura de DLL (carga PE32+, exports, DllMain,
 * refcount, chamadas de funções do convidado) + PE12 e2e via LoadLibraryA. */
#include "pt_util.h"
#include <portico/pr_peproc.h>
#include <portico/pr_winhello.h>
#include <portico/pr_log.h>
#include <stdlib.h>
#include <string.h>

static int dll_log_has(const pr_log* log, const char* needle) {
    pr_log_entry es[512];
    size_t n = pr_log_read(log, es, 512, 0);
    for (size_t i = 0; i < n; i++)
        if (strstr(es[i].msg, needle)) return 1;
    return 0;
}

static pr_status dll_run(pr_peproc* p, uint64_t* total) {
    *total = 0;
    pr_status st = PR_OK;
    for (int i = 0; i < 300 && pr_peproc_state(p) == PR_PEPROC_STOP_NONE; i++) {
        uint64_t executed = 0;
        st = pr_peproc_step(p, 1000, &executed);
        *total += executed;
        if (st != PR_OK) break;
    }
    return st;
}

void test_dll(void) {
    /* ---- 1) ciclo de vida de DLL via API do host ---- */
    pr_log* log = pr_log_create(512);
    uint8_t* data = NULL;
    size_t len = 0;
    CHECK(pr_winhello_build(10, (void**)&data, &len) == PR_OK);
    pr_peproc* p = NULL;
    CHECK(pr_peproc_create(data, len, log, &p) == PR_OK);
    CHECK(pr_peproc_prepare(p) == PR_OK);
    free(data);

    uint8_t* dll = NULL;
    size_t dlen = 0;
    CHECK(pr_winhello_build(11, (void**)&dll, &dlen) == PR_OK);
    CHECK(pr_peproc_provide_dll(p, "TestDLL.DLL", dll, dlen) == PR_OK);
    free(dll);

    uint64_t h1 = 0;
    CHECK(pr_peproc_load_dll(p, "testdll.dll", &h1) == PR_OK);
    CHECK(h1 != 0);
    CHECK(pr_peproc_module_handle(p, "TestDll.dll") == h1);   /* canônico */
    uint64_t h2 = 0;
    CHECK(pr_peproc_load_dll(p, "testdll.dll", &h2) == PR_OK); /* refcount */
    CHECK(h2 == h1);

    uint64_t ret = 0;
    uint64_t args[4] = { 3, 5, 0, 0 };
    uint64_t add = pr_peproc_module_proc(p, h1, "add", 0);
    CHECK(add != 0);
    CHECK(pr_peproc_call(p, add, args, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)ret, 8);
    uint64_t ga = pr_peproc_module_proc(p, h1, "get_answer", 0);
    CHECK(ga != 0);
    CHECK(pr_peproc_call(p, ga, NULL, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)ret, 42);
    uint64_t tw = pr_peproc_module_proc(p, h1, "twice", 0);
    CHECK(tw != 0);
    args[0] = 21;
    CHECK(pr_peproc_call(p, tw, args, &ret) == PR_OK);
    CHECK_EQ_U32((uint32_t)ret, 42);
    CHECK(pr_peproc_module_proc(p, h1, "nao_existe", 0) == 0);

    /* FreeLibrary com refcount 2 -> 1 (ainda carregada) -> 0 (unloaded) */
    CHECK(pr_peproc_free_dll(p, h1) == PR_OK);
    CHECK(pr_peproc_module_handle(p, "testdll.dll") == h1);
    CHECK(pr_peproc_free_dll(p, h1) == PR_OK);
    CHECK(pr_peproc_module_handle(p, "testdll.dll") == 0);

    /* DLL não fornecida: falha honesta (nunca finge carga) */
    uint64_t hmiss = 0;
    CHECK(pr_peproc_load_dll(p, "nao_fornecida.dll", &hmiss) != PR_OK);
    CHECK(hmiss == 0);
    pr_peproc_destroy(p);
    pr_log_destroy(log);

    /* ---- 2) PE12 e2e: LoadLibraryA/GetProcAddress/call/FreeLibrary ---- */
    log = pr_log_create(512);
    data = NULL;
    CHECK(pr_winhello_build(12, (void**)&data, &len) == PR_OK);
    p = NULL;
    CHECK(pr_peproc_create(data, len, log, &p) == PR_OK);
    CHECK(pr_peproc_prepare(p) == PR_OK);
    free(data);
    dll = NULL;
    CHECK(pr_winhello_build(11, (void**)&dll, &dlen) == PR_OK);
    CHECK(pr_peproc_provide_dll(p, "testdll.dll", dll, dlen) == PR_OK);
    free(dll);
    uint64_t total = 0;
    pr_status st = dll_run(p, &total);
    CHECK(st == PR_OK);
    CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_EXIT);
    CHECK_EQ_U32(pr_peproc_exit_code(p), 0);   /* 7 checagens internas */
    CHECK(total > 20);
    /* logs do ciclo de vida */
    CHECK(dll_log_has(log, "dll provided testdll.dll"));
    CHECK(dll_log_has(log, "DllMain testdll.dll attach ret=1"));
    CHECK(dll_log_has(log, "dll unloaded testdll.dll"));
    pr_peproc_destroy(p);
    pr_log_destroy(log);
}
