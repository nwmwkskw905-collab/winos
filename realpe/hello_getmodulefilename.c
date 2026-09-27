#include <windows.h>

/* hello_getmodulefilename — validação por PE real de kernel32!GetModuleFileNameA.
 * Representação real do módulo principal (inventário G65): o loader carrega o PE
 * com pr_pe_load(..., "app.exe") → nome canônico do módulo principal = "app.exe".
 * Contrato de retorno:
 *   10 = LastError alterado em caminho de sucesso/truncamento
 *   11 = retorno inválido (n==0 / n>=260 / nSize=0 não retornou 0)
 *   12 = conteúdo inválido (NUL ausente / bytes != "app.exe")
 *   13 = canary/marker alterado (escrita além do permitido)
 *   14 = truncamento inesperado (buffer pequeno)
 *   15 = segunda chamada inconsistente
 *   65 = fluxo completo */

int main(void) {
    struct { char b[260]; unsigned char can[4]; } big;
    struct { unsigned char b[8]; unsigned char can[4]; } small;
    char b2[260];
    DWORD n, n2, r;
    int i;

    /* prepara canary/marker */
    for (i = 0; i < 4; i++) big.can[i] = 0xCC;
    for (i = 0; i < 8; i++) small.b[i] = 0xEE;
    for (i = 0; i < 4; i++) small.can[i] = 0xCC;

    /* §8 — caminho normal (buffer grande) */
    SetLastError(0xDEAD);
    n = GetModuleFileNameA(NULL, big.b, sizeof(big.b));
    if (n == 0 || n >= sizeof(big.b)) return 11;
    if (big.b[n] != 0) return 12;
    /* §9 — conteúdo objetivo: nome real do módulo = "app.exe" (7 chars) */
    if (n != 7 || big.b[0] != 'a' || big.b[1] != 'p' || big.b[2] != 'p' ||
        big.b[3] != '.' || big.b[4] != 'e' || big.b[5] != 'x' || big.b[6] != 'e')
        return 12;
    for (i = 0; i < 4; i++)
        if (big.can[i] != 0xCC) return 13;
    if (GetLastError() != 0xDEAD) return 10;

    /* §12 — identidade entre chamadas consecutivas */
    for (i = 0; i < 260; i++) b2[i] = 0;
    n2 = GetModuleFileNameA(NULL, b2, sizeof(b2));
    if (n2 != n) return 15;
    for (i = 0; i < (int)n; i++)
        if (b2[i] != big.b[i]) return 15;
    if (b2[n2] != 0) return 15;

    /* §10 — buffer pequeno + truncamento (nSize=4 < 7+NUL) */
    SetLastError(0xDEAD);
    r = GetModuleFileNameA(NULL, (char*)small.b, 4);
    if (r != 3) return 14;
    if (small.b[0] != 'a' || small.b[1] != 'p' || small.b[2] != 'p' || small.b[3] != 0)
        return 14;
    for (i = 4; i < 8; i++)
        if (small.b[i] != 0xEE) return 13;
    for (i = 0; i < 4; i++)
        if (small.can[i] != 0xCC) return 13;
    if (GetLastError() != 0xDEAD) return 10;

    /* §11 — nSize = 0: retorno 0, nada escrito */
    r = GetModuleFileNameA(NULL, big.b, 0);
    if (r != 0) return 14;

    return 65;
}
