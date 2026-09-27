# RELATÓRIO — GRUPO 32: validação real de `SetFilePointer` por PE x64

**Desfecho: CONCLUÍDO SEM BLOCKER (CASO B) — `hello_file_seek.exe → rc=42` na
primeira execução, com runtime INTOCADO. `SetFilePointer` = VALIDADO POR PE REAL.
Nenhuma correção foi necessária; nenhuma funcionalidade foi antecipada.**

---

## 1. Baseline (início do grupo)

```text
C tests:     3392 / 0        Swift tests:   63 / 0
warnings:       0            analyzer:       0
PEs:     15/15 verdes
hello_input=42 · hello_gl11=42 · hello_sse=7 · hello_gl12=42
hello_stdio=5 · hello_file=42 · hello_file_w=42
```

## 2. Auditoria (FASE 1 — sem alterar código)

| Item | Estado encontrado |
|---|---|
| Implementação existente | `f_SetFilePointer` (pr_win32.c:1872) — **implementação real completa**: valida handle de slot, `fseek` no `FILE*` e retorna `(uint32_t)ftell(f)` = **nova posição** |
| Assinatura/ABI | `DWORD SetFilePointer(HANDLE a[0], LONG a[1], PLONG a[2], DWORD a[3])` — retorno = posição (low 32) ou `W32_INVALID_SET_FILE_PTR (0xFFFFFFFF)` + `last_error` real (6 `INVALID_HANDLE` / 87 `INVALID_PARAMETER` / 5 `ACCESS_DENIED`) |
| `LONG`/`DWORD` | `dist = (long)(int32_t)(uint32_t)a[1]` — **sinal do `LONG` preservado** ✓; método em `a[3]`: `W32_SEEK_BEGIN/CUR/END` = **0/1/2** = `FILE_BEGIN/CURRENT/END` reais → `SEEK_SET/CUR/END` |
| `lpDistanceToMoveHigh` (a[2]) | lido como `uint64_t*`/8 bytes (assimetria latente vs `PLONG` = 4 da ABI real; diferente de leitura/escrita de volta) — só ativo se ≠NULL. **Limitação registrada, NÃO alterada** — o fluxo do PE usa `NULL` (como os testes unitários); não é blocker observado |
| Posição dos handles | `FILE*` por slot — `fseek`, `fread` (`f_ReadFile`) e `fwrite` (`f_WriteFile`) compartilham a **mesma posição do stream** ✓ (prova unitária: `BEGIN 0` → releitura fiel) |
| Representação de handles | `W32_FILE_BASE (0xC0000000) \| (k+1)` — **os mesmos** de `CreateFileA/W`, `ReadFile`, `WriteFile`, `CloseHandle` ✓ |
| Import resolver | API registrada — `pr_win32_call(ctx, "kernel32.dll", "SetFilePointer", …)` funcional nos testes ✓ |
| VFS / `pr_win32_ptr` | o handler não usa o VFS (opera no `FILE*` já aberto); `pr_win32_ptr` só aparece no out-param `a[2]` (não exercitado) |
| Cobertura unitária | `test_vfs.c`: `FILE_BEGIN 0` → retorno 0 + releitura fiel de 16 bytes; `FILE_END 0` → retorno 16 ✓ (UNIT-TESTADO) |
| Arquivos analisados | `Sources/PorticoRuntime/src/pr_win32.c` (handler + defines L1657-1671 + tabela IMPL), `Tests/PorticoRuntimeTests/test_vfs.c`, `test_peproc.c` |

## 3. PE criado

- **Fonte**: `realpe/hello_file_seek.c` (**48 linhas**).
- **Binário**: `Tests/PorticoRuntimeTests/data/hello_file_seek.exe`
  (`x86_64-w64-mingw32-gcc -O2 -s`, 0 warnings; entry RVA `0x13F0`, CUI 5.2).
- **Imports**: `KERNEL32.dll` = **CreateFileA, WriteFile, SetFilePointer,
  ReadFile, CloseHandle** (exatamente as APIs do fluxo) + `msvcrt.dll` = só o CRT
  (`main → exit → ExitProcess`, padrão dos demais `hello_*`).
- **Fluxo**: `CreateFileA(GENERIC_READ|GENERIC_WRITE, CREATE_ALWAYS)` →
  `WriteFile("AAAABBBBCCCCDDDD", 16)` → `SetFilePointer(h, 8, NULL, FILE_BEGIN)`
  (retorno deve ser 8) → `ReadFile(4)` → comparar com `"CCCC"` → `CloseHandle` →
  `return 42`. Contrato: 42 = OK; 10..17 = etapa da 1ª falha.
- **Offsets utilizados**: escrita de 16 bytes (posição pós-escrita = 16);
  `FILE_BEGIN` com **offset 8**; leitura de 4 bytes = `"CCCC"`. **Prova de
  reposicionamento**: sem o seek o `ReadFile` partiria da posição 16 (EOF →
  0 bytes); na posição 0 leria `"AAAA"`; ler exatamente `"CCCC"` comprova que o
  ponteiro mudou. `FILE_CURRENT`/`FILE_END` **não** foram usados no PE (sem
  blocker relacionado — sem bateria especulativa). Arquivo:
  `winos_seek_test.txt` no `fs_root`. Sem rede/threads/sockets/caminhos
  absolutos/APIs não implementadas.

## 4. Execução (FASE 3 — baseline, runtime intocado)

| Métrica | Valor |
|---|---|
| Resultado | **SUCESSO na primeira execução** (nenhum blocker) |
| `rc` | **42** |
| Instruções | **371** (`dbg_diag`) · log=33 (`dbg_input`) |
| API onde parou | nenhuma — fluxo completo (`msvcrt!exit → ExitProcess(42)` @0x00FF63CF) |
| Erro Win32 | nenhum |
| Posição observada | retorno de `SetFilePointer` = **8** (etapa 13 do contrato passou); `ReadFile` leu **do offset 8** (bytes `"CCCC"`) |
| Conteúdo final do arquivo | `41 41 41 41 42 42 42 42 43 43 43 43 44 44 44 44` = `"AAAABBBBCCCCDDDD"` (16 bytes, hex exato) |

## 5. Correção (FASE 4)

**Não houve blocker — nenhuma correção foi aplicada.** Nenhum arquivo do runtime
foi alterado neste grupo (somente foram criados `realpe/hello_file_seek.c` e o
binário do PE). A assimetria latente de `lpDistanceToMoveHigh` (8 bytes vs
`PLONG` 4) foi **apenas registrada** (item 2) — não é blocker do fluxo validado
e corrigi-la seria antecipação proibida pelo briefing.

## 6. Regressão (FASE 5)

```text
make c-test            → 3392 verificações, 0 falhas
swift test             → Executed 63 tests, with 0 failures
gcc -Wall -Wextra      → 0 warnings
gcc -fanalyzer (build/analyzer32.err) → 0 diagnósticos
PE battery (15 PEs)    → 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
hello_input (sem noinput) → rc=42 log=105
hello_gl11             → rc=42   ✓
hello_sse              → rc=7    ✓
hello_gl12             → rc=42   ✓
hello_stdio            → rc=5    ✓
hello_file             → rc=42   ✓
hello_file_w           → rc=42   ✓
hello_file_seek        → rc=42   ✓ (NOVO — objetivo do grupo)
```

## 7. Resultado

| Item | Classificação |
|---|---|
| `SetFilePointer` (`FILE_BEGIN`; retorno = nova posição) | **VALIDADO POR PE REAL** · UNIT-TESTADO (`FILE_BEGIN`/`FILE_END`) · IMPLEMENTADO |
| `SetFilePointer` (`FILE_CURRENT`/`FILE_END` via PE real) | NÃO VALIDADO por PE real (cobertos por teste unitário; sem blocker que exigisse exercitá-los) |
| `SetFilePointer` (`lpDistanceToMoveHigh` ≠ NULL) | NÃO VALIDADO (assimetria de largura registrada — lacuna honesta) |

Critérios de aceitação (FASE 6): API importada/chamada ✓ · ponteiro alterado ✓ ·
próximo `ReadFile` usou a nova posição ✓ · bytes exatos `"CCCC"` ✓ · `rc=42` ✓ ·
sem corrupção (contagens `DWORD` ABI-corretas; comparação byte a byte passou) ✓ ·
regressão verde ✓.

Sem declaração de compatibilidade geral com aplicações Windows nem com jogos
comerciais.

## 8. Próximo blocker

**"Nenhum próximo blocker real foi derivado deste grupo."**
