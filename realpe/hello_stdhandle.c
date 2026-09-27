#include <windows.h>

/* hello_stdhandle — validação por PE real de GetStdHandle.
 * Contrato de retorno:
 *   10/11/12 = GetStdHandle in/out/err retornou NULL
 *   13/14/15 = GetStdHandle in/out/err retornou INVALID_HANDLE_VALUE
 *   16/17/18 = chamada repetida in/out/err inconsistente
 *   42 = fluxo completo */

int main(void) {
    HANDLE hin  = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE herr = GetStdHandle(STD_ERROR_HANDLE);

    if (hin  == NULL) return 10;
    if (hout == NULL) return 11;
    if (herr == NULL) return 12;

    if (hin  == INVALID_HANDLE_VALUE) return 13;
    if (hout == INVALID_HANDLE_VALUE) return 14;
    if (herr == INVALID_HANDLE_VALUE) return 15;

    /* chamadas repetidas devem ser consistentes */
    if (GetStdHandle(STD_INPUT_HANDLE)  != hin)  return 16;
    if (GetStdHandle(STD_OUTPUT_HANDLE) != hout) return 17;
    if (GetStdHandle(STD_ERROR_HANDLE)  != herr) return 18;

    return 42;
}
