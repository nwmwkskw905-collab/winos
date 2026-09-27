/* hello_virt_query.c — G40: validação real de VirtualQuery + MEMORY_BASIC_INFORMATION.
 * Fluxo (§6-§9):
 *   p = VirtualAlloc(NULL, 4096, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE); p==NULL -> 10
 *   memset(&mbi,0,sizeof mbi); r = VirtualQuery(p, &mbi, sizeof mbi)
 *   r != 48 (sizeof compilado pelo MinGW x64) -> 11
 *   mbi.AllocationBase == p -> 12; AllocationProtect == PAGE_READWRITE -> 13
 *   RegionSize >= 4096 -> 14; State == MEM_COMMIT -> 15
 *   Protect == PAGE_READWRITE -> 16; Type == MEM_PRIVATE -> 17
 *   VirtualProtect(p, 4096, PAGE_READONLY, &old); VirtualQuery; Protect == PAGE_READONLY -> 18
 *   VirtualProtect(p, 4096, PAGE_READWRITE, &old)  (restauração; sem violação de acesso)
 *   VirtualFree(p, 0, MEM_RELEASE); FALSE -> 19; 42
 * Chama explicitamente: VirtualAlloc, VirtualQuery, VirtualProtect, VirtualFree (+CRT). */
#include <stdint.h>
#include <string.h>
#include <windows.h>

int main(void)
{
    LPVOID p = VirtualAlloc(NULL, 4096,
                            MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (p == NULL)
        return 10;

    MEMORY_BASIC_INFORMATION mbi;
    memset(&mbi, 0, sizeof(mbi));
    SIZE_T r = VirtualQuery(p, &mbi, sizeof(mbi));
    if (r != sizeof(mbi) || sizeof(mbi) != 48)
        return 11;

    if (mbi.AllocationBase == NULL || mbi.AllocationBase != p)
        return 12;
    if (mbi.AllocationProtect != PAGE_READWRITE)
        return 13;
    if (mbi.RegionSize < 4096)
        return 14;
    if (mbi.State != MEM_COMMIT)
        return 15;
    if (mbi.Protect != PAGE_READWRITE)
        return 16;
    if (mbi.Type != MEM_PRIVATE)
        return 17;

    DWORD oldProtect = 0;
    VirtualProtect(p, 4096, PAGE_READONLY, &oldProtect);

    memset(&mbi, 0, sizeof(mbi));
    r = VirtualQuery(p, &mbi, sizeof(mbi));
    if (r != sizeof(mbi) || mbi.Protect != PAGE_READONLY)
        return 18;

    VirtualProtect(p, 4096, PAGE_READWRITE, &oldProtect);

    if (VirtualFree(p, 0, MEM_RELEASE) == FALSE)
        return 19;

    return 42;
}
