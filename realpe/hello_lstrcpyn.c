#include <windows.h>

/* hello_lstrcpyn — validação por PE real de kernel32!lstrcpynA.
 * Asserções só de propriedades estáveis do contrato documentado:
 * cópia com NUL, truncamento com NUL sempre, limite iCharCount=1,
 * fonte vazia, identidade do ponteiro de retorno e escrita limitada
 * (canário 0xCC nunca tocado além de min(strlen,cap-1)+1 bytes).
 * Contrato de retorno:
 *   10 = retorno != ponteiro destino (cópia normal)
 *   11 = bytes/NUL incorretos (cópia normal)
 *   12 = escrita além do necessário (cópia normal)
 *   13 = retorno != ponteiro destino (truncamento)
 *   14 = bytes/NUL incorretos (truncamento)
 *   15 = escrita além de iCharCount (truncamento)
 *   16 = retorno != ponteiro destino (iCharCount=1)
 *   17 = iCharCount=1 não produziu NUL imediato
 *   18 = iCharCount=1 escreveu além do NUL
 *   19 = fonte vazia incorreta
 *   52 = fluxo completo */

int main(void) {
    char buf[32];
    unsigned i;
    char* r;

    /* (1) cópia normal: "abc" cabe em 8 */
    for (i = 0; i < sizeof buf; i++) buf[i] = (char)0xCC;
    r = lstrcpynA(buf, "abc", 8);
    if (r != buf) return 10;
    if (buf[0] != 'a' || buf[1] != 'b' || buf[2] != 'c' || buf[3] != 0) return 11;
    if (buf[4] != (char)0xCC) return 12;

    /* (2) truncamento: "abcdefgh" em 4 = "abc" + NUL, nada além */
    for (i = 0; i < sizeof buf; i++) buf[i] = (char)0xCC;
    r = lstrcpynA(buf, "abcdefgh", 4);
    if (r != buf) return 13;
    if (buf[0] != 'a' || buf[1] != 'b' || buf[2] != 'c' || buf[3] != 0) return 14;
    if (buf[4] != (char)0xCC) return 15;

    /* (3) iCharCount = 1: somente o NUL */
    for (i = 0; i < sizeof buf; i++) buf[i] = (char)0xCC;
    r = lstrcpynA(buf, "abc", 1);
    if (r != buf) return 16;
    if (buf[0] != 0) return 17;
    if (buf[1] != (char)0xCC) return 18;

    /* (4) fonte vazia: só o NUL */
    for (i = 0; i < sizeof buf; i++) buf[i] = (char)0xCC;
    r = lstrcpynA(buf, "", 8);
    if (r != buf) return 19;
    if (buf[0] != 0) return 19;

    return 52;
}
