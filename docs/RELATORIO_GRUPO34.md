# RELATÓRIO — GRUPO 34: validação real do Heap Win32 por PE x64

**Desfecho: CONCLUÍDO SEM BLOCKER — `hello_heap.exe → rc=42` na primeira
execução, com runtime INTOCADO. Heap Win32 = VALIDADO POR PE REAL.**

---

## 1. Baseline (início do grupo)

```text
C tests:     3392 / 0        Swift tests:   63 / 0
warnings:       0            analyzer:       0
PEs:     15/15 verdes
hello_input=42 · hello_gl11=42 · hello_sse=7 · hello_gl12=42 · hello_stdio=5
hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_mbwc=42
```

## 2. Auditoria (FASE 1 — sem alterar código)

| Item | Estado encontrado |
|---|---|
| Handlers | `f_GetProcessHeap`, `f_HeapAlloc`, `f_HeapSize`, `f_HeapFree` (pr_win32.c) — os 4 na tabela IMPL (L4290-4293) |
| ABI `GetProcessHeap(void)` | retorna o pseudo-handle fixo `PR_WIN32_H_HEAP = 0xF0F0F002` — nunca NULL ✓ |
| ABI `HeapAlloc(HANDLE, DWORD dwFlags, SIZE_T)` | `LPVOID` = **endereço do guest** (caminho VM: `pr_vm_alloc(R\|W, "w32heap")` → `gaddr`); caminho host (testes): `calloc` + `track_block`; size 0 → 1; handle inválido → `INVALID_HANDLE (6)` + NULL; OOM → `PR_ERR_NOMEM` + NULL ✓ |
| ABI `HeapSize(HANDLE, DWORD, LPCVOID)` | `SIZE_T` = tamanho **exato do pedido** (via `find_gblock`/`find_block`); erro = `(SIZE_T)-1` + `INVALID_PARAMETER (87)` ✓ (contrato real) |
| ABI `HeapFree(HANDLE, DWORD, LPVOID)` | `BOOL` = TRUE; `pr_vm_unmap` + swap-remove do rastreio; erro = FALSE + 87 ✓ |
| Flags (`dwFlags`) | **não interpretadas** — memória sempre zerada (equivalente `HEAP_ZERO_MEMORY` documentado no código); lacuna registrada; o PE usa `flags=0` (caminho essencial) |
| Representação do heap | 1 heap por processo (pseudo-handle único); blocos do guest rastreados em `ctx->gblocks` (endereço + tamanho) |
| Integração com a VM do guest | `pr_vm_alloc`/`pr_vm_unmap` — alocação registrada no mapa de páginas do convidado (R\|W) sob a tag `"w32heap"`; o PE acessa por seu próprio VA |
| `pr_win32_ptr` / isolamento | bloco alcançável só dentro do tamanho alocado (cobertura unitária: 64 bytes OK, 65 bytes rejeitado) |
| Testes existentes | `test_win32.c` (GetProcessHeap/HeapAlloc 64/HeapSize==64/HeapFree + limites) e `pr_win32_lookup("HeapAlloc")` ✓ |

## 3. PE criado

- **Fonte**: `realpe/hello_heap.c` (46 linhas).
- **Binário**: `Tests/PorticoRuntimeTests/data/hello_heap.exe`
  (`x86_64-w64-mingw32-gcc -O2 -s`, 0 warnings; entry `0x13F0`, CUI 5.2).
- **Imports**: `KERNEL32.dll` = **GetProcessHeap, HeapAlloc, HeapSize, HeapFree**
  (só as APIs necessárias) + `msvcrt.dll` (CRT padrão dos `hello_*`).
- **Tamanho alocado**: **64 bytes** (determinístico).
- **Fluxo**: `GetProcessHeap` (handle ≠ NULL) → `HeapAlloc(h, 0, 64)` (ponteiro ≠
  NULL) → escrita do padrão `p[i] = 0xA5 ^ i` em todos os 64 bytes → releitura e
  conferência byte a byte → `HeapSize(h, 0, p) == 64` (rejeitando `(SIZE_T)-1`) →
  `HeapFree(h, 0, p)` == TRUE → `return 42`. A memória é acessada por ponteiro
  `volatile` — a conferência lê de verdade o heap do guest (sem o compilador
  eliminar a verificação). Sem threads/arquivos/GUI/OpenGL/sockets/APIs extras.
- **Contrato de retorno**: 42 = OK; 10 = heap NULL; 11 = alloc NULL; 12 = bytes
  corrompidos; 13 = `HeapSize` erro; 14 = tamanho ≠ 64; 15 = `HeapFree` FALSE.

## 4. Execução (FASE 3 — baseline, runtime intocado)

| Métrica | Valor |
|---|---|
| Resultado | **SUCESSO na primeira execução** (nenhum blocker) |
| `rc` | **42** |
| Instruções | **1366** (`dbg_diag`) · log=31 (`dbg_input`) |
| API da 1ª falha | nenhuma — fluxo completo (`msvcrt!exit → ExitProcess(42)` @0x00FF63CF) |
| Erro Win32 | nenhum |
| Endereço de `HeapAlloc` | VA do guest via `pr_vm_alloc` (tag `"w32heap"`) — não exposto pelas tools; validade comprovada pela escrita/leitura byte a byte (etapa 12 não disparou) |
| Tamanho de `HeapSize` | **64** (etapa 14 não disparou) |
| Resultado de `HeapFree` | **TRUE** (etapa 15 não disparou) |

## 5. Correção (FASE 4)

**"Runtime permaneceu intocado."** Nenhum blocker foi observado; nenhum arquivo
do runtime foi alterado (somente `realpe/hello_heap.c` e o binário do PE foram
criados). `HeapReAlloc`, `VirtualAlloc`, `VirtualProtect`, threads, TLS, sockets,
registry, COM, DirectX, Vulkan, áudio, novas instruções x86 e compatibilidade
com jogos comerciais **não** foram tocados nem antecipados.

## 6. Regressão (FASE 5)

```text
make c-test            → 3392 verificações, 0 falhas
swift test             → Executed 63 tests, with 0 failures
gcc -Wall -Wextra      → 0 warnings
gcc -fanalyzer (build/analyzer34.err) → 0 diagnósticos
PE battery (15 PEs)    → 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
hello_input (sem noinput) → rc=42 log=105
hello_gl11             → rc=42   ✓
hello_sse              → rc=7    ✓
hello_gl12             → rc=42   ✓
hello_stdio            → rc=5    ✓
hello_file             → rc=42   ✓
hello_file_w           → rc=42   ✓
hello_file_seek        → rc=42   ✓
hello_mbwc             → rc=42   ✓
hello_heap             → rc=42   ✓ (NOVO — objetivo do grupo)
```

## 7. Resultado

| Item | Classificação |
|---|---|
| Heap Win32 (`GetProcessHeap`/`HeapAlloc`/`HeapSize`/`HeapFree`, `flags=0`) | **VALIDADO POR PE REAL** · UNIT-TESTADO · IMPLEMENTADO |
| `dwFlags` (`HEAP_ZERO_MEMORY`/`HEAP_NO_SERIALIZE`/`HEAP_GENERATE_EXCEPTIONS`) | NÃO VALIDADAS — não interpretadas (memória sempre zerada); lacuna documentada |
| `HeapReAlloc` | NÃO IMPLEMENTADO (fora do grupo) |

Critérios de aceitação (FASE 6): `GetProcessHeap` ✓ · `HeapAlloc` retornou
memória válida do guest ✓ · escrita ✓ · leitura sem corrupção ✓ · `HeapSize`
compatível (64) ✓ · `HeapFree` ✓ · `rc=42` ✓ · sem corrupção ✓ · regressão
verde ✓.

Sem declaração de compatibilidade geral com aplicações Windows nem com jogos
comerciais.

## 8. Próximo blocker real

**"Nenhum próximo blocker real foi derivado deste grupo."**
