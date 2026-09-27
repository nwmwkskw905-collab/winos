/* hello_heap.c — G34: validação real do Heap Win32 por PE x64.
 *
 * Fluxo (só o caminho essencial): GetProcessHeap → HeapAlloc(64 bytes) →
 * escrever padrão determinístico → reler do heap do guest e conferir todos os
 * bytes → HeapSize == 64 → HeapFree → return 42.
 *
 * A memória é acessada via ponteiro volatile: o reler confere de verdade o que
 * está no heap do guest (sem otimização do compilador eliminar a verificação).
 * Imports KERNEL32: GetProcessHeap, HeapAlloc, HeapSize, HeapFree (+CRT msvcrt).
 *
 * Contrato de saída: 42 = fluxo completo OK; 10..15 = primeira falha (etapa).
 */
#include <windows.h>

#define HEAP_BYTES 64

int main(void) {
    /* 1/2) heap do processo */
    HANDLE h = GetProcessHeap();
    if (h == NULL) return 10;

    /* 3/4) alocar bloco pequeno determinístico */
    volatile unsigned char* p =
        (volatile unsigned char*)HeapAlloc(h, 0, (SIZE_T)HEAP_BYTES);
    if (p == NULL) return 11;

    /* 5) padrão conhecido em todos os bytes */
    for (int i = 0; i < HEAP_BYTES; i++)
        p[i] = (unsigned char)(0xA5 ^ i);

    /* 6) reler e conferir byte a byte (corrupção = etapa 12) */
    for (int i = 0; i < HEAP_BYTES; i++)
        if (p[i] != (unsigned char)(0xA5 ^ i)) return 12;

    /* 7/8) tamanho do bloco alocado */
    SIZE_T sz = HeapSize(h, 0, (const void*)p);
    if (sz == (SIZE_T)-1) return 13;
    if (sz != (SIZE_T)HEAP_BYTES) return 14;

    /* 9/10) liberar */
    if (!HeapFree(h, 0, (void*)p)) return 15;

    return 42;
}
