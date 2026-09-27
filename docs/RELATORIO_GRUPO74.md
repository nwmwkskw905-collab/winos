# RELATÓRIO GRUPO 74 — Família Unicode / MultiByteToWideChar

## 1. Status
**G74 CONCLUÍDO — INVENTÁRIO + VALIDAÇÃO + UMA CORREÇÃO MÍNIMA (Situação B)**. Inventário read-only primeiro: **as DUAS APIs já existiam** (`f_MultiByteToWideChar` L1589 `IMPL/24`; `f_WideCharToMultiByte` L1663 `IMPL/32`) — a suposição do G73 ("só MB2WC parcial") estava **superestimada**; não se criou segunda implementação nem se reescreveu nada. O PE de diagnóstico `hello_unicode_probe.exe` executou **direto no runtime intacto** (sem `Unsupported`) e **demonstrou UMA lacuna pequena**: `cchWideChar == 0` com `lpWideCharStr != NULL` devolvia 0 (validava buffer de 0 bytes) em vez do **modo consulta** do contrato Windows. Corrigido **somente esse bloco** (Situação B). Resultados: ASCII/2-byte/3-byte UTF-8 (Olá, €=U+20AC) corretos; `-1` NUL-terminado **com** NUL contado; tamanho positivo **sem** NUL; consulta (NULL,0) e cap 0 = need; buffer insuficiente = **trunca e devolve o escrito** (divergência Windows registrada); não-BMP (U+1F600) = consulta devolve **1** (Windows: 2, par surrogate) e a **conversão recusa** com EXECUTION STOPPED honesto = **limitação, sem expansão** (§5-D); CP_ACP(0)=CP1252; flags aceitas e **ignoradas**. **20/20 rc=74** (log=742 idênticos); C **3398/0**; Swift **63/63**; warnings 0; analyzer **7/0 novos**; battery 15/15 (fingerprints byte-idênticos); `hello_mbwc` (PE Unicode histórico G33) **42/29 preservado**; **G52–G74 = 23/23 verdes**; checklist **45/45**; `build/win_fs` = só `fixture.txt`.

## 2. Inventário read-only (§1 — ANTES de qualquer alteração)
Buscas (`MultiByteToWideChar|f_MultiByteToWideChar|WideCharToMultiByte|f_WideCharToMultiByte|CP_ACP|CP_UTF8|MB_|WC_|WideChar|MultiByte|UTF-8|UTF-16|wchar|unicode`) em `Sources/`, `Tests/`, `realpe/`, `tools/` + catálogo. Respostas às 15 perguntas:

1. `f_MultiByteToWideChar`? **SIM** · 2. Onde? `pr_win32.c` **L1589–1660** · 3. Implementação atual? completa na §4 · 4. Catálogo? **SIM** `IMPL("kernel32.dll","MultiByteToWideChar",f_MultiByteToWideChar,24)` (L5009) · 5. Assinatura registrada: **24 bytes = 6 args × 4** (conforme Win64: 4 regs + 2 stack, cada DWORD/ponteiro 4 no stdcall do catálogo) · 6. `f_WideCharToMultiByte`? **SIM** (L1663–~1710) · 7. Catálogo? **SIM** `IMPL(...,32)` = 8 args × 4 (L5010) · 8. TODOs? **SIM, 2**: `TODO(...,"MultiByteToWideChar","conversão de código de página")` L5054 + idem `WideCharToMultiByte` L5055 (contidos nos 30) · 9. Testes unitários? nenhum dedicado (C não mexe em conversão) · 10. PEs importadores? **SIM**: `realpe/hello_mbwc.c` → `hello_mbwc.exe` (G33, round-trip `CP_UTF8` com `-1`) + startup CRT dos PEs recentes · 11. Code paths internos com conversão: `guest_cmdline_w` (linha UTF-16 em `guest_cmdline+256`, L199/5451), `IsDBCSLeadByteEx` (L1577, lead UTF-8 `>=0xC0`), `___lc_codepage_func`=**1252 fixo** (L1561), rotina em L1923 "UTF-16→UTF-8 (BMP); **surrogates/não-BMP recusados**", L2492 "UTF-16→ASCII (subset honesto)" · 12/13. Helpers de conversão? **não independentes** — a conversão vive nos dois handlers + tabela CP1252 `w32_cp1252_hi[32]` (L1580) · 14. Limitações explícitas: "codepage fora do suporte (ACP/1252/UTF-8)" + "UTF-8 fora do BMP" (ambas com `PR_ERR_UNSUPPORTED` honesto) · 15. Buffers: `uint16_t*`/`char*` em memória do convidado via `pr_win32_ptr`; varredura `-1` limitada a **0x4000** (família `wcslen`).

## 3. APIs encontradas (§3 — estado real das assinaturas)
`MultiByteToWideChar(UINT,DWORD,LPCCH,int,LPWSTR,int)` = 6 args (24) — registrado ✓. `WideCharToMultiByte(UINT,DWORD,LPCWCH,int,LPSTR,int,LPCCH,LPBOOL)` = 8 args (32) — registrado ✓ (os 2 últimos aceitos; o tratamento efetivo = §4). Confirmedas por código + ABI do PE real (§9).

## 4. Implementação pré-existente (§2 — preservada; NÃO reescrita)
**`f_MultiByteToWideChar`**: codepages **0 (ACP→CP1252), 1252, 65001**; `cbMultiByte=-1` = NUL-terminated (varredura segura até NUL, **inclui** o NUL na contagem); positivo = tamanho exato (sem NUL); consulta (`lpWideCharStr==NULL`); CP1252 via tabela real 0x80–0x9F (€=0x20AC etc.); UTF-8 1/2/3 bytes; **4 bytes → `cpv > 0xFFFF` → `PR_ERR_UNSUPPORTED`** (não-BMP recusado, nunca inventado); sem parcial (se `w==cap` devolve o escrito).
**`f_WideCharToMultiByte`**: mesmos codepages; `-1` varre até NUL; UTF-8 1/2/3 bytes de encode; CP1252 reverso (`'?'` se inexistente); cap insuficiente = `break` (parcial); consulta devolve `slen` (hipótese 1:1 — correta p/ CP1252, **divergente p/ UTF-8 com não-ASCII**).
**Validação histórica**: `hello_mbwc.exe` (G33) = round-trip `"winos_ñ"` (C3 B1) com `-1` nos dois sentidos = **42/29** (revalidado neste grupo, preservado).

## 5. Lacunas reais (§5/§12 — o que o probe demonstrou)
1. **DEMONSTRADA e CORRIGIDA (Situação B)**: `cchWideChar == 0` com `lpWideCharStr != NULL` → retornava **0** (via rejeição de buffer 0 bytes em `pr_win32_ptr`) em vez do **modo consulta** (`need`) do contrato Windows ("lpWideCharStr ignorado quando cchWideChar é zero"). O `f_WideCharToMultiByte` já protegia `len?:1` — só MB2WC tinha o padrão sem guarda. **1 bloco trocado** (§15).
2. **REGISTRADA como limitação (sem correção — sem demanda)**: não-BMP/surrogate pairs — consulta conta **1** unidade (Windows: 2); conversão = EXECUTION STOPPED honesto ("UTF-8 fora do BMP"). §5-D veda expandir sem evidência.
3. **REGISTRADA como divergência**: buffer insuficiente = **trunca e devolve o escrito** (Windows: `0` + `ERROR_INSUFFICIENT_BUFFER`). §6/§15 = observar, não inventar retorno.
4. **REGISTRADA como divergência**: consulta do `WideCharToMultiByte` devolve nº de unidades, não bytes UTF-8 (1:1). Sem demanda do probe (§13: não tocar).
5. `flags` aceitas e **ignoradas** (sem `MB_ERR_INVALID_CHARS` etc.) — §9 veda implementar todas.
6. TODOs "conversão de código de página" (outras CPs) permanecem — sem demanda.

## 6. PE criado (§4)
`realpe/hello_unicode_probe.c` → `Tests/PorticoRuntimeTests/data/hello_unicode_probe.exe` (41984 B, **0 warnings**). Contratos: 10 = A retorno ≠ 6; 11 = A conteúdo/terminador; 12 = B (Olá) errado; 13 = C (€) errado; 14 = D contagem não-BMP ≠ 1; 15 = comportamento de buffer observado divergente; 16 = -1 × positivo inconsistente; 17 = ACP inesperado; 18 = flags alterou resultado; 19 = canário/entrada corrompidos; 74 = OK.

## 7. Imports (§7 — por nome; loader real)
`KERNEL32.dll`: **`MultiByteToWideChar` (hint 0423 / IAT `0x14000f200`)**, `SetLastError` (0554), `GetLastError` (0283) + imports do startup MinGW que **já existem** no runtime (`WideCharToMultiByte` 062d, `IsDBCSLeadByteEx` 0391+, `InitializeCriticalSection`, etc. — nenhum artificial; `WideCharToMultiByte` **não** é usada pelo main do probe — §13). `msvcrt`: `printf` (diagnóstico).

## 8. Execução inicial (§11 — runtime INTACTO)
**Sem `Unsupported Win32 API`** — as APIs existem; o probe **executou parcialmente** e parou no contrato **15** (`rc=15`): o sub-caso `cchWideChar==0` com dst não-NULL devolveu ≠ 6. Console observado até a parada: A/B/C/D + buffers suficiente/exato/insuficiente/consulta OK. **Resultado real registrado** (§5.1) → decisão **Situação B** (§12). Após a correção mínima: **`rc=74` na primeira re-execução**.

## 9. ABI/disassembly (§10 — prova dos 6 argumentos)
```
140007cc8:  ff 15 3a 75 00 00    call   *0x753a(%rip)      # 0x14000f208  ; IAT SetLastError(0xDEAD)
140007cce:  48 8d 44 24 70       lea    0x70(%rsp),%rax    ; &a_in
140007cd3:  31 d2                xor    %edx,%edx          ; *** RDX = dwFlags = 0 ***
140007cd5:  b9 e9 fd 00 00       mov    $0xfde9,%ecx       ; *** RCX = 0xFDE9 = 65001 = CP_UTF8 ***
140007cda:  49 89 c0             mov    %rax,%r8           ; *** R8  = lpMultiByteStr (64-bit) ***
140007cdd:  41 b9 ff ff ff ff    mov    $0xffffffff,%r9d   ; *** R9  = cbMultiByte = -1 ***
140007ce8:  48 8b 35 11 75 00 00 mov    0x7511(%rip),%rsi  # 0x14000f200   ; IAT[MultiByteToWideChar]
140007cef:  c7 44 24 28 20 00 00 00 movl $0x20,0x28(%rsp)  ; *** stack[1] = cchWideChar = 32 (arg 6) ***
140007cf7:  48 89 6c 24 20       mov    %rbp,0x20(%rsp)    ; *** stack[0] = lpWideCharStr (arg 5) ***
140007cfc:  ff d6                call   *%rsi              ; *** MultiByteToWideChar via IAT ***
140007d18:  89 c3                mov    %eax,%ebx          ; *** retorno int consumido de EAX ***
```
Provado no PE real: **RCX=CodePage / RDX=flags / R8=lpMultiByteStr / R9=cbMultiByte / args 5-6 na stack (`0x20`/`0x28(%rsp)` = área além do shadow space) / retorno em EAX**; largura consumida = `int`/ponteiro 64-bit sem truncamento (`movzwl` dos code units de volta). Convenção Win64 confirmada por disassembly, não só pela assinatura C.

## 10. Casos Unicode testados (§5/§17)
| Caso | Entrada (bytes) | Esperado | Observado |
|---|---|---|---|
| A ASCII | `"WinOS"` `-1` | `0057 0069 006E 004F 0053 0000` (6) | **idêntico**, ret=6 ✓ |
| B 2-byte | `"Olá"` = `4F 6C C3 A1` | `004F 006C 00E1 0000` (4) | **idêntico** (U+00E1) ✓ |
| C 3-byte | `"€"` = `E2 82 AC` | `20AC 0000` (2) | **idêntico** (U+20AC) ✓ |
| D não-BMP | `F0 9F 98 80` (U+1F600) | Windows: 2 (par surrogate) | **consulta=1** (limitação); conversão = **EXECUTION STOPPED** "UTF-8 fora do BMP" (não chamada — §5-D) |

Único ASCII seria insuficiente (§17) — 2-byte e 3-byte validados byte a byte. 4-byte = limitação registrada.

## 11. Buffer/terminação (§6/§7)
Com `"WinOS"` (`-1` ⇒ need **6** = 5+NUL): **suficiente (32)** → 6 ✓ · **exato (6)** → 6 ✓ · **insuficiente (3)** → **ret 3 + `W i n` truncado** (Portico; Windows = 0 + `ERROR_INSUFFICIENT_BUFFER`) — `small[3]` canário `0xDDDD` **intocado** (nada além da região) ✓ · **`lpWideCharStr == NULL` (0,0)** → **6** (consulta) ✓ · **`cchWideChar == 0`** → **6** (consulta; pós-correção §15) ✓ · **vazia `""` `-1`** → **1** (só NUL) ✓. **Terminação (§7)**: `cbMultiByte=-1` → **conta e escreve o NUL** (6/4/2/1 incluem NUL) vs **positivo (5)** → **5 sem NUL** (`wbuf[5]=0xEEEE` **não escrito** — provado por canário) ✓ — diferença `-1` × positivo verificada explicitamente.

## 12. Retornos (§15)
Quantidade de UTF-16 code units **com NUL quando `-1`**, **sem NUL quando positivo**; insuficiente = escrito (3); consulta = need (6); vazia = 1. **Nenhum valor artificial** introduzido — os contratos do PE registram o comportamento observado (com divergências vs Windows nas §5.2–5.3).

## 13. Erros (§16)
**Comportamento observado**: codepage fora de {0,1252,65001} e UTF-8 fora do BMP → `diagf` + **`PR_ERR_UNSUPPORTED` = EXECUTION STOPPED** (motivo honesto em `pr_win32_last_detail`; nunca sucesso falso). Ponteiro fora do convidado → `PR_ERR_FAULT`. **LastError**: `SetLastError(0xDEAD)` → `MultiByteToWideChar` → `GetLastError()` = **`0x0000DEAD`** (impresso pelo probe: `[*] A ret=6 last=0000DEAD`) = **preservado** (APIs de conversão não mexem em LastError; `GetLastError`/`SetLastError` **não** modificados). Contrato Windows conhecido (ex.: `ERROR_INSUFFICIENT_BUFFER`) **não** copiado; divergências declaradas.

## 14. Canários (§14)
`pre[4]` (`0xA1A1..0xD4D4`) e `suf[4]` (`0xE5E5..0x1818`) em `WCHAR` **intactos** após todas as conversões (contrato 19). Buffer de entrada **nunca modificado** (bytes de `a_in/b_in/c_in` verificados após as chamadas). Região esperada é a única escrita (`small[3]=0xDDDD`, `wbuf[5]=0xEEEE` provas positivas).

## 15. Alterações realizadas (§12-B — mínimas)
**Runtime: 1 bloco** em `f_MultiByteToWideChar` (troca da ordem `dst/cap`: a consulta `!a[4] || cap==0` acontece **antes** de validar o buffer — `lpWideCharStr` ignorado quando `cchWideChar==0`, contrato Windows). Mais: `realpe/hello_unicode_probe.c` + `.exe` (PE novo). **NADA mais**: `f_WideCharToMultiByte`, tabela CP1252, TODOs, `f_Tls*`/stdio/etc. intocados; **nenhuma segunda implementação/infra** (`unicode_table`&c.); nenhum PE histórico alterado.

## 16. C (§19)
**3398 verificações, 0 falhas** (sem CHECKs novos).

## 17. Swift (§19)
**Executed 63 tests, with 0 failures** ✓.

## 18. warnings (§19)
**0** (runtime/drivers `-Wall -Wextra`; PE MinGW 0).

## 19. analyzer (§19)
**7 total / 0 novos / 7 pré-existentes** (mesma lista do G72/G73). Nada mascarado.

## 20. PE battery (§18)
**15/15 rc=42** — logs **28/43/180/88/72/75/98/118/76/81/194/131/55/200/105** byte-idênticos + **`hello_mbwc` = 42/29** (PE Unicode histórico preservado). Nenhuma regressão.

## 21. 20 execuções (§20)
**20/20** `exited=1 rc=74 log=742` **idênticos** (rc/exited/log constantes; console byte-idêntico entre execuções).

## 22. checkpoints G52–G74 (§22)
**23/23 verdes**: … G72=72/274, G73=73/334, **G74=74/742**.

## 23. checklist (§23)
**45/45** — item acrescentado: **45. Unicode conversion / MultiByteToWideChar (G74)** ✓ (44 anteriores inalteradas). Grupo com validação suficiente (não é inventário inconclusivo): casos A–C byte a byte + buffers + terminação + ACP + flags + ABI + 20/20.

## 24. MD5 (§24)
| Arquivo | antes | depois | o que mudou |
|---|---|---|---|
| `pr_win32.c` | `75b1c8f954370b73490e9d1ccc6e84a5` (= pós-G73; reverso exato confirmado) | `893daf6ffaefeafa5e6464a691e6fd92` | **1 bloco** de `f_MultiByteToWideChar` (consulta `cap==0`) |
| Combinado (todos .c/.h) | `a59e7cef7d2ef7ccd3296edb01a51651` | `5467d7acbb256bc5401061fff092edc1` | `pr_win32.c` + novo `realpe/hello_unicode_probe.c` |

## 25. filesystem (§25)
`build/win_fs` = **só `fixture.txt`** após todos os testes (o probe não usa arquivos). Sem temporários.

## 26. limitações (§24 — explícitas)
1. **Unicode geral NÃO declarado** — apenas CP_UTF8 (1/2/3 bytes BMP) + CP1252/ACP testados.
2. **UTF-16 completo não**: **surrogate pairs / não-BMP (≥ U+10000) NÃO suportados** (consulta=1 unidade; conversão recusa).
3. **ACP** = CP1252 fixa e determinística (não o ACP real do Windows do usuário).
4. **Outras code pages** (932, 936, 1200…) = `EXECUTION STOPPED` honesto (TODO "conversão de código de página").
5. **Flags** (`MB_ERR_INVALID_CHARS`, `MB_PRECOMPOSED`…) = aceitas e **ignoradas**.
6. **Strings enormes**: varredura `-1` limitada a **0x4000** bytes/unidades.
7. **UTF-8 inválido** não é diagnosticado (bytes malformados são engolidos como 1 unidade em alguns caminhos) — não testado como erro.
8. **`WideCharToMultiByte`**: validada só pelo round-trip histórico (BMP); consulta UTF-8 = 1:1 (divergente); não foi alvo do probe (§13).
9. Buffer insuficiente = truncamento (divergência Windows registrada §5.3).
10. **Sem compatibilidade geral** Windows/Winlar/GTA V/MX Bikes/jogos.

## 27. compatibilidade efetivamente demonstrada
Somente o observado: `MultiByteToWideChar` por import real (IAT `0x14000f200`, hint 0423) com ABI Win64 provada (RCX/RDX/R8/R9/stack/EAX); `"WinOS"`→UTF-16 com NUL; `"Olá"` (U+00E1) e `"€"` (U+20AC) byte a byte; `-1` conta NUL / positivo não; consulta NULL e cap 0 = need; truncamento documentado; CP_ACP(0)→CP1252 (`0xF1→0x00F1`); flags inertes; entradas/canários intactos; LastError preservado; `hello_mbwc` round-trip preservado. **Sem** declaração de Unicode geral.

## 28. próximo candidato (somente indicação — NÃO executar G75; estado real pós-G74)
Do inventário real: MB2WC/WC2MB **existem** e estão validadas em BMP; sobram (a) **`kernel32!ReadFileEx`** — TODO real remanescente (E/S assíncrona; complexidade alta; candidato listado no enunciado), (b) expansão **não-BMP/surrogates** — limitação observada neste grupo, mas **só com demanda de PE real** (§24.2/§5-D veda expandir sem evidência), (c) outras code pages (TODO) — também sem demanda. **G74 indica: `kernel32!ReadFileEx`** (é a lacuna real com TODO e sem implementação nenhuma), com a ressalva de começar pelo inventário read-only (ver se algum PE real já o importa).
