# RELATÓRIO — GRUPO 39: validação real de VirtualProtect por PE x64

```text
STATUS: VirtualProtect foi validado por PE real — após correção de UM blocker
        real em f_VirtualAlloc (primeiro blocker observado, §6). Nenhum claim
        de compatibilidade geral com aplicações Windows ou jogos comerciais.

BASELINE (pré-alteração, md5 conferido):
  C tests: 3392 / 0        Swift tests: 63 / 0
  warnings: 0              analyzer: 0 (build/analyzer39.err)
  PE battery: 15/15 (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200; input 105)
  hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_mbwc=42
  hello_heap=42 · hello_qpc=42 (log=96) · hello_virt=42 (log=64)
  hello_gl11=42 · hello_gl12=42 · hello_sse=7 · hello_stdio=5
  MD5 runtime: 4a23bc7bf8bc2d6e0ff55969290bfcd2 (build/g39_before.md5)

PE criado: realpe/hello_virt_protect.c → Tests/PorticoRuntimeTests/data/
  hello_virt_protect.exe — x86_64-w64-mingw32-gcc -O2 -s (GCC 14-posix),
  0 erros/0 avisos. Fluxo exato do §4 (contrato 10..14 + 42); sem escrita em
  página somente-leitura; sem VirtualQuery.

Imports: código = VirtualAlloc, VirtualProtect, VirtualFree (KERNEL32) + CRT.
  KERNEL32 completo no binário: DeleteCriticalSection, EnterCriticalSection,
  GetLastError, InitializeCriticalSection, LeaveCriticalSection,
  SetUnhandledExceptionFilter, Sleep, TlsGetValue, VirtualAlloc, VirtualFree,
  VirtualProtect, VirtualQuery. Os doze últimos (exceto os três do fluxo) são o
  conjunto padrão do CRT startup mingw (idêntico, item a item, ao dos demais
  hello_*). VirtualQuery aparece apenas como import do startup e NUNCA é
  chamado pelo código/fluxo (regra do §5 cumprida). msvcrt = CRT padrão.

Primeira execução (runtime INTOCADO, md5 inalterado):
  VirtualAlloc → VirtualProtect (1ª chamada, TRUE) → exit(11) após 0.16 ms
  (exec=331; log=29). A2ª VirtualProtect e o VirtualFree NÃO chegaram a rodar.

rc: 11 na primeira execução; 42 após a correção (probe: exec=370, log=31;
  oficial com harness novo: exited=1 rc=42 log=31).

API que falhou: nenhuma retornou FALSE. A etapa que falhou foi a VERIFICAÇÃO de
  lpflOldProtect: o DWORD de saída recebeu 0x40 (PAGE_EXECUTE_READWRITE) em vez
  de 0x04 (PAGE_READWRITE). Causa raiz: f_VirtualAlloc (pr_win32.c:390) ignorava
  flProtect e criava toda região "w32virt" com R|W|X fixo; prot_to_page(R|W|X)
  devolve 0x40. Auditoria confirmada no corpo exato de f_VirtualProtect
  (pr_win32.c:2263): old é lido ANTES da mudança (ordem correta) e escrito como
  DWORD de 4 bytes via pr_win32_ptr(..., 4) (ABI x64 correta, não repete o
  defeito LPDWORD do G30).

Correções no runtime: UMA (primeiro blocker real, sem ampliar):
  f_VirtualAlloc passa a converter flProtect (a[3]) com page_to_prot quando
  n >= 4 e o valor é válido; n < 4 ou valor inválido mantém o comportamento
  anterior (R|W|X). Mais: forward declaration de page_to_prot (definida depois
  no arquivo). VirtualQuery/MBI, VirtualProtect, flags adicionais, MEM_DECOMMIT,
  MEM_RESET, PAGE_GUARD/NOCACHE/WRITECOMBINE: INTOCADOS.

Correções no harness: nenhuma alteração de código das tools. Nota operacional:
  após o patch, o binário stale de build/dbg_input (compilado pré-correção)
  chegou a acusar rc=11 e foi descartado; a medição oficial usa harness
  recompilado sobre o runtime corrigido.

C tests: 3392 / 0 (pós-correção; nenhum teste existente alterado ou removido;
  o caso corrigido é exercitado pelo PE real deste grupo)
Swift tests: 63 / 0
warnings: 0
analyzer: 0 (build/analyzer39b.err)
PE battery: 15/15 verdes — contagens idênticas à baseline
  (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200; input 105)
hello_virt: 42 (log=64, idêntico à baseline — validação G38 preservada)
hello_virt_protect: 42 (log=31)
MD5 runtime antes/depois:
  4a23bc7bf8bc2d6e0ff55969290bfcd2  →  1b87770e4e727e6bb69a80c1ce4d57b6
  (delta = exatamente o patch documentado acima)
```

Extras da regressão (§8): hello_file=42 · hello_file_w=42 · hello_file_seek=42 ·
hello_mbwc=42 · hello_heap=42 · hello_qpc=42 · hello_stdio=5 · hello_gl12=42 ·
hello_sse=7 — todos conferidos. Observação honesta: o log do hello_qpc variou
96→98 (+2 `fputc`) porque os valores `%llu` de CLOCK_MONOTONIC ganharam um
dígito ao cruzar 1e12 ns (`t1=1149137278497` agora vs `756144165691` na
baseline) — artefato de largura do printf, sem relação com a correção; rc=42
estável nas duas execuções pós-correção.

## Critérios de aceitação (§7)

| Critério | Resultado |
|---|---|
| `VirtualProtect` chamado pelo PE real | ✓ (2× no log: `kernel32.dll!VirtualProtect`) |
| retornar `TRUE` | ✓ (as duas chamadas; o fluxo chegou ao fim) |
| `lpflOldProtect` = `PAGE_READWRITE` na 1ª troca | ✓ (a 1ª execução provou a checagem: 0x40 → rc=11; pós-correção: 0x04) |
| `lpflOldProtect` = `PAGE_READONLY` na restauração | ✓ (0x02; rc≠12) |
| dados da memória intactos | ✓ (`0x11 0x22 0x33 0x44` preservados; rc≠13) |
| `VirtualFree` retorna `TRUE` | ✓ (rc≠14) |
| PE termina com `rc=42` | ✓ |
| nenhuma regressão | ✓ (suite completa verde) |
| nenhum claim de compatibilidade geral | ✓ |

## Declaração

**VirtualProtect foi validado por PE real.**

Fluxo confirmado por `hello_virt_protect.exe`:
`VirtualProtect → ABI x64 (lpflOldProtect = DWORD 4 bytes) → mudança de proteção
real na VM (R|W → R → R|W) → restauração → preservação da memória`. O primeiro e
único blocker real foi `f_VirtualAlloc` ignorar `flProtect`, corrigido
minimalmente. `VirtualQuery`/MBI permanecem para etapa futura, conforme
determinado.
