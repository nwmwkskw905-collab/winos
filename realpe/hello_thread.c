/* hello_thread.c — G42: validação real de CreateThread + WaitForSingleObject + CloseHandle.
 * Fluxo (contrato do briefing):
 *   result = 0
 *   h = CreateThread(NULL, 0, worker, &result, 0, NULL)   — NULL -> 10
 *   WaitForSingleObject(h, INFINITE) != WAIT_OBJECT_0     -> 11
 *   result != 0x12345678                                  -> 12
 *   CloseHandle(h) == FALSE                               -> 13
 *   return 42
 * worker: recebe &result, escreve 0x12345678, retorna 0x42 (retorno NÃO conferido).
 * Imports: CreateThread, WaitForSingleObject, CloseHandle + CRT. Sem GetExitCodeThread,
 * CreateRemoteThread, _beginthread, Sleep, TlsAlloc, TlsSetValue, mutex/evento/semaforo. */
#include <windows.h>

static volatile LONG result = 0;

static DWORD WINAPI worker(LPVOID param)
{
    *(volatile LONG *)param = 0x12345678;
    return 0x42;
}

int main(void)
{
    result = 0;

    HANDLE h = CreateThread(NULL, 0, worker, (LPVOID)&result, 0, NULL);
    if (h == NULL)
        return 10;

    DWORD w = WaitForSingleObject(h, INFINITE);
    if (w != WAIT_OBJECT_0)
        return 11;

    if (result != 0x12345678)
        return 12;

    if (CloseHandle(h) == FALSE)
        return 13;

    return 42;
}
