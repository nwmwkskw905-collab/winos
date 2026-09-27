#include <windows.h>
int main(){
    char cwd[96];
    DWORD n = GetCurrentDirectoryA(96, cwd);
    if (n==0 || n>=96) return 10;
    // SetCurrentDirectoryA to same
    if (!SetCurrentDirectoryA(cwd)) return 11;
    // GetFullPathNameA
    char full[96];
    DWORD nf = GetFullPathNameA("file.txt", 96, full, NULL);
    if (nf==0) return 12;
    // GetTempPathA
    char tmp[96];
    DWORD nt = GetTempPathA(96, tmp);
    if (nt==0) return 13;
    // Create file for Move/Copy test using our VFS
    // Use CreateFileA
    HANDLE h = CreateFileA("C:\\Temp\\a.txt", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h==INVALID_HANDLE_VALUE) return 14;
    DWORD wr=0;
    WriteFile(h, "hi", 2, &wr, NULL);
    FlushFileBuffers(h);
    // SetFilePointerEx to 0
    LARGE_INTEGER li; li.QuadPart=0;
    LARGE_INTEGER newp;
    if (!SetFilePointerEx(h, li, &newp, FILE_BEGIN)) { CloseHandle(h); return 15; }
    if (newp.QuadPart!=0) { CloseHandle(h); return 16; }
    CloseHandle(h);
    // CopyFile
    if (!CopyFileA("C:\\Temp\\a.txt", "C:\\Temp\\b.txt", FALSE)) return 17;
    // MoveFile
    if (!MoveFileA("C:\\Temp\\b.txt", "C:\\Temp\\c.txt")) return 18;
    // GetDiskFreeSpaceEx
    ULARGE_INTEGER freeAvail, total, totalFree;
    if (!GetDiskFreeSpaceExA(NULL, &freeAvail, &total, &totalFree)) return 19;
    if (total.QuadPart==0) return 20;
    // cleanup
    DeleteFileA("C:\\Temp\\a.txt");
    DeleteFileA("C:\\Temp\\c.txt");
    return 42;
}
