# RELATÓRIO GRUPO 70 — kernel32!TlsAlloc validada por PE x64 real

## 1. Status
**G70 CONCLUÍDO COM SUCESSO** — inventário completo primeiro (regra do G69): `TlsAlloc` era **lacuna REAL** (diferente do G69: este TODO não era duplicata morta — 0 handler, 0 catálogo IMPL, 0 teste, 0 PE). Implementação mínima sobre a arquitetura existente `ctx->tls_slots[64]` (sem tabela paralela): bitmap `tls_used` (1 campo) + `f_TlsAlloc` + 1 linha de catálogo. PE x64 real `hello_tlsalloc.exe` (14336 bytes, import por nome hint **05c5**): índice válido 0..59, **nunca** os reservados 60..63, integração com `TlsGetValue` (G55), segunda alocação distinta, **exaustão determinística = 60 slots** + `0xFFFFFFFF`/`8`; **20/20 rc=70** (log=94); C **3398/0**; Swift **63/63**; warnings 0; analyzer **7**; battery 15/15; **G52–G69 18/18 verdes** (G55 revalidado).

## 2. API validada
`kernel32.dll!TlsAlloc(void)` → `DWORD` (índice TLS ou `0xFFFFFFFF` = `TLS_OUT_OF_INDEXES`). `TlsSetValue`/`TlsFree` = **0 implementações** (§8/§9: não necessários para o contrato testado — não implementados).

## 3. Inventário read-only antes da alteração (§1)
Buscas completas (`TlsAlloc|TlsGetValue|TlsSetValue|TlsFree|f_Tls*|tls_slots|TLS|Tls|IMPL/TODO kernel32|last_error|ERROR_*|DWORD|pthread|thread`) em Sources/Tests/realpe/tools + catálogo:
**1.1 TlsAlloc**: handler **NÃO**; protótipo **NÃO**; registro = só `TODO("kernel32.dll","TlsAlloc","TLS do convidado")` **L4963** (sem IMPL paralelo = lacuna real, não duplicata morta); teste unitário **NÃO**; PE utilizador **NÃO** (só comentário em `hello_thread.c`); implementação parcial **NÃO**; helper de alocação **NÃO**.
**1.2 Família TLS**:

| API | Handler | Catálogo | Teste | PE real | Estado |
|---|---|---|---|---|---|
| TlsAlloc | NÃO | TODO L4963 | NÃO | NÃO | **lacuna real → G70** |
| TlsGetValue | `f_TlsGetValue` L938 | IMPL L4924 (4) | unitário? não; G55 | `hello_tlsgetvalue` rc=55 | validada G55 |
| TlsSetValue | NÃO | NÃO | NÃO | NÃO | ausente (não tocada) |
| TlsFree | NÃO | NÃO | NÃO | NÃO | ausente (não tocada) |

## 4. Estado da infraestrutura TLS existente (§2 — 9 respostas)
1. **Capacidade**: `ctx->tls_slots[64]` (L222), índices 0..63. 2. **Reservados (4 — correção da memória G55, que listava 3)**: **[60]=lconv** (`f_msvcrt_localeconv` L1487), [61]=strerror pool (L1416), [62]=errno (L1401), [63]=iob (L1251) → **livres = 0..59 (60 slots)**. 3. **Contador de livres**: NÃO. 4. **Bitmap**: NÃO (criado em G70 — §6). 5. **Estado de alocação**: NÃO existia. 6. **Isolamento por thread**: NÃO — `ctx` é por-processo (slots compartilhados por todas as threads). 7. **`f_TlsGetValue`** (L938–944): `idx>=64` → 0 + `last_error=6`; senão `last_error=0` (**limpa LastError**) e retorna `tls_slots[idx]` — **não distingue slot livre de alocado** (documentado; §14: não alterado para embelezar teste). 8. **Inicialização**: `pr_win32_create` = `calloc` → slots zerados (vazio = NULL = contrato G55). 9. **Reutilizável**: o próprio `tls_slots` + o padrão de reserva por convenção interna.

## 5. Descoberta sobre TlsAlloc (§5)
**Lacuna real comprovada** (inventário §3): o TODO L4963 **não** tinha IMPL paralelo (diferente de `SetFilePointer`/`CloseHandle` do G69/G68); a execução com runtime intacto confirmou (`Unsupported Win32 API | TlsAlloc`). Necessidade do estado de alocação (§6): **comprovada** — sem marcar o slot como alocado seria impossível (a) garantir 2ª alocação distinta (§12.9), (b) esgotamento determinístico (§15), (c) nunca devolver os reservados depois de 59 livres consumidos. Solução = **`uint64_t tls_used`** (bitmap de 64 bits, 1 campo no ctx opaco) — a menor estrutura possível; nada paralelo aos `tls_slots`.

## 6. Arquitetura reutilizada (§3)
`ctx->tls_slots[64]` preservado como **infraestrutura oficial única** (sem `tls_slots_2`/`tls_alloc_table`/`tls_map`/`tls_context`); `f_TlsGetValue` intocado; reservas internas do CRT intocadas (apenas marcadas no bitmap); `pr_win32_create` estendido em 2 linhas.

## 7. Alterações realizadas (mínimas, justificadas em §5)
1. `pr_win32.c` — campo `uint64_t tls_used` junto a `tls_slots[64]` (1 linha).
2. `pr_win32.c` — `pr_win32_create`: init `tls_used = bits 60..63` (2 linhas).
3. `pr_win32.c` — `f_TlsAlloc` (novo, ~14 linhas: scan bits 0..59, seta, retorna; esgotado → 8 + `0xFFFFFFFF`).
4. `pr_win32.c` — catálogo: TODO L4963 → `IMPL(..., 0)` (1 linha). `TlsSetValue`/`TlsFree` **não** implementados (§8/§9).

## 8. Catálogo / registro
`IMPL("kernel32.dll", "TlsAlloc", f_TlsAlloc, 0)` — **0 argumentos → stdcall_bytes 0** (padrão `GetCurrentProcessId`/`IsDebuggerPresent`). TODO relacionado removido por substituição (esperado); TODOs: 31 → **30** (nenhum não relacionado removido); sem duplicação.

## 9. PE criado
`realpe/hello_tlsalloc.c` → `Tests/PorticoRuntimeTests/data/hello_tlsalloc.exe` (14336 B). Import direto por nome. Nenhum PE histórico alterado.

## 10. Compilação
```
x86_64-w64-mingw32-gcc -O2 -s realpe/hello_tlsalloc.c -o Tests/PorticoRuntimeTests/data/hello_tlsalloc.exe
```

## 11. Imports (§11 — só o necessário)
`KERNEL32.dll` por nome: `TlsAlloc` (hint **05c5**, IAT `0x1400081b0`), `TlsGetValue` (05c7/`0x1400081b8`), `SetLastError` (0554/`0x140008198`), `GetLastError` (0283/`0x140008180`) + CRT (`msvcrt.dll`). Nenhuma outra API nova.

## 12. Execução inicial
**Runtime intacto**: `EXECUTION STOPPED — Unsupported Win32 API — Module: kernel32.dll — Function: TlsAlloc — Technical: TLS do convidado` (steps=200000) = 1º blocker real. Depois da implementação: `exited=1 rc=70 steps=1 exec=1015` (inclui o loop de exaustão).

## 13. Blocker encontrado
`Unsupported Win32 API | kernel32.dll | TlsAlloc` = **lacuna real do catálogo** (§5); único blocker; resolvido pela implementação mínima §7.

## 14. ABI/disassembly (§13)
```
mov    $0xdead,%ecx                            ; SetLastError(0xDEAD)
mov    0x5aff(%rip),%rsi # 0x140008198         ; rsi = IAT[SetLastError]
call   *%rsi
mov    0x5b0e(%rip),%rdi # 0x1400081b0         ; rdi = IAT[TlsAlloc]
call   *%rdi                    ; *** TlsAlloc: SEM argumentos ***
mov    %eax,%ebx                ; *** retorno consumido como DWORD (EAX) ***
cmp    $0xffffffff,%eax         ; *** TLS_OUT_OF_INDEXES? ***
je     <fail 10>
lea    -0x3c(%rax),%eax         ; *** idx−60: detecta slots reservados (0x3c=60) ***
cmp    $0x3,%eax                ; idx em 60..63? → fail 12
jbe    <fail 12>
mov    0x5ab3(%rip),%rbp # 0x140008180         ; rbp = IAT[GetLastError]
call   *%rbp
cmp    $0xdead,%edx             ; *** LastError preservado em sucesso? *** (fail 15)
mov    %ebx,%ecx                ; *** RCX = índice (DWORD zero-ext) ***
call   *0x5acb(%rip) # 0x1400081b8             ; *** TlsGetValue(idx) ***
test   %rax,%rax                ; *** v == NULL? *** (fail 13)
...
call   *%rdi                    ; TlsAlloc (2ª)
cmp    $0xffffffff,%eax / cmp %eax,%ebx        ; distinta da 1ª?
```
**Consumidor trata o retorno como `DWORD`** (comparações em EAX/EBX com `0xffffffff`, aritmética de 32 bits) — confirmado por desmontagem, não inferido do C.

## 15. TlsAlloc principal (§4/§12 — comportamento efetivamente implementado)
| Aspecto | Documentado/Windows (referência) | **Portico (implementado/observado)** |
|---|---|---|
| retorno | índice TLS ou `TLS_OUT_OF_INDEXES` (0xFFFFFFFF) | idem |
| faixa | 0..TLS_MINIMUM_AVAILABLE−1 (64+) | **0..59** (60 slots) |
| validação | índice usável por TlsGetValue/SetValue | idem (Get validado; Set ausente) |
| determinismo | — | 1º livre em ordem crescente (teste só exige distinção/bounds) |
| esgotamento | 0xFFFFFFFF + GetLastError | `0xFFFFFFFF` + **8** (`W32_ERROR_NOT_ENOUGH_MEMORY`, constante existente) |
| escopo | **por thread** | **por processo** (divergência — §30) |

## 16. Integração com TlsGetValue (§7/§14)
`idx = TlsAlloc(); v = TlsGetValue(idx)`: índice **aceito** (dentro de 0..63 do handler G55) → `v == NULL` (**valor inicial = vazio**, herdado do `calloc` — estado real, não inventado) + `GetLastError() == 0` (**contrato G55 observado**: sucesso limpa LastError) ✓. `TlsGetValue` **não** foi alterado — ele continua sem distinguir slot livre de alocado (documentado em §30).

## 17. Slots reservados (§5)
`TlsAlloc` retorna **somente 0..59**; os índices **60 (lconv), 61 (strerror), 62 (errno), 63 (iob)** nunca são devolvidos (bitmap pré-marcado; PE verifica `idx>=60` → contrato 12) — reservas internas existentes **inalteradas**.

## 18. Segunda alocação (§12.9/§18)
`TlsAlloc()` duas vezes → índices **distintos** (contrato 14 = iguais seria falha) — propriedade garantida pelo estado de alocação (§5). Ordem/consecutividade **não** assumida (não é garantia testada).

## 19. Exaustão (§15 — testada, determinística)
Loop limitado (70 tentativas): **60 alocações bem-sucedidas** (2 + 58) e a seguinte = `0xFFFFFFFF` + `GetLastError() == 8` (contrato 16). Nenhum índice ≥ 60 devolvido durante a exaustão. Teste determinístico (capacidade fixa da arquitetura) — sem virar teste de estresse.

## 20. LastError (§16 — contrato observado do Portico)
- **Sucesso de `TlsAlloc`**: **preservado** (`0xDEAD` intacto) — testado (contrato 15).
- **`TlsGetValue` de slot vazio**: **limpa** para 0 (contrato G55, retestado §16).
- **Esgotamento**: **8** (`W32_ERROR_NOT_ENOUGH_MEMORY`, constante já existente no runtime) — definido como contrato implementado; não alegado equivalente ao Windows (que não fixa o código documentalmente).

## 21. C (§20)
**3398 verificações, 0 falhas** (nenhum CHECK adicionado — justificado: o caminho real `TlsAlloc` é coberto pelo PE x64 real, incluindo exaustão; testes unitários de TLS não existiam e criar paralelos seria infra duplicada; registrado como decisão).

## 22. Swift (§20)
**Executed 63 tests, with 0 failures** ✓ (intocados).

## 23. Warnings (§20)
**0** (`-Wall -Wextra`).

## 24. Analyzer (§21)
**7/0** — 7 pré-existentes (pr_cpu.c:126, pr_cpu64.c:401, pr_gfx.c:98, pr_peproc.c:831, pr_win32.c:3611/3615, pr_hello.c:885); **0 novos** (bitmap/handler sem alertas).

## 25. PE battery (§22)
**15/15** — logs 28/43/180/70/72/75/98/118/76/81/194/131/55/200/105 (rc=42/5).

## 26. 20 execuções (§23)
**20/20** `exited=1 rc=70 log=94` (determinístico; `steps=1 exec=1015`); sem crash, sem `Unsupported`, sem variação; estabilidade total.

## 27. Checkpoints G52–G70 (§25)
**G52–G69 = 18/18 verdes** (G52=52/31, G53=53/38, G54=54/30, **G55=55/36 revalidado §17**, G56=56/32, G57=57/33, G58=58/39, G59=59/40, G60=60/45, G61=61/33, G62=62/33, G63=63/33, G64=64/29, G65=65/35, G66=66/37, G67=67/48, G68=68/55, G69=69/55) + **G70=70/94** ⇒ **G52–G70 = 19/19 verdes**.

## 28. Checklist acumulada (§26)
**41/41** — item acrescentado: **41. win32 TLS alloc (G70 TlsAlloc)** ✓ (40 anteriores inalteradas).

## 29. MD5 (§19)
| Arquivo | antes | depois | motivo |
|---|---|---|---|
| `pr_win32.c` | `27152efdfbf69ee7c111b71c9d9ab037` (= pós-G69) | `7b140a4b6bab779800b52358bdcfcf15` | §7.1–4: bitmap + init + `f_TlsAlloc` + catálogo |
| Combinado (todos .c/.h) | `02818b338d9f811c520cec1fb6dc25c3` | `d741436a0cd745195f2d47cb8fc40938` | `pr_win32.c` + novo `realpe/hello_tlsalloc.c` |

**Limpeza (§24)**: o PE **não cria arquivos** — `build/win_fs` permanece exatamente igual (informado explicitamente; nenhum resíduo).

## 30. Limitações (§27)
1. **Capacidade: 60 slots alocáveis** (0..59); 60..63 reservados ao CRT interno (lconv/strerror/errno/iob) — não configuráveis.
2. **Sem isolamento por thread**: os slots são **por processo** (compartilhados) — Windows TLS é per-thread; persistência por thread = **não suportada**; múltiplas threads veem o mesmo slot.
3. **`TlsSetValue` ausente**: slots permanecem NULL sem gravação (o valor inicial é o único demonstrado).
4. **`TlsFree` ausente**: índices alocados não são liberados (sem realocação no mesmo processo).
5. **Esgotamento**: `0xFFFFFFFF` + `8` (contrato implementado); sem expansão dinâmica (Windows tem expansão além de 64).
6. `TlsGetValue` não distingue slot livre/alocado (aceita 0..63) — G55, inalterado.
7. Sem equivalência Windows declarada; não é TLS completo.

## 31. Compatibilidade efetivamente demonstrada (§28)
Somente o que o PE real demonstrou: `TlsAlloc` por import real resolvido pelo loader, ABI x64 sem argumentos com retorno DWORD consumido em EAX, faixa 0..59 com exclusão dos reservados 60..63, aceitação por `TlsGetValue` com valor inicial NULL e limpeza de LastError (contrato G55), segunda alocação distinta, exaustão determinística (60) com `0xFFFFFFFF`+8, LastError preservado em sucesso, 20/20 estável. **Sem** declaração de compatibilidade Windows/Winlar/GTA V/MX Bikes/jogos comerciais.

## 32. Próximo candidato (somente indicação — NÃO executar; baseado no estado real)
Estado real pós-G70: TLS = `TlsAlloc`+`TlsGetValue` validados; **`TlsSetValue` = lacuna real** (0 handler/catálogo) — sem ele os slots nunca saem de NULL e o valor do TLS não é utilizável; `TlsFree` = lacuna real (ciclo completo). TODOs restantes: `ReadFileEx` (assíncrono — complexidade alta), `MultiByteToWideChar`/`WideCharToMultiByte` (família Unicode adiada), duplicatas mortas (`SetFilePointer`, `CloseHandle`). **G71 sugerido: `kernel32!TlsSetValue`** (2 args DWORD+LPVOID; completa o ciclo útil TLS com as duas APIs já validadas; `TlsFree` como continuação natural). Alternativa: `kernel32!TlsFree`.
