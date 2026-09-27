# RELATÓRIO — GRUPO 30: correção da ABI dos out-params de E/S

**Desfecho: CONCLUÍDO — `hello_file.exe → rc=42` (fluxo completo aprovado) com
regressão completa verde. E/S de arquivos = IMPLEMENTADO · UNIT-TESTADO ·
VALIDADO POR PE REAL.**

---

## 1. Estado inicial (início do grupo)

```text
Baseline (G29): c-test 3382/0 · swift 63/0 · 0 warnings · 0 analyzer · 15/15 PEs
hello_gl11=42 · hello_sse=7 · hello_gl12=42 · hello_stdio=5 · hello_file=18
Blocker comprovado no G29: out-params LPDWORD gravados como uint64_t (8 bytes)
  → corrupção da pilha do convidado → hello_file falhava na comparação (rc=18)
  mesmo com bytes escritos=17, GetFileSize=17, bytes lidos=17 e arquivo íntegro.
  Experimento controlado sem out-params (hello_file_diag) → rc=42 isolou a causa.
```

## 2. Causa do ABI incorreto

`lpNumberOfBytesWritten`/`lpNumberOfBytesRead`/`lpFileSizeHigh` são **`LPDWORD`**
na ABI Win32 real (**4 bytes**). Os handlers os tratavam como `uint64_t*`
(**8 bytes**): validavam `sizeof(uint64_t)` via `pr_win32_ptr` e gravavam
`*out = valor` em 8 bytes. Para um PE real compilado com `DWORD` (4 bytes), os
4 bytes excedentes sobrescrevem a memória adjacente do convidado (em `hello_file`,
a região de `buf[]` usada na comparação → rc=18). Os testes unitários usavam slots
`uint64_t` como alvo e nunca expunham o overrun (`CHECK_EQ_U32` lia só os 32 bits
baixos) — o defeito ficava mascarado.

## 3. Arquivos alterados

| Arquivo | Papel |
|---|---|
| `Sources/PorticoRuntime/src/pr_win32.c` | 5 sítios de out-param corrigidos (runtime) |
| `Tests/PorticoRuntimeTests/test_vfs.c` | adaptado à ABI real + guardas adjacentes + nova cobertura de `lpFileSizeHigh` |
| `Tests/PorticoRuntimeTests/test_win32.c` | adaptado à ABI real + guardas adjacentes |

**NÃO alterado** (regra de segurança respeitada): VFS, `vfs_resolve`, `CreateFileA`,
lógica de criação/leitura/escrita de arquivos, handles, `hello_gl12`, OpenGL, SSE,
CPU, loader, imports. `hello_file.exe` e `realpe/hello_file.c` **intocados** (o PE
é a régua — não foi adaptado ao runtime).

## 4. Patch aplicado (mínimo — só a representação do out-param)

Nos 5 sítios: `uint64_t*` → `uint32_t*`, `sizeof(uint64_t)` → `sizeof(uint32_t)`,
gravação com cast `(uint32_t)` — comentário `/* ABI Win32: LPDWORD (4 bytes) */`:

1. `f_WriteFile` — caminho **arquivo** (`lpNumberOfBytesWritten` = `put`);
2. `f_WriteFile` — caminho **console** (`lpNumberOfBytesWritten` = `len`);
3. `f_ReadFile` — caminho **arquivo** (`lpNumberOfBytesRead` = `got`);
4. `f_ReadFile` — caminho **stdin/EOF** (`lpNumberOfBytesRead` = `0`);
5. `f_GetFileSize` — `lpFileSizeHigh` (`*hi = 0`); **comportamento `NULL`
   preservado** (o ramo `if (n >= 2 && a[1])` segue intacto — verificado com
   `GetFileSize(h, NULL)` = 17 no PE e nos testes).

Fora de escopo, auditado e **não alterado**: `QueryPerformanceFrequency/Counter`
gravam `LARGE_INTEGER*` (8 bytes = **ABI correta**); estruturas L542/811/822/832.

## 5. Mudanças nos testes

- **Identificados** todos os pontos que passam out-params: `test_vfs.c`
  (`slots[0]` WriteFile ×2, `slots[1]` ReadFile, `GetFileSize` só com `hi=NULL`) e
  `test_win32.c` (`slots[2]` WriteFile console, `slots[3]` ReadFile stdin).
- **Adaptado** para a ABI real: os alvos dos out-params passaram a `uint32_t`
  (DWORD) em memória validada pelo `pr_win32_ptr` (região `scratch`), em **pares
  [valor | guarda adjacente]** — `test_vfs`: `slots[0..7]` como DWORDs;
  `test_win32`: `dslots[0..3]` em `scratch+144` (os `slots[0..1]` de
  `QueryPerformance*` continuam `uint64_t` = `LARGE_INTEGER`, correto).
- **Cobertura só aumentou** (3382 → **3392 verificações**): +10 checks de guarda e
  uma **nova** cobertura: `GetFileSize` com `lpFileSizeHigh` **não-NULL**
  (antes inexistente). Nenhum teste foi removido ou enfraquecido.
- Observação de harness registrada durante a correção: `pr_win32_ptr` exige que o
  ponteiro resida em região validada (VM/imagem/scratch/heap) — um local de pilha
  do host é legitimamente rejeitado (primeira versão do teste usou `struct` na
  pilha e foi rejeitada; corrigido para `scratch`, sem tocar no runtime).

## 6. Resultado do `hello_file`

```text
./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_file.exe
→ exited=1 rc=42 exec=501   (ExitProcess via msvcrt!exit @0x00FF63CF)

Fluxo completo aprovado:
CreateFileA → WriteFile → GetFileSize → CloseHandle →
CreateFileA (reabrir) → ReadFile → comparação → CloseHandle → ExitProcess(42)

Arquivo: build/win_fs/winos_file_test.txt =
  77 69 6e 6f 73 2d 66 69 6c 65 2d 69 6f 2d 67 32 39  "winos-file-io-g29"
  (17 bytes, hex exato)
```

## 7. Regressão completa (após o patch)

```text
make c-test            → 3392 verificações, 0 falhas
swift test             → Executed 63 tests, with 0 failures
gcc -Wall -Wextra      → 0 warnings
gcc -fanalyzer (build/analyzer30.err: src+tests+tools) → 0 diagnósticos
PE battery (15 PEs)    → 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
hello_input (sem noinput) → rc=42 log=105
hello_gl11             → rc=42   ✓ (preservado)
hello_sse              → rc=7    ✓ (preservado)
hello_gl12             → rc=42   ✓ (preservado)
hello_stdio            → rc=5    ✓ (preservado)
hello_file             → rc=42   ✓ (NOVO — objetivo do grupo)
```

## 8. Confirmação de ausência de corrupção

- **`WriteFile` informa 17 bytes** ✓ (etapa 12 do PE passou);
- **`GetFileSize` informa 17** ✓ (etapa 15);
- **`ReadFile` informa 17 bytes** ✓ (etapa 17);
- **buffer do convidado sem corrupção** ✓ — a comparação byte a byte
  (`buf[i] != kData[i]`) passou (etapa 18 não ocorreu; rc=42);
- **nenhum dado adjacente aos out-params foi alterado** ✓ — evidência
  antes/depois das guardas adjacentes (pares `[valor|guarda]` em `scratch`):

| Out-param | Antes (valor/guarda) | Depois (valor/guarda) | Resultado |
|---|---|---|---|
| WriteFile arquivo (`test_vfs`) | `0 / 0xC0FFEE01` | `16 / 0xC0FFEE01` · depois `1 / 0xC0FFEE01` | guarda intacta |
| ReadFile arquivo (`test_vfs`) | `0 / 0xC0FFEE02` | `16 / 0xC0FFEE02` | guarda intacta |
| GetFileSize hi (`test_vfs`, nova) | `0xFFFFFFFF / 0xC0FFEE03` | `0 / 0xC0FFEE03` | guarda intacta |
| WriteFile console (`test_win32`) | `0 / 0xC0FFEE03` | `9 / 0xC0FFEE03` | guarda intacta |
| ReadFile stdin (`test_win32`) | `0xdead / 0xC0FFEE04` | `0 / 0xC0FFEE04` | guarda intacta |

  No PE real: antes do patch os 4 bytes excedentes corrompiam `buf[]` (rc=18);
  depois, `wr`, `got`, `h` e `buf` permanecem íntegros (rc=42). Arquivo no host
  byte a byte idêntico ao escrito (item 6).

## 9. Estado final da E/S de arquivos

| Item | Classificação |
|---|---|
| `CreateFileA` | IMPLEMENTADO · UNIT-TESTADO · **VALIDADO POR PE REAL** |
| `WriteFile` (arquivo e console) | IMPLEMENTADO · UNIT-TESTADO · **VALIDADO POR PE REAL** |
| `GetFileSize` (incl. `lpFileSizeHigh`) | IMPLEMENTADO · UNIT-TESTADO · **VALIDADO POR PE REAL** |
| `ReadFile` | IMPLEMENTADO · UNIT-TESTADO · **VALIDADO POR PE REAL** |
| `CloseHandle` (arquivo/std) | IMPLEMENTADO · UNIT-TESTADO · **VALIDADO POR PE REAL** |
| VFS (`vfs_resolve`/prefixo) | IMPLEMENTADO · UNIT-TESTADO · **VALIDADO POR PE REAL** |
| **Subsistema E/S de arquivos** | **IMPLEMENTADO · UNIT-TESTADO · VALIDADO POR PE REAL** (critério do G30 atendido) |
| `CreateFileW`, `SetFilePointer`, `ReadFileEx`, handles genéricos | fora do fluxo deste grupo — estados anteriores preservados (não exercitados por PE real) |

## 10. Próximo blocker real

**Nenhum.** O fluxo alvo (`CreateFileA→WriteFile→GetFileSize→CloseHandle→
reabrir→ReadFile→comparar→CloseHandle→return 42`) executou completo sem revelar
outro blocker — não há problema real a derivar. Por regra do briefing
("não criar trabalho adicional caso `hello_file` passe"), nenhum próximo grupo é
proposto aqui; expansões (`CreateFileW` por PE real, `SetFilePointer`, etc.)
permanecem fora de escopo e não foram antecipadas.

Sem declaração de compatibilidade com GTA V, MX Bikes ou jogos comerciais.
