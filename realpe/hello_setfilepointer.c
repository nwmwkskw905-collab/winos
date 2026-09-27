#include <windows.h>

/* hello_setfilepointer — validação por PE x64 real de kernel32!SetFilePointer
 * (handler existente desde G32; hello_file_seek = evidência principal de FILE_BEGIN
 * + ReadFile; este PE cobre o contrato completo: FILE_CURRENT/FILE_END, negativos,
 * lpDistanceToMoveHigh, handle/método inválidos, abaixo de zero e limpeza).
 * Fixture: "seek_g69.txt" com "ABCDEFGHIJ" (10 bytes). O PE só conhece caminhos guest.
 * Contratos (exit codes):
 *   10 = fixture (criação/limpeza)
 *   11 = FILE_BEGIN incorreto
 *   12 = FILE_CURRENT incorreto
 *   13 = FILE_END incorreto
 *   14 = efeito ReadFile incorreto
 *   15 = handle inválido não tratado
 *   16 = LastError de sucesso incoerente
 *   17 = método inválido não recusado
 *   18 = negative seek incorreto
 *   19 = lpDistanceToMoveHigh não tratado
 *   69 = fluxo completo */

int main(void) {
    HANDLE h;
    DWORD wr = 0, got = 0, r;
    char b;
    LONG hi;
    static const char kData[] = "ABCDEFGHIJ";   /* 10 bytes; offset 3 = 'D' */

    /* ---- fixture ---- */
    h = CreateFileA("seek_g69.txt", GENERIC_READ | GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 10;
    if (!WriteFile(h, kData, 10, &wr, NULL) || wr != 10) return 10;

    /* §8 — FILE_BEGIN */
    if (SetFilePointer(h, 0, NULL, FILE_BEGIN) != 0) return 11;
    if (SetFilePointer(h, 3, NULL, FILE_BEGIN) != 3) return 11;

    /* §9 — FILE_CURRENT (positivo e negativo = §18) */
    if (SetFilePointer(h, 2, NULL, FILE_CURRENT) != 5) return 12;
    if (SetFilePointer(h, -2, NULL, FILE_CURRENT) != 3) return 18;

    /* §10 — FILE_END (0 e relativo negativo) */
    if (SetFilePointer(h, 0, NULL, FILE_END) != 10) return 13;
    if (SetFilePointer(h, -4, NULL, FILE_END) != 6) return 13;

    /* §11 — efeito real: seek → ReadFile devolve 'D' */
    if (SetFilePointer(h, 3, NULL, FILE_BEGIN) != 3) return 11;
    b = 0;
    if (!ReadFile(h, &b, 1, &got, NULL) || got != 1) return 14;
    if (b != 'D') return 14;

    /* §13 — lpDistanceToMoveHigh não-NULL (entrada 0; posição < 4 GiB) */
    hi = 0;
    if (SetFilePointer(h, 0, &hi, FILE_BEGIN) != 0) return 19;
    if (hi != 0) return 19;

    /* §12 — handle inválido: 0xFFFFFFFF + 6 (ERROR_INVALID_HANDLE) */
    SetLastError(0xDEAD);
    if (SetFilePointer(INVALID_HANDLE_VALUE, 0, NULL, FILE_BEGIN) != 0xFFFFFFFFu)
        return 15;
    if (GetLastError() != 6) return 15;

    /* §17 — método inválido: 0xFFFFFFFF + 87 */
    SetLastError(0xDEAD);
    if (SetFilePointer(h, 0, NULL, 99) != 0xFFFFFFFFu) return 17;
    if (GetLastError() != 87) return 17;

    /* §18 — abaixo de zero: fseek falha → 0xFFFFFFFF + 5 (contrato observado) */
    if (SetFilePointer(h, 5, NULL, FILE_BEGIN) != 5) return 11;
    SetLastError(0xDEAD);
    if (SetFilePointer(h, -7, NULL, FILE_CURRENT) != 0xFFFFFFFFu) return 18;
    if (GetLastError() != 5) return 18;

    /* §16 — sucesso preserva LastError */
    SetLastError(0xDEAD);
    if (SetFilePointer(h, 0, NULL, FILE_BEGIN) != 0) return 11;
    if (GetLastError() != 0xDEAD) return 16;

    /* §21 — limpeza da fixture (mecanismo validado G68) */
    if (!CloseHandle(h)) return 10;
    if (DeleteFileA("seek_g69.txt") != TRUE) return 10;
    if (GetFileAttributesA("seek_g69.txt") != INVALID_FILE_ATTRIBUTES) return 10;

    return 69;
}
