#include <windows.h>

/* hello_deletefile — validação por PE real de kernel32!DeleteFileA.
 * Fixture autocontida via mecanismo validado (CreateFileA/WriteFile/CloseHandle,
 * padrão G67/hello_file.c); o PE só conhece caminhos guest.
 * Contratos (exit codes):
 *   10 = fixture não estabelecida
 *   11 = DeleteFileA de existente não retornou TRUE
 *   12 = arquivo não foi removido (ainda existe)
 *   13 = inexistente retornou TRUE
 *   14 = LastError incoerente
 *   15 = NULL não tratado
 *   16 = separador inconsistente
 *   17 = diretório não recusado
 *   18 = traversal não bloqueado
 *   68 = fluxo completo */

int main(void) {
    HANDLE h;
    DWORD wr;
    static const char kData[] = "g68ok";

    /* ---- ciclo principal: delete_g68.txt ---- */
    h = CreateFileA("delete_g68.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 10;
    if (!WriteFile(h, kData, 5, &wr, NULL) || wr != 5) return 10;
    CloseHandle(h);
    if (GetFileAttributesA("delete_g68.txt") == INVALID_FILE_ATTRIBUTES) return 10;

    /* §6/§10 — sucesso: TRUE + arquivo removido + LastError observado (preservado) */
    SetLastError(0xDEAD);
    if (DeleteFileA("delete_g68.txt") != TRUE) return 11;
    if (GetLastError() != 0xDEAD) return 14;
    if (GetFileAttributesA("delete_g68.txt") != INVALID_FILE_ATTRIBUTES) return 12;

    /* §6/§8 — inexistente: FALSE + convenção atual (ERROR_FILE_NOT_FOUND = 2) */
    SetLastError(0xDEAD);
    if (DeleteFileA("definitely_missing_g68.bin") != FALSE) return 13;
    if (GetLastError() != 2) return 14;

    /* §2 — NULL: FALSE + 87 (padrão G65–G67) */
    SetLastError(0xDEAD);
    if (DeleteFileA(NULL) != FALSE) return 15;
    if (GetLastError() != 87) return 15;

    /* §6.9 — separadores: segundo ciclo com "\\" */
    h = CreateFileA("delete_g68b.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 10;
    if (!WriteFile(h, kData, 5, &wr, NULL) || wr != 5) return 10;
    CloseHandle(h);
    SetLastError(0xDEAD);
    if (DeleteFileA(".\\delete_g68b.txt") != TRUE) return 16;
    if (GetFileAttributesA("delete_g68b.txt") != INVALID_FILE_ATTRIBUTES) return 12;

    /* §8 — diretório: DeleteFileA não remove diretórios → FALSE + ACCESS_DENIED(5) */
    SetLastError(0xDEAD);
    if (DeleteFileA(".") != FALSE) return 17;
    if (GetLastError() != 5) return 14;

    /* §9 — traversal: vfs_resolve existente recusa ".." → FALSE + ACCESS_DENIED(5) */
    SetLastError(0xDEAD);
    if (DeleteFileA("..\\g68_escape.txt") != FALSE) return 18;
    if (GetLastError() != 5) return 14;

    return 68;
}
