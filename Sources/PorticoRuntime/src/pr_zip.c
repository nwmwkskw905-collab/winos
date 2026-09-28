/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_zip.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>

#ifdef PR_ENABLE_ZLIB
#include <zlib.h>
#endif

#define ZIP_EOCD_SIG 0x06054b50u
#define ZIP_CD_SIG   0x02014b50u
#define ZIP_LH_SIG   0x04034b50u
#define ZIP_MAX_ENTRIES 65535u

struct pr_zip {
    uint8_t* data;       /* buffer do zip (externo em open_mem) */
    uint8_t* owned_data; /* buffer interno (open_file); liberado em close */
    size_t len;
    size_t entry_count;
    pr_zip_entry* entries;
};

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

pr_status pr_zip_open_mem(const void* data, size_t len, pr_zip** out) {
    if (!data || !out || len < 22) return PR_ERR_INVALID;
    const uint8_t* d = (const uint8_t*)data;

    /* Localiza EOCD (pode haver comentário de até 65535 bytes):
     * procura do fim para o início (última ocorrência é o EOCD real). */
    size_t max_back = len < (size_t)(22 + 65535) ? len : (size_t)(22 + 65535);
    size_t eocd = (size_t)-1;
    for (size_t back = 22; back <= max_back; back++) {
        if (rd32(d + len - back) == ZIP_EOCD_SIG) {
            eocd = len - back;
            break;
        }
    }
    if (eocd == (size_t)-1) return PR_ERR_FORMAT;
    if (eocd + 22 > len) return PR_ERR_FORMAT;

    uint16_t total16 = rd16(d + eocd + 10);
    uint32_t cd_size = rd32(d + eocd + 12);
    uint32_t cd_off = rd32(d + eocd + 16);
    if (cd_off == 0xFFFFFFFFu || cd_size == 0xFFFFFFFFu) {
        return PR_ERR_UNSUPPORTED; /* ZIP64: fora do escopo v1 */
    }
    if ((size_t)cd_off + (size_t)cd_size > len) return PR_ERR_FORMAT;

    pr_zip* z = (pr_zip*)calloc(1, sizeof(pr_zip));
    if (!z) return PR_ERR_NOMEM;
    z->len = len;
    z->data = NULL; /* memória do chamador */
    z->entry_count = total16;
    if (z->entry_count == 0) z->entry_count = 0;
    z->entries = (pr_zip_entry*)calloc(z->entry_count ? z->entry_count : 1, sizeof(pr_zip_entry));
    if (!z->entries) { free(z); return PR_ERR_NOMEM; }

    size_t p = cd_off;
    for (size_t i = 0; i < z->entry_count; i++) {
        if (p + 46 > len) { pr_zip_close(z); return PR_ERR_FORMAT; }
        if (rd32(d + p) != ZIP_CD_SIG) { pr_zip_close(z); return PR_ERR_FORMAT; }
        pr_zip_entry* e = &z->entries[i];
        memset(e, 0, sizeof(*e));
        e->comp_method = rd16(d + p + 10);
        e->crc32 = rd32(d + p + 16);
        e->comp_size = rd32(d + p + 20);
        e->uncomp_size = rd32(d + p + 24);
        uint16_t nlen = rd16(d + p + 28);
        uint16_t xlen = rd16(d + p + 30);
        uint16_t clen = rd16(d + p + 32);
        e->local_header_off = rd32(d + p + 42);
        if (e->comp_size == 0xFFFFFFFFu || e->uncomp_size == 0xFFFFFFFFu ||
            e->local_header_off == 0xFFFFFFFFu) {
            pr_zip_close(z);
            return PR_ERR_UNSUPPORTED; /* ZIP64 */
        }
        size_t name_len = nlen < 255 ? nlen : 255;
        if (p + 46 + nlen > len) { pr_zip_close(z); return PR_ERR_FORMAT; }
        memcpy(e->name, d + p + 46, name_len);
        e->name[name_len] = 0;
        e->is_dir = (name_len > 0 && (e->name[name_len - 1] == '/' || e->name[name_len - 1] == '\\'));
        p += 46 + nlen + xlen + clen;
    }
    /* Guarda o pontador const original via cast (sem ownership). */
    z->data = (uint8_t*)(uintptr_t)d;
    *out = z;
    return PR_OK;
}

pr_status pr_zip_open_file(const char* path, pr_zip** out) {
    if (!path || !out) return PR_ERR_INVALID;
    FILE* f = fopen(path, "rb");
    if (!f) return PR_ERR_IO;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > (long)1024 * 1024 * 1024) { fclose(f); return PR_ERR_FORMAT; }
    uint8_t* buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); return PR_ERR_NOMEM; }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fclose(f);
        free(buf);
        return PR_ERR_IO;
    }
    fclose(f);
    pr_status st = pr_zip_open_mem(buf, (size_t)sz, out);
    if (st != PR_OK) {
        free(buf);
        return st;
    }
    (*out)->owned_data = buf; /* ownership do buffer passa ao handle */
    return PR_OK;
}

void pr_zip_close(pr_zip* z) {
    if (!z) return;
    free(z->entries);
    free(z->owned_data);
    free(z);
}

size_t pr_zip_count(const pr_zip* z) {
    return z ? z->entry_count : 0;
}

pr_status pr_zip_entry_info(const pr_zip* z, size_t idx, pr_zip_entry* out) {
    if (!z || !out || idx >= z->entry_count) return PR_ERR_RANGE;
    *out = z->entries[idx];
    return PR_OK;
}

pr_status pr_zip_extract(const pr_zip* z, size_t idx, void** out_data, size_t* out_len) {
    if (!z || !out_data || !out_len || idx >= z->entry_count) return PR_ERR_INVALID;
    const pr_zip_entry* e = &z->entries[idx];
    const uint8_t* d = z->data;

    if (e->local_header_off + 30 > z->len) return PR_ERR_FORMAT;
    const uint8_t* lh = d + e->local_header_off;
    if (rd32(lh) != ZIP_LH_SIG) return PR_ERR_FORMAT;
    uint16_t nlen = rd16(lh + 26);
    uint16_t xlen = rd16(lh + 28);
    size_t data_off = e->local_header_off + 30 + nlen + xlen;
    if (data_off + e->comp_size > z->len) return PR_ERR_FORMAT;

    uint8_t* out = (uint8_t*)malloc((size_t)e->uncomp_size + 1);
    if (!out) return PR_ERR_NOMEM;
    out[e->uncomp_size] = 0;

    if (e->comp_method == 0) {
        if (e->comp_size != e->uncomp_size) { free(out); return PR_ERR_FORMAT; }
        memcpy(out, d + data_off, (size_t)e->uncomp_size);
    } else if (e->comp_method == 8) {
#ifdef PR_ENABLE_ZLIB
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) { free(out); return PR_ERR_FORMAT; }
        zs.next_in = (Bytef*)(uintptr_t)(d + data_off);
        zs.avail_in = (uInt)e->comp_size;
        zs.next_out = out;
        zs.avail_out = (uInt)e->uncomp_size;
        int zr = inflate(&zs, Z_FINISH);
        inflateEnd(&zs);
        if (zr != Z_STREAM_END) { free(out); return PR_ERR_FORMAT; }
        if (zs.total_out != e->uncomp_size) { free(out); return PR_ERR_FORMAT; }
#else
        free(out);
        return PR_ERR_UNSUPPORTED;
#endif
    } else {
        free(out);
        return PR_ERR_UNSUPPORTED;
    }

#ifdef PR_ENABLE_ZLIB
    uint32_t crc = (uint32_t)crc32(0, out, (uInt)e->uncomp_size);
    if (crc != e->crc32) { free(out); return PR_ERR_FORMAT; }
#endif
    *out_data = out;
    *out_len = (size_t)e->uncomp_size;
    return PR_OK;
}

void pr_zip_free_mem(void* p) {
    free(p);
}

/* ---- sanitização de caminho + extração para diretório ---- */

static int path_is_safe(const char* name) {
    if (!name || !name[0]) return 0;
    if (name[0] == '/' || name[0] == '\\') return 0;
    if (strlen(name) >= 2 && name[1] == ':') return 0; /* "C:..." */
    /* rejeita qualquer componente ".." */
    const char* p = name;
    while (*p) {
        const char* sep = p;
        while (*sep && *sep != '/' && *sep != '\\') sep++;
        size_t n = (size_t)(sep - p);
        if (n == 2 && p[0] == '.' && p[1] == '.') return 0;
        if (!*sep) break;
        p = sep + 1;
    }
    return 1;
}

static pr_status mkpath(const char* dir, char* scratch) {
    /* cria dir recursivamente (scratch é mutável, mesmo conteúdo) */
    for (char* p = scratch + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(scratch, 0755) != 0 && errno != EEXIST) {
                *p = '/';
                return PR_ERR_IO;
            }
            *p = '/';
        }
    }
    if (mkdir(scratch, 0755) != 0 && errno != EEXIST) return PR_ERR_IO;
    (void)dir;
    return PR_OK;
}

pr_status pr_zip_extract_to_dir(const pr_zip* z, const char* dir,
                                size_t* out_files, pr_log_hook_fn hook, void* hook_ud) {
    if (!z || !dir) return PR_ERR_INVALID;
    if (out_files) *out_files = 0;
    size_t files = 0;

    /* diretório raiz */
    char* root = (char*)malloc(strlen(dir) + 2);
    if (!root) return PR_ERR_NOMEM;
    strcpy(root, dir);
    pr_status st = mkpath(dir, root);
    free(root);
    if (st != PR_OK) return st;

    for (size_t i = 0; i < z->entry_count; i++) {
        pr_zip_entry e;
        if (pr_zip_entry_info(z, i, &e) != PR_OK) continue;
        if (e.is_dir) continue;
        if (!path_is_safe(e.name)) {
            if (hook) hook(hook_ud, "entrada ZIP com caminho inseguro ignorada");
            continue;
        }
        size_t dlen = strlen(dir);
        size_t nlen = strlen(e.name);
        char* full = (char*)malloc(dlen + nlen + 2);
        if (!full) return PR_ERR_NOMEM;
        memcpy(full, dir, dlen);
        full[dlen] = '/';
        /* normaliza separadores */
        for (size_t k = 0; k < nlen; k++) {
            char c = e.name[k];
            if (c == '\\') c = '/';
            full[dlen + 1 + k] = c;
        }
        full[dlen + 1 + nlen] = 0;

        /* cria diretórios pais (até o último '/') */
        char* mk = (char*)malloc(strlen(full) + 2);
        if (!mk) { free(full); return PR_ERR_NOMEM; }
        strcpy(mk, full);
        char* slash = strrchr(mk, '/');
        if (slash) {
            *slash = 0;
            st = mkpath(dir, mk);
        }
        free(mk);
        if (st != PR_OK) { free(full); return st; }

        void* buf = NULL;
        size_t blen = 0;
        st = pr_zip_extract(z, i, &buf, &blen);
        if (st != PR_OK) {
            if (hook) hook(hook_ud, "falha ao extrair entrada ZIP");
            free(full);
            continue;
        }
        FILE* f = fopen(full, "wb");
        if (!f) {
            pr_zip_free_mem(buf);
            free(full);
            return PR_ERR_IO;
        }
        fwrite(buf, 1, blen, f);
        fclose(f);
        pr_zip_free_mem(buf);
        free(full);
        files++;
    }
    if (out_files) *out_files = files;
    return PR_OK;
}
