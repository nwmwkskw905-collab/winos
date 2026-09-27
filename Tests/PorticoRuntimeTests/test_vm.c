/* Testes da memória virtual do processo: regiões, permissões, tradução, falhas. */
#include "pt_util.h"

#include "portico/pr_vm.h"

void test_vm(void) {
    pr_vm* vm = pr_vm_create(1 << 20); /* 1 MiB */
    CHECK(vm != NULL);
    CHECK_EQ_U32(pr_vm_space(vm), 1 << 20);

    /* limites de criação */
    CHECK(pr_vm_create(4096) == NULL);          /* espaço pequeno demais */
    CHECK(pr_vm_create(512u << 20) == NULL);    /* acima de 256 MiB */

    /* map básico */
    void* host = NULL;
    CHECK(pr_vm_map(vm, 0x1000, 0x1000, PR_VM_PROT_R | PR_VM_PROT_W, "dados", &host) == PR_OK);
    CHECK(host != NULL);
    /* sobreposição é recusada */
    CHECK(pr_vm_map(vm, 0x1800, 0x1000, PR_VM_PROT_R, "overlap", NULL) == PR_ERR_STATE);
    /* fora do espaço */
    CHECK(pr_vm_map(vm, 0x000F0000, 0x20000, PR_VM_PROT_R, "longe", NULL) == PR_ERR_RANGE);

    /* escrita/leitura validadas */
    uint32_t v = 0xDEADBEEF;
    CHECK(pr_vm_write(vm, 0x1FFC, &v, 4) == PR_OK);
    uint32_t back = 0;
    CHECK(pr_vm_read(vm, 0x1FFC, &back, 4) == PR_OK);
    CHECK_EQ_U32(back, 0xDEADBEEF);

    /* região não mapeada → fault */
    CHECK(pr_vm_read(vm, 0x5000, &back, 4) == PR_ERR_FAULT);
    uint32_t addr = 0; size_t len = 0; int acc = 0;
    CHECK_EQ_U32(pr_vm_last_denied(vm, &addr, &len, &acc), 1);
    CHECK_EQ_U32(addr, 0x5000);
    CHECK_EQ_U32(pr_vm_denied_count(vm), 1);

    /* proteção: região RX recusa escrita */
    CHECK(pr_vm_map(vm, 0x4000, 0x1000, PR_VM_PROT_R | PR_VM_PROT_X, "codigo", NULL) == PR_OK);
    CHECK(pr_vm_write(vm, 0x4000, &v, 4) == PR_ERR_FAULT);       /* sem W */
    CHECK(pr_vm_read(vm, 0x4000, &back, 4) == PR_OK);            /* R ok */
    CHECK_EQ_U32(pr_vm_check(vm, 0x4000, 1, PR_VM_PROT_X), 1);   /* fetch ok */

    /* RW recusa execução */
    CHECK_EQ_U32(pr_vm_check(vm, 0x1000, 1, PR_VM_PROT_X), 0);

    /* muda proteção para RWX e escreve */
    CHECK(pr_vm_protect(vm, 0x4000, 0x1000, PR_VM_PROT_R | PR_VM_PROT_W | PR_VM_PROT_X) == PR_OK);
    CHECK(pr_vm_write(vm, 0x4000, &v, 4) == PR_OK);

    /* protect exige cobrir regiões inteiras */
    CHECK(pr_vm_protect(vm, 0x4800, 0x100, PR_VM_PROT_R) == PR_ERR_STATE);

    /* unmap de região completa */
    CHECK(pr_vm_unmap(vm, 0x1000, 0x800) == PR_ERR_STATE);  /* parcial */
    CHECK(pr_vm_unmap(vm, 0x1000, 0x1000) == PR_OK);
    CHECK(pr_vm_read(vm, 0x1FFC, &back, 4) == PR_ERR_FAULT); /* desmapeada */

    /* alloc: alinhado, não sobreposto, do fim para o baixo */
    uint32_t a1 = 0, a2 = 0;
    CHECK(pr_vm_alloc(vm, 100, PR_VM_PROT_R | PR_VM_PROT_W, "heap", &a1, &host) == PR_OK);
    CHECK_EQ_U32(a1 & 0xFFF, 0);
    CHECK(host != NULL);
    CHECK(pr_vm_alloc(vm, 0x2000, PR_VM_PROT_R, "heap2", &a2, NULL) == PR_OK);
    CHECK(a2 != a1);
    /* não colide com 0x4000 */
    CHECK(a2 + 0x2000 <= 0x4000 || a2 >= 0x5000);

    /* lista de regiões */
    pr_vm_region regs[16];
    size_t n = pr_vm_regions(vm, regs, 16);
    CHECK(n >= 3);
    int saw_codigo = 0, saw_heap = 0;
    for (size_t i = 0; i < n; i++) {
        if (strcmp(regs[i].tag, "codigo") == 0) {
            saw_codigo = 1;
            CHECK_EQ_U32(regs[i].prot, PR_VM_PROT_R | PR_VM_PROT_W | PR_VM_PROT_X);
        }
        if (strcmp(regs[i].tag, "heap") == 0) saw_heap = 1;
    }
    CHECK(saw_codigo && saw_heap);

    /* tradução exige o acesso exato */
    CHECK(pr_vm_translate(vm, a1, 4, PR_VM_PROT_W) != NULL);
    CHECK_EQ_U32((uintptr_t)pr_vm_translate(vm, 0x80000000u, 4, PR_VM_PROT_R) == 0, 1);

    pr_vm_destroy(vm);
}
