#include <windows.h>

/* hello_terminateprocess — validação por PE real de kernel32!TerminateProcess.
 * Contrato de retorno:
 *   10 = smoke pré-chamada falhou (ambiente não funcional)
 *   11 = a chamada RETORNOU (a terminação real não ocorreu)
 *   64 = terminação real com uExitCode=64 (exclusivo G64) — EVENTO FINAL;
 *        nenhum código após a chamada deve executar (§6).
 * hProcess = pseudo-handle (HANDLE)-1 (valor padrão de GetCurrentProcess);
 * o handler atual do Portico IGNORA este argumento (documentado no relatório). */

int main(void) {
    /* §6: asserções IMPORTANTES ANTES da chamada de terminação */
    SetLastError(0xDEAD);
    if (GetLastError() != 0xDEAD) return 10;

    /* §8/§11: RDX = uExitCode = 64 → ctx->exit_code = 64 → halted → rc = 64 */
    TerminateProcess((HANDLE)-1, 64);

    /* §6: nunca deve alcançar aqui — a terminação é o evento final */
    return 11;
}
