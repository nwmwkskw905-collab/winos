# RELATORIO_GRUPO22 — OPENGL32.dll!glShadeModel

Data: 2026-09-24/25. Objetivo do grupo: descobrir e implementar EXCLUSIVAMENTE
`OPENGL32.dll!glShadeModel` (o bloqueador registrado como NEXT BLOCKER no G21),
na menor implementação real possível (GL_SMOOTH + GL_FLAT), sem nenhuma outra
API, terminando no segundo blocker real.

---

## 1. Estado inicial (baseline, registrado ANTES de qualquer alteração)

| Item | Valor |
|---|---|
| make c-test | **3245 verificações, 0 falhas** |
| swift test | **63 tests, 0 failures** |
| Warnings (-Wall -Wextra) | **0** |
| Analyzer (-fanalyzer) | **0** |
| 15 PEs | rc=42; logs 28/43/180/88*/72/75/98/118/76/81/194/131/55/200/105* |
| hello_gl11.exe | rc=42, log=265 |
| hello_sse.exe | rc=7 (sucesso projetado), log=209 |
| OPENGL32.dll | 48 exports reais (G21: glDepthMask = 48º) |
| hello_gl12.exe | PARADO no 1º blocker: `glShadeModel` (registrado no G21) |

(\*ver item 11: `hello_gdi` (88) e `hello_input` (105) rodam no modo injetado.)

## 2. Qual bloqueador foi atacado neste grupo

`OPENGL32.dll!glShadeModel` — exatamente o blocker nº 1 do hello_gl12,
registrado como NEXT BLOCKER no item 17 do RELATORIO_GRUPO21.md. Nenhuma outra
API foi escolhida, inventada ou implementada.

## 3. Evidência ANTES da implementação (fase de resolução de imports)

Execução `./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_gl12.exe`:

```
prepare falhou | EXECUTION STOPPED
Reason: Unsupported Win32 API
Module: OPENGL32.dll
Function: glShadeModel
Address: (—)
Architecture: x86-64
Technical: API conhecida do módulo mas sem implementação
```

Fase: **prepare (resolução de imports)**. Instruções executadas: **0**. RIP/RVA
e bytes: (—) — a parada é antes da execução.

## 4. O que foi implementado (exatamente)

* `pr_gl_state.shade_model` (default `GL_SMOOTH` = 0x1D00, preservado).
* `pr_gl_shade_model(pr_gl_state*, unsigned mode)`: `GL_SMOOTH`/`GL_FLAT`
  (0x1D01) aceitos; qualquer outro enum → `GL_INVALID_ENUM` (0x500) com
  **estado preservado**; sem contexto corrente → silencioso (convenção do
  runtime). Só os dois modos existentes — nada além do observado.
* Rasterizador (`pr_rasterize_triangle`): **gate por `shade_model`** — o
  caminho do hello_gl12 usa `GL_FLAT` ⇒ **cor sólida do último vértice de cada
  triângulo** (provoking vertex = convenção desktop), observável no
  framebuffer; `GL_SMOOTH` (default) mantém a interpolação baricêntrica
  original intacta.
* Wrapper `f_glShadeModel` (pr_win32.c): contexto ausente ou `n<1` →
  `PR_ERR_INVALID`; senão `pr_gl_shade_model`. Export `glShadeModel` = **49º**
  da OPENGL32.dll com IMPL_NOTE documentando os dois modos.

Nada mais foi tocado: nenhuma API nova por precaução, nenhum enum além dos dois
do caminho observado, nenhum modo de shading inventado.

## 5. Arquivos alterados

| Arquivo | Mudança |
|---|---|
| `Sources/PorticoRuntime/src/pr_gl.h` | declaração `pr_gl_shade_model` |
| `Sources/PorticoRuntime/src/pr_gl.c` | defines GL_SMOOTH/GL_FLAT; campo `shade_model`; `pr_gl_shade_model`; gate FLAT/SMOOTH no rasterizador |
| `Sources/PorticoRuntime/src/pr_win32.c` | `f_glShadeModel` + entrada na tabela de exports (49º) |
| `Tests/PorticoRuntimeTests/test_gl11.c` | helper `draw_quad_cv` (quad com color array v0-vermelho/v1-verde/v2-azul/v3-amarelo) + bloco `== G22: glShadeModel ==` |

## 6. Testes adicionados (bloco G22 — 17 verificações, só adições)

1. Wrapper: `pr_win32_call` com `n=0` → `PR_ERR_INVALID`; com `ctx=NULL` →
   `PR_ERR_INVALID`.
2. Sem contexto corrente: chamada silenciosa (`PR_OK`), sem crash.
3. **DEFAULT preservado (GL_SMOOTH) sem nunca chamar a API**: sonda (185,110)
   no triângulo {v0,v1,v2} = interpolação baricêntrica (73±1, 137±1, 45±1).
4. **Caso válido exercitado pelo hello_gl12 — `glShadeModel(GL_FLAT)`**: sonda
   (185,110) = 0x0000FF (cor sólida de v2/azul); sondas (135,130) e (150,130)
   = 0x00FFFF00 (cor sólida de v3/amarelo) — dois triângulos, duas cores
   planas; `glGetError()==0`.
5. `GL_SMOOTH` restaura a interpolação (sonda ≠ cores flat, = (73,137,45)).
6. Enum inválido (0x9999): `glGetError()==0x500`, depois 0; **estado
   preservado** (continua FLAT: sonda = 0x0000FF).

Sem enfraquecer nada; apenas adições.

## 7. Resultado dos testes C

`make c-test` → **3262 verificações, 0 falhas** (3245 do baseline + 17 do G22).

## 8. Resultado dos testes Swift

`swift test` → **Executed 63 tests, with 0 failures**.

## 9. Warnings

Compilação C `-Wall -Wextra -Werror=implicit-function-declaration` (testes e
ferramentas): **0 warnings**.

## 10. Resultado do analyzer

`gcc -std=c11 -fanalyzer -Wall -Wextra -Wno-analyzer-use-of-uninitialized-value`
sobre runtime + testes (`build/analyzer22.err`): **0 diagnósticos**.

## 11. Os 15 PEs continuam byte-idênticos

rc=42 e contagens de log **todas iguais aos contratos**:

| PE | log | PE | log | PE | log |
|---|---|---|---|---|---|
| hello_real | 28 | hello_gl3 | 98 | hello_gl8 | 131 |
| hello_user | 43 | hello_gl4 | 118 | hello_gl9 | 55 |
| hello_app | 180 | hello_gl5 | 76 | hello_gl10 | 200 |
| hello_gdi | 88\* | hello_gl6 | 81 | hello_input | 105\* |
| hello_gl | 72 | hello_gl7 | 194 | | |

\*Procedimento da bateria (esclarecido neste grupo, NÃO é regressão): os
binários são os mesmos (sha256 estável sob `make c-test`, verificado
antes/depois; runs determinísticos). A matriz completa modo×tool mostrou:
13 PEs idênticos nos dois modos; **`hello_gdi` = 70 sem eventos e 88 no modo
injetado** (as 18 entradas são o caminho de tratamento de mensagens — sem
injeção elas não existem por construção); **`hello_input` = 105 no modo
injetado** (sem injeção não completa). Os contratos (88/105) referem-se aos
runs injetados. `hello_gl11.exe` atual: sha256 `e2a573e4…`, 131128 B (o
registro `9ae336b1…`/131232 B era do build do G20, anterior ao rebase do
fonte no G21).

## 12. hello_gl11.exe preservado

**exited=1 rc=42 log=265** — cadeia imutável completa (31/140 · 32/141 ·
39/148 · 40/149 · 51/249 · 53/251 · 59/257 · 60/258 · 42/265). Testes C do
hello_gl11 (G15–G21) intactos e verdes; nenhum teste antigo alterado.

## 13. hello_sse.exe preservado

**exited=1 rc=7 log=209** (sucesso projetado).

## 14. hello_gl12.exe após a implementação — PARADO no 2º blocker

Re-executado após o G22: o prepare **passou** (imports resolvidos — o blocker
`glShadeModel` foi resolvido no plano de imports e validado por testes de
comportamento no framebuffer, item 6) e a cena **começou a executar**, mas parou
em um **blocker de CPU** (item 17). Não avançou até a fase GL — a parada é
anterior, dentro do `gen_tex()` (geração da textura), que roda antes de
qualquer chamada OpenGL.

## 15. Número de instruções executadas (hello_gl12)

A parada ocorre **dentro do primeiro step** de execução (orçamento de 10.000
instruções/step; o diagnóstico de parada não emite a contagem exata). A
instrução bloqueadora está no laço de `gen_tex`, antes de qualquer chamada GL.

## 16. Determinismo

Duas execuções consecutivas do hello_gl12 com o mesmo binário:
**saídas byte-idênticas** (`diff` vazio), incluindo RIP, opcode e bytes.

## 17. PRÓXIMO BLOCKER REAL (2º) — REGISTRADO, NÃO IMPLEMENTADO

```
step[0] falhou
EXECUTION STOPPED
Reason: instrucao fora do subconjunto x64 (opcode 0F 71)
Module: app.exe
Function: <instrução x64>
Address: 0x00FE19C6
Architecture: x86-64
Technical: rip=0xFE19C6 opcode=0x0F addr=0x00000000 bytes=66 0F 71 D0 01 66 0F DB
```

* **Tipo**: instrução x64 não suportada (prioridade 1 para descoberta).
* **Instrução**: `PSRLW xmm0, 1` — `66 0F 71 D0 01` (grupo 0F 71 /2, imediato
  Ib=1): deslocamento lógico à direita por palavra (packed word) em XMM.
* **Endereço/RVA**: RIP `0x00FE19C6`; VA no PE `0x1400029c6` (objdump:
  `1400029c6: 66 0f 71 d0 01  psrlw $0x1,%xmm0`).
* **Bytes no ponto da parada**: `66 0F 71 D0 01` (e `66 0F DB` a seguir, não
  alcançado).
* **Fase**: execução (step[0]).
* **Contexto**: laço do `gen_tex()` vetorizado pelo gcc 14 -O2 —
  `movdqu (%r15,%rax,1),%xmm0` → `psrlw $0x1,%xmm0` → `pand %xmm2,%xmm0` →
  `paddb %xmm1,%xmm0` = a média de bytes `(tmp[i]+64)>>1` da fusão de pixels.
* **Prováveis bloqueadores seguintes no MESMO laço** (para descobrir pela
  execução, nesta ordem, no G23+): `66 0F DB` = `PAND xmm,xmm/m`; `66 0F FC` =
  `PADDB xmm,xmm/m` (grupo dos SSE2 packed logic/arith inteiros).

Para o G23: implementar somente a instrução que a execução mostrar primeiro
(`PSRLW` imediato, se continuar sendo ela), no menor subconjunto real, e parar
no próximo gap real.

## 18. Limitações reais restantes

* O hello_gl12 **não conclui a cena**: parado no blocker de CPU do item 17,
  antes da fase GL. O `glShadeModel` está implementado e comprovado por
  testes (item 6), mas a cena ainda não o alcança.
* Shading: apenas `GL_SMOOTH`/`GL_FLAT` (os dois modos clássicos reais);
  nenhum outro estado de shading existe no runtime.
* A janela do PE é superfície interna (bitmap BGRA); não usamos GPU do
  Windows, drivers ou OpenGL do host.
* Resultados obtidos com os PEs controlados do repositório em Linux/x86-64;
  não declaramos compatibilidade com GPUs, drivers ou jogos comerciais.
* Bloqueadores de CPU (SSE2 packed: 0F 71/0F DB/0F FC) seguem fora do
  subconjunto e são parados honestamente com `EXECUTION STOPPED`.
