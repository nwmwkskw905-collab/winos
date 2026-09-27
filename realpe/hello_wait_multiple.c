/*
 * hello_wait_multiple.c — G47: WaitForMultipleObjects sobre duas threads reais.
 *
 * Duas threads guest (pthreads reais) executam tarefas simples independentes e
 * marcam sua conclusão em memória compartilhada. A main espera AMBAS com
 * WaitForMultipleObjects(2, handles, TRUE, INFINITE) e exige WAIT_OBJECT_0.
 *
 * Códigos de diagnóstico (contrato):
 *   10 = primeira CreateThread falhou
 *   11 = segunda CreateThread falhou
 *   12 = WaitForMultipleObjects != WAIT_OBJECT_0
 *   13 = worker1 não sinalizou conclusão
 *   14 = worker2 não sinalizou conclusão
 *   15 = CloseHandle(h1) retornou FALSE
 *   16 = CloseHandle(h2) retornou FALSE
 *   42 = fluxo completo
 *
 * Imports: CreateThread, WaitForMultipleObjects, CloseHandle + CRT.
 * Sem GetExitCodeThread, eventos, semáforos ou outras APIs novas.
 */
#include <windows.h>

HANDLE h1, h2;
volatile LONG worker1_result = 0;
volatile LONG worker2_result = 0;
volatile LONG worker1_done = 0;
volatile LONG worker2_done = 0;

DWORD WINAPI worker1(LPVOID param) {
    (void)param;
    volatile LONG acc = 0;
    for (int i = 1; i <= 1000; i++) acc += i;   /* tarefa simples independente */
    worker1_result = acc;
    worker1_done = 1;
    return 0x41;
}

DWORD WINAPI worker2(LPVOID param) {
    (void)param;
    volatile LONG acc = 0;
    for (int i = 1; i <= 500; i++) acc += i;    /* tarefa simples independente */
    worker2_result = acc;
    worker2_done = 1;
    return 0x42;
}

int main(void) {
    HANDLE handles[2];
    h1 = CreateThread(NULL, 0, worker1, NULL, 0, NULL);
    if (!h1) return 10;
    h2 = CreateThread(NULL, 0, worker2, NULL, 0, NULL);
    if (!h2) return 11;
    handles[0] = h1;
    handles[1] = h2;
    DWORD r = WaitForMultipleObjects(2, handles, TRUE, INFINITE);
    if (r != WAIT_OBJECT_0) return 12;
    if (worker1_done != 1) return 13;
    if (worker2_done != 1) return 14;
    if (!CloseHandle(h1)) return 15;
    if (!CloseHandle(h2)) return 16;
    return 42;
}
