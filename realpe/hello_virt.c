/* hello_virt.c — G38: validação real de VirtualAlloc + VirtualFree por PE x64.
 * Fluxo mínimo (FASE 3..6):
 *   p = VirtualAlloc(NULL, 4096, MEM_COMMIT|MEM_RESERVE (0x3000),
 *                    PAGE_READWRITE (0x04))
 *   p == NULL -> 10
 *   p[i] = 0xA5 ^ i (i = 0..63) com acesso volatile
 *   releitura byte a byte; divergência -> 11
 *   r = VirtualFree(p, 0, MEM_RELEASE (0x8000)); FALSE -> 12; TRUE -> 42
 * Imports: KERNEL32 VirtualAlloc/VirtualFree + CRT (printf).
 * FASE 7: NÃO testa VirtualProtect/Query/MBI, MEM_DECOMMIT/MEM_RESET/PAGE_GUARD,
 * múltiplas regiões, endereço solicitado, alinhamento especial ou threads. */
#include <stdint.h>
#include <stdio.h>
#include <windows.h>

int main(void)
{
    LPVOID p = VirtualAlloc(NULL, 4096,
                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (p == NULL)
        return 10;

    /* escrita pelo próprio PE no VA do guest (padrão determinístico) */
    volatile unsigned char *v = (volatile unsigned char *)p;
    int i;
    for (i = 0; i < 64; i++)
        v[i] = (unsigned char)(0xA5u ^ (unsigned)i);

    /* leitura pelo próprio PE — provar store+load via tradução */
    for (i = 0; i < 64; i++)
        if (v[i] != (unsigned char)(0xA5u ^ (unsigned)i))
            return 11;

    BOOL r = VirtualFree(p, 0, MEM_RELEASE);
    if (r == FALSE)
        return 12;

    printf("virt: p=0x%llx free=%d\n",
           (unsigned long long)(uintptr_t)p, (int)r);
    return 42;
}
