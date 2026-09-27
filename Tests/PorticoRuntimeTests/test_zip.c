#include "pt_util.h"
#include "portico/pr_zip.h"

#include <stdlib.h>
#include <zlib.h>

/* Monta um ZIP em memória com 3 entradas: stored, deflate e uma com caminho inseguro. */
typedef struct { char name[64]; const char* payload; int method; } zitem;

static size_t build_zip(uint8_t** out) {
    zitem items[3] = {
        {"hello.txt", "Hello Portico", 0},
        {"sub/dir/data.txt", "conteudo comprimido do Portico em 2026", 8},
        {"../evil.txt", "nao deve extrair", 0},
    };
    uint8_t comp[512];
    uint8_t* buf = (uint8_t*)calloc(1, 65536);
    CHECK(buf != NULL);
    if (!buf) return 0;   /* OOM real: CHECK acima já falhou */
    size_t pos = 0;
    size_t local_off[3];
    uint32_t crcs[3], csizes[3], usizes[3];

    for (int i = 0; i < 3; i++) {
        size_t ulen = strlen(items[i].payload);
        usizes[i] = (uint32_t)ulen;
        crcs[i] = (uint32_t)crc32(0, (const Bytef*)items[i].payload, (uInt)ulen);
        size_t clen = ulen;
        if (items[i].method == 8) {
            z_stream zs;
            memset(&zs, 0, sizeof(zs));
            deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            zs.next_in = (Bytef*)items[i].payload;
            zs.avail_in = (uInt)ulen;
            zs.next_out = comp;
            zs.avail_out = sizeof(comp);
            deflate(&zs, Z_FINISH);
            clen = zs.total_out;
            deflateEnd(&zs);
        }
        csizes[i] = (uint32_t)clen;

        local_off[i] = pos;
        /* local header */
        uint32_t sig = 0x04034b50;
        memcpy(buf + pos, &sig, 4); pos += 4;
        uint16_t v = 20, flags = 0, m = (uint16_t)items[i].method, t = 0, d = 0;
        memcpy(buf + pos, &v, 2); pos += 2;
        memcpy(buf + pos, &flags, 2); pos += 2;
        memcpy(buf + pos, &m, 2); pos += 2;
        memcpy(buf + pos, &t, 2); pos += 2;
        memcpy(buf + pos, &d, 2); pos += 2;
        memcpy(buf + pos, &crcs[i], 4); pos += 4;
        memcpy(buf + pos, &csizes[i], 4); pos += 4;
        memcpy(buf + pos, &usizes[i], 4); pos += 4;
        uint16_t nlen = (uint16_t)strlen(items[i].name), xlen = 0;
        memcpy(buf + pos, &nlen, 2); pos += 2;
        memcpy(buf + pos, &xlen, 2); pos += 2;
        memcpy(buf + pos, items[i].name, nlen); pos += nlen;
        if (items[i].method == 8) {
            memcpy(buf + pos, comp, clen);
        } else {
            memcpy(buf + pos, items[i].payload, clen);
        }
        pos += clen;
    }

    size_t cd_start = pos;
    for (int i = 0; i < 3; i++) {
        uint32_t sig = 0x02014b50;
        memcpy(buf + pos, &sig, 4); pos += 4;
        uint16_t v = 20, v2 = 20, flags = 0, m = (uint16_t)items[i].method, t = 0, d = 0;
        memcpy(buf + pos, &v, 2); pos += 2;
        memcpy(buf + pos, &v2, 2); pos += 2;
        memcpy(buf + pos, &flags, 2); pos += 2;
        memcpy(buf + pos, &m, 2); pos += 2;
        memcpy(buf + pos, &t, 2); pos += 2;
        memcpy(buf + pos, &d, 2); pos += 2;
        memcpy(buf + pos, &crcs[i], 4); pos += 4;
        memcpy(buf + pos, &csizes[i], 4); pos += 4;
        memcpy(buf + pos, &usizes[i], 4); pos += 4;
        uint16_t nlen = (uint16_t)strlen(items[i].name), xlen = 0, clen = 0;
        memcpy(buf + pos, &nlen, 2); pos += 2;
        memcpy(buf + pos, &xlen, 2); pos += 2;
        memcpy(buf + pos, &clen, 2); pos += 2;
        uint16_t disk = 0, iattr = 0;
        uint32_t eattr = 0;
        memcpy(buf + pos, &disk, 2); pos += 2;
        memcpy(buf + pos, &iattr, 2); pos += 2;
        memcpy(buf + pos, &eattr, 4); pos += 4;
        uint32_t lo = (uint32_t)local_off[i];
        memcpy(buf + pos, &lo, 4); pos += 4;
        memcpy(buf + pos, items[i].name, nlen); pos += nlen;
    }
    size_t cd_size = pos - cd_start;
    uint32_t sig = 0x06054b50;
    memcpy(buf + pos, &sig, 4); pos += 4;
    uint16_t n3 = 3, n3b = 3, z = 0;
    memcpy(buf + pos, &z, 2); pos += 2;
    memcpy(buf + pos, &z, 2); pos += 2;
    memcpy(buf + pos, &n3, 2); pos += 2;
    memcpy(buf + pos, &n3b, 2); pos += 2;
    uint32_t cds = (uint32_t)cd_size, cdo = (uint32_t)cd_start;
    memcpy(buf + pos, &cds, 4); pos += 4;
    memcpy(buf + pos, &cdo, 4); pos += 4;
    memcpy(buf + pos, &z, 2); pos += 2;

    *out = buf;
    return pos;
}

void test_zip(void) {
    uint8_t* zip = NULL;
    size_t zlen = build_zip(&zip);
    CHECK(zlen > 0);

    pr_zip* z = NULL;
    pr_status st = pr_zip_open_mem(zip, zlen, &z);
    CHECK(st == PR_OK);
    CHECK_EQ_U32((unsigned)pr_zip_count(z), 3);

    pr_zip_entry e;
    st = pr_zip_entry_info(z, 0, &e);
    CHECK(st == PR_OK);
    CHECK_STR(e.name, "hello.txt");
    CHECK_EQ_U32(e.comp_method, 0);
    CHECK_EQ_U32((unsigned)e.uncomp_size, 13);

    st = pr_zip_entry_info(z, 1, &e);
    CHECK_STR(e.name, "sub/dir/data.txt");
    CHECK_EQ_U32(e.comp_method, 8);

    void* data = NULL;
    size_t dlen = 0;
    st = pr_zip_extract(z, 0, &data, &dlen);
    CHECK(st == PR_OK);
    CHECK_EQ_U32((unsigned)dlen, 13);
    CHECK(memcmp(data, "Hello Portico", 13) == 0);
    pr_zip_free_mem(data);

    data = NULL;
    dlen = 0;
    st = pr_zip_extract(z, 1, &data, &dlen);
    CHECK(st == PR_OK);
    if (st == PR_OK) {
        const char* expect = "conteudo comprimido do Portico em 2026";
        CHECK_EQ_U32((unsigned)dlen, strlen(expect));
        CHECK(memcmp(data, expect, strlen(expect)) == 0);
        pr_zip_free_mem(data);
    }

    /* extração para diretório: entradas seguras extraídas; "../evil.txt" bloqueada */
    const char* dir = "build/test_zip_out";
    size_t files = 0;
    st = pr_zip_extract_to_dir(z, dir, &files, NULL, NULL);
    CHECK(st == PR_OK);
    CHECK_EQ_U32((unsigned)files, 2);

    FILE* f = fopen("build/test_zip_out/hello.txt", "rb");
    CHECK(f != NULL);
    if (f) {
        char b[32] = {0};
        size_t r = fread(b, 1, sizeof(b), f);
        fclose(f);
        CHECK_EQ_U32((unsigned)r, 13);
    }
    f = fopen("build/test_zip_out/sub/dir/data.txt", "rb");
    CHECK(f != NULL);
    if (f) fclose(f);
    f = fopen("build/test_zip_out/evil.txt", "rb");
    CHECK(f == NULL);
    f = fopen("evil.txt", "rb");
    CHECK(f == NULL);

    pr_zip_close(z);
    free(zip);
}
