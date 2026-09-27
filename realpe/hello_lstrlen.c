#include <windows.h>

/* hello_lstrlen — validação por PE real de kernel32!lstrlenA.
 * Contrato de retorno:
 *   10 = lstrlenA("") != 0
 *   11 = lstrlenA("abc") != 3
 *   12 = lstrlenA("PorticoRuntime") != 14
 *   13 = NUL interno não respeitado (buffer "abc\0xyz\0" != 3)
 *   14 = comprimento errado no buffer com canary OU canary alterado
 *   15 = LastError alterado no caminho de sucesso
 *   16 = caminho explícito NULL do handler != 0
 *   17 = caminho de erro não definiu last_error (esperado 87 = ERROR_INVALID_PARAMETER;
 *        observação do Portico, não contrato universal de Windows)
 *   58 = fluxo completo */

int main(void) {
    char empty[] = "";
    char abc[] = "abc";
    char longstr[] = "PorticoRuntime";   /* 14 chars; esperado local conhecido = 14 */
    unsigned char innul[8] = { 'a', 'b', 'c', 0, 'x', 'y', 'z', 0 };
    unsigned char canary[16] = {
        'P', 'o', 'r', 't', 'i', 'c', 'o', 0,
        0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC
    };
    int r, i;

    /* §6 — string vazia: somente NUL => comprimento 0 */
    r = lstrlenA(empty);
    if (r != 0) return 10;

    /* §7 — string simples */
    r = lstrlenA(abc);
    if (r != 3) return 11;

    /* §8 — string maior (esperado conhecido localmente; sem usar lstrlenA) */
    r = lstrlenA(longstr);
    if (r != 14) return 12;

    /* §9 — NUL interno: para no primeiro NUL, não percorre bytes posteriores */
    r = lstrlenA((const char*)innul);
    if (r != 3) return 13;

    /* §10 — memória e canary: comprimento correto e sem escrita */
    r = lstrlenA((const char*)canary);
    if (r != 7) return 14;
    for (i = 8; i < 16; i++)
        if (canary[i] != 0xCC) return 14;

    /* §14 — LastError intacto no caminho de sucesso */
    SetLastError(0xDEAD);
    (void)lstrlenA(abc);
    if (GetLastError() != 0xDEAD) return 15;

    /* §11 — caminho explícito NULL do handler (observação do Portico) */
    r = lstrlenA(NULL);
    if (r != 0) return 16;

    /* §11/§14 — caminho de erro define last_error = 87 (atual do Portico) */
    SetLastError(0xDEAD);
    (void)lstrlenA(NULL);
    if (GetLastError() != 87) return 17;

    return 58;
}
