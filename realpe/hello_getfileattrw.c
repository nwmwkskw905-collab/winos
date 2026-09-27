#include <windows.h>

/* hello_getfileattrw — G79: validação por PE x64 real de kernel32!GetFileAttributesW
 * Contratos:
 *   10 = fixture CreateFileA falhou
 *   11 = GetFileAttributesA falhou
 *   12 = GetFileAttributesW falhou
 *   13 = atributos diferentes A vs W
 *   14 = arquivo inexistente não retorna INVALID_FILE_ATTRIBUTES
 *   15 = LastError não setado para inexistente
 *   16 = limpeza DeleteFile falhou
 *   79 = OK
 */

int main(void) {
    HANDLE h;
    DWORD wr;
    DWORD attrA, attrW;

    h = CreateFileA("attr_g79.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 10;
    if (!WriteFile(h, "x", 1, &wr, NULL)) return 10;
    CloseHandle(h);

    attrA = GetFileAttributesA("attr_g79.txt");
    if (attrA == INVALID_FILE_ATTRIBUTES) return 11;

    attrW = GetFileAttributesW(L"attr_g79.txt");
    if (attrW == INVALID_FILE_ATTRIBUTES) return 12;
    if (attrA != attrW) return 13;

    /* inexistente */
    SetLastError(0xDEAD);
    attrW = GetFileAttributesW(L"no_such_file_123.txt");
    if (attrW != INVALID_FILE_ATTRIBUTES) return 14;
    if (GetLastError() != 2 && GetLastError() != 3) return 15; /* FILE_NOT_FOUND ou PATH_NOT_FOUND */

    if (!DeleteFileA("attr_g79.txt")) return 16;
    if (GetFileAttributesA("attr_g79.txt") != INVALID_FILE_ATTRIBUTES) return 16;

    return 79;
}
