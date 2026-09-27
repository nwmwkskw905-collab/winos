#include "pt_util.h"
#include "pt_pe_builder.h"
#include "portico/pr_pe.h"

void test_pe(void) {
    uint8_t buf[0x400];
    size_t len = pt_build_pe32(buf, sizeof(buf), 0);
    CHECK(len == 0x400);

    CHECK(pr_pe_looks_like(buf, len));
    CHECK(!pr_pe_looks_like("ZZ", 2));

    pr_pe_info info;
    pr_status st = pr_pe_scan(buf, len, &info);
    CHECK(st == PR_OK);
    CHECK_EQ_U32(info.is_pe, 1);
    CHECK_EQ_U32(info.machine, 0x014C);
    CHECK_STR(info.arch, "x86");
    CHECK_EQ_U32(info.subsystem, 2);
    CHECK_EQ_U32(info.section_count, 1);
    CHECK_EQ_U32(info.import_count, 1);
    CHECK_EQ_U32(info.entry_point_rva, 0x1000);
    CHECK_EQ_U32(info.image_base, 0x00400000);
    CHECK_EQ_U32(info.is_dll, 0);
    CHECK_EQ_U32(info.is_pe32plus, 0);

    char name[64];
    st = pr_pe_import_name(buf, len, 0, name, sizeof(name));
    CHECK(st == PR_OK);
    CHECK_STR(name, "KERNEL32.dll");
    st = pr_pe_import_name(buf, len, 1, name, sizeof(name));
    CHECK(st == PR_ERR_RANGE);

    pr_pe_section secs[4];
    size_t total = 0;
    size_t copied = pr_pe_sections(buf, len, secs, 4, &total);
    CHECK_EQ_U32(copied, 1);
    CHECK_EQ_U32(total, 1);
    CHECK_STR(secs[0].name, ".rdata");
    CHECK_EQ_U32(secs[0].virtual_addr, 0x1000);

    uint32_t off = 0;
    st = pr_pe_rva_to_offset(buf, len, 0x1030, &off);
    CHECK(st == PR_OK);
    CHECK_EQ_U32(off, 0x230);

    /* variante DLL */
    len = pt_build_pe32(buf, sizeof(buf), 1);
    st = pr_pe_scan(buf, len, &info);
    CHECK(st == PR_OK);
    CHECK_EQ_U32(info.is_dll, 1);

    /* lixo não é PE */
    st = pr_pe_scan("MZ\1\2\3", 5, &info);
    CHECK(st == PR_ERR_FORMAT);
    st = pr_pe_scan("XX", 2, &info);
    CHECK(st == PR_ERR_FORMAT);
}
