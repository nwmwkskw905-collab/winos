# RELATÓRIO — GRUPO 27: validação da sonda 32 do hello_gl12

**Veredito: CASO B — a expectativa da sonda 32 estava incorreta; o runtime está
correto.** Corrigido apenas o ponto de amostragem do teste/probe (a expectativa
`(255,255,0)` foi mantida intacta). `hello_gl12` termina com **`ExitProcess(42)` e
todas as 10 sondas verdes** = **validado ponta a ponta**.

---

## 1. Causa comprovada

`px_is(135, 130, 255, 255, 0)` (“faceta amarela”) amostra um pixel que **a cortina
translúcida cobre**; o valor correto desse pixel é o blend ciano-α=0,5 sobre o
amarelo = **(128,255,128)**, exatamente o obtido e exatamente a cor que a **própria
sonda 33** documenta como correta da cortina. O valor esperado (255,255,0) é
**geometricamente inalcançável** em (135,130): o centro de amostragem `(135,5; 130,5)`
é interior estrito da cortina (≥10 px de qualquer borda) e está 0,5 px **fora** do
marcador branco (borda superior). A expectativa foi escrita acreditando-se que o
pixel mostraria a faceta amarela descoberta — o ponto foi escolhido no canto
superior-esquerdo do marcador (que se projeta exatamente em (135,130)), confundindo-o
com a borda da cortina (que termina só em y=140).

## 2. Evidências utilizadas (todas independentes entre si)

**(a) Reprodução (sem alterar código):** `dbg_px hello_gl12.exe` → `exited=1 rc=32`;
(135,130)=(128,255,128) vs esperado (255,255,0). Ordem das primitivas (fonte+dbg_log):
assoalho (DrawElements, cinza 0.2) → painel facetado (DrawElements+color array,
`glShadeModel(GL_FLAT)`) → cortina (2 triângulos imediatos, `glColor4f(0,1,1,0.5)`,
`glEnable(GL_BLEND)` + `glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`,
`glDepthMask(GL_FALSE)`…`GL_TRUE`) → marcador branco (z=-1.75, blend desligado) →
painel texturizado (ortho). Estado GL relevante: FLAT ativo do início ao fim,
`GL_DEPTH_TEST` ligado, `GL_LESS` (default do runtime), depth-write desligado só na
cortina, `glViewport(0,0,320,240)`, `glFrustum(-1.6,1.6,-1.2,1.2,1,20)`, MV=identidade.

**(b) Geometria analítica (matemática pura, python independente do runtime).**
Projeção: `x_ndc = 0.625·x_eye/w`, `y_ndc = (5/6)·y_eye/w`, `w = −z_eye`; viewport
`vx = (x_ndc+1)·160`, `vy = (y_ndc+1)·120`. Retângulos projetados (todos caem em
**números inteiros exatos**):

| elemento | z_eye | vértices (x,y) | retângulo (vx,vy) |
|---|---|---|---|
| assoalho | −4 | ±3.2, ±1.6 | [80,240]×[80,160] |
| painel | −2 | ±0.8, ±0.5 | **[120,200]×[95,145]** |
| cortina | −1.5 | −0.525..−0.075, ±0.3 | **[125,155]×[100,140]** |
| marcador | −1.75 | −0.4375..−0.2625, ±0.175 | **[135,145]×[110,130]** |

- **Sonda 32, centro (135,5; 130,5)** (regra de amostragem do runtime `pxf=px+0.5`
  — `pr_gl.c:699`): painel SIM (lado amarelo — 25,8 px acima da diagonal
  v0(120,95)–v2(200,145), triângulo {v0,v2,v3} = FLAT provoking v3 = (1,1,0));
  **cortina SIM** (w das 3 arestas de T2 = +495, +285, +420, todas > 0 estrito —
  `edge2` de `pr_gl.c`); **marcador NÃO** (aresta superior TR→TL dá w = −5 < 0
  estrito; diagonal de T1 dá w = −5 < 0). Winding: todos os 8 triângulos CCW
  (áreas +1200/+4000/+200), sem flip; sem clipping (vértices dentro do volume).
- **Sonda 33 (130,125)**: mesma superfície (painel amarelo + cortina, sem marcador)
  = **mesma região visual da sonda 32** (5 px de distância, ambas interiores).
- **Sonda 34 (140,120)**: painel + cortina + **marcador SIM** (w>0 nas 3 arestas) →
  branco sobrescreve (blend já desligado) ✓.

**(c) Composição exata da cortina (tarefa 3).** Cor ciano (0,1,1), α=0,5, ordem:
painel (amarelo) desenhado antes → dst=(1,1,0); cortina depois com
`sf=GL_SRC_ALPHA→sa=0.5`, `df=GL_ONE_MINUS_SRC_ALPHA→0.5` (`pr_gl.c:753-776`):
R = 0·0.5+1·0.5 = 0.5; G = 1·0.5+1·0.5 = 1.0; B = 1·0.5+0·0.5 = 0.5.
Conversão `f2b(v)=(uint8_t)(v·255+0.5)` (`pr_gl.c:187`):
**R=128, G=255, B=128 → (128,255,128) EXATO**, idêntico ao obtido (e ao que a sonda
33 aceita: r∈{127,128}, g=255, b∈{127,128}).

**(d) Marcador (tarefa 4).** Centro (135,5;130,5): a borda superior do marcador está
em vy=130,0; o centro amostrado está 0,5 px além (fora). A regra top-left
(`pr_gl.c:642-647`, inclusiva para a aresta superior) só inclui amostras **sobre** a
borda (w==0) — o centro cai em w=−5 (estritamente fora). Coluna px=135 **é** coberta
(centro 135,5 → w=+10 na aresta esquerda), o que bate com a malha (branco em
(135,110..125)). Tocar a borda ≠ cobrir o pixel — comprovado para ambos os lados.

**(e) Malha empírica de vizinhança (tarefa 5).** 18 amostras — o padrão é coerente
com a geometria, sem exceção:

| pixel | obtido | geometria prevê |
|---|---|---|
| (135,130) | (128,255,128) | blend cortina ✓ |
| (130,125) | (128,255,128) | blend cortina ✓ |
| (140,120) | (255,255,255) | marcador ✓ |
| (135,125) | (255,255,255) | marcador ✓ |
| (140,130)/(130,130)/(135,135)/(125,130) | (128,255,128) | blend cortina ✓ |
| (155,130)/(135,140) | (255,255,0) | fora da cortina (bordas dx/dy) ✓ |
| (170,140)/(165,140)/(175,140)/(170,135) | (255,255,0) | faceta amarela pura ✓ |
| (170,145) | (51,51,51) | acima do painel = assoalho ✓ |
| (185,110) | (0,0,255) | faceta azul (sonda 31) ✓ |

**(f) Verificação formal da correção do teste (tarefa 8.4).** O novo ponto (170,140),
centro (170,5;140,5): painel amarelo (13,9 px acima da diagonal; 4,5 px abaixo da
borda superior; 29,5 px da direita), cortina **fora** por 15,5 px em x, marcador fora
por 25 px, painel texturizado fora. Confirmado empiricamente em 4 amostras vizinhas.

## 3. Arquivos alterados

- `realpe/hello_gl12.c` — **1 linha** (sonda 32): `px_is(135, 130, 255, 255, 0)` →
  `px_is(170, 140, 255, 255, 0)` (expectativa `(255,255,0)` intacta; `return 32`
  intacto; comentário atualizado). **Nenhuma outra linha.**
- `Tests/PorticoRuntimeTests/data/hello_gl12.exe` — rebuild da fonte acima
  (`x86_64-w64-mingw32-gcc -O2 -s -o … -luser32 -lopengl32`; 0 warnings).
  Diff de desmontagem entre o binário A/B (pré-correção) e o final: **exatamente 2
  imediatos** — `mov $0x87,%ecx → $0xAA` (135→170) e `mov $0x82,%edx → $0x8C`
  (130→140) — em `140003038/140003041`. Nada mais no binário.
- `docs/RELATORIO_GRUPO27.md` — este relatório.

## 4. Arquivos NÃO alterados

- `Sources/PorticoRuntime/src/pr_gl.c` e todo o **runtime** (rasterizador, blend,
  depth, GL_FLAT, provoking vertex, `glDrawElements`) — intactos (Caso B determina
  não alterar o runtime).
- `Sources/PorticoRuntime/src/pr_win32.c`, `include/portico/pr_gl.h`, todos os
  `Tests/PorticoRuntimeTests/*.c` (suíte 3382 verificações idêntica).
- `realpe/hello_gl12.c` fora da 1 linha; demais PEs; G25/PADDB/PAND/PSRLW/MOVUPS.
- Relatórios históricos G5–G26.

## 5. Testes executados

| teste | comando | resultado |
|---|---|---|
| C checks | `make c-test` | **3382 / 0** |
| Swift | `swift test` (raiz) | **63 / 0** |
| gcc warnings | `-Wall -Wextra -Werror=implicit-function-declaration` | **0** |
| analyzer | `gcc -std=c11 -fanalyzer …` → `build/analyzer27.err` | **0 diagnósticos** |
| 15 PEs | `dbg_input hello_*.exe` | **15/15 verdes** (28/43/180/70·105/72–200) |
| hello_gl11 | `dbg_diag` | **rc=42** |
| hello_sse | `dbg_diag` | **rc=7** |
| hello_gl12 | `dbg_px`/execução real | **rc=42, 10/10 sondas** |

## 6. Resultados antes/depois

| | antes (G26) | depois (G27) |
|---|---|---|
| sonda 32 | (135,130) obtido (128,255,128) ≠ esperado (255,255,0) → rc=32 | ponto (170,140) obtido **(255,255,0) = esperado** ✓ |
| sondas 30–31, 33–39 | verdes | verdes (inalteradas) |
| hello_gl12 | `exited=1 rc=32` | **`exited=1 rc=42`** |
| runtime | — | **byte a byte idêntico** (nenhuma linha alterada) |
| suíte C/Swift | 3382/0 · 63/0 | 3382/0 · 63/0 |

Nota sobre o `dbg_px` pós-execução: no caminho de sucesso o PE executa
`wglMakeCurrent(NULL,NULL); wglDeleteContext(rc); ReleaseDC(...)` antes do
`return 42` — o teardown destrói a superfície e uma amostragem **após** a saída lê
zeros. Os `return 32` antigos saíam antes do teardown (por isso os pixels “sobreviviam”
ao término). As cores da cena são as mesmas — provado pelo binário A/B (fonte
intacta, cenas idênticas) e pelas 10 sondas do próprio PE, lidas via `glReadPixels`
**durante** a renderização, todas verdes.

## 7. Estado final do hello_gl12

**VALIDADO PONTA A PONTA**: todas as **10 sondas verdes** (30 assoalho, 31 faceta
azul FLAT, 32 faceta amarela FLAT, 33 cortina blend, 34 marcador/depth-mask,
35–38 textura procedural, 39 fundo) + `glGetError()==0` + **`ExitProcess(42)`**.
Compatibilidade declarada apenas para este PE controlado — sem afirmação de
compatibilidade com GTA V, MX Bikes ou jogos comerciais (proibido pelo contrato).

## 8. Próximo blocker real

**NENHUM.** Não há bloqueio restante neste caminho: o PE termina em `42` com todas as
sondas verdes e a regressão completa está verde (itens 5–6). Nenhum problema futuro
foi investigado ou implementado.
