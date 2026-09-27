/* hello_mbwc.c — G33: validação real da conversão Unicode (MB2WC/WC2MB).
 *
 * Fluxo (round-trip UTF-8 ↔ UTF-16, BMP, idioma real de todo programa Win32:
 * strings NUL-terminated com comprimento -1):
 *   MultiByteToWideChar(CP_UTF8, "winos_ñ", -1) → conferir unidades UTF-16 →
 *   WideCharToMultiByte(CP_UTF8, …, -1) → conferir os bytes UTF-8 de volta →
 *   return 42.
 *
 * "winos_ñ" em UTF-8 (bytes explícitos, independente do encoding do fonte):
 *   77 69 6e 6f 73 5f c3 b1 00  = 9 bytes (ñ = U+00F1 = C3 B1)
 * Esperado em UTF-16: w i n o s _ 00F1 0000 = 8 unidades.
 *
 * Imports KERNEL32: MultiByteToWideChar, WideCharToMultiByte (+CRT msvcrt).
 *
 * Contrato de saída: 42 = fluxo completo OK; 10..15 = primeira falha (etapa).
 */
#include <windows.h>

static const char kUtf8[] = {
    0x77, 0x69, 0x6E, 0x6F, 0x73, 0x5F, (char)0xC3, (char)0xB1, 0
};
#define ULEN 8   /* unidades UTF-16 com NUL */
#define BLEN 9   /* bytes UTF-8 com NUL */

int main(void) {
    wchar_t wbuf[32];
    char out[32];
    int nw, nb;

    /* 1) UTF-8 → UTF-16 (comprimento -1 = NUL-terminated) */
    nw = MultiByteToWideChar(CP_UTF8, 0, kUtf8, -1, wbuf, 32);
    if (nw == 0) return 10;
    if (nw != ULEN) return 11;
    if (wbuf[6] != 0x00F1 || wbuf[7] != 0) return 12;

    /* 2) UTF-16 → UTF-8 (round-trip, comprimento -1) */
    nb = WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, out, 32, NULL, NULL);
    if (nb == 0) return 13;
    if (nb != BLEN) return 14;
    for (int i = 0; i < BLEN; i++)
        if (out[i] != kUtf8[i]) return 15;

    return 42;
}
