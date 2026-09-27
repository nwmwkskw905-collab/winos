# Relatório do Grupo 48 — Inventário do próximo capability real (estado pós-G47)

## STATUS

**CONCLUÍDO** — inventário técnico completo do `PorticoRuntime` (estado final do G47) + seleção de **UM** próximo capability. **Nada foi implementado**; **nenhum arquivo do runtime foi alterado** (byte-for-byte idêntico); nenhum PE novo criado; nenhum blocker antecipado; nenhum opcode varrido.

## 1. BASELINE CONFIRMADO (estado final do G47)

- C tests: `3392 verificações, 0 falhas` (3392/3392) ✓ · Swift tests: `Executed 63 tests, with 0 failures` (63/63) ✓ · warnings: 0 ✓ · analyzer: 0 ✓
- PE battery: `15/15` (28/43/180/70/72/75/98/118/76/81/194/131/55/200/105) ✓
- hello_wait_multiple = 42/32 ✓ · hello_thread_timeout = 42/55 ✓ · hello_mutex_timeout = 42/40 ✓ · hello_thread_shared = 42/33 ✓ · hello_thread = 42/30 ✓ · hello_mutex = 42/39 ✓ · hello_cs = 42/44 ✓ · hello_virt = 42/64 ✓ · hello_virt_protect = 42/31 ✓ · hello_virt_query = 42/33 ✓ · hello_qpc = 42/99 ✓ · hello_heap = 42/31 ✓ · hello_mbwc = 42/29 ✓ · hello_file = 42/36 ✓ · hello_file_w = 42/33 ✓ · hello_file_seek = 42/33 ✓ · hello_stdio = 5/155 ✓ · hello_gl12 = 42/116 ✓ · hello_sse = 7/209 ✓
- **MD5: `c9e8afa71214412be92bff3de0b35278`** — igual ao registrado no briefing ✓

## 2. INVENTÁRIO

### Catálogo (`pr_win32.c`) — 226 entradas = `count=226` dos stubs

| Macro | Qtd | Significado (definição real no código) |
|---|---|---|
| `IMPL` | 118 | `{…, PR_WIN32_IMPLEMENTED, fn, b, "comportamento real", …}` |
| `IMPL_NOTE` | 58 | implementada **com nota** de comportamento/limitação documentada |
| `IMPL_ORD` | 4 | implementada **com ordinal público** (`"comportamento real (ordinal publico)"`) — ws2_32 |
| `IMPL_FX` | 12 | implementada com **argumentos em XMM** (`arg_xmm`) + nota — GL float |
| `DATA_SYM` | 3 | dados exportados (célula RW no convidado): `__initenv`, `_fmode`, `_commode` |
| `TODO` | 31 | `PR_WIN32_UNSUPPORTED` (chamada = EXECUTION STOPPED honesto) |

- Handlers `f_*`: **187** (kernel32/user32/gdi32/opengl32/msvcrt/ws2_32).
- Testes C: **35** (`test_win32`, `test_win32x`, `test_wincompat`, `test_vfs`, `test_gl1–9`, `test_gl10/11`, `test_gdiwin`, `test_pe*`, `test_cpu*`, …). Testes Swift: **10** suítes (`WindowsExecutionTests`, `RuntimeAndHonestyTests`, `CompatLayerTests`, …).
- PEs reais: **35** `realpe/hello_*.c` executados (battery + especiais).
- Método de cruzamento: tabela IMPL*/TODO × imports reais dos 35 PEs (`objdump -p`, 1744 linhas) × cobertura de `grep` nos testes C × histórico dos grupos G3–G47.

### A — Validado por PE real

**A1 (com contrato/asserção de PE — rc + fluxo verificado):**
- Loader/PE x64 real: relocations, imports por nome, DLL própria (`hello_dll`), stubs `INT 0x2E`, dispatch Win32 **dentro das workers** (G44), `.pdata`.
- kernel32: `VirtualAlloc/Free/Protect/Query` (hello_virt*), `QueryPerformanceCounter/Frequency` (hello_qpc; tempos G45/G46), `GetProcessHeap/HeapAlloc/HeapFree/HeapSize` (hello_heap), `CreateFileA/CreateFileW/ReadFile/WriteFile/GetFileSize/SetFilePointer/CloseHandle-file` (hello_file/file_w/file_seek), `MultiByteToWideChar/WideCharToMultiByte/IsDBCSLeadByteEx` (hello_mbwc), `Sleep` (150 ms reais medidos em G45), CS ×4 (hello_cs), `CreateThread/WaitForSingleObject-thread/CloseHandle-thread` (hello_thread), threads+memória compartilhada (hello_thread_shared), `CreateMutexA/ReleaseMutex/Wait-mutex/CloseHandle-mutex` (hello_mutex; timeout G45→mutex), timeout finito thread (G46), `WaitForMultipleObjects` subconjunto (G47).
- user32/gdi32: janela/mensagem/pintura (`RegisterClassA/CreateWindowExA/ShowWindow/GetMessageA/TranslateMessage/DispatchMessageA/DefWindowProcA/BeginPaint/EndPaint/GetClientRect/GetDC/ReleaseDC/FillRect/SetTimer/KillTimer/DestroyWindow/PostQuitMessage/UpdateWindow/SetPixel/GetPixel/CreateSolidBrush/DeleteObject/SwapBuffers`) — hello_user/app/input/gdi.
- opengl32/wgl: subconjunto GL 1.1+1.2 (49 APIs importadas, gl1–gl12) — **não** GL completo (regra permanente).
- msvcrt: CRT exercitado pelos PEs (malloc/calloc/free/realloc/memcpy/memset/strlen/strcmp/strncmp/fwrite/fflush-interno/vfprintf/fprintf/fputc/strerror/localeconv/wcslen/exit…).
- `LoadLibraryA/FreeLibrary/GetModuleHandleA/GetProcAddress` (imports de PEs + hello_dll-era) — A1/A2.
- CPU x64: subconjunto + SSE/SSE2 escalar (hello_sse) + `PUNPCKLQDQ` (G47).

**A2 (exercitado por PEs reais — despachado em execução — mas sem asserção semântica dedicada):** `GetLastError/SetLastError` (caminhos de erro de arquivo), `GetTickCount64`, `SetUnhandledExceptionFilter` (startup), `GetProcAddress` (caminhos pontuais).

### B — Implementado + unit-tested, mas ainda sem PE real (candidatas reais — 50 APIs)

Definição mecânica: entrada `IMPL/IMPL_NOTE/IMPL_ORD` cujo `dll!name` **não aparece em NENHUM import dos 35 PEs**.

- **IMPL_ORD (4) — ws2_32: `htonl`, `htons`, `ntohl`, `ntohs`** — unit-tested em `test_wincompat.c`. (Escolhida — seção 4.)
- IMPL (30): kernel32 `GetStdHandle`, `lstrlenA`, `lstrcpyA`, `lstrcpynA`, `ExitProcess`, `TerminateProcess`, `GetCurrentProcessId`, `GetCurrentThreadId`, `IsDebuggerPresent`, `LoadLibraryW`, `GetModuleHandleW`, `GetCommandLineA`, `GetCommandLineW`, `GetCurrentDirectoryA`, `GetVersion`, `GetVersionExA`, `OutputDebugStringA`; user32 `SetFocus`, `GetFocus`; gdi32 `CreateCompatibleDC`, `SelectObject`, `CreateCompatibleBitmap`, `BitBlt`, `PatBlt`, `DeleteDC`, `GetDeviceCaps`; msvcrt `strcpy`, `strcat`, `printf`, `fflush`.
- IMPL_NOTE (16): kernel32 `GetEnvironmentVariableA/W`, `GetSystemInfo`; user32 `MessageBoxA/W`, `InvalidateRect`, `PeekMessageA/W`, `PostMessageA/W`, `GetMessageW`, `DispatchMessageW`, `DefWindowProcW`, `GetKeyState`, `GetAsyncKeyState`; gdi32 `StretchBlt`.
- Cobertura unit-test confirmada para a família (`test_wincompat.c` p/ ws2_32; `test_dll/test_pe*/test_win32/test_win32x` p/ tls/tick/version/env/cmdline/lstr/module).

### C — Implementado parcialmente (limitações objetivas)

- `CloseHandle`: files/std/threads/mutex reais; **handles genéricos** (GDI/processo) ⇒ `FALSE`+`ERROR_INVALID_HANDLE` (TODO residual mantido de propósito).
- `WaitForSingleObject`: só threads (join/poll/timeout real) e mutexes (lock/poll/timeout real); outros tipos ⇒ `WAIT_FAILED`.
- `WaitForMultipleObjects`: **só** `(2, thread_handles, TRUE, INFINITE)`; wait-any/nCount≠2/timeout/`WFMOEx`/`MsgWait` ⇒ `WAIT_FAILED`+erro honesto.
- `CreateMutexA`: só `(NULL, bInitialOwner, NULL)` — sem nome; 8 mutexes/processo.
- `CreateThread`: `lpSecurityAttributes=NULL`, `dwCreationFlags=0`; 8 threads; sem `GetExitCodeThread`/`TerminateThread`.
- **TLS pela metade**: `TlsGetValue` implementado (64 slots) mas **`TlsAlloc` = TODO** e `TlsSetValue` inexistente — `TlsGetValue` é importado pelo startup mingw e **nunca despachado** em nenhum PE.
- `MultiByteToWideChar/WideCharToMultiByte`: subconjunto de flags (TODO residual honesto).
- `SetFilePointer`: modos além do suportado (TODO residual).
- `SetTimer` (nota + TODO residual), `GetEnvironmentVariable*` (nota: ambiente do convidado), `MessageBox*` (nota: janela interna/modal simplificado).
- CRITICAL_SECTION: **48 bytes vs 40 reais do winnt.h** (divergência documentada no G41, não corrigida).
- CPU x64: subconjunto (packed aritmético `66` fora; AVX/VEX fora; x87 parcial) — fault honesto fora dele.

### D — Ainda não implementado (TODOs reais — chamada = EXECUTION STOPPED)

- kernel32: `GetModuleFileNameA`, `ReadFileEx`, `TlsAlloc`.
- user32: `GetSystemMetrics`.
- advapi32 (registro): `RegOpenKeyExA`, `RegQueryValueExA`, `RegSetValueExA`, `RegCloseKey`.
- gdi32: `CreateFontA`, `TextOutA`.
- ole32 (COM): `CoInitialize`, `CoInitializeEx`, `CoCreateInstance`, `CoUninitialize`.
- shell32: `ShellExecuteA`, `CommandLineToArgvW`.
- ws2_32 (sockets): `WSAStartup`, `WSACleanup`, `socket`, `connect`, `send`, `recv`, `closesocket`, `getaddrinfo`.
- msvcrt: `__C_specific_handler` (SEH), `signal`.
- (TODOs residuais de APIs da categoria C estão listados lá — não duplicados aqui.)

### E — Fora de escopo (política permanente — separado de D)

- DirectX/D3D9 completo; JIT; OpenGL completo; GPU Windows real; DRM/assinatura/bypass de sandbox; código proprietário/assets de terceiros (Winlator etc.); `CreateRemoteThread`; APCs; mutex nomeado entre processos; declarações de compatibilidade geral com Windows/jogos comerciais (GTA V, MX Bikes…).

## 3. PRINCIPAIS CANDIDATAS (ordem de atratividade)

1. **ws2_32 `htonl/htons/ntohl/ntohs` (IMPL_ORD)** ← ESCOLHIDA
2. `GetVersion/GetVersionExA` + `GetSystemInfo` (identidade do sistema — constantes objetivas)
3. `GetCommandLineA/W` + `GetCurrentDirectoryA` (strings do processo)
4. `lstrlenA/lstrcpyA/lstrcpynA` (strings kernel32)
5. Cluster GDI: `CreateCompatibleDC/SelectObject/CreateCompatibleBitmap/BitBlt/PatBlt/DeleteDC/GetDeviceCaps` (compat GDI — 7 APIs num PE)
6. `GetEnvironmentVariableA/W` (ambiente do convidado)
7. `GetStdHandle` + std handles (paths std de `WriteFile` — só unit-tested hoje)
8. msvcrt `printf/fflush/strcpy/strcat` (stdio formatado completo)
9. `MessageBoxA` (modal/janela interna — aceitação menos objetiva)
10. TLS completo — exigiria implementar `TlsAlloc`+`TlsSetValue` (D→novo) = mais implementação que as demais

## 4. UMA CANDIDATA ESCOLHIDA

**ws2_32: `htonl`, `htons`, `ntohl`, `ntohs`** — capability **`IMPL_ORD`** (ordinais públicos 8/9/14/15 do winsock 1.1), handlers `f_htonl/f_htons/f_ntohl/f_ntohs` (`pr_win32.c` ~L3240; `ntohl` reusa `htonl` — "inverso é a mesma operação"), unit-tested em `Tests/PorticoRuntimeTests/test_wincompat.c`, **sem nenhum PE real até hoje** (nenhum dos 35 PEs importa `ws2_32.dll`).

## 5. JUSTIFICATIVA TÉCNICA OBJETIVA

- **Já existe por completo** (`"comportamento real (ordinal publico)"`) — **zero** implementação nova exigida ✓
- **Unit-tested + sem PE** = correspondência exata com a categoria **B** ✓
- **PE mínimo** (≈30 linhas, 5 asserções de constantes) ✓
- **Sem CPU especulativo**: apenas inteiros/byte-swap (se o gcc emitir algo fora do subset, será blocker observado no próximo grupo, regra G47) ✓
- **Aceitação objetiva**: valores exatos de byte-swap são matemática — sem margem, sem timing ✓
- **Não duplica** nada validado: nenhum PE tocou `ws2_32.dll` ✓ e valida também o **binding de import por ordinal público** (catálogo `IMPL_ORD`), caminho do loader sem evidência de PE ✓

## 6. APIs ENVOLVIDAS

`ws2_32.dll!htonl` (ordinal 8), `ws2_32.dll!htons` (9), `ws2_32.dll!ntohl` (14), `ws2_32.dll!ntohs` (15) — exatamente 4. Mais CRT de startup (já validado). Nenhuma outra.

## 7. LIMITAÇÕES CONHECIDAS

- Sockets (`WSAStartup/socket/connect/send/recv/closesocket/getaddrinfo`) = D — **não** são alvo; `htonl` etc. **não exigem** `WSAStartup` (igual ao Windows real).
- `ntohl`/`ntohs` reutilizam `htonl`/`htons` (documentado no código) — operações idênticas em qualquer endianness-alvo x86.
- Binding por **nome vs ordinal**: o `import lib` do MinGW (`-lws2_32`) pode emitir o binding por nome; o `IMPL_ORD` cobre os dois (nome + ordinal). Se o binding sair por nome, o caminho **por ordinal** permanece sem PE real (registrar no próximo grupo; não forçar nada agora).
- Sem `WSACleanup`/estado de rede — a capability é só a família byte-swap.

## 8. PROPOSTA DE PE MÍNIMO (NÃO criado neste grupo)

`realpe/hello_winsock_ord.c` → `Tests/PorticoRuntimeTests/data/hello_winsock_ord.exe` (`x86_64-w64-mingw32-gcc -O2 -s … -lws2_32`):

```c
#include <winsock2.h>            /* ou declarações diretas u_long/u_short */
int main(void) {
    if (htonl(0x12345678u) != 0x78563412u) return 10;
    if (ntohl(0x12345678u) != 0x78563412u) return 11;
    if ((u_short)htons(0x1234u) != 0x3412u) return 12;
    if ((u_short)ntohs(0x1234u) != 0x3412u) return 13;
    if (htonl(ntohl(0xAABBCCDDu)) != 0xAABBCCDDu) return 14;
    return 42;
}
```

Fluxo de aceitação: 4 constantes exatas + 1 round-trip + rc 42. Registrar (§5 do padrão dos grupos): imports (ws2_32 por nome/ordinal), série do log (`htonl ×…`), rc, MD5 do runtime intocado na 1ª execução.

## 9. CONTRATO DE RETORNO SUGERIDO

- `10` = `htonl` incorreto · `11` = `ntohl` incorreto · `12` = `htons` incorreto · `13` = `ntohs` incorreto · `14` = round-trip incorreto · `42` = fluxo completo. Nenhum outro código.

## 10. CONFIRMAÇÃO DE ARQUIVOS DO RUNTIME

**Nenhum arquivo do runtime foi alterado.** Somente leitura (`grep`/`objdump`/`ls`) + artefatos descartáveis de `build/` (tools e `pe_imports.txt`). `Sources/PorticoRuntime/**` e `include/**` intactos.

## 11. MD5 ANTES/DEPOIS

- Antes: `c9e8afa71214412be92bff3de0b35278`
- Depois: `c9e8afa71214412be92bff3de0b35278` — **idêntico** ✓ (byte-for-byte)

## 12. REGRESSÃO COMPLETA

- C tests: `3392 verificações, 0 falhas` ✓ · Swift tests: `Executed 63 tests, with 0 failures` ✓ · warnings: 0 ✓ · analyzer: 0 ✓
- PE battery: `15/15` ✓ · hello_wait_multiple=42 · hello_thread_timeout=42 · hello_mutex_timeout=42 · hello_thread_shared=42 · hello_thread=42 · hello_mutex=42 · hello_cs=42 · hello_virt=42 · hello_virt_protect=42 · hello_virt_query=42 · hello_qpc=42 · hello_heap=42 · hello_mbwc=42 · hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_stdio=5 · hello_gl12=42 · hello_sse=7 ✓

**Não se afirma compatibilidade geral com Windows nem com jogos comerciais.**
