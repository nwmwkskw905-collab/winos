#include <windows.h>

/* hello_lstrcpy — validação por PE real de kernel32!lstrcpyA.
 * Contrato de retorno:
 *   10 = cópia simples errada ou r != dst (retorno deve ser o ponteiro de destino)
 *   11 = dst[3] != '\0' (NUL não copiado)
 *   12 = "PorticoRuntime" (14 chars) copiado com erro
 *   13 = origem alterada pela cópia
 *   14 = bytes após o primeiro NUL copiados
 *   15 = canary do destino alterado (escrita além do terminador)
 *   16 = LastError alterado no caminho de sucesso
 *   17 = caminho dst NULL inesperado (esperado: retorno NULL + last_error 87)
 *   18 = caminho src NULL inesperado (esperado: retorno NULL + last_error 87)
 *   59 = fluxo completo
 * Os caminhos NULL são o comportamento explícito do handler do Portico
 * (pr_win32_ptr: addr==0 → NULL → !src/!dst), não contrato universal de Windows. */

int main(void) {
    char src[] = { 'a', 'b', 'c', 0 };
    char gold[] = { 'a', 'b', 'c', 0 };
    char dst[8];
    char *r;
    int i;

    /* §6.1/§6.2/§6.4 — string simples, NUL copiado, retorno == destino original */
    for (i = 0; i < 8; i++) dst[i] = 0;
    r = lstrcpyA(dst, src);
    if (r != dst) return 10;
    if (dst[0] != 'a' || dst[1] != 'b' || dst[2] != 'c') return 10;
    if (dst[3] != 0) return 11;

    /* §6.5 — origem preservada */
    for (i = 0; i < 4; i++)
        if (src[i] != gold[i]) return 13;

    /* §6.3 — string maior (conteúdo completo; comprimento conhecido = 14) */
    {
        char srcL[] = "PorticoRuntime";
        char exp[] = "PorticoRuntime";
        char dstL[20];
        for (i = 0; i < 20; i++) dstL[i] = (char)0xEE;
        r = lstrcpyA(dstL, srcL);
        if (r != dstL) return 12;
        for (i = 0; i < 15; i++)
            if (dstL[i] != exp[i]) return 12;
    }

    /* §6.6 — bytes após o primeiro NUL não copiados */
    {
        char srcN[8] = { 'a', 'b', 'c', 0, 'x', 'y', 'z', 0 };
        char dstN[16];
        for (i = 0; i < 16; i++) dstN[i] = (char)0xEE;
        r = lstrcpyA(dstN, srcN);
        if (r != dstN) return 14;
        if (dstN[0] != 'a' || dstN[1] != 'b' || dstN[2] != 'c' || dstN[3] != 0) return 14;
        for (i = 4; i < 16; i++)
            if (dstN[i] != (char)0xEE) return 14;
    }

    /* §6.7 — canary do destino intacto (destino com espaço suficiente) */
    {
        char srcC[] = "Portico";   /* 7 chars + NUL = 8 bytes escritos */
        unsigned char buf[16];
        for (i = 0; i < 16; i++) buf[i] = 0xCC;
        r = lstrcpyA((char*)buf, srcC);
        if (r != (char*)buf) return 15;
        for (i = 8; i < 16; i++)
            if (buf[i] != 0xCC) return 15;
    }

    /* §6.8 — LastError intacto no caminho de sucesso */
    SetLastError(0xDEAD);
    (void)lstrcpyA(dst, src);
    if (GetLastError() != 0xDEAD) return 16;

    /* §8 — caminhos NULL explícitos do handler (observação do Portico) */
    SetLastError(0xDEAD);
    r = lstrcpyA(NULL, src);
    if (r != NULL || GetLastError() != 87) return 17;
    SetLastError(0xDEAD);
    r = lstrcpyA(dst, NULL);
    if (r != NULL || GetLastError() != 87) return 18;

    return 59;
}
