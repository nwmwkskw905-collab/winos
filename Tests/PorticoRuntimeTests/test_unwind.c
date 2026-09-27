/* Testes — SEH/unwind x64 mínimo: .pdata (RUNTIME_FUNCTION) + .xdata
 * (UNWIND_INFO): indexação, lookup e interpretação dos unwind codes. */
#include "pt_util.h"
#include "portico/pr_unwind.h"
#include "portico/pr_pe.h"
#include "portico/pr_peproc.h"
#include "portico/pr_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* imagem sintética: 2 RUNTIME_FUNCTION + UNWIND_INFO (um com handler,
 * um com chain) para validar o parser sem depender do toolchain */
static void build_synth(uint8_t* img) {
    /* UNWIND_INFO A em 0x100: v1, 3 codes: PUSH_NONVOL rbp, ALLOC_SMALL 0x20,
     * SET_FPREG + SAVE_NONVOL rbx; EHANDLER em 0x200 */
    static const uint8_t uia[] = {
        0x01 | (0x01 << 3), /* v1 | EHANDLER */
        8,                  /* prolog */
        5,                  /* slots: push + alloc + setfp + save + extra */
        0,                  /* sem frame reg */
        0x00, 0x50,         /* PUSH_NONVOL rbp (op=0 info=5) */
        0x02, 0x32,         /* ALLOC_SMALL info=3 -> 3*8+8 = 32 */
        0x04, 0x03,         /* SET_FPREG (op=3 info=0) */
        0x06, 0x34,         /* SAVE_NONVOL rbx (op=4 info=3) */
        0x00, 0x00,         /* extra: frame offset 0 */
        0x00, 0x00,         /* pad (count 5 impar) */
        0x00, 0x02, 0x00, 0x00, /* handler_rva = 0x200 */
    };
    /* UNWIND_INFO B em 0x140: v1, 1 code PUSH_NONVOL r12; CHAININFO->0x30 */
    static const uint8_t uib[] = {
        0x01 | (0x04 << 3), /* v1 | CHAININFO */
        2,
        1,
        0,
        0x00, 0xC0,         /* PUSH_NONVOL r12 (op=0 info=12) */
        0x00, 0x00,         /* pad */
        0x00, 0x01, 0x00, 0x00, /* chained begin 0x100 */
        0x10, 0x00, 0x00, 0x00, /* chained end 0x10 */
        0x00, 0x01, 0x00, 0x00, /* chained unwind 0x100 */
    };
    /* .pdata em 0x40: 2 entradas + terminador */
    static const uint8_t pdata[] = {
        0x00, 0x01, 0x00, 0x00, /* begin 0x100 */
        0x40, 0x01, 0x00, 0x00, /* end 0x140 */
        0x00, 0x01, 0x00, 0x00, /* unwind 0x100 */
        0x80, 0x01, 0x00, 0x00, /* begin 0x180 */
        0xC0, 0x01, 0x00, 0x00, /* end 0x1C0 */
        0x40, 0x01, 0x00, 0x00, /* unwind 0x140 */
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    };
    memset(img, 0, 0x400);
    memcpy(img + 0x40, pdata, sizeof pdata);
    memcpy(img + 0x100, uia, sizeof uia);
    memcpy(img + 0x140, uib, sizeof uib);
}

void test_unwind(void) {
    /* 1) parser sintético completo */
    {
        uint8_t img[0x400];
        build_synth(img);
        pr_unwind_entry tab[8];
        size_t n = 0;
        CHECK(pr_unwind_index(img, sizeof img, 0x40, 12 * 3, tab, 8, &n) == PR_OK);
        CHECK(n == 2);
        CHECK(tab[0].begin_rva == 0x100 && tab[0].end_rva == 0x140);
        CHECK(tab[1].unwind_rva == 0x140);

        CHECK(pr_unwind_lookup(tab, n, 0x100) == 0);
        CHECK(pr_unwind_lookup(tab, n, 0x13F) == 0);
        CHECK(pr_unwind_lookup(tab, n, 0x180) == 1);
        CHECK(pr_unwind_lookup(tab, n, 0x200) == -1);

        pr_unwind_info ui;
        CHECK(pr_unwind_parse(img, sizeof img, 0x100, &ui) == PR_OK);
        CHECK(ui.version == 1 && ui.flags == PR_UNW_FLAG_EHANDLER);
        CHECK(ui.has_handler && ui.handler_rva == 0x200);
        CHECK(ui.ncodes == 4);
        uint64_t frame = 0;
        uint32_t mask = 0;
        CHECK(pr_unwind_compute(&ui, &frame, &mask) == PR_OK);
        CHECK(frame == 40);            /* 8 (push) + 32 (alloc small) */
        CHECK((mask & (1u << 5)) != 0);  /* rbp */
        CHECK((mask & (1u << 3)) != 0);  /* rbx */

        pr_unwind_info uc;
        CHECK(pr_unwind_parse(img, sizeof img, 0x140, &uc) == PR_OK);
        CHECK(uc.is_chained && uc.chained.begin_rva == 0x100);
        CHECK(uc.ncodes == 1 && uc.codes[0].op == PR_UWOP_PUSH_NONVOL);
    }

    /* 2) PE real (MinGW): .pdata indexada e interpretável por inteiro */
    {
        FILE* f = fopen("Tests/PorticoRuntimeTests/data/hello_real.exe", "rb");
        CHECK(f != NULL);
        if (f) {
            fseek(f, 0, SEEK_END);
            long len = ftell(f);
            fseek(f, 0, SEEK_SET);
            uint8_t* pe = malloc((size_t)len);
            CHECK(pe && fread(pe, 1, (size_t)len, f) == (size_t)len);
            fclose(f);

            pr_pe_loaded img;
            CHECK(pr_pe_load(pe, (size_t)len, &img, "hello_real.exe") == PR_OK);
            uint32_t exr = 0, exs = 0;
            CHECK(pr_pe_data_dir(pe, (size_t)len, 3, &exr, &exs) == PR_OK);
            CHECK(exr != 0 && exs >= 12);

            pr_unwind_entry tab[256];
            size_t n = 0;
            CHECK(pr_unwind_index(img.image, img.image_size, exr, exs,
                                  tab, 256, &n) == PR_OK);
            CHECK(n > 4);   /* mainCRTStartup + main + helpers do CRT */
            for (size_t i = 0; i < n; i++) {
                pr_unwind_info ui;
                CHECK(pr_unwind_parse(img.image, img.image_size,
                                      tab[i].unwind_rva, &ui) == PR_OK);
                uint64_t frame = 0;
                uint32_t mask = 0;
                CHECK(pr_unwind_compute(&ui, &frame, &mask) == PR_OK);
            }
            CHECK(pr_unwind_lookup(tab, n, img.entry_rva) >= 0);

            /* e pelo processo: índice exposto na API do peproc */
            pr_log* log = pr_log_create(64);
            pr_peproc* p = NULL;
            CHECK(pr_peproc_create(pe, (size_t)len, log, &p) == PR_OK);
            if (p) {
                CHECK(pr_peproc_unwind_count(p) == n);
                pr_unwind_info ui;
                CHECK(pr_peproc_unwind_at(p, 0, &ui) == PR_OK);
                CHECK(pr_peproc_unwind_find(p, img.entry_rva, &ui) >= 0);
                pr_peproc_destroy(p);
            }
            pr_log_destroy(log);

            /* honestidade: versão desconhecida = PR_ERR_UNSUPPORTED */
            uint8_t bad[64];
            memset(bad, 0, sizeof bad);
            bad[0] = 0x07;   /* version 7 */
            pr_unwind_info ub;
            CHECK(pr_unwind_parse(bad, sizeof bad, 0, &ub) == PR_ERR_UNSUPPORTED);

            pr_pe_loaded_free(&img);
            free(pe);
        }
    }
}
