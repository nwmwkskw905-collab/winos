/* test_vfs.c — FASE 7: filesystem virtual Windows→sandbox.
 * Raiz ÚNICA permitida; "..", drives e caminhos fora da raiz são NEGADOS com
 * log (nunca acessa o filesystem do iOS). Sem raiz: negação honesta. */
#include "pt_util.h"

#include "portico/pr_win32.h"
#include "portico/pr_log.h"
#include "portico/pr_peproc.h"
#include "portico/pr_winhello.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define INVALID_H 0xFFFFFFFFFFFFFFFFull

void test_vfs(void) {
    pr_log* log = pr_log_create(128);
    pr_win32_ctx* ctx = pr_win32_create(log);
    CHECK(ctx != NULL);
    size_t ssz = 0;
    uint8_t* scratch = pr_win32_scratch(ctx, &ssz);
    CHECK(scratch != NULL && ssz >= 216);
    char* fname = (char*)(void*)scratch;          /* 0..127 */
    uint32_t* slots = (uint32_t*)(void*)(scratch + 128);  /* DWORD (ABI): pares [valor|guarda] */
    uint8_t* buf = scratch + 160;                 /* 160..191 */
    uint64_t args[8];
    uint64_t r = 0, err = 0;

    /* -- 1) sem raiz: CreateFile negado honesto (PATH_NOT_FOUND) -- */
    strcpy(fname, "C:\\dados\\teste.txt");
    args[0] = (uint64_t)(uintptr_t)fname;
    args[1] = 0xC0000000ull; args[2] = 0; args[3] = 0; args[4] = 2;
    args[5] = 0; args[6] = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "CreateFileA", args, 7, &r) == PR_OK);
    CHECK(r == INVALID_H);
    pr_win32_call(ctx, "kernel32.dll", "GetLastError", NULL, 0, &err);
    CHECK_EQ_U32((uint32_t)err, 3);   /* ERROR_PATH_NOT_FOUND */

    /* -- 2) raiz do sandbox -- */
    mkdir("build", 0755);
    mkdir("build/vfs_root", 0755);
    CHECK(pr_win32_set_fs_root(ctx, "build/vfs_root") == PR_OK);

    /* CREATE_ALWAYS com drive + barras invertidas (normaliza para a raiz) */
    strcpy(fname, "C:\\arquivo.txt");
    args[0] = (uint64_t)(uintptr_t)fname;
    args[1] = 0xC0000000ull; args[2] = 0; args[3] = 0; args[4] = 2;
    args[5] = 0; args[6] = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "CreateFileA", args, 7, &r) == PR_OK);
    CHECK(r != INVALID_H);
    uint64_t h = r;

    /* WriteFile 16 bytes */
    memcpy(buf, "Portico VFS 2026", 16);
    slots[0] = 0; slots[1] = 0xC0FFEE01u;   /* guarda adjacente ao LPDWORD */
    args[0] = h; args[1] = (uint64_t)(uintptr_t)buf;
    args[2] = 16; args[3] = (uint64_t)(uintptr_t)&slots[0];
    r = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "WriteFile", args, 4, &r) == PR_OK);
    CHECK_EQ_U32((uint32_t)r, 1);
    CHECK_EQ_U32(slots[0], 16);
    CHECK_EQ_U32(slots[1], 0xC0FFEE01u);   /* sem overrun */

    /* GetFileSize == 16 */
    args[0] = h; args[1] = 0;
    r = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetFileSize", args, 2, &r) == PR_OK);
    CHECK_EQ_U32((uint32_t)r, 16);

    /* SetFilePointer BEGIN 0 e releitura fiel */
    args[0] = h; args[1] = 0; args[2] = 0; args[3] = 0;
    r = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "SetFilePointer", args, 4, &r) == PR_OK);
    CHECK_EQ_U32((uint32_t)r, 0);
    slots[2] = 0; slots[3] = 0xC0FFEE02u;   /* guarda adjacente ao LPDWORD */
    memset(buf, 0, 32);
    args[0] = h; args[1] = (uint64_t)(uintptr_t)buf;
    args[2] = 16; args[3] = (uint64_t)(uintptr_t)&slots[2];
    r = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "ReadFile", args, 4, &r) == PR_OK);
    CHECK_EQ_U32((uint32_t)r, 1);
    CHECK_EQ_U32(slots[2], 16);
    CHECK_EQ_U32(slots[3], 0xC0FFEE02u);   /* sem overrun */
    CHECK(memcmp(buf, "Portico VFS 2026", 16) == 0);

    /* SetFilePointer END 0 → posição 16; anexa "!"; tamanho 17 */
    args[0] = h; args[1] = 0; args[2] = 0; args[3] = 2;
    r = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "SetFilePointer", args, 4, &r) == PR_OK);
    CHECK_EQ_U32((uint32_t)r, 16);
    buf[0] = '!';
    slots[0] = 0; slots[1] = 0xC0FFEE01u;
    args[0] = h; args[1] = (uint64_t)(uintptr_t)buf; args[2] = 1; args[3] = (uint64_t)(uintptr_t)&slots[0];
    r = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "WriteFile", args, 4, &r) == PR_OK);
    CHECK_EQ_U32(slots[0], 1);
    CHECK_EQ_U32(slots[1], 0xC0FFEE01u);
    args[0] = h; args[1] = 0;
    r = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetFileSize", args, 2, &r) == PR_OK);
    CHECK_EQ_U32((uint32_t)r, 17);
    /* lpFileSizeHigh = LPDWORD (4 bytes) — nova cobertura + guarda adjacente */
    slots[4] = 0xFFFFFFFFu; slots[5] = 0xC0FFEE03u;
    args[0] = h; args[1] = (uint64_t)(uintptr_t)&slots[4];
    r = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "GetFileSize", args, 2, &r) == PR_OK);
    CHECK_EQ_U32((uint32_t)r, 17);
    CHECK_EQ_U32(slots[4], 0);
    CHECK_EQ_U32(slots[5], 0xC0FFEE03u);   /* sem overrun */

    /* CloseHandle; fechar 2x → INVALID_HANDLE (6) */
    args[0] = h;
    r = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "CloseHandle", args, 1, &r) == PR_OK);
    CHECK_EQ_U32((uint32_t)r, 1);
    r = 1;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "CloseHandle", args, 1, &r) == PR_OK);
    CHECK_EQ_U32((uint32_t)r, 0);
    err = 0;
    pr_win32_call(ctx, "kernel32.dll", "GetLastError", NULL, 0, &err);
    CHECK_EQ_U32((uint32_t)err, 6);

    /* -- 3) fuga de sandbox: ".." NEGADO com log -- */
    strcpy(fname, "..\\fuga.txt");
    args[0] = (uint64_t)(uintptr_t)fname;
    args[1] = 0xC0000000ull; args[2] = 0; args[3] = 0; args[4] = 2;
    args[5] = 0; args[6] = 0;
    r = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "CreateFileA", args, 7, &r) == PR_OK);
    CHECK(r == INVALID_H);
    err = 0;
    pr_win32_call(ctx, "kernel32.dll", "GetLastError", NULL, 0, &err);
    CHECK_EQ_U32((uint32_t)err, 5);   /* ERROR_ACCESS_DENIED */
    {
        pr_log_entry es[128];
        size_t n = pr_log_read(log, es, 128, 0);
        int saw = 0;
        for (size_t i = 0; i < n; i++)
            if (strstr(es[i].msg, "NEGADO")) saw = 1;
        CHECK(saw);
    }

    /* -- 4) CREATE_NEW em arquivo existente → ERROR_FILE_EXISTS (80) -- */
    strcpy(fname, "arquivo.txt");
    args[0] = (uint64_t)(uintptr_t)fname;
    args[1] = 0xC0000000ull; args[2] = 0; args[3] = 0; args[4] = 1;
    args[5] = 0; args[6] = 0;
    r = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "CreateFileA", args, 7, &r) == PR_OK);
    CHECK(r == INVALID_H);
    err = 0;
    pr_win32_call(ctx, "kernel32.dll", "GetLastError", NULL, 0, &err);
    CHECK_EQ_U32((uint32_t)err, 80);

    /* -- 5) OPEN_EXISTING ausente → ERROR_FILE_NOT_FOUND (2) -- */
    strcpy(fname, "nao_existe.txt");
    args[0] = (uint64_t)(uintptr_t)fname;
    args[1] = 0x80000000ull; args[2] = 0; args[3] = 0; args[4] = 3;
    args[5] = 0; args[6] = 0;
    r = 0;
    CHECK(pr_win32_call(ctx, "kernel32.dll", "CreateFileA", args, 7, &r) == PR_OK);
    CHECK(r == INVALID_H);
    err = 0;
    pr_win32_call(ctx, "kernel32.dll", "GetLastError", NULL, 0, &err);
    CHECK_EQ_U32((uint32_t)err, 2);

    /* -- 6) CreateFileW (subset ASCII) escreve/le -- */
    /* "w.txt" em UTF-16 no scratch */
    {
        uint16_t* wn = (uint16_t*)(void*)(scratch + 200);  /* 200..211 */
        wn[0] = 'w'; wn[1] = '.'; wn[2] = 't'; wn[3] = 'x'; wn[4] = 't'; wn[5] = 0;
        args[0] = (uint64_t)(uintptr_t)wn;
        args[1] = 0xC0000000ull; args[2] = 0; args[3] = 0; args[4] = 2;
        args[5] = 0; args[6] = 0;
        r = 0;
        CHECK(pr_win32_call(ctx, "kernel32.dll", "CreateFileW", args, 7, &r) == PR_OK);
        CHECK(r != INVALID_H);
        args[0] = r;
        uint64_t cl = 0;
        CHECK(pr_win32_call(ctx, "kernel32.dll", "CloseHandle", args, 1, &cl) == PR_OK);
        CHECK_EQ_U32((uint32_t)cl, 1);
    }

    pr_win32_destroy(ctx);
    pr_log_destroy(log);

    /* -- 7) e2e PE13: CreateFileA/WriteFile/CloseHandle via processo real,
     *    arquivo criado DENTRO da raiz do sandbox (path Windows→Portico→
     *    sandbox) -- */
    {
        pr_log* log2 = pr_log_create(256);
        uint8_t* data = NULL;
        size_t len = 0;
        CHECK(pr_winhello_build(13, (void**)&data, &len) == PR_OK);
        pr_peproc* p = NULL;
        CHECK(pr_peproc_create(data, len, log2, &p) == PR_OK);
        free(data);
        mkdir("build/vfs_root2", 0755);
        CHECK(pr_peproc_set_fs_root(p, "build/vfs_root2") == PR_OK);
        CHECK(pr_peproc_prepare(p) == PR_OK);
        uint64_t total = 0;
        pr_status st = PR_OK;
        for (int i = 0; i < 300 && pr_peproc_state(p) == PR_PEPROC_STOP_NONE; i++) {
            uint64_t ex = 0;
            st = pr_peproc_step(p, 1000, &ex);
            total += ex;
            if (st != PR_OK) break;
        }
        CHECK(st == PR_OK);
        CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_EXIT);
        CHECK_EQ_U32(pr_peproc_exit_code(p), 0);
        CHECK(total > 15);
        FILE* chk = fopen("build/vfs_root2/nota.txt", "rb");
        CHECK(chk != NULL);
        if (chk) {
            char b[32];
            memset(b, 0, sizeof(b));
            size_t nr = fread(b, 1, 31, chk);
            fclose(chk);
            CHECK_EQ_U32((uint32_t)nr, 15);
            CHECK(memcmp(b, "Portico VFS E2E", 15) == 0);
        }
        pr_peproc_destroy(p);
        pr_log_destroy(log2);
    }
}
