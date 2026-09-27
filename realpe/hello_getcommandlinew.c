#include <windows.h>

/* hello_getcommandlinew — validação por PE real de kernel32!GetCommandLineW.
 * Conteúdo determinístico do runtime: L"portico" (UTF-16LE) em guest_cmdline_w
 * (página RO w32static, guest_cmdline+256; semeada por pr_win32_bind_vm).
 * Contrato de retorno:
 *   10 = ponteiro NULL
 *   11 = sem NUL wide dentro do limite seguro (128 unidades)
 *   12 = comprimento != 7
 *   13 = conteúdo != L"portico"
 *   14 = p[7] != 0x0000
 *   15 = identidade perdida entre chamadas (a != b)
 *   16 = LastError alterado pela chamada
 *   17 = ANSI (GetCommandLineA) e UTF-16 divergentes (comparação opcional §12)
 *   57 = fluxo completo */

int main(void) {
    LPWSTR w = GetCommandLineW();
    LPWSTR b;
    LPSTR a;
    unsigned i, len;
    static const unsigned short expect[7] =
        { 0x0070, 0x006F, 0x0072, 0x0074, 0x0069, 0x0063, 0x006F };

    /* A — ponteiro não nulo */
    if (w == NULL) return 10;

    /* B — NUL wide dentro de limite seguro (128 unidades = 256 bytes);
     * varredura local em unidades uint16, sem lstrlenW. */
    len = 0;
    for (i = 0; i < 128; i++) {
        if (w[i] == 0) { len = i; break; }
    }
    if (i >= 128) return 11;

    /* C — conteúdo UTF-16 esperado */
    if (len != 7) return 12;
    for (i = 0; i < 7; i++)
        if ((unsigned short)w[i] != expect[i]) return 13;

    /* D — NUL wide explícito em p[7] */
    if (w[7] != 0) return 14;

    /* E — consistência/identidade (armazenamento persistente confirmado) */
    b = GetCommandLineW();
    if (b != w) return 15;

    /* LastError — handler é retorno puro; não deve tocá-lo */
    SetLastError(0xDEAD);
    (void)GetCommandLineW();
    if (GetLastError() != 0xDEAD) return 16;

    /* Relação com GetCommandLineA (opcional §12): mesmo conteúdo */
    a = GetCommandLineA();
    if (a == NULL) return 17;
    for (i = 0; i < 8; i++)
        if ((unsigned short)(unsigned char)a[i] != (unsigned short)w[i]) return 17;

    return 57;
}
