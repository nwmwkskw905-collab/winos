/*
 * hello_mutex.c — G44: exclusão mútua real entre duas threads guest via mutex Win32.
 *
 * Duas threads guest (pthreads reais, G43) compartilham um CreateMutexA e se
 * alternam numa seção protegida. Verificação DIRETA de exclusão mútua:
 * `inside`/`violation` — se as duas entrassem simultaneamente, violation=1.
 *
 * Códigos de diagnóstico (§9):
 *   10 = CreateMutexA falhou
 *   11 = CreateThread(worker1) falhou
 *   12 = CreateThread(worker2) falhou
 *   13 = WaitForSingleObject(h1) falhou
 *   14 = WaitForSingleObject(h2) falhou
 *   15 = worker1 não entrou na seção protegida
 *   16 = worker2 não entrou na seção protegida
 *   17 = shared_value inválido (§9) OU violation != 0 (§11) — colisão do contrato
 *   18 = CloseHandle(h1) falhou
 *   19 = CloseHandle(h2) falhou
 *   20 = CloseHandle(mutex) falhou
 *   42 = fluxo completo aprovado
 * Workers retornam 0x11/0x12 se o Wait delas não devolver WAIT_OBJECT_0 (§6/§7).
 *
 * Imports: CreateMutexA, WaitForSingleObject, ReleaseMutex, CloseHandle,
 * CreateThread + CRT. Ordem worker1→worker2 e worker2→worker1 são ambas válidas.
 */
#include <windows.h>

HANDLE mutex;
volatile LONG shared_value = 0;
volatile LONG worker1_entered = 0;
volatile LONG worker2_entered = 0;
volatile LONG inside = 0;
volatile LONG violation = 0;

DWORD WINAPI worker1(LPVOID param) {
    (void)param;
    if (WaitForSingleObject(mutex, INFINITE) != WAIT_OBJECT_0) return 0x11;
    if (inside != 0)
        violation = 1;
    inside = 1;
    shared_value = 0x11111111;
    worker1_entered = 1;
    inside = 0;
    ReleaseMutex(mutex);
    return 0x41;
}

DWORD WINAPI worker2(LPVOID param) {
    (void)param;
    if (WaitForSingleObject(mutex, INFINITE) != WAIT_OBJECT_0) return 0x12;
    if (inside != 0)
        violation = 1;
    inside = 1;
    shared_value = 0x22222222;
    worker2_entered = 1;
    inside = 0;
    ReleaseMutex(mutex);
    return 0x42;
}

int main(void) {
    mutex = CreateMutexA(NULL, FALSE, NULL);
    if (!mutex) return 10;
    HANDLE h1 = CreateThread(NULL, 0, worker1, NULL, 0, NULL);
    if (!h1) return 11;
    HANDLE h2 = CreateThread(NULL, 0, worker2, NULL, 0, NULL);
    if (!h2) return 12;
    if (WaitForSingleObject(h1, INFINITE) != WAIT_OBJECT_0) return 13;
    if (WaitForSingleObject(h2, INFINITE) != WAIT_OBJECT_0) return 14;
    if (worker1_entered != 1) return 15;
    if (worker2_entered != 1) return 16;
    if (shared_value != 0x11111111 && shared_value != 0x22222222) return 17;
    if (violation != 0) return 17;
    if (!CloseHandle(h1)) return 18;
    if (!CloseHandle(h2)) return 19;
    if (!CloseHandle(mutex)) return 20;
    return 42;
}
