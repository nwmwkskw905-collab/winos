/* DLL real MinGW-w64: exporta add3 via __declspec(dllexport).
 * Usada pela FASE 5 (LoadLibraryA + GetProcAddress + chamada MS x64 + FreeLibrary). */
#include <windows.h>

__declspec(dllexport) int add3(int a, int b) { return a + b + 3; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID reserved) {
    (void)h; (void)reason; (void)reserved;
    return TRUE;
}
