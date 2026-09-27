/* hello_file_w.c — G31: validação real do caminho Unicode (CreateFileW).
 *
 * Fluxo (determinístico, arquivo único dentro do prefixo/VFS do runtime):
 *   CreateFileW(CREATE_ALWAYS, GENERIC_WRITE) → WriteFile → CloseHandle →
 *   CreateFileW(OPEN_EXISTING, GENERIC_READ) → ReadFile → comparar →
 *   CloseHandle → return 42.
 *
 * Arquivo: nome com caractere NÃO-ASCII (ñ = U+00F1) para comprovar que o
 * caminho passa de fato pela conversão UTF-16 de CreateFileW.
 * A string UTF-16 é montada COM UNIDADES EXPLÍCITAS (wchar_t numérico = UTF-16
 * no MinGW x86_64) — independente do encoding do código-fonte.
 * Imports KERNEL32: CreateFileW, WriteFile, ReadFile, CloseHandle (+CRT msvcrt).
 *
 * Contrato de saída: 42 = fluxo completo OK; 10..19 = primeira falha (etapa).
 */
#include <windows.h>

/* "winos_ñ.txt" em UTF-16: w i n o s _ ñ(F1) . t x t */
static const wchar_t kPath[] = {
    0x0077, 0x0069, 0x006E, 0x006F, 0x0073, 0x005F,
    0x00F1,
    0x002E, 0x0074, 0x0078, 0x0074, 0x0000
};
static const char kData[] = "winos-file-w-g31";   /* 16 bytes, determinístico */
#define KLEN 16

int main(void) {
    char buf[32];
    DWORD wr = 0, got = 0;
    HANDLE h;

    /* 1) criar e escrever (caminho Unicode) */
    h = CreateFileW(kPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 10;
    if (!WriteFile(h, kData, (DWORD)KLEN, &wr, NULL)) return 11;
    if (wr != (DWORD)KLEN) return 12;
    if (!CloseHandle(h)) return 13;

    /* 2) reabrir (mesmo caminho Unicode) e ler */
    h = CreateFileW(kPath, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 14;
    if (!ReadFile(h, buf, (DWORD)sizeof buf, &got, NULL)) return 16;
    if (got != (DWORD)KLEN) return 17;

    /* 3) comparar conteúdo byte a byte */
    for (DWORD i = 0; i < (DWORD)KLEN; i++)
        if (buf[i] != kData[i]) return 18;

    /* 4) fechar e sair */
    if (!CloseHandle(h)) return 19;
    return 42;
}
