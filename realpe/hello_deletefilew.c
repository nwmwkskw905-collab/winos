#include <windows.h>

/* hello_deletefilew — G80: validação por PE x64 real de kernel32!DeleteFileW
 * Contratos:
 *   10 = fixture CreateFileA falhou
 *   11 = DeleteFileW falhou
 *   12 = GetFileAttributes após delete não retorna INVALID
 *   13 = DeleteFileW inexistente não falha
 *   14 = LastError inexistente
 *   15 = DeleteFileW NULL não falha
 *   80 = OK
 */

int main(void) {
    HANDLE h;
    DWORD wr;
    BOOL ok;

    h = CreateFileA("delw_g80.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 10;
    if (!WriteFile(h, "y", 1, &wr, NULL)) return 10;
    CloseHandle(h);

    ok = DeleteFileW(L"delw_g80.txt");
    if (!ok) return 11;
    if (GetFileAttributesA("delw_g80.txt") != INVALID_FILE_ATTRIBUTES) return 12;

    /* inexistente deve falhar */
    SetLastError(0xDEAD);
    ok = DeleteFileW(L"no_such_456.txt");
    if (ok) return 13;
    if (GetLastError() != 2) return 14; /* FILE_NOT_FOUND */

    /* NULL deve falhar */
    SetLastError(0xDEAD);
    ok = DeleteFileW(NULL);
    if (ok) return 15;
    if (GetLastError() != 87) return 15;

    return 80;
}
