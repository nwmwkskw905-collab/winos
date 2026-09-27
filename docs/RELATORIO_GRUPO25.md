# RELATORIO_GRUPO25 — CPU x64/SSE2: PADDB xmm, xmm (66 0F FC /r)

Data: 2026-09-25. Objetivo: implementar EXCLUSIVAMENTE o `PADDB xmm, xmm`
(66 0F FC /r, forma registrador-registrador observada) — soma inteira
MODULAR de 8 bits por lane, sem carry entre lanes — sem implementar MOVUPS
ou qualquer outra instrução/API por antecipação. O próximo obstáculo real é
registrado ao final, determinado pela execução.

---

## 1. Baseline (registrado antes de qualquer alteração)

| Item | Valor |
|---|---|
| make c-test | **3321 verificações, 0 falhas** |
| swift test | **63 tests, 0 failures** |
| Warnings | **0** |
| Analyzer | **0** (build/analyzer24.err vazio) |
| 15 PEs | rc=42 nos contratos; bateria 17/17 (28/43/180/88\*/72/75/98/118/76/81/194/131/55/200/105\*) |
| hello_gl11.exe | rc=42, log=265 |
| hello_sse.exe | rc=7 (sucesso projetado), log=209 |
| OPENGL32.dll | 49 exports reais |
| hello_gl12.exe | parado em `paddb` (blocker do G24) |

(\*hello_gdi=88 e hello_input=105 no modo injetado — calibração G22.)

## 2. Blocker atacado

Exatamente o registrado pelo G24 (evidência anterior, verbatim):
`EXECUTION STOPPED | Reason: instrucao fora do subconjunto x64 (opcode 0F FC)
| Module: app.exe | Address: 0x00FE19CF | Technical: rip=0xFE19CF opcode=0x0F
addr=0x00000000 bytes=66 0F FC C1 41 0F 11 04` — objdump
`1400029cf: 66 0f fc c1  paddb %xmm1,%xmm0`, laço de `gen_tex()`. Nenhuma
outra instrução/API foi escolhida.

## 3. Opcode e encoding

* **Instrução**: `PADDB xmm, xmm` — packed byte add.
* **Encoding**: `66 0F FC /r`, `mod=11`; destino = ModRM.reg; fonte =
  ModRM.r/m. Caso real: `66 0F FC C1` = `paddb %xmm1,%xmm0`.

## 4. Implementação (exatamente)

Bloco único em `pr_cpu64.c` (após o bloco do PAND), reutilizando
`decode_rm`/`FAULT` e o `c->xmm[16][16]` existente (sem estrutura paralela):

* **Operação**: 16 lanes de 8 bits, `destino_byte = destino_byte + fonte_byte`
  **modular/truncado em 8 bits** (ex.: `0xFF + 0x01 = 0x00`); **nenhum carry
  para lane vizinha** — cada byte é somado isoladamente.
* **Escopo mínimo**: SOMENTE `mod=11` (registrador-registrador). Forma com
  memória (`mod≠11`) → fault honesto `"paddb exige forma
  registrador-registrador (xmm, xmm)"`; sem o prefixo 66 → continua fora do
  subconjunto.
* **Não implementado (fault honesto mantido)**: PADDW, PADDD, PADDQ, PSUBB/
  PSUBW/…, PANDN, POR, demais packed, AVX. **MOVUPS NÃO foi implementado** —
  já era suportado antes (0F 10/11) e a execução nem parou nele (item 14).
* **Estado preservado (SDM)**: RFLAGS intocados (sem CF/OF — PADDB não toca
  flags), MXCSR e demais registradores intocados; alias destino==fonte é
  seguro.
* **pr_gl.c / pr_gl.h NÃO foram modificados** (md5 estáveis durante o grupo:
  `bc83284d…` / `5bc6913f…`); nenhuma API OpenGL adicionada.

## 5. Arquivos alterados

| Arquivo | Mudança |
|---|---|
| `Sources/PorticoRuntime/src/pr_cpu64.c` | bloco `op2 == 0xFC && p66 && !prep` (PADDB, ~20 linhas) |
| `Tests/PorticoRuntimeTests/test_cpu64ext.c` | bloco `GRUPO 25` com 6 casos (35 verificações) + atualização do teste negativo do G24 (ver nota) |

Nota de alteração de teste existente (honestidade): o caso 6 do G24 assegurava
"PADDB continua fora" (`66 0F FC C1` → fault). Essa asserção tornou-se falsa
**por propósito do G25** (PADDB é o blocker implementado). O teste foi
atualizado para o irmão ainda não implementado **PADDW** (`66 0F FD C1` →
fault), preservando a intenção ("nada da família packed escorregou"); a
família inteira (PADDW/PADDD/PADDQ/PSUBB/PANDN/POR + forma memória + sem 66)
fica coberta pelas 8 negativas do próprio G25. Nenhum outro teste anterior foi
alterado.

## 6. Testes adicionados (6 casos, 35 verificações, só adições)

1. **Encoding exato do hello_gl12** `66 0F FC C1` (`paddb %xmm1,%xmm0`):
   **soma sem overflow** `0x10+0x20=0x30` nas 16 lanes; fonte intacta; demais
   XMM (xmm7) intactos; GPRs (RAX/RCX) intactos; RFLAGS intactos; **`n == 2`**
   (paddb + hlt).
2. **Overflow modular + independência de lanes / SEM carry entre bytes**:
   `0xFF+0x01=0x00`, `0x80+0x80=0x00`, `0xFE+0x03=0x01`, `0xF0+0x20=0x10`
   (0x110 truncado); lanes vizinhas permanecem exatas (nenhum carry vazou).
3. **Valores diversos em todas as 16 lanes** (pares distintos; wraps isolados
   em lanes 2, 4, 9, 10, 11, 14 comprovam byte a byte a independência).
4. **Padrões + diferentes XMMs + REX.R/REX.B**: `00+00=00`, `FF+00=FF`,
   `00+FF=FF`, `AA+55=FF`, `55+AA=FF`, `FF+FF=FE` em xmm0/2/4/6/8/10
   (`66 0F FC C1/D3/E5/F7` e `66 45 0F FC C1/D3` para xmm8–11); fonte
   (xmm11) intacta; `n == 7`.
5. **Destino == fonte** (`66 0F FC C0`): dobra modular por lane (80+80=00,
   FF+FF=FE, AA+AA=54, 81+81=02…); alias seguro; `n == 2`.
6. **Nada de implementação antecipada**: PADDW (0F FD), PADDD (0F FE), PADDQ
   (0F D4), PSUBB (0F F8), PANDN (0F DF), POR (0F EB) → fault; forma memória
   (`66 0F FC 02`) → fault ("registrador"); sem prefixo 66 (`0F FC C1`) →
   fault.

## 7. make c-test

**3356 verificações, 0 falhas** (3321 do baseline + 35 do G25).

## 8. swift test

**Executed 63 tests, with 0 failures** (toolchain Swift 6.1.2 reinstalado em
`~/.cache/swifttc` antes da execução).

## 9. Warnings

Compilação `-Wall -Wextra -Werror=implicit-function-declaration` e build das
ferramentas: **0 warnings**.

## 10. Analyzer

`gcc -std=c11 -fanalyzer …` sobre runtime + testes (`build/analyzer25.err`):
**0 diagnósticos**.

## 11. Os 15 PEs preservados

rc=42 e contagens de log **todas nos contratos**: 28/43/180/88/72/75/98/118/
76/81/194/131/55/200/105 (gdi/input no modo injetado). **17/17** verdes.

## 12. hello_gl11.exe preservado

**exited=1 rc=42 log=265** — cadeia imutável completa.

## 13. hello_sse.exe preservado

**exited=1 rc=7 log=209** (sucesso projetado).

## 14. hello_gl12.exe — ultrapassou 0x00FE19CF e COMPLETOU a execução

Duas execuções consecutivas, saídas **byte-idênticas** (`diff` vazio):

```
exited=1 rc=31 steps=1 exec=2903
PROCESS EXIT
Reason: ExitProcess
Module: msvcrt.dll
Function: exit
Address: 0x00FE03CF
Exit code: 31
```

* O executor **não parou mais em instrução nem em API**: o laço
  `movdqu→psrlw→pand→paddb→movups` executou por completo (**2903 instruções,
  1 step**), incluindo o `41 0F 11 …` (MOVUPS store com REX — já suportado
  antes do G25; não foi preciso — e não foi — implementar nada novo).
* **rc=31 NÃO é sucesso**: é o código PROJETADO do hello_gl12 para falha de
  sonda (30..39 = sondas reprovadas). A sonda que reprova é a 31
  (`hello_gl12.c:201`): `px_is(185, 110, 0, 0, 255) /* faceta azul (flat) */`.
* **Validação direta do trabalho SSE do G23–G25**: os 4 texels gerados pelo
  `gen_tex` batem EXATAMENTE com o esperado — (224,174)=(32,32,159),
  (256,190)=(104,159,32), (240,214)=(95,96,96), (280,230)=(47,64,128) —
  além do assoalho (100,110)=(51,51,51), o marcador (140,120)=(255,255,255)
  e o fundo (300,220)=(0,0,0).
* O hello_gl12 **NÃO foi concluído com sucesso** (não há ExitProcess(42)).
* Critério "ultrapassar 0x00FE19CF" cumprido (execução chegou ao fim).

## 15. PRÓXIMO BLOCKER REAL — registrado, NÃO corrigido

**Não há mais blocker de instrução x64 nem de API Win32/OpenGL no caminho do
hello_gl12.** O próximo obstáculo real é **comportamental** (comportamento de
renderização divergente do esperado pelo PE):

* **Sintoma**: sonda 31 reprova — `px_is(185, 110, 0, 0, 255)` = "faceta azul
  (flat)" (`realpe/hello_gl12.c:201`). Obtido via `dbg_px`: **(46,130,79)** =
  cor **interpolada**; esperado **(0,0,255)** = cor sólida do último vértice
  sob `GL_FLAT` (v2 azul).
* **Sondas relacionadas também divergem** (verificadas por `dbg_px`; não
  alcançadas pelo rc, que para na primeira): (135,130) obtido **(103,194,152)**,
  esperado (255,255,0) = faceta amarela FLAT (sonda 32); (130,125) obtido
  **(111,189,144)**, esperado ~(128,255,128) da cortina (sonda 33).
* **Fase**: execução completa (2903 instruções, 1 step); saída via
  `msvcrt!exit` (0x00FE03CF) com o código projetado 31.
* **Opcode/bytes/RIP/VA**: não se aplicam (não é parada de instrução).
* **Contexto**: `glShadeModel(GL_FLAT)` (`hello_gl12.c:93`) + painel com color
  array (`pc`: v0 vermelho, v1 verde, v2 azul, v3 amarelo) +
  `glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, idx)` com
  `idx={0,1,2, 0,2,3}`. As facetas saem **interpoladas (SMOOTH)** em vez da
  cor sólida do último vértice (FLAT). O gate FLAT implementado no G22 está
  comprovado nos testes unitários via `glDrawArrays`; a via do PE usa
  `glDrawElements` + color array. Hipóteses para o G26 investigar (NÃO
  implementadas aqui): (a) o gate `shade_model` do rasterizador não está
  ativo nessa via/estado no momento do draw; (b) o `shade_model` não está
  `GL_FLAT` no momento do desenho do painel (reset/reuso de estado).
* Para o G26: descobrir a causa pela execução (sem reescrever o PE, sem
  enfraquecer a sonda) e corrigir SOMENTE o comportamento observado.

## 16. Limitações reais restantes

* hello_gl12 termina com **rc=31 (sonda reprovada)**: as facetas GL_FLAT do
  painel renderizam interpoladas; a cena NÃO está validada ponta a ponta.
* PADDB coberto somente na forma registrador-registrador; a forma com memória
  (`paddb xmm, m128`) continua fora do subconjunto (fault honesto) por não ser
  necessária ao caminho observado.
* Aritmética packed inteira restante (PADDW/PADDD/PADDQ, PSUB*, PMULLW,
  PMAX/PMIN…), lógicas irmãs (PANDN/POR) e AVX seguem fora, com fault
  honesto.
* Resultados obtidos com os PEs controlados do repositório em Linux/x86-64;
  não declaramos compatibilidade com CPUs/GPUs, drivers ou jogos comerciais.
