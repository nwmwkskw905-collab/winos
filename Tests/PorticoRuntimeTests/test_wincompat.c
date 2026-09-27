/* Compatibilidade Windows incremental: PE32+/x64 (loader), relocations
 * reais e importação por ordinal — com diagnóstico honesto quando algo
 * não é suportado (nunca sucesso falso). */
#include "pt_util.h"

#include "portico/pr_peproc.h"
#include "portico/pr_winhello.h"
#include "portico/pr_pe.h"
#include "portico/pr_win32.h"
#include "portico/pr_log.h"

#include <stdlib.h>
#include <string.h>

static void w32_local(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

void test_wincompat(void) {
    pr_log* log = pr_log_create(256);
    CHECK(log != NULL);

    /* ============================================================
     * 1) Parser de relocations (Basereloc): contagem/enumeração/validação
     * ============================================================ */
    void* data = NULL;
    size_t len = 0;
    CHECK(pr_winhello_build(4, &data, &len) == PR_OK);   /* PE32 com relocs */
    CHECK(pr_pe_reloc_count(data, len) == 4);            /* 2 úteis + 2 pad */
    {
        pr_pe_reloc r0, r2;
        CHECK_EQ_U32(pr_pe_reloc_at(data, len, 0, &r0), PR_OK);
        CHECK_EQ_U32(r0.rva, 0x10B0);
        CHECK_EQ_U32(r0.type, 3);                        /* HIGHLOW */
        /* [0]=slot 0x10B0, [1]=pad ABSOLUTE, [2]=disp 0x2007, [3]=pad */
        CHECK_EQ_U32(pr_pe_reloc_at(data, len, 2, &r2), PR_OK);
        CHECK_EQ_U32(r2.rva, 0x2007);
        CHECK_EQ_U32(r2.type, 3);
        pr_pe_reloc r4;
        CHECK(pr_pe_reloc_at(data, len, 4, &r4) != PR_OK);
        char why[96];
        CHECK_EQ_U32(pr_pe_reloc_validate(data, len, why, sizeof(why)), PR_OK);
    }
    /* tipo desconhecido é rejeitado com motivo */
    {
        uint8_t* bad = (uint8_t*)malloc(len);
        CHECK(bad != NULL);
        if (!bad) return;   /* OOM real: CHECK acima já falhou */
        memcpy(bad, data, len);
        /* entrada 0x30B0 vira 0x50B0 (tipo 5) — em RVA 0x3008 (file 0x608) */
        bad[0x608] = 0xB0; bad[0x609] = 0x50;
        char why[96] = {0};
        CHECK(pr_pe_reloc_validate(bad, len, why, sizeof(why)) == PR_ERR_FORMAT);
        CHECK(strstr(why, "tipo") != NULL);
        free(bad);
    }

    /* ============================================================
     * 2) Relocations E2E: base preferida fora do espaço → relocaliza e
     *    o programa real roda (ExitProcess(42) pelo caminho ajustado)
     * ============================================================ */
    {
        pr_peproc* p = NULL;
        CHECK_EQ_U32(pr_peproc_create(data, len, log, &p), PR_OK);
        pr_vm* vm = pr_peproc_vm(p);
        uint32_t load_base = 0;
        /* base real é diferente da preferida (0x02000000) */
        {
            /* recupera via eip − entry */
            load_base = pr_cpu_eip(pr_peproc_cpu(p)) - 0x2000;
        }
        CHECK(load_base != 0x02000000);
        CHECK(load_base + 0x4000 <= (16u << 20));

        /* slot de dados relocalizado: 0x02001234 → load_base + 0x1234 */
        uint32_t slot = 0;
        CHECK_EQ_U32(pr_vm_read(vm, load_base + 0x10B0, &slot, 4), PR_OK);
        CHECK_EQ_U32(slot, load_base + 0x1234);

        /* o disp do call [abs] foi ajustado para o IAT real */
        uint32_t disp = 0;
        CHECK_EQ_U32(pr_vm_read(vm, load_base + 0x2007, &disp, 4), PR_OK);
        CHECK_EQ_U32(disp, load_base + 0x1060);

        CHECK_EQ_U32(pr_peproc_prepare(p), PR_OK);
        uint64_t executed = 0;
        CHECK_EQ_U32(pr_peproc_step(p, 100000, &executed), PR_OK);
        CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_EXIT);
        CHECK_EQ_U32(pr_peproc_exit_code(p), 42);
        pr_peproc_destroy(p);
    }
    free(data);

    /* ============================================================
     * 3) PE32+ / x64: carga completa (imports 8 bytes + relocs DIR64);
     *    execução REAL pelo interpretador x64 mínimo (subconjunto):
     *    `xor rax,rax; ret` → retorno do entry → PROCESS EXIT code 0
     * ============================================================ */
    CHECK(pr_winhello_build(2, &data, &len) == PR_OK);   /* PE32+ com imports */
    {
        pr_peproc* p = NULL;
        CHECK_EQ_U32(pr_peproc_create(data, len, log, &p), PR_OK);
        uint32_t load_base = pr_cpu_eip(pr_peproc_cpu(p)) - 0x2000;
        CHECK_EQ_U32(load_base, 0x00400000);             /* base preferida */

        /* imports PE32+: IAT de 8 bytes por slot */
        CHECK_EQ_U32(pr_peproc_prepare(p), PR_OK);
        uint32_t lo = 0, hi = 0;
        pr_vm* vm = pr_peproc_vm(p);
        CHECK_EQ_U32(pr_vm_read(vm, load_base + 0x1080, &lo, 4), PR_OK);
        CHECK_EQ_U32(pr_vm_read(vm, load_base + 0x1084, &hi, 4), PR_OK);
        CHECK_EQ_U32(hi, 0);
        CHECK(lo >= 0x00E00000 && lo < 0x00E01000);
        uint32_t lo2 = 0;
        CHECK_EQ_U32(pr_vm_read(vm, load_base + 0x1088, &lo2, 4), PR_OK);
        CHECK(lo2 != lo);

        /* execução x64 real: entry `xor rax,rax; ret` → exit 0 (sentinela) */
        uint64_t executed = 0;
        CHECK_EQ_U32(pr_peproc_step(p, 1000, &executed), PR_OK);
        CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_EXIT);
        CHECK_EQ_U32(pr_peproc_exit_code(p), 0);
        CHECK(executed >= 2);
        const char* diag = pr_peproc_diagnostic(p);
        CHECK(strstr(diag, "PROCESS EXIT") != NULL);
        CHECK(strstr(diag, "Reason:") != NULL);
        CHECK(strstr(diag, "Module:") != NULL);
        CHECK(strstr(diag, "Function:") != NULL);
        CHECK(strstr(diag, "Address:") != NULL);
        CHECK(strstr(diag, "Architecture:") != NULL);
        CHECK(strstr(diag, "x86-64") != NULL);
        pr_peproc_destroy(p);
    }
    free(data);

    /* PE32+ com relocs DIR64 forçadas (base 0x140000000) */
    CHECK(pr_winhello_build(5, &data, &len) == PR_OK);
    {
        pr_peproc* p = NULL;
        CHECK_EQ_U32(pr_peproc_create(data, len, log, &p), PR_OK);
        uint32_t load_base = pr_cpu_eip(pr_peproc_cpu(p)) - 0x1000;
        CHECK(load_base < 0x01000000);                   /* relocalizado */
        /* slot DIR64: 0x140001234 → (u64)load_base + 0x1234 */
        uint64_t slot = 0;
        CHECK_EQ_U32(pr_vm_read(pr_peproc_vm(p), load_base + 0x10B0, &slot, 8),
                     PR_OK);
        CHECK(slot == (uint64_t)load_base + 0x1234);
        pr_peproc_destroy(p);
    }
    free(data);

    /* sem relocations e base indisponível → recusa com motivo */
    CHECK(pr_winhello_build(2, &data, &len) == PR_OK);
    {
        /* patch: image base vira 0x00E00000 (colide com a página de stubs) */
        uint8_t* d = (uint8_t*)data;
        w32_local(d + 0x98 + 28, 0x00E00000);
        pr_peproc* p = NULL;
        CHECK(pr_peproc_create(data, len, log, &p) != PR_OK);
        const char* diag = pr_peproc_diagnostic(p);
        CHECK(strstr(diag, "relocations") != NULL);
        pr_peproc_destroy(p);
    }
    free(data);

    /* ============================================================
     * 4) Importação POR ORDINAL (ws2_32!#9 htons — ordinal público do
     *    winsock.def) + ExitProcess por nome. Exit code = 0x0102.
     * ============================================================ */
    CHECK(pr_winhello_build(3, &data, &len) == PR_OK);
    {
        /* resolução direta no catálogo */
        const pr_win32_export* e = pr_win32_lookup_ordinal("ws2_32.dll", 9);
        CHECK(e != NULL && strcmp(e->name, "htons") == 0);
        CHECK(pr_win32_lookup_ordinal("ws2_32.dll", 8) != NULL);  /* htonl */
        CHECK(pr_win32_lookup_ordinal("ws2_32.dll", 14) != NULL); /* ntohl */
        CHECK(pr_win32_lookup_ordinal("ws2_32.dll", 15) != NULL); /* ntohs */
        CHECK(pr_win32_lookup_ordinal("ws2_32.dll", 999) == NULL);
        CHECK(pr_win32_lookup_ordinal("kernel32.dll", 5) == NULL); /* nunca adivinhar */

        pr_peproc* p = NULL;
        CHECK_EQ_U32(pr_peproc_create(data, len, log, &p), PR_OK);
        CHECK_EQ_U32(pr_peproc_prepare(p), PR_OK);
        uint64_t executed = 0;
        CHECK_EQ_U32(pr_peproc_step(p, 100000, &executed), PR_OK);
        CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_EXIT);
        /* htons(0x0201) = 0x0102 */
        CHECK_EQ_U32(pr_peproc_exit_code(p), 0x0102);
        pr_peproc_destroy(p);
    }
    /* ordinal desconhecido → EXECUTION STOPPED honesto (sem sucesso falso) */
    {
        uint8_t* bad = (uint8_t*)malloc(len);
        CHECK(bad != NULL);
        if (!bad) return;   /* OOM real: CHECK acima já falhou */
        memcpy(bad, data, len);
        /* ILT ws2_32 em RVA 0x1030 (file 0x230): ordinal 9 → 999 */
        w32_local(bad + 0x230, 0x800003E7);
        pr_peproc* p = NULL;
        CHECK_EQ_U32(pr_peproc_create(bad, len, log, &p), PR_OK);
        CHECK(pr_peproc_prepare(p) == PR_ERR_UNSUPPORTED);
        CHECK_EQ_U32(pr_peproc_state(p), PR_PEPROC_STOP_MISSING_API);
        const char* diag = pr_peproc_diagnostic(p);
        CHECK(strstr(diag, "ordinal") != NULL);
        CHECK(strstr(diag, "#999") != NULL);
        pr_peproc_destroy(p);
        free(bad);
    }
    free(data);

    /* ============================================================
     * 5) Alignment inválido é rejeitado na carga
     * ============================================================ */
    CHECK(pr_winhello_build(0, &data, &len) == PR_OK);
    {
        uint8_t* bad = (uint8_t*)malloc(len);
        CHECK(bad != NULL);
        if (!bad) return;   /* OOM real: CHECK acima já falhou */
        memcpy(bad, data, len);
        w32_local(bad + 0x98 + 36, 0x0101); /* file alignment não potência de 2 */
        pr_pe_loaded img;
        CHECK(pr_pe_load(bad, len, &img, "x") == PR_ERR_FORMAT);
        free(bad);
    }
    free(data);

    pr_log_destroy(log);
}
