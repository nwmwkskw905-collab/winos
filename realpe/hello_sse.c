/* PE real MinGW-w64 (GRUPO 3): aritmética SSE2 real (volatile impede fold).
 * Valida resultado numérico no retorno e na saída formatada. */
#include <stdio.h>
#include <math.h>

static double poly(volatile double* x) {
    double v = *x;
    return 3.0 * v * v + 2.0 * v + 1.0;
}

static float fsum(const volatile float* a, int n) {
    float s = 0.0f;
    for (int i = 0; i < n; i++) s += a[i] * 2.0f;
    return s;
}

int main(void) {
    volatile double xin = 2.5;
    double x = poly(&xin);                 /* 24.75 */
    volatile double arr[4] = { 1.0, 2.0, 3.0, 4.0 };
    double total = 0.0;
    for (int i = 0; i < 4; i++) total += arr[i] * arr[i];   /* 30.0 */
    double q = total / 3.0;                /* 10.0 */
    double r = sqrt(q);                    /* ~3.16227766 */
    volatile float fv[3] = { 1.5f, 2.0f, 2.5f };
    float fs = fsum(fv, 3);                /* 12.0 */
    double cvt = (double)fs + x;           /* 36.75 */
    int cmp = x > 10.0 ? 1 : 0;            /* 1 */
    long ip = (long)(x * 4.0);             /* 99 */
    printf("x=%.2f total=%.1f q=%.1f r=%.3f fs=%.1f cvt=%.2f cmp=%d ip=%ld\n",
           x, total, q, r, fs, cvt, cmp, ip);
    return (ip == 99 && cmp == 1) ? 7 : 1;
}
