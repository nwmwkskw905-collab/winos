/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

/* Sondagem real de capacidades do ambiente. */
#include "portico/pr_cap.h"

#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>

#if defined(__APPLE__)
#if __has_include(<TargetConditionals.h>)
#include <TargetConditionals.h>
#endif
#if __has_include(<sys/sysctl.h>)
#include <sys/sysctl.h>
#endif
#endif

#ifndef TARGET_OS_IPHONE
#define TARGET_OS_IPHONE 0
#endif
#ifndef TARGET_OS_SIMULATOR
#define TARGET_OS_SIMULATOR 0
#endif

void pr_cap_probe(pr_cap_info* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    out->page_size = sysconf(_SC_PAGESIZE);

#if defined(_SC_PHYS_PAGES)
    long pages = sysconf(_SC_PHYS_PAGES);
    if (pages > 0) out->total_ram = (uint64_t)pages * (uint64_t)out->page_size;
#endif

#if defined(__APPLE__) && defined(__MACH__) && !defined(__linux__)
    {
        size_t n = sizeof(out->cpu_brand) - 1;
#if __has_include(<sys/sysctl.h>)
        if (sysctlbyname("machdep.cpu.brand_string", out->cpu_brand, &n, NULL, 0) != 0) {
            strncpy(out->cpu_brand, "Apple", sizeof(out->cpu_brand) - 1);
        }
#else
        strncpy(out->cpu_brand, "Apple", sizeof(out->cpu_brand) - 1);
#endif
    }
#if TARGET_OS_IPHONE
    out->is_ios = 1;
#if TARGET_OS_SIMULATOR
    out->is_simulator = 1;
#endif
#endif
#else
    /* Linux: /proc/cpuinfo (também usado quando simulando Apple no Linux) */
    FILE* f = fopen("/proc/cpuinfo", "r");
    if (f) {
        char line[256];
        while (fgets(line, sizeof(line), f)) {
            if (strncmp(line, "model name", 10) == 0) {
                char* colon = strchr(line, ':');
                if (colon) {
                    colon++;
                    while (*colon == ' ') colon++;
                    snprintf(out->cpu_brand, sizeof(out->cpu_brand), "%s", colon);
                    char* nl = strchr(out->cpu_brand, '\n');
                    if (nl) *nl = 0;
                }
                break;
            }
        }
        fclose(f);
    }
#endif
    if (!out->cpu_brand[0]) {
        strncpy(out->cpu_brand, "desconhecido", sizeof(out->cpu_brand) - 1);
    }

    /* Sonda: é possível mapear memória executável anônima? */
    void* p = mmap(NULL, (size_t)out->page_size,
                   PROT_READ | PROT_WRITE | PROT_EXEC,
                   MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) {
        out->exec_mem_mappable = 0;
    } else {
        out->exec_mem_mappable = 1;
#if defined(__APPLE__)
        /* iOS: NÃO executamos a memória sondada (codesigning/W^X poderia matar o app).
         * A política da plataforma proíbe JIT de apps de terceiros. */
        out->jit_available = 0;
        out->write_xor_execute = 1;
#else
        /* Ambiente de desenvolvimento: executa um stub `ret` real para provar JIT possível. */
        ((uint8_t*)p)[0] = 0xC3; /* RET */
        __builtin___clear_cache((char*)p, (char*)p + 16);
        {
            int (*fn)(void) = (int (*)(void))(void*)p;
            fn();
        }
        out->jit_available = 1;
        out->write_xor_execute = 0;
#endif
        munmap(p, (size_t)out->page_size);
    }

#if defined(__APPLE__) && TARGET_OS_IPHONE
    snprintf(out->note, sizeof(out->note),
             "iOS: JIT proibido para apps de terceiros; execucao apenas por interpretacao.");
#else
    if (out->jit_available) {
        snprintf(out->note, sizeof(out->note),
                 "Ambiente de desenvolvimento: memoria executavel disponivel (JIT possivel).");
    } else {
        snprintf(out->note, sizeof(out->note),
                 "Memoria executavel indisponivel neste ambiente; use interpretacao.");
    }
#endif
}
