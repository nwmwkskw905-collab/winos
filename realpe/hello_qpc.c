/* hello_qpc.c — G36: validação real de QPC/QPF por PE x64.
 *
 * Fluxo (determinístico):
 *   QueryPerformanceFrequency(&f) → TRUE e f == 1000000000 (contrato declarado
 *   do runtime: contador em nanossegundos; travado pelo teste unitário) →
 *   QueryPerformanceCounter(&t1) → TRUE → QueryPerformanceCounter(&t2) → TRUE e
 *   t2 >= t1 (monotônico) → return 42.
 *
 * LARGE_INTEGER real de 8 bytes (QuadPart). Não depende do valor absoluto do
 * contador — apenas monotonicidade relativa (t2 >= t1; valores iguais são
 * legítimos conforme a resolução efetiva).
 * Imports KERNEL32: QueryPerformanceFrequency, QueryPerformanceCounter (+CRT).
 *
 * Contrato de saída: 42 = fluxo completo OK; 10..14 = primeira falha (etapa).
 */
#include <windows.h>
#include <stdio.h>

int main(void) {
    LARGE_INTEGER f, t1, t2;

    /* 1) frequência do contador */
    if (!QueryPerformanceFrequency(&f)) return 10;
    if (f.QuadPart != 1000000000LL) return 11;

    /* 2) duas leituras do contador */
    if (!QueryPerformanceCounter(&t1)) return 12;
    if (!QueryPerformanceCounter(&t2)) return 13;

    /* 3) monotonicidade (t2 >= t1) */
    if (t2.QuadPart < t1.QuadPart) return 14;

    /* registro dos valores observados (captura de console do runtime) */
    printf("qpc f=%llu t1=%llu t2=%llu d=%llu\n",
           (unsigned long long)f.QuadPart,
           (unsigned long long)t1.QuadPart,
           (unsigned long long)t2.QuadPart,
           (unsigned long long)(t2.QuadPart - t1.QuadPart));
    return 42;
}
