# RELATÓRIO — GRUPO 31: validação real de `CreateFileW` por PE x64

**Desfecho: CONCLUÍDO — `hello_file_w.exe → rc=42` (fluxo completo Unicode
aprovado). `CreateFileW` = VALIDADO POR PE REAL (subset BMP; surrogates/não-BMP
recusados honestamente). Primeiro blocker corrigido com patch mínimo (conversão
UTF-16→UTF-8 em `f_CreateFileW`); nenhum novo blocker surgiu.**

---

## 1. Estado inicial (baseline do G30)

```text
C tests:     3392 / 0        Swift tests:   63 / 0
warnings:       0            analyzer:       0
PEs:     15/15 verdes
hello_gl11=42 · hello_sse=7 · hello_gl12=42 · hello_stdio=5 · hello_file=42
```

## 2. Auditoria (FASE 1 — sem alterar código)

| Item | Estado encontrado |
|---|---|
| `f_CreateFileW` (pr_win32.c:1771) | **Implementação parcial real**: lê UTF-16 do guest unit a unit (`pr_win32_ptr(a[0]+i*2, 2)`); conversão UTF-16→ASCII **recusa `>127`** com `INVALID_PARAMETER (87)`; núcleo idêntico a `CreateFileA` (slots ×8, GENERIC_READ/WRITE, dispositions 1–5, fopen binário, erros 2/3/5/80/87) |
| Formato `wchar_t` esperado | **UTF-16 (2 bytes/unidade)** — o PE monta a string com unidades explícitas (`wchar_t` numérico), independente do encoding do fonte |
| Conversão WideChar existente | além desta, só `f_LoadLibraryW` (L1912) converte UTF-16→ASCII p/ **nomes de módulo** — fora do fluxo deste PE (intocada) |
| `pr_win32_ptr` (L224) | validação por região (VM/imagem/scratch/heap) — leitura da string UTF-16 usa 2 bytes/unidade ✓ |
| VFS / resolução | pós-conversão o caminho passa por **`vfs_resolve`** — `fs_root` obrigatório, drive/UNC pulados, `..` recusado — **fs_root segue protegido** ✓ |
| Handles | retorna `W32_FILE_BASE\|(k+1)` = **mesmo tipo** de `CreateFileA` → compatível com `ReadFile`/`WriteFile`/`CloseHandle` |
| Import resolver | tabela IMPL registra `CreateFileW` (L4304) ✓ |
| APIs necessárias ao PE | `WriteFile`, `ReadFile`, `CloseHandle` — todas VALIDADAS POR PE REAL no G29/G30 |
| Testes existentes | `test_vfs.c:170` cobre apenas o **subset ASCII** |

## 3. PE criado

- **Fonte**: `realpe/hello_file_w.c` (**52 linhas**).
- **Binário**: `Tests/PorticoRuntimeTests/data/hello_file_w.exe`
  (`x86_64-w64-mingw32-gcc -O2 -s`, 0 warnings; entry RVA `0x13F0`, CUI 5.2).
- **Imports**: `KERNEL32.dll` = **CreateFileW, WriteFile, ReadFile, CloseHandle**
  (exatamente as APIs do fluxo) + `msvcrt.dll` = só o CRT (retorno de `main` →
  `exit` → `ExitProcess`, mesmo mecanismo dos demais `hello_*`).
- **Fluxo executado**: `CreateFileW(GENERIC_WRITE, CREATE_ALWAYS)` →
  `WriteFile(16 bytes)` → `CloseHandle` → `CreateFileW(GENERIC_READ,
  OPEN_EXISTING)` → `ReadFile` → comparação byte a byte → `CloseHandle` →
  `return 42`. Contrato: 42 = OK; 10..19 = etapa da 1ª falha.
- **Caminho Unicode usado**: `winos_ñ.txt` (ñ = **U+00F1**, fora de ASCII) —
  montado como array `wchar_t` com unidades explícitas
  (`0x0077…0x005F, 0x00F1, 0x002E…`), sem depender do encoding do fonte.
  Conteúdo escrito: `"winos-file-w-g31"` (16 bytes). Sem rede/threads/sockets/
  caminhos absolutos/APIs não implementadas.

## 4. Execução (FASE 3 — baseline, runtime intocado)

| Métrica | Valor |
|---|---|
| `rc` | **10** (etapa `CreateFileW` criar) |
| Instruções | **321** (`dbg_diag`) · log=28 (`dbg_input`) |
| API onde parou | **`CreateFileW`** (1ª chamada) |
| Erro Win32 | `INVALID_PARAMETER (87)` — ramo `*w > 127` (U+00F1) |
| `build/win_fs` | vazio — arquivo **não** criado |

**Primeiro blocker encontrado**: recusa de código não-ASCII na conversão
UTF-16→interna de `f_CreateFileW` (limitação da auditoria, agora comprovada por
execução real).

## 5. Correção (FASE 4)

- **Arquivo alterado**: `Sources/PorticoRuntime/src/pr_win32.c` — **somente o
  loop de conversão de `f_CreateFileW`** (7 linhas → 18 linhas).
- **Causa exata**: `if (*w > 127) return INVALID_PARAMETER` — o subconjunto
  ASCII recusava qualquer caractere fora de 7 bits (comentário original:
  "não-ASCII é recusado, nunca adivinhado").
- **Correção mínima**: conversão **UTF-16 → UTF-8 (BMP)** para o buffer do
  caminho — 1 byte (U+0001..007F), 2 bytes (..07FF), 3 bytes (..FFFF) — com
  índice de unidade separado do índice de saída e truncamento no buffer (mesma
  semântica de `CreateFileA`). **Surrogates (U+D800..DFFF) continuam recusados**
  com `INVALID_PARAMETER` — mantido o princípio "recusar, nunca adivinhar";
  planos suplementares (não-BMP) ficam registrados como lacuna honesta.
- **Nenhuma funcionalidade futura foi antecipada**: `LoadLibraryW`, MB/WC
  (`MultiByteToWideChar`/`WideCharToMultiByte`), `SetFilePointer`, `ReadFileEx`,
  threads, TLS, sockets, registry, COM, DirectX, Vulkan, áudio, instruções x86 e
  compatibilidade com jogos **não** foram tocados. `hello_file.exe`,
  `hello_gl12`, OpenGL, SSE, CPU, loader, imports e o VFS já validado permanecem
  **inalterados** (nenhuma regressão foi observada).

## 6. Testes (FASE 5 — após a correção)

```text
make c-test            → 3392 verificações, 0 falhas   (3392+ ✓)
swift test             → Executed 63 tests, with 0 failures
gcc -Wall -Wextra      → 0 warnings
gcc -fanalyzer (build/analyzer31.err) → 0 diagnósticos
PE battery (15 PEs)    → 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
hello_input (sem noinput) → rc=42 log=105
hello_gl11             → rc=42   ✓
hello_sse              → rc=7    ✓
hello_gl12             → rc=42   ✓
hello_stdio            → rc=5    ✓
hello_file             → rc=42   ✓
hello_file_w           → rc=42   ✓ (NOVO — objetivo do grupo)
```

Evidências de integridade do fluxo (`exec=485`):
`WriteFile` = 16 bytes ✓ (etapa 12), `CloseHandle` ×2 ✓ (13/19), reabertura ✓
(14), `ReadFile` = 16 bytes ✓ (17), comparação byte a byte ✓ (18) — sem
corrupção (out-params `DWORD` ABI-corretos desde o G30). Arquivo criado pelo PE
em `build/win_fs/`:

```text
nome:    77 69 6e 6f 73 5f c3 b1 2e 74 78 74  = "winos_ñ.txt" em UTF-8 (ñ→C3 B1)
conteúdo: 77 69 6e 6f 73 2d 66 69 6c 65 2d 77 2d 67 33 31  = "winos-file-w-g31"
          (16 bytes, hex exato)
```

## 7. Resultado

| Item | Classificação |
|---|---|
| `CreateFileW` (subset BMP: ASCII + Latin/2-3 bytes UTF-8) | **VALIDADO POR PE REAL** · UNIT-TESTADO (subset ASCII) · IMPLEMENTADO |
| `CreateFileW` — surrogates/não-BMP | NÃO IMPLEMENTADO (recusa honesta `INVALID_PARAMETER`, lacuna documentada) |
| `CreateFileW` — `f_LoadLibraryW` (nomes de módulo não-ASCII) | fora do fluxo — estado anterior preservado (subset ASCII) |

Critérios de aceitação (FASE 6): caminho completo ✓ · `CreateFileW` chamado
(imports + arquivo não-ASCII criado) ✓ · arquivo no `fs_root` ✓ · `WriteFile` ✓ ·
`CloseHandle` ✓ · reaberto ✓ · `ReadFile` ✓ · comparação ✓ · `rc=42` ✓ · sem
corrupção ✓ · regressão verde ✓.

Não há declaração de compatibilidade geral com aplicações Windows nem com jogos
comerciais.

## 8. Próximo blocker real

**"Nenhum próximo blocker real foi derivado deste grupo."**
