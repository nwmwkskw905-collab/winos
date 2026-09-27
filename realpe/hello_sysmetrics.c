#include <windows.h>

/* hello_sysmetrics — G76: validação por PE x64 real de user32!GetSystemMetrics
 * Métricas fundamentais para GUI (CXSCREEN, CYSCREEN, etc).
 * Contratos:
 *   10 = CXSCREEN incorreto (0 ou >4096)
 *   11 = CYSCREEN incorreto
 *   12 = FULLSCREEN incorreto
 *   13 = XVSCROLL/YHSCROLL incorreto
 *   14 = MOUSEPRESENT incorreto
 *   15 = ICON/CURSOR metrics
 *   16 = CAPTION/MENU metrics
 *   17 = índice inválido não retorna 0
 *   18 = LastError alterado (GetSystemMetrics não deve alterar)
 *   76 = OK
 */

int main(void) {
    int v;
    DWORD last;

    /* SM_CXSCREEN =0, SM_CYSCREEN=1 — devem ser >0 e <=4096 (320x240 nos testes) */
    v = GetSystemMetrics(0);
    if (v <= 0 || v > 4096) return 10;
    v = GetSystemMetrics(1);
    if (v <= 0 || v > 4096) return 11;

    /* FULLSCREEN =16,17 — devem ser >= CXSCREEN/CYSCREEN ou iguais */
    int cx = GetSystemMetrics(0);
    int cy = GetSystemMetrics(1);
    int fullx = GetSystemMetrics(16);
    int fully = GetSystemMetrics(17);
    if (fullx < cx || fully < cy) return 12;
    if (fullx <=0 || fully <=0) return 12;

    /* XVSCROLL=2, YHSCROLL=3, YVSCROLL=20, XHSCROLL=21 — típicos 16 */
    if (GetSystemMetrics(2) <=0) return 13;
    if (GetSystemMetrics(3) <=0) return 13;
    if (GetSystemMetrics(20) <=0) return 13;
    if (GetSystemMetrics(21) <=0) return 13;

    /* MOUSEPRESENT=19 — 1 */
    if (GetSystemMetrics(19) != 1) return 14;

    /* ICON=11,12 e CURSOR=13,14 — 32 */
    if (GetSystemMetrics(11) != 32) return 15;
    if (GetSystemMetrics(12) != 32) return 15;
    if (GetSystemMetrics(13) != 32) return 15;
    if (GetSystemMetrics(14) != 32) return 15;

    /* CAPTION=4, MENU=15 — >0 */
    if (GetSystemMetrics(4) <=0) return 16;
    if (GetSystemMetrics(15) <=0) return 16;

    /* índice inválido (999) deve retornar 0 */
    if (GetSystemMetrics(999) != 0) return 17;

    /* LastError não deve ser alterado (GetSystemMetrics não seta) */
    SetLastError(0xDEAD);
    GetSystemMetrics(0);
    last = GetLastError();
    if (last != 0xDEAD) return 18;

    return 76;
}
