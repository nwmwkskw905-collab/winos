# RELATÓRIO — GRUPO 41: validação real de Critical Section por PE x64

```text
STATUS: Critical Section básica foi validada por PE real — na PRIMEIRA execução,
        com o runtime do Grupo 40 completamente intocado (CASO B). Nenhum claim
        de compatibilidade geral com aplicações Windows.

BASELINE (idêntica ao estado final do G40, conferida por MD5):
  C tests: 3392 / 0        Swift tests: 63 / 0
  warnings: 0              analyzer: 0 (build/analyzer41.err)
  PE battery: 15/15 (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200; input 105)
  hello_virt=42 (log=64) · hello_virt_protect=42 (31) · hello_virt_query=42 (33) ·
  hello_qpc=42 (99) · hello_heap=42 (31) · hello_mbwc=42 (29) · hello_file=42 (36) ·
  hello_file_w=42 (33) · hello_file_seek=42 (33) · hello_stdio=5 (155) ·
  hello_gl12=42 (116) · hello_sse=7 (209)
  MD5 runtime: 957920b285c21590dbc10c3c06283242  (= estado final do G40 ✓)

PE criado: realpe/hello_cs.c → Tests/PorticoRuntimeTests/data/hello_cs.exe —
  x86_64-w64-mingw32-gcc -O2 -s (GCC 14-posix), 0 erros/0 avisos.
  CRITICAL_SECTION local (40 bytes, struct compilada pelo MinGW x64). Fluxo do
  §4 com os códigos do §6: como as APIs de Critical Section são VOID, a
  falha/erro observável = estado de last_error cercado por SetLastError(0) e
  lido por GetLastError (ambos implementados — auditoria L769/L776); dados
  observados nas posições 12 (dentro da seção) e 15 (segunda entrada). Nenhum
  valor de retorno foi inventado.

Imports: código chama apenas InitializeCriticalSection, EnterCriticalSection,
  LeaveCriticalSection, DeleteCriticalSection, SetLastError, GetLastError + CRT.
  KERNEL32 no binário = conjunto padrão do CRT startup (Sleep, TlsGetValue,
  SetUnhandledExceptionFilter, VirtualProtect, VirtualQuery — nunca chamados
  pelo fluxo) + os seis do código. CreateThread, CreateRemoteThread,
  _beginthread, WaitForSingleObject, TlsAlloc e TlsSetValue: ausentes do código
  e do import table (§5/§10). Sem threads.

Primeira execução (runtime do G40 intocado — MD5 conferido após):
  InitializeCriticalSection → Enter → (value = 0x12345678) → Leave → Enter →
  (value conferido) → Leave → DeleteCriticalSection → exit(42) após 0.18 ms
  (exec=418; log=44). Todos os pares SetLastError/GetLastError no log; nenhuma
  API falhou; nenhuma etapa de diagnóstico (10..17) foi disparada.

rc: 42 (primeira execução).

Primeiro blocker: NENHUM.

Correção aplicada: NENHUMA (runtime preservado byte-for-byte).

Alterações no runtime: NENHUMA.
Alterações no harness: NENHUMA (probe padrão de observação).

C tests: 3392 / 0
Swift tests: 63 / 0
warnings: 0
analyzer: 0 (build/analyzer41b.err)
PE battery: 15/15 verdes — contagens idênticas à baseline
hello_cs: 42 (log=44)
hello_virt: 42 (log=64 — idêntico)
hello_virt_protect: 42 (log=31 — idêntico)
hello_virt_query: 42 (log=33 — idêntico)
MD5 runtime antes/depois:
  957920b285c21590dbc10c3c06283242  ==  957920b285c21590dbc10c3c06283242
  (byte-for-byte idêntico)
```

## Auditoria (§2) — achados registrados, NÃO corrigidos (não observados pelo PE)

| Item | Achado |
|---|---|
| ABI dos handlers | `f_InitializeCriticalSection` (pr_win32.c:824), `f_Enter` (835), `f_Leave` (845), `f_Delete` (855) — todos VOID, 1 argumento (ponteiro do objeto) |
| Tamanho/estrutura | tratam o objeto como **6×uint64 (48 B)**; o `CRITICAL_SECTION` real do winnt.h x64 tem **40 B** (`DebugInfo@0, LockCount@8, RecursionCount@12, OwningThread@16, LockSemaphore@24, SpinCount@32`) |
| Escrita além do struct | `Initialize`/`Delete` zeram/validam **48 B — 8 bytes além** dos 40 B reais; campos internos também divergem dos offsets reais (`cs[2]`/"RecursionCount" vive em @16, `cs[3]`/"OwningThread" em @24) |
| Observado pelo PE? | **NÃO** — checks de dados (12/15) passaram, o retorno `42` funcionou; o overrun não teve efeito visível neste fluxo. Regra do grupo (§2) aplicada: **não corrigir**; registrado para etapa futura |
| Validação de ponteiro guest | `pr_win32_ptr(a[0], 48)`; inválido → `last_error = INVALID_PARAMETER` observável via `GetLastError` ✓ |
| Inicialização | zera o objeto = não tomada |
| Enter | incrementa contagem + marca dono (single-thread, nunca bloqueia) |
| Leave | decrementa contagem; zera dono ao liberar |
| Delete | zera o objeto |
| Testes existentes | nenhum teste unitário de Critical Section (grep vazio em Tests/) |

## Critérios de aceitação (§9)

| Critério | Resultado |
|---|---|
| `InitializeCriticalSection` executado pelo PE real | ✓ (no log) |
| primeiro `EnterCriticalSection` funcionar | ✓ (rc≠11) |
| código dentro da seção executar corretamente | ✓ (`value = 0x12345678`; rc≠12) |
| primeiro `LeaveCriticalSection` funcionar | ✓ (rc≠13) |
| segundo `EnterCriticalSection` funcionar | ✓ (rc≠14) |
| dados permanecerem corretos | ✓ (rc≠15) |
| segundo `LeaveCriticalSection` funcionar | ✓ (rc≠16) |
| `DeleteCriticalSection` completar sem falha | ✓ (rc≠17) |
| PE terminar com `rc=42` | ✓ |
| regressão permanecer verde | ✓ (suite completa) |

## Declaração

**Critical Section básica foi validada por PE real.**

Ciclo completo `InitializeCriticalSection → EnterCriticalSection →
LeaveCriticalSection → EnterCriticalSection → LeaveCriticalSection →
DeleteCriticalSection` confirmado em contexto de thread único, sem alteração
nenhuma no runtime. Nenhuma implementação antecipada de `CreateThread`,
`WaitForSingleObject`, `TlsAlloc` ou `TlsSetValue` foi incluída. Não há
declaração de compatibilidade geral com aplicações Windows.
