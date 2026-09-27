/*
 * hello_thread_shared.c — G43: comunicação entre DUAS threads guest reais.
 *
 * Duas threads guest (pthreads reais, CPUs guest independentes) compartilham a
 * memória do processo e escrevem em variáveis globais VOLATILE. O objetivo é
 * comprovar que ambas executaram e se comunicaram pela memória guest compartilhada.
 *
 * Códigos de diagnóstico (exatos — sem extras):
 *   10 = primeira CreateThread falhou
 *   11 = segunda CreateThread falhou
 *   12 = Wait da thread 1 falhou
 *   13 = Wait da thread 2 falhou
 *   14 = worker1 não marcou worker1_done
 *   15 = worker2 não marcou worker2_done
 *   16 = shared_value != 0x11111111 e != 0x22222222
 *   17 = CloseHandle(h1) falhou
 *   18 = CloseHandle(h2) falhou
 *   42 = fluxo completo aprovado
 *
 * Imports: somente CreateThread, WaitForSingleObject, CloseHandle + CRT.
 * A ordem final de shared_value NÃO é determinística (aceita 0x11111111 OU
 * 0x22222222) — o check 16 também detecta escrita rasgada (mix retornaria 16).
 */
#include <windows.h>

volatile LONG worker1_done = 0;
volatile LONG worker2_done = 0;
volatile LONG shared_value = 0;

DWORD WINAPI worker1(LPVOID param) {
    (void)param;
    shared_value = 0x11111111;
    worker1_done = 1;
    return 0x41;
}

DWORD WINAPI worker2(LPVOID param) {
    (void)param;
    shared_value = 0x22222222;
    worker2_done = 1;
    return 0x42;
}

int main(void) {
    HANDLE h1 = CreateThread(NULL, 0, worker1, NULL, 0, NULL);
    if (!h1) return 10;
    HANDLE h2 = CreateThread(NULL, 0, worker2, NULL, 0, NULL);
    if (!h2) return 11;
    if (WaitForSingleObject(h1, INFINITE) != WAIT_OBJECT_0) return 12;
    if (WaitForSingleObject(h2, INFINITE) != WAIT_OBJECT_0) return 13;
    if (worker1_done != 1) return 14;
    if (worker2_done != 1) return 15;
    if (shared_value != 0x11111111 && shared_value != 0x22222222) return 16;
    if (!CloseHandle(h1)) return 17;
    if (!CloseHandle(h2)) return 18;
    return 42;
}
