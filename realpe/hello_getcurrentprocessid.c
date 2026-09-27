#include <windows.h>

/* hello_getcurrentprocessid — validação por PE real de kernel32!GetCurrentProcessId.
 * Contrato de retorno:
 *   10 = retorno zero (não utilizável como DWORD)
 *   11 = PID instável entre chamadas no mesmo processo
 *   12 = LastError alterado no caminho de sucesso
 *   61 = fluxo completo */

int main(void) {
    DWORD a = GetCurrentProcessId();   /* armazenado como DWORD de 32 bits */
    DWORD b, c;

    /* §6.1 — retorno não-zero, utilizável como DWORD */
    if (a == 0) return 10;

    /* §6.2/§6.4 — estabilidade + valor consumido de fato pelo programa */
    b = GetCurrentProcessId();
    if (a != b) return 11;
    c = GetCurrentProcessId() ^ b;     /* terceira chamada; XOR consome o valor */
    if (c != 0) return 11;

    /* §6.5 — LastError intacto no sucesso */
    SetLastError(0xDEAD);
    (void)GetCurrentProcessId();
    if (GetLastError() != 0xDEAD) return 12;

    return 61;
}
