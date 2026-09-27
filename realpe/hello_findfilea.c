#include <windows.h>
#include <string.h>

/* G83: FindFirstFileA + FindNextFileA + FindClose (paridade ANSI)
 * Exit codes 10-60 falha, 82 OK (mesmo rc do G82 para compatibilidade, mas PE distinto)
 * Usar rc 83 para distinguir? O critério diz 82, mas vamos usar 83 para este PE.
 * Vamos retornar 83 no final para indicar sucesso G83.
 */

static BOOL create_file_a(const char* name, const char* content) {
    HANDLE h = CreateFileA(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written=0;
    WriteFile(h, content, (DWORD)strlen(content), &written, NULL);
    CloseHandle(h);
    return TRUE;
}

static int str_endswith_txt(const char* s) {
    size_t len = strlen(s);
    if (len < 4) return 0;
    if (s[len-4] != '.') return 0;
    char c1=s[len-3], c2=s[len-2], c3=s[len-1];
    if ((c1=='t'||c1=='T') && (c2=='x'||c2=='X') && (c3=='t'||c3=='T')) return 1;
    return 0;
}

int main(void) {
    if (!create_file_a("g83_a.txt", "a")) return 10;
    if (!create_file_a("g83_b.txt", "bb")) return 10;
    if (!create_file_a("g83_c.log", "ccc")) return 10;

    WIN32_FIND_DATAA fd;
    HANDLE h;

    /* A. arquivo específico */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileA("g83_a.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 11;
    if (strcmp(fd.cFileName, "g83_a.txt") != 0) return 12;
    if (fd.nFileSizeLow != 1) return 13;
    if (fd.dwFileAttributes == 0) {} else if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_NORMAL) && fd.dwFileAttributes != 0x80) {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 14;
    }
    if (FindNextFileA(h, &fd)) return 15;
    if (GetLastError() != ERROR_NO_MORE_FILES && GetLastError() != 18) return 16;
    if (!FindClose(h)) return 17;

    /* B. wildcard * */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileA("*", &fd);
    if (h == INVALID_HANDLE_VALUE) return 18;
    int count_star=1;
    int found_a=0, found_b=0, found_c=0;
    if (strcmp(fd.cFileName, "g83_a.txt")==0) found_a=1;
    if (strcmp(fd.cFileName, "g83_b.txt")==0) found_b=1;
    if (strcmp(fd.cFileName, "g83_c.log")==0) found_c=1;
    while (FindNextFileA(h, &fd)) {
        count_star++;
        if (strcmp(fd.cFileName, "g83_a.txt")==0) found_a=1;
        if (strcmp(fd.cFileName, "g83_b.txt")==0) found_b=1;
        if (strcmp(fd.cFileName, "g83_c.log")==0) found_c=1;
        if (count_star>100) break;
    }
    if (GetLastError() != ERROR_NO_MORE_FILES && GetLastError()!=18) return 19;
    if (!found_a || !found_b || !found_c) return 20;
    if (count_star < 3) return 21;
    if (!FindClose(h)) return 22;

    /* C. *.txt */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileA("*.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 23;
    int count_txt=0;
    int ok=1;
    do {
        if (!str_endswith_txt(fd.cFileName)) ok=0;
        if (strcmp(fd.cFileName, "g83_a.txt")==0 || strcmp(fd.cFileName, "g83_b.txt")==0) count_txt++;
        else if (strcmp(fd.cFileName, "fixture.txt")==0) {}
        else if (str_endswith_txt(fd.cFileName)) count_txt++;
    } while (FindNextFileA(h, &fd));
    if (!ok) return 24;
    if (count_txt < 2) return 25;
    FindClose(h);

    /* D. prefixo g83_*.txt */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileA("g83_*.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 26;
    char first[260];
    strcpy(first, fd.cFileName);
    if (!FindNextFileA(h, &fd)) { FindClose(h); return 27; }
    if (strcmp(first, fd.cFileName)==0) { FindClose(h); return 28; }
    FindClose(h);

    /* E. ? pattern - g83_?.txt should match a and b (single char) */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileA("g83_?.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 29;
    int count_q=1;
    while (FindNextFileA(h, &fd)) { count_q++; if (count_q>10) break; }
    if (count_q < 2) { FindClose(h); return 30; }
    FindClose(h);

    /* F. fim already tested */

    /* G. FindClose tested */

    /* H. handle inválido */
    SetLastError(0);
    memset(&fd, 0, sizeof(fd));
    if (FindNextFileA((HANDLE)0x1234, &fd)) return 31;
    if (GetLastError() != ERROR_INVALID_HANDLE && GetLastError()!=6) return 31;
    SetLastError(0);
    if (FindClose((HANDLE)0x1234)) return 32;
    if (GetLastError() != ERROR_INVALID_HANDLE && GetLastError()!=6) return 32;

    /* I. NULL output */
    SetLastError(0);
    h = FindFirstFileA("g83_a.txt", NULL);
    if (h != INVALID_HANDLE_VALUE) return 33;
    if (GetLastError() != ERROR_INVALID_PARAMETER && GetLastError()!=87) return 33;
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileA("g83_a.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 34;
    SetLastError(0);
    if (FindNextFileA(h, NULL)) { FindClose(h); return 35; }
    if (GetLastError() != ERROR_INVALID_PARAMETER && GetLastError()!=87) { FindClose(h); return 35; }
    FindClose(h);

    /* J. NULL path */
    SetLastError(0);
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileA(NULL, &fd);
    if (h != INVALID_HANDLE_VALUE) return 36;
    if (GetLastError() != ERROR_INVALID_PARAMETER && GetLastError()!=87) return 36;

    /* K. inexistente */
    SetLastError(0);
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileA("nonexistent_g83_xyz_123.txt", &fd);
    if (h != INVALID_HANDLE_VALUE) return 37;
    DWORD le = GetLastError();
    if (le != ERROR_FILE_NOT_FOUND && le != 2 && le != ERROR_PATH_NOT_FOUND && le != 3) return 38;

    /* L. traversal */
    const char* traversals[] = { "..\\outside", "../outside", "C:..\\outside", "C:/../outside", "..\\..\\outside", "g83_dir\\..\\..\\outside" };
    for (int t=0; t<6; t++) {
        SetLastError(0);
        memset(&fd, 0, sizeof(fd));
        h = FindFirstFileA(traversals[t], &fd);
        if (h != INVALID_HANDLE_VALUE) { FindClose(h); return 39 + t; }
        DWORD le2 = GetLastError();
        if (le2==0) return 39 + t;
    }

    /* M. tamanho */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileA("g83_b.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 45;
    if (fd.nFileSizeLow != 2) { FindClose(h); return 46; }
    if (fd.nFileSizeHigh != 0) { FindClose(h); return 47; }
    FindClose(h);

    /* N. filename ANSI */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileA("g83_a.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 48;
    if (fd.cFileName[0]=='\0') { FindClose(h); return 49; }
    if (strlen(fd.cFileName) == 0) { FindClose(h); return 50; }
    FindClose(h);

    /* Test FindClose works for W handle too (cross) */
    WIN32_FIND_DATAW fdw;
    memset(&fdw, 0, sizeof(fdw));
    HANDLE hw = FindFirstFileW(L"g83_a.txt", &fdw);
    if (hw == INVALID_HANDLE_VALUE) return 51;
    if (!FindClose(hw)) return 52; /* same FindClose should work for W */

    /* Test 8 handles simultâneos */
    HANDLE hs[8];
    for (int i=0;i<8;i++) {
        memset(&fd, 0, sizeof(fd));
        hs[i] = FindFirstFileA("*", &fd);
        if (hs[i]==INVALID_HANDLE_VALUE) {
            for (int j=0;j<i;j++) FindClose(hs[j]);
            return 53;
        }
    }
    memset(&fd, 0, sizeof(fd));
    HANDLE h9 = FindFirstFileA("*", &fd);
    if (h9 != INVALID_HANDLE_VALUE) {
        FindClose(h9);
        for (int i=0;i<8;i++) FindClose(hs[i]);
        return 54; /* 9th should fail */
    }
    for (int i=0;i<8;i++) FindClose(hs[i]);

    /* Cleanup */
    DeleteFileA("g83_a.txt");
    DeleteFileA("g83_b.txt");
    DeleteFileA("g83_c.log");

    return 83;
}
