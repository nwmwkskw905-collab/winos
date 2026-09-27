# RELATÓRIO — GRUPO 40: validação real de VirtualQuery e MEMORY_BASIC_INFORMATION x64

```text
STATUS: VirtualQuery + MEMORY_BASIC_INFORMATION validados por PE real — após
        correção do primeiro blocker (layout x64 do MBI). Nenhum claim de
        compatibilidade geral com aplicações Windows ou jogos comerciais.

BASELINE (pré-alteração):
  C tests: 3392 / 0        Swift tests: 63 / 0
  warnings: 0              analyzer: 0 (build/analyzer40.err)
  PE battery: 15/15 (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200; input 105)
  hello_virt=42 (log=64) · hello_virt_protect=42 (log=31) · hello_qpc=42 ·
  hello_heap=42 · hello_mbwc=42 · hello_file=42 · hello_file_w=42 ·
  hello_file_seek=42 · hello_stdio=5 · hello_gl12=42 · hello_sse=7
  MD5 runtime: 1b87770e4e727e6bb69a80c1ce4d57b6
  (= contém a correção do G39 em f_VirtualAlloc — preservada, verificado)

PE criado: realpe/hello_virt_query.c → Tests/PorticoRuntimeTests/data/
  hello_virt_query.exe — x86_64-w64-mingw32-gcc -O2 -s (GCC 14-posix),
  0 erros/0 avisos. Fluxo dos §6–§9 (contrato 10..19 + 42); valida os seis
  campos LENDO a struct MEMORY_BASIC_INFORMATION compilada pelo MinGW x64
  (§12), com memset prévio (campo não escrito pelo runtime = falha visível);
  sizeof compilado conferido == 48 no check 11.

Imports: o código chama explicitamente apenas VirtualAlloc, VirtualQuery,
  VirtualProtect, VirtualFree + CRT (memset). KERNEL32 no binário: o conjunto
  padrão do CRT startup mingw (DeleteCriticalSection, EnterCriticalSection,
  GetLastError, InitializeCriticalSection, LeaveCriticalSection,
  SetUnhandledExceptionFilter, Sleep, TlsGetValue) + os quatro do fluxo.
  Sem CreateThread/WaitForSingleObject/TlsAlloc/ReadFile/WriteFile/
  VirtualQueryEx (§10). msvcrt = CRT padrão.

Primeira execução (runtime exatamente como estava; md5 conferido após):
  VirtualAlloc → VirtualQuery (1ª) → exit(13) após 0.26 ms (exec=341; log=29).
  As demais chamadas não chegaram a rodar.

rc: 13 na primeira execução; 42 após a correção (probe: exec=399; oficial:
  exited=1 rc=42 log=33).

Primeiro blocker: etapa que falhou = check 13 (mbi.AllocationProtect ==
  PAGE_READWRITE). A struct compilada pelo MinGW lê AllocationProtect em @16 e
  recebeu 0x1000 (metade baixa do RegionSize escrito no 2º qword). Causa: o
  handler escrevia o MBI como seis uint64_t sequenciais — a divergência
  estrutural já registrada no Grupo 37. O check 12 passava por coincidência
  (AllocationBase@8 recebia o valor de base); 15/16/17 falhariam em seguida
  (Protect fixo 0x04 agravava); 18 também (Protect não refletia a proteção).

Correção aplicada: f_VirtualQuery passa a escrever o MBI nos offsets REAIS do
  winnt.h x64 (a struct que o PE compila), via memcpy por offset — sem seis
  uint64_t:
    offset 0  BaseAddress        (8)  = base da região
    offset 8  AllocationBase     (8)  = base da alocação (= p)
    offset 16 AllocationProtect  (4)  = prot_to_page(prot real da região)
    offset 20 padding            (4)  = 0
    offset 24 RegionSize         (8)  = tamanho da região
    offset 32 State              (4)  = MEM_COMMIT (0x1000) / MEM_FREE (0x10000)
    offset 36 Protect            (4)  = prot_to_page(prot real da região)
    offset 40 Type               (4)  = MEM_PRIVATE (0x20000) / 0 (livre)
    offset 44 padding            (4)  = 0
  total 48 bytes. Protect/AllocationProtect passam a refletir a proteção real
  (o valor fixo 0x04 não satisfaria os checks 16 e 18). O ramo MEM_FREE usa os
  mesmos offsets.

Nota sobre o layout do briefing (§4): a tabela apresentada omite o BaseAddress
  (coloca AllocationBase@0, RegionSize@16, State@24, Protect@32). O §12 manda
  validar pela struct compilada pelo MinGW x64 — e o winnt.h real tem
  BaseAddress@0 e AllocationBase@8, com AllocationProtect@16, RegionSize@24,
  State@32, Protect@36, Type@40 (exatamente os offsets esperados já registrados
  no Grupo 37 §14). Com os offsets literais da tabela do briefing, os checks de
  campo do §7 não fechariam (AllocationBase leria @8 e receberia
  AllocationProtect). A correção segue a struct real do MinGW — que é o que o
  PE lê e o que diferencia a validação de ABI real (§12).

Alterações no runtime: somente f_VirtualQuery (pr_win32.c, ~L878) + forward
  declaration de prot_to_page. VirtualAlloc (comportamento do G39 preservado —
  md5 pré-patch conferido), VirtualProtect, VirtualFree, File I/O, Heap, QPC,
  Unicode, OpenGL, threads, TLS e demais APIs: INTOCADOS.

Alterações no harness: nenhuma. Probe padrão de observação recompilada junto
  com o dbg_input (evita binário stale, lição do G39).

C tests: 3392 / 0 (pós-alteração; nenhum teste existente alterado ou removido)
Swift tests: 63 / 0
warnings: 0
analyzer: 0 (build/analyzer40b.err)
PE battery: 15/15 verdes — contagens idênticas à baseline
hello_virt: 42 (log=64 — idêntico)
hello_virt_protect: 42 (log=31 — idêntico)
hello_virt_query: 42 (log=33)
MD5 runtime antes/depois:
  1b87770e4e727e6bb69a80c1ce4d57b6  →  957920b285c21590dbc10c3c06283242
  (delta = exatamente o patch documentado acima)
```

## Critérios de aceitação (§14)

| Critério | Resultado |
|---|---|
| `VirtualQuery` chamado pelo PE real | ✓ (2× no log: `kernel32.dll!VirtualQuery`) |
| retorno == 48 | ✓ (check 11: `r == sizeof(mbi) == 48`) |
| `AllocationBase` correto | ✓ (check 12: `== p`) |
| `AllocationProtect` correto | ✓ (check 13: `== PAGE_READWRITE`) |
| `RegionSize` válido | ✓ (check 14: 4096 ≥ 4096) |
| `State == MEM_COMMIT` | ✓ (check 15) |
| `Protect == PAGE_READWRITE` | ✓ (check 16) |
| `Type == MEM_PRIVATE` | ✓ (check 17) |
| após `VirtualProtect`, `Protect == PAGE_READONLY` | ✓ (check 18; fluxo: `VirtualAlloc → Q → P(RO) → Q → P(RW) → VirtualFree`) |
| `VirtualFree` funcionar | ✓ (check 19 não disparou) |
| PE termina com `rc=42` | ✓ |
| regressão verde | ✓ (suite completa) |

## Declaração

**VirtualQuery foi validado por PE real.**

`MEMORY_BASIC_INFORMATION` com 48 bytes e campos nos offsets corretos foi
comprovado lendo a estrutura compilada pelo MinGW x64 (não apenas o valor de
retorno). Limitação conhecida, documentada sem efeito nos critérios deste
grupo: `AllocationProtect` reflete a proteção **corrente** da região (a VM não
guarda a proteção original de alocação; rastreá-la exigiria alterar
`VirtualAlloc`/`VirtualProtect`, vedado neste grupo) — no fluxo validado (antes
de qualquer mudança) é `PAGE_READWRITE`, como no Windows; após um
`VirtualProtect` o Windows real preservaria a proteção original. O
`RegionSize` de regiões contíguas legíveis é calculado por varredura de
páginas (limitação pré-existente, fora do escopo).
