/* PE real MinGW-w64 (GRUPO 5): programa composto — CRT, stdio formatado,
 * heap (malloc/realloc/free), DLL real (LoadLibraryA/GetProcAddress/
 * chamada MS x64/FreeLibrary), SSE2 (doubles), CPUID/RDTSC determinísticos,
 * múltiplas funções (com .pdata/.xdata reais). Sai com 42 se TUDO ok. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <windows.h>
#include <intrin.h>

typedef int (*add3_fn)(int, int);

/* várias funções -> RUNTIME_FUNCTION na .pdata */
static double poly(double x) { return 3.0 * x * x + 2.0 * x + 1.0; }

static int fib(int n) {
    if (n < 2) return n;
    int a = 0, b = 1;
    for (int i = 2; i <= n; i++) { int t = a + b; a = b; b = t; }
    return b;
}

static int heap_work(void) {
    char* s = (char*)malloc(32);
    if (!s) return -1;
    memcpy(s, "winos-heap", 11);
    s = (char*)realloc(s, 64);
    if (!s) return -1;
    strcat(s, "-ok");
    int bad = strcmp(s, "winos-heap-ok") != 0;
    free(s);
    return bad ? -1 : 11;
}

static int dll_work(void) {
    HINSTANCE h = LoadLibraryA("hello_dll.dll");
    if (!h) return -1;
    add3_fn f = (add3_fn)GetProcAddress(h, "add3");
    if (!f) return -2;
    int r = f(39, 0);
    if (!FreeLibrary(h)) return -3;
    return r;   /* 42 */
}

int main(void) {
    volatile double xin = 2.5;
    double x = poly(xin);                    /* 24.75 (SSE2 real) */
    double r = sqrt(x);                      /* ~4.9749 */
    int f10 = fib(10);                       /* 55 */
    int hw = heap_work();                    /* 11 */
    int dw = dll_work();                     /* 42 */

    int info[4] = { 0, 0, 0, 0 };
    __cpuid(info, 0);                        /* CPUID folha 0 (determinístico) */
    char vendor[13] = { 0 };
    memcpy(vendor + 0, &info[1], 4);
    memcpy(vendor + 4, &info[3], 4);
    memcpy(vendor + 8, &info[2], 4);
    unsigned long long t0 = __rdtsc();       /* RDTSC determinístico */

    printf("x=%.2f r=%.3f fib=%d heap=%d dll=%d\n", x, r, f10, hw, dw);
    printf("cpu=%.12s tsc>0=%d\n", vendor, t0 > 0 ? 1 : 0);

    if (x < 24.0 || x > 25.0) return 1;
    if (r < 4.9 || r > 5.0) return 2;
    if (f10 != 55) return 3;
    if (hw != 11) return 4;
    if (dw != 42) return 5;
    return 42;
}
