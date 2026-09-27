/* PE real MinGW-w64 (GRUPO 1): stdio formatado real do msvcrt.
 * Exige printf/fprintf/fwrite/vfprintf + __iob_func e valida a SAÍDA. */
#include <stdio.h>
#include <stdarg.h>

static void vlogf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static int addup(int a, int b) { return a + b; }

int main(void) {
    int n = printf("n=%d s=%s hex=%x\n", 42, "winos", 0xBEEF);
    fprintf(stdout, "f=%d %c %5d|\n", 7, 'A', 3);
    fwrite("raw-bytes\n", 1, 10, stdout);
    vlogf("err %s %d\n", "v", 9);
    printf("sum=%d ptr=%p\n", addup(40, 2), (void*)main);
    return n > 0 ? 5 : 1;
}
