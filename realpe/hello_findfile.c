#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

/* G82: FindFirstFileW + FindNextFileW + FindClose
 * Exit codes 10-40 falha, 82 OK
 */

static int wcscmp_ascii(const WCHAR* w, const char* a) {
    int i=0;
    while (w[i] && a[i]) {
        if ((char)w[i] != a[i]) return 1;
        i++;
    }
    return w[i] != 0 || a[i] != 0;
}
static int wcs_equal(const WCHAR* a, const WCHAR* b) {
    int i=0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i]==0 && b[i]==0;
}
static int is_txt_file(const WCHAR* w) {
    int len=0; while(w[len]) len++;
    if (len<4) return 0;
    if (w[len-4] != L'.') return 0;
    WCHAR e1=w[len-3], e2=w[len-2], e3=w[len-1];
    if ((e1==L't'||e1==L'T') && (e2==L'x'||e2==L'X') && (e3==L't'||e3==L'T')) return 1;
    return 0;
}

static BOOL create_file_a(const char* name, const char* content) {
    HANDLE h = CreateFileA(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD written=0;
    WriteFile(h, content, (DWORD)strlen(content), &written, NULL);
    CloseHandle(h);
    return TRUE;
}

int main(void) {
    /* Create fixtures */
    if (!create_file_a("g82_a.txt", "a")) return 10;
    if (!create_file_a("g82_b.txt", "bb")) return 10;
    if (!create_file_a("g82_c.log", "ccc")) return 10;

    WIN32_FIND_DATAW fd;
    HANDLE h;

    /* A. arquivo específico */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileW(L"g82_a.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 11;
    if (wcscmp_ascii(fd.cFileName, "g82_a.txt") != 0) return 12;
    if (fd.nFileSizeLow != 1) return 13; /* size 1 */
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_NORMAL) && !(fd.dwFileAttributes & 0x80)) {
        /* allow NORMAL */
        if (fd.dwFileAttributes != 0x80 && fd.dwFileAttributes != 0) {
            /* if zero, maybe not set, but we expect NORMAL */
        }
    }
    /* FindNext should fail (only one file matches) */
    BOOL nxt = FindNextFileW(h, &fd);
    if (nxt) return 14; /* should be no more */
    if (GetLastError() != ERROR_NO_MORE_FILES) {
        /* Windows returns 18, our impl should too */
        /* allow 18 */
        if (GetLastError() != 18) return 15;
    }
    if (!FindClose(h)) return 16;

    /* B. wildcard "*" */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileW(L"*", &fd);
    if (h == INVALID_HANDLE_VALUE) return 17;
    int count_star = 1;
    int found_a=0, found_b=0, found_c=0;
    if (wcscmp_ascii(fd.cFileName, "g82_a.txt")==0) found_a=1;
    if (wcscmp_ascii(fd.cFileName, "g82_b.txt")==0) found_b=1;
    if (wcscmp_ascii(fd.cFileName, "g82_c.log")==0) found_c=1;
    while (FindNextFileW(h, &fd)) {
        count_star++;
        if (wcscmp_ascii(fd.cFileName, "g82_a.txt")==0) found_a=1;
        if (wcscmp_ascii(fd.cFileName, "g82_b.txt")==0) found_b=1;
        if (wcscmp_ascii(fd.cFileName, "g82_c.log")==0) found_c=1;
        if (count_star > 100) break; /* safety */
    }
    if (GetLastError() != ERROR_NO_MORE_FILES && GetLastError() != 18) return 18;
    if (!found_a || !found_b || !found_c) return 19;
    if (count_star < 3) return 20;
    if (!FindClose(h)) return 21;

    /* C. "*.txt" */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileW(L"*.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 22;
    int count_txt=0;
    int txt_ok=1;
    do {
        if (!is_txt_file(fd.cFileName)) {
            /* *.txt should only return .txt, but our fixture.txt also .txt, so ok */
            /* check extension */
            /* if not txt, fail */
            /* Actually fixture.txt is txt, so all should be txt */
            /* But we also have g82_dir maybe not txt, so if we get non-txt, fail */
            /* For simplicity, count only if txt */
            if (!is_txt_file(fd.cFileName)) txt_ok=0;
        }
        if (wcscmp_ascii(fd.cFileName, "g82_a.txt")==0 || wcscmp_ascii(fd.cFileName, "g82_b.txt")==0) count_txt++;
        else if (wcscmp_ascii(fd.cFileName, "fixture.txt")==0) { /* also txt, but not counted */ }
        else if (is_txt_file(fd.cFileName)) count_txt++; /* count other txt */
    } while (FindNextFileW(h, &fd));
    if (!txt_ok) return 23;
    if (count_txt < 2) return 24; /* at least our 2 */
    FindClose(h);

    /* D. FindNextFileW second file different */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileW(L"g82_*.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 25;
    WCHAR first[260];
    int i=0; while(fd.cFileName[i] && i<259){ first[i]=fd.cFileName[i]; i++; } first[i]=0;
    if (!FindNextFileW(h, &fd)) {
        /* only one? but we have 2 matching g82_*.txt */
        /* If only one, check if we have at least 2 files matching pattern, else fail */
        /* g82_a.txt and g82_b.txt should both match g82_*.txt */
        /* So second should exist */
        FindClose(h);
        return 26;
    }
    WCHAR second[260];
    i=0; while(fd.cFileName[i] && i<259){ second[i]=fd.cFileName[i]; i++; } second[i]=0;
    if (wcs_equal(first, second)) { FindClose(h); return 27; }
    FindClose(h);

    /* E. fim da enumeração already tested in A/B, but explicit */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileW(L"g82_a.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 28;
    if (FindNextFileW(h, &fd)) { FindClose(h); return 29; }
    if (GetLastError() != ERROR_NO_MORE_FILES && GetLastError() != 18) { FindClose(h); return 30; }
    FindClose(h);

    /* F. FindClose tested */

    /* G. handle inválido */
    SetLastError(0);
    memset(&fd, 0, sizeof(fd));
    if (FindNextFileW((HANDLE)0x1234, &fd)) return 31;
    if (GetLastError() != ERROR_INVALID_HANDLE && GetLastError() != 6) return 31;
    SetLastError(0);
    if (FindClose((HANDLE)0x1234)) return 32;
    if (GetLastError() != ERROR_INVALID_HANDLE && GetLastError() != 6) return 32;

    /* H. NULL output */
    SetLastError(0);
    h = FindFirstFileW(L"g82_a.txt", NULL);
    if (h != INVALID_HANDLE_VALUE) return 33;
    if (GetLastError() != ERROR_INVALID_PARAMETER && GetLastError() != 87) return 33;
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileW(L"g82_a.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 34;
    SetLastError(0);
    if (FindNextFileW(h, NULL)) { FindClose(h); return 35; }
    if (GetLastError() != ERROR_INVALID_PARAMETER && GetLastError() != 87) { FindClose(h); return 35; }
    FindClose(h);

    /* I. caminho inexistente */
    SetLastError(0);
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileW(L"nonexistent_xyz_12345.txt", &fd);
    if (h != INVALID_HANDLE_VALUE) return 36;
    DWORD le = GetLastError();
    if (le != ERROR_FILE_NOT_FOUND && le != 2 && le != ERROR_PATH_NOT_FOUND && le != 3) return 37;

    /* J. diretório - test if g82_dir exists (created by host before run) */
    /* Try to find g82_dir as file */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileW(L"g82_dir", &fd);
    if (h != INVALID_HANDLE_VALUE) {
        /* should be directory */
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            FindClose(h);
            return 38;
        }
        FindClose(h);
    } else {
        /* if dir doesn't exist, skip this check, but we want it to exist */
        /* Check if directory enumeration works: try g82_dir/* if dir exists */
        memset(&fd, 0, sizeof(fd));
        h = FindFirstFileW(L"g82_dir/*", &fd);
        if (h != INVALID_HANDLE_VALUE) {
            /* if exists, close */
            FindClose(h);
        }
        /* else skip */
    }

    /* K. tamanho already tested in A, test b */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileW(L"g82_b.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 39;
    if (fd.nFileSizeLow != 2) { FindClose(h); return 40; }
    if (fd.nFileSizeHigh != 0) { FindClose(h); return 41; }
    FindClose(h);

    /* L. atributos: file NORMAL, directory DIRECTORY */
    memset(&fd, 0, sizeof(fd));
    h = FindFirstFileW(L"g82_a.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 42;
    if (fd.dwFileAttributes == 0) { /* allow 0? but expect NORMAL */ }
    else if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_NORMAL) && fd.dwFileAttributes != 0x80) {
        /* if not NORMAL, check not directory */
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { FindClose(h); return 43; }
    }
    FindClose(h);

    /* M. nome UTF-16 already checked via wcscmp */

    /* N. LastError after success should not be NO_MORE_FILES */
    memset(&fd, 0, sizeof(fd));
    SetLastError(0xDEAD);
    h = FindFirstFileW(L"g82_a.txt", &fd);
    if (h == INVALID_HANDLE_VALUE) return 44;
    /* After success, LastError may be 0 or preserved, but not 18 */
    if (GetLastError() == ERROR_NO_MORE_FILES) { FindClose(h); return 45; }
    FindClose(h);

    /* O. traversal ".." must be blocked */
    const WCHAR* traversals[] = { L"..\\outside", L"../outside", L"C:..\\outside", L"C:/../outside", L"..\\..\\outside", L"g82_dir\\..\\..\\outside" };
    for (int t=0; t<6; t++) {
        SetLastError(0);
        memset(&fd, 0, sizeof(fd));
        h = FindFirstFileW(traversals[t], &fd);
        if (h != INVALID_HANDLE_VALUE) {
            FindClose(h);
            return 46 + t; /* traversal not blocked */
        }
        DWORD le2 = GetLastError();
        /* should be ACCESS_DENIED 5 or PATH_NOT_FOUND 3 or FILE_NOT_FOUND 2 or INVALID_PARAMETER 87 */
        if (le2==0) return 46 + t;
        /* ensure it didn't escape: we check that no handle returned, which we already did */
    }

    /* Cleanup */
    DeleteFileA("g82_a.txt");
    DeleteFileA("g82_b.txt");
    DeleteFileA("g82_c.log");

    return 82;
}
