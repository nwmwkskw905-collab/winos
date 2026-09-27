/* SEH/unwind x64 mínimo — implementação (ver pr_unwind.h). */
#include "portico/pr_unwind.h"
#include "portico/pr_pe.h"
#include <string.h>

static uint32_t rd32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t rd16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

pr_status pr_unwind_index(const uint8_t* image, size_t image_size,
                          uint32_t exc_rva, uint32_t exc_size,
                          pr_unwind_entry* out, size_t cap, size_t* count) {
    if (!image || !out || !count) return PR_ERR_INVALID;
    *count = 0;
    if (!exc_rva || exc_size < 12) return PR_OK;   /* sem .pdata = ok vazio */
    if ((size_t)exc_rva + exc_size > image_size) return PR_ERR_FORMAT;
    size_t n = exc_size / 12;
    for (size_t i = 0; i < n; i++) {
        const uint8_t* e = image + exc_rva + i * 12;
        uint32_t b = rd32(e), en = rd32(e + 4), u = rd32(e + 8);
        if (b == 0 && en == 0 && u == 0) break;    /* terminador padrão */
        if (!(b < en) || en > image_size || u >= image_size) return PR_ERR_FORMAT;
        if (*count >= cap) return PR_ERR_RANGE;
        out[*count].begin_rva = b;
        out[*count].end_rva = en;
        out[*count].unwind_rva = u;
        (*count)++;
    }
    return PR_OK;
}

int pr_unwind_lookup(const pr_unwind_entry* tab, size_t n, uint32_t rva) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (rva < tab[mid].begin_rva) hi = mid;
        else if (rva >= tab[mid].end_rva) lo = mid + 1;
        else return (int)mid;
    }
    return -1;
}

pr_status pr_unwind_parse(const uint8_t* image, size_t image_size,
                          uint32_t unwind_rva, pr_unwind_info* out) {
    if (!image || !out) return PR_ERR_INVALID;
    memset(out, 0, sizeof *out);
    if ((size_t)unwind_rva + 4 > image_size) return PR_ERR_FORMAT;
    const uint8_t* p = image + unwind_rva;
    out->version = p[0] & 0x7;
    out->flags = (uint8_t)(p[0] >> 3);
    out->prolog_size = p[1];
    uint8_t count = p[2];            /* nº de SLOTS u16 (codes + extras) */
    out->frame_reg = p[3] & 0xF;
    out->frame_offset = (uint32_t)(p[3] >> 4) * 16;
    if (out->version != 1 && out->version != 2) return PR_ERR_UNSUPPORTED;
    if ((size_t)unwind_rva + 4 + (size_t)count * 2 > image_size) return PR_ERR_FORMAT;

    const uint8_t* c = p + 4;
    size_t i = 0;
    while (i < count) {
        if (out->ncodes >= PR_UNWIND_MAX_CODES) return PR_ERR_RANGE;
        pr_unwind_code* uc = &out->codes[out->ncodes];
        uc->prolog_off = c[i * 2];
        uc->op = (uint8_t)(c[i * 2 + 1] & 0xF);
        uc->info = (uint8_t)(c[i * 2 + 1] >> 4);
        uc->extras = 0;
        i++;
        switch (uc->op) {
        case PR_UWOP_ALLOC_LARGE:
            if (uc->info == 0) {
                if (i + 1 > count) return PR_ERR_FORMAT;
                uc->extra[0] = rd16(c + i * 2);
                uc->extras = 1;
                i += 1;
            } else if (uc->info == 1) {
                if (i + 2 > count) return PR_ERR_FORMAT;
                uc->extra[0] = rd16(c + i * 2);
                uc->extra[1] = rd16(c + i * 2 + 2);
                uc->extras = 2;
                i += 2;
            } else {
                return PR_ERR_UNSUPPORTED;
            }
            break;
        case PR_UWOP_SAVE_NONVOL:
        case PR_UWOP_SAVE_XMM128:
            if (i + 1 > count) return PR_ERR_FORMAT;
            uc->extra[0] = rd16(c + i * 2);
            uc->extras = 1;
            i += 1;
            break;
        case PR_UWOP_SAVE_NONVOL_FAR:
        case PR_UWOP_SAVE_XMM128_FAR:
            if (i + 2 > count) return PR_ERR_FORMAT;
            uc->extra[0] = rd16(c + i * 2);
            uc->extra[1] = rd16(c + i * 2 + 2);
            uc->extras = 2;
            i += 2;
            break;
        case PR_UWOP_PUSH_NONVOL:
        case PR_UWOP_ALLOC_SMALL:
        case PR_UWOP_SET_FPREG:
        case PR_UWOP_PUSH_MACHFRAME:
            break;
        default:
            return PR_ERR_UNSUPPORTED;
        }
        out->ncodes++;
    }
    /* alinhamento dword após os slots */
    size_t off = 4 + (size_t)count * 2;
    if (count & 1) off += 2;
    if (out->flags & PR_UNW_FLAG_CHAININFO) {
        if ((size_t)unwind_rva + off + 12 > image_size) return PR_ERR_FORMAT;
        const uint8_t* e = p + off;
        out->is_chained = 1;
        out->chained.begin_rva = rd32(e);
        out->chained.end_rva = rd32(e + 4);
        out->chained.unwind_rva = rd32(e + 8);
    } else if (out->flags & (PR_UNW_FLAG_EHANDLER | PR_UNW_FLAG_UHANDLER)) {
        if ((size_t)unwind_rva + off + 4 > image_size) return PR_ERR_FORMAT;
        out->has_handler = 1;
        out->handler_rva = rd32(p + off);
    }
    return PR_OK;
}

pr_status pr_unwind_compute(const pr_unwind_info* ui,
                            uint64_t* frame_bytes, uint32_t* saved_mask) {
    if (!ui) return PR_ERR_INVALID;
    uint64_t frame = 0;
    uint32_t mask = 0;
    for (size_t i = 0; i < ui->ncodes; i++) {
        const pr_unwind_code* uc = &ui->codes[i];
        switch (uc->op) {
        case PR_UWOP_PUSH_NONVOL:
            frame += 8;
            mask |= 1u << (uc->info & 15);
            break;
        case PR_UWOP_ALLOC_LARGE:
            if (uc->info == 0) frame += (uint64_t)uc->extra[0] * 8;
            else if (uc->info == 1)
                frame += (uint64_t)uc->extra[0] | ((uint64_t)uc->extra[1] << 16);
            else return PR_ERR_UNSUPPORTED;
            break;
        case PR_UWOP_ALLOC_SMALL:
            frame += (uint64_t)uc->info * 8 + 8;
            break;
        case PR_UWOP_SET_FPREG:
            break;
        case PR_UWOP_SAVE_NONVOL:
        case PR_UWOP_SAVE_NONVOL_FAR:
            mask |= 1u << (uc->info & 15);
            break;
        case PR_UWOP_SAVE_XMM128:
        case PR_UWOP_SAVE_XMM128_FAR:
            break;
        case PR_UWOP_PUSH_MACHFRAME:
            frame += uc->info ? 48 : 40;
            break;
        default:
            return PR_ERR_UNSUPPORTED;
        }
    }
    if (frame_bytes) *frame_bytes = frame;
    if (saved_mask) *saved_mask = mask;
    return PR_OK;
}
