#include <windows.h>

/* hello_tickcount64 — validação por PE real de kernel32!GetTickCount64.
 * ULONGLONG GetTickCount64(void) — retorno inteiro de 64 bits em RAX.
 * Somente propriedades estáveis (nenhum valor absoluto de host, sem sleeps):
 *   10 = b < a (regressão temporal entre chamadas sucessivas)
 *   11 = c < b
 *   12 = c < a
 *   13 = delta c-a incoerente (lixo/truncamento observável em bits altos)
 *   54 = fluxo completo */

int main(void) {
    ULONGLONG a, b, c;

    /* Cenário A: chamada inicial — armazenamento integral em 64 bits */
    a = GetTickCount64();

    /* Cenário B: segunda chamada — sem regressão temporal (b >= a) */
    b = GetTickCount64();
    if (b < a) return 10;

    /* Cenário C: terceira chamada — monotonicidade não decrescente */
    c = GetTickCount64();
    if (c < b) return 11;
    if (c < a) return 12;

    /* Ausência de truncamento/lixo observável: três chamadas consecutivas não
     * podem transcorrer mais de 0x00FFFFFF ms nem exibir lixo em bits altos. */
    if (c - a > 0x00FFFFFFull) return 13;

    return 54;
}
