# RELATORIO_GRUPO23 — CPU x64/SSE2: PSRLW xmm, imm8 (66 0F 71 /2 ib)

Data: 2026-09-25. Objetivo: implementar EXCLUSIVAMENTE a primeira instrução
real observada no blocker do G22 — `PSRLW xmm, imm8` — reutilizando a
infraestrutura SSE existente, sem implementar PAND/PADDB/demais SSE2 e sem
criar suporte genérico para o grupo 0F 71. Termina no próximo blocker real.

---

## 1. Baseline (registrado antes de qualquer alteração)

| Item | Valor |
|---|---|
| make c-test | **3262 verificações, 0 falhas** |
| swift test | **63 tests, 0 failures** |
| Warnings (-Wall -Wextra) | **0** |
| Analyzer (-fanalyzer) | **0** (build/analyzer22.err vazio) |
| 15 PEs | rc=42 nos contratos (28/43/180/88\*/72/75/98/118/76/81/194/131/55/200/105\*) |
| hello_gl11.exe | rc=42, log=265 |
| hello_sse.exe | rc=7 (sucesso projetado), log=209 |
| OPENGL32.dll | 49 exports reais (glShadeModel = 49º, G22) |
| hello_gl12.exe | parado em `psrlw` (blocker do G22) |

(\*hello_gdi=88 e hello_input=105 no modo injetado — ver G22 item 11.)

## 2. Blocker atacado

Exatamente o registrado pelo G22 (evidência anterior à implementação,
verbatim): `EXECUTION STOPPED | Reason: instrucao fora do subconjunto x64
(opcode 0F 71) | Module: app.exe | Address: 0x00FE19C6 | Technical:
rip=0xFE19C6 opcode=0x0F addr=0x00000000 bytes=66 0F 71 D0 01 66 0F DB` —
objdump `1400029c6: 66 0f 71 d0 01 psrlw $0x1,%xmm0`, no laço de `gen_tex()`.
Encoding observado: `66 0F 71 /2 ib`. Nenhuma outra instrução/API foi
escolhida ou implementada.

## 3. Implementação (exatamente)

Bloco único em `pr_cpu64.c`, imediatamente após o PSHUFD, reutilizando
`decode_rm`/`fetch`/FAULT e o armazenamento `c->xmm[16][16]`:

* **Encoding somente**: `66 0F 71 /2 ib` (p66=1, sem F2/F3). O XMM destino
  fica no **r/m (mod=11)** e o campo reg identifica a extensão **/2**.
* **Operação**: packed 16-bit **logical** right shift — 8 lanes unsigned de
  16 bits (ordem little-endian no XMM), `lane >> imm8` por lane.
* **imm8 ≥ 16 ⇒ lane = 0** (Intel SDM), sem UB de shift.
* **Escopo mínimo (regra 10)**: `regf != 2` (i.e. /4 PSRAW, /6 PSLLW…) →
  fault honesto `"0F 71 somente /2 (psrlw) no subconjunto x64"`; forma com
  memória (mod≠11) → fault `"psrlw imm8 exige operando registrador xmm"`;
  sem o prefixo 66 → continua fora do subconjunto (fault genérico).
* **Estado preservado (regra 9)**: RFLAGS e MXCSR não são tocados (SDM);
  demais XMM e GPRs não são tocados; as 8 lanes do destino são escritas
  (operação packed completa).
* Não há suporte genérico ao grupo 0F 71; PAND (0F DB) e PADDB (0F FC) **não**
  foram implementados.

## 4. Arquivos alterados

| Arquivo | Mudança |
|---|---|
| `Sources/PorticoRuntime/src/pr_cpu64.c` | bloco `op2 == 0x71 && p66 && !prep` com o PSRLW (~25 linhas) |
| `Tests/PorticoRuntimeTests/test_cpu64ext.c` | bloco `GRUPO 23` com 9 casos (31 verificações) |

Nenhum outro arquivo tocado; nenhum teste anterior alterado.

## 5. Testes adicionados (9 casos, só adições)

1. **Encoding exato do hello_gl12** `66 0F 71 D0 01` (`psrlw $1,%xmm0`): 8
   lanes com valores 0x0000/0x0001/0xFFFF/intermediários (0x1234, 0x7FFF,
   0x8000, 0xFFFE, 0x0003); esperado lógico byte a byte (0xFFFF→0x7FFF prova
   lógico, não aritmético); **`n == 2` (psrlw + hlt = só o necessário)**;
   demais XMM (1 e 7), GPRs (RAX/RCX) e RFLAGS intactos.
2. **Shift 0** = identidade exata nas 8 lanes.
3. **Shift intermediário** (`$4`, `psrlw $4,%xmm3`): lanes diversas.
4. **Shift 15** (máximo sem zerar): 0xFFFF→0x0001, 0x8000→0x0001,
   0x0001→0x0000, 0xC000→0x0001, 0x4000→0x0000.
5. **Shift ≥ 16 zera tudo**: `$16` e `$255` (imediato cru de 8 bits).
6. **XMM alto via REX.B**: `66 41 0F 71 D3 03` (`psrlw $3,%xmm11`).
7. **Somente /2**: `66 0F 71 E0 01` (PSRAW /4) e `66 0F 71 F0 01` (PSLLW /6)
   → fault com "/2" no motivo — o grupo 0F 71 NÃO virou suporte genérico.
8. **Forma memória** `66 0F 71 10 01` (mod≠11) → fault ("registrador").
9. **Sem o prefixo 66** `0F 71 D0 01` → fault (fora do subconjunto).

Nota de correção honesta: o primeiro rascunho do caso 3 usava ModRM `0xC3`
(/0) por engano de encoding do próprio teste; o `regf != 2` do implementado o
rejeitou (3 falhas), comprovando na prática o escopo /2. Corrigido para
`0xD3` (/2, xmm3) — encoding do teste conferido byte a byte.

## 6. make c-test

**3293 verificações, 0 falhas** (3262 do baseline + 31 do G23).

## 7. swift test

**Executed 63 tests, with 0 failures** (toolchain Swift 6.1.2 reinstalado em
`~/.cache/swifttc` antes da execução).

## 8. Warnings

Compilação `-Wall -Wextra -Werror=implicit-function-declaration` (runtime +
testes) e build das ferramentas: **0 warnings**.

## 9. Analyzer

`gcc -std=c11 -fanalyzer -Wall -Wextra -Wno-analyzer-use-of-uninitialized-value`
sobre runtime + testes (`build/analyzer23.err`): **0 diagnósticos**.

## 10. Os 15 PEs preservados

rc=42 e contagens de log **todas nos contratos**: 28/43/180/88/72/75/98/118/
76/81/194/131/55/200/105 (hello_gdi=88 e hello_input=105 no modo injetado,
como calibrado no G22). 17/17 da bateria verdes; nenhum binário alterado.

## 11. hello_gl11.exe preservado

**exited=1 rc=42 log=265** — cadeia imutável completa.

## 12. hello_sse.exe preservado

**exited=1 rc=7 log=209** (sucesso projetado).

## 13. hello_gl12.exe — avançou ALÉM de 0x1400029c6

Duas execuções consecutivas, saídas **byte-idênticas** (`diff` vazio):

```
step[0] falhou
EXECUTION STOPPED
Reason: instrucao fora do subconjunto x64 (opcode 0F DB)
Module: app.exe
Function: <instrução x64>
Address: 0x00FE19CB
Architecture: x86-64
Technical: rip=0xFE19CB opcode=0x0F addr=0x00000000 bytes=66 0F DB C2 66 0F FC C1
```

O RIP saiu de `0x00FE19C6` para `0x00FE19CB` (+5 bytes = o tamanho exato do
`psrlw`): o `movdqu` + `psrlw` do laço executaram e o executor parou na
instrução seguinte. Critério "avançar além de 0x1400029c6" cumprido.

## 14. PRÓXIMO BLOCKER REAL — registrado, NÃO implementado

* **Instrução**: `PAND xmm0, xmm2` — `66 0F DB C2` (packed bitwise AND,
  SSE2).
* **Opcode**: `0F DB` (prefixo 66).
* **RIP**: `0x00FE19CB`. **VA/RVA**: `0x1400029cb` (objdump do G22:
  `1400029cb: 66 0f db c2  pand %xmm2,%xmm0`).
* **Bytes**: `66 0F DB C2` (e `66 0F FC C1` logo em seguida = `PADDB xmm0,
  xmm1`, **não alcançado**).
* **Função/contexto**: `gen_tex()` (laço vetorizado `movdqu→psrlw→pand→paddb`
  da média de bytes `(tmp[i]+64)>>1`), módulo app.exe (hello_gl12.exe).
* **Fase**: execução (step[0]; orçamento de 10.000 instruções/step).
* **Quantidade de instruções**: o diagnóstico de parada não emite a contagem
  exata; a parada é na 3ª instrução do corpo do laço (após `movdqu`+`psrlw`
  do primeiro iter), dentro do primeiro step.

Para o G24: implementar somente `PAND xmm, xmm/m` (66 0F DB) se continuar
sendo o blocker observado; o `PADDB` (66 0F FC) provavelmente será o
seguinte — descobrir pela execução, nunca por antecipação.

## 15. Limitações reais restantes

* O hello_gl12 **não conclui a cena**: parado em `PAND` (item 14), ainda no
  `gen_tex`, antes de qualquer chamada OpenGL.
* Grupo 0F 71 coberto somente na forma `66 0F 71 /2` registrador; PSRAW
  (/4), PSLLW (/6), PSRLD/PSLLD/PSRAD (0F 72), PSRLQ/PSLLQ/PSRLDQ/PSLLDQ
  (0F 73) e as formas Packed-Shift com memória continuam fora (fault
  honesto).
* Não há MXCSR/denormais/rounding envolvidos (operação inteira pura), e
  nenhum estado de flags é afetado — corretamente, pois PSRLW não toca
  RFLAGS.
* Resultados obtidos com os PEs controlados do repositório em Linux/x86-64;
  não declaramos compatibilidade com CPUs/GPUs, drivers ou jogos comerciais.
