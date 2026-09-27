#include <windows.h>

/* hello_readfileex — G75: validação por PE x64 real de kernel32!ReadFileEx
 * (E/S assíncrona mínima com OVERLAPPED + completion routine).
 * Fixture: "readex_g75.txt" com "WinOS-ReadFileEx-42" (19 bytes).
 * Contratos (exit codes):
 *   10 = fixture criação
 *   11 = WriteFile
 *   12 = Close após escrita
 *   13 = reabrir para leitura
 *   14 = ReadFileEx offset 0 falhou (retorno FALSE)
 *   15 = callback não chamado
 *   16 = bytes transferidos incorretos
 *   17 = conteúdo incorreto
 *   18 = OVERLAPPED Internal/InternalHigh incorretos
 *   19 = ReadFileEx offset 6 falhou
 *   20 = conteúdo offset incorreto
 *   21 = NULL OVERLAPPED não recusado
 *   22 = handle inválido não recusado
 *   23 = zero-byte read falhou
 *   24 = limpeza DeleteFile
 *   75 = fluxo completo OK
 */

static const char kPath[] = "readex_g75.txt";
static const char kData[] = "WinOS-ReadFileEx-42"; /* 19 bytes */
#define KLEN 19

static volatile int g_called = 0;
static volatile DWORD g_error = 0xDEAD;
static volatile DWORD g_bytes = 0xDEAD;
static volatile LPOVERLAPPED g_ov = NULL;

VOID CALLBACK MyCompletion(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped) {
    g_called = 1;
    g_error = dwErrorCode;
    g_bytes = dwNumberOfBytesTransfered;
    g_ov = lpOverlapped;
}

int main(void) {
    HANDLE h;
    DWORD wr = 0;
    OVERLAPPED ov;
    char buf[64];
    BOOL ok;

    /* 1) fixture */
    h = CreateFileA(kPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return 10;
    if (!WriteFile(h, kData, KLEN, &wr, NULL) || wr != KLEN) return 11;
    if (!CloseHandle(h)) return 12;

    /* 2) reabrir para leitura (com FILE_FLAG_OVERLAPPED para realismo, mas Portico ignora flag) */
    h = CreateFileA(kPath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (h == INVALID_HANDLE_VALUE) return 13;

    /* 3) ReadFileEx offset 0 */
    memset(&ov, 0, sizeof ov);
    ov.Offset = 0;
    ov.OffsetHigh = 0;
    memset(buf, 0xEE, sizeof buf);
    g_called = 0;
    g_error = 0xDEAD;
    g_bytes = 0xDEAD;
    g_ov = NULL;

    ok = ReadFileEx(h, buf, KLEN, &ov, MyCompletion);
    if (!ok) return 14;
    if (!g_called) return 15;
    if (g_bytes != KLEN) return 16;
    if (g_error != 0) return 16;
    if (g_ov != &ov) return 15;
    /* conteúdo */
    for (int i = 0; i < KLEN; i++) if (buf[i] != kData[i]) return 17;
    /* OVERLAPPED Internal/High */
    if (ov.Internal != 0) return 18;
    if (ov.InternalHigh != KLEN) return 18;

    /* 4) ReadFileEx offset 6 -> "ReadFileEx-42" ; ler 4 bytes -> "Read" */
    memset(&ov, 0, sizeof ov);
    ov.Offset = 6;
    ov.OffsetHigh = 0;
    memset(buf, 0xEE, sizeof buf);
    g_called = 0;
    g_error = 0xDEAD;
    g_bytes = 0xDEAD;
    g_ov = NULL;

    ok = ReadFileEx(h, buf, 4, &ov, MyCompletion);
    if (!ok) return 19;
    if (!g_called) return 15;
    if (g_bytes != 4) return 16;
    if (buf[0] != 'R' || buf[1] != 'e' || buf[2] != 'a' || buf[3] != 'd') return 20;
    if (ov.InternalHigh != 4) return 18;

    /* 5) NULL OVERLAPPED -> deve falhar FALSE + 87 */
    SetLastError(0xDEAD);
    g_called = 0;
    ok = ReadFileEx(h, buf, 4, NULL, MyCompletion);
    if (ok) return 21;
    if (GetLastError() != 87) return 21;
    if (g_called) return 21; /* callback não deve ser chamado em falha */

    /* 6) handle inválido -> FALSE + 6 */
    SetLastError(0xDEAD);
    memset(&ov, 0, sizeof ov);
    g_called = 0;
    ok = ReadFileEx(INVALID_HANDLE_VALUE, buf, 4, &ov, MyCompletion);
    if (ok) return 22;
    if (GetLastError() != 6) return 22;
    if (g_called) return 22;

    /* 7) zero-byte read (n=0) -> TRUE + callback 0 bytes */
    memset(&ov, 0, sizeof ov);
    ov.Offset = 0;
    g_called = 0;
    g_bytes = 0xDEAD;
    ok = ReadFileEx(h, buf, 0, &ov, MyCompletion);
    if (!ok) return 23;
    if (!g_called) return 23;
    if (g_bytes != 0) return 23;
    if (ov.InternalHigh != 0) return 18;

    /* 8) limpeza */
    if (!CloseHandle(h)) return 12;
    if (!DeleteFileA(kPath)) return 24;
    if (GetFileAttributesA(kPath) != INVALID_FILE_ATTRIBUTES) return 24;

    return 75;
}
