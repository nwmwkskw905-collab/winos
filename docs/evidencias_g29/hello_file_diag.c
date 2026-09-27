/* hello_file.c — G29: validação real de E/S de arquivos do convidado (kernel32).
 *
 * Fluxo (determinístico, arquivo único dentro do prefixo/VFS do runtime):
 *   CreateFileA(CREATE_ALWAYS, GENERIC_WRITE) → WriteFile → GetFileSize →
 *   CloseHandle → CreateFileA(OPEN_EXISTING, GENERIC_READ) → ReadFile →
 *   comparar conteúdo → CloseHandle → return 42.
 *
 * Arquivo: winos_file_test.txt (relativo ao prefixo do convidado).
 * Imports KERNEL32: CreateFileA, WriteFile, GetFileSize, ReadFile, CloseHandle.
 * CRT MinGW (retorno de main → exit → ExitProcess) = padrão dos demais hello_*.
 *
 * Contrato de saída: 42 = fluxo completo OK; 10..19 = primeira falha (etapa).
 */
#include <windows.h>

static const char kPath[] = "winos_file_test.txt";
static const char kData[] = "winos-file-io-g29";   /* 17 bytes, determinístico */
#define KLEN 17

int main(void) {
    char buf[32];
    DWORD wr = 0, got = 0;
    HANDLE h;

    /* 1) criar e escrever */
    h = CreateFileA(kPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 10;
    if (!WriteFile(h, kData, (DWORD)KLEN, NULL, NULL)) return 11;
    if (0) return 12;

    /* 2) tamanho do arquivo */
    if (GetFileSize(h, NULL) != (DWORD)KLEN) return 15;

    /* 3) fechar */
    if (!CloseHandle(h)) return 13;

    /* 4) reabrir e ler */
    h = CreateFileA(kPath, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 14;
    if (!ReadFile(h, buf, (DWORD)sizeof buf, NULL, NULL)) return 16;
    if (0) return 17;

    /* 5) comparar conteúdo byte a byte */
    for (DWORD i = 0; i < (DWORD)KLEN; i++)
        if (buf[i] != kData[i]) return 18;

    /* 6) fechar e sair */
    if (!CloseHandle(h)) return 19;
    return 42;
}
