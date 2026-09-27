/* Testes da camada Win32: dispatch real, catálogo honesto (unsupported loga
 * e falha — nunca finge), handles, heap, console e encerramento. */
#include "pt_util.h"

#include "portico/pr_win32.h"
#include "portico/pr_log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void test_win32(void) {
    pr_log* log = pr_log_create(128);
    pr_win32_ctx* ctx = pr_win32_create(log);
    CHECK(ctx != NULL);

    /* Espaço de memória do convidado (scratch validado). Ponteiros de saída
     * das APIs precisam pertencer a ele — mesma disciplina do guest real. */
    size_t scratch_sz = 0;
    uint8_t* scratch = pr_win32_scratch(ctx, &scratch_sz);
    CHECK(scratch != NULL && scratch_sz > 64);
    uint64_t* slots = (uint64_t*)(void*)(scratch + 128); /* slots[0..3] */

    /* -- relógio real -- */
    uint64_t t1 = 0, t2 = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetTickCount64", NULL, 0, &t1) == PR_OK);
    CHECK(pr_win32_call(ctx, "kernel32", "GetTickCount64", NULL, 0, &t2) == PR_OK);
    CHECK(t2 >= t1);

    /* QPC real */
    slots[0] = 0; slots[1] = 0;
    uint64_t args[4];
    args[0] = (uint64_t)(uintptr_t)&slots[0];
    CHECK(pr_win32_call(ctx, "kernel32.dll", "QueryPerformanceFrequency", args, 1, NULL) == PR_OK);
    CHECK(slots[0] == 1000000000ull);
    args[0] = (uint64_t)(uintptr_t)&slots[1];
    CHECK(pr_win32_call(ctx, "kernel32.dll", "QueryPerformanceCounter", args, 1, NULL) == PR_OK);
    CHECK(slots[1] > 0);

    /* -- heap real -- */
    uint64_t heap = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetProcessHeap", NULL, 0, &heap) == PR_OK);
    CHECK(heap != 0);
    args[0] = heap; args[1] = 0; args[2] = 64;
    uint64_t blk = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "HeapAlloc", args, 3, &blk) == PR_OK);
    CHECK(blk != 0);
    /* ponteiro validado */
    CHECK(pr_win32_ptr(ctx, blk, 64) != NULL);
    CHECK(pr_win32_ptr(ctx, blk, 65) == NULL); /* fora do bloco */
    CHECK(pr_win32_ptr(ctx, 0xDEAD0000ull, 4) == NULL);

    args[0] = heap; args[1] = 0; args[2] = blk;
    uint64_t sz = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "HeapSize", args, 3, &sz) == PR_OK);
    CHECK_EQ_U32(sz, 64);
    uint64_t freed = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "HeapFree", args, 3, &freed) == PR_OK);
    CHECK_EQ_U32(freed, 1);
    CHECK(pr_win32_ptr(ctx, blk, 8) == NULL); /* liberado */

    /* VirtualAlloc/VirtualFree */
    args[0] = 0; args[1] = 128;
    uint64_t vblk = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "VirtualAlloc", args, 2, &vblk) == PR_OK);
    CHECK(vblk != 0 && pr_win32_ptr(ctx, vblk, 128) != NULL);
    args[0] = vblk;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "VirtualFree", args, 1, NULL) == PR_OK);
    CHECK(pr_win32_ptr(ctx, vblk, 4) == NULL);

    /* -- strings (scratch validado) -- */
    memcpy(scratch + 32, "abcde", 6);
    args[0] = (uint64_t)(uintptr_t)(scratch + 32);
    uint64_t slen = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "lstrlenA", args, 1, &slen) == PR_OK);
    CHECK_EQ_U32(slen, 5);
    memcpy(scratch + 64, "0123456789", 11);
    args[0] = (uint64_t)(uintptr_t)(scratch + 32);
    args[1] = (uint64_t)(uintptr_t)(scratch + 64);
    uint64_t dst = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "lstrcpyA", args, 2, &dst) == PR_OK);
    CHECK_EQ_U32(dst, args[0]);
    CHECK(strcmp((char*)scratch + 32, "0123456789") == 0);

    /* lstrcmpA (G60): igualdade / menor / maior */
    memcpy(scratch + 32, "abc", 4);
    memcpy(scratch + 64, "abc", 4);
    args[0] = (uint64_t)(uintptr_t)(scratch + 32);
    args[1] = (uint64_t)(uintptr_t)(scratch + 64);
    uint64_t lcmp = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "lstrcmpA", args, 2, &lcmp) == PR_OK);
    CHECK((int)lcmp == 0);
    memcpy(scratch + 64, "abd", 4);
    CHECK(pr_win32_call(ctx, "kernel32.dll", "lstrcmpA", args, 2, &lcmp) == PR_OK);
    CHECK((int)lcmp < 0);
    memcpy(scratch + 32, "abd", 4);
    memcpy(scratch + 64, "abc", 4);
    CHECK(pr_win32_call(ctx, "kernel32.dll", "lstrcmpA", args, 2, &lcmp) == PR_OK);
    CHECK((int)lcmp > 0);

    /* -- console real (WriteFile → captura) -- */
    uint64_t hout = 0;
    args[0] = (uint64_t)(uint64_t)(int64_t)-11; /* STD_OUTPUT_HANDLE */
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetStdHandle", args, 1, &hout) == PR_OK);
    CHECK(hout != 0);
    memcpy(scratch + 64, "ola mundo", 10);
    /* LPDWORD = DWORD (4 bytes) — pares [valor|guarda] em memoria validada */
    uint32_t* dslots = (uint32_t*)(void*)(scratch + 144);
    dslots[0] = 0; dslots[1] = 0xC0FFEE03u;
    args[0] = hout;
    args[1] = (uint64_t)(uintptr_t)(scratch + 64);
    args[2] = 9;
    args[3] = (uint64_t)(uintptr_t)&dslots[0];
    uint64_t ok = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "WriteFile", args, 5, &ok) == PR_OK);
    CHECK_EQ_U32(ok, 1);
    CHECK_EQ_U32(dslots[0], 9);
    CHECK_EQ_U32(dslots[1], 0xC0FFEE03u);   /* sem overrun */
    char console[64];
    CHECK_EQ_U32(pr_win32_stdout_read(ctx, console, sizeof(console)), 9);
    CHECK_STR(console, "ola mundo");

    /* ReadFile em stdin = EOF real (via handle resolvido) */
    /* LPDWORD = DWORD (4 bytes) — pares [valor|guarda] em memoria validada */
    dslots[2] = 0xdead; dslots[3] = 0xC0FFEE04u;
    uint64_t hin = 0;
    args[0] = (uint64_t)(int64_t)-10; /* STD_INPUT_HANDLE */
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetStdHandle", args, 1, &hin) == PR_OK);
    CHECK(hin != 0);
    args[0] = hin;
    args[2] = 8;
    args[3] = (uint64_t)(uintptr_t)&dslots[2];
    CHECK(pr_win32_call(ctx, "kernel32.dll", "ReadFile", args, 4, &ok) == PR_OK);
    CHECK_EQ_U32(ok, 1);
    CHECK_EQ_U32(dslots[2], 0);
    CHECK_EQ_U32(dslots[3], 0xC0FFEE04u);   /* sem overrun */

    /* -- last error real -- */
    args[0] = 1234;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "SetLastError", args, 1, NULL) == PR_OK);
    uint64_t err = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetLastError", NULL, 0, &err) == PR_OK);
    CHECK_EQ_U32(err, 1234);
    CHECK_EQ_U32(pr_win32_last_error(ctx), 1234);

    /* -- módulos e GetProcAddress -- */
    args[0] = (uint64_t)(uintptr_t) "KERNEL32.dll";
    /* a string acima é literal do host; para ser válida precisa estar em memória
       rastreada — copia p/ scratch */
    memcpy(scratch + 64, "KERNEL32.dll", 13);
    args[0] = (uint64_t)(uintptr_t)(scratch + 64);
    uint64_t hmod = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetModuleHandleA", args, 1, &hmod) == PR_OK);
    CHECK(hmod != 0);
    memcpy(scratch + 96, "GetTickCount64", 15);
    args[0] = hmod;
    args[1] = (uint64_t)(uintptr_t)(scratch + 96);
    uint64_t token = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetProcAddress", args, 2, &token) == PR_OK);
    CHECK(token != 0); /* token de ligação real (entrada do catálogo) */
    memcpy(scratch + 96, "NaoExisteEstaAPI", 17);
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetProcAddress", args, 2, &token) == PR_OK);
    CHECK_EQ_U32(token, 0);
    CHECK_EQ_U32(pr_win32_last_error(ctx), 127); /* ERROR_PROC_NOT_FOUND */

    /* GetCommandLineA → ponteiro validado */
    uint64_t cmd = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetCommandLineA", NULL, 0, &cmd) == PR_OK);
    CHECK(cmd != 0 && pr_win32_ptr(ctx, cmd, 8) != NULL);

    /* GetVersion: versão declarada do ambiente (5.0 build 2195) */
    uint64_t ver = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetVersion", NULL, 0, &ver) == PR_OK);
    CHECK_EQ_U32(ver, (2195u << 16) | 5u);

    /* PID real */
    uint64_t pid = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetCurrentProcessId", NULL, 0, &pid) == PR_OK);
    CHECK_EQ_U32(pid, (uint64_t)getpid());

    /* -- APIs não implementadas: log + falha honesta -- */
    uint64_t ret = 0xDEAD;
    size_t before_unsup = pr_win32_calls_unsupported(ctx);
    CHECK(pr_win32_call(ctx, "ws2_32.dll", "socket", NULL, 0, &ret) == PR_ERR_UNSUPPORTED);
    CHECK_EQ_U32(ret, 0); /* não finge retorno */
    CHECK_EQ_U32(pr_win32_calls_unsupported(ctx), before_unsup + 1);
    CHECK(pr_win32_call(ctx, "gdi32.dll", "CreateFontA", NULL, 0, &ret) == PR_ERR_UNSUPPORTED);
    /* PeekMessageA foi implementada (GRUPO 6); GetSystemMetrics implementada em G76
     * (métricas de tela mínimas). */
    {
        uint64_t sm_args[1] = { 0 }; /* SM_CXSCREEN */
        CHECK(pr_win32_call(ctx, "user32.dll", "GetSystemMetrics", sm_args, 1, &ret) == PR_OK);
        CHECK(ret > 0 && ret <= 4096);
    }

    /* -- MessageBoxA: implementada DEGRADADA (sem UI) — log WARN + IDOK -- */
    memcpy(scratch + 160, "tem certeza?", 13);
    memcpy(scratch + 192, "titulo", 7);
    {
        uint64_t mb[4] = { 0, (uint64_t)(uintptr_t)(scratch + 160),
                           (uint64_t)(uintptr_t)(scratch + 192), 0 };
        CHECK(pr_win32_call(ctx, "user32.dll", "MessageBoxA", mb, 4, &ret) == PR_OK);
    }
    CHECK_EQ_U32(ret, 1); /* IDOK — documentado; NÃO finge janela */

    /* desconhecida: PR_ERR_RANGE + log de erro */
    CHECK(pr_win32_call(ctx, "foo32.dll", "Bar", NULL, 0, &ret) == PR_ERR_RANGE);

    /* o log registrou MessageBoxA degradada + chamada desconhecida */
    pr_log_entry entries[32];
    size_t n = pr_log_read(log, entries, 32, 0);
    int saw_msgbox = 0, saw_unknown = 0;
    for (size_t i = 0; i < n; i++) {
        if (strstr(entries[i].msg, "MessageBoxA") && strstr(entries[i].msg, "sem UI") &&
            entries[i].level == PR_LOG_WARN)
            saw_msgbox = 1;
        if (strstr(entries[i].msg, "desconhecida") && entries[i].level == PR_LOG_ERROR)
            saw_unknown = 1;
    }
    CHECK(saw_msgbox);
    CHECK(saw_unknown);

    /* -- ExitProcess: encerramento real sinalizado -- */
    CHECK_EQ_U32(pr_win32_halted(ctx), 0);
    args[0] = 42;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "ExitProcess", args, 1, NULL) == PR_OK);
    CHECK_EQ_U32(pr_win32_halted(ctx), 1);
    CHECK_EQ_U32(pr_win32_exit_code(ctx), 42);

    /* -- catálogo: módulos e cobertura honesta -- */
    const pr_win32_module_info* mods = NULL;
    size_t nm = pr_win32_modules(&mods);
    CHECK_EQ_U32(nm, 7); /* kernel32/user32/advapi32/ws2_32/gdi32/ole32/shell32 */
    int saw_kernel = 0, saw_user = 0, saw_ws = 0, saw_gdi = 0,
        saw_ole = 0, saw_shell = 0, saw_adv = 0;
    for (size_t i = 0; i < nm; i++) {
        if (!strcmp(mods[i].name, "kernel32.dll")) {
            saw_kernel = 1;
            CHECK(mods[i].implemented >= 20);
            CHECK(mods[i].cataloged > mods[i].implemented); /* há catalogadas sem impl. */
        }
        if (!strcmp(mods[i].name, "user32.dll")) {
            saw_user = 1;
            /* G6: 20 (janelas/mensagens/GDI) + G7: 12 (PostMessage, aliases W,
             * Set/GetFocus, Set/KillTimer, Get(Async)KeyState) + G76: GetSystemMetrics = 33 reais. */
            CHECK_EQ_U32(mods[i].implemented, 33);
            CHECK(mods[i].cataloged >= 5);
        }
        if (!strcmp(mods[i].name, "ws2_32.dll")) saw_ws = 1;
        if (!strcmp(mods[i].name, "gdi32.dll")) saw_gdi = 1;
        if (!strcmp(mods[i].name, "ole32.dll")) saw_ole = 1;
        if (!strcmp(mods[i].name, "shell32.dll")) saw_shell = 1;
        if (!strcmp(mods[i].name, "advapi32.dll")) saw_adv = 1;
    }
    CHECK(saw_kernel && saw_user && saw_ws && saw_gdi && saw_ole && saw_shell && saw_adv);

    /* lookup direto */
    CHECK(pr_win32_lookup("kernel32", "gettickcount64") != NULL); /* sem caixa/.dll */
    CHECK(pr_win32_lookup("kernel32.dll", "HeapAlloc") != NULL);
    CHECK(pr_win32_lookup("user32.dll", "MessageBoxA")->status == PR_WIN32_IMPLEMENTED);
    CHECK(strstr(pr_win32_lookup("user32.dll", "MessageBoxA")->note, "sem UI") != NULL);
    CHECK(pr_win32_lookup("user32.dll", "PeekMessageA")->status == PR_WIN32_IMPLEMENTED);
    CHECK(pr_win32_lookup("user32.dll", "GetSystemMetrics")->status == PR_WIN32_IMPLEMENTED);
    CHECK(pr_win32_lookup("user32.dll", "Nada") == NULL);

    pr_win32_destroy(ctx);
    pr_log_destroy(log);
}
