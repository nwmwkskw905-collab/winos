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
    double x = poly(&xin);                 // 24.75
    int ix = (int)(x*100); // 2475
    if (ix != 2475) return 1;
    volatile double arr[4] = { 1.0, 2.0, 3.0, 4.0 };
    double total = 0.0;
    for (int i = 0; i < 4; i++) total += arr[i] * arr[i];   // 30.0
    int itotal = (int)total;
    if (itotal != 30) return 2;
    double q = total / 3.0;                // 10.0
    int iq = (int)q;
    if (iq != 10) return 3;
    double r = sqrt(q);                    // ~3.162
    int ir = (int)(r*1000); // 3162
    if (ir < 3160 || ir > 3163) return 4;
    volatile float fv[3] = { 1.5f, 2.0f, 2.5f };
    float fs = fsum(fv, 3);                // 12.0
    int ifs = (int)fs;
    if (ifs != 12) return 5;
    double cvt = (double)fs + x;           // 36.75
    int icvt = (int)(cvt*100); // 3675
    if (icvt != 3675) return 6;
    int cmp = x > 10.0 ? 1 : 0;            // 1
    if (cmp != 1) return 7;
    long ip = (long)(x * 4.0);             // 99
    if (ip != 99) return 8;
    return 42;
}
