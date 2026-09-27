/* Testes da cadeia REAL de execução Windows mínima:
 * PE → CPU → memória → imports → Win32 → processo → saída. */
#include "pt_util.h"

#include "portico/pr_peproc.h"
#include "portico/pr_winhello.h"
#include "portico/pr_pe.h"
#include "portico/pr_win32.h"
#include "portico/pr_log.h"

#include <stdlib.h>
#include <string.h>

void test_peproc(void) {
    pr_log* log = pr_log_create(256);
    CHECK(log != NULL);

    /* ============================================================
     * 1) Programa mínimo: GetTickCount64 + ExitProcess(42)
     * ============================================================ */
    void* data = NULL;
    size_t len = 0;
    CHECK(pr_winhello_build(0, &data, &len) == PR_OK);
    CHECK(data != NULL && len == 0x600);

    pr_peproc* p = NULL;
    CHECK(pr_peproc_create(data, len, log, &p) == PR_OK);
    CHECK(p != NULL);
    CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_NONE);

    /* entry point localizado */
    pr_cpu* cpu = pr_peproc_cpu(p);
    CHECK(cpu != NULL);
    CHECK_EQ_U32(pr_cpu_eip(cpu), 0x00402000);

    /* imports resolvidos: IAT aponta para os thunks */
    CHECK(pr_peproc_prepare(p) == PR_OK);
    {
        uint32_t iat0 = 0, iat1 = 0;
        CHECK_EQ_U32(pr_vm_read(pr_peproc_vm(p), 0x00401090, &iat0, 4), PR_OK);
        CHECK_EQ_U32(pr_vm_read(pr_peproc_vm(p), 0x00401094, &iat1, 4), PR_OK);
        CHECK(iat0 >= 0x00E00000 && iat0 < 0x00E01000);
        CHECK(iat1 >= 0x00E00000 && iat1 < 0x00E01000);
        CHECK(iat0 != iat1);
    }

    /* executa: dois calls stdcall + ExitProcess(42) */
    uint64_t executed = 0;
    CHECK(pr_peproc_step(p, 100000, &executed) == PR_OK);
    CHECK(executed > 0);
    CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_EXIT);
    CHECK_EQ_U32(pr_peproc_exited(p), 1);
    CHECK_EQ_U32(pr_peproc_exit_code(p), 42);

    /* diagnóstico de saída com código */
    const char* diag = pr_peproc_diagnostic(p);
    CHECK(strstr(diag, "PROCESS EXIT") != NULL);
    CHECK(strstr(diag, "ExitProcess") != NULL);
    CHECK(strstr(diag, "42") != NULL);

    /* as duas APIs foram chamadas de verdade */
    CHECK_EQ_U32(pr_win32_calls_implemented(pr_peproc_win32(p)), 2);
    /* a pilha do convidado foi usada (sentinela + chamadas) e está na região */
    CHECK(pr_cpu_reg(cpu, PR_REG_ESP) < 0x00F00000);
    CHECK(pr_cpu_reg(cpu, PR_REG_ESP) >= 0x00F00000 - 0x10000);

    pr_peproc_destroy(p);
    free(data);

    /* ============================================================
     * 2) Programa GDI: PatBlt desenha retângulo real na superfície
     * ============================================================ */
    CHECK(pr_winhello_build(1, &data, &len) == PR_OK);
    p = NULL;
    CHECK(pr_peproc_create(data, len, log, &p) == PR_OK);
    CHECK(pr_peproc_prepare(p) == PR_OK);
    executed = 0;
    CHECK(pr_peproc_step(p, 200000, &executed) == PR_OK);
    CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_EXIT);
    CHECK_EQ_U32(pr_peproc_exit_code(p), 0);

    pr_surf* surf = pr_peproc_surface(p);
    CHECK(surf != NULL);
    if (surf) {
        const uint32_t* px = pr_surf_pixels(surf);
        CHECK(px != NULL);
        if (px) {
            /* retângulo (8,8,64,48) em vermelho COLORREF 0x000000FF → XRGB 0x00FF0000 */
            uint32_t w = pr_surf_width(surf);
            CHECK_EQ_U32(px[20 * w + 20], 0x00FF0000u);  /* dentro */
            CHECK_EQ_U32(px[0 * w + 0], 0x00000000u);    /* fora */
            CHECK_EQ_U32(px[100 * w + 100], 0x00000000u);
        }
    }

    /* apresentação no stream (→ Metal) */
    pr_gfx_stream* stream = pr_gfx_stream_create(8, 0);
    CHECK(pr_peproc_present(p, stream, 640, 360) == PR_OK);
    {
        uint32_t w = 0, h = 0;
        const uint32_t* s = pr_gfx_surface(stream, &w, &h);
        CHECK(s != NULL);
        CHECK_EQ_U32(w, 320);
        if (s) {
            CHECK_EQ_U32(s[20 * 320 + 20], 0x00FF0000u);
        }
    }
    pr_gfx_stream_destroy(stream);
    pr_peproc_destroy(p);
    free(data);

    /* ============================================================
     * 3) API não suportada no load: diagnóstico EXECUTION STOPPED
     * ============================================================ */
    static uint8_t pe_badapi[0x600];
    {
        /* reusa o builder2 de test_pe_loader via include aqui? em vez disso,
         * constrói um PE com MessageBoxA usando pr_pe indireto: copia o hello
         * e troca o nome da importação p/ algo não implementado. */
        CHECK(pr_winhello_build(0, &data, &len) == PR_OK);
        memcpy(pe_badapi, data, len);
        free(data);
        /* "GetTickCount64" (14 chars) → espécime FORA de escopo (14 chars).
         * FASE 6 implementou LoadLibraryA; o espécime precisa continuar sendo
         * uma API genuinamente não implementada (intenção do teste preservada:
         * diagnosticar Unsupported Win32 API). "CreateHardLinkA" não está no
         * catálogo nem no escopo (FASE 7 = só CreateFile/Read/Write/Close/
         * GetFileSize/SetFilePointer). */
        memcpy(pe_badapi + 0x262, "CreateHardLinkA", 15);
    }
    p = NULL;
    CHECK(pr_peproc_create(pe_badapi, sizeof(pe_badapi), log, &p) == PR_OK);
    pr_status st = pr_peproc_prepare(p);
    CHECK(st == PR_ERR_UNSUPPORTED);
    CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_MISSING_API);
    diag = pr_peproc_diagnostic(p);
    CHECK(strstr(diag, "EXECUTION STOPPED") != NULL);
    CHECK(strstr(diag, "Reason:") != NULL);
    CHECK(strstr(diag, "Unsupported Win32 API") != NULL);
    CHECK(strstr(diag, "Module:\nKERNEL32.dll") != NULL);
    CHECK(strstr(diag, "Function:\nCreateHardLinkA") != NULL);
    pr_peproc_destroy(p);

    /* ============================================================
     * 4) DLL ausente: diagnóstico claro
     * ============================================================ */
    uint8_t pe_baddll[0x600];
    CHECK(pr_winhello_build(0, &data, &len) == PR_OK);
    memcpy(pe_baddll, data, len);
    free(data);
    memcpy(pe_baddll + 0x240, "FOO32.dll", 10); /* nome da DLL importada */
    p = NULL;
    CHECK(pr_peproc_create(pe_baddll, sizeof(pe_baddll), log, &p) == PR_OK);
    CHECK(pr_peproc_prepare(p) == PR_ERR_UNSUPPORTED);
    CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_MISSING_DLL);
    diag = pr_peproc_diagnostic(p);
    CHECK(strstr(diag, "Missing DLL") != NULL);
    CHECK(strstr(diag, "FOO32.dll") != NULL);
    pr_peproc_destroy(p);

    /* ============================================================
     * 5) Acesso inválido de memória: EXECUTION STOPPED com eip/addr
     * ============================================================ */
    /* PE cujo código escreve em endereço não mapeado: mov [0x00900000], eax
     * (C7 05 imm32 imm32) + hlt */
    CHECK(pr_winhello_build(0, &data, &len) == PR_OK);
    {
        uint8_t* c = (uint8_t*)data + 0x400;
        size_t o = 0;
        c[o++] = 0xC7; c[o++] = 0x05;            /* mov [imm32], imm32 */
        uint32_t addr = 0x00900000, imm = 1;
        memcpy(c + o, &addr, 4); o += 4;
        memcpy(c + o, &imm, 4); o += 4;
        c[o++] = 0xF4;
    }
    p = NULL;
    CHECK(pr_peproc_create(data, len, log, &p) == PR_OK);
    CHECK(pr_peproc_prepare(p) == PR_OK);
    st = pr_peproc_step(p, 1000, &executed);
    CHECK(st == PR_ERR_FAULT);
    CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_BAD_MEMORY);
    diag = pr_peproc_diagnostic(p);
    CHECK(strstr(diag, "Invalid memory access") != NULL);
    CHECK(strstr(diag, "0x00900000") != NULL);
    pr_peproc_destroy(p);
    free(data);

    /* ============================================================
     * 6) Executável inválido e arquitetura não suportada
     * ============================================================ */
    p = NULL;
    uint8_t junk[32] = {'M', 'Z', 0};
    CHECK(pr_peproc_create(junk, sizeof(junk), log, &p) != PR_OK);
    if (p) {
        diag = pr_peproc_diagnostic(p);
        CHECK(strstr(diag, "Invalid executable") != NULL);
        pr_peproc_destroy(p);
    }

    /* PE64 é recusado com diagnóstico (sem fingir execução) */
    /* constrói um PE32+ mínimo: sobrescreve a magic do hello */
    CHECK(pr_winhello_build(0, &data, &len) == PR_OK);
    {
        uint8_t* d = (uint8_t*)data;
        uint16_t magic = 0x20B;
        memcpy(d + 0x80 + 24, &magic, 2);
    }
    p = NULL;
    CHECK(pr_peproc_create(data, len, log, &p) == PR_ERR_UNSUPPORTED);
    if (p) {
        CHECK(strstr(pr_peproc_diagnostic(p), "Unsupported executable") != NULL);
        pr_peproc_destroy(p);
    }
    free(data);

    /* ============================================================
     * 7) Instrução fora do subconjunto: diagnóstico técnico
     * ============================================================ */
    CHECK(pr_winhello_build(0, &data, &len) == PR_OK);
    {
        uint8_t* c = (uint8_t*)data + 0x400;
        c[0] = 0x0F; c[1] = 0x0B; /* UD2 (inválido) */
        c[2] = 0xF4;
    }
    p = NULL;
    CHECK(pr_peproc_create(data, len, log, &p) == PR_OK);
    CHECK(pr_peproc_prepare(p) == PR_OK);
    st = pr_peproc_step(p, 100, &executed);
    CHECK(st == PR_ERR_FAULT);
    CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_BAD_INSN);
    CHECK(strstr(pr_peproc_diagnostic(p), "opcode") != NULL
          || strstr(pr_peproc_diagnostic(p), "Technical") != NULL);
    pr_peproc_destroy(p);
    free(data);

    pr_log_destroy(log);
}
