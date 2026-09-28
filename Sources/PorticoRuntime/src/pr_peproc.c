/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_peproc.h"
#include "portico/pr_unwind.h"
#include "portico/pr_pe.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "portico/pr_cpu64.h"

/* Layout do processo. */
#define PROC_SPACE      (16u << 20)   /* 16 MiB de espaço de endereçamento */
#define PROC_STACK_TOP  0x00F00000u   /* pilha de 64 KiB abaixo do topo */
#define PROC_STACK_SZ   0x00010000u
#define PROC_STUB_BASE  0x00E00000u   /* páginas de thunks (stubs stdcall) */
#define PROC_STUB_STRIDE 16u
#define PROC_STUB_MAX    1024u        /* G88: expande de 256 → 1024 thunks */
#define PROC_STUB_SIZE   (PROC_STUB_MAX * PROC_STUB_STRIDE + 0x1000u) /* 1024*16 + 1 página sentinel = 0x5000 */
#define PROC_API_VECTOR 0x2E          /* INT 0x2E = chamada Win32 */
#define PROC_DATA_BASE   0x00E10000u  /* células RW p/ símbolos de dados (msvcrt) */
#define PROC_TEB_BASE    0x00E20000u  /* TEB (gs:[...]); PEB em TEB_BASE+0x1000 */
#define PROC_CALL_VECTOR 0x2F         /* INT 0x2F = sentinel de pr_peproc_call */
#define PROC_CALL_SENTINEL (PROC_STUB_BASE + PROC_STUB_MAX * PROC_STUB_STRIDE + 0xFF0u) /* após thunks */
#define PROC_MAX_IMPORTS 64
#define PROC_SENTINEL   0xDEAD0000u   /* retorno de saída do entry point */

/* Módulo do convidado (DLL PE32+ fornecida pelo app e/ou carregada). */
typedef struct proc_mod {
    char     name[64];      /* canônico minúsculo com .dll */
    uint8_t* data;          /* bytes originais (parser de exports/imports) */
    size_t   len;
    uint64_t base;          /* VA mapeada (0 = só fornecida) */
    uint32_t span;          /* tamanho mapeado em páginas */
    uint32_t entry_rva;     /* DllMain (0 = sem entry) */
    int      refs;
    int      mapped;
} proc_mod;

struct pr_peproc {
    pr_log* log;
    uint8_t* file;        /* bytes originais do PE (parser de imports/relocs) */
    size_t file_len;
    pr_pe_loaded img;
    int img_loaded;
    pr_vm* vm;
    pr_cpu* cpu;
    pr_cpu64* cpu64;   /* engine x86-64 (subconjunto) quando PE32+ */
    pr_win32_ctx* w32;
    uint32_t load_base;   /* base REAL da imagem (pode diferir da preferida) */
    pr_peproc_stop stop;
    char diag[768];
    uint32_t exit_code;
    int prepared;
    uint64_t api_call_count;
    int started;      /* [CPU] execution started já logado */
    uint64_t start_ns; /* relogio monotônico no início da execução */
    /* FASE 6 — módulos (DLLs PE32+) e chamadas de funções do convidado */
    proc_mod mods[8];
    size_t nmods;
    int call_done;
    pr_win32_modops modops;
    uint32_t bind_addrs[1024];   /* endereço de resolução por índice do catálogo:
                                 * thunk (código) OU célula RW (dados) */
    /* SEH/unwind x64: índice .pdata (RUNTIME_FUNCTION) da imagem */
    pr_unwind_entry unw[1024];
    size_t nunw;
};

static double proc_ms(pr_peproc* p) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    uint64_t ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    return p->start_ns ? (double)(ns - p->start_ns) / 1e6 : 0.0;
}

static const char* proc_arch(const pr_peproc* p) {
    if (p && p->img_loaded) {
        if (p->img.machine == 0x8664) return "x86-64";
        if (p->img.machine == 0x014C) return "x86 (IA-32)";
    }
    return "(desconhecida)";
}

/* ---- diagnóstico EXECUTION STOPPED (Reason/Module/Function/Address/Architecture) ---- */

static void diag_set(pr_peproc* p, pr_peproc_stop stop, const char* reason,
                     const char* module, const char* function,
                     const char* technical, uint64_t addr) {
    p->stop = stop;
    char addr_txt[32];
    if (addr != UINT64_MAX) snprintf(addr_txt, sizeof(addr_txt), "0x%08X", (uint32_t)addr);
    else snprintf(addr_txt, sizeof(addr_txt), "(—)");
    snprintf(p->diag, sizeof(p->diag),
             "EXECUTION STOPPED\n\nReason:\n%s\n\nModule:\n%s\n\nFunction:\n%s\n\n"
             "Address:\n%s\n\nArchitecture:\n%s%s%s",
             reason,
             (module && module[0]) ? module : "(processo)",
             (function && function[0]) ? function : "(—)",
             addr_txt,
             proc_arch(p),
             (technical && technical[0]) ? "\n\nTechnical:\n" : "",
             (technical && technical[0]) ? technical : "");
    if (p->log) {
        pr_log_write(p->log, PR_LOG_ERROR, "peproc", "%s — %s!%s @ %s",
                     reason,
                     (module && module[0]) ? module : "-",
                     (function && function[0]) ? function : "-",
                     addr_txt);
    }
}

/* ---- guard de memória do convidado ---- */

static int proc_guard(void* ud, uint32_t addr, size_t len, int acc) {
    pr_peproc* p = (pr_peproc*)ud;
    return pr_vm_check(p->vm, addr, len, acc);
}

/* ---- dispatch Win32 via stub: mov eax, idx; INT 0x2E; ret N ---- */

static int proc_api_trap(void* ud, pr_cpu* cpu, uint8_t vector) {
    pr_peproc* p = (pr_peproc*)ud;
    uint32_t esp = pr_cpu_reg(cpu, PR_REG_ESP);
    uint32_t call_site = 0;
    pr_vm_read(p->vm, esp, &call_site, 4); /* [esp] = retorno = após o call */
    if (vector != PROC_API_VECTOR) {
        char tech[64];
        snprintf(tech, sizeof(tech), "vector INT 0x%02X", vector);
        diag_set(p, PR_PEPROC_STOP_BAD_INSN,
                 "Unsupported interrupt", "", "", tech, pr_cpu_eip(cpu));
        return 1;
    }
    size_t idx = (size_t)pr_cpu_reg(cpu, PR_REG_EAX);
    const pr_win32_export* catalog = NULL;
    size_t ncat = pr_win32_catalog(&catalog);
    if (idx >= ncat) {
        diag_set(p, PR_PEPROC_STOP_API_UNSUPPORTED,
                 "Unsupported Win32 API", "(despacho)", "(índice inválido)", "",
                 call_site);
        return 1;
    }
    const pr_win32_export* e = &catalog[idx];
    if (e->status != PR_WIN32_IMPLEMENTED || !e->fn) {
        diag_set(p, PR_PEPROC_STOP_API_UNSUPPORTED,
                 "Unsupported Win32 API", e->module, e->name, e->note, call_site);
        return 1;
    }

    /* stdcall: [esp] = retorno; [esp+4..] = argumentos */
    size_t nargs = e->stdcall_bytes / 4;
    uint64_t args[12];
    if (nargs > 12) nargs = 12;
    for (size_t i = 0; i < nargs; i++) {
        uint32_t v = 0;
        if (pr_vm_read(p->vm, esp + 4 + (uint32_t)(i * 4), &v, 4) != PR_OK) {
            diag_set(p, PR_PEPROC_STOP_BAD_MEMORY,
                     "Invalid memory access (argumentos da API)",
                     e->module, e->name, "", call_site);
            return 1;
        }
        args[i] = v;
    }
    if (p->log)
        pr_log_write(p->log, PR_LOG_INFO, "win32", "[WIN32] API %s!%s",
                     e->module, e->name);
    uint64_t ret = 0;
    pr_status st = pr_win32_call_entry(p->w32, e, args, nargs, &ret);
    p->api_call_count++;

    pr_cpu_set_reg(cpu, PR_REG_EAX, (uint32_t)(ret & 0xFFFFFFFFu));
    pr_cpu_set_reg(cpu, PR_REG_EDX, (uint32_t)(ret >> 32));

    if (pr_win32_halted(p->w32)) {
        /* ExitProcess/TerminateProcess: o processo termina aqui */
        p->exit_code = pr_win32_exit_code(p->w32);
        p->stop = PR_PEPROC_STOP_EXIT;
        snprintf(p->diag, sizeof(p->diag),
                 "PROCESS EXIT\n\nReason:\nExitProcess\n\nModule:\n%s\n\nFunction:\n%s\n\n"
                 "Address:\n0x%08X\n\nArchitecture:\n%s\n\nExit code:\n%u",
                 e->module, e->name, call_site, proc_arch(p), p->exit_code);
        if (p->log)
            pr_log_write(p->log, PR_LOG_INFO, "proc", "[PROCESS] exit code %u after %.2f ms", p->exit_code, proc_ms(p));
        return 1;
    }
    if (st == PR_ERR_UNSUPPORTED) {
        diag_set(p, PR_PEPROC_STOP_API_UNSUPPORTED,
                 "Unsupported Win32 API", e->module, e->name,
                 pr_win32_last_detail(p->w32)
                     ? pr_win32_last_detail(p->w32)
                     : "a API está catalogada mas não implementada neste build",
                 call_site);
        return 1;
    }
    return 0; /* o stub segue para `ret N` (limpeza stdcall real) */
}

static int proc_api_trap64(void* ud, pr_cpu64* cpu, uint8_t vector) {
    pr_peproc* p = (pr_peproc*)ud;
    uint64_t rsp = pr_cpu64_reg(cpu, PR_R64_RSP);
    uint64_t call_site64 = 0;
    if (pr_vm_read(p->vm, (uint32_t)rsp, &call_site64, 8) != PR_OK) call_site64 = 0;
    uint32_t call_site = (uint32_t)call_site64;
    if (vector == PROC_CALL_VECTOR) {
        /* sentinel de pr_peproc_call: marca fim e CONTINUA (return 0) — o HLT
         * seguinte no stub sentinela encerra o sub-run com PR_OK e RAX intacto */
        p->call_done = 1;
        return 0;
    }
    if (vector != PROC_API_VECTOR) {
        char tech[64];
        snprintf(tech, sizeof(tech), "vector INT 0x%02X", vector);
        diag_set(p, PR_PEPROC_STOP_BAD_INSN,
                 "Unsupported interrupt", "", "", tech, (uint32_t)pr_cpu64_rip(cpu));
        pr_cpu64_set_reg(cpu, PR_R64_RAX, 0);   /* erro: NUNCA sucesso falso */
        return 1;
    }
    size_t idx = (size_t)(pr_cpu64_reg(cpu, PR_R64_RAX) & 0xFFFFFFFFu);
    const pr_win32_export* catalog = NULL;
    size_t ncat = pr_win32_catalog(&catalog);
    if (idx >= ncat) {
        diag_set(p, PR_PEPROC_STOP_API_UNSUPPORTED,
                 "Unsupported Win32 API", "(despacho)", "(índice inválido)", "",
                 call_site);
        pr_cpu64_set_reg(cpu, PR_R64_RAX, 0);
        return 1;
    }
    const pr_win32_export* e = &catalog[idx];
    if (e->status != PR_WIN32_IMPLEMENTED || !e->fn) {
        diag_set(p, PR_PEPROC_STOP_API_UNSUPPORTED,
                 "Unsupported Win32 API", e->module, e->name, e->note, call_site);
        pr_cpu64_set_reg(cpu, PR_R64_RAX, 0);
        return 1;
    }

    size_t nargs = e->stdcall_bytes / 4;
    uint64_t args[12];
    if (nargs > 12) nargs = 12;
    static const int argreg[4] = { PR_R64_RCX, PR_R64_RDX, 8, 9 };
    for (size_t i = 0; i < nargs; i++) {
        uint64_t v = 0;
        if (i < 4) {
            if ((e->arg_xmm >> i) & 1u) {
                /* ABI x64: argumentos float/double vêm em XMM0..3 */
                uint8_t xb[16];
                pr_cpu64_xmm(cpu, (int)i, xb);
                memcpy(&v, xb, 8);
            } else {
                v = pr_cpu64_reg(cpu, argreg[i]);
            }
        } else if (pr_vm_read(p->vm, (uint32_t)(rsp + 0x28 + (i - 4) * 8),
                              &v, 8) != PR_OK) {
            diag_set(p, PR_PEPROC_STOP_BAD_MEMORY,
                     "Invalid memory access (argumentos da API)",
                     e->module, e->name, "", call_site);
            pr_cpu64_set_reg(cpu, PR_R64_RAX, 0);
            return 1;
        }
        args[i] = v;
    }
    if (p->log)
        pr_log_write(p->log, PR_LOG_INFO, "win32", "[WIN32] API %s!%s",
                     e->module, e->name);
    uint64_t ret = 0;
    pr_status st = pr_win32_call_entry(p->w32, e, args, nargs, &ret);
    p->api_call_count++;
    pr_cpu64_set_reg(cpu, PR_R64_RAX, ret);

    if (pr_win32_halted(p->w32)) {
        p->exit_code = pr_win32_exit_code(p->w32);
        p->stop = PR_PEPROC_STOP_EXIT;
        snprintf(p->diag, sizeof(p->diag),
                 "PROCESS EXIT\n\nReason:\nExitProcess\n\nModule:\n%s\n\nFunction:\n%s\n\n"
                 "Address:\n0x%08X\n\nArchitecture:\n%s\n\nExit code:\n%u",
                 e->module, e->name, call_site, proc_arch(p), p->exit_code);
        if (p->log)
            pr_log_write(p->log, PR_LOG_INFO, "proc", "[PROCESS] exit code %u after %.2f ms", p->exit_code, proc_ms(p));
        return 1;
    }
    if (st == PR_ERR_UNSUPPORTED) {
        diag_set(p, PR_PEPROC_STOP_API_UNSUPPORTED,
                 "Unsupported Win32 API", e->module, e->name,
                 pr_win32_last_detail(p->w32)
                     ? pr_win32_last_detail(p->w32)
                     : "a API está catalogada mas não implementada neste build",
                 call_site);
        return 1;
    }
    return 0;
}

/* ---- FASE 6: módulos (DLLs PE32+) e chamadas ao convidado ---- */

static void mod_canon(const char* in, char* out, size_t cap) {
    size_t start = 0, i, o = 0;
    for (i = 0; in[i]; i++) if (in[i] == '\\' || in[i] == '/') start = i + 1;
    for (i = start; in[i] && o + 1 < cap; i++) {
        char ch = in[i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        out[o++] = ch;
    }
    out[o] = 0;
    if (o < 4 || strcmp(out + o - 4, ".dll") != 0) {
        if (o + 5 <= cap) memcpy(out + o, ".dll", 5);
    }
}

static proc_mod* mod_find(pr_peproc* p, const char* name) {
    char c[64];
    if (!name || !name[0]) return NULL;
    mod_canon(name, c, sizeof(c));
    for (size_t i = 0; i < p->nmods; i++)
        if (!strcmp(p->mods[i].name, c)) return &p->mods[i];
    return NULL;
}

static proc_mod* mod_by_base(pr_peproc* p, uint64_t base) {
    for (size_t i = 0; i < p->nmods; i++)
        if (p->mods[i].mapped && p->mods[i].base == base) return &p->mods[i];
    return NULL;
}

pr_status pr_peproc_provide_dll(pr_peproc* p, const char* name,
                                const void* dll_data, size_t dll_len) {
    if (!p || !name || !dll_data || dll_len < 0x200) return PR_ERR_INVALID;
    proc_mod* m = mod_find(p, name);
    int fresh = 0;
    if (!m) {
        if (p->nmods >= 8) return PR_ERR_NOMEM;
        m = &p->mods[p->nmods++];
        memset(m, 0, sizeof(*m));
        mod_canon(name, m->name, sizeof(m->name));
        fresh = 1;
    } else {
        free(m->data);
        m->data = NULL;
    }
    m->data = (uint8_t*)malloc(dll_len);
    if (!m->data) {
        if (fresh) p->nmods--;
        return PR_ERR_NOMEM;
    }
    memcpy(m->data, dll_data, dll_len);
    m->len = dll_len;
    m->refs = 0; m->base = 0; m->mapped = 0; m->entry_rva = 0;
    if (p->log)
        pr_log_write(p->log, PR_LOG_INFO, "mem",
                     "[MEM] dll provided %s len=%u (sandbox do app)",
                     m->name, (unsigned)dll_len);
    return PR_OK;
}

uint64_t pr_peproc_module_proc(pr_peproc* p, uint64_t handle, const char* name,
                               uint32_t ordinal) {
    if (!p) return 0;
    proc_mod* m = mod_by_base(p, handle);
    if (!m) return 0;
    pr_pe_export ex[64];
    size_t total = 0;
    size_t n = pr_pe_exports(m->data, m->len, ex, 64, &total);
    for (size_t i = 0; i < n; i++) {
        if (name && name[0]) {
            if (ex[i].by_name && !strcmp(ex[i].name, name))
                return m->base + ex[i].rva;
        } else if (ex[i].ordinal == (uint16_t)ordinal) {
            return m->base + ex[i].rva;
        }
    }
    return 0;
}

uint64_t pr_peproc_module_handle(pr_peproc* p, const char* name) {
    if (!p) return 0;
    proc_mod* m = mod_find(p, name);
    return (m && m->mapped) ? m->base : 0;
}

pr_status pr_peproc_free_dll(pr_peproc* p, uint64_t handle) {
    if (!p) return PR_ERR_INVALID;
    proc_mod* m = mod_by_base(p, handle);
    if (!m) return PR_ERR_INVALID;
    if (m->refs > 0) m->refs--;
    if (m->refs == 0 && m->mapped) {
        if (m->entry_rva) {
            uint64_t args[4] = { handle, 0, 0, 0 };  /* DLL_PROCESS_DETACH */
            uint64_t ret = 0;
            pr_peproc_call(p, m->base + m->entry_rva, args, &ret);
        }
        pr_vm_unmap(p->vm, (uint32_t)m->base, m->span);
        if (p->log)
            pr_log_write(p->log, PR_LOG_INFO, "proc",
                         "[PROCESS] dll unloaded %s base=0x%llX",
                         m->name, (unsigned long long)m->base);
        m->mapped = 0;
        m->base = 0;
    }
    return PR_OK;
}

pr_status pr_peproc_call(pr_peproc* p, uint64_t fn, const uint64_t args[4],
                         uint64_t* out_ret) {
    if (!p || !p->cpu64 || !fn) return PR_ERR_INVALID;
    pr_cpu64* c = p->cpu64;
    uint64_t save_r[16];
    for (int i = 0; i < 16; i++) save_r[i] = pr_cpu64_reg(c, i);
    uint64_t save_rip = pr_cpu64_rip(c);
    uint64_t save_rf = pr_cpu64_rflags(c);
    uint64_t save_rsp = pr_cpu64_reg(c, PR_R64_RSP);
    pr_peproc_stop save_stop = p->stop;

    /* frame Microsoft x64: [rsp]=sentinel, 32 bytes de shadow acima */
    uint64_t rsp = (save_rsp - 0x400) & ~0xFull;
    uint64_t frame = rsp - 0x28;
    uint64_t sentinel = PROC_CALL_SENTINEL;
    if (pr_vm_write(p->vm, (uint32_t)frame, &sentinel, 8) != PR_OK) return PR_ERR_FAULT;
    pr_cpu64_set_reg(c, PR_R64_RSP, frame);
    pr_cpu64_set_reg(c, PR_R64_RCX, args ? args[0] : 0);
    pr_cpu64_set_reg(c, PR_R64_RDX, args ? args[1] : 0);
    pr_cpu64_set_reg(c, 8,  args ? args[2] : 0);
    pr_cpu64_set_reg(c, 9,  args ? args[3] : 0);
    pr_cpu64_set_reg(c, PR_R64_RAX, 0);
    pr_cpu64_set_rip(c, fn);

    p->call_done = 0;
    uint64_t exec = 0;
    pr_status st = pr_cpu64_run(c, 1000000, &exec);

    int ok = (st == PR_OK && p->call_done);
    if (ok && out_ret) *out_ret = pr_cpu64_reg(c, PR_R64_RAX);
    for (int i = 0; i < 16; i++) pr_cpu64_set_reg(c, i, save_r[i]);
    pr_cpu64_set_reg(c, PR_R64_RSP, save_rsp);
    pr_cpu64_set_rip(c, save_rip);
    pr_cpu64_set_rflags(c, save_rf);
    /* limpa halted/fault do sub-run (o HLT do sentinel deixaria halted=1 e o
     * run EXTERNO — este call pode estar aninhado num trap de API — abortaria
     * como PROCESS EXIT). pr_cpu64_run(budget=0) zera o estado e não executa. */
    pr_cpu64_run(c, 0, NULL);
    p->stop = save_stop;
    if (ok) return PR_OK;
    return st != PR_OK ? st : PR_ERR_FAULT;
}

/* reentrada do convidado a partir de APIs do CRT (_initterm/_onexit) */
static uint64_t proc_guest_call(void* ud, uint64_t fn, const uint64_t args[4],
                                uint64_t* out_ret) {
    return pr_peproc_call((pr_peproc*)ud, fn, args, out_ret) == PR_OK ? 1 : 0;
}

/* callbacks do modops (Win32 -> peproc) */
static uint64_t mod_load_cb(void* ud, const char* name) {
    uint64_t h = 0;
    return pr_peproc_load_dll((pr_peproc*)ud, name, &h) == PR_OK ? h : 0;
}
static uint64_t mod_find_cb(void* ud, const char* name) {
    return pr_peproc_module_handle((pr_peproc*)ud, name);
}
static uint64_t mod_proc_cb(void* ud, uint64_t handle, const char* name,
                            uint32_t ordinal) {
    return pr_peproc_module_proc((pr_peproc*)ud, handle, name, ordinal);
}
static int mod_free_cb(void* ud, uint64_t handle) {
    return pr_peproc_free_dll((pr_peproc*)ud, handle) == PR_OK;
}

pr_status pr_peproc_set_fs_root(pr_peproc* p, const char* host_dir) {
    if (!p || !p->w32) return PR_ERR_INVALID;
    return pr_win32_set_fs_root(p->w32, host_dir);
}

pr_status pr_peproc_load_dll(pr_peproc* p, const char* name, uint64_t* out_handle) {
    if (!p || !name || !out_handle) return PR_ERR_INVALID;
    *out_handle = 0;
    proc_mod* m = mod_find(p, name);
    if (!m || !m->data) {
        if (p->log)
            pr_log_write(p->log, PR_LOG_WARN, "proc",
                         "[PROCESS] LoadLibrary %s: DLL nao fornecida pelo app (sandbox)",
                         name);
        return PR_ERR_UNSUPPORTED;
    }
    if (m->mapped) { m->refs++; *out_handle = m->base; return PR_OK; }

    pr_pe_loaded img;
    if (pr_pe_load(m->data, m->len, &img, m->name) != PR_OK) {
        if (p->log)
            pr_log_write(p->log, PR_LOG_ERROR, "proc",
                         "[PROCESS] %s: imagem DLL invalida (pr_pe_load)", m->name);
        return PR_ERR_FORMAT;
    }
    if (!img.is_dll || !img.is_pe32plus || img.machine != 0x8664) {
        if (p->log)
            pr_log_write(p->log, PR_LOG_WARN, "proc",
                         "[PROCESS] %s nao e DLL PE32+ x64 (is_dll=%d pe32plus=%d machine=0x%X)",
                         m->name, img.is_dll, img.is_pe32plus, img.machine);
        pr_pe_loaded_free(&img);
        return PR_ERR_UNSUPPORTED;
    }
    uint32_t span = (uint32_t)((img.image_size + 0xFFF) & ~0xFFFu);
    uint32_t entry = img.entry_rva;
    uint32_t npages = span / 0x1000;
    uint32_t base = 0;
    if (npages == 0 || npages > 64 || pr_vm_find_gap(p->vm, span, &base) != PR_OK) {
        pr_pe_loaded_free(&img);
        return PR_ERR_NOMEM;
    }

    /* prot por página (flags COFF; headers = R) — mesmo critério da imagem */
    typedef struct { uint32_t pg0, npg; int prot; } page_group;
    page_group groups[64];
    size_t ngroups = 0;
    for (uint32_t pg = 0; pg < npages && ngroups < 64; ) {
        uint32_t rva = pg * 0x1000;
        int prot = 0;
        for (uint32_t i = 0; i < img.section_count; i++) {
            uint32_t sva = img.sections[i].virtual_addr;
            uint32_t spn = img.sections[i].virtual_size > img.sections[i].raw_size
                         ? img.sections[i].virtual_size : img.sections[i].raw_size;
            if (spn == 0) continue;
            if (rva + 0x1000 <= sva || sva + spn <= rva) continue;
            uint32_t ch = img.sections[i].characteristics;
            if (ch & 0x40000000u) prot |= PR_VM_PROT_R;
            if (ch & 0x80000000u) prot |= PR_VM_PROT_W;
            if (ch & 0x20000000u) prot |= PR_VM_PROT_X;
        }
        prot = prot ? prot : PR_VM_PROT_R;
        uint32_t g = pg + 1;
        while (g < npages) {
            uint32_t rva2 = g * 0x1000;
            int prot2 = 0;
            for (uint32_t i = 0; i < img.section_count; i++) {
                uint32_t sva = img.sections[i].virtual_addr;
                uint32_t spn = img.sections[i].virtual_size > img.sections[i].raw_size
                             ? img.sections[i].virtual_size : img.sections[i].raw_size;
                if (spn == 0) continue;
                if (rva2 + 0x1000 <= sva || sva + spn <= rva2) continue;
                uint32_t ch = img.sections[i].characteristics;
                if (ch & 0x40000000u) prot2 |= PR_VM_PROT_R;
                if (ch & 0x80000000u) prot2 |= PR_VM_PROT_W;
                if (ch & 0x20000000u) prot2 |= PR_VM_PROT_X;
            }
            if ((prot2 ? prot2 : PR_VM_PROT_R) != prot) break;
            g++;
        }
        groups[ngroups].pg0 = pg;
        groups[ngroups].npg = g - pg;
        groups[ngroups].prot = prot;
        ngroups++;
        pg = g;
    }
    size_t mapped = 0;
    for (; mapped < ngroups; mapped++) {
        page_group* gr = &groups[mapped];
        if (pr_vm_map(p->vm, base + gr->pg0 * 0x1000, gr->npg * 0x1000,
                      gr->prot, "dll", NULL) != PR_OK) break;
    }
    if (mapped != ngroups) {
        for (size_t i = 0; i < mapped; i++)
            pr_vm_unmap(p->vm, base + groups[i].pg0 * 0x1000, groups[i].npg * 0x1000);
        pr_pe_loaded_free(&img);
        return PR_ERR_NOMEM;
    }

    /* relocations (obrigatórias: base escolhida por gap) */
    char why[96] = { 0 };
    if (pr_pe_reloc_apply(m->data, m->len, img.image, img.image_size,
                          img.image_base64, base, why, sizeof(why)) != PR_OK) {
        for (size_t i = 0; i < ngroups; i++)
            pr_vm_unmap(p->vm, base + groups[i].pg0 * 0x1000, groups[i].npg * 0x1000);
        if (p->log)
            pr_log_write(p->log, PR_LOG_ERROR, "proc",
                         "[PROCESS] %s: relocations invalidas (%s)",
                         m->name, why[0] ? why : "sem detalhe");
        pr_pe_loaded_free(&img);
        return PR_ERR_FORMAT;
    }
    /* copia imagem (loader write, como a imagem principal) */
    for (uint32_t off = 0; off < img.image_size; off += 0x1000) {
        uint32_t n = (img.image_size - off) < 0x1000
                   ? (uint32_t)(img.image_size - off) : 0x1000u;
        if (pr_vm_loader_write(p->vm, base + off, img.image + off, n) != PR_OK) {
            for (size_t i = 0; i < ngroups; i++)
                pr_vm_unmap(p->vm, base + groups[i].pg0 * 0x1000, groups[i].npg * 0x1000);
            pr_pe_loaded_free(&img);
            return PR_ERR_FAULT;
        }
    }

    /* imports da DLL: embutidos -> thunks; módulos carregados -> exports */
    for (size_t di = 0; ; di++) {
        char dll[64];
        if (pr_pe_import_name(m->data, m->len, di, dll, sizeof(dll)) != PR_OK) break;
        size_t nf = pr_pe_import_func_count(m->data, m->len, di);
        proc_mod* dep = mod_find(p, dll);
        for (size_t fi = 0; fi < nf; fi++) {
            pr_pe_import_func fn;
            if (pr_pe_import_func_at(m->data, m->len, di, fi, &fn) != PR_OK) {
                pr_pe_loaded_free(&img);
                return PR_ERR_FORMAT;
            }
            uint64_t target = 0;
            const pr_win32_export* ee = fn.by_ordinal
                ? pr_win32_lookup_ordinal(dll, fn.ordinal)
                : pr_win32_lookup(dll, fn.name);
            if (ee && (ee->status == PR_WIN32_IMPLEMENTED ||
                       ee->status == PR_WIN32_UNSUPPORTED)) {
                size_t idx = pr_win32_index_of(ee);
                target = (uint64_t)p->bind_addrs[idx];
            } else if (dep && dep->mapped) {
                target = pr_peproc_module_proc(p, dep->base,
                                               fn.by_ordinal ? NULL : fn.name,
                                               fn.ordinal);
            }
            if (!target) {
                if (p->log)
                    pr_log_write(p->log, PR_LOG_ERROR, "proc",
                                 "[PROCESS] %s importa %s!%s — sem resolucao (honesto)",
                                 m->name, dll,
                                 fn.by_ordinal ? "(ordinal)" : fn.name);
                for (size_t i = 0; i < ngroups; i++)
                    pr_vm_unmap(p->vm, base + groups[i].pg0 * 0x1000, groups[i].npg * 0x1000);
                pr_pe_loaded_free(&img);
                return PR_ERR_UNSUPPORTED;
            }
            uint32_t iat_rva = 0;
            if (pr_pe_import_iat_rva(m->data, m->len, di, fi, &iat_rva) != PR_OK) {
                pr_pe_loaded_free(&img);
                return PR_ERR_FORMAT;
            }
            uint64_t slot = target;
            pr_vm_loader_write(p->vm, base + iat_rva, &slot, 8);
        }
    }

    m->base = base;
    m->span = span;
    m->entry_rva = entry;
    m->mapped = 1;
    m->refs = 1;
    pr_pe_loaded_free(&img);
    *out_handle = base;
    if (p->log)
        pr_log_write(p->log, PR_LOG_INFO, "mem",
                     "[MEM] dll mapped %s base=0x%08X size=0x%X",
                     m->name, base, span);

    /* DllMain(hinst, DLL_PROCESS_ATTACH, NULL) — retorno FALSE = carga falha */
    if (entry) {
        uint64_t args[4] = { base, 1, 0, 0 };
        uint64_t ret = 0;
        pr_status cs = pr_peproc_call(p, base + entry, args, &ret);
        if (cs != PR_OK || ret == 0) {
            if (p->log)
                pr_log_write(p->log, PR_LOG_ERROR, "proc",
                             "[PROCESS] DllMain %s recusou o attach (ret=%llu st=%d)",
                             m->name, (unsigned long long)ret, (int)cs);
            pr_peproc_free_dll(p, base);
            return PR_ERR_UNSUPPORTED;
        }
        if (p->log)
            pr_log_write(p->log, PR_LOG_INFO, "proc",
                         "[PROCESS] DllMain %s attach ret=%llu",
                         m->name, (unsigned long long)ret);
    }
    return PR_OK;
}

static int proc_guard64(void* ud, uint64_t addr, size_t len, int acc) {
    if (addr + len < addr || addr + len > PROC_SPACE) return 0;
    return proc_guard(ud, (uint32_t)addr, len, acc);
}

/* ---- criação / carga ---- */

pr_status pr_peproc_create(const void* pe_data, size_t pe_len, pr_log* log,
                           pr_peproc** out) {
    if (!pe_data || !out) return PR_ERR_INVALID;
    pr_peproc* p = (pr_peproc*)calloc(1, sizeof(pr_peproc));
    if (!p) return PR_ERR_NOMEM;
    p->log = log;
    *out = p;

    /* guarda os bytes do arquivo (parser de imports/relocations) */
    p->file = (uint8_t*)malloc(pe_len);
    if (!p->file) return PR_ERR_NOMEM;
    memcpy(p->file, pe_data, pe_len);
    p->file_len = pe_len;

    pr_status st = pr_pe_load(p->file, p->file_len, &p->img, "app.exe");
    if (st != PR_OK) {
        diag_set(p, PR_PEPROC_STOP_BAD_EXECUTABLE,
                 "Invalid executable (carga PE falhou)", "", "",
                 pr_status_str(st), UINT64_MAX);
        return st == PR_ERR_RANGE ? st : PR_ERR_FORMAT;
    }
    p->img_loaded = 1;

    /* Arquiteturas aceitas na carga: PE32 i386 (executado) e PE32+ x86-64
     * (carregado; execução exige backend de CPU x64 — recusada no step). */
    int is_x86 = (p->img.machine == 0x014C && !p->img.is_pe32plus);
    int is_x64 = (p->img.machine == 0x8664 && p->img.is_pe32plus);
    if (!is_x86 && !is_x64) {
        char tech[96];
        snprintf(tech, sizeof(tech), "machine=0x%04X magic=%s",
                 p->img.machine, p->img.is_pe32plus ? "PE32+" : "PE32");
        diag_set(p, PR_PEPROC_STOP_BAD_EXECUTABLE,
                 "Unsupported executable (arquitetura)", p->img.module_name, "",
                 tech, UINT64_MAX);
        return PR_ERR_UNSUPPORTED;
    }

    p->cpu = pr_cpu_create(PROC_SPACE);
    /* VM por cima da memória física da CPU: mesmos bytes, permissões no guard */
    p->vm = pr_vm_create_on(PROC_SPACE, p->cpu ? pr_cpu_mem(p->cpu) : NULL);
    p->w32 = pr_win32_create(log);
    if (!p->vm || !p->cpu || !p->w32) return PR_ERR_NOMEM;

    /* pilha */
    st = pr_vm_map(p->vm, PROC_STACK_TOP - PROC_STACK_SZ, PROC_STACK_SZ,
                   PR_VM_PROT_R | PR_VM_PROT_W, "stack", NULL);
    if (st != PR_OK) return st;
    pr_cpu_set_reg(p->cpu, PR_REG_ESP, PROC_STACK_TOP);
    if (log)
        pr_log_write(log, PR_LOG_INFO, "mem",
                     "[MEM] stack mapped 0x%08X..0x%08X prot=rw",
                     (uint32_t)(PROC_STACK_TOP - PROC_STACK_SZ), PROC_STACK_TOP - 1);

    /* SEH/unwind: indexa .pdata (RUNTIME_FUNCTION) + valida UNWIND_INFO */
    {
        uint32_t exr = 0, exs = 0;
        if (pr_pe_data_dir(pe_data, pe_len, 3, &exr, &exs) == PR_OK && exr) {
            size_t cnt = 0;
            pr_status us = pr_unwind_index(p->img.image, p->img.image_size,
                                           exr, exs, p->unw, 1024, &cnt);
            if (us == PR_OK) {
                p->nunw = cnt;
                /* valida a interpretação de cada UNWIND_INFO agora
                 * (falha honesta no prepare = problema do binário) */
                for (size_t i = 0; i < cnt; i++) {
                    pr_unwind_info ui;
                    if (pr_unwind_parse(p->img.image, p->img.image_size,
                                        p->unw[i].unwind_rva, &ui) != PR_OK) {
                        diag_set(p, PR_PEPROC_STOP_BAD_EXECUTABLE,
                                 "Invalid executable (.pdata/.xdata)", p->img.module_name,
                                 "UNWIND_INFO",
                                 "unwind info nao interpretavel neste runtime",
                                 p->unw[i].unwind_rva);
                        return PR_ERR_FORMAT;
                    }
                }
                if (log)
                    pr_log_write(log, PR_LOG_INFO, "pe",
                                 "[PE] .pdata: %u funcoes RUNTIME_FUNCTION indexadas",
                                 (unsigned)cnt);
            }
        }
    }

    /* TEB + PEB mínimos (x64 Windows): gs:[0x30] -> PEB; campos documentados.
     * O CRT real do MinGW lê o PEB no startup; cada campo ausente depois
     * destes gera EXECUTION STOPPED honesto e é implementado sob demanda. */
    {
        void* teb_host = NULL;
        void* peb_host = NULL;
        const uint32_t teb_addr = PROC_TEB_BASE, peb_addr = PROC_TEB_BASE + 0x1000u;
        if (pr_vm_map(p->vm, teb_addr, 0x1000, PR_VM_PROT_R | PR_VM_PROT_W,
                      "teb", &teb_host) != PR_OK ||
            pr_vm_map(p->vm, peb_addr, 0x1000, PR_VM_PROT_R | PR_VM_PROT_W,
                      "peb", &peb_host) != PR_OK)
            return PR_ERR_NOMEM;
        memset(teb_host, 0, 0x1000);
        memset(peb_host, 0, 0x1000);
        /* NT_TIB (x64): +0x30 = Self (TEB*); PEB* em +0x60 */
        *(uint64_t*)((uint8_t*)teb_host + 0x00) = 0;               /* ExceptionList */
        *(uint64_t*)((uint8_t*)teb_host + 0x08) = PROC_STACK_TOP;  /* StackBase */
        *(uint64_t*)((uint8_t*)teb_host + 0x10) =
            PROC_STACK_TOP - PROC_STACK_SZ;                        /* StackLimit */
        *(uint64_t*)((uint8_t*)teb_host + 0x30) = teb_addr;        /* Self */
        *(uint64_t*)((uint8_t*)teb_host + 0x60) = peb_addr;        /* PEB* */
        *(uint8_t*)((uint8_t*)peb_host + 0x02) = 0;                /* BeingDebugged */
        *(uint64_t*)((uint8_t*)peb_host + 0x10) = p->load_base;    /* ImageBaseAddress */
        if (p->cpu64) pr_cpu64_set_gs_base(p->cpu64, teb_addr);
        if (log)
            pr_log_write(log, PR_LOG_INFO, "mem",
                         "[MEM] TEB/PEB mapped teb=0x%08X peb=0x%08X gs_base=0x%08X",
                         teb_addr, peb_addr, teb_addr);
    }

    /* páginas de thunks (stubs stdcall reais) — G88 expande para 1024 */
    void* stub_host = NULL;
    st = pr_vm_map(p->vm, PROC_STUB_BASE, PROC_STUB_SIZE,
                   PR_VM_PROT_R | PR_VM_PROT_W, "stubs", &stub_host);
    if (st != PR_OK) return st;

    const pr_win32_export* catalog = NULL;
    size_t ncat = pr_win32_catalog(&catalog);
    if (ncat > PROC_STUB_MAX) ncat = PROC_STUB_MAX;
    uint32_t thunk_addrs[1024];
    uint8_t* s = (uint8_t*)stub_host;
    /* símbolos de dados (DATA_SYM): células RW de 16B numa página própria;
     * o slot IAT recebe o endereço da célula (não um thunk de código) */
    {
        void* data_host = NULL;
        if (pr_vm_map(p->vm, PROC_DATA_BASE, 0x1000,
                      PR_VM_PROT_R | PR_VM_PROT_W, "win32-data", &data_host) == PR_OK) {
            if (data_host) memset(data_host, 0, 0x1000);
        }
    }
    size_t data_cell = 0;
    for (size_t i = 0; i < ncat; i++) {
        if (catalog[i].is_data) {
            p->bind_addrs[i] = PROC_DATA_BASE + (uint32_t)(data_cell * 16);
            data_cell++;
            continue;
        }
        uint8_t* b = s + i * PROC_STUB_STRIDE;
        size_t o = 0;
        b[o++] = 0xB8;                             /* mov eax, imm32 */
        uint32_t id = (uint32_t)i;
        memcpy(b + o, &id, 4); o += 4;
        b[o++] = 0xCD; b[o++] = PROC_API_VECTOR;   /* int 0x2E */
        if (p->img.is_pe32plus) {
            b[o++] = 0xC3;                         /* ret (caller cleanup x64) */
        } else {
            b[o++] = 0xC2;                         /* ret imm16 (stdcall) */
            uint16_t nb = catalog[i].stdcall_bytes;
            memcpy(b + o, &nb, 2); o += 2;
        }
        thunk_addrs[i] = PROC_STUB_BASE + (uint32_t)(i * PROC_STUB_STRIDE);
        p->bind_addrs[i] = thunk_addrs[i];
    }
    /* W^X: as páginas de código ficam RX após a escrita */
    pr_vm_protect(p->vm, PROC_STUB_BASE, PROC_STUB_SIZE, PR_VM_PROT_R | PR_VM_PROT_X);
    if (log)
        pr_log_write(log, PR_LOG_INFO, "mem",
                     "[MEM] api stubs 0x%08X..0x%08X prot=rx count=%u",
                     (uint32_t)PROC_STUB_BASE, (uint32_t)(PROC_STUB_BASE + PROC_STUB_SIZE -1),
                     (unsigned)ncat);
    pr_win32_bind_thunks(p->w32, thunk_addrs, ncat);
    /* sentinel de pr_peproc_call (INT 0x2F; HLT) após os thunks:
     * o trap marca call_done e retorna 0 (continua); o HLT seguinte encerra o
     * sub-run com PR_OK (convenção real de pr_cpu64_run: halted -> PR_OK). */
    {
        uint32_t sentinel_off = PROC_STUB_MAX * PROC_STUB_STRIDE + 0xFF0u;
        if (sentinel_off + 3 <= PROC_STUB_SIZE) {
            s[sentinel_off] = 0xCD; s[sentinel_off+1] = PROC_CALL_VECTOR; s[sentinel_off+2] = 0xF4;
        }
    }
    /* modops: LoadLibrary/GetProcAddress/FreeLibrary -> módulos reais */
    p->modops.load = mod_load_cb;
    p->modops.find = mod_find_cb;
    p->modops.proc = mod_proc_cb;
    p->modops.free = mod_free_cb;
    p->modops.ud = p;
    pr_win32_set_modops(p->w32, &p->modops);
    pr_win32_set_guestcall(p->w32, proc_guest_call, p);

    /* ---- posicionamento da imagem: base preferida ou relocations ---- */
    uint64_t preferred = p->img.image_base64;
    uint32_t img_size = (uint32_t)p->img.image_size;
    uint32_t img_span = (img_size + 0xFFF) & ~0xFFFu;

    /* protótipo de prot por página (flags COFF das seções; headers = R) */
    uint32_t npages = img_span / 0x1000;
    if (npages == 0 || npages > 1024) {
        diag_set(p, PR_PEPROC_STOP_BAD_EXECUTABLE,
                 "Unsupported executable (tamanho)", p->img.module_name, "",
                 "imagem excede o espaço do processo", UINT64_MAX);
        return PR_ERR_UNSUPPORTED;
    }
    int page_prot[1024];
    for (uint32_t pg = 0; pg < npages; pg++) {
        uint32_t rva = pg * 0x1000;
        int prot = 0;
        for (uint32_t i = 0; i < p->img.section_count; i++) {
            uint32_t sva = p->img.sections[i].virtual_addr;
            uint32_t span = p->img.sections[i].virtual_size > p->img.sections[i].raw_size
                          ? p->img.sections[i].virtual_size
                          : p->img.sections[i].raw_size;
            if (span == 0) continue;
            if (rva + 0x1000 <= sva || sva + span <= rva) continue;
            uint32_t ch = p->img.sections[i].characteristics;
            if (ch & 0x40000000u) prot |= PR_VM_PROT_R;
            if (ch & 0x80000000u) prot |= PR_VM_PROT_W;
            if (ch & 0x20000000u) prot |= PR_VM_PROT_X;
        }
        page_prot[pg] = prot ? prot : PR_VM_PROT_R;
    }

    /* grupos contíguos de páginas com prot igual (regiões do VM) */
    typedef struct { uint32_t pg0, npg; int prot; } page_group;
    page_group groups[64];
    size_t ngroups = 0;
    for (uint32_t pg = 0; pg < npages && ngroups < 64; ) {
        uint32_t g = pg + 1;
        while (g < npages && page_prot[g] == page_prot[pg]) g++;
        groups[ngroups].pg0 = pg;
        groups[ngroups].npg = g - pg;
        groups[ngroups].prot = page_prot[pg];
        ngroups++;
        pg = g;
    }

    /* tenta a base preferida; senão reloca (com Basereloc válido) */
    uint32_t base = 0;
    int placed = 0;
    if (preferred != 0 && preferred <= 0xFFFFFFFFu &&
        preferred + img_span <= PROC_SPACE) {
        size_t mapped = 0;
        for (; mapped < ngroups; mapped++) {
            page_group* gr = &groups[mapped];
            if (pr_vm_map(p->vm, (uint32_t)preferred + gr->pg0 * 0x1000,
                          gr->npg * 0x1000, gr->prot, "image", NULL) != PR_OK)
                break;
        }
        if (mapped == ngroups) {
            placed = 1;
            base = (uint32_t)preferred;
        } else {
            for (size_t i = 0; i < mapped; i++)
                pr_vm_unmap(p->vm, (uint32_t)preferred + groups[i].pg0 * 0x1000,
                            groups[i].npg * 0x1000);
        }
    }
    if (!placed) {
        if (pr_vm_find_gap(p->vm, img_span, &base) != PR_OK) {
            diag_set(p, PR_PEPROC_STOP_BAD_EXECUTABLE,
                     "Unsupported executable (memória)", p->img.module_name, "",
                     "imagem não cabe no espaço do processo", UINT64_MAX);
            return PR_ERR_UNSUPPORTED;
        }
        for (size_t i = 0; i < ngroups; i++) {
            page_group* gr = &groups[i];
            st = pr_vm_map(p->vm, base + gr->pg0 * 0x1000, gr->npg * 0x1000,
                           gr->prot, "image", NULL);
            if (st != PR_OK) return st;
        }
        /* novo base exige tabela de relocations REAL (senão nada seria ajustado) */
        if (p->img.reloc_rva == 0) {
            char tech[160];
            snprintf(tech, sizeof(tech),
                     "base preferida 0x%llX indisponível e a imagem não tem "
                     "tabela de relocations",
                     (unsigned long long)preferred);
            diag_set(p, PR_PEPROC_STOP_BAD_EXECUTABLE,
                     "Unsupported executable (relocations)",
                     p->img.module_name, "", tech, UINT64_MAX);
            return PR_ERR_UNSUPPORTED;
        }
        char why[96];
        if (pr_pe_reloc_validate(p->file, p->file_len, why, sizeof(why)) != PR_OK) {
            char tech[160];
            snprintf(tech, sizeof(tech),
                     "base preferida 0x%llX indisponível e %s",
                     (unsigned long long)preferred,
                     why[0] ? why : "tabela de relocations inválida");
            diag_set(p, PR_PEPROC_STOP_BAD_EXECUTABLE,
                     "Unsupported executable (relocations)",
                     p->img.module_name, "", tech, UINT64_MAX);
            return PR_ERR_UNSUPPORTED;
        }
    }
    p->load_base = base;
    if (log)
        pr_log_write(log, PR_LOG_INFO, "mem",
                     "[MEM] image mapped base=0x%08X size=0x%X pages=%u",
                     base, img_size, (unsigned)npages);

    /* aplica relocations (valida sempre; ajusta quando delta ≠ 0) */
    {
        char why[96];
        if (pr_pe_reloc_apply(p->file, p->file_len, p->img.image,
                              p->img.image_size, preferred, base,
                              why, sizeof(why)) != PR_OK) {
            char tech[160];
            snprintf(tech, sizeof(tech), "%s", why[0] ? why
                : "tabela de relocations inválida");
            diag_set(p, PR_PEPROC_STOP_BAD_EXECUTABLE,
                     "Invalid executable (relocations)", p->img.module_name, "",
                     tech, UINT64_MAX);
            return PR_ERR_FORMAT;
        }
    }

    /* copia a imagem (já relocalizada) para os grupos mapeados */
    for (uint32_t pg = 0; pg < npages; pg++) {
        uint32_t va = base + pg * 0x1000;
        size_t n = (pg + 1) * 0x1000 <= p->img.image_size
                     ? 0x1000 : p->img.image_size - pg * 0x1000;
        if (pr_vm_loader_write(p->vm, va, p->img.image + pg * 0x1000, n) != PR_OK) {
            diag_set(p, PR_PEPROC_STOP_BAD_MEMORY,
                     "Invalid memory access (carga da imagem)",
                     p->img.module_name, "", "", va);
            return PR_ERR_FAULT;
        }
    }

    pr_win32_bind_vm(p->w32, p->vm, base, p->img.module_name);
    /* a imagem do convidado é lida via pr_vm (páginas); sem cópia host única */

    pr_cpu_set_guard(p->cpu, proc_guard, p);
    pr_cpu_set_trap(p->cpu, proc_api_trap, p);
    if (p->img.is_pe32plus) {
        p->cpu64 = pr_cpu64_create_on(pr_cpu_mem(p->cpu), PROC_SPACE);
        if (!p->cpu64) return PR_ERR_NOMEM;
        pr_cpu64_set_guard(p->cpu64, proc_guard64, p);
        pr_cpu64_set_trap(p->cpu64, proc_api_trap64, p);
        pr_cpu64_set_gs_base(p->cpu64, PROC_TEB_BASE);   /* gs = TEB */
        pr_cpu64_set_reg(p->cpu64, PR_R64_RSP, PROC_STACK_TOP);
    }

    if (p->img.entry_rva >= p->img.image_size) {
        diag_set(p, PR_PEPROC_STOP_BAD_EXECUTABLE,
                 "Invalid executable (entry point)", p->img.module_name, "",
                 "entry point fora da imagem", UINT64_MAX);
        return PR_ERR_FORMAT;
    }
    pr_cpu_set_eip(p->cpu, base + p->img.entry_rva);
    if (p->cpu64)
        pr_cpu64_set_rip(p->cpu64, (uint64_t)base + p->img.entry_rva);
    if (p->log)
        pr_log_write(p->log, PR_LOG_INFO, "pe",
                     "[PE] loaded base=0x%08X entry=0x%08X arch=%s",
                     base, base + p->img.entry_rva, proc_arch(p));
    return PR_OK;
}

void pr_peproc_destroy(pr_peproc* p) {
    if (!p) return;
    pr_cpu_destroy(p->cpu);
    pr_cpu64_destroy(p->cpu64);
    pr_win32_destroy(p->w32);
    pr_vm_destroy(p->vm);
    if (p->img_loaded) pr_pe_loaded_free(&p->img);
    free(p->file);
    free(p);
}

/* ---- preparação: resolução de imports (IAT → thunks) ---- */

pr_status pr_peproc_prepare(pr_peproc* p) {
    if (!p) return PR_ERR_INVALID;
    if (p->prepared) return PR_OK;
    if (p->stop == PR_PEPROC_STOP_BAD_EXECUTABLE) return PR_ERR_FORMAT;

    const uint8_t* file = p->file;
    size_t file_len = p->file_len;
    int slot_bytes = p->img.is_pe32plus ? 8 : 4;

    size_t dll_index = 0;
    for (;;) {
        char dll[32];
        if (pr_pe_import_name(file, file_len, dll_index, dll, sizeof(dll)) != PR_OK)
            break;
        size_t nf = pr_pe_import_func_count(file, file_len, dll_index);
        for (size_t f = 0; f < nf; f++) {
            pr_pe_import_func fn;
            if (pr_pe_import_func_at(file, file_len, dll_index, f, &fn) != PR_OK)
                continue;

            const pr_win32_export* e = NULL;
            char fn_label[80];
            if (fn.by_ordinal) {
                snprintf(fn_label, sizeof(fn_label), "#%u", (unsigned)fn.ordinal);
                e = pr_win32_lookup_ordinal(dll, fn.ordinal);
                if (e)
                    snprintf(fn_label, sizeof(fn_label), "#%u (%s)",
                             (unsigned)fn.ordinal, e->name);
            } else {
                snprintf(fn_label, sizeof(fn_label), "%s", fn.name);
                e = pr_win32_lookup(dll, fn.name);
            }

            /* DLL sem nenhuma entrada no catálogo = DLL ausente */
            int dll_known = 0;
            if (!e) {
                const pr_win32_export* catalog = NULL;
                size_t ncat = pr_win32_catalog(&catalog);
                for (size_t m = 0; m < ncat; m++) {
                    if (pr_win32_lookup(dll, catalog[m].name)) { dll_known = 1; break; }
                }
            } else {
                dll_known = 1;
            }
            if (!dll_known) {
                diag_set(p, PR_PEPROC_STOP_MISSING_DLL,
                         "Missing DLL (import not resolvable)", dll, fn_label,
                         "a DLL não faz parte do conjunto suportado neste build",
                         UINT64_MAX);
                return PR_ERR_UNSUPPORTED;
            }
            if (!e) {
                diag_set(p, PR_PEPROC_STOP_MISSING_API,
                         fn.by_ordinal
                             ? "Unsupported Win32 API (por ordinal)"
                             : "Unsupported Win32 API",
                         dll, fn_label,
                         fn.by_ordinal
                             ? "ordinal sem correspondência pública neste runtime "
                               "(nunca se adivinha ordinais)"
                             : "API conhecida do módulo mas sem implementação",
                         UINT64_MAX);
                return PR_ERR_UNSUPPORTED;
            }
            if (e->is_data) {
                if (p->log)
                    pr_log_write(p->log, PR_LOG_INFO, "win32",
                                 "[WIN32] import de dados %s!%s -> celula RW 0x%08X",
                                 dll, fn_label,
                                 p->bind_addrs[pr_win32_index_of(e)]);
            } else if (e->status == PR_WIN32_UNSUPPORTED) {
                /* catalogada sem implementação real: liga ao stub de
                 * diagnóstico — a CHAMADA vira EXECUTION STOPPED honesto
                 * (Windows real também só falharia ao invocar o corpo) */
                if (p->log)
                    pr_log_write(p->log, PR_LOG_WARN, "win32",
                                 "[WIN32] import %s!%s SEM implementação real — stub de "
                                 "diagnóstico (chamada = EXECUTION STOPPED)",
                                 dll, fn_label);
            } else if (e->status != PR_WIN32_IMPLEMENTED || !e->fn) {
                diag_set(p, PR_PEPROC_STOP_MISSING_API,
                         "Unsupported Win32 API", dll, fn_label,
                         e->note ? e->note : "catalogada, sem implementação",
                         UINT64_MAX);
                return PR_ERR_UNSUPPORTED;
            }

            /* grava o thunk no slot IAT real (largura 4/8 conforme o formato) */
            uint32_t iat_rva = 0;
            if (pr_pe_import_iat_rva(file, file_len, dll_index, f, &iat_rva) != PR_OK) {
                diag_set(p, PR_PEPROC_STOP_BAD_EXECUTABLE,
                         "Invalid executable (IAT)", dll, fn_label, "", UINT64_MAX);
                return PR_ERR_FORMAT;
            }
            size_t idx = pr_win32_index_of(e);
            uint32_t thunk = p->bind_addrs[idx];
            uint32_t slot_va = p->load_base + iat_rva;
            uint8_t slot[8] = {0};
            memcpy(slot, &thunk, 4);
            if (pr_vm_loader_write(p->vm, slot_va, slot, (size_t)slot_bytes) != PR_OK) {
                diag_set(p, PR_PEPROC_STOP_BAD_MEMORY,
                         "Invalid memory access (IAT)", dll, fn_label, "",
                         slot_va);
                return PR_ERR_FAULT;
            }
        }
        dll_index++;
    }

    /* sentinela de retorno do entry (x64: slot 8B; ret cai em PROC_SENTINEL) */
    if (p->img.is_pe32plus) {
        uint64_t rsp64 = pr_cpu64_reg(p->cpu64, PR_R64_RSP) - 8;
        pr_cpu64_set_reg(p->cpu64, PR_R64_RSP, rsp64);
        uint64_t sentinel = PROC_SENTINEL;
        pr_vm_write(p->vm, (uint32_t)rsp64, &sentinel, 8);
    } else {
        uint32_t esp = pr_cpu_reg(p->cpu, PR_REG_ESP) - 4;
        pr_cpu_set_reg(p->cpu, PR_REG_ESP, esp);
        uint32_t sentinel = PROC_SENTINEL;
        pr_vm_write(p->vm, esp, &sentinel, 4);
    }

    p->prepared = 1;
    return PR_OK;
}

/* ---- execução ---- */

pr_status pr_peproc_step(pr_peproc* p, uint64_t budget, uint64_t* executed) {
    if (!p || !p->prepared) return PR_ERR_STATE;
    if (executed) *executed = 0;

    if (!p->started) {
        p->started = 1;
        {
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            p->start_ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
        }
        if (p->log)
            pr_log_write(p->log, PR_LOG_INFO, "cpu",
                         "[CPU] execution started arch=%s", proc_arch(p));
    }

    /* x86-64: execução REAL pelo interpretador mínimo (subconjunto straight-
     * line do PE visual de teste). Instrução fora do subconjunto → fault com
     * opcode, RIP e endereço registrados + diagnóstico claro. */
    if (p->img.machine == 0x8664 && p->stop == PR_PEPROC_STOP_NONE) {
        uint64_t n64 = 0;
        uint32_t denied64 = pr_vm_denied_count(p->vm);
        pr_status st64 = pr_cpu64_run(p->cpu64, budget, &n64);
        if (executed) *executed = n64;
        if (p->stop != PR_PEPROC_STOP_NONE) return PR_OK; /* parada pelo trap */

        if (st64 == PR_OK) {
            if (pr_win32_halted(p->w32)) {
                p->stop = PR_PEPROC_STOP_EXIT;
                p->exit_code = pr_win32_exit_code(p->w32);
                snprintf(p->diag, sizeof(p->diag),
                         "PROCESS EXIT\n\nReason:\nExitProcess\n\nModule:\n%s\n\n"
                         "Function:\n%s\n\nAddress:\n0x%08X\n\n"
                         "Architecture:\n%s\n\nExit code:\n%u",
                         p->img.module_name, "ExitProcess",
                         (uint32_t)pr_cpu64_rip(p->cpu64), proc_arch(p), p->exit_code);
                if (p->log)
                    pr_log_write(p->log, PR_LOG_INFO, "proc",
                                 "[PROCESS] exit code %u after %.2f ms", p->exit_code, proc_ms(p));
            } else if (pr_cpu64_halted(p->cpu64)) {
                p->exit_code = (uint32_t)pr_cpu64_reg(p->cpu64, PR_R64_RAX);
                p->stop = PR_PEPROC_STOP_EXIT;
                snprintf(p->diag, sizeof(p->diag),
                         "PROCESS EXIT (HLT)\n\nReason:\nHLT (parada do processador)\n\n"
                         "Module:\n%s\n\nFunction:\n<entry>\n\nAddress:\n0x%08X\n\n"
                         "Architecture:\n%s\n\nExit code:\n%u",
                         p->img.module_name, (uint32_t)pr_cpu64_rip(p->cpu64),
                         proc_arch(p), p->exit_code);
                if (p->log)
                    pr_log_write(p->log, PR_LOG_INFO, "proc",
                                 "[PROCESS] exit code %u after %.2f ms", p->exit_code, proc_ms(p));
            }
            return PR_OK;
        }

        const pr_cpu64_fault* f64 = pr_cpu64_last_fault(p->cpu64);
        if (f64 && f64->rip == (uint64_t)PROC_SENTINEL) {
            /* entry retornou para a sentinela (contrato CRT): exit em EAX */
            p->exit_code = (uint32_t)pr_cpu64_reg(p->cpu64, PR_R64_RAX);
            p->stop = PR_PEPROC_STOP_EXIT;
            snprintf(p->diag, sizeof(p->diag),
                     "PROCESS EXIT\n\nReason:\nentry retornou (sentinela CRT)\n\n"
                     "Module:\n%s\n\nFunction:\n<entry>\n\nAddress:\n0x%08X\n\n"
                     "Architecture:\n%s\n\nExit code:\n%u",
                     p->img.module_name, (uint32_t)PROC_SENTINEL,
                     proc_arch(p), p->exit_code);
            if (p->log)
                pr_log_write(p->log, PR_LOG_INFO, "proc",
                             "[PROCESS] exit code %u after %.2f ms", p->exit_code, proc_ms(p));
            return PR_OK;
        }
        /* fault real: memória (guard) ou instrução fora do subconjunto */
        int bad_mem = pr_vm_denied_count(p->vm) > denied64;
        uint32_t daddr = 0; size_t dlen = 0; int dacc = 0;
        if (bad_mem && pr_vm_last_denied(p->vm, &daddr, &dlen, &dacc) && p->log)
            pr_log_write(p->log, PR_LOG_WARN, "mem",
                         "[MEM] fault addr=0x%08X len=%u acc=%c%c%c rip=0x%llX",
                         daddr, (unsigned)dlen,
                         (dacc & PR_VM_PROT_R) ? 'R' : '-',
                         (dacc & PR_VM_PROT_W) ? 'W' : '-',
                         (dacc & PR_VM_PROT_X) ? 'X' : '-',
                         (unsigned long long)(f64 ? f64->rip : 0));
        p->stop = bad_mem ? PR_PEPROC_STOP_BAD_MEMORY : PR_PEPROC_STOP_BAD_INSN;
        {
            char bytestr[32];
            size_t bp = 0;
            bytestr[0] = 0;
            if (f64) {
                uint8_t nb = f64->nbytes > 8 ? 8 : f64->nbytes;
                for (uint8_t i = 0; i < nb; i++)
                    bp += (size_t)snprintf(bytestr + bp, sizeof(bytestr) - bp,
                                           "%s%02X", i ? " " : "", f64->bytes[i]);
            }
            snprintf(p->diag, sizeof(p->diag),
                 "EXECUTION STOPPED\n\nReason:\n%s\n\nModule:\n%s\n\n"
                 "Function:\n<instrução x64>\n\nAddress:\n0x%08X\n\n"
                 "Architecture:\n%s\n\n"
                 "Technical: rip=0x%llX opcode=0x%02X addr=0x%08X bytes=%s",
                 f64 ? f64->reason : "fault", p->img.module_name,
                 (uint32_t)(f64 ? f64->rip : 0), proc_arch(p),
                 (unsigned long long)(f64 ? f64->rip : 0),
                 f64 ? f64->opcode : 0, f64 ? f64->addr : 0, bytestr);
        }
        return PR_ERR_FAULT;
    }
    if (p->stop != PR_PEPROC_STOP_NONE) return PR_OK;

    uint64_t n = 0;
    uint32_t denied0 = pr_vm_denied_count(p->vm);
    pr_status st = pr_cpu_run(p->cpu, budget, &n);
    if (executed) *executed = n;

    if (p->stop != PR_PEPROC_STOP_NONE) return PR_OK; /* parada pelo trap */

    if (st == PR_ERR_FAULT) {
        const pr_cpu_fault* f = pr_cpu_last_fault(p->cpu);
        uint32_t addr = 0; size_t len = 0; int acc = 0;
        char tech[128];
        /* memória só se houve denegação NESTE passo; senão é instrução */
        if (pr_vm_denied_count(p->vm) > denied0 &&
            pr_vm_last_denied(p->vm, &addr, &len, &acc)) {
            if (p->log)
                pr_log_write(p->log, PR_LOG_WARN, "mem",
                             "[MEM] fault addr=0x%08X len=%u acc=%c%c%c eip=0x%08X",
                             addr, (unsigned)len,
                             (acc & PR_VM_PROT_R) ? 'R' : '-',
                             (acc & PR_VM_PROT_W) ? 'W' : '-',
                             (acc & PR_VM_PROT_X) ? 'X' : '-', f->eip);
            snprintf(tech, sizeof(tech),
                     "eip=0x%08X opcode=0x%02X addr=0x%08X len=%u acc=%c%c%c",
                     f->eip, f->opcode, addr, (unsigned)len,
                     (acc & PR_VM_PROT_R) ? 'R' : '-',
                     (acc & PR_VM_PROT_W) ? 'W' : '-',
                     (acc & PR_VM_PROT_X) ? 'X' : '-');
            diag_set(p, PR_PEPROC_STOP_BAD_MEMORY, "Invalid memory access",
                     "", "", tech, addr);
        } else {
            snprintf(tech, sizeof(tech), "eip=0x%08X opcode=0x%02X — %s",
                     f->eip, f->opcode, f->reason);
            diag_set(p, PR_PEPROC_STOP_BAD_INSN, "Unsupported instruction",
                     "", "", tech, f->eip);
        }
        return PR_ERR_FAULT;
    }
    if (pr_cpu_halted(p->cpu) && p->stop == PR_PEPROC_STOP_NONE) {
        /* HLT sem ExitProcess: fim com o código em EAX */
        p->exit_code = pr_cpu_reg(p->cpu, PR_REG_EAX);
        p->stop = PR_PEPROC_STOP_EXIT;
        snprintf(p->diag, sizeof(p->diag),
                 "PROCESS EXIT\n\nReason:\nHLT\n\nModule:\n(processo)\n\n"
                 "Function:\n(—)\n\nAddress:\n0x%08X\n\nArchitecture:\n%s\n\n"
                 "Exit code:\n%u",
                 (uint32_t)pr_cpu_eip(p->cpu), proc_arch(p), p->exit_code);
    }
    return PR_OK;
}

pr_peproc_stop pr_peproc_state(const pr_peproc* p) {
    return p ? p->stop : PR_PEPROC_STOP_BAD_EXECUTABLE;
}

int pr_peproc_exited(const pr_peproc* p) {
    return p && p->stop == PR_PEPROC_STOP_EXIT;
}

uint32_t pr_peproc_exit_code(const pr_peproc* p) {
    return p ? p->exit_code : 0;
}

const char* pr_peproc_diagnostic(const pr_peproc* p) {
    return p ? p->diag : "";
}

pr_cpu* pr_peproc_cpu(pr_peproc* p) { return p ? p->cpu : NULL; }
pr_win32_ctx* pr_peproc_win32(pr_peproc* p) { return p ? p->w32 : NULL; }
pr_vm* pr_peproc_vm(pr_peproc* p) { return p ? p->vm : NULL; }
pr_log* pr_peproc_log(pr_peproc* p) { return p ? p->log : NULL; }

pr_surf* pr_peproc_surface(pr_peproc* p) {
    return p ? pr_win32_surface(p->w32) : NULL;
}

pr_status pr_peproc_present(pr_peproc* p, pr_gfx_stream* stream,
                            uint32_t target_w, uint32_t target_h) {
    pr_surf* s = pr_peproc_surface(p);
    if (!s || !stream) return PR_ERR_STATE;
    return pr_surf_present(s, stream, target_w, target_h);
}

/* ---- FASE SEH/unwind x64: acesso ao índice .pdata ---- */
size_t pr_peproc_unwind_count(const pr_peproc* p) {
    return p ? p->nunw : 0;
}

pr_status pr_peproc_unwind_at(pr_peproc* p, size_t index, pr_unwind_info* out) {
    if (!p || !out || index >= p->nunw) return PR_ERR_INVALID;
    return pr_unwind_parse(p->img.image, p->img.image_size,
                           p->unw[index].unwind_rva, out);
}

int pr_peproc_unwind_find(pr_peproc* p, uint32_t rva, pr_unwind_info* out) {
    if (!p) return -1;
    int idx = pr_unwind_lookup(p->unw, p->nunw, rva);
    if (idx >= 0 && out) {
        if (pr_unwind_parse(p->img.image, p->img.image_size,
                            p->unw[idx].unwind_rva, out) != PR_OK)
            return -1;
    }
    return idx;
}
