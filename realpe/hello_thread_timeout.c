/*
 * hello_thread_timeout.c — G46: WaitForSingleObject com timeout finito em THREAD.
 *
 * A worker roda ~150 ms (Sleep existente); a main chama
 * WaitForSingleObject(hworker, 50) e deve receber WAIT_TIMEOUT após ~50 ms REAIS.
 * Depois espera INFINITE (join real) e fecha o handle.
 *
 * Códigos de diagnóstico (contrato):
 *   10 = CreateThread falhou
 *   11 = worker não iniciou (não sinalizou dentro de 2000 ms)
 *   12 = Wait(hworker, 50) != WAIT_TIMEOUT
 *   13 = tempo do timeout fora de [25, 250] ms
 *   14 = Wait(hworker, INFINITE) != WAIT_OBJECT_0
 *   15 = CloseHandle(hworker) retornou FALSE
 *   42 = fluxo completo
 * O tempo é calculado por QueryPerformanceCounter e validado pelo check 13
 * (registrado mesmo quando reprova — a faixa é o instrumento).
 *
 * Imports: CreateThread, WaitForSingleObject, CloseHandle, Sleep,
 * QueryPerformanceFrequency, QueryPerformanceCounter + CRT. Sem APIs novas.
 */
#include <windows.h>

HANDLE hworker;
volatile LONG worker_started = 0;

DWORD WINAPI worker(LPVOID param) {
    (void)param;
    worker_started = 1;
    Sleep(150);                     /* permanece executando por ~150 ms */
    return 0x41;
}

int main(void) {
    LARGE_INTEGER t0, t1, freq;
    hworker = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    if (!hworker) return 10;

    /* garante que a worker começou (janela generosa de 2000 ms) */
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    while (worker_started != 1) {
        QueryPerformanceCounter(&t1);
        double waited = (double)(t1.QuadPart - t0.QuadPart) * 1000.0
                        / (double)freq.QuadPart;
        if (waited > 2000.0) return 11;
    }

    QueryPerformanceCounter(&t0);
    DWORD result = WaitForSingleObject(hworker, 50);
    QueryPerformanceCounter(&t1);
    if (result != WAIT_TIMEOUT) return 12;
    {
        double ms = (double)(t1.QuadPart - t0.QuadPart) * 1000.0
                    / (double)freq.QuadPart;
        if (ms < 25.0 || ms > 250.0) return 13;
    }

    if (WaitForSingleObject(hworker, INFINITE) != WAIT_OBJECT_0) return 14;
    if (!CloseHandle(hworker)) return 15;
    return 42;
}
