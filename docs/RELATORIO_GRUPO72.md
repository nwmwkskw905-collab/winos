# RELATÓRIO GRUPO 72 — kernel32!TlsFree validada por PE x64 real

## 1. Status
**G72 CONCLUÍDO COM SUCESSO** — ciclo TLS **completo e fechado**: `TlsAlloc → TlsSetValue → tls_slots[idx] → TlsGetValue → TlsFree → índice reutilizável`. Inventário primeiro (regra G69/G70/G71): `TlsFree` = **lacuna real** (0 ocorrências em todo o projeto). Implementação mínima sobre a infraestrutura G70/G71 (`tls_used`/`tls_slots`, sem estrutura paralela): `f_TlsFree` + 1 linha no catálogo. PE x64 real `hello_tlsfree.exe` (40448 bytes, import por nome hint **05c6**): libera índice alocado (bit limpo + slot zerado = sem lixo observável), **reutilização provada** (TlsAlloc→TlsFree→TlsAlloc retorna o MESMO índice — first-fit do G70), NULL não libera, double-free/inválidos/não-alocados/reservados rejeitados (**6**), slots internos 60..63 **não corrompidos** (leitura via API pública antes/depois), **20/20 rc=72** (log=274, 3 execuções byte-idênticas); C **3398/0**; Swift **63/63**; warnings 0; analyzer **7/0 novos**; battery 15/15 (fingerprint histórico byte-idêntico); **G52–G72 = 21/21 verdes**; checklist **43/43**; `build/win_fs` = só `fixture.txt`.

## 2. Inventário read-only (§1 — executado ANTES de qualquer alteração)
Buscas completas (`TlsFree|f_TlsFree|TlsAlloc|f_TlsAlloc|TlsSetValue|f_TlsSetValue|TlsGetValue|f_TlsGetValue|tls_slots|tls_used|IMPL(|TODO(|LastError|W32_ERROR_INVALID_HANDLE|W32_ERROR_NOT_ENOUGH_MEMORY`) em `Sources/`, `Tests/`, `realpe/`, `tools/`, catálogo e estruturas:

1. handler `TlsFree`? **NÃO** · 2. protótipo? **NÃO** · 3. catálogo? **NÃO** · 4. TODO? **NÃO** (os 30 TODOs não incluem TLS) · 5. PE importador? **NÃO** · 6. teste unitário? **NÃO** · 7. helper parcial? **NÃO** · 8. infra de liberação de índices? **NÃO existia** — `tls_used` só faz `|=` (alocar) — **`TlsFree` = lacuna real confirmada nos dois sentidos** (0 handler + 0 TODO = padrão G71; nada a duplicar).

9. **`tls_used` (G70)**: bitmap `uint64_t` (L223); `f_TlsAlloc` (L2622–2634) = **busca do primeiro bit livre 0..59** (`for i < 60: if !(tls_used & 1<<i) { set; return i }`); esgotado → `0xFFFFFFFF` + `W32_ERROR_NOT_ENOUGH_MEMORY`(8); init L5272 = bits 60..63 setados.
10. **60–63 protegidos**: inicializados em `pr_win32_create` (L5272) e gravados só pelo CRT (`[63]=iob` L1252, `[62]=errno` L1402, `[61]=strerror` L1417, `[60]=lconv` L1488).
11. **`f_TlsSetValue` (G71)**: `idx > 59 || !(tls_used & 1ull<<idx)` → `FALSE` + 6 (**Opção B** — checagem `>59` ANTES do bit).
12. **`f_TlsGetValue` (G55)**: `idx >= 64` → `FALSE`+6; senão **limpa LastError** e retorna `tls_slots[idx]` (leniente 0..63).
13. **Convenção de erros da família TLS**: falha = **6** (`W32_ERROR_INVALID_HANDLE`, definido em `pr_win32.c` L52; G55 literal `6` L943; G71 usa o mesmo) + `W32_ERROR_NOT_ENOUGH_MEMORY`=8 (L45, só `TlsAlloc` esgotado).

Catálogo TLS pré-G72: `TlsGetValue`/4 (L4954), `TlsAlloc`/0 (L4993), `TlsSetValue`/8 (L4994). **NÃO duplicado**; nenhum registro alterado.

## 3. API validada
`kernel32.dll!TlsFree(DWORD dwTlsIndex)` → `BOOL` (`TRUE`/`FALSE`). Assinatura confirmada por disassembly do PE real (§13): **1 argumento** em `ECX/RCX`, retorno em `EAX`, sem argumentos adicionais; o PE consome apenas `ECX` de entrada.

## 4. Infraestrutura reutilizada (§2)
`ctx->tls_slots[64]` + `ctx->tls_used` = **fonte única**; `f_TlsFree` opera diretamente no bit (`tls_used`) e no slot (`tls_slots[idx]`). **NÃO** criado `tls_values`/`tls_map`/`tls_table`/`tls_storage`/bitmap paralelo/tabela paralela/segundo mecanismo de alocação. 0..59 alocáveis; 60=lconv, 61=strerror, 62=errno, 63=iob **intocados**.

## 5. Contrato/decisões de implementação (§4)
| Caso | Contrato Portico (implementado/observado) | Base |
|---|---|---|
| Índice alocado (bit set) | **`TRUE`**; bit limpo (`tls_used &= ~`) + `tls_slots[idx] = 0`; **LastError preservado** | §4 + §7 (sem lixo observável = invariante) |
| Índice nunca alocado | **`FALSE` + 6** | convenção da família (Opção B/G71) |
| Índice já liberado (double-free) | **`FALSE` + 6** (bit já limpo = caso idêntico ao não alocado) | coerência com o estado atual |
| Reservados 60/61/62/63 | **`FALSE` + 6** — checagem `idx > 59` ANTES do bit (os 4 bits estão SET em `tls_used`; sem ela seriam "liberáveis") | §4/§6 mandatório |
| Fora da capacidade (`64`, `0xFFFFFFFF`) | **`FALSE` + 6** — sem acesso fora de `tls_slots` | §4 |
| Sucesso | LastError **preservado** (não tocado) — **observado**, não inventado; evidência = comportamento natural + família G71 (Set preserva) | §9 |

**Decisão sobre o slot**: limpar `tls_slots[idx] = 0` no free é **necessário** para a invariante §7 ("TlsFree não deixa lixo observável após liberar; reutilização não reintroduz o valor antigo") — sem ele, `TlsGetValue` (G55, leniente) ainda mostraria o valor antigo. **Decisão sobre reutilização**: §6 exige observar; o algoritmo real do G70 = **primeiro bit livre** (documentado no inventário) — para o cenário do PE (alocar A → liberar A → alocar B, sem outras alocações), o algoritmo **garante** `idxB == idxA`; o PE exige essa igualdade como contrato 14 e documenta a origem (não é uma ordem inventada).

## 6. Alterações realizadas (mínimas)
1. `pr_win32.c` — `f_TlsFree` (novo, 15 linhas: validação `idx > 59 || !bit` → `FALSE`+6; senão limpa bit + zera slot + `TRUE`).
2. `pr_win32.c` — catálogo: **1 linha nova** `IMPL("kernel32.dll", "TlsFree", f_TlsFree, 4)` após `TlsSetValue`.
Nada mais: `f_TlsAlloc`/`f_TlsSetValue`/`f_TlsGetValue`/`tls_slots`/`tls_used`/reservas CRT **intocados**.

## 7. Catálogo (§16)
`IMPL(..., 4)` = **1 arg × 4** = padrão do projeto para `DWORD` único (idêntico a `TlsGetValue`, 1 DWORD = 4) — determinado pela assinatura real `TlsFree(DWORD)` confirmada por disassembly (§3), **não** copiado de outra API. Pós-alteração: `f_TlsFree` = **2** ocorrências (handler + catálogo); registros `TlsFree` = **1**; TODOs = **30** (inalterados); **sem duplicação**.

## 8. PE criado
`realpe/hello_tlsfree.c` → `Tests/PorticoRuntimeTests/data/hello_tlsfree.exe` (40448 B). Nenhum PE histórico alterado.

## 9. Compilação
```
x86_64-w64-mingw32-gcc -O2 -s realpe/hello_tlsfree.c -o Tests/PorticoRuntimeTests/data/hello_tlsfree.exe
```
Ajuste documentado durante a compilação/execução: `puts()` → `printf()` (ver §11/§12 — gap real do CRT `msvcrt!puts`); contratos **intocados**.

## 10. Imports (por nome; loader real; sem resolução manual)
`KERNEL32.dll`: `TlsAlloc` (05c5/`0x14000d238`), **`TlsFree` (05c6/`0x14000d240`)**, `TlsGetValue` (05c7/`0x14000d248`), `TlsSetValue` (05c8/`0x14000d250`), `GetLastError` (0283), `SetLastError` (0554) (+ `SetUnhandledExceptionFilter`, `Sleep` do startup MinGW). `msvcrt.dll`: `__iob_func`, `fprintf`, `fwrite`, `vfprintf` (superfície já validada). CRT necessário apenas.

## 11. Execução inicial (§17)
**Runtime intacto (ANTES da implementação)** — parada honesta:
```
prepare falhou: EXECUTION STOPPED — Reason: Unsupported Win32 API
Module: KERNEL32.dll — Function: TlsFree — Architecture: x86-64
Technical: API conhecida do módulo mas sem implementação
```
Depois de implementar `TlsFree`, a execução revelou um **segundo blocker real** (§12): `Unsupported Win32 API | msvcrt.dll | puts` — o gcc emitiu `puts` neste PE (o PE G71 usou `fwrite`); o runtime implementa `printf`/`fwrite`/`vfprintf`, **não** `puts`. Por §14 (fora do escopo expandir msvcrt) a lacuna foi **resolvida no PE** trocando `puts`→`printf` (superfície validada) **sem tocar nos contratos** — a lacuna fica registrada como blocker observado (§32). Em seguida a execução retornou `rc=20`: bug do **teste** (medição de LastError depois de `TlsGetValue`, que limpa LastError por contrato G55 L944); corrigida a ordem da medição (direto após `TlsFree`/`TlsSetValue`), **`rc=72` na primeira execução real completa**.

## 12. Blocker
1. **`Unsupported Win32 API | KERNEL32.dll | TlsFree`** = lacuna real do grupo (§2) — resolvida por `f_TlsFree` (§6).
2. **`Unsupported Win32 API | msvcrt.dll | puts`** = lacuna real do CRT (0 handler; não é do escopo §14) — contornada no PE via `printf` (validada), **não implementada**; candidata a grupo futuro (§34).

## 13. ABI/disassembly (§12 — prova no PE real)
```
140007d10:  b9 ad de 00 00        mov    $0xdead,%ecx        ; SetLastError(0xDEAD)
140007d15:  41 ff d4              call   *%r12               ; IAT SetLastError
140007d18:  4c 8b 3d 21 55 00 00  mov    0x5521(%rip),%r15   # 0x14000d240  ; rsi/r15 = IAT[TlsFree]
140007d1f:  89 f1                 mov    %esi,%ecx           ; *** índice por ECX (DWORD) ***
140007d21:  41 ff d7              call   *%r15               ; *** TlsFree via IAT ***
140007d24:  83 e8 01              sub    $0x1,%eax           ; *** BOOL consumido por EAX ***
140007d27:  74 0a                 je     ok
140007d29:  b8 0c 00 00 00        mov    $0xc,%eax           ; fail 12
140007d33:  41 ff d6              call   *%r14               ; GetLastError
140007d36:  3d ad de 00 00        cmp    $0xdead,%eax        ; LastError preservado (contrato 20)
```
```
140007bd8:  ff 25 62 56 00 00     jmp    *0x5662(%rip)       # 0x14000d240  ; thunk IAT[TlsFree]
140007ca3:  49 bf f0 de bc 9a 78 ... movabs $0x123456789abcdef0,%r15  ; *** VALUE_A em registrador de 64 bits ***
140007cf8:  89 f1                 mov    %esi,%ecx           ; índice (DWORD)
140007cfa:  ff d3                 call   *%rbx               ; TlsGetValue
140007cfc:  48 89 c2              mov    %rax,%rdx           ; *** retorno LPVOID via RAX 64-bit ***
140007cff:  b8 f0 de bc 9a        mov    $0x9abcdef0,%eax    ; *** ramo anti-truncamento 32-bit ***
140007d04:  48 39 c2              cmp    %rax,%rdx
```
Provado: índice por **ECX** (1 arg — justifica `IMPL(...,4)`); retorno BOOL por **EAX**; chamada pela **IAT de `TlsFree`** (`0x14000d240`); BOOL comparado (`sub $0x1,%eax`/`je`); valores de 64 bits em registradores de 64 bits (`%r15`, `%rax`, `cmp %rax,%rdx`) — **sem truncamento** (ramo `0x9abcdef0` não dispara).

## 14. Ciclo completo TlsAlloc + TlsSetValue + TlsGetValue + TlsFree (§14)
Demonstrado pelo PE real: `idx=TlsAlloc()` → `TlsSetValue(idx, VALUE_A)` → `TlsGetValue(idx)==VALUE_A` → **`TlsFree(idx)==TRUE`** ✓ (contratos 10/11/12). Família TLS **inteira** certificada: `TlsAlloc` (G70) + `TlsGetValue` (G55) + `TlsSetValue` (G71) + `TlsFree` (G72).

## 15. Reutilização do índice (§6 — destaque do grupo)
Cenário executado: `idxA=TlsAlloc()` → Set(VALUE_A) → Get==VALUE_A → **`TlsFree(idxA)==TRUE`** → `TlsGetValue(idxA)==NULL` (sem lixo, contrato 13) → `idxB=TlsAlloc()` → **`idxB == idxA`** ✓ (contrato 14 — reutilização real provada; algoritmo = primeiro bit livre do G70, documentado §5) → `TlsGetValue(idxB)==NULL` (**VALUE_A NÃO reaparece**, contrato 15) → Set(VALUE_B) → Get==VALUE_B. Cenário §8 também executado: ciclo posterior (`TlsAlloc` após free com valor gravado) → `Get==NULL` (valores antigos não herdam) → `TlsFree` → TRUE.

## 16. VALUE_A (§7)
`0x123456789ABCDEF0` (bits altos ≠ 0): `movabs` em `%r15` de 64 bits → gravada/recuperada **intacta** (`cmp %rax` 64-bit); ramo anti-truncamento (`0x9ABCDEF0`) **não** disparado (contrato 21) — **largura 64-bit comprovada em Set/Get**.

## 17. VALUE_B (§7)
`0x0FEDCBA987654321` gravado/recuperado no **índice reutilizado** (contrato 15) — sem mistura com VALUE_A; 64 bits íntegros.

## 18. NULL (§8)
No índice reutilizado (contrato 16): `TlsSetValue(idx2, NULL)` → **`TRUE`** (valor permitido); `TlsGetValue(idx2)==NULL`; `TlsSetValue(idx2, VALUE_B)` de novo → **`TRUE`** = o índice **continua alocado** (NULL **não** libera); depois `TlsFree(idx2)` → **`TRUE`** (§11-G: free de índice com NULL segue o contrato normal). Registrado exatamente o observado: NULL é valor, não liberação.

## 19. Índices inválidos (§4/§11-H)
`TlsFree(64)` e `TlsFree(0xFFFFFFFF)` → **`FALSE` + 6** (contrato 18) — sem acesso fora de `tls_slots`. **Índice já liberado** (double-free de `idx2`): `TlsFree(idxB)` de novo → **`FALSE` + 6** (contrato 17) — mesmo tratamento do nunca-alocado (bit limpo), documentado §5.

## 20. Slots reservados (§5/§11-I/J)
`TlsFree(60)` e `TlsFree(63)` → **`FALSE` + 6** (contrato 19) — checagem `idx > 59` impede a liberação mesmo com os bits 60..63 setados em `tls_used`. **Não-corrupção provada sem risco ao CRT**: os 4 valores de `TlsGetValue(60..63)` lidos via **API pública** antes e depois de todas as tentativas = **idênticos** (`save[4]` preservado). O PE nunca acessa `ctx->tls_slots` diretamente.

## 21. LastError (§9 — observado, sem equivalência com Windows)
- **Sucesso**: **preservado** (`0xDEAD` intacto imediatamente após `TlsFree` e após `TlsSetValue`; contratos 20) — medido **sem `TlsGetValue` no meio** (o Get limpa LastError por contrato G55 = comportamento existente preservado).
- **Falha** (double-free/64/`0xFFFFFFFF`/60/63/não alocado): **6** (`W32_ERROR_INVALID_HANDLE`) = **convenção da família TLS** (idêntica a G55/G71) — comportamento do Portico; **não** se afirma equivalência com Windows apenas pelo número.

## 22. C (§18)
**3398 verificações, 0 falhas** (nenhum CHECK adicionado — justificado §18: cobertura real do ciclo é do PE x64; CHECKs duplicados seriam artificiais).

## 23. Swift (§18)
**Executed 63 tests, with 0 failures** ✓ (nenhum `.swift` tocado).

## 24. Warnings (§18)
**0** (`-Wall -Wextra -Werror=implicit-function-declaration`; build MinGW também 0).

## 25. Analyzer (§18)
**7 total / 0 novos / 7 pré-existentes** (pr_cpu.c `core.NonNullParamChecker`; pr_cpu64.c `core.BitwiseShift`; pr_gfx.c `core.NonNullParamChecker`; pr_peproc.c DeadStores `o`; pr_win32.c ×2 DeadStores `r`; pr_winhello.c DeadStores `extra`) — mesma lista do G71 (nomes de linha deslocados por edições acima; categorias idênticas); nada mascarado.

## 26. PE battery (§18)
**15/15 rc=42** com o driver certificado (`dbg_input` com injeção tecla/mouse + `hello_dll.dll`): logs **28/43/180/88/72/75/98/118/76/81/194/131/55/200/105** (hello_real/user/app/gdi/gl/gl2..gl10/input) = **fingerprint histórico byte-idêntico** (G20/G21).

## 27. 20 execuções (§19)
**20/20** `exited=1 rc=72` (log=274 determinístico); **3 execuções byte-idênticas** (md5 da saída `e2466de3…` idêntico); nenhum crash; nenhum `Unsupported`; nenhum resultado variável; **win_fs verificado após as execuções** (§31) — sem crescimento inesperado.

## 28. Checkpoints G52–G72 (§20)
**21/21 verdes** após a implementação: G52=52/31, G53=53/38, G54=54/30, G55=55/36, G56=56/32, G57=57/33, G58=58/39, G59=59/40, G60=60/45, G61=61/33, G62=62/33, G63=63/33, G64=64/29, G65=65/35, G66=66/37, G67=67/48, G68=68/55, G69=69/55, G70=70/94, G71=71/59, **G72=72/274**. Nenhum grupo anterior regrediu.

## 29. Checklist acumulada (§21)
**43/43** — item acrescentado: **43. win32 TLS free value (G72 TlsFree)** ✓ (42 anteriores inalteradas).

## 30. MD5 (§22)
| Arquivo | antes | depois | o que mudou |
|---|---|---|---|
| `pr_win32.c` | `14df6e895abb456eb594b9f20603e82b` (= pós-G71, reverso exato confirmado) | `eb41583c355dd665ad189cc70dcca6f7` | `f_TlsFree` + 1 linha de catálogo (§6) |
| Combinado (todos .c/.h) | `77f245dbbf0496ffb204888272336d52` | `cfa85db99488c278ea886979870db5ad` | `pr_win32.c` + novo `realpe/hello_tlsfree.c` |

## 31. Limpeza do filesystem (§23)
`hello_tlsfree` **não cria arquivos** (fonte sem nenhuma API de arquivo). Durante a verificação, `build/win_fs` recebeu `winos_file_test.txt`/`winos_seek_test.txt`/`winos_ñ.txt` — **não do PE do G72**, e sim de execuções exploratórias dos PEs de arquivo (`hello_file`/`hello_file_seek`/`hello_file_w` — **fora** da bateria e dos checkpoints; esses PEs deixam seus fixtures por design). Os resíduos foram **removidos deterministicamente**; estado final = **`build/win_fs` = só `fixture.txt`** = equivalente ao estado anterior ao G72 (verificado após as execuções §27). `hello_tlsfree.exe` permanece em `Tests/PorticoRuntimeTests/data/` (produto do grupo, como todos os PEs).

## 32. Limitações (§25 — continuam explícitas)
1. **TLS por processo** — não é TLS thread-local; sem isolamento/suporte a múltiplas threads (PE não testa threads — limitação registrada §15).
2. **`TlsFree` ≠ Windows em todos os detalhes** — contrato Portico documentado (§5): rejeição de não-alocado/já-liberado com 6, slot zerado no free, last_error 6 da família; sem equivalência geral declarada.
3. Capacidade fixa: **60 índices** (0..59); **60..63 reservados** ao CRT.
4. `TlsGetValue` continua leniente 0..63 (G55, intocado §13) — só `TlsSetValue`/`TlsFree` são estritos (Opção B).
5. Divergências LastError vs Windows documentadas (§21).
6. **`msvcrt!puts` ausente** (gap observado por PE real — §12) — não implementado (fora de escopo §14).
7. **Sem FLS** (`FlsAlloc`/`FlsSetValue`/`FlsGetValue`/`FlsFree` = fora do escopo §14).
8. Sem compatibilidade com Winlar/GTA V/MX Bikes/jogos comerciais/Windows em geral.

## 33. Compatibilidade efetivamente demonstrada (§25)
Somente o que o PE real demonstrou: ciclo completo `TlsAlloc→TlsSetValue→TlsGetValue→TlsFree` por imports reais resolvidos pelo loader; ABI `RCX=DWORD`/`EAX=BOOL`/IAT; **reutilização de índice** (first-fit observado); liberação sem lixo observável; VALUE_A/VALUE_B 64-bit íntegros; NULL = valor que não libera; double-free/não-alocado/inválidos/reservados rejeitados com **6** sem tocar 60..63 (não-corrupção por leitura pública antes/depois); sucesso preserva LastError; 20/20 estável. **Sem** declaração de compatibilidade geral.

## 34. Próximo candidato (somente indicação — NÃO executar G73; baseado no estado real pós-G72)
Família TLS **completa** (Alloc/Set/Get/Free certificados). Lacunas reais restantes, em ordem de sinal: **(1) `msvcrt!puts`** — gap observado por PE real neste próprio grupo (2º blocker §12; superfície mínima, mesma família de `printf`/`fwrite` já validada); **(2) `kernel32!ReadFileEx`** (assíncrono — complexidade alta; TODO real); **(3) `MultiByteToWideChar`/`WideCharToMultiByte`** (família Unicode adiada; `f_MultiByteToWideChar` parcial pré-existente — inventário obrigatório). **G73 sugerido: `msvcrt!puts`** (a menor lacuna real hoje observada por PE).
