#include <windows.h>

/* hello_getenv — validação por PE real de kernel32!GetEnvironmentVariableA.
 * Variável de teste semeada pela infraestrutura existente (pr_win32_env_set):
 *   G53_VAR = "abcdefghij0123456789"   (comprimento 20; need = 21 com NUL)
 * Auxiliares de assertion: SetLastError/GetLastError (contrato de erro público
 * da própria API). Contrato de retorno:
 *   10 = cenário A: comprimento retornado != 20
 *   11 = cenário A: conteúdo/NUL incorretos
 *   12 = cenário A: escrita além de strlen+1 (canário)
 *   13 = lookup case-insensitive falhou
 *   14 = cenário B: variável inexistente não retornou 0
 *   15 = cenário B: GetLastError != ERROR_ENVVAR_NOT_FOUND
 *   16 = cenário B: buffer tocado
 *   17 = cenário C: retorno != 21 (tamanho com NUL)
 *   18 = cenário C: GetLastError != ERROR_INSUFFICIENT_BUFFER
 *   19 = cenário C: buffer tocado (canário)
 *   20 = cenário D: retorno != 21 (consulta de tamanho)
 *   21 = cenário D: GetLastError != ERROR_INSUFFICIENT_BUFFER
 *   53 = fluxo completo */

int main(void) {
    char buf[64];
    unsigned i;
    DWORD r;

    /* ---- Cenário A: variável existente, buffer grande ---- */
    for (i = 0; i < sizeof buf; i++) buf[i] = (char)0xCC;
    r = GetEnvironmentVariableA("G53_VAR", buf, sizeof buf);
    if (r != 20) return 10;
    if (buf[0] != 'a' || buf[19] != '9' || buf[20] != 0) return 11;
    if (buf[21] != (char)0xCC) return 12;

    /* ---- Lookup case-insensitive (contrato público Win32) ---- */
    r = GetEnvironmentVariableA("g53_var", buf, sizeof buf);
    if (r != 20) return 13;

    /* ---- Cenário B: variável inexistente ---- */
    for (i = 0; i < sizeof buf; i++) buf[i] = (char)0xCC;
    SetLastError(0);
    r = GetEnvironmentVariableA("G53_INEXISTENTE_XYZ", buf, sizeof buf);
    if (r != 0) return 14;
    if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) return 15;
    if (buf[0] != (char)0xCC) return 16;

    /* ---- Cenário C: buffer insuficiente (precisa de 21, damos 8) ---- */
    for (i = 0; i < sizeof buf; i++) buf[i] = (char)0xCC;
    SetLastError(0);
    r = GetEnvironmentVariableA("G53_VAR", buf, 8);
    if (r != 21) return 17;
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return 18;
    if (buf[0] != (char)0xCC) return 19;

    /* ---- Cenário D: consulta de tamanho (lpBuffer = NULL) ---- */
    SetLastError(0);
    r = GetEnvironmentVariableA("G53_VAR", NULL, 0);
    if (r != 21) return 20;
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) return 21;

    return 53;
}
