# RELATÓRIO GRUPO 71 — kernel32!TlsSetValue validada por PE x64 real

## 1. Status
**G71 CONCLUÍDO COM SUCESSO** — ciclo TLS completo pela primeira vez: `TlsAlloc → TlsSetValue → tls_slots[idx] → TlsGetValue → valor recuperado`. Inventário primeiro (regra G69/G70): `TlsSetValue` = **lacuna real** (0 handler/TODO/catálogo — só comentários históricos). Implementação mínima sobre a infraestrutura G70 (`tls_slots` + `tls_used`; sem tabela paralela): `f_TlsSetValue` + 1 linha no catálogo. PE x64 real `hello_tlssetvalue.exe` (14848 bytes, import por nome hint **05c8**): VALUE_A `0x123456789ABCDEF0` (64 bits íntegros, comprovados no disassembly), VALUE_B sobrescreve, NULL aceito sem liberar o índice, inválidos/reservados rejeitados (**6**), slots internos **não corrompidos**, **20/20 rc=71** (log=59); C **3398/0**; Swift **63/63**; warnings 0; analyzer **7**; battery 15/15; **G52–G70 19/19 verdes**; `build/win_fs` inalterado.

## 2. API validada
`kernel32.dll!TlsSetValue(DWORD dwTlsIndex, LPVOID lpTlsValue)` → `BOOL` (`TRUE`/`FALSE`). `TlsFree` = **0 implementações** (fora do escopo §1/§19).

## 3. Inventário read-only (§1)
Buscas completas (`TlsSetValue|f_TlsSetValue|TlsFree|f_TlsFree|TlsAlloc|TlsGetValue|f_Tls*|tls_slots|tls_used|IMPL/TODO kernel32|ERROR_*|LastError`) em Sources/Tests/realpe/tools + catálogo:
**TlsSetValue**: handler **NÃO**; protótipo **NÃO**; catálogo **NÃO** (nem TODO); teste unitário **NÃO**; PE importador **NÃO** (só comentários em `hello_thread.c`/`hello_tlsgetvalue.c`); helper **NÃO**; parcial **NÃO** = **lacuna real confirmada**.
**Família TLS**:

| API | Handler | Catálogo | Teste | PE real | Estado |
|---|---|---|---|---|---|
| TlsAlloc | sim | IMPL (0) | G70 | `hello_tlsalloc` | validada G70 |
| TlsGetValue | sim | IMPL (4) | G55 | `hello_tlsgetvalue` | validada G55 |
| TlsSetValue | **não** | **não** | não | não | **lacuna → G71** |
| TlsFree | não | não | não | não | fora do escopo (§19) |

## 4. Infraestrutura TLS reutilizada (§2/§3)
`ctx->tls_slots[64]` (L222) + `ctx->tls_used` (L223) confirmados; **0..59 alocáveis / 60= lconv, 61= strerror, 62= errno, 63= iob reservados** (L1488/1417/1402/1252) — convenção **não modificada**. `TlsSetValue` grava **diretamente em `ctx->tls_slots[idx]`** (o mesmo armazenamento lido por `f_TlsGetValue` L945) — sem `tls_values`/`tls_map`/`tls_storage`/`tls_table` (§3). `f_TlsGetValue` **intocado** (§20).

## 5. Descoberta sobre TlsSetValue (§4/§5 — contrato)
**Decisão §5 = Opção B (índice alocado)**, justificada: (a) §6 é mandatório — 60..63 **nunca** aceitos (a Opção A literal de 0..63 violaria a proteção dos slots internos do CRT); (b) dentre 0..59, exigir o bit em `tls_used` reusa o estado do G70 sem estrutura nova, mantém o ciclo coerente `TlsAlloc marca → Set escreve → Get lê` e impede escrita em índice não alocado; (c) código de erro = **6** (`W32_ERROR_INVALID_HANDLE`) = **convenção da família TLS existente** (idêntica ao `f_TlsGetValue` G55) — não inventado. `TlsGetValue` permanece leniente (0..63, G55) — divergência interna Get(leniente)/Set(estrito) **documentada**.

| Aspecto | Documentado/Windows (referência) | **Portico (implementado/observado)** |
|---|---|---|
| args | DWORD index; LPVOID value | idem (64 bits íntegros) |
| sucesso | TRUE | TRUE + **LastError preservado** |
| índice fora de capacidade | FALSE + erro | FALSE + **6** |
| índice reservado 60..63 | n/a (conceito Portico) | FALSE + **6** (nunca escreve) |
| índice não alocado | prescrito: índice de TlsAlloc | FALSE + **6** (Opção B) |
| NULL como valor | permitido (é LPVOID) | **permitido** (TRUE); **não** libera o índice |

## 6. Alterações realizadas (mínimas)
1. `pr_win32.c` — `f_TlsSetValue` (novo, ~15 linhas: validação `idx>59 || !tls_used`, escrita `tls_slots[idx]=a[1]`, BOOL).
2. `pr_win32.c` — catálogo: **1 linha nova** `IMPL("kernel32.dll", "TlsSetValue", f_TlsSetValue, 8)` após `TlsAlloc`.
Nada mais: `TlsGetValue`/`TlsAlloc`/`tls_slots`/`tls_used`/reservas CRT **intocados**; `TlsFree` **não** implementado (§19).

## 7. Catálogo
`IMPL(..., 8)` = 2 args × 4 (padrão `lstrcpyA=8`/`GetProcAddress=8`). **1 registro novo**; TODOs **30 → 30** (nenhum removido — a API não tinha TODO); sem duplicação.

## 8. PE criado
`realpe/hello_tlssetvalue.c` → `Tests/PorticoRuntimeTests/data/hello_tlssetvalue.exe` (14848 B). Nenhum PE histórico alterado.

## 9. Compilação
```
x86_64-w64-mingw32-gcc -O2 -s realpe/hello_tlssetvalue.c -o Tests/PorticoRuntimeTests/data/hello_tlssetvalue.exe
```
(Correção durante a compilação: `uint32_t` indisponível via `<windows.h>` → substituído por `DWORD`/`ULONG_PTR` Win32 no PE; nenhum impacto no runtime.)

## 10. Imports (só o necessário)
`KERNEL32.dll` por nome: `TlsAlloc` (05c5/`0x1400081b8`), `TlsSetValue` (hint **05c8**/`0x1400081c8`), `TlsGetValue` (05c7/`0x1400081c0`), `SetLastError` (0554/`0x1400081a0`), `GetLastError` (0283/`0x140008188`) + CRT.

## 11. Execução inicial
**Runtime intacto**: `prepare falhou — EXECUTION STOPPED — Unsupported Win32 API — Module: KERNEL32.dll — Function: TlsSetValue — Technical: API conhecida do módulo mas sem implementação` = 1º blocker real. Depois: `exited=1 rc=71 steps=1 exec=574`.

## 12. Blocker
`Unsupported Win32 API | kernel32.dll | TlsSetValue` = lacuna real (§3); único blocker; resolvido pela implementação mínima §6.

## 13. ABI/disassembly (§15/§16)
```
movabs $0x123456789abcdef0,%rbx      ; *** VALUE_A em 64 bits ***
mov    $0xdead,%ecx                   ; SetLastError(0xDEAD)
call   *%r13                          ; IAT SetLastError (0x1400081a0)
mov    0x5ac9(%rip),%rsi # 0x1400081c8 ; rsi = IAT[TlsSetValue]
mov    %rbx,%rdx                      ; *** RDX = LPVOID value (64 bits intactos) ***
mov    %edi,%ecx                      ; *** RCX = DWORD index ***
call   *%rsi                          ; *** TlsSetValue ***
sub    $0x1,%eax                      ; *** BOOL via EAX: == TRUE(1)? ***
je     ok / mov $0xb,%eax             ; fail 11
call   *%r14                          ; GetLastError
cmp    $0xdead,%edx                   ; *** sucesso preserva? *** (fail 16)
mov    %edi,%ecx
call   *%rbp                          ; TlsGetValue(idx) (0x1400081c0)
mov    %rax,%r15                      ; *** LPVOID retorno via RAX 64-bit ***
cmp    %rbx,%rax                      ; *** comparação 64-bit contra VALUE_A ***
je     ok
mov    $0x9abcdef0,%eax               ; *** ramo anti-truncamento 32-bit ***
cmp    %rax,%r15                      ; (fail 17 se só o DWORD baixo chegou)
```
R9+ não utilizados. `TlsAlloc` também visível: `cmp $0x3b,%edi` (idx>59 → fail 10) e o loop de salvamento dos reservados (`TlsGetValue(60..63)`).

## 14. TlsAlloc + TlsSetValue + TlsGetValue (§18 — critério principal)
Cadeia completa demonstrada pelo PE real: `idx=TlsAlloc()` → `TlsSetValue(idx, VALUE_A)` → `tls_slots[idx]` → `TlsGetValue(idx)==VALUE_A` ✓ (contratos 10/11/17).

## 15. VALUE_A (§8/§10)
Sentinela `0x123456789ABCDEF0` (64 bits com superiores ≠ 0; sem memória inválida): gravada e recuperada **intacta** (`cmp %rbx,%rax`); ramo de truncamento 32-bit (`0x9ABCDEF0`) **não** disparado = **largura 64-bit comprovada** (contrato 17).

## 16. VALUE_B (§11/§21)
`TlsSetValue(idx, 0x0FEDCBA987654321)` → `TlsGetValue == VALUE_B` ✓ (contrato 12) = sobrescrita real do slot; persistência determinística no mesmo processo (A→B).

## 17. NULL (§7/§12)
`TlsSetValue(idx, NULL)` → **`TRUE`** (valor permitido); `TlsGetValue(idx) == NULL`; e **`TlsSetValue(idx, VALUE_A)` de novo == `TRUE`** = o índice **continua alocado** (`tls_used` inalterado — NULL **não** libera) ✓ (contrato 13). Registrado: NULL ≠ "slot livre".

## 18. Índices inválidos (§13)
`0xFFFFFFFF`, `64` e **índice não alocado** (`58/59` ≠ idx) → todos **`FALSE` + `GetLastError()==6`** ✓ (contrato 14) = Opção B exercitada.

## 19. Slots reservados (§6/§14/§19)
`TlsSetValue(60, VALUE_A)` e `TlsSetValue(63, VALUE_A)` → **`FALSE` + 6** ✓ (contrato 15). **Não-corrupção provada sem risco ao CRT**: valores dos slots 60..63 lidos **via API pública** (`TlsGetValue`) antes e depois das tentativas = **idênticos** (save[4] preservado); o valor do slot do PE também permaneceu intacto. O PE nunca escreve diretamente em `ctx->tls_slots` (proteção fechada pela API).

## 20. LastError (§17 — contrato observado do Portico)
- **Sucesso**: **preservado** (`0xDEAD` intacto após `TlsSetValue`) — testado (contrato 16).
- **Falha** (índice inválido/reservado/não alocado): **6** (`W32_ERROR_INVALID_HANDLE`, código da família TLS = mesmo do `TlsGetValue` G55 — não inventado, não copiado do Windows).
- `TlsGetValue` de slot vazio continua **limpando** para 0 (G55, inalterado).

## 21. C (§23)
**3398 verificações, 0 falhas** (nenhum CHECK adicionado — justificado: o ciclo completo é coberto pelo PE x64 real; CHECKs duplicados seriam cobertura artificial, §23).

## 22. Swift (§24)
**Executed 63 tests, with 0 failures** ✓.

## 23. Warnings (§25)
**0** (`-Wall -Wextra`).

## 24. Analyzer (§26)
**7/0** — 7 pré-existentes; **0 novos**.

## 25. PE battery (§27)
**15/15** — logs 28/43/180/70/72/75/98/118/76/81/194/131/55/200/105 (rc=42/5).

## 26. 20 execuções (§28)
**20/20** `exited=1 rc=71 log=59` (determinístico; `steps=1 exec=574`); sem crash, sem `Unsupported`, sem variação; **estável**.

## 27. Checkpoints G52–G71 (§22)
**G52–G70 = 19/19 verdes** (G52=52/31 … G64=64/29, G65=65/35, G66=66/37, G67=67/48, G68=68/55, G69=69/55, G70=70/94) + **G71=71/59** ⇒ **G52–G71 = 20/20 verdes**.

## 28. Checklist acumulada (§31)
**42/42** — item acrescentado: **42. win32 TLS set value (G71 TlsSetValue)** ✓ (41 anteriores inalteradas).

## 29. MD5 (§30)
| Arquivo | antes | depois | o que mudou |
|---|---|---|---|
| `pr_win32.c` | `7b140a4b6bab779800b52358bdcfcf15` (= pós-G70) | `14df6e895abb456eb594b9f20603e82b` | `f_TlsSetValue` + 1 linha de catálogo (§6) |
| Combinado (todos .c/.h) | `d741436a0cd745195f2d47cb8fc40938` | `77f245dbbf0496ffb204888272336d52` | `pr_win32.c` + novo `realpe/hello_tlssetvalue.c` |

**Limpeza (§29)**: o PE **não cria arquivos**; `build/win_fs` permanece com apenas `fixture.txt` pré-existente — **inalterado** ✓.

## 30. Limitações (§32)
1. **TLS por processo** — sem isolamento por thread; sem persistência por thread (todas as threads veem o mesmo `tls_slots`); **múltiplas threads não testadas** (§21).
2. **`TlsFree` ausente** — índices alocados não são liberados/reutilizados no mesmo processo.
3. Capacidade fixa: **60 índices** (0..59); **60..63 reservados** ao CRT (lconv/strerror/errno/iob).
4. `TlsGetValue` continua aceitando 0..63 sem checar alocação (comportamento G55 existente, inalterado) — só `TlsSetValue` é estrito (Opção B).
5. LastError de falha = **6** (convenção da família) — divergências vs Windows declaradas (§5); NULL não-libera = contrato Portico documentado.
6. Sem compatibilidade geral Windows/Winlar/GTA V/MX Bikes/jogos (§33).

## 31. Compatibilidade efetivamente demonstrada (§33)
Somente o que o PE real demonstrou: ciclo completo `TlsAlloc→TlsSetValue→tls_slots[idx]→TlsGetValue` por imports reais resolvidos pelo loader; ABI `RCX=DWORD index`/`RDX=LPVOID 64-bit íntegro`/`EAX=BOOL`; VALUE_A/VALUE_B/NULL; rejeição de inválidos/não alocados/reservados com **6**; não-corrupção dos slots internos (leitura antes/depois); sucesso preserva LastError; 20/20 estável. **Sem** declaração de compatibilidade geral.

## 32. Próximo candidato (somente indicação — NÃO executar; baseado no estado real)
Estado real: TLS = `TlsAlloc`/`TlsSetValue`/`TlsGetValue` validados; **`TlsFree` = lacuna real** (0 handler/catálogo) = fecha o ciclo completo da família (alocar→gravar→ler→**liberar**) — o candidato natural do G72. Demais TODOs reais: `ReadFileEx` (assíncrono — complexidade alta), `MultiByteToWideChar`/`WideCharToMultiByte` (família Unicode adiada). **G72 sugerido: `kernel32!TlsFree`** (2 args ou 1 — verificar no inventário; reutiliza `tls_used`/`tls_slots`).
