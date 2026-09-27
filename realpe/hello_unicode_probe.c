/*
 * hello_unicode_probe.c — PE x64 real (G74) de diagnóstico da família Unicode.
 * Importa por nome: kernel32!MultiByteToWideChar (+ printf/SetLastError/GetLastError).
 * WideCharToMultiByte NÃO é necessária aqui (round-trip já coberto por hello_mbwc);
 * §13 do grupo: só implementar/usar se o PE demonstrar necessidade direta.
 *
 * Contratos (rc) — registram o comportamento OBSERVADO do runtime (contrato
 * Portico), com as divergências vs Windows documentadas no relatório:
 *  10 = caso A ("WinOS", -1): retorno != 6 unidades (5 + NUL)
 *  11 = caso A: conteúdo/terminador UTF-16 errado
 *  12 = caso B ("Olá", 2-byte UTF-8): retorno/conteúdo errado (4 unidades)
 *  13 = caso C ("€", 3-byte UTF-8): retorno/conteúdo errado (U+20AC, 2 unidades)
 *  14 = caso D (U+1F600 não-BMP): contagem em modo consulta inesperada
 *       (contrato Portico observado = 1 unidade; Windows = 2 (par surrogate) —
 *       LIMITAÇÃO; conversão recusa com EXECUTION STOPPED e por isso NÃO é
 *       chamada aqui — §5-D: registrar limitação, não expandir escopo)
 *  15 = buffer (suficiente/exato/insuficiente/NULL/0/vazio): comportamento
 *       observado divergente do previsto no contrato Portico
 *  16 = cbMultiByte positivo x -1 inconsistente
 *  17 = CP_ACP(0) inesperado (ACP observada = CP1252)
 *  18 = flags != 0 alterou o resultado (observado: flags aceitas e ignoradas)
 *  19 = canário corrompido / entrada modificada
 *  74 = OK
 */
#include <windows.h>
#include <stdio.h>

/* CP_UTF8 = 65001; CP_ACP = 0 (definidos em windows.h) */

int main(void)
{
    /* entradas mutáveis (bytes explícitos, independentes do fonte) */
    char a_in[8]  = { 'W','i','n','O','S', 0, 0x11, 0x22 };            /* "WinOS" */
    char b_in[8]  = { 'O','l', (char)0xC3, (char)0xA1, 0, 0x33, 0x44, 0x55 }; /* "Olá" */
    char c_in[8]  = { (char)0xE2, (char)0x82, (char)0xAC, 0, 0x66, 0x77, 0x08, 0x09 }; /* "€" */
    char d_in[8]  = { (char)0xF0, (char)0x9F, (char)0x98, (char)0x80, 0, 0, 0, 0 };    /* U+1F600 */
    char e_in[4]  = { 0, 0x44, 0x55, 0x66 };                            /* "" */
    char acp_in[4]= { (char)0xF1, 0, 0x77, 0x88 };                      /* 0xF1 = ñ em CP1252 */
    WCHAR pre[4]  = { 0xA1A1, 0xB2B2, 0xC3C3, 0xD4D4 };
    WCHAR suf[4]  = { 0xE5E5, 0xF6F6, 0x0707, 0x1818 };
    WCHAR wbuf[40];
    WCHAR small[4];
    int r, q, i;

    for (i = 0; i < 40; i++) wbuf[i] = 0xEEEE;

    /* ---- §5-A: ASCII "WinOS" com cbMultiByte=-1 ---- */
    SetLastError(0xDEAD);
    r = MultiByteToWideChar(CP_UTF8, 0, a_in, -1, wbuf, 32);
    printf("[*] A ret=%d last=%08lX w=%04X %04X %04X %04X %04X %04X\n",
           r, (unsigned long)GetLastError(),
           wbuf[0], wbuf[1], wbuf[2], wbuf[3], wbuf[4], wbuf[5]);
    if (r != 6) return 10;
    if (wbuf[0]!='W'||wbuf[1]!='i'||wbuf[2]!='n'||wbuf[3]!='O'||wbuf[4]!='S'||wbuf[5]!=0)
        return 11;

    /* ---- §5-B: UTF-8 2 bytes "Olá" (C3 A1 = U+00E1) ---- */
    r = MultiByteToWideChar(CP_UTF8, 0, b_in, -1, wbuf, 32);
    printf("[*] B ret=%d w=%04X %04X %04X %04X\n", r, wbuf[0], wbuf[1], wbuf[2], wbuf[3]);
    if (r != 4) return 12;
    if (wbuf[0]!='O'||wbuf[1]!='l'||wbuf[2]!=0x00E1||wbuf[3]!=0) return 12;

    /* ---- §5-C: UTF-8 3 bytes "€" (E2 82 AC = U+20AC) ---- */
    r = MultiByteToWideChar(CP_UTF8, 0, c_in, -1, wbuf, 32);
    printf("[*] C ret=%d w=%04X %04X\n", r, wbuf[0], wbuf[1]);
    if (r != 2) return 13;
    if (wbuf[0] != 0x20AC || wbuf[1] != 0) return 13;

    /* ---- §5-D: não-BMP U+1F600 — SOMENTE modo consulta (a conversão
     * recusa com EXECUTION STOPPED = limitação registrada; não expandir) ---- */
    q = MultiByteToWideChar(CP_UTF8, 0, d_in, 4, NULL, 0);
    printf("[*] D (query F0 9F 98 80) ret=%d (Portico=1; Windows=2 par surrogate)\n", q);
    if (q != 1) return 14;

    /* ---- §6: tamanhos de buffer (src "WinOS", -1 => need 6) ---- */
    r = MultiByteToWideChar(CP_UTF8, 0, a_in, -1, wbuf, 32);   /* suficiente */
    if (r != 6) return 15;
    r = MultiByteToWideChar(CP_UTF8, 0, a_in, -1, wbuf, 6);    /* exato */
    printf("[*] buf exato ret=%d\n", r);
    if (r != 6) return 15;
    small[0]=small[1]=small[2]=small[3]=0xDDDD;
    r = MultiByteToWideChar(CP_UTF8, 0, a_in, -1, small, 3);   /* insuficiente */
    printf("[*] buf insuf(3) ret=%d s=%04X %04X %04X %04X (Portico=trunca e devolve 3; Windows=0+ERROR_INSUFFICIENT_BUFFER)\n",
           r, small[0], small[1], small[2], small[3]);
    if (r != 3) return 15;
    if (small[0]!='W'||small[1]!='i'||small[2]!='n') return 15;
    if (small[3] != 0xDDDD) return 15;  /* nada além da região esperada */
    q = MultiByteToWideChar(CP_UTF8, 0, a_in, -1, NULL, 0);    /* consulta */
    printf("[*] consulta (NULL,0) ret=%d\n", q);
    if (q != 6) return 15;
    q = MultiByteToWideChar(CP_UTF8, 0, a_in, -1, wbuf, 0);    /* cchWideChar=0 */
    if (q != 6) return 15;
    r = MultiByteToWideChar(CP_UTF8, 0, e_in, -1, wbuf, 32);   /* vazia "" */
    printf("[*] vazia ret=%d w0=%04X\n", r, wbuf[0]);
    if (r != 1 || wbuf[0] != 0) return 15;

    /* ---- §7: -1 x tamanho positivo (5 bytes sem NUL) ---- */
    for (i = 0; i < 40; i++) wbuf[i] = 0xEEEE;
    r = MultiByteToWideChar(CP_UTF8, 0, a_in, 5, wbuf, 32);
    printf("[*] positivo(5) ret=%d w5=%04X (0xEEEE = não escrito)\n", r, wbuf[5]);
    if (r != 5) return 16;
    if (wbuf[0]!='W'||wbuf[4]!='S') return 16;
    if (wbuf[5] != 0xEEEE) return 16;   /* sem NUL quando cbMultiByte positivo */

    /* ---- §8: CP_ACP(0) = CP1252 observada ---- */
    r = MultiByteToWideChar(CP_ACP, 0, a_in, -1, wbuf, 32);
    if (r != 6 || wbuf[0]!='W') return 17;
    r = MultiByteToWideChar(CP_ACP, 0, acp_in, -1, wbuf, 32);
    printf("[*] ACP ret=%d w=%04X %04X (F1 -> 00F1)\n", r, wbuf[0], wbuf[1]);
    if (r != 2 || wbuf[0] != 0x00F1 || wbuf[1] != 0) return 17;

    /* ---- §9: flags != 0 (observado: aceitas e ignoradas) ---- */
    r = MultiByteToWideChar(CP_UTF8, 0x8, b_in, -1, wbuf, 32);
    printf("[*] flags=8 ret=%d w=%04X %04X %04X\n", r, wbuf[0], wbuf[1], wbuf[2]);
    if (r != 4 || wbuf[2] != 0x00E1) return 18;

    /* ---- §14: canários + entradas intactas ---- */
    if (pre[0]!=0xA1A1||pre[3]!=0xD4D4||suf[0]!=0xE5E5||suf[3]!=0x1818) return 19;
    if (a_in[0]!='W'||a_in[4]!='S'||a_in[5]!=0||a_in[6]!=0x11) return 19;
    if (b_in[2]!=(char)0xC3||b_in[3]!=(char)0xA1||b_in[4]!=0) return 19;
    if (c_in[0]!=(char)0xE2||c_in[2]!=(char)0xAC) return 19;
    if (small[3] != 0xDDDD) return 19;

    printf("[*] unicode probe: A-D + buffers + ACP + flags OK\n");
    return 74;
}
