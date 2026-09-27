#include <windows.h>

/* hello_tlsalloc — validação por PE x64 real de kernel32!TlsAlloc.
 * Infra existente: ctx->tls_slots[64]; reservas internas do CRT = [60] lconv,
 * [61] strerror, [62] errno, [63] iob → alocáveis = 0..59 (60 slots).
 * Valor inicial de um slot = NULL (calloc) e TlsGetValue limpa LastError em
 * sucesso (contrato G55 observado).
 * Contratos (exit codes):
 *   10 = TlsAlloc falhou
 *   11 = índice fora da faixa suportada
 *   12 = índice em slot reservado (60..63)
 *   13 = TlsGetValue rejeitou / valor inicial fora do contrato
 *   14 = segunda alocação igual à primeira
 *   15 = LastError alterado por TlsAlloc em sucesso
 *   16 = exaustão incorreta (capacidade/erro)
 *   70 = fluxo completo */

int main(void) {
    DWORD idx, idx2, i, total;
    LPVOID v;

    /* §12 — alocação principal */
    SetLastError(0xDEAD);
    idx = TlsAlloc();
    if (idx == 0xFFFFFFFFu) return 10;
    if (idx >= 60 && idx <= 63) return 12;
    if (idx > 59) return 11;
    if (GetLastError() != 0xDEAD) return 15;

    /* §7/§14 — integração com TlsGetValue (G55): aceita o índice; vazio = NULL;
     * sucesso limpa LastError (contrato observado G55) */
    SetLastError(0xDEAD);
    v = TlsGetValue(idx);
    if (v != NULL) return 13;
    if (GetLastError() != 0) return 13;

    /* §18 — segunda alocação: slot distinto (arquitetura marca alocação) */
    idx2 = TlsAlloc();
    if (idx2 == 0xFFFFFFFFu) return 10;
    if (idx2 == idx) return 14;
    if (idx2 >= 60 && idx2 <= 63) return 12;
    if (idx2 > 59) return 11;

    /* §19 — exaustão determinística: 60 alocáveis no total */
    total = 2;   /* idx e idx2 já alocados */
    for (i = 0; i < 70; i++) {
        DWORD x = TlsAlloc();
        if (x == 0xFFFFFFFFu) {
            if (total != 60) return 16;
            if (GetLastError() != 8) return 16;   /* ERROR_NOT_ENOUGH_MEMORY */
            return 70;
        }
        if (x >= 60) return 12;
        total++;
    }
    return 16;   /* nunca esgotou */
}
