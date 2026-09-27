/*
 * hello_mutex_timeout.c — G45: timeout finito real de WaitForSingleObject em mutex.
 *
 * A worker adquire o mutex e o segura ~150 ms (Sleep existente); a main chama
 * WaitForSingleObject(mutex, 50) e deve receber WAIT_TIMEOUT após ~50 ms REAIS
 * (não imediato). Depois a main re-adquire com INFINITE e libera.
 *
 * Códigos de diagnóstico (contrato confirmado — opção A):
 *   10 = CreateMutexA falhou
 *   11 = CreateThread(worker) falhou
 *   12 = Wait(50) != WAIT_TIMEOUT
 *   13 = tempo decorrido fora da margem [25, 250] ms
 *   14 = Wait(INFINITE) != WAIT_OBJECT_0
 *   15 = ReleaseMutex falhou
 *   16 = CloseHandle(hworker) falhou
 *   17 = CloseHandle(mutex) falhou
 *   42 = fluxo completo aprovado
 * Worker retorna 0x11 se o Wait dela falhar (não conferido — sem GetExitCodeThread).
 *
 * Imports: CreateMutexA, WaitForSingleObject, ReleaseMutex, CloseHandle,
 * CreateThread, Sleep, QueryPerformanceFrequency, QueryPerformanceCounter + CRT.
 */
#include <windows.h>

HANDLE mutex;
HANDLE hworker;
volatile LONG worker_started = 0;
volatile LONG worker_released = 0;

DWORD WINAPI worker(LPVOID param) {
    (void)param;
    if (WaitForSingleObject(mutex, INFINITE) != WAIT_OBJECT_0) return 0x11;
    worker_started = 1;
    Sleep(150);                     /* segura o mutex por ~150 ms */
    ReleaseMutex(mutex);
    worker_released = 1;
    return 0x41;
}

int main(void) {
    LARGE_INTEGER t0, t1, freq;
    mutex = CreateMutexA(NULL, FALSE, NULL);
    if (!mutex) return 10;
    hworker = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (!hworker) return 11;

    while (worker_started != 1) { /* aguarda a worker segurar o mutex */ }

    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    DWORD result = WaitForSingleObject(mutex, 50);
    QueryPerformanceCounter(&t1);
    if (result != WAIT_TIMEOUT) return 12;
    {
        double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0
                    / (double)freq.QuadPart;
        if (ms < 25.0 || ms > 250.0) return 13;
    }

    while (worker_released != 1) { /* aguarda a worker liberar o mutex */ }

    if (WaitForSingleObject(mutex, INFINITE) != WAIT_OBJECT_0) return 14;
    if (!ReleaseMutex(mutex)) return 15;
    if (!CloseHandle(hworker)) return 16;
    if (!CloseHandle(mutex)) return 17;
    return 42;
}
