# RELATÓRIO — GRUPO 37: auditoria de Virtual Memory antes da validação por PE

**Desfecho: CONCLUÍDO — auditoria somente-leitura (runtime byte-for-byte
idêntico). Fluxo mínimo escolhido: OPÇÃO A — `VirtualAlloc` + escrita/leitura +
`VirtualFree` (contrato suficientemente definido; PE especificado para o próximo
grupo, NÃO criado neste grupo). Um blocker estático foi registrado em
`VirtualQuery` (layout do `MEMORY_BASIC_INFORMATION`) — não afeta o fluxo A e NÃO
foi corrigido.**

---

## 1. Baseline (FASE 1)

```text
C tests:     3392 / 0        Swift tests:   63 / 0
warnings:       0            analyzer:       0
PE battery: 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
hello_input=42 · hello_gl11=42 · hello_sse=7 · hello_gl12=42 · hello_stdio=5
hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_mbwc=42
hello_heap=42 · hello_qpc=42
```

## 2. Handlers encontrados

| API | Handler | Local |
|---|---|---|
| `VirtualAlloc` | `f_VirtualAlloc` | pr_win32.c:390 |
| `VirtualFree` | `f_VirtualFree` | pr_win32.c:410 |
| `VirtualQuery` | `f_VirtualQuery` | pr_win32.c:878 |
| `VirtualProtect` | `f_VirtualProtect` | pr_win32.c:2263 |

Todos na tabela IMPL (status `PR_WIN32_IMPLEMENTED`). Código lido linha a linha
(não se confiou em comentários/tabela).

## 3. Estado real de `VirtualAlloc`

- **ABI**: `LPVOID VirtualAlloc(LPVOID lpAddress, SIZE_T dwSize, DWORD flAllocationType, DWORD flProtect)` (4 parâmetros).
- **Comportamento real**: com `ctx->vm` (processo PE) → `pr_vm_alloc(vm, size, R|W|X, "w32virt", &gaddr)` e retorna o **VA do guest**; sem VM (testes) → `calloc` + `track_block`.
- **Parâmetros efetivamente lidos**: **só `a[1]` (dwSize)**. `lpAddress`, `flAllocationType` e `flProtect` são **IGNORADOS** (`n < 2` apenas).
- **Retorno**: VA do guest ≠ 0 em sucesso; `NULL` + `INVALID_PARAMETER (87)` se `size == 0`; `NULL` + `PR_ERR_NOMEM` (sem `last_error`) se OOM.
- **Permissões**: região nasce **sempre R|W|X** no guest (qualquer que seja `flProtect`); host: sempre RW.
- Estado: **IMPLEMENTADO (parcial — flags ignoradas) · UNIT-TESTADO (host) · SEM PE REAL**.

## 4. Estado real de `VirtualFree`

- **ABI**: `BOOL VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType)` (3 parâmetros).
- **Comportamento real (guest)**: varre `pr_vm_regions` por uma região com **tag `"w32virt"` e BASE == `lpAddress`** → `pr_vm_unmap(base, size)` → TRUE. Não achou → FALSE + `INVALID_PARAMETER (87)`.
- **Parâmetros efetivamente lidos**: **só `a[0]` (endereço)**. `dwSize` e `dwFreeType` são **IGNORADOS** — o comportamento é sempre `MEM_RELEASE` da região inteira; `MEM_DECOMMIT` não existe.
- **Isolamento**: só desmapeia memória de `VirtualAlloc` — memória de heap (`w32heap`) e do loader (seções) é recusada ✓.
- Estado: **IMPLEMENTADO (parcial) · UNIT-TESTADO (host) · SEM PE REAL**.

## 5. Estado real de `VirtualProtect`

- **ABI**: `BOOL VirtualProtect(LPVOID, SIZE_T, DWORD flNewProtect, PDWORD lpflOldProtect)` (4 parâmetros — os 4 lidos).
- **`lpflOldProtect` = `PDWORD`**: validado e gravado com **4 bytes** ✓ **ABI correta** (não repete o defeito LPDWORD do G30).
- **Guest**: exige que `[addr, addr+size]` caiba **inteiro em uma região** (`pr_vm_regions`); `old = prot_to_page(região)`; aplica `pr_vm_protect` **real** (permissões mudam de fato na VM) → TRUE.
- **Host** (testes): só dentro de blocos `track_block`; `old` fixo `0x04` (PAGE_READWRITE); sem `mprotect` real.
- **`flNewProtect`** → `page_to_prot`: aceita `0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80`; outros valores → **REJEITADO** (`INVALID_PARAMETER`); modificadores altos (`PAGE_GUARD=0x100` etc.) **mascarados** (`p & 0xFF`) = ignorados.
- Estado: **IMPLEMENTADO · UNIT-TESTADO (host, 2×) · SEM PE REAL**.

## 6. Estado real de `VirtualQuery`

- **ABI**: `SIZE_T VirtualQuery(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T)` (3 parâmetros). Requer `ctx->vm` (sem VM → `INVALID_PARAMETER`). Retorna 48 (bytes preenchidos) ✓.
- **Comportamento real**: endereço mapeado → varre páginas de 4 KB (teto do guest = 16 MB) p/ `RegionSize`, State=`MEM_COMMIT (0x1000)`, Type=`MEM_PRIVATE (0x20000)`; endereço livre → State=`MEM_FREE (0x10000)` com `RegionSize` do vão até a próxima região.
- **`Protect` fixo `PAGE_READWRITE (0x04)`** ("conservador") — **não reflete** a proteção real da região.
- **BLOCKER ESTÁTICO (ver §14)**: o empacotamento dos 48 bytes **diverge** do `MEMORY_BASIC_INFORMATION` x64 real.
- Estado: **IMPLEMENTADO (layout divergente) · SEM TESTE UNITÁRIO · SEM PE REAL**.

## 7. Integração com guest VM

Camada real (`pr_vm.h`): `pr_vm_alloc` (gap no espaço de 16 MB + mapa), `pr_vm_map`, `pr_vm_unmap`, `pr_vm_protect`, `pr_vm_read/write`, `pr_vm_loader_write`, `pr_vm_translate`, `pr_vm_regions`; prot = `NONE/R/W/X`.

1. `VirtualAlloc` devolve um VA do guest? **SIM** (`gaddr` de `pr_vm_alloc`).
2. A memória pode ser acessada pelo PE? **SIM** — stores/loads do convidado via `pr_vm_translate` (mesmo caminho provado por `hello_heap`).
3. `VirtualFree` remove o mapeamento? **SIM** (`pr_vm_unmap` da região `w32virt`).
4. `VirtualProtect` altera permissões reais? **SIM** no guest (`pr_vm_protect`); host = sem efeito real (documentado).
5. `VirtualQuery` retorna informações coerentes? **PARCIAL** — base/tamanho/state corretos; `Protect` fixo; **layout da struct divergente** (§14).
6. Diferenças entre famílias de memória: **loader** = seções da imagem (tags de seção, via `pr_vm_map/loader_write`); **heap** = tag `w32heap`, R|W, rastreado em `gblocks` (só `HeapSize/HeapFree` alcançam); **VirtualAlloc** = tag `w32virt`, R|W|X, liberado **só** por `VirtualFree` (casamento exato de base+tag). Três famílias isoladas entre si ✓.

## 8. Flags suportadas

| Flag | Estado |
|---|---|
| `PAGE_NOACCESS (0x01)` | suportada (VirtualProtect → PROT_NONE) |
| `PAGE_READONLY (0x02)` | suportada (→ R) |
| `PAGE_READWRITE (0x04)` | suportada (→ R\|W) |
| `PAGE_WRITECOPY (0x08)` | suportada (→ R\|W) |
| `PAGE_EXECUTE (0x10)` | suportada (→ X) |
| `PAGE_EXECUTE_READ (0x20)` | suportada (→ R\|X) |
| `PAGE_EXECUTE_READWRITE (0x40)` | suportada (→ R\|W\|X) |
| `PAGE_EXECUTE_WRITECOPY (0x80)` | suportada (→ R\|W\|X) |
| `MEM_RELEASE` | suportada por comportamento único do `VirtualFree` (sempre release) |

## 9. Flags não suportadas

| Flag/parâmetro | Estado |
|---|---|
| `MEM_COMMIT` / `MEM_RESERVE` (flAllocationType) | **IGNORADAS** — efeito implícito reserve+commit em uma tacada |
| `MEM_DECOMMIT`, `MEM_RESET`, `MEM_TOP_DOWN`, `MEM_PHYSICAL` | **IGNORADAS/NÃO SUPORTADAS** |
| `flProtect` em `VirtualAlloc` | **IGNORADA** — região nasce sempre R\|W\|X |
| `lpAddress ≠ NULL` em `VirtualAlloc` | **IGNORADO** — endereço sempre escolhido pela VM |
| `dwSize`/`dwFreeType` ≠ `MEM_RELEASE` em `VirtualFree` | **IGNORADOS** |
| `PAGE_GUARD`, `PAGE_NOCACHE`, `PAGE_WRITECOMBINE` (modificadores) | **IGNORADAS** (mascaradas `& 0xFF`) |
| `Protect` real no `VirtualQuery` | **NÃO informado** (fixo RW) |

Nada disso foi implementado nesta fase (regra cumprida).

## 10. Testes existentes

| Teste | Cobertura |
|---|---|
| `test_win32.c:62-70` | `VirtualAlloc(NULL, 128)` + `VirtualFree` — **caminho host** (sem VM): ponteiro válido, acesso 128 bytes, free invalida o acesso ✓ |
| `test_win32x.c:117-135` | `VirtualProtect` sobre bloco `HeapAlloc` (host): `PAGE_EXECUTE_READWRITE` → TRUE, `old = 0x04` em **DWORD 4 bytes**, endereço fora de bloco → FALSE ✓ |
| `VirtualQuery` | **NENHUM teste unitário** |

## 11. Uso indireto por PE, se existir

- `realpe/*.c`: **NENHUM** uso de `Virtual*` (grep vazio).
- `test_pes.c:74`: espécime **sintético** do pipeline de testes chama `VirtualAlloc` + escrita/leitura de padrão (evidência adjacente, **não conta** como PE x64 real pelo critério do G35).
- Loader e CRT não exercitam as APIs `Virtual*` (usam `pr_vm_*`/heap internamente).

## 12. Primeiro fluxo recomendado

**OPÇÃO A — `VirtualAlloc` + escrita/leitura + `VirtualFree`** (a Opção B exige
memória já alocada; a Opção C está barrada pelo layout do MBI — §14). Contrato
suficientemente definido — **especificação exata do PE do próximo grupo** (NÃO
criado aqui):

```text
Fonte: realpe/hello_virt.c  →  Tests/PorticoRuntimeTests/data/hello_virt.exe
Imports: KERNEL32.dll = VirtualAlloc, VirtualFree  (+ CRT msvcrt normal)

Fluxo:
  p = VirtualAlloc(NULL, 4096, MEM_COMMIT|MEM_RESERVE (0x3000),
                   PAGE_READWRITE (0x04))
      10 = p == NULL
  escrever padrão determinístico: p[i] = 0xA5 ^ i  (i = 0..63) via stores do PE
  reler e conferir os 64 bytes (acesso volatile)
      11 = byte corrompido / leitura divergente
  r = VirtualFree(p, 0, MEM_RELEASE (0x8000))
      12 = r == FALSE
  return 42

Observações de contrato (registradas na auditoria):
  - o runtime aceita quaisquer flAllocationType/flProtect (ignoradas) — o PE usa
    os valores REAIS da ABI; espera-se sucesso pelo efeito implícito;
  - lpAddress = NULL é o caso suportado;
  - VirtualFree só libera a base de uma região "w32virt" (comportamento Único =
    MEM_RELEASE da região inteira).
```

## 13. Dependências

| Item | Detalhe |
|---|---|
| APIs necessárias | `VirtualAlloc`, `VirtualFree` (KERNEL32) + CRT |
| Funções internas | `pr_vm_alloc` (tag `w32virt`), `pr_vm_unmap`, `pr_vm_regions` |
| Estruturas | **nenhuma** (o fluxo A não usa MBI) |
| Loader | nenhuma dependência nova (imports padrão) |
| VM do guest | `ctx->vm` presente sob `pr_peproc` ✓ (caminho já provado por `hello_heap`) |
| Acesso direto ao VA | **SIM** — stores/loads normais do PE (mesmo mecanismo validado no G34) |
| Dependência NÃO validada por PE | **nenhuma** |

## 14. Blocker estático, se houver

**Registrado (NÃO corrigido — não impede o PE mínimo do fluxo A):**

- **Arquivo/função**: `Sources/PorticoRuntime/src/pr_win32.c` — `f_VirtualQuery`
  (região L878–906, escritas `mbi[0..5]`).
- **ABI envolvida**: `MEMORY_BASIC_INFORMATION` x64 (48 bytes).
- **Comportamento esperado (Win32 real)**: `AllocationProtect` (DWORD) @16,
  `RegionSize` (SIZE_T) @24, `State` (DWORD) @32, `Protect` (DWORD) @36,
  `Type` (DWORD) @40.
- **Comportamento atual**: 6 × `uint64_t` — `size` @16, `state` @24, `protect`
  @32, `type` @40 — campos deslocados em relação à struct real; um PE real leria
  `RegionSize`/`State`/`Protect` errados.
- **Motivo de não corrigir**: a regra do grupo é não alterar o runtime sem
  blocker real observado por execução; este defeito não bloqueia o fluxo mínimo
  escolhido (A não usa `VirtualQuery`).

Para o fluxo A: **"Auditoria concluída sem blocker estático; próximo passo é
validação por PE real."**

## 15. Regressão (FASE 8)

```text
Integridade: md5(Sources/PorticoRuntime/src+include) =
  4a23bc7bf8bc2d6e0ff55969290bfcd2  (ANTES == DEPOIS — byte-for-byte idêntico)
make c-test            → 3392 verificações, 0 falhas (re-executado pós-auditoria)
swift test             → Executed 63 tests, with 0 failures
gcc -Wall -Wextra      → 0 warnings
gcc -fanalyzer (build/analyzer37.err) → 0 diagnósticos
PE battery (15 PEs)    → 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
hello_input=42 · hello_gl11=42 · hello_sse=7 · hello_gl12=42 · hello_stdio=5
hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_mbwc=42
hello_heap=42 · hello_qpc=42
```

Nenhuma funcionalidade nova foi introduzida.

## 16. Estado final

| API | Classificação |
|---|---|
| `VirtualAlloc` | IMPLEMENTADO (flags ignoradas) · UNIT-TESTADO (host) · **pronto p/ PE real** (fluxo A) |
| `VirtualFree` | IMPLEMENTADO (release único) · UNIT-TESTADO (host) · **pronto p/ PE real** (fluxo A) |
| `VirtualProtect` | IMPLEMENTADO · UNIT-TESTADO (host) · pendente de PE real (fluxo B, futuro) |
| `VirtualQuery` | IMPLEMENTADO com **layout MBI divergente** (blocker estático registrado) · sem testes · fluxo C adiado |

Próximo passo: **criar `hello_virt.exe` conforme a especificação do §12 e
validar o fluxo A por PE x64 real** (grupo seguinte). Nada foi implementado nem
corrigido neste grupo.

Não há declaração de compatibilidade geral com aplicações Windows nem com jogos
comerciais.
