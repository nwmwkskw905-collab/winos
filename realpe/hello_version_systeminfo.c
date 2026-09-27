#include <windows.h>
#include <string.h>

/* hello_version_systeminfo — validação por PE real de GetVersion,
 * GetVersionExA e GetSystemInfo (somente propriedades estáveis do contrato).
 * Contrato de retorno:
 *   10 = GetVersion() retornou zero
 *   11 = GetVersion não decomponível (major == 0)
 *   12 = GetVersionExA retornou FALSE
 *   13 = dwOSVersionInfoSize não preservado
 *   14 = versão inconsistente entre GetVersion e GetVersionExA
 *   15 = canário corrompido (escrita fora de OSVERSIONINFOA)
 *   16 = GetSystemInfo: dwPageSize == 0
 *   17 = GetSystemInfo: dwAllocationGranularity == 0
 *   18 = GetSystemInfo: dwNumberOfProcessors == 0
 *   42 = fluxo completo */

int main(void) {
    DWORD v = GetVersion();
    if (v == 0) return 10;
    if (LOBYTE(LOWORD(v)) == 0) return 11;

    /* GetVersionExA com canários de 16 bytes antes/depois da estrutura. */
    unsigned char buf[sizeof(OSVERSIONINFOA) + 32];
    unsigned i;
    for (i = 0; i < sizeof buf; i++) buf[i] = 0xA5;
    OSVERSIONINFOA* osvi = (OSVERSIONINFOA*)(void*)(buf + 16);
    osvi->dwOSVersionInfoSize = sizeof(OSVERSIONINFOA);

    if (!GetVersionExA(osvi)) return 12;

    if (osvi->dwOSVersionInfoSize != sizeof(OSVERSIONINFOA)) return 13;

    if (osvi->dwMajorVersion != (DWORD)LOBYTE(LOWORD(v))) return 14;
    if (osvi->dwMinorVersion != (DWORD)HIBYTE(LOWORD(v))) return 14;

    /* campos principais acessíveis */
    {
        volatile DWORD dwBuild = osvi->dwBuildNumber;
        volatile char c0 = osvi->szCSDVersion[0];
        (void)dwBuild; (void)c0;
    }

    for (i = 0; i < 16; i++)
        if (buf[i] != 0xA5) return 15;
    for (i = 16 + sizeof(OSVERSIONINFOA); i < sizeof buf; i++)
        if (buf[i] != 0xA5) return 15;

    /* GetSystemInfo com estrutura zerada. */
    SYSTEM_INFO si;
    memset(&si, 0, sizeof si);
    GetSystemInfo(&si);

    if (si.dwPageSize == 0) return 16;
    if (si.dwAllocationGranularity == 0) return 17;
    if (si.dwNumberOfProcessors == 0) return 18;

    /* ponteiros/campos de arquitetura acessíveis */
    {
        volatile LPVOID lo = si.lpMinimumApplicationAddress;
        volatile LPVOID hi = si.lpMaximumApplicationAddress;
        volatile WORD arch = si.wProcessorArchitecture;
        (void)lo; (void)hi; (void)arch;
    }

    return 42;
}
