#include <windows.h>

/* hello_isdebuggerpresent — validação por PE real de kernel32!IsDebuggerPresent.
 * Contrato de retorno:
 *   10 = alguma chamada retornou != FALSE (0)
 *   11 = comportamento inconsistente entre chamadas
 *   12 = LastError alterado no caminho de sucesso
 *   63 = fluxo completo
 * Comportamento observado do handler do Portico: FALSE (0) = "não sob depuração". */

int main(void) {
    BOOL a = IsDebuggerPresent();
    BOOL b = IsDebuggerPresent();
    BOOL c = IsDebuggerPresent();

    /* comportamento atual: FALSE (0) nas três chamadas */
    if (a != 0 || b != 0 || c != 0) return 10;

    /* consistência entre chamadas, com consumo real dos valores */
    if (a != b) return 11;
    if (b != c) return 11;
    if ((a ^ b ^ c) != 0) return 11;   /* XOR consome os três valores */

    /* LastError intacto no sucesso */
    SetLastError(0xDEAD);
    (void)IsDebuggerPresent();
    if (GetLastError() != 0xDEAD) return 12;

    return 63;
}
