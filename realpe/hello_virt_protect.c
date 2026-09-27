/* hello_virt_protect.c — G39: validação real de VirtualProtect por PE x64.
 * Fluxo mínimo (§4):
 *   p = VirtualAlloc(NULL, 4096, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE)
 *   p == NULL -> 10
 *   p[0..3] = 0x11 0x22 0x33 0x44
 *   VirtualProtect(p, 4096, PAGE_READONLY, &old)
 *       != TRUE ou old != PAGE_READWRITE -> 11
 *   VirtualProtect(p, 4096, PAGE_READWRITE, &old)
 *       != TRUE ou old != PAGE_READONLY -> 12
 *   dados alterados -> 13
 *   VirtualFree(p, 0, MEM_RELEASE) != TRUE -> 14
 *   return 42
 * Imports: KERNEL32 VirtualAlloc/VirtualProtect/VirtualFree + CRT.
 * NÃO escreve em página somente-leitura; NÃO usa VirtualQuery. */
#include <stdint.h>
#include <windows.h>

int main(void)
{
    volatile unsigned char *p =
        (volatile unsigned char *)VirtualAlloc(NULL, 4096,
                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (p == NULL)
        return 10;

    p[0] = 0x11;
    p[1] = 0x22;
    p[2] = 0x33;
    p[3] = 0x44;

    DWORD oldProtect = 0;
    BOOL result = VirtualProtect((LPVOID)p, 4096, PAGE_READONLY, &oldProtect);
    if (result == FALSE || oldProtect != PAGE_READWRITE)
        return 11;

    oldProtect = 0;
    result = VirtualProtect((LPVOID)p, 4096, PAGE_READWRITE, &oldProtect);
    if (result == FALSE || oldProtect != PAGE_READONLY)
        return 12;

    if (p[0] != 0x11 || p[1] != 0x22 || p[2] != 0x33 || p[3] != 0x44)
        return 13;

    result = VirtualFree((LPVOID)p, 0, MEM_RELEASE);
    if (result == FALSE)
        return 14;

    return 42;
}
