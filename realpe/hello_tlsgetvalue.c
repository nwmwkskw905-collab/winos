#include <windows.h>

/* hello_tlsgetvalue — validação por PE real de kernel32!TlsGetValue.
 * TLS parcial do Portico (sem TlsAlloc/TlsSetValue — não implementados aqui):
 * somente os caminhos demonstráveis no estado atual: slot vazio e índice
 * inválido. Inventário: tls_slots[64]; válidos 0..63 (61..63 = convenção
 * interna do CRT, evitados); 0 = vazio; 64 = primeiro inválido.
 * Auxiliares de assertion: SetLastError (preparação inequívoca) e GetLastError.
 * Contrato de retorno:
 *   10 = A(0): retorno != NULL
 *   11 = A(0): GetLastError != 0 (sucesso não limpou LastError)
 *   12 = B(64): retorno != NULL
 *   13 = B(64): GetLastError != ERROR_INVALID_HANDLE
 *   14 = B(0xFFFFFFFF): retorno != NULL
 *   15 = B(0xFFFFFFFF): GetLastError != ERROR_INVALID_HANDLE
 *   55 = fluxo completo */

int main(void) {
    LPVOID r;

    /* Cenário A — índice válido vazio (0): sucesso com valor NULL; o
     * handler limpa LastError no sucesso (propriedade exercitada). */
    SetLastError(0xDEAD);
    r = TlsGetValue(0);
    if (r != NULL) return 10;
    if (GetLastError() != 0) return 11;

    /* Cenário B1 — primeiro índice inválido (64): fora de tls_slots[0..63] */
    SetLastError(0xDEAD);
    r = TlsGetValue(64);
    if (r != NULL) return 12;
    if (GetLastError() != ERROR_INVALID_HANDLE) return 13;

    /* Cenário B2 — DWORD máximo (0xFFFFFFFF): fora do intervalo */
    SetLastError(0xDEAD);
    r = TlsGetValue(0xFFFFFFFFu);
    if (r != NULL) return 14;
    if (GetLastError() != ERROR_INVALID_HANDLE) return 15;

    return 55;
}
