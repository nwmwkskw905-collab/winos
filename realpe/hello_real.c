/* hello_real.c — FASE 3: primeiro PE REAL (MinGW-w64, CRT real).
 * - inicia pelo CRT real (mainCRTStartup do MinGW)
 * - memcpy/memset reais
 * - cálculo inteiro
 * - função importada de kernel32 (GetTickCount64)
 * - exit code verificável: 42 quando tudo confere */
#include <string.h>
#include <windows.h>

static int checksum(const unsigned char* p, int n) {
    int s = 0;
    for (int i = 0; i < n; i++) s = (s * 31 + p[i]) & 0xFFFF;
    return s;
}

int main(void) {
    unsigned char a[64], b[64];
    memset(a, 0x11, sizeof a);
    memcpy(b, a, sizeof b);

    int s = 0;
    for (int i = 1; i <= 10; i++) s += i * 3;   /* 165 */

    /* importada de kernel32 — resultado vai para sink volátil (não some) */
    volatile ULONGLONG tick = GetTickCount64();
    (void)tick;

    int bad = 0;
    if (b[5] != 0x11) bad++;
    if (checksum(b, 64) != checksum(a, 64)) bad++;
    if (s != 165) bad++;
    return 42 - bad;   /* 42 = sucesso verificável */
}
