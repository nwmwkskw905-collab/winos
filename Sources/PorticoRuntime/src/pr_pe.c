/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_pe.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* Limites defensivos contra arquivos malformados. */
#define PE_MAX_IMPORTS 512

static uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int pr_pe_looks_like(const void* data, size_t len) {
    const uint8_t* d = (const uint8_t*)data;
    return (len >= 2 && d[0] == 'M' && d[1] == 'Z');
}

/* Localiza o offset do cabeçalho PE e valida. */
static pr_status pe_headers(const uint8_t* d, size_t len,
                            uint32_t* out_pe_off, uint32_t* out_opt_off,
                            uint32_t* out_opt_size, uint32_t* out_sec_off,
                            uint16_t* out_nsections) {
    if (!pr_pe_looks_like(d, len)) return PR_ERR_FORMAT;
    if (len < 0x40) return PR_ERR_FORMAT;
    uint32_t pe_off = rd32(d + 0x3C);
    if (pe_off + 24 > len) return PR_ERR_FORMAT;
    if (memcmp(d + pe_off, "PE\0\0", 4) != 0) return PR_ERR_FORMAT;
    uint16_t nsections = rd16(d + pe_off + 6);
    uint16_t opt_size = rd16(d + pe_off + 20);
    uint32_t opt_off = pe_off + 24;
    if (opt_off + opt_size > len) return PR_ERR_FORMAT;
    if (nsections > PR_PE_MAX_SECTIONS) return PR_ERR_FORMAT;
    *out_pe_off = pe_off;
    *out_opt_off = opt_off;
    *out_opt_size = opt_size;
    *out_sec_off = opt_off + opt_size;
    *out_nsections = nsections;
    return PR_OK;
}

pr_status pr_pe_scan(const void* data, size_t len, pr_pe_info* out) {
    if (!data || !out) return PR_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    const uint8_t* d = (const uint8_t*)data;
    uint32_t pe_off, opt_off, opt_size, sec_off;
    uint16_t nsections;
    pr_status st = pe_headers(d, len, &pe_off, &opt_off, &opt_size, &sec_off, &nsections);
    if (st != PR_OK) return st;

    out->is_pe = 1;
    out->machine = rd16(d + pe_off + 4);
    out->timestamp = rd32(d + pe_off + 8);
    out->section_count = nsections;

    if (opt_size < 68) return PR_ERR_FORMAT;
    uint16_t magic = rd16(d + opt_off);
    out->is_pe32plus = (magic == 0x20B);
    if (magic != 0x10B && magic != 0x20B) return PR_ERR_FORMAT;

    out->subsystem = rd16(d + opt_off + (out->is_pe32plus ? 68 : 68));
    out->dll_characteristics = rd16(d + opt_off + (out->is_pe32plus ? 70 : 70));
    out->entry_point_rva = rd32(d + opt_off + 16);
    out->size_of_image = rd32(d + opt_off + 56);
    out->section_alignment = rd32(d + opt_off + 32);
    out->file_alignment = rd32(d + opt_off + 36);
    if (!out->is_pe32plus) {
        out->image_base = rd32(d + opt_off + 28);
        out->image_base64 = out->image_base;
    } else {
        out->image_base64 =
            (uint64_t)rd32(d + opt_off + 24) |
            ((uint64_t)rd32(d + opt_off + 28) << 32);
        if (out->image_base64 <= 0xFFFFFFFFu)
            out->image_base = (uint32_t)out->image_base64;
    }

    uint16_t coff_chars = rd16(d + pe_off + 22);
    out->is_dll = (coff_chars & 0x2000) != 0;

    switch (out->machine) {
        case 0x014C: strncpy(out->arch, "x86", sizeof(out->arch) - 1); break;
        case 0x8664: strncpy(out->arch, "x86_64", sizeof(out->arch) - 1); break;
        case 0x01C0:
        case 0x01C4: strncpy(out->arch, "arm", sizeof(out->arch) - 1); break;
        case 0xAA64: strncpy(out->arch, "arm64", sizeof(out->arch) - 1); break;
        default:     strncpy(out->arch, "desconhecido", sizeof(out->arch) - 1); break;
    }

    /* Conta DLLs importadas (diretório de dados índice 1). */
    uint32_t dd_off = opt_off + (out->is_pe32plus ? 112 : 96); /* início dos data directories */
    uint32_t dd_size = rd16(d + opt_off + (out->is_pe32plus ? 108 : 92)); /* NumberOfRvaAndSizes */
    (void)dd_size;
    uint32_t imp_rva = 0, imp_size = 0;
    if (opt_off + (out->is_pe32plus ? 112 + 16 : 96 + 16) <= opt_off + opt_size) {
        imp_rva = rd32(d + dd_off + 8);   /* entrada 1: VirtualAddress */
        imp_size = rd32(d + dd_off + 12); /* entrada 1: Size */
        (void)imp_size;
    }
    if (imp_rva != 0) {
        uint32_t imp_off = 0;
        if (pr_pe_rva_to_offset(d, len, imp_rva, &imp_off) == PR_OK) {
            size_t count = 0;
            for (size_t i = 0; i < PE_MAX_IMPORTS; i++) {
                uint32_t e = imp_off + (uint32_t)(i * 20);
                if (e + 20 > len) break;
                uint32_t name_rva = rd32(d + e + 12);
                uint32_t thunk0 = rd32(d + e + 0);
                uint32_t thunk1 = rd32(d + e + 16);
                if (name_rva == 0 && thunk0 == 0 && thunk1 == 0) break;
                count++;
            }
            out->import_count = (uint32_t)count;
        }
    }

    /* Diretório de relocations (índice 5). */
    if (opt_off + (out->is_pe32plus ? 112 + 48 : 96 + 48) <= opt_off + opt_size) {
        out->reloc_rva = rd32(d + dd_off + 5 * 8);
        out->reloc_size = rd32(d + dd_off + 5 * 8 + 4);
    }
    return PR_OK;
}

size_t pr_pe_sections(const void* data, size_t len, pr_pe_section* out,
                      size_t max, size_t* out_count) {
    if (out_count) *out_count = 0;
    if (!data) return 0;
    const uint8_t* d = (const uint8_t*)data;
    uint32_t pe_off, opt_off, opt_size, sec_off;
    uint16_t nsections;
    if (pe_headers(d, len, &pe_off, &opt_off, &opt_size, &sec_off, &nsections) != PR_OK) {
        return 0;
    }
    if (out_count) *out_count = nsections;
    size_t copied = 0;
    for (uint16_t i = 0; i < nsections && copied < max; i++) {
        uint32_t e = sec_off + (uint32_t)i * 40;
        if (e + 40 > len) break;
        pr_pe_section s;
        memset(&s, 0, sizeof(s));
        memcpy(s.name, d + e, 8);
        s.name[8] = 0;
        s.virtual_size = rd32(d + e + 8);
        s.virtual_addr = rd32(d + e + 12);
        s.raw_size = rd32(d + e + 16);
        s.raw_ptr = rd32(d + e + 20);
        s.characteristics = rd32(d + e + 36);
        if (out) out[copied] = s;
        copied++;
    }
    return copied;
}

pr_status pr_pe_rva_to_offset(const void* data, size_t len, uint32_t rva,
                              uint32_t* out_off) {
    if (!data || !out_off) return PR_ERR_INVALID;
    const uint8_t* d = (const uint8_t*)data;
    uint32_t pe_off, opt_off, opt_size, sec_off;
    uint16_t nsections;
    pr_status st = pe_headers(d, len, &pe_off, &opt_off, &opt_size, &sec_off, &nsections);
    if (st != PR_OK) return st;

    /* Cabeçalhos: RVA dentro do tamanho bruto do cabeçalho. */
    uint32_t hdr_size = rd32(d + opt_off + 60); /* SizeOfHeaders */
    if (rva < hdr_size && rva < len) {
        *out_off = rva;
        return PR_OK;
    }
    for (uint16_t i = 0; i < nsections; i++) {
        uint32_t e = sec_off + (uint32_t)i * 40;
        if (e + 40 > len) return PR_ERR_FORMAT;
        uint32_t va = rd32(d + e + 12);
        uint32_t vsz = rd32(d + e + 8);
        uint32_t raw = rd32(d + e + 20);
        uint32_t rsz = rd32(d + e + 16);
        uint32_t span = vsz > rsz ? vsz : rsz;
        if (rva >= va && rva < va + span) {
            uint32_t off = raw + (rva - va);
            if (off >= len) return PR_ERR_RANGE;
            *out_off = off;
            return PR_OK;
        }
    }
    return PR_ERR_RANGE;
}

pr_status pr_pe_import_name(const void* data, size_t len, size_t index,
                            char* out, size_t out_len) {
    if (!data || !out || out_len == 0) return PR_ERR_INVALID;
    const uint8_t* d = (const uint8_t*)data;
    uint32_t pe_off, opt_off, opt_size, sec_off;
    uint16_t nsections;
    pr_status st = pe_headers(d, len, &pe_off, &opt_off, &opt_size, &sec_off, &nsections);
    if (st != PR_OK) return st;

    int is_pe32plus = (rd16(d + opt_off) == 0x20B);
    uint32_t dd_off = opt_off + (is_pe32plus ? 112 : 96);
    if (dd_off + 16 > opt_off + opt_size) return PR_ERR_FORMAT;
    uint32_t imp_rva = rd32(d + dd_off + 8);
    if (imp_rva == 0) return PR_ERR_RANGE;
    uint32_t imp_off = 0;
    st = pr_pe_rva_to_offset(d, len, imp_rva, &imp_off);
    if (st != PR_OK) return st;

    for (size_t i = 0; i <= index && i < PE_MAX_IMPORTS; i++) {
        uint32_t e = imp_off + (uint32_t)(i * 20);
        if (e + 20 > len) return PR_ERR_RANGE;
        uint32_t name_rva = rd32(d + e + 12);
        uint32_t thunk0 = rd32(d + e + 0);
        if (i == index) {
            if (name_rva == 0) return PR_ERR_RANGE;
            uint32_t name_off = 0;
            st = pr_pe_rva_to_offset(d, len, name_rva, &name_off);
            if (st != PR_OK) return st;
            size_t j = 0;
            while (name_off + j < len && j + 1 < out_len && d[name_off + j] != 0) {
                out[j] = (char)d[name_off + j];
                j++;
            }
            out[j] = 0;
            return PR_OK;
        }
        if (name_rva == 0 && thunk0 == 0) return PR_ERR_RANGE;
    }
    return PR_ERR_RANGE;
}

/* ============================================================
 * Extensão: funções de importação por função, exports, carga em
 * memória (SEM execução) e diagnóstico legível.
 * ============================================================ */

#include <stdlib.h>

/* Offset do diretório de dados idx (0=exports, 1=imports) no optional header. */
static int pe_data_dir(const uint8_t* d, size_t len, int is_plus,
                       unsigned idx, uint32_t* out_rva, uint32_t* out_size) {
    uint32_t pe_off, opt_off, opt_size, sec_off;
    uint16_t nsections;
    if (pe_headers(d, len, &pe_off, &opt_off, &opt_size, &sec_off, &nsections) != PR_OK)
        return 0;
    uint32_t dd_off = opt_off + (is_plus ? 112 : 96);
    uint32_t n_dirs = rd32(d + opt_off + (is_plus ? 108 : 92));
    if (idx >= n_dirs) return 0;
    uint32_t e = dd_off + idx * 8;
    if (e + 8 > opt_off + opt_size) return 0;
    *out_rva = rd32(d + e);
    *out_size = rd32(d + e + 4);
    return 1;
}

/* Tamanho de entrada de thunk por arquitetura. */
static unsigned thunk_size(int is_plus) { return is_plus ? 8 : 4; }

/* Lê entrada de thunk; retorna 1 se válida, define *out_val e *out_by_ord. */
static int read_thunk(const uint8_t* d, size_t len, uint32_t off, int is_plus,
                      uint32_t* out_val, int* out_by_ord) {
    if (is_plus) {
        if (off + 8 > len) return 0;
        uint64_t v = (uint64_t)rd32(d + off) | ((uint64_t)rd32(d + off + 4) << 32);
        if (v == 0) return 0;
        *out_by_ord = (v >> 63) != 0;
        *out_val = (uint32_t)(v & 0x7FFFFFFFu);
    } else {
        if (off + 4 > len) return 0;
        uint32_t v = rd32(d + off);
        if (v == 0) return 0;
        *out_by_ord = (v & 0x80000000u) != 0;
        *out_val = v & 0x7FFFFFFFu;
    }
    return 1;
}

/* Offset (no arquivo) do início do thunk array da dll_index-ésima DLL
 * (OriginalFirstThunk preferido; fallback FirstThunk). */
static int imp_dll_thunk_off(const uint8_t* d, size_t len, size_t dll_index,
                             uint32_t* out_off, int* is_plus_out) {
    uint32_t pe_off, opt_off, opt_size, sec_off;
    uint16_t nsections;
    if (pe_headers(d, len, &pe_off, &opt_off, &opt_size, &sec_off, &nsections) != PR_OK)
        return 0;
    int is_plus = (rd16(d + opt_off) == 0x20B);
    uint32_t imp_rva, imp_size;
    if (!pe_data_dir(d, len, is_plus, 1, &imp_rva, &imp_size) || imp_rva == 0)
        return 0;
    uint32_t imp_off = 0;
    if (pr_pe_rva_to_offset(d, len, imp_rva, &imp_off) != PR_OK) return 0;
    uint32_t e = imp_off + (uint32_t)(dll_index * 20);
    if (e + 20 > len) return 0;
    uint32_t oft = rd32(d + e + 0);
    uint32_t ft = rd32(d + e + 16);
    uint32_t name_rva = rd32(d + e + 12);
    if (name_rva == 0 && oft == 0 && ft == 0) return 0;
    uint32_t thunk_rva = oft ? oft : ft;
    uint32_t thunk_off = 0;
    if (pr_pe_rva_to_offset(d, len, thunk_rva, &thunk_off) != PR_OK) return 0;
    *out_off = thunk_off;
    if (is_plus_out) *is_plus_out = is_plus;
    return 1;
}

size_t pr_pe_import_func_count(const void* data, size_t len, size_t dll_index) {
    if (!data) return 0;
    const uint8_t* d = (const uint8_t*)data;
    uint32_t thunk_off;
    int is_plus;
    if (!imp_dll_thunk_off(d, len, dll_index, &thunk_off, &is_plus)) return 0;
    size_t tsz = thunk_size(is_plus);
    size_t count = 0;
    for (size_t i = 0; i < PE_MAX_IMPORTS; i++) {
        uint32_t val; int by_ord;
        if (!read_thunk(d, len, thunk_off + (uint32_t)(i * tsz), is_plus, &val, &by_ord))
            break;
        count++;
    }
    return count;
}

pr_status pr_pe_import_func_at(const void* data, size_t len,
                               size_t dll_index, size_t func_index,
                               pr_pe_import_func* out) {
    if (!data || !out) return PR_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    const uint8_t* d = (const uint8_t*)data;
    uint32_t thunk_off;
    int is_plus;
    if (!imp_dll_thunk_off(d, len, dll_index, &thunk_off, &is_plus)) return PR_ERR_RANGE;
    size_t tsz = thunk_size(is_plus);
    uint32_t val; int by_ord;
    if (!read_thunk(d, len, thunk_off + (uint32_t)(func_index * tsz), is_plus,
                    &val, &by_ord))
        return PR_ERR_RANGE;
    if (by_ord) {
        out->by_ordinal = 1;
        out->ordinal = (uint16_t)(val & 0xFFFF);
    } else {
        /* Import By Name: u16 hint + nome ASCII. */
        uint32_t off = 0;
        if (pr_pe_rva_to_offset(d, len, val, &off) != PR_OK) return PR_ERR_FORMAT;
        if (off + 2 > len) return PR_ERR_FORMAT;
        out->hint = rd16(d + off);
        size_t j = 0;
        while (off + 2 + j < len && j + 1 < sizeof(out->name) && d[off + 2 + j] != 0) {
            out->name[j] = (char)d[off + 2 + j];
            j++;
        }
        out->name[j] = 0;
    }
    return PR_OK;
}

pr_status pr_pe_data_dir(const void* data, size_t len, unsigned dir_index,
                         uint32_t* rva, uint32_t* size) {
    if (!data || !rva || !size) return PR_ERR_INVALID;
    *rva = 0;
    *size = 0;
    const uint8_t* d = (const uint8_t*)data;
    uint32_t pe_off, opt_off, opt_size, sec_off;
    uint16_t dummy_ns;
    pr_status st = pe_headers(d, len, &pe_off, &opt_off, &opt_size, &sec_off, &dummy_ns);
    if (st != PR_OK) return st;
    uint16_t magic = rd16(d + opt_off);
    if (magic != 0x10B && magic != 0x20B) return PR_ERR_FORMAT;
    uint32_t dd_off = opt_off + (magic == 0x20B ? 112u : 96u);
    uint32_t dd_cnt = rd32(d + opt_off + (magic == 0x20B ? 108u : 92u));
    if (dir_index >= dd_cnt) return PR_OK;   /* ausente = 0/0 */
    uint32_t off = dd_off + dir_index * 8;
    if ((size_t)off + 8 > len || off + 8 > opt_off + opt_size) return PR_ERR_FORMAT;
    *rva = rd32(d + off);
    *size = rd32(d + off + 4);
    return PR_OK;
}

size_t pr_pe_exports(const void* data, size_t len, pr_pe_export* out,
                     size_t max, size_t* out_count) {
    if (out_count) *out_count = 0;
    if (!data) return 0;
    const uint8_t* d = (const uint8_t*)data;
    uint32_t pe_off, opt_off, opt_size, sec_off;
    uint16_t nsections;
    if (pe_headers(d, len, &pe_off, &opt_off, &opt_size, &sec_off, &nsections) != PR_OK)
        return 0;
    int is_plus = (rd16(d + opt_off) == 0x20B);
    uint32_t exp_rva, exp_size;
    if (!pe_data_dir(d, len, is_plus, 0, &exp_rva, &exp_size) || exp_rva == 0)
        return 0;
    uint32_t exp_off = 0;
    if (pr_pe_rva_to_offset(d, len, exp_rva, &exp_off) != PR_OK) return 0;
    if (exp_off + 40 > len) return 0;

    uint32_t ordinal_base = rd32(d + exp_off + 16);
    uint32_t nfuncs = rd32(d + exp_off + 20);
    uint32_t nnames = rd32(d + exp_off + 24);
    uint32_t afuncs_rva = rd32(d + exp_off + 28);
    uint32_t anames_rva = rd32(d + exp_off + 32);
    uint32_t aords_rva = rd32(d + exp_off + 36);
    (void)nfuncs;
    if (nnames > PE_MAX_IMPORTS) nnames = PE_MAX_IMPORTS;

    uint32_t afuncs = 0, anames = 0, aords = 0;
    if (pr_pe_rva_to_offset(d, len, afuncs_rva, &afuncs) != PR_OK) return 0;
    if (pr_pe_rva_to_offset(d, len, anames_rva, &anames) != PR_OK) return 0;
    if (pr_pe_rva_to_offset(d, len, aords_rva, &aords) != PR_OK) return 0;

    size_t copied = 0;
    for (uint32_t i = 0; i < nnames; i++) {
        if (anames + i * 4 + 4 > len || aords + i * 2 + 2 > len) break;
        uint32_t name_rva = rd32(d + anames + i * 4);
        uint16_t ord_idx = rd16(d + aords + i * 2);
        pr_pe_export e;
        memset(&e, 0, sizeof(e));
        uint32_t name_off = 0;
        if (pr_pe_rva_to_offset(d, len, name_rva, &name_off) == PR_OK) {
            size_t j = 0;
            while (name_off + j < len && j + 1 < sizeof(e.name) && d[name_off + j] != 0) {
                e.name[j] = (char)d[name_off + j];
                j++;
            }
            e.name[j] = 0;
        }
        e.ordinal = (uint16_t)(ordinal_base + ord_idx);
        e.by_name = 1;
        if (afuncs + (uint32_t)ord_idx * 4 + 4 <= len) {
            e.rva = rd32(d + afuncs + (uint32_t)ord_idx * 4);
        }
        if (out && copied < max) out[copied] = e;
        copied++;
    }
    return copied;
}

/* ---- Carga em memória (SEM execução) ---- */

pr_status pr_pe_load(const void* data, size_t len, pr_pe_loaded* out,
                     const char* module_name) {
    if (!data || !out) return PR_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    const uint8_t* d = (const uint8_t*)data;
    pr_pe_info info;
    pr_status st = pr_pe_scan(d, len, &info);
    if (st != PR_OK) return st;
    if (info.size_of_image == 0 || info.size_of_image > (64u << 20))
        return PR_ERR_RANGE; /* defesa: imagem vazia ou > 64 MiB */

    /* Alignment (PE spec): potências de 2; File 0x200..0x10000 quando
     * Section >= 0x1000; abaixo disso devem ser iguais. */
    {
        uint32_t fa = info.file_alignment, sa = info.section_alignment;
        if (fa == 0 || sa == 0 || (fa & (fa - 1)) || (sa & (sa - 1)))
            return PR_ERR_FORMAT;
        if (sa < 0x1000) {
            if (sa != fa) return PR_ERR_FORMAT;
        } else if (fa < 0x200 || fa > 0x10000 || fa > sa) {
            return PR_ERR_FORMAT;
        }
    }

    uint8_t* img = (uint8_t*)calloc(1, info.size_of_image);
    if (!img) return PR_ERR_NOMEM;

    /* Cabeçalhos (até SizeOfHeaders, limitado ao arquivo). */
    uint32_t pe_off = rd32(d + 0x3C);
    uint32_t opt_size = rd16(d + pe_off + 20);
    (void)opt_size;
    uint32_t opt_off = pe_off + 24;
    uint32_t hdr_size = rd32(d + opt_off + 60);
    if (hdr_size > info.size_of_image) hdr_size = info.size_of_image;
    if (hdr_size > len) hdr_size = (uint32_t)len;
    memcpy(img, d, hdr_size);

    /* Seções: copia raw→virtual dentro dos limites. */
    pr_pe_section secs[PR_PE_MAX_SECTIONS];
    size_t nsec = pr_pe_sections(d, len, secs, PR_PE_MAX_SECTIONS, NULL);
    for (size_t i = 0; i < nsec; i++) {
        uint32_t va = secs[i].virtual_addr;
        uint32_t rsz = secs[i].raw_size;
        uint32_t raw = secs[i].raw_ptr;
        if (va >= info.size_of_image) continue;
        uint32_t room = info.size_of_image - va;
        uint32_t ncopy = rsz < room ? rsz : room;
        if (raw + ncopy > len) {
            if (raw >= len) continue;
            ncopy = (uint32_t)len - raw;
        }
        memcpy(img + va, d + raw, ncopy);
        out->sections[i] = secs[i];
    }

    out->image = img;
    out->image_size = info.size_of_image;
    out->entry_rva = info.entry_point_rva;
    out->image_base = info.image_base;
    out->image_base64 = info.image_base64;
    out->section_alignment = info.section_alignment;
    out->file_alignment = info.file_alignment;
    out->reloc_rva = info.reloc_rva;
    out->reloc_size = info.reloc_size;
    out->is_pe32plus = info.is_pe32plus;
    out->is_dll = info.is_dll;
    out->machine = info.machine;
    out->subsystem = info.subsystem;
    out->section_count = (uint32_t)nsec;
    memcpy(out->arch, info.arch, sizeof(out->arch));
    if (module_name) {
        size_t j = 0;
        while (module_name[j] && j + 1 < sizeof(out->module_name)) {
            out->module_name[j] = module_name[j];
            j++;
        }
        out->module_name[j] = 0;
    }
    return PR_OK;
}

void pr_pe_loaded_free(pr_pe_loaded* img) {
    if (!img) return;
    free(img->image);
    img->image = NULL;
    img->image_size = 0;
}

void* pr_pe_loaded_ptr(const pr_pe_loaded* img, uint32_t rva, size_t len) {
    if (!img || !img->image) return NULL;
    if ((uint64_t)rva + len > img->image_size) return NULL;
    return img->image + rva;
}

void* pr_pe_loaded_entry(const pr_pe_loaded* img) {
    return pr_pe_loaded_ptr(img, img ? img->entry_rva : 0, 1);
}

pr_status pr_pe_loaded_string(const pr_pe_loaded* img, uint32_t rva,
                              char* out, size_t out_len) {
    if (!img || !out || out_len == 0) return PR_ERR_INVALID;
    const char* p = (const char*)pr_pe_loaded_ptr(img, rva, 1);
    if (!p) return PR_ERR_RANGE;
    size_t max = img->image_size - rva;
    size_t j = 0;
    while (j < max && j + 1 < out_len && p[j]) {
        out[j] = p[j];
        j++;
    }
    out[j] = 0;
    return PR_OK;
}

/* ---- Diagnóstico ---- */

static size_t diag_add(char* out, size_t out_len, size_t pos,
                       const char* fmt, ...) {
    if (pos + 1 >= out_len) return pos;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out + pos, out_len - pos, fmt, ap);
    va_end(ap);
    if (n < 0) return pos;
    size_t wrote = (size_t)n;
    if (wrote > out_len - pos - 1) wrote = out_len - pos - 1;
    return pos + wrote;
}

size_t pr_pe_diagnose(const void* data, size_t len, char* out, size_t out_len) {
    if (!out || out_len == 0) return 0;
    out[0] = 0;
    if (!data) return 0;
    const uint8_t* d = (const uint8_t*)data;
    size_t p = 0;

    p = diag_add(out, out_len, p, "== Diagnóstico PE ==\n");
    if (!pr_pe_looks_like(d, len)) {
        p = diag_add(out, out_len, p, "resultado: NAO é imagem PE (sem assinatura MZ)\n");
        return p;
    }
    pr_pe_info info;
    if (pr_pe_scan(d, len, &info) != PR_OK) {
        p = diag_add(out, out_len, p,
            "resultado: assinatura MZ presente, mas cabeçalho PE INVÁLIDO\n");
        return p;
    }
    p = diag_add(out, out_len, p, "resultado: imagem PE VÁLIDA\n");
    p = diag_add(out, out_len, p, "formato: %s | tipo: %s\n",
                 info.is_pe32plus ? "PE32+ (64 bits)" : "PE32 (32 bits)",
                 info.is_dll ? "DLL" : "EXECUTÁVEL");
    p = diag_add(out, out_len, p, "arquitetura: %s (machine=0x%04X)\n",
                 info.arch, info.machine);
    p = diag_add(out, out_len, p, "subsystem: %s (%u)\n",
                 info.subsystem == 2 ? "GUI" : (info.subsystem == 3 ? "console" : "outro"),
                 (unsigned)info.subsystem);
    p = diag_add(out, out_len, p, "image base: 0x%08X | tamanho em memória: %u bytes\n",
                 info.image_base, info.size_of_image);
    p = diag_add(out, out_len, p, "entry point: RVA 0x%08X\n", info.entry_point_rva);

    pr_pe_section secs[PR_PE_MAX_SECTIONS];
    size_t nsec = pr_pe_sections(d, len, secs, PR_PE_MAX_SECTIONS, NULL);
    p = diag_add(out, out_len, p, "seções (%u):\n", (unsigned)nsec);
    for (size_t i = 0; i < nsec; i++) {
        p = diag_add(out, out_len, p,
            "  %-8s vaddr=0x%06X vsize=%-6u raw=0x%06X/%-6u flags=0x%08X\n",
            secs[i].name, secs[i].virtual_addr, secs[i].virtual_size,
            secs[i].raw_ptr, secs[i].raw_size, secs[i].characteristics);
    }

    p = diag_add(out, out_len, p, "imports: %u DLL(s)\n", info.import_count);
    for (uint32_t i = 0; i < info.import_count && i < 32; i++) {
        char dll[PR_PE_MAX_NAME];
        if (pr_pe_import_name(d, len, i, dll, sizeof(dll)) != PR_OK) continue;
        size_t nf = pr_pe_import_func_count(d, len, i);
        p = diag_add(out, out_len, p, "  %s (%u funções):\n", dll, (unsigned)nf);
        for (size_t f = 0; f < nf && f < 16; f++) {
            pr_pe_import_func fn;
            if (pr_pe_import_func_at(d, len, i, f, &fn) != PR_OK) continue;
            if (fn.by_ordinal) {
                p = diag_add(out, out_len, p, "    #%u (ordinal)\n", (unsigned)fn.ordinal);
            } else {
                p = diag_add(out, out_len, p, "    %s\n", fn.name);
            }
        }
        if (nf > 16) p = diag_add(out, out_len, p, "    … +%u\n", (unsigned)(nf - 16));
    }

    pr_pe_export exps[64];
    size_t nexps = pr_pe_exports(d, len, exps, 64, NULL);
    p = diag_add(out, out_len, p, "exports: %u\n", (unsigned)nexps);
    for (size_t i = 0; i < nexps && i < 8; i++) {
        p = diag_add(out, out_len, p, "  %s (ord %u, RVA 0x%08X)\n",
                     exps[i].name[0] ? exps[i].name : "(sem nome)",
                     (unsigned)exps[i].ordinal, exps[i].rva);
    }
    if (nexps > 8) p = diag_add(out, out_len, p, "  … +%u\n", (unsigned)(nexps - 8));

    p = diag_add(out, out_len, p,
        "AVISO: etapa apenas de análise/carga — nenhum código é executado.\n");
    return p;
}

pr_status pr_pe_import_iat_rva(const void* data, size_t len,
                               size_t dll_index, size_t func_index,
                               uint32_t* out_rva) {
    if (!data || !out_rva) return PR_ERR_INVALID;
    const uint8_t* d = (const uint8_t*)data;
    uint32_t pe_off, opt_off, opt_size, sec_off;
    uint16_t nsections;
    if (pe_headers(d, len, &pe_off, &opt_off, &opt_size, &sec_off, &nsections) != PR_OK)
        return PR_ERR_FORMAT;
    int is_plus = (rd16(d + opt_off) == 0x20B);
    uint32_t imp_rva, imp_size;
    if (!pe_data_dir(d, len, is_plus, 1, &imp_rva, &imp_size) || imp_rva == 0)
        return PR_ERR_RANGE;
    uint32_t imp_off = 0;
    if (pr_pe_rva_to_offset(d, len, imp_rva, &imp_off) != PR_OK) return PR_ERR_FORMAT;
    uint32_t e = imp_off + (uint32_t)(dll_index * 20);
    if (e + 20 > len) return PR_ERR_RANGE;
    uint32_t ft = rd32(d + e + 16); /* FirstThunk (IAT) */
    if (ft == 0) return PR_ERR_RANGE;
    *out_rva = ft + (uint32_t)(func_index * (is_plus ? 8u : 4u));
    return PR_OK;
}

/* ---- Relocations (Basereloc) ---- */

static uint64_t rd64(const uint8_t* p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}
static void wr32u(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static void wr64u(uint8_t* p, uint64_t v) {
    wr32u(p, (uint32_t)v);
    wr32u(p + 4, (uint32_t)(v >> 32));
}

typedef struct reloc_iter {
    const uint8_t* d;
    size_t len;
    uint32_t dir_off, dir_end;
    uint32_t block_off, entry_i;
    int bad;
    int done;
    char err[96];
} reloc_iter;

static void reloc_iter_open(const void* data, size_t len, reloc_iter* it) {
    memset(it, 0, sizeof(*it));
    it->d = (const uint8_t*)data;
    it->len = len;
    it->done = 1;
    uint32_t pe_off, opt_off, opt_size, sec_off;
    uint16_t nsections;
    if (pe_headers(it->d, len, &pe_off, &opt_off, &opt_size, &sec_off,
                   &nsections) != PR_OK) {
        it->bad = 1;
        snprintf(it->err, sizeof(it->err), "cabeçalhos PE inválidos");
        return;
    }
    int is_plus = (rd16(it->d + opt_off) == 0x20B);
    uint32_t rva = 0, size = 0;
    if (!pe_data_dir(it->d, len, is_plus, 5, &rva, &size) || rva == 0 ||
        size == 0) {
        return; /* sem relocations: vazio, não erro */
    }
    uint32_t off = 0;
    if (pr_pe_rva_to_offset(it->d, len, rva, &off) != PR_OK) {
        it->bad = 1;
        snprintf(it->err, sizeof(it->err), "Basereloc inacessível (rva 0x%08X)", rva);
        return;
    }
    it->dir_off = off;
    it->dir_end = off + size > it->len ? (uint32_t)it->len : off + size;
    it->block_off = off;
    it->entry_i = 0;
    it->done = 0;
}

/* 1 = entrada lida; 0 = fim; -1 = erro (it->err). */
static int reloc_iter_next(reloc_iter* it, pr_pe_reloc* out) {
    while (!it->done) {
        if (it->block_off + 8 > it->dir_end) {
            it->done = 1;
            return 0;
        }
        uint32_t page = rd32(it->d + it->block_off);
        uint32_t bsize = rd32(it->d + it->block_off + 4);
        if (bsize == 0 && page == 0) { it->done = 1; return 0; } /* padding */
        if (bsize < 8 || (bsize & 1) ||
            it->block_off + (uint64_t)bsize > it->dir_end) {
            it->bad = 1;
            it->done = 1;
            snprintf(it->err, sizeof(it->err),
                     "bloco de relocation inválido (page 0x%08X, size %u)",
                     page, bsize);
            return -1;
        }
        size_t nentries = (bsize - 8) / 2;
        if (it->entry_i < nentries) {
            uint16_t e = rd16(it->d + it->block_off + 8 + it->entry_i * 2);
            it->entry_i++;
            out->type = (uint8_t)(e >> 12);
            out->rva = page + (e & 0xFFF);
            return 1;
        }
        it->block_off += bsize;
        it->entry_i = 0;
    }
    return 0;
}

static int reloc_type_ok(uint8_t type) {
    return type == 0 || type == 3 || type == 10;
}

size_t pr_pe_reloc_count(const void* data, size_t len) {
    if (!data) return 0;
    reloc_iter it;
    pr_pe_reloc r;
    size_t n = 0;
    for (reloc_iter_open(data, len, &it); !it.bad; ) {
        int v = reloc_iter_next(&it, &r);
        if (v == 1) n++;
        else break;
    }
    return it.bad ? 0 : n;
}

pr_status pr_pe_reloc_at(const void* data, size_t len, size_t index,
                         pr_pe_reloc* out) {
    if (!data || !out) return PR_ERR_INVALID;
    reloc_iter it;
    pr_pe_reloc r;
    size_t i = 0;
    for (reloc_iter_open(data, len, &it); !it.bad; ) {
        int v = reloc_iter_next(&it, &r);
        if (v != 1) break;
        if (i == index) { *out = r; return PR_OK; }
        i++;
    }
    return PR_ERR_RANGE;
}

pr_status pr_pe_reloc_validate(const void* data, size_t len,
                               char* out_reason, size_t cap) {
    if (out_reason && cap) out_reason[0] = 0;
    if (!data) return PR_ERR_INVALID;
    reloc_iter it;
    pr_pe_reloc r;
    for (reloc_iter_open(data, len, &it); !it.bad; ) {
        int v = reloc_iter_next(&it, &r);
        if (v == 0) return PR_OK;
        if (v < 0) break;
        if (!reloc_type_ok(r.type)) {
            it.bad = 1;
            snprintf(it.err, sizeof(it.err),
                     "tipo de relocation %u nao suportado (rva 0x%08X)",
                     (unsigned)r.type, r.rva);
            break;
        }
    }
    if (it.bad) {
        if (out_reason && cap)
            snprintf(out_reason, cap, "%s", it.err);
        return PR_ERR_FORMAT;
    }
    return PR_OK;
}

pr_status pr_pe_reloc_apply(const void* data, size_t len, uint8_t* image,
                            size_t image_size, uint64_t preferred_base,
                            uint64_t load_base, char* out_reason, size_t cap) {
    if (out_reason && cap) out_reason[0] = 0;
    if (!data || !image) return PR_ERR_INVALID;
    int64_t delta = (int64_t)load_base - (int64_t)preferred_base;
    reloc_iter it;
    pr_pe_reloc r;
    for (reloc_iter_open(data, len, &it); !it.bad; ) {
        int v = reloc_iter_next(&it, &r);
        if (v == 0) return PR_OK;
        if (v < 0) break;
        if (!reloc_type_ok(r.type)) {
            it.bad = 1;
            snprintf(it.err, sizeof(it.err),
                     "tipo de relocation %u nao suportado (rva 0x%08X)",
                     (unsigned)r.type, r.rva);
            break;
        }
        if (r.type == 0) continue; /* ABSOLUTE: padding */
        size_t need = (r.type == 10) ? 8 : 4;
        if ((uint64_t)r.rva + need > image_size) {
            it.bad = 1;
            snprintf(it.err, sizeof(it.err),
                     "relocation fora da imagem (rva 0x%08X)", r.rva);
            break;
        }
        uint8_t* slot = image + r.rva;
        if (r.type == 3) { /* HIGHLOW (PE32) */
            uint32_t val = rd32(slot);
            val = (uint32_t)(val + (uint32_t)(int32_t)delta);
            wr32u(slot, val);
        } else { /* DIR64 (PE32+) */
            uint64_t val = rd64(slot);
            val = (uint64_t)((int64_t)val + delta);
            wr64u(slot, val);
        }
    }
    if (out_reason && cap)
        snprintf(out_reason, cap, "%s", it.err);
    return PR_ERR_FORMAT;
}
