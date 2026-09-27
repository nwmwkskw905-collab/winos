#include <windows.h>

/* hello_tlssetvalue — validação por PE x64 real de kernel32!TlsSetValue.
 * Ciclo completo: TlsAlloc → TlsSetValue → tls_slots[idx] → TlsGetValue.
 * Contrato Portico (Opção B): só índice alocado por TlsAlloc (0..59, bit em
 * tls_used); 60..63 reservados do CRT (lconv/strerror/errno/iob) NUNCA aceitos;
 * índices não alocados/fora da capacidade → FALSE + ERROR_INVALID_HANDLE (6,
 * código da família TLS = mesmo de TlsGetValue G55). NULL = valor permitido
 * (não libera o índice). Sucesso preserva LastError.
 * Contratos (exit codes):
 *   10 = TlsAlloc falhou
 *   11 = ciclo Set(VALUE_A)→Get falhou
 *   12 = VALUE_B não sobrescreveu
 *   13 = NULL após valor não tratado / índice liberado indevidamente
 *   14 = índice inválido aceito / LastError de falha incorreto
 *   15 = slot reservado aceito / corrupção de slot interno
 *   16 = LastError de sucesso alterado
 *   17 = largura 64-bit corrompida
 *   71 = fluxo completo */

#define VALUE_A 0x123456789ABCDEF0ull
#define VALUE_B 0x0FEDCBA987654321ull

int main(void) {
    DWORD idx, other, i;
    LPVOID v;
    LPVOID save[4];

    idx = TlsAlloc();
    if (idx == 0xFFFFFFFFu || idx > 59) return 10;

    /* §14 — preservar estado dos slots internos (leitura via API pública) */
    for (i = 0; i < 4; i++) save[i] = TlsGetValue(60 + i);

    /* §10/§18 — ciclo principal: VALUE_A (sentinela 64-bit, §8/§16) */
    SetLastError(0xDEAD);
    if (TlsSetValue(idx, (LPVOID)VALUE_A) != TRUE) return 11;
    if (GetLastError() != 0xDEAD) return 16;
    v = TlsGetValue(idx);
    if (v != (LPVOID)VALUE_A) {
        if ((ULONG_PTR)v == (DWORD)VALUE_A) return 17;  /* truncado 32-bit */
        return 11;
    }

    /* §11/§21 — segundo valor sobrescreve */
    if (TlsSetValue(idx, (LPVOID)VALUE_B) != TRUE) return 11;
    if (TlsGetValue(idx) != (LPVOID)VALUE_B) return 12;

    /* §7/§12 — NULL como valor: aceito; NÃO libera o índice */
    if (TlsSetValue(idx, NULL) != TRUE) return 13;
    if (TlsGetValue(idx) != NULL) return 13;
    if (TlsSetValue(idx, (LPVOID)VALUE_A) != TRUE) return 13;  /* ainda alocado */

    /* §13 — índices inválidos: FALSE + 6 */
    other = (idx == 59) ? 58 : 59;   /* garantidamente não alocado */
    SetLastError(0xDEAD);
    if (TlsSetValue(0xFFFFFFFFu, (LPVOID)VALUE_A) != FALSE) return 14;
    if (GetLastError() != 6) return 14;
    SetLastError(0xDEAD);
    if (TlsSetValue(64, (LPVOID)VALUE_A) != FALSE) return 14;
    if (GetLastError() != 6) return 14;
    SetLastError(0xDEAD);
    if (TlsSetValue(other, (LPVOID)VALUE_A) != FALSE) return 14;  /* Opção B */
    if (GetLastError() != 6) return 14;

    /* §6/§14/§19 — reservados 60..63: FALSE + 6 e nenhum slot interno alterado */
    SetLastError(0xDEAD);
    if (TlsSetValue(60, (LPVOID)VALUE_A) != FALSE) return 15;
    if (GetLastError() != 6) return 15;
    if (TlsSetValue(63, (LPVOID)VALUE_A) != FALSE) return 15;
    for (i = 0; i < 4; i++)
        if (TlsGetValue(60 + i) != save[i]) return 15;

    /* valor do slot do PE intacto após as tentativas */
    if (TlsGetValue(idx) != (LPVOID)VALUE_A) return 13;

    return 71;
}
