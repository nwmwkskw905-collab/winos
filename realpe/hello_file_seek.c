/* hello_file_seek.c — G32: validação real de SetFilePointer por PE x64.
 *
 * Fluxo (determinístico, arquivo único dentro do prefixo/VFS):
 *   CreateFileA(GENERIC_READ|GENERIC_WRITE, CREATE_ALWAYS) → WriteFile(16 bytes
 *   "AAAABBBBCCCCDDDD") → SetFilePointer(8, FILE_BEGIN) → ReadFile(4 bytes) →
 *   comparar com "CCCC" → CloseHandle → return 42.
 *
 * A prova: após o WriteFile a posição do stream = 16 (EOF) — sem o reposiciona-
 * mento o ReadFile devolveria 0 bytes; na posição 0 devolveria "AAAA". Ler
 * exatamente "CCCC" (offset 8) comprova que o ponteiro mudou.
 * Imports KERNEL32: CreateFileA, WriteFile, SetFilePointer, ReadFile, CloseHandle.
 *
 * Contrato de saída: 42 = fluxo completo OK; 10..17 = primeira falha (etapa).
 */
#include <windows.h>

static const char kPath[] = "winos_seek_test.txt";
static const char kData[] = "AAAABBBBCCCCDDDD";   /* offset 8..11 = "CCCC" */
#define KLEN 16
#define SEEK_OFF 8

int main(void) {
    char buf[8];
    DWORD wr = 0, got = 0;
    HANDLE h;

    /* 1) criar e escrever a sequência conhecida */
    h = CreateFileA(kPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 10;
    if (!WriteFile(h, kData, (DWORD)KLEN, &wr, NULL)) return 11;
    if (wr != (DWORD)KLEN) return 12;

    /* 2) reposicionar (posição atual = 16 → 8) e verificar o retorno */
    if (SetFilePointer(h, (LONG)SEEK_OFF, NULL, FILE_BEGIN) != (DWORD)SEEK_OFF)
        return 13;

    /* 3) ler a partir do novo offset e comparar */
    if (!ReadFile(h, buf, 4, &got, NULL)) return 14;
    if (got != 4) return 15;
    for (DWORD i = 0; i < 4; i++)
        if (buf[i] != kData[SEEK_OFF + i]) return 16;   /* espera "CCCC" */

    /* 4) fechar e sair */
    if (!CloseHandle(h)) return 17;
    return 42;
}
