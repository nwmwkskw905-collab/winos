#include <windows.h>

/* hello_getstartupinfo — validação por PE real de kernel32!GetStartupInfoA.
 * Layout STARTUPINFOA verificado no toolchain MinGW-w64 x64 (sizeof=104).
 * Contratos:
 *   10 = LastError alterado pela chamada de sucesso
 *   11 = cb inválido (nem sentinel nem sizeof)
 *   12 = campo numérico/reserved inesperado
 *   13 = ponteiro guest inválido (caminho de erro não observado)
 *   14 = canary/marker corrompido
 *   15 = ponteiro de string não-NULL (conteúdo de string inválido)
 *   16 = standard handle inválido
 *   17 = segunda chamada inconsistente
 *   66 = fluxo completo */

#define SENTINEL_CB 0x12345678u

struct block {
    unsigned char pre[8];
    STARTUPINFOA si;
    unsigned char can[8];
};

static void fill_pre(struct block* b) {
    int i;
    for (i = 0; i < 8; i++) b->pre[i] = 0x5A;
    for (i = 0; i < 8; i++) b->can[i] = 0xC3;
}

static int pre_ok(struct block* b) {
    int i;
    for (i = 0; i < 8; i++)
        if (b->pre[i] != 0x5A) return 0;
    for (i = 0; i < 8; i++)
        if (b->can[i] != 0xC3) return 0;
    return 1;
}

static int same_si(const STARTUPINFOA* x, const STARTUPINFOA* y) {
    const unsigned char* p = (const unsigned char*)x;
    const unsigned char* q = (const unsigned char*)y;
    unsigned int i;
    for (i = 0; i < sizeof(STARTUPINFOA); i++)
        if (p[i] != q[i]) return 0;
    return 1;
}

int main(void) {
    static struct block a, b;
    int i;

    fill_pre(&a);
    fill_pre(&b);
    for (i = 0; i < (int)sizeof(STARTUPINFOA); i++)
        ((unsigned char*)&a.si)[i] = 0xA5;
    a.si.cb = SENTINEL_CB;

    /* chamada principal (§6) */
    SetLastError(0xDEAD);
    GetStartupInfoA(&a.si);
    if (GetLastError() != 0xDEAD) return 10;

    /* §8 — cb: preserva (sentinel) ou sobrescreve (sizeof); senão inválido */
    if (a.si.cb != SENTINEL_CB && a.si.cb != (DWORD)sizeof(STARTUPINFOA))
        return 11;

    /* §10 — strings: NULL registradas explicitamente no contrato */
    if (a.si.lpReserved != 0 || a.si.lpDesktop != 0 || a.si.lpTitle != 0)
        return 15;

    /* §9 — campos numéricos */
    if (a.si.dwX || a.si.dwY || a.si.dwXSize || a.si.dwYSize ||
        a.si.dwXCountChars || a.si.dwYCountChars || a.si.dwFillAttribute)
        return 12;
    if (a.si.dwFlags != 0x100) return 12;   /* STARTF_USESTDHANDLES */
    if (a.si.wShowWindow != 0) return 12;
    if (a.si.cbReserved2 != 0) return 12;   /* §13 */
    if (a.si.lpReserved2 != 0) return 12;

    /* §11 — standard handles: mesma representação de GetStdHandle */
    if (a.si.hStdInput != GetStdHandle(STD_INPUT_HANDLE)) return 16;
    if (a.si.hStdOutput != GetStdHandle(STD_OUTPUT_HANDLE)) return 16;
    if (a.si.hStdError != GetStdHandle(STD_ERROR_HANDLE)) return 16;

    /* §7/§18 — canary/markers */
    if (!pre_ok(&a)) return 14;

    /* §17 — ponteiro guest inválido (caminho de erro do handler) */
    SetLastError(0xDEAD);
    GetStartupInfoA((STARTUPINFOA*)(uintptr_t)1);
    if (GetLastError() != 87) return 13;
    if (!pre_ok(&a)) return 14;

    /* §21 — segunda chamada: determinismo byte a byte */
    for (i = 0; i < (int)sizeof(STARTUPINFOA); i++)
        ((unsigned char*)&b.si)[i] = 0xA5;
    b.si.cb = SENTINEL_CB;
    GetStartupInfoA(&b.si);
    if (!same_si(&a.si, &b.si)) return 17;
    if (!pre_ok(&b)) return 14;

    return 66;
}
