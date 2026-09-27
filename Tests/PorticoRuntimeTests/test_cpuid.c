/* Testes — CPUID/RDTSC: CPU virtual determinístico, sem informações do host. */
#include "pt_util.h"
#include "portico/pr_cpu64.h"
#include <string.h>

void test_cpuid(void) {
    static uint8_t mem[8192];
    pr_cpu64* c = pr_cpu64_create_on(mem, (uint32_t)sizeof(mem));
    CHECK(c != NULL);
    if (!c) return;

    /* folha 0: vendor PORTICO_VRTX + max leaf 1 */
    {
        memset(mem, 0, sizeof(mem));
        static const uint8_t prog[] = { 0xB8, 0, 0, 0, 0, 0x0F, 0xA2, 0xF4 }; /* mov eax,0; cpuid; hlt */
        memcpy(mem + 0x200, prog, sizeof prog);
        pr_cpu64_set_rip(c, 0x200);
        pr_cpu64_set_rflags(c, 0x202);
        uint64_t n = 0;
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        char vendor[13];
        uint32_t ebx = (uint32_t)pr_cpu64_reg(c, 3);
        uint32_t edx = (uint32_t)pr_cpu64_reg(c, 2);
        uint32_t ecx = (uint32_t)pr_cpu64_reg(c, 1);
        memcpy(vendor + 0, &ebx, 4);
        memcpy(vendor + 4, &edx, 4);
        memcpy(vendor + 8, &ecx, 4);
        vendor[12] = 0;
        CHECK_STR(vendor, "PORTICO_VRTX");
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, 0), 1u);
    }

    /* folha 1: identidade virtual fixa; só TSC/CMOV/SSE/SSE2 anunciados */
    {
        memset(mem, 0, sizeof(mem));
        static const uint8_t prog[] = { 0xB8, 1, 0, 0, 0, 0x0F, 0xA2, 0xF4 };
        memcpy(mem + 0x200, prog, sizeof prog);
        pr_cpu64_set_rip(c, 0x200);
        uint64_t n = 0;
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, 2),
                     (1u << 4) | (1u << 15) | (1u << 25) | (1u << 26));
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, 1), 0u);   /* ecx */
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, 3), 0u);   /* ebx */
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, 0), 0x6F0u);
    }

    /* folhas inexistentes: zeros (nunca vaza informação do host) */
    {
        memset(mem, 0, sizeof(mem));
        static const uint8_t prog[] = { 0xB8, 0x80, 0, 0, 0, 0x0F, 0xA2, 0xF4 };
        memcpy(mem + 0x200, prog, sizeof prog);
        pr_cpu64_set_rip(c, 0x200);
        uint64_t n = 0;
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, 0), 0u);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, 3), 0u);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, 2), 0u);
        CHECK_EQ_U32((uint32_t)pr_cpu64_reg(c, 1), 0u);
    }

    /* RDTSC determinístico: execuções idênticas em CPUs novas => mesmo TSC */
    {
        uint64_t a = 0, b = 0;
        for (int round = 0; round < 2; round++) {
            memset(mem, 0, sizeof(mem));
            static const uint8_t prog[] = { 0x0F, 0x31, 0xF4 };  /* rdtsc; hlt */
            memcpy(mem + 0x200, prog, sizeof prog);
            pr_cpu64* c2 = pr_cpu64_create_on(mem, (uint32_t)sizeof(mem));
            pr_cpu64_set_rip(c2, 0x200);
            uint64_t n = 0;
            CHECK(pr_cpu64_run(c2, 100, &n) == PR_OK);
            uint64_t v = (uint64_t)(uint32_t)pr_cpu64_reg(c2, 0) |
                         ((uint64_t)(uint32_t)pr_cpu64_reg(c2, 2) << 32);
            if (round == 0) a = v;
            else b = v;
            pr_cpu64_destroy(c2);
        }
        CHECK(a == b);   /* determinístico: nada do host */
        CHECK(a > 0);
    }

    /* monotônico: TSC cresce com as instruções executadas */
    {
        memset(mem, 0, sizeof(mem));
        static const uint8_t prog[] = { 0x0F, 0x31, 0x90, 0x90, 0x0F, 0x31, 0xF4 };
        /* rdtsc; nop; nop; rdtsc; hlt — 2a leitura em EDX:EAX é maior */
        memcpy(mem + 0x200, prog, sizeof prog);
        pr_cpu64_set_rip(c, 0x200);
        uint64_t n = 0;
        CHECK(pr_cpu64_run(c, 100, &n) == PR_OK);
        uint64_t v = (uint64_t)(uint32_t)pr_cpu64_reg(c, 0) |
                     ((uint64_t)(uint32_t)pr_cpu64_reg(c, 2) << 32);
        CHECK(v >= 32);   /* 4 instruções * 8 ticks */
    }

    pr_cpu64_destroy(c);
}
