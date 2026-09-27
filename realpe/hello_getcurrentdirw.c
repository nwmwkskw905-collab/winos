#include <windows.h>

/* hello_getcurrentdirw — G77: validação por PE x64 real de kernel32!GetCurrentDirectoryW
 * Contratos:
 *   10 = A variante falhou
 *   11 = W variante falhou
 *   12 = W conteúdo incorreto (não ASCII)
 *   13 = W tamanho incorreto
 *   14 = buffer pequeno não retorna need
 *   15 = LastError não setado em buffer pequeno
 *   16 = W não NUL-termina
 *   77 = OK
 */

int main(void) {
    char bufA[128];
    WCHAR bufW[128];
    DWORD retA, retW;
    DWORD last;

    /* A variante deve funcionar (já validada em G??) */
    retA = GetCurrentDirectoryA(128, bufA);
    if (retA == 0 || retA >= 128) return 10;

    /* W variante tamanho suficiente */
    memset(bufW, 0xEE, sizeof bufW);
    retW = GetCurrentDirectoryW(128, bufW);
    if (retW == 0 || retW >= 128) return 11;
    if (retW != retA) return 13; /* mesmo tamanho sem NUL */

    /* Conteúdo: cada WCHAR deve ser ASCII do bufA + NUL */
    for (DWORD i = 0; i < retA; i++) {
        if (bufW[i] != (WCHAR)bufA[i]) return 12;
    }
    if (bufW[retA] != 0) return 16; /* NUL terminador */

    /* Buffer pequeno: deve retornar need (com NUL) e setar ERROR_INSUFFICIENT_BUFFER */
    SetLastError(0xDEAD);
    retW = GetCurrentDirectoryW(1, bufW); /* 1 WCHAR insuficiente para "\" */
    if (retW <= 1) return 14; /* deve retornar need >2 */
    if (GetLastError() != 122) return 15; /* ERROR_INSUFFICIENT_BUFFER */

    /* NULL buffer com tamanho 0 deve retornar need */
    retW = GetCurrentDirectoryW(0, NULL);
    if (retW == 0) return 17;

    return 77;
}
