#include <windows.h>

/* hello_getcurrentthreadid — validação por PE real de kernel32!GetCurrentThreadId.
 * Contrato de retorno:
 *   10 = retorno zero (não utilizável como DWORD)
 *   11 = Thread ID instável entre chamadas no mesmo processo
 *   12 = LastError alterado no caminho de sucesso
 *   62 = fluxo completo */

int main(void) {
    DWORD a = GetCurrentThreadId();   /* DWORD = assinatura Win32 (32 bits) */
    DWORD b = GetCurrentThreadId();
    DWORD c = GetCurrentThreadId();
    DWORD mix;

    /* não-zero, utilizável como DWORD */
    if (a == 0) return 10;

    /* estabilidade no processo (a == b == c) com consumo real dos valores */
    if (a != b) return 11;
    if (b != c) return 11;
    mix = a ^ c;                       /* XOR consome o valor */
    if (mix != 0) return 11;

    /* LastError intacto no sucesso */
    SetLastError(0xDEAD);
    (void)GetCurrentThreadId();
    if (GetLastError() != 0xDEAD) return 12;

    return 62;
}
