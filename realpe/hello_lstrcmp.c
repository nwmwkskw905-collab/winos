#include <windows.h>

/* hello_lstrcmp — validação por PE real de kernel32!lstrcmpA.
 * Contrato de retorno (10–19 = falha; 60 = sucesso):
 *   10 = igualdade: "abc" vs "abc" != 0 ou "" vs "" != 0
 *   11 = "abc" vs "abd" não < 0
 *   12 = "abd" vs "abc" não > 0
 *   13 = prefixos: "abc" vs "abcd" não < 0 ou "abcd" vs "abc" não > 0
 *   14 = "apple" vs "banana" não < 0 (diferença no 1º byte)
 *   15 = "PorticoA" vs "PorticoB" não < 0 (diferença tardia)
 *   16 = NUL interno: "abc\0xyz" vs "abc\0other" != 0
 *   17 = buffers originais modificados
 *   18 = LastError alterado no sucesso
 *   19 = caminhos NULL (esperado: retorno 0 + last_error 87 — observação do Portico)
 * Sinais testados como ==0 / <0 / >0 (sem fixar valor numérico; §7). */

static int same(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

int main(void) {
    char s1[] = "abc", t1[] = "abc";
    char e1[] = "", e2[] = "";
    char s3[] = "abc", t3[] = "abd";
    char s4[] = "abd", t4[] = "abc";
    char s5[] = "abc", t5[] = "abcd";
    char s6[] = "abcd", t6[] = "abc";
    char s7[] = "apple", t7[] = "banana";
    char s8[] = "PorticoA", t8[] = "PorticoB";
    char s9[8] = { 'a', 'b', 'c', 0, 'x', 'y', 'z', 0 };
    char t9[9] = { 'a', 'b', 'c', 0, 'o', 't', 'h', 'e', 'r' };
    int r;

    /* 1 — igualdade simples + 2 — igualdade de vazias */
    r = lstrcmpA(s1, t1);
    if (r != 0) return 10;
    r = lstrcmpA(e1, e2);
    if (r != 0) return 10;

    /* 3 — primeira menor */
    r = lstrcmpA(s3, t3);
    if (!(r < 0)) return 11;

    /* 4 — primeira maior */
    r = lstrcmpA(s4, t4);
    if (!(r > 0)) return 12;

    /* 5 — prefixo: "abc" < "abcd"; 6 — inverso: "abcd" > "abc" */
    r = lstrcmpA(s5, t5);
    if (!(r < 0)) return 13;
    r = lstrcmpA(s6, t6);
    if (!(r > 0)) return 13;

    /* 7 — diferença no primeiro caractere */
    r = lstrcmpA(s7, t7);
    if (!(r < 0)) return 14;

    /* 8 — diferença depois de vários caracteres */
    r = lstrcmpA(s8, t8);
    if (!(r < 0)) return 15;

    /* 9 — NUL interno: comparação para no primeiro NUL */
    r = lstrcmpA(s9, t9);
    if (r != 0) return 16;

    /* 10 — origens não modificadas (byte a byte vs conteúdo esperado) */
    if (!same(s1, "abc") || !same(t1, "abc")) return 17;
    if (!same(e1, "") || !same(e2, "")) return 17;
    if (!same(s3, "abc") || !same(t3, "abd")) return 17;
    if (!same(s4, "abd") || !same(t4, "abc")) return 17;
    if (!same(s5, "abc") || !same(t5, "abcd")) return 17;
    if (!same(s6, "abcd") || !same(t6, "abc")) return 17;
    if (!same(s7, "apple") || !same(t7, "banana")) return 17;
    if (!same(s8, "PorticoA") || !same(t8, "PorticoB")) return 17;
    if (s9[0] != 'a' || s9[1] != 'b' || s9[2] != 'c' || s9[3] != 0 ||
        s9[4] != 'x' || s9[5] != 'y' || s9[6] != 'z' || s9[7] != 0) return 17;
    if (t9[0] != 'a' || t9[1] != 'b' || t9[2] != 'c' || t9[3] != 0 ||
        t9[4] != 'o' || t9[5] != 't' || t9[6] != 'h' || t9[7] != 'e' || t9[8] != 'r') return 17;

    /* 11 — LastError intacto no sucesso */
    SetLastError(0xDEAD);
    (void)lstrcmpA(s1, t1);
    if (GetLastError() != 0xDEAD) return 18;

    /* §8 — caminhos NULL explícitos (retorno 0 + error 87; observação do Portico) */
    SetLastError(0xDEAD);
    r = lstrcmpA(NULL, s1);
    if (r != 0 || GetLastError() != 87) return 19;
    SetLastError(0xDEAD);
    r = lstrcmpA(s1, NULL);
    if (r != 0 || GetLastError() != 87) return 19;

    return 60;
}
