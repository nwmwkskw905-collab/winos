/* hello_cs.c — G41: validação real do ciclo básico de Critical Section por PE x64.
 * Fluxo (§4): CRITICAL_SECTION local → Initialize → Enter → value = 0x12345678 →
 * Leave → Enter → conferir value → Leave → Delete → 42.
 * Códigos (§6) — APIs CS são VOID; a falha/erro observável é o estado de
 * last_error (GetLastError) cercado por SetLastError(0), mais as checagens de
 * dados (12/15). Não usa threads/CreateThread/WaitForSingleObject/Sleep (§5). */
#include <windows.h>

int main(void)
{
    volatile int value = 0;
    CRITICAL_SECTION cs;

    SetLastError(0);
    InitializeCriticalSection(&cs);
    if (GetLastError() != 0)
        return 10;

    SetLastError(0);
    EnterCriticalSection(&cs);
    if (GetLastError() != 0)
        return 11;
    value = 0x12345678;                 /* dentro da seção */
    if (value != 0x12345678)
        return 12;

    SetLastError(0);
    LeaveCriticalSection(&cs);
    if (GetLastError() != 0)
        return 13;

    SetLastError(0);
    EnterCriticalSection(&cs);
    if (GetLastError() != 0)
        return 14;
    if (value != 0x12345678)            /* dados na segunda entrada */
        return 15;

    SetLastError(0);
    LeaveCriticalSection(&cs);
    if (GetLastError() != 0)
        return 16;

    if (GetLastError() != 0)            /* erro residual antes de Delete */
        return 17;
    DeleteCriticalSection(&cs);
    return 42;
}
