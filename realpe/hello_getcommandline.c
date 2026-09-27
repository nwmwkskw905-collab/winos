#include <windows.h>

/* hello_getcommandline — validação por PE real de kernel32!GetCommandLineA.
 * Conteúdo determinístico do runtime atual: pr_win32_create semeia "portico";
 * pr_win32_bind_vm copia para página RO do guest (guest_cmdline, ANSI em 256
 * bytes). Contrato de retorno:
 *   10 = ponteiro NULL
 *   11 = sem NUL dentro do limite seguro (256)
 *   12 = identidade perdida entre chamadas (a != b)
 *   13 = comprimento != 7
 *   14 = conteúdo != "portico"
 *   15 = LastError alterado pela chamada (comportamento existente quebrado)
 *   56 = fluxo completo */

int main(void) {
    LPSTR a = GetCommandLineA();
    LPSTR b;
    unsigned i, len;

    /* Assertion A/E — ponteiro não nulo e memória acessível */
    if (a == NULL) return 10;

    /* Assertion B — NUL termination dentro de limite seguro (região ANSI
     * do guest = 256 bytes); varredura local limitada, sem lstrlenA. */
    len = 0;
    for (i = 0; i < 256; i++) {
        if (a[i] == 0) { len = i; break; }
    }
    if (i >= 256) return 11;

    /* Assertion C — consistência entre chamadas: o runtime retorna o mesmo
     * armazenamento persistente (identidade faz parte do contrato atual). */
    b = GetCommandLineA();
    if (b != a) return 12;

    /* Assertion D — conteúdo determinístico semeado pelo loader atual */
    if (len != 7) return 13;
    if (a[0] != 'p' || a[1] != 'o' || a[2] != 'r' || a[3] != 't' ||
        a[4] != 'i' || a[5] != 'c' || a[6] != 'o') return 14;

    /* LastError — o handler é retorno puro; a chamada não deve tocá-lo. */
    SetLastError(0xDEAD);
    (void)GetCommandLineA();
    if (GetLastError() != 0xDEAD) return 15;

    return 56;
}
