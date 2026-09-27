/* Usuário de DLL real (FASE 5): LoadLibraryA + GetProcAddress + chamada
 * ABI MS x64 + FreeLibrary. Sai com 42 se add3(39,0)==42. */
#include <windows.h>

typedef int (*add3_fn)(int, int);

int main(void) {
    HINSTANCE h = LoadLibraryA("hello_dll.dll");
    if (!h) return 1;
    add3_fn f = (add3_fn)GetProcAddress(h, "add3");
    if (!f) return 2;
    int r = f(39, 0);
    if (r != 42) return 3;
    if (!FreeLibrary(h)) return 4;
    return 42;
}
