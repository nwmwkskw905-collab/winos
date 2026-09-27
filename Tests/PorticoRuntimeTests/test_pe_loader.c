/* Testes do PE loader: imports por função, exports, carga/mapeamento e
 * diagnóstico. Os testes antigos de parser (test_pe.c) permanecem intactos. */
#include "pt_util.h"
#include "pt_pe_builder.h"
#include "pt_pe_builder2.h"

#include "portico/pr_pe.h"

#include <stdlib.h>

void test_pe_loader(void) {
    static uint8_t buf[0x600];
    size_t len = pt_build_pe_full(buf, sizeof(buf));
    CHECK(len == 0x600);

    /* -- scan básico coerente -- */
    pr_pe_info info;
    CHECK(pr_pe_scan(buf, len, &info) == PR_OK);
    CHECK_EQ_U32(info.section_count, 2);
    CHECK_EQ_U32(info.import_count, 2);
    CHECK_STR(info.arch, "x86");
    CHECK_EQ_U32(info.entry_point_rva, 0x1000);

    /* -- imports por função -- */
    CHECK_EQ_U32(pr_pe_import_func_count(buf, len, 0), 2);
    CHECK_EQ_U32(pr_pe_import_func_count(buf, len, 1), 1);
    CHECK_EQ_U32(pr_pe_import_func_count(buf, len, 9), 0);

    pr_pe_import_func fn;
    CHECK(pr_pe_import_func_at(buf, len, 0, 0, &fn) == PR_OK);
    CHECK_STR(fn.name, "GetTickCount64");
    CHECK_EQ_U32(fn.by_ordinal, 0);
    CHECK_EQ_U32(fn.hint, 7);
    CHECK(pr_pe_import_func_at(buf, len, 0, 1, &fn) == PR_OK);
    CHECK_EQ_U32(fn.by_ordinal, 1);
    CHECK_EQ_U32(fn.ordinal, 5);
    CHECK(pr_pe_import_func_at(buf, len, 1, 0, &fn) == PR_OK);
    CHECK_STR(fn.name, "MessageBoxA");
    CHECK(pr_pe_import_func_at(buf, len, 1, 5, &fn) == PR_ERR_RANGE);

    /* -- exports -- */
    pr_pe_export exps[8];
    size_t nexps = pr_pe_exports(buf, len, exps, 8, NULL);
    CHECK_EQ_U32(nexps, 2);
    CHECK_STR(exps[0].name, "Alpha");
    CHECK_EQ_U32(exps[0].ordinal, 1);
    CHECK_EQ_U32(exps[0].rva, 0x2070);
    CHECK_STR(exps[1].name, "Beta");
    CHECK_EQ_U32(exps[1].ordinal, 2);
    CHECK_EQ_U32(exps[1].rva, 0x2080);

    /* builder antigo (sem exports) → 0 */
    static uint8_t buf2[0x400];
    size_t len2 = pt_build_pe32(buf2, sizeof(buf2), 0);
    CHECK_EQ_U32(pr_pe_exports(buf2, len2, exps, 8, NULL), 0);

    /* -- carga/mapeamento em memória (SEM execução) -- */
    pr_pe_loaded img;
    CHECK(pr_pe_load(buf, len, &img, "game.exe") == PR_OK);
    CHECK(img.image != NULL);
    CHECK_EQ_U32(img.image_size, 0x3000);
    CHECK_EQ_U32(img.entry_rva, 0x1000);
    CHECK_EQ_U32(img.section_count, 2);
    CHECK_STR(img.module_name, "game.exe");
    CHECK_STR(img.arch, "x86");
    CHECK_EQ_U32(img.is_dll, 0);

    /* entry point localizável (aponte para os dados mapeados) */
    void* ep = pr_pe_loaded_entry(&img);
    CHECK(ep == img.image + 0x1000);

    /* seção copiada para o RVA correto */
    char s[32];
    CHECK(pr_pe_loaded_string(&img, 0x1060, s, sizeof(s)) == PR_OK);
    CHECK_STR(s, "KERNEL32.dll");
    /* exports mapeados também */
    CHECK(pr_pe_loaded_string(&img, 0x2090, s, sizeof(s)) == PR_OK);
    CHECK_STR(s, "Alpha");

    /* limites: RVA fora da imagem */
    CHECK(pr_pe_loaded_ptr(&img, 0x2FFF, 4) == NULL);
    CHECK(pr_pe_loaded_ptr(&img, 0x2FFC, 4) != NULL);
    CHECK(pr_pe_loaded_string(&img, 0x100000, s, sizeof(s)) == PR_ERR_RANGE);

    pr_pe_loaded_free(&img);
    pr_pe_loaded_free(&img); /* idempotente */
    CHECK(img.image == NULL);

    /* carga de PE inválido */
    uint8_t junk[16] = {1, 2, 3, 4};
    pr_pe_loaded bad;
    CHECK(pr_pe_load(junk, sizeof(junk), &bad, NULL) == PR_ERR_FORMAT);

    /* -- diagnóstico legível -- */
    char report[4096];
    size_t n = pr_pe_diagnose(buf, len, report, sizeof(report));
    CHECK(n > 0);
    CHECK(strstr(report, "imagem PE VÁLIDA") != NULL);
    CHECK(strstr(report, "PE32 (32 bits)") != NULL);
    CHECK(strstr(report, "console") != NULL);
    CHECK(strstr(report, ".rdata") != NULL);
    CHECK(strstr(report, "KERNEL32.dll") != NULL);
    CHECK(strstr(report, "GetTickCount64") != NULL);
    CHECK(strstr(report, "MessageBoxA") != NULL);
    CHECK(strstr(report, "Alpha") != NULL);
    CHECK(strstr(report, "entry point: RVA 0x00001000") != NULL);
    CHECK(strstr(report, "nenhum código é executado") != NULL);

    /* diagnóstico de não-PE */
    n = pr_pe_diagnose(junk, sizeof(junk), report, sizeof(report));
    CHECK(n > 0);
    CHECK(strstr(report, "NAO é imagem PE") != NULL);
}
