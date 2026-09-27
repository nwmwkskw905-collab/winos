#include <windows.h>

/* hello_getfileattributes — validação por PE real de kernel32!GetFileAttributesA.
 * Fixture criada pelo mecanismo já validado (CreateFileA/WriteFile/CloseHandle,
 * como hello_file.c) dentro do FS guest — o PE só conhece caminhos guest.
 * Atributos mínimos do contrato Portico: regular=0x80 (NORMAL), dir=0x10 (DIRECTORY),
 * erro=INVALID_FILE_ATTRIBUTES (0xFFFFFFFF) + LastError documentado.
 * Contratos:
 *   10 = arquivo existente não reconhecido (fixture/query)
 *   11 = arquivo inexistente retornou sucesso
 *   12 = atributo incorreto
 *   13 = LastError incorreto no sucesso
 *   14 = LastError incorreto no erro
 *   15 = NULL não tratado
 *   16 = caminho/separador inconsistente
 *   17 = resultado não determinístico
 *   67 = fluxo completo */

int main(void) {
    HANDLE h;
    DWORD wr, attr, a2;
    static const char kData[] = "g67ok";

    /* fixture.txt via mecanismo já validado (G30 hello_file) */
    h = CreateFileA("fixture.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 10;
    if (!WriteFile(h, kData, 5, &wr, NULL) || wr != 5) return 10;
    CloseHandle(h);

    /* §8 — arquivo existente */
    SetLastError(0xDEAD);
    attr = GetFileAttributesA("fixture.txt");
    if (attr == INVALID_FILE_ATTRIBUTES) return 10;
    if (attr != FILE_ATTRIBUTE_NORMAL) return 12;      /* 0x80: regular, sem DIRECTORY */
    if (GetLastError() != 0xDEAD) return 13;

    /* §22.17 — determinismo */
    a2 = GetFileAttributesA("fixture.txt");
    if (a2 != attr) return 17;

    /* §9 — arquivo inexistente */
    SetLastError(0xDEAD);
    if (GetFileAttributesA("definitely_missing_g67.bin") != INVALID_FILE_ATTRIBUTES)
        return 11;
    if (GetLastError() != 2) return 14;                /* ERROR_FILE_NOT_FOUND observado */

    /* §15 — NULL */
    SetLastError(0xDEAD);
    if (GetFileAttributesA(NULL) != INVALID_FILE_ATTRIBUTES) return 15;
    if (GetLastError() != 87) return 15;

    /* §10/§11 — relativo e separadores */
    SetLastError(0xDEAD);
    a2 = GetFileAttributesA("./fixture.txt");
    if (a2 != attr) return 16;
    a2 = GetFileAttributesA(".\\fixture.txt");
    if (a2 != attr) return 16;
    if (GetLastError() != 0xDEAD) return 13;

    /* §12 — diretório (raiz guest ".") */
    SetLastError(0xDEAD);
    a2 = GetFileAttributesA(".");
    if (a2 == INVALID_FILE_ATTRIBUTES || (a2 & FILE_ATTRIBUTE_DIRECTORY) == 0)
        return 12;
    if (a2 != FILE_ATTRIBUTE_DIRECTORY) return 12;     /* 0x10 exato */
    if (GetLastError() != 0xDEAD) return 13;

    return 67;
}
