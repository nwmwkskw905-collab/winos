/*
 * hello_puts.c — PE x64 real (G73) para validar msvcrt!puts.
 *
 * Imports reais resolvidos pelo loader: msvcrt!puts + SetLastError/GetLastError
 * (kernel32) + printf (diagnóstico; NÃO substitui puts nos casos testados).
 *
 * Contratos (rc):
 *  10 = caso A ("hello") com retorno negativo/EOF
 *  11 = canário PREFIXO corrompido por puts
 *  12 = string de entrada MODIFICADA por puts (deve permanecer intacta)
 *  13 = canário SUFIXO corrompido por puts
 *  14 = ponteiro inválido/NULL não devolveu EOF (-1)
 *  15 = casos B–F com retorno inconsistente (negativo ou != retorno do caso A)
 *  73 = OK
 *
 * Observação estrutural: um guest não consegue reler o stdout do host; a prova
 * do newline ("hello\n" e não "hello") vem da saída observável do runtime
 * (console capture/log) verificada pelo harness — conforme o enunciado §4
 * ("A prova deve vir da saída observável/log produzido pelo runtime").
 */
#include <windows.h>
#include <stdio.h>

/* puts é declarado em <stdio.h>; chamada direta = import por nome via IAT. */

int main(void)
{
    unsigned char pre[8] = { 0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6, 0x07, 0x18 };
    unsigned char suf[8] = { 0x28, 0x39, 0x4A, 0x5B, 0x6C, 0x7D, 0x8E, 0x9F };
    /* caso A: string simples em buffer mutável (sem const-fold) */
    char ta[8]  = { 'h', 'e', 'l', 'l', 'o', 0, 0x55, 0x66 };
    /* caso B: string vazia */
    char tb[2]  = { 0, 0x77 };
    /* caso C: string grande (> 300 bytes) — nada de buffer minúsculo */
    char tc[320];
    /* caso D: espaços preservados */
    char td[32];
    /* caso E: ASCII variado byte a byte */
    char te[20];
    /* §12: char text[] = "WinOS" verificado após puts */
    char tw[8]  = { 'W', 'i', 'n', 'O', 'S', 0, 0x33, 0x44 };
    char tf1[2] = { 'A', 0 };
    char tf2[2] = { 'B', 0 };
    char tf3[2] = { 'C', 0 };
    int rA, rB, rC, rD, rE, rF1, rF2, rF3, rW, rX, rN;
    int i;

    /* caso C: 'x' * 300 + NUL */
    for (i = 0; i < 300; i++) tc[i] = 'x';
    tc[300] = 0;
    /* caso D: "WinOS Portico Runtime" */
    { const char* s = "WinOS Portico Runtime"; for (i = 0; s[i]; i++) td[i] = s[i]; td[i] = 0; }
    /* caso E: "ABC123_-+=.,!" */
    { const char* s = "ABC123_-+=.,!"; for (i = 0; s[i]; i++) te[i] = s[i]; te[i] = 0; }

    /* ---- Caso A: string simples (retorno + LastError observado) ---- */
    SetLastError(0xDEAD);
    rA = puts(ta);
    printf("[*] A ret=%d last=%08lX\n", rA, (unsigned long)GetLastError());
    if (rA < 0) return 10;

    /* ---- §12: string intacta + canários após A ---- */
    if (pre[0] != 0xA1 || pre[7] != 0x18) return 11;
    if (ta[0] != 'h' || ta[1] != 'e' || ta[2] != 'l' || ta[3] != 'l' ||
        ta[4] != 'o' || ta[5] != 0) return 12;
    if (suf[0] != 0x28 || suf[7] != 0x9F) return 13;

    /* ---- Caso B: string vazia (linha vazia) ---- */
    rB = puts(tb);
    printf("[*] B ret=%d\n", rB);
    if (rB < 0 || rB != rA) return 15;

    /* ---- Caso C: string grande ---- */
    rC = puts(tc);
    printf("[*] C ret=%d\n", rC);
    if (rC < 0 || rC != rA) return 15;

    /* ---- Caso D: espaços ---- */
    rD = puts(td);
    printf("[*] D ret=%d\n", rD);
    if (rD < 0 || rD != rA) return 15;

    /* ---- Caso E: ASCII variado ---- */
    rE = puts(te);
    printf("[*] E ret=%d\n", rE);
    if (rE < 0 || rE != rA) return 15;

    /* ---- Caso F: múltiplas chamadas na ordem A,B,C ---- */
    rF1 = puts(tf1);
    rF2 = puts(tf2);
    rF3 = puts(tf3);
    printf("[*] F ret=%d,%d,%d\n", rF1, rF2, rF3);
    if (rF1 < 0 || rF2 < 0 || rF3 < 0) return 15;
    if (rF1 != rA || rF2 != rA || rF3 != rA) return 15;

    /* ---- §12: char text[] = "WinOS" permanece "WinOS\0" após puts ---- */
    rW = puts(tw);
    if (rW < 0 || rW != rA) return 15;
    if (tw[0] != 'W' || tw[1] != 'i' || tw[2] != 'n' || tw[3] != 'O' ||
        tw[4] != 'S' || tw[5] != 0) return 12;
    if (pre[0] != 0xA1 || pre[7] != 0x18) return 11;
    if (suf[0] != 0x28 || suf[7] != 0x9F) return 13;
    if (tb[0] != 0 || tb[1] != 0x77) return 12;
    if (tc[299] != 'x' || tc[300] != 0) return 12;

    /* ---- §13: ponteiro inválido e NULL — sem crash; EOF (-1).
     * volatile esconde os ponteiros do otimizador (teste intencional). ---- */
    { volatile ULONG_PTR bad = (ULONG_PTR)0x1;
      rX = puts((const char *)bad); }
    printf("[*] X (invalid ptr) ret=%d\n", rX);
    if (rX != -1) return 14;
    { volatile ULONG_PTR nul = (ULONG_PTR)0;
      rN = puts((const char *)nul); }
    printf("[*] N (null ptr) ret=%d\n", rN);
    if (rN != -1) return 14;

    printf("[*] puts: A-F + canaries + EOF em erro OK\n");
    return 73;
}
