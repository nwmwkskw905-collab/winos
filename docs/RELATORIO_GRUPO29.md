# RELATÓRIO — GRUPO 29: validação real de E/S de arquivos Windows

**Desfecho: CASO A — primeiro blocker real encontrado e corrigido (prefixo VFS no
harness); regressão completa verde; PARADO no próximo blocker real (defeito de ABI
no out-param de contagem de `ReadFile`/`WriteFile`), determinado com prova por
experimento controlado e NÃO corrigido neste grupo (regra: um blocker por grupo).**

---

## 1. Baseline (início do grupo)

```text
C checks:       3382 / 0
Swift tests:      63 / 0
gcc warnings:      0
analyzer:          0
PEs:        15/15 verdes
hello_gl11:      rc=42
hello_sse:       rc=7
hello_gl12:      rc=42
hello_stdio:     rc=5
```

## 2. Auditoria (ETAPA 1 — sem alterar código)

| Item | Estado encontrado |
|---|---|
| `CreateFileA` | `f_CreateFileA` (pr_win32.c:1701) — lê path do convidado, `vfs_resolve` → host, 8 slots de arquivo (`ctx->files[8]`), dispositions 1–5 (CREATE_NEW…TRUNCATE_EXISTING), fopen binário (`wb/w+b/rb/r+b`), erros reais (2/3/5/80/87), log `[VFS]`, handle = `W32_FILE_BASE(0xC0000000)\|(k+1)`, `INVALID_HANDLE_VALUE=-1` ✓ |
| `CreateFileW` | `f_CreateFileW` (L1767) — UTF-16→ASCII (não-ASCII recusado), núcleo duplicado do A (mesma semântica) |
| `ReadFile` | `f_ReadFile` (L729) — `fread` p/ `ctx->files[k]`, out-param `lpNumberOfBytesWritten` gravado como **`uint64_t` (8 bytes)** ← ver blocker #2; stdin = TRUE com 0 bytes (EOF) |
| `WriteFile` | `f_WriteFile` (L678) — `fwrite`+`fflush` p/ arquivo; stdout/stderr = captura de console com log; out-param **idem 8 bytes** ← blocker #2 |
| `GetFileSize` | `f_GetFileSize` — ftell/SEEK_END/restaura posição, retorno u32 correto, `lpFileSizeHigh` gravado como u64 (mesma classe do blocker #2; evitado com NULL no PE) |
| `CloseHandle` | `f_CloseHandle` — `fclose` p/ arquivo ✓; std handles = no-op TRUE; demais = `INVALID_HANDLE` (GDI usa DeleteObject) — "handles genéricos" = TODO declarado |
| VFS | `vfs_resolve` (L1675) — exige `fs_root ≠ ""` (**fail-closed**), pula drive `C:`/UNC, `\\`→`/`, **recusa `..`**, host = `fs_root + "/" + norm`; API `pr_win32_set_fs_root` / `pr_peproc_set_fs_root` |
| Testes unitários | `test_vfs.c` (criação/negativas/escape/`GetFileSize`==16/17), `test_win32.c`, `test_peproc.c` — todos via `pr_win32_call` com slots `uint64_t` (por isso a largura de 8 bytes nunca foi exposta) |

**Achado de auditoria (pré-execução)**: os out-params `LPDWORD` gravam 8 bytes em
ABI que prevê 4 — risco de corrupção de pilha para PE real (confirmado pelo
blocker #2).

## 3. PE criado

- **Fonte**: `realpe/hello_file.c` (novo, 51 linhas, determinístico).
- **Binário**: `Tests/PorticoRuntimeTests/data/hello_file.exe`
  (`x86_64-w64-mingw32-gcc -O2 -s`; 0 warnings; toolchain mingw reinstalado via
  pacotes .deb extraídos sem root em `~/.cache/mingwtc`).
- **Imports**: `KERNEL32.dll` = **CreateFileA, WriteFile, GetFileSize, ReadFile,
  CloseHandle** (exatamente as 5 do fluxo) + `msvcrt.dll` = só o CRT MinGW
  (`exit/_initterm/__set_app_type/...`) — justificado: retorno de `main` →
  `exit` → `ExitProcess`, **mesmo mecanismo já validado** dos demais `hello_*`.
  Nenhuma outra API.
- **Comportamento**: `CreateFileA(winos_file_test.txt, GENERIC_WRITE,
  CREATE_ALWAYS)` → `WriteFile(17 bytes)` → `GetFileSize` → `CloseHandle` →
  `CreateFileA(GENERIC_READ, OPEN_EXISTING)` → `ReadFile` → comparação byte a
  byte → `CloseHandle` → `return 42`.
- **Arquivo**: `winos_file_test.txt` (relativo ao prefixo do convidado →
  `build/win_fs/winos_file_test.txt` no host), conteúdo `"winos-file-io-g29"`
  (17 bytes). Sem rede/threads/sockets/caminhos de host.
- **Contrato de saída**: **42** = fluxo completo; **10..19** = etapa da primeira
  falha (10 criar-escrever, 11 WriteFile, 12 contagem escrita, 13 fechar-escrita,
  14 reabrir, 16 ReadFile, 17 contagem leitura, 18 comparação, 19 fechar-leitura,
  15 GetFileSize).

## 4. Execução (ETAPA 4 — cadeia real)

- **Ponto de entrada**: RVA `0x13F0` (PE32+ CUI 5.2). **DLLs**: KERNEL32 + msvcrt.
- **Execução 1 (sem alterar runtime/harness)**: `exited=1 rc=10 exec=321` —
  falha na etapa `CreateFileA`. Cadeia: PE→loader→imports→kernel32→VFS.
  Erro registrado: `W32_ERROR_PATH_NOT_FOUND (3)` → `INVALID_HANDLE_VALUE`
  (fs_root vazio; `pr_win32.c:1711-1715`).
- **Após o fix do harness (blocker #1)**: `exited=1 rc=18 exec=395` — etapas 10–17
  **passaram** (handles `0xC0000001`, bytes escritos=17, `GetFileSize`=17,
  `CloseHandle`×2, bytes lidos=17); falha **somente a comparação de conteúdo**.
- **Bytes no host** (`docs/evidencias_g29/arquivo_convidado.txt`):
  `77 69 6e 6f 73 2d 66 69 6c 65 2d 69 6f 2d 67 32 39` = `"winos-file-io-g29"`
  **exato** — o caminho WriteFile→arquivo→ReadFile preserva os dados.
- **Execução diagnóstica controlada** (`docs/evidencias_g29/hello_file_diag.c` =
  `hello_file.c` com `lpNumberOfBytesWritten/read = NULL`):
  **`exited=1 rc=42 exec=493`** — **fluxo completo aprovado** quando os out-params
  de contagem não são passados. Prova por isolamento de variável única.
- **RIP/opcode de fault**: nenhum (encerramento normal via `msvcrt!exit` em todas
  as execuções). **Log estruturado**: eventos `[VFS] CreateFile…` + 155–395
  instruções/execução conforme a ferramenta (`dbg_input hello_file → log`).

## 5. Blocker

**Blocker #1 — CORRIGIDO (este grupo)**
- **Localização**: VFS — provisionamento do prefixo no **harness de execução real**.
- **Causa**: `vfs_resolve` exige `fs_root` ≠ "" (fail-closed, correto por design);
  as ferramentas `tools/dbg_*.c` **nunca chamavam** `pr_peproc_set_fs_root`
  (só `test_vfs.c` o fazia) → `CreateFileA` sempre `INVALID_HANDLE_VALUE` +
  `PATH_NOT_FOUND` no caminho real.
- **Evidência**: rc=10 em `hello_file` (etapa CreateFileA); cadeia de código
  `f_CreateFileA`→`vfs_resolve` (L1675-1697); grep: nenhuma tool definia raiz.
- **Correção aplicada** (menor patch, **runtime intocado** — o comportamento
  fail-closed está correto): `tools/dbg_diag.c`, `dbg_input.c`, `dbg_log.c`,
  `dbg_px.c` — antes de `pr_peproc_prepare`: `mkdir("build/win_fs", 0777)` +
  `pr_peproc_set_fs_root(p, "build/win_fs")` (mesmo papel do host real/app iOS,
  que configura o prefixo do Portico).

**Blocker #2 — DETERMINADO, NÃO corrigido (parada do grupo)**
- **Localização**: `f_WriteFile`/`f_ReadFile` (`pr_win32.c`) — out-param
  `lpNumberOfBytesWritten/read`; mesma classe em `f_GetFileSize`
  (`lpFileSizeHigh`).
- **Causa**: gravação de **`uint64_t` (8 bytes)** onde a ABI Win32 real (`LPDWORD`)
  grava **`DWORD` (4 bytes)** → 4 bytes excedentes corrompem a pilha do convidado
  adjacente ao `DWORD wr/got` real (a comparação em `main` lê `buf[]` atingido →
  rc=18). Os testes unitários usam slots `uint64_t` e mascaram o defeito.
- **Evidência**: (a) rc=18 com contagens corretas (etapas 12 e 17 passaram) e
  arquivo íntegro; (b) experimento controlado sem out-params → **rc=42**;
  (c) código: `*written = put` com `uint64_t*` validado por
  `pr_win32_ptr(..., sizeof(uint64_t))`.
- **Correção prevista (NÃO aplicada — próximo grupo)**: gravar/validar
  **4 bytes** (`DWORD`) em `f_WriteFile`/`f_ReadFile` (e `lpFileSizeHigh`),
  preservando os testes unitários (comparações em 32 bits).

## 6. Regressão (após a correção do blocker #1 — completa)

```text
make c-test            → 3382 verificações, 0 falhas
swift test             → Executed 63 tests, with 0 failures
gcc -Wall -Wextra      → 0 warnings (tools incluídas na checagem)
gcc -fanalyzer (build/analyzer29.err, src+tests+tools) → 0 diagnósticos
PE battery (15 PEs)    → 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
hello_input (sem noinput) → rc=42 log=105
hello_gl11             → rc=42   ✓ (preservado)
hello_sse              → rc=7    ✓ (preservado)
hello_gl12             → rc=42   ✓ (preservado)
hello_stdio            → rc=5    ✓ (preservado)
hello_file             → rc=18   (estado real atual — ver blocker #2)
```

## 7. Estado final

| Item | Classificação |
|---|---|
| `CreateFileA` | IMPLEMENTADO · UNIT-TESTADO · exercitado por PE real (semântica confirmada) |
| `CreateFileW` | IMPLEMENTADO · UNIT-TESTADO · NÃO exercitado por PE real (ASCII é o usado) |
| `WriteFile` (arquivo) | IMPLEMENTADO · UNIT-TESTADO · exercitado por PE real (bytes íntegros no host) — out-param com defeito de ABI (blocker #2) |
| `WriteFile` (console) | VALIDADO POR PE REAL (hello_stdio/app — stdout/stderr) |
| `ReadFile` | IMPLEMENTADO · UNIT-TESTADO · exercitado por PE real (contagem correta) — out-param com defeito (blocker #2) |
| `GetFileSize` | VALIDADO POR PE REAL (retornou 17 exato no fluxo) · UNIT-TESTADO |
| `CloseHandle` (arquivo/std) | VALIDADO POR PE REAL (2× no fluxo) · UNIT-TESTADO |
| `CloseHandle` (genérico) | NÃO IMPLEMENTADO (TODO declarado — GDI usa DeleteObject) |
| VFS (`vfs_resolve`/prefixo) | VALIDADO POR PE REAL (após provisionar raiz) · UNIT-TESTADO |
| Subsistema E/S de arquivos | **EM DESENVOLVIMENTO** — critério ETAPA 7 não pleno: `rc=42` só no build diagnóstico; comparação falha pelo blocker #2 |
| `SetFilePointer` | IMPLEMENTADO (handler existe) · UNIT-TESTADO · NÃO exercitado por PE real |
| `ReadFileEx` | NÃO IMPLEMENTADO (TODO) |
| MB/WC, `CreateThread`, `WaitForSingleObject`, TLS, registro, COM, sockets, fontes | NÃO IMPLEMENTADO (TODOs — fora do escopo) |

- **Concluído**: auditoria completa; PE real mínimo criado no padrão `hello_*`;
  caminho `PE→loader→imports→kernel32→VFS→arquivo` executado de verdade;
  blocker #1 corrigido com patch mínimo; regressão total verde; blocker #2
  determinado com prova controlada.
- **Em andamento**: validação plena da E/S de arquivos (6 das 9 condições da
  ETAPA 7 já comprovadas por execução real: criar/abrir, WriteFile, GetFileSize,
  reabrir, ReadFile, CloseHandle; pendem: comparação sem corrupção, rc=42 e o
  critério de regressão após a correção final).
- **Não validado por PE real**: `CreateFileW`, `SetFilePointer`, out-params
  (enquanto o blocker #2 persistir).

## 8. Próximo passo

Derivado **exclusivamente** do blocker encontrado (o próximo blocker real, já
determinado pela execução):

**Corrigir a largura do out-param de contagem em `f_WriteFile`/`f_ReadFile`**
(`lpNumberOfBytesWritten/read`: gravar/validar `DWORD` de 4 bytes, como a ABI
Win32 real) e o `lpFileSizeHigh` de `f_GetFileSize`; re-executar `hello_file`
(esperado: **rc=42**) e concluir o critério da ETAPA 7.

Nenhum outro subsistema foi implementado ou antecipado. Sem declaração de
compatibilidade com GTA V, MX Bikes ou jogos comerciais — progresso medido apenas
por cobertura/validação real do runtime.
