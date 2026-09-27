#include <windows.h>

/* hello_getmodfilenamew — G78: validação por PE x64 real de kernel32!GetModuleFileNameW
 * Contratos:
 *   10 = A falhou
 *   11 = W falhou
 *   12 = tamanhos diferentes
 *   13 = conteúdo W diferente de A
 *   14 = truncamento W incorreto
 *   15 = NUL terminador W ausente
 *   16 = handle inválido não recusado
 *   78 = OK
 */

int main(void) {
    char bufA[128];
    WCHAR bufW[128];
    DWORD retA, retW;

    retA = GetModuleFileNameA(NULL, bufA, 128);
    if (retA == 0) return 10;

    retW = GetModuleFileNameW(NULL, bufW, 128);
    if (retW == 0) return 11;
    if (retW != retA) return 12;

    for (DWORD i = 0; i < retA; i++) {
        if (bufW[i] != (WCHAR)bufA[i]) return 13;
    }
    if (bufW[retA] != 0) return 15;

    /* truncamento: buffer 4 */
    retA = GetModuleFileNameA(NULL, bufA, 4);
    retW = GetModuleFileNameW(NULL, bufW, 4);
    if (retA != 3) return 14; /* cap-1 */
    if (retW != 3) return 14;
    if (bufA[3] != 0) return 15;
    if (bufW[3] != 0) return 15;

    /* handle inválido */
    SetLastError(0xDEAD);
    retA = GetModuleFileNameA((HMODULE)0x1234, bufA, 128);
    if (retA != 0) return 16;
    if (GetLastError() != 126) return 16; /* MOD_NOT_FOUND */

    SetLastError(0xDEAD);
    retW = GetModuleFileNameW((HMODULE)0x1234, bufW, 128);
    if (retW != 0) return 16;
    if (GetLastError() != 126) return 16;

    return 78;
}
