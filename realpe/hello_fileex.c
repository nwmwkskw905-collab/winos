#include <windows.h>
#include <string.h>

static BOOL create_file_a(const char* name, const char* content) {
    HANDLE h = CreateFileA(name, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD w=0; WriteFile(h, content, (DWORD)strlen(content), &w, NULL); CloseHandle(h); return TRUE;
}

int main(void) {
    /* Test CreateDirectoryA */
    if (!CreateDirectoryA("g84_dir", NULL)) {
        DWORD le = GetLastError();
        if (le != ERROR_ALREADY_EXISTS && le != 183) return 10;
    }
    /* Create file inside */
    if (!create_file_a("g84_dir\\inner.txt", "hi")) return 11;

    /* GetFileAttributesExA */
    WIN32_FILE_ATTRIBUTE_DATA data;
    memset(&data, 0, sizeof(data));
    if (!GetFileAttributesExA("g84_dir", GetFileExInfoStandard, &data)) return 12;
    if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 13;
    memset(&data, 0, sizeof(data));
    if (!GetFileAttributesExA("g84_dir\\inner.txt", GetFileExInfoStandard, &data)) return 14;
    if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) return 15;
    if (data.nFileSizeLow != 2) return 16;

    /* GetFileAttributesExW */
    WIN32_FILE_ATTRIBUTE_DATA dataW;
    memset(&dataW, 0, sizeof(dataW));
    if (!GetFileAttributesExW(L"g84_dir", GetFileExInfoStandard, &dataW)) return 17;
    if (!(dataW.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return 18;
    memset(&dataW, 0, sizeof(dataW));
    if (!GetFileAttributesExW(L"g84_dir\\inner.txt", GetFileExInfoStandard, &dataW)) return 19;
    if (dataW.nFileSizeLow != 2) return 20;

    /* Test NULL checks */
    SetLastError(0);
    if (GetFileAttributesExA(NULL, GetFileExInfoStandard, &data)) return 21;
    if (GetLastError() != ERROR_INVALID_PARAMETER && GetLastError()!=87) return 21;
    SetLastError(0);
    if (GetFileAttributesExA("g84_dir", GetFileExInfoStandard, NULL)) return 22;
    if (GetLastError() != ERROR_INVALID_PARAMETER && GetLastError()!=87) return 22;
    SetLastError(0);
    if (GetFileAttributesExA("nonexist_xyz", GetFileExInfoStandard, &data)) return 23;
    if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError()!=2) return 23;

    /* Test traversal blocked */
    SetLastError(0);
    if (CreateDirectoryA("..\\outside", NULL)) return 24;
    SetLastError(0);
    if (GetFileAttributesExA("..\\outside", GetFileExInfoStandard, &data)) return 25;

    /* RemoveDirectory */
    /* Should fail if not empty */
    SetLastError(0);
    if (RemoveDirectoryA("g84_dir")) return 26; /* should fail because not empty */
    /* Delete inner then remove */
    if (!DeleteFileA("g84_dir\\inner.txt")) return 27;
    if (!RemoveDirectoryA("g84_dir")) return 28;

    /* Test W variants */
    if (!CreateDirectoryW(L"g84_dirw", NULL)) return 29;
    if (!create_file_a("g84_dirw\\innerw.txt", "hi")) { RemoveDirectoryA("g84_dirw"); return 30; }
    if (!RemoveDirectoryW(L"g84_dirw")) { /* should fail not empty */ }
    DeleteFileA("g84_dirw\\innerw.txt");
    if (!RemoveDirectoryW(L"g84_dirw")) return 31;

    /* Cleanup any leftovers */
    DeleteFileA("g84_dir\\inner.txt");
    RemoveDirectoryA("g84_dir");
    DeleteFileA("g84_dirw\\innerw.txt");
    RemoveDirectoryA("g84_dirw");
    RemoveDirectoryW(L"g84_dir");
    RemoveDirectoryW(L"g84_dirw");

    return 84;
}
