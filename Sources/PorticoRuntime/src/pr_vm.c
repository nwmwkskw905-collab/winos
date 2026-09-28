/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_vm.h"

#include <stdlib.h>
#include <string.h>

typedef struct vm_region {
    int used;
    uint32_t base;
    uint32_t size;
    int prot;
    char tag[PR_VM_TAG_MAX];
} vm_region;

struct pr_vm {
    uint8_t* mem;
    int external;
    size_t   space;
    vm_region regions[PR_VM_MAX_REGIONS];
    uint32_t next_free_hint; /* busca de alocação a partir do fim */
    /* último acesso negado */
    int      denied;
    uint32_t d_addr;
    size_t   d_len;
    int      d_acc;
    uint32_t denied_count;
};

pr_vm* pr_vm_create(size_t space) {
    if (space < 0x10000 || space > (256u << 20)) return NULL;
    pr_vm* vm = (pr_vm*)calloc(1, sizeof(pr_vm));
    if (!vm) return NULL;
    vm->mem = (uint8_t*)calloc(1, space);
    if (!vm->mem) { free(vm); return NULL; }
    vm->space = space;
    vm->next_free_hint = (uint32_t)space;
    return vm;
}

pr_vm* pr_vm_create_on(size_t space, void* backing) {
    if (space < 0x10000 || space > (256u << 20) || !backing) return NULL;
    pr_vm* vm = (pr_vm*)calloc(1, sizeof(pr_vm));
    if (!vm) return NULL;
    vm->mem = (uint8_t*)backing;
    vm->external = 1;
    vm->space = space;
    vm->next_free_hint = (uint32_t)space;
    return vm;
}

void pr_vm_destroy(pr_vm* vm) {
    if (!vm) return;
    if (!vm->external) free(vm->mem);
    free(vm);
}

size_t pr_vm_space(const pr_vm* vm) { return vm ? vm->space : 0; }

static vm_region* find_region(pr_vm* vm, uint32_t addr) {
    for (int i = 0; i < PR_VM_MAX_REGIONS; i++) {
        vm_region* r = &vm->regions[i];
        if (r->used && addr >= r->base && addr < r->base + r->size) return r;
    }
    return NULL;
}

static int overlaps(pr_vm* vm, uint32_t base, uint32_t size) {
    for (int i = 0; i < PR_VM_MAX_REGIONS; i++) {
        vm_region* r = &vm->regions[i];
        if (!r->used) continue;
        if (base < r->base + r->size && r->base < base + size) return 1;
    }
    return 0;
}

pr_status pr_vm_map(pr_vm* vm, uint32_t addr, size_t len, int prot,
                    const char* tag, void** out_host) {
    if (!vm || len == 0) return PR_ERR_INVALID;
    if ((uint64_t)addr + len > vm->space) return PR_ERR_RANGE;
    if (overlaps(vm, addr, (uint32_t)len)) return PR_ERR_STATE;
    for (int i = 0; i < PR_VM_MAX_REGIONS; i++) {
        vm_region* r = &vm->regions[i];
        if (r->used) continue;
        r->used = 1;
        r->base = addr;
        r->size = (uint32_t)len;
        r->prot = prot & (PR_VM_PROT_R | PR_VM_PROT_W | PR_VM_PROT_X);
        r->tag[0] = 0;
        if (tag) {
            size_t j = 0;
            while (tag[j] && j + 1 < sizeof(r->tag)) { r->tag[j] = tag[j]; j++; }
            r->tag[j] = 0;
        }
        if (out_host) *out_host = vm->mem + addr;
        return PR_OK;
    }
    return PR_ERR_NOMEM;
}

uint8_t* pr_vm_backing(pr_vm* vm, uint32_t* out_space) {
    if (!vm) return NULL;
    if (out_space) *out_space = (uint32_t)vm->space;
    return vm->mem;
}

pr_status pr_vm_unmap(pr_vm* vm, uint32_t addr, size_t len) {
    if (!vm || len == 0) return PR_ERR_INVALID;
    vm_region* r = find_region(vm, addr);
    if (!r) return PR_ERR_RANGE;
    if (addr != r->base || len != r->size) return PR_ERR_STATE; /* região completa */
    memset(r, 0, sizeof(*r));
    return PR_OK;
}

pr_status pr_vm_protect(pr_vm* vm, uint32_t addr, size_t len, int prot) {
    if (!vm || len == 0) return PR_ERR_INVALID;
    uint32_t end = addr + (uint32_t)len;
    uint32_t cur = addr;
    int touched = 0;
    while (cur < end) {
        vm_region* r = find_region(vm, cur);
        if (!r || r->base != cur) return PR_ERR_STATE; /* precisa cobrir regiões inteiras */
        r->prot = prot & (PR_VM_PROT_R | PR_VM_PROT_W | PR_VM_PROT_X);
        cur += r->size;
        touched = 1;
    }
    return touched ? PR_OK : PR_ERR_RANGE;
}

pr_status pr_vm_alloc(pr_vm* vm, size_t len, int prot, const char* tag,
                      uint32_t* out_addr, void** out_host) {
    if (!vm || len == 0) return PR_ERR_INVALID;
    uint32_t alen = (uint32_t)((len + 0xFFF) & ~(size_t)0xFFF);
    if (alen == 0 || alen > vm->space) return PR_ERR_RANGE;
    /* busca do fim para o baixo, alinhado a página */
    uint32_t cand = vm->next_free_hint;
    while (cand >= alen) {
        uint32_t base = cand - alen;
        if (!overlaps(vm, base, alen)) {
            void* host = NULL;
            pr_status st = pr_vm_map(vm, base, alen, prot, tag, &host);
            if (st != PR_OK) return st;
            vm->next_free_hint = base;
            if (out_addr) *out_addr = base;
            if (out_host) *out_host = host;
            return PR_OK;
        }
        /* recua além da região conflitante */
        vm_region* r = find_region(vm, base);
        cand = r ? r->base : (base - 0x1000);
    }
    return PR_ERR_RANGE;
}

void* pr_vm_translate(pr_vm* vm, uint32_t addr, size_t len, int acc) {
    if (!vm || len == 0) return NULL;
    if ((uint64_t)addr + len > vm->space) {
        vm->denied = 1; vm->d_addr = addr; vm->d_len = len; vm->d_acc = acc;
        vm->denied_count++;
        return NULL;
    }
    vm_region* r = find_region(vm, addr);
    if (!r || addr + len > r->base + r->size) {
        vm->denied = 1; vm->d_addr = addr; vm->d_len = len; vm->d_acc = acc;
        vm->denied_count++;
        return NULL;
    }
    if ((r->prot & acc) != acc) {
        vm->denied = 1; vm->d_addr = addr; vm->d_len = len; vm->d_acc = acc;
        vm->denied_count++;
        return NULL;
    }
    return vm->mem + addr;
}

pr_status pr_vm_read(const pr_vm* vm, uint32_t addr, void* out, size_t len) {
    /* castaway const p/ contador de denegations no translate */
    void* p = pr_vm_translate((pr_vm*)vm, addr, len, PR_VM_PROT_R);
    if (!p) return PR_ERR_FAULT;
    memcpy(out, p, len);
    return PR_OK;
}

pr_status pr_vm_write(pr_vm* vm, uint32_t addr, const void* in, size_t len) {
    void* p = pr_vm_translate(vm, addr, len, PR_VM_PROT_W);
    if (!p) return PR_ERR_FAULT;
    memcpy(p, in, len);
    return PR_OK;
}

pr_status pr_vm_loader_write(pr_vm* vm, uint32_t addr, const void* in, size_t len) {
    if (!vm || !in || len == 0) return PR_ERR_INVALID;
    if ((uint64_t)addr + len > vm->space) return PR_ERR_RANGE;
    vm_region* r = find_region(vm, addr);
    if (!r || (uint64_t)addr + len > (uint64_t)r->base + r->size)
        return PR_ERR_FAULT;
    memcpy(vm->mem + addr, in, len);
    return PR_OK;
}

pr_status pr_vm_find_gap(pr_vm* vm, uint32_t span, uint32_t* out_base) {
    if (!vm || span == 0) return PR_ERR_INVALID;
    uint32_t alen = (span + 0xFFF) & ~0xFFFu;
    if (alen == 0 || alen > vm->space) return PR_ERR_RANGE;
    uint32_t cand = vm->next_free_hint;
    while (cand >= alen) {
        uint32_t base = cand - alen;
        if (!overlaps(vm, base, alen)) {
            if (out_base) *out_base = base;
            return PR_OK;
        }
        vm_region* r = find_region(vm, base);
        cand = r ? r->base : (base - 0x1000);
    }
    return PR_ERR_RANGE;
}

size_t pr_vm_regions(const pr_vm* vm, pr_vm_region* out, size_t max) {
    if (!vm) return 0;
    size_t total = 0, copied = 0;
    for (int i = 0; i < PR_VM_MAX_REGIONS; i++) {
        const vm_region* r = &vm->regions[i];
        if (!r->used) continue;
        total++;
        if (out && copied < max) {
            out[copied].base = r->base;
            out[copied].size = r->size;
            out[copied].prot = r->prot;
            memcpy(out[copied].tag, r->tag, sizeof(r->tag));
            copied++;
        }
    }
    return total;
}

int pr_vm_check(pr_vm* vm, uint32_t addr, size_t len, int acc) {
    return pr_vm_translate(vm, addr, len, acc) != NULL;
}

int pr_vm_last_denied(const pr_vm* vm, uint32_t* out_addr, size_t* out_len,
                      int* out_acc) {
    if (!vm || !vm->denied) return 0;
    if (out_addr) *out_addr = vm->d_addr;
    if (out_len) *out_len = vm->d_len;
    if (out_acc) *out_acc = vm->d_acc;
    return 1;
}

uint32_t pr_vm_denied_count(const pr_vm* vm) { return vm ? vm->denied_count : 0; }
