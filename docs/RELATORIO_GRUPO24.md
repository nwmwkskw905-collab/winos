# RELATORIO_GRUPO24 — CPU x64/SSE2: PAND xmm, xmm (66 0F DB /r)

Data: 2026-09-25. Objetivo: implementar EXCLUSIVAMENTE o PAND necessário
para ultrapassar o blocker real observado no G23 — a forma
registrador-registrador `66 0F DB /r` (caso real `66 0F DB C2`) — sem
implementar PADDB/PANDN/POR/demais SSE2, sem suporte genérico e sem tocar em
OpenGL. Termina no próximo blocker real.

---

## 1. Baseline (registrado antes de qualquer alteração)

| Item | Valor |
|---|---|
| make c-test | **3293 verificações, 0 falhas** |
| swift test | **63 tests, 0 failures** |
| Warnings (-Wall -Wextra) | **0** |
| Analyzer (-fanalyzer) | **0** (build/analyzer23.err vazio) |
| 15 PEs | rc=42 nos contratos (28/43/180/88\*/72/75/98/118/76/81/194/131/55/200/105\*) |
| hello_gl11.exe | rc=42, log=265 |
| hello_sse.exe | rc=7 (sucesso projetado), log=209 |
| OPENGL32.dll | 49 exports reais |
| hello_gl12.exe | parado em `pand` (blocker do G23) |

(\*hello_gdi=88 e hello_input=105 no modo injetado — calibração G22.)

## 2. Blocker atacado

Exatamente o registrado pelo G23 (evidência anterior, verbatim):
`EXECUTION STOPPED | Reason: instrucao fora do subconjunto x64 (opcode 0F DB)
| Module: app.exe | Address: 0x00FE19CB | Technical: rip=0xFE19CB opcode=0x0F
addr=0x00000000 bytes=66 0F DB C2 66 0F FC C1` — objdump
`1400029cb: 66 0f db c2  pand %xmm2,%xmm0`, laço de `gen_tex()`. Nenhuma outra
instrução/API foi escolhida ou implementada.

## 3. Implementação (exatamente)

Bloco único em `pr_cpu64.c` (após o bloco do PSRLW), reutilizando
`decode_rm`/`FAULT` e o `c->xmm[16][16]` existente (sem estrutura paralela):

* **Encoding**: `66 0F DB /r` com p66 e sem F2/F3. Destino = `xmm[regf]`
  (ModRM.reg), fonte = `xmm[o.reg]` (ModRM.r/m) — `destino = destino AND
  fonte` em 128 bits, byte a byte.
* **Escopo mínimo (caminho observado)**: SOMENTE a forma
  **registrador-registrador** (`mod=11`) — `66 0F DB C2` = `pand %xmm2,%xmm0`.
  Forma com memória (mod≠11) → fault honesto `"pand exige forma
  registrador-registrador (xmm, xmm)"`; sem o prefixo 66 → continua fora do
  subconjunto.
* **Não implementado (continuam com fault honesto)**: PADDB (`0F FC`), PANDN
  (`0F DF`), POR (`0F EB`), PADDW/PSUB*/PMULLW e toda a aritmética packed
  inteira; AVX; qualquer API OpenGL.
* **Estado preservado (SDM)**: RFLAGS e MXCSR intocados; fonte, demais XMM e
  GPRs intocados; alias destino==fonte é seguro (operação byte a byte em
  registrador único).

## 4. Arquivos alterados

| Arquivo | Mudança |
|---|---|
| `Sources/PorticoRuntime/src/pr_cpu64.c` | bloco `op2 == 0xDB && p66 && !prep` (PAND, ~20 linhas) |
| `Tests/PorticoRuntimeTests/test_cpu64ext.c` | bloco `GRUPO 24` com 6 casos (28 verificações) |

Nenhum outro arquivo tocado; nenhum teste anterior alterado.

## 5. Testes adicionados (6 casos, 28 verificações, só adições)

1. **Encoding exato do hello_gl12** `66 0F DB C2` (`pand %xmm2,%xmm0`): AND
   128-bit completo nos 16 bytes com zero/all-ones/alternado/misto
   (0x00, 0xFF, 0xAA/0x55, 0x12/0x34, 0xFE/0x7F, 0xC3/0x3C, 0x99/0x66);
   destino conferido byte a byte; **fonte intacta**; demais XMM (xmm7)
   intactos; GPRs (RAX/RCX) intactos; RFLAGS intactos; **`n == 2`** (pand +
   hlt = só a instrução necessária).
2. **Valores zero e all-ones** nos dois sentidos (`zeros &= ones` e
   `ones &= zeros`) → zero; `n == 3` (2 pands + hlt).
3. **Padrões alternados**: `AA&55=00`, `AA&AA=AA`, `FF&55=55` em três pares
   distintos (xmm0/xmm2, xmm1/xmm3, xmm5/xmm4); `n == 4`.
4. **Diferentes XMMs (altos)**: `66 45 0F DB C3` = `pand %xmm11,%xmm8`
   (REX.R+REX.B); destino correto e fonte (xmm11) intacta.
5. **Destino == fonte**: `66 0F DB C0` (`pand %xmm0,%xmm0`) preserva o valor
   (AND idempotente; sem corrupção de alias); `n == 2`.
6. **Formas não suportadas rejeitadas honestamente**: memória
   (`66 0F DB 02` = pand xmm0,[rdx]) → fault ("registrador"); sem prefixo 66
   (`0F DB C2`) → fault; **PADDB `66 0F FC C1` → fault** (comprova que NÃO foi
   implementado por antecipação).

Nota de correção honesta: o primeiro rascunho do caso 3 tinha dois erros de
montagem própria (registrador com conflito xmm2-fonte/xmm2-destino e ModRM
`0xE4` em vez de `0xEC` para `pand %xmm4,%xmm5`); o aviso
`-Wunused-variable` denunciou o caso e ambos foram corrigidos antes da
assinatura. Os encodings de todos os casos foram revalidados byte a byte.

## 6. make c-test

**3321 verificações, 0 falhas** (3293 do baseline + 28 do G24).

## 7. swift test

**Executed 63 tests, with 0 failures** (toolchain Swift 6.1.2 reinstalado em
`~/.cache/swifttc` antes da execução).

## 8. Warnings

Compilação `-Wall -Wextra -Werror=implicit-function-declaration` (runtime +
testes) e build das ferramentas: **0 warnings**.

## 9. Analyzer

`gcc -std=c11 -fanalyzer -Wall -Wextra -Wno-analyzer-use-of-uninitialized-value`
sobre runtime + testes (`build/analyzer24.err`): **0 diagnósticos**.

## 10. Os 15 PEs preservados

rc=42 e contagens de log **todas nos contratos**: 28/43/180/88/72/75/98/118/
76/81/194/131/55/200/105 (gdi/input no modo injetado). **17/17** da bateria
verdes; nenhum binário alterado.

## 11. hello_gl11.exe preservado

**exited=1 rc=42 log=265** — cadeia imutável completa.

## 12. hello_sse.exe preservado

**exited=1 rc=7 log=209** (sucesso projetado).

## 13. hello_gl12.exe — avançou ALÉM de 0x00FE19CB

Duas execuções consecutivas, saídas **byte-idênticas** (`diff` vazio):

```
step[0] falhou
EXECUTION STOPPED
Reason: instrucao fora do subconjunto x64 (opcode 0F FC)
Module: app.exe
Function: <instrução x64>
Address: 0x00FE19CF
Architecture: x86-64
Technical: rip=0xFE19CF opcode=0x0F addr=0x00000000 bytes=66 0F FC C1 41 0F 11 04
```

O RIP saiu de `0x00FE19CB` para `0x00FE19CF` (+4 bytes = o tamanho exato do
`pand`): o laço executou `movdqu` + `psrlw` + `pand` e parou na instrução
seguinte. Critério "ultrapassar 0x1400029cb" cumprido.

## 14. PRÓXIMO BLOCKER REAL — registrado, NÃO implementado

* **Instrução**: `PADDB xmm0, xmm1` — `66 0F FC C1` (packed byte add, SSE2).
* **Opcode**: `0F FC` (prefixo 66).
* **RIP**: `0x00FE19CF`. **VA/RVA**: `0x1400029cf` (objdump do G22:
  `1400029cf: 66 0f fc c1  paddb %xmm1,%xmm0`).
* **Bytes**: `66 0F FC C1` (e `41 0F 11 04…` logo em seguida = `movups` store
  com REX, **não alcançado**).
* **Função/contexto**: `gen_tex()` (laço vetorizado `movdqu→psrlw→pand→paddb`
  da média de bytes `(tmp[i]+64)>>1`), módulo app.exe (hello_gl12.exe).
* **Fase**: execução (step[0]; orçamento de 10.000 instruções/step).
* **Quantidade de instruções**: o diagnóstico de parada não emite a contagem
  exata; a parada é na 4ª instrução do corpo do laço (após `movdqu`+`psrlw`+
  `pand` do primeiro iter), dentro do primeiro step.

Para o G25: implementar somente `PADDB xmm, xmm` (`66 0F FC /r`) se continuar
sendo o blocker observado; o `movups` (0F 11 com REX.B) aparece como candidato
do próprio laço logo em seguida — descobrir pela execução, nunca por
antecipação.

## 15. Limitações reais restantes

* O hello_gl12 **não conclui a cena**: parado em `PADDB` (item 14), ainda no
  `gen_tex`, antes de qualquer chamada OpenGL.
* PAND coberto somente na forma registrador-registrador; a forma com memória
  (`pand xmm, m128`) continua fora do subconjunto (fault honesto) por não ser
  necessária ao caminho observado.
* Toda a aritmética packed inteira (PADDW/PADDB, PSUB*, PMULLW, PMAX/PMIN…)
  e lógicas irmãs (PANDN/POR) seguem fora, com fault honesto.
* Resultados obtidos com os PEs controlados do repositório em Linux/x86-64;
  não declaramos compatibilidade com CPUs/GPUs, drivers ou jogos comerciais.
