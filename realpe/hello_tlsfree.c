/*
 * hello_tlsfree.c — PE x64 real (G72) para validar kernel32!TlsFree.
 * Fechar o ciclo completo: TlsAlloc -> TlsSetValue -> TlsGetValue -> TlsFree.
 *
 * Imports reais resolvidos pelo loader: TlsAlloc, TlsSetValue, TlsGetValue,
 * TlsFree, SetLastError, GetLastError + CRT.
 *
 * Contratos (rc):
 *  10 = TlsAlloc falhou (0xFFFFFFFF ou indice >= 60)
 *  11 = Set(VALUE_A)/Get(VALUE_A) falhou (ciclo gravar/ler)
 *  12 = TlsFree(indice alocado) != TRUE
 *  13 = lixo observavel apos TlsFree (Get != NULL)
 *  14 = reutilizacao: TlsAlloc apos free nao retornou o mesmo indice (first-fit
 *       documentado do G70 garante reuso neste cenario) ou retornou invalido
 *  15 = fantasma do valor antigo apos reuso / VALUE_B nao gravado/recuperado
 *  16 = NULL: Set(NULL)!=TRUE, Get!=NULL, ou indice "liberado" por NULL
 *  17 = TlsFree de novo (double-free) nao seguiu o contrato (FALSE + 6)
 *  18 = indices fora da capacidade (64, 0xFFFFFFFF) aceitos ou erro != 6
 *  19 = reservados (60, 63) aceitos, erro != 6, ou slots internos 60..63 alterados
 *  20 = sucesso alterou LastError (contrato da familia: preservado)
 *  21 = truncamento 32 bits detectado no valor de 64 bits
 *  72 = OK (ciclo completo)
 */
#include <windows.h>
#include <stdio.h>

#define VALUE_A 0x123456789ABCDEF0ull
#define VALUE_B 0x0FEDCBA987654321ull

int main(void)
{
    DWORD idxA, idx2;
    ULONG_PTR va = (ULONG_PTR)VALUE_A;
    ULONG_PTR vb = (ULONG_PTR)VALUE_B;
    void *save[4];
    int i;

    /* J: salvar slots internos 60..63 via API publica (antes) */
    for (i = 0; i < 4; i++) save[i] = TlsGetValue(60 + i);

    /* A: alocacao */
    idxA = TlsAlloc();
    if (idxA == 0xFFFFFFFFUL || idxA >= 60) return 10;

    /* B: gravar VALUE_A (64 bits) e recuperar.
     * Nota G55: TlsGetValue limpa LastError em sucesso — medir ANTES do Get. */
    SetLastError(0xDEAD);
    if (TlsSetValue(idxA, (LPVOID)va) != TRUE) return 11;
    if (GetLastError() != 0xDEAD) return 20;
    if ((ULONG_PTR)TlsGetValue(idxA) != va) {
        if ((ULONG_PTR)TlsGetValue(idxA) == (ULONG_PTR)(DWORD)VALUE_A) return 21;
        return 11;
    }

    /* C: liberar (sucesso preserva LastError — medido direto apos TlsFree) */
    SetLastError(0xDEAD);
    if (TlsFree(idxA) != TRUE) return 12;
    if (GetLastError() != 0xDEAD) return 20;

    /* 13: sem lixo observavel apos free */
    if (TlsGetValue(idxA) != NULL) return 13;

    /* D: reutilizacao do indice (algoritmo G70 = primeiro bit livre) */
    idx2 = TlsAlloc();
    if (idx2 == 0xFFFFFFFFUL || idx2 >= 60) return 14;
    if (idx2 != idxA) return 14;

    /* 15a: valor antigo (VALUE_A) NAO reaparece no indice reutilizado */
    if (TlsGetValue(idx2) != NULL) return 15;

    /* E: VALUE_B (64 bits) no indice reutilizado */
    if (TlsSetValue(idx2, (LPVOID)vb) != TRUE) return 15;
    if ((ULONG_PTR)TlsGetValue(idx2) != vb) return 15;

    /* F: NULL e valor permitido; NAO libera o indice */
    if (TlsSetValue(idx2, NULL) != TRUE) return 16;
    if (TlsGetValue(idx2) != NULL) return 16;
    if (TlsSetValue(idx2, (LPVOID)vb) != TRUE) return 16;

    /* G: liberar (TRUE) e double-free (FALSE + 6) */
    if (TlsFree(idx2) != TRUE) return 17;
    SetLastError(0xDEAD);
    if (TlsFree(idx2) != FALSE) return 17;
    if (GetLastError() != 6) return 17;

    /* H: indices fora da capacidade (sem acesso fora de tls_slots) */
    SetLastError(0xDEAD);
    if (TlsFree(64) != FALSE) return 18;
    if (GetLastError() != 6) return 18;
    if (TlsFree(0xFFFFFFFFUL) != FALSE) return 18;
    if (GetLastError() != 6) return 18;

    /* I: reservados 60 e 63 NUNCA liberados */
    SetLastError(0xDEAD);
    if (TlsFree(60) != FALSE) return 19;
    if (GetLastError() != 6) return 19;
    if (TlsFree(63) != FALSE) return 19;
    if (GetLastError() != 6) return 19;

    /* J: nao corrupcao — slots 60..63 identicos (via API publica) */
    for (i = 0; i < 4; i++)
        if (TlsGetValue(60 + i) != save[i]) return 19;

    /* §8 cenario 2: ciclo posterior nao herda valor antigo */
    idxA = TlsAlloc();
    if (idxA == 0xFFFFFFFFUL || idxA >= 60) return 14;
    if (TlsGetValue(idxA) != NULL) return 15;
    if (TlsFree(idxA) != TRUE) return 12;

    printf("[*] ciclo TlsAlloc->TlsSetValue->TlsGetValue->TlsFree OK\n");
    printf("[*] reuso de indice (first-fit G70): idx=%lu reutilizado\n", (unsigned long)idx2);
    printf("[*] NULL nao libera; double-free=FALSE+6; 60..63 preservados\n");
    return 72;
}
