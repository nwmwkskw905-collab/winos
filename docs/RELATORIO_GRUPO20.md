# RELATÓRIO GRUPO 20 — correção exclusiva das sondas de validação do hello_gl11 (fechamento em rc=42)

Data: 2026-09-24. Objetivo único: corrigir SOMENTE as sondas/asserts de validação
do `hello_gl11` que produziam `rc=31`, levando o PE ao `ExitProcess(42)` natural.
Nenhum arquivo de runtime foi alterado. Metodologia: EXECUTAR → OBSERVAR →
CORRIGIR APENAS A SONDA → REPETIR, um assert por ciclo, com a causa observável
registrada em cada passo.

## 1. Baseline antes

- `make c-test`: 3224 verificações, 0 falhas.
- `swift test` (XCTest): 63 testes, 0 falhas.
- Warnings `-Wall -Wextra`: 0. Analyzer (`-fanalyzer`): 0 saídas.
- 15 PEs históricos: todos rc=42, byte-idênticos (logs 28/43/180/88/72/75/98/
  118/76/81/194/131/55/200/105). `OPENGL32.dll`: 47 exports.
- `glDepthFunc` e `0F 13` MOVLPS/MOVLPD store: implementados e certificados (G18/G19).
- `hello_gl11`: execução completa (1232 instruções), `ExitProcess` honesto, rc=31
  (2 execuções byte-idênticas). Nenhum `EXECUTION STOPPED`; nenhum bloqueador de
  CPU/API.

## 2. Alteração exata feita no hello_gl11

Somente `realpe/hello_gl11.c`, e somente linhas de **validação** (8 pontos),
descobertas uma a uma por execução (8 ciclos, seção 6):

| assert | tipo de correção | de | para |
|---|---|---|---|
| 31 (tapete magenta T1) | coordenada | (220,100) | **(235,85)** |
| 32 (assoalho texel ciano) | coordenada | (100,140) | **(110,155)** |
| 39 (parede gradiente) | valor RGB esperado | (191,191,64) | **(190,190,65)** |
| 40 (parede gradiente) | valor RGB esperado | (32,32,223) | **(31,31,224)** |
| 51 (assoalho texel branco) | coordenada | (220,100) | **(235,85)** |
| 53 (tapete azul T2) | coordenada | (165,148) | **(140,145)** |
| 59 (parede gradiente) | valor RGB esperado | (191,191,64) | **(190,190,65)** |
| 60 (parede gradiente) | valor RGB esperado | (32,32,223) | **(31,31,224)** |

- Nenhuma geometria, ordem de desenho, profundidade, cor de cena ou lógica
  alterada; nenhum assert remido/enfraquecido; nenhuma comparação RGB removida
  (todas continuam exatas; os dois asserts de blend 50% mantêm sua tolerância ±1
  original); nenhum `return 42` artificial.
- `Tests/PorticoRuntimeTests/test_gl11.c` (teste, não runtime): removido o ramo que
  aceitava rc=31; o bloco do PE agora **exige** `exited && exit_code == 42`
  (qualquer outro rc ou `EXECUTION STOPPED` = falha) e valida o frame final na
  superfície com as mesmas correções (235,85 branco; 100,140 e 140,145 azul;
  paredes (190,190,65)/(31,31,224)).
- Regenerado `Tests/PorticoRuntimeTests/data/hello_gl11.exe`
  (`x86_64-w64-mingw32-gcc -O2 realpe/hello_gl11.c -o ... -lopengl32 -lgdi32
  -luser32`; codegen `-O2` preservado — o `movlps %xmm1,0x20(%rsp)` (`0F 13 4C
  24 20`) continua em `0x140001a50`, único do binário).

## 3. Coordenada antiga

- **(220,100)** — usada nos asserts 31 (fase 1) e 51 (fase 2).
- **(100,140)** — assert 32. **(165,148)** — assert 53.
- (Os asserts de parede 39/40/59/60 mantêm a coordenada; o que estava errado era
  o valor RGB esperado — ver seção 5.)

## 4. Coordenada nova

- **(235,85)** — asserts 31 (tapete magenta) e 51 (assoalho texel branco).
- **(110,155)** — assert 32 (assoalho texel ciano).
- **(140,145)** — assert 53 (tapete azul T2).

## 5. Justificativa geométrica

Geometria real do PE (`glFrustum(-1.6,1.6,-1.2,1.2,1,20)`, vp 320x240;
`x_gl=(x/(1.6|z|)+1)*160`, `y_gl=(y/(1.2|z|)+1)*120`; assoalho z=-4 em
x_gl 80..240 × y_gl 80..160, T1 = vértices 0,1,2 (inferior-direita), T2 = 0,2,3
(superior-esquerda), diagonal y=40+x/2; caixa A z=-2: [80..160]×[90..150] (fase 2
A′: [150..230]×[95..155]); caixa B z=-2.5: [220..300]×[90..150]; sombra z=-2.5:
[100..200]×[75..92]; parede z=-5: [40..280]×[60..180]):

- **(220,100) era inalcançável**: o centro do pixel (220.5,100.5) é estritamente
  interior à caixa B (x_gl 220..300 — a borda esquerda cai exatamente na borda do
  pixel 220), desenhada **depois e mais perto** que o tapete/assoalho (z=-2.5 vs
  -4; `z_win` 0,63 vs 0,79) → o pixel mostra sempre a caixa B. Provado
  empiricamente: (220,100)=(0,0,255) na fase 1 (G19) e o assert 51 falhava na
  fase 2 com a caixa B verde.
- **(235,85)**: interior de T1 com folgas ≥4,5 px às bordas (y=80: 5,5 px;
  x=240: 4,5 px; diagonal: 64,8 px); fora da caixa B (centro 85,5 < 90: 4,5 px
  de folga), da sombra (x > 200: 35 px) e de A/A′. T1 fase 1 = tapete magenta;
  T1 fase 2 = assoalho tex2 lado direito (u=0,96875 → texel branco) — mesma
  posição nas duas fases, preservando o espírito do par de asserts 31↔51.
- **(100,140) era inalcançável**: centro (100.5,140.5) interior à caixa A
  [80..160]×[90..150] (verde, z=-2) → nunca mostrava o assoalho (rc=32 observado).
  **(110,155)**: interior de T2 (60 px da diagonal), 5,5 px acima do topo da
  caixa A, fora da sombra (y > 92), 4,5 px abaixo do topo do assoalho; u=0,1875
  → texel ciano. Observado (0,255,255).
- **(165,148) era inalcançável**: centro (165.5,148.5) interior à caixa A′
  [150..230]×[95..155] (vermelha, z=-2) na fase 2 (rc=53 observado).
  **(140,145)**: interior de T2 (31,8 px da diagonal), 9,5 px à esquerda de A′,
  fora da sombra; tapete T2 azul.
- **Paredes (39/40/59/60)**: o rasterizador amostra o **centro do pixel**
  (`pxf = px + 0,5`). Os valores antigos eram os da amostra no canto inteiro:
  (50,90)→t=0,25→(191,191,64) e (265,165)→t=0,875→(32,32,223). Com amostragem
  real: (50.5,90.5)→t=61/240→f2b=(190,190,65) e (265.5,165.5)→t=105,5/120→
  f2b=(31,31,224). Mais: **(191,191,64) não existe em pixel algum** do
  gradiente — r=191 exige t∈(0,24902,0,25294] e b=64 exige t∈[0,24902,0,25294),
  interseção t∈(0,24902,0,25294) → y_centro∈(89,88; 90,35), que não contém
  centro de pixel algum (…, 89,5; 90,5, …). A correção do valor esperado é a
  derivação formal do mesmo gradiente — as comparações continuam exatas.

## 6. Execução do hello_gl11

8 ciclos EXECUTAR → OBSERVAR → CORRIGIR SÓ A SONDA (um assert por execução, sem
mascarar nada):

| ciclo | alteração | rc observado | causa observável |
|---|---|---|---|
| 0 | — (baseline) | 31 | sonda (220,100) coberta pela caixa B |
| 1 | assert 31 → (235,85) | 32 | sonda (100,140) coberta pela caixa A |
| 2 | assert 32 → (110,155) | 39 | valor da parede amostrado no canto inteiro |
| 3 | assert 39 → (190,190,65) | 40 | idem na 2ª amostra de parede |
| 4 | assert 40 → (31,31,224) | 51 | gêmeo da sonda (220,100) na fase 2 |
| 5 | assert 51 → (235,85) | 53 | sonda (165,148) coberta pela caixa A′ |
| 6 | assert 53 → (140,145) | 59 | gêmeo do valor de parede (fase 2) |
| 7 | assert 59 → (190,190,65) | 60 | idem |
| 8 | assert 60 → (31,31,224) | **42** | — |

- `prepare` passa; `glDepthFunc` resolve; `0F 13` executa (draw_box_lit renderiza
  as duas caixas); as duas fases completas; 2× `SwapBuffers`; teardown
  (`glDeleteTextures` do tex2, `wglMakeCurrent(NULL)`, `wglDeleteContext`,
  `ReleaseDC`); **`ExitProcess` honesto** via `msvcrt!exit` em `0x00FE03CF`.
- Nenhum `EXECUTION STOPPED`; nenhuma parada de runtime em nenhum ciclo.

## 7. Exit code

**42** — `ExitProcess` com exit code 42 (2 execuções byte-idênticas:
`exited=1 rc=42 log=265`). Critério de sucesso atingido: **fechamento da
validação do hello_gl11 registrado**.

## 8. Instruções executadas

- `exec=3256` instruções x64 (`steps=1`, sem paradas), até `ExitProcess`
  (diagnóstico completo: `build/dbg_diag` → `exited=1 rc=42 steps=1 exec=3256`).
- Caminho executado inclui: setup de contexto/viewport/glFrustum/glDepthFunc,
  as duas fases da galeria (parede por color array + `glDrawElements`
  `GL_UNSIGNED_INT`, assoalho texturizado `GL_UNSIGNED_SHORT`, tapete coplanar,
  sombra com blend, caixas A/A′/B iluminadas INT+SHORT, HUD, marcador), o
  ciclo real de texturas (`glGenTextures`/`glDeleteTextures`), os 2 swaps e o
  teardown. O `movlps` (`0F 13`) em `0x140001a50` é executado (codegen `-O2`
  confirmado por objdump; caixas renderizadas comprovam o fluxo).

## 9. Observações do framebuffer

Ferramenta de diagnóstico `tools/dbg_px.c` (não é runtime) leu o framebuffer real
do contexto do PE em execução (fase 1); o frame final (fase 2) foi validado na
superfície (pós-swap) pelo `test_gl11`:

- Fase 1: (235,85)=(255,0,255) tapete magenta; (110,155)=(0,255,255) texel
  ciano; (130,110)=(0,255,0) caixa A; (255,120)=(0,0,255) caixa B;
  (275,30)=(255,255,255) marcador; (300,200)=(0,0,0) fundo; parede com
  gradiente vertical puro — (50,85)=(201,201,54), (50,90)=(190,190,65),
  (50,95)=(180,180,75), (50,110)=(148,148,107) — e uniforme na horizontal
  ((45|55|60,90)=(190,190,65)); (265,165)=(31,31,224).
- Fase 2 (superfície, test_gl11): (235,85)=(255,255,255) texel branco;
  (100,140) e (140,145) = (0,0,255) tapete azul T2; (190,125)=(255,0,0) caixa
  A′; (255,120)=(0,255,0) caixa B; HUD e sombra com a tolerância ±1 original;
  (275,30)=(255,0,0) marcador vermelho; paredes (190,190,65) e (31,31,224);
  fundo preto.
- Ausência de alteração indevida: nenhuma geometria/ordem/profundidade tocada;
  os 15 PEs históricos seguem byte-idênticos; `glDepthFunc`/`0F 13`/47 exports
  intactos (itens 14/16).

## 10. Testes C antes/depois

- Antes: 3224 verificações, 0 falhas. Depois: **3232 verificações, 0 falhas**.
- Única mudança em testes: o bloco do `hello_gl11` em `test_gl11.c` (item 2) —
  agora estrito (`rc==42` obrigatório; o ramo que aceitava rc=31 foi **removido**
  conforme o brief) e com as sondas refletidas. Nenhum teste anterior removido
  ou enfraquecido; nenhuma validação trivializada.

## 11. Testes Swift

- Antes: 63 testes, 0 falhas. Depois: **63 testes, 0 falhas** (`Executed 63
  tests, with 0 failures`). Nenhum `.swift` tocado.
- Observação de processo: o toolchain Swift 6.1.2 não persiste entre sessões e
  foi reinstalado com `scripts/setup_toolchain.sh` (idem MinGW via
  `apt-get install gcc-mingw-w64-x86-64`) — limitação registrada no item 17.

## 12. Warnings

- `-Wall -Wextra -Werror=implicit-function-declaration`: **0 warnings** antes e
  depois (`build/gcc.err` vazio em todas as compilações); o build MinGW do PE
  também compilou com 0 avisos.

## 13. Analyzer

- `gcc -fanalyzer` (runtime + testes): **0 saídas** antes e depois
  (`build/analyzer20.err` vazio).

## 14. Resultado dos 15 PEs

- **15/15 com rc=42, byte-idênticos** (duas execuções cada; contadores de log
  idênticos aos do baseline: hello_real 28, hello_user 43, hello_app 180,
  hello_gdi 88, hello_gl 72, hello_gl2 75, hello_gl3 98, hello_gl4 118,
  hello_gl5 76, hello_gl6 81, hello_gl7 194, hello_gl8 131, hello_gl9 55,
  hello_gl10 200, hello_input 105).
- `hello_gl11`: **rc=42 log=265, 2 execuções byte-idênticas** (determinístico).
- `hello_stdio` continua fora dos 15, com rc=5 = código de sucesso projetado.

## 15. Arquivos modificados

- `realpe/hello_gl11.c` — somente linhas de validação (8 pontos do item 2 +
  comentários de justificativa).
- `Tests/PorticoRuntimeTests/data/hello_gl11.exe` — recompilado a partir da
  fonte alterada (MinGW-w64 `-O2`).
- `Tests/PorticoRuntimeTests/test_gl11.c` — bloco do PE: exige rc=42; reflete as
  sondas corrigidas.
- `tools/dbg_px.c` — **novo**, utilitário de diagnóstico de framebuffer (não é
  parte do runtime nem dos testes).
- `docs/RELATORIO_GRUPO20.md` — este relatório.
- **Nenhum arquivo de `Sources/` modificado.**

## 16. Confirmação de que nenhum runtime/API/instrução foi implementado

- `Sources/PorticoRuntime/**` intocado: nenhuma instrução x64 nova (0F 12, 0F 16
  continuam fora do subconjunto), nenhuma API OpenGL/Win32 nova, `glDepthFunc`
  intacto, rasterizador intacto, nenhum bypass, nenhum tratamento por RIP.
- Provas: os 15 PEs históricos byte-idênticos (logs imutáveis); a suíte C verde
  com todos os caminhos certificados preservados; Swift 63/0; 0 warnings;
  0 analyzer; 47 exports preservados; o `hello_gl11` mudou apenas na validação —
  renderiza a mesma cena (as observações de framebuffer do item 9 batem com a
  geometria da fonte inalterada).

## 17. Limitações restantes

- `hello_sse.c` existe como fonte, sem `.exe` construído (fora da bateria).
- Formas load de MOVLPD/MOVLPS (`0F 12`) e MOVHPS (`0F 16`) continuam fora do
  subconjunto x64 (fault honesto) — não atingidas por execução real.
- APIs antecipadas (glDepthMask, glShadeModel, glGet*, glFogf, glTexEnvf/i,
  glMultMatrixf, glLoadMatrixf, glAlphaFunc, glHint, glCullFace, glFrontFace,
  glColorMaterial, glLightModel*, glLineWidth, glPointSize, glTexSubImage2D,
  glCopyTexImage2D, glPolygonMode, glPolygonOffset, glScissor) e DirectX,
  threads e sincronização seguem **não implementados**.
- Nenhuma compatibilidade é declarada com GTA V, MX Bikes ou jogos comerciais
  (não testados); **não** é compatibilidade total com OpenGL 1.1; **não** há uso
  de GPU Windows; visibilidade de GPU via Metal real continua não implementada;
  nenhum código proprietário foi copiado.
- Toolchains (Swift, MinGW) não persistem entre sessões: reinstalar via
  `scripts/setup_toolchain.sh` e `apt-get install gcc-mingw-w64-x86-64`.

---

## DESFECHO FINAL

- **Critério de sucesso ATINGIDO**: `hello_gl11` termina naturalmente em
  `ExitProcess` com **exit code = 42** (2 execuções byte-idênticas,
  `log=265`), sem nenhum `EXECUTION STOPPED` — **validação do hello_gl11
  fechada**.
- Testes C: **3232/0** (antes 3224/0). Swift: **63/0**. Warnings: **0**.
  Analyzer: **0**. 15 PEs: **15/15 rc=42 byte-idênticos**.
- Instruções executadas: **3256** até o ExitProcess (duas fases + 2 swaps +
  teardown).
- Resumo técnico: eram **8 defeitos de validação** do PE (autoria G18), todos de
  sondagem, em 3 classes: (a) sondas dentro de caixas mais próximas (220,100 em
  B; 100,140 em A; 165,148 em A′) — corrigidas para pontos livres das mesmas
  regiões-alvo, com folgas ≥4,5 px; (b) valores de gradiente calculados no canto
  inteiro em vez do centro do pixel (39/40/59/60) — corrigidos pela derivação
  formal da amostragem (`(191,191,64)`/`(32,32,223)` são inatingíveis em pixel
  algum); (c) o gêmeo da sonda (220,100) na fase 2 (assert 51). Cada correção
  foi descoberta por execução (rc observado 31→32→39→40→51→53→59→60→42), com
  causa observável provada por leitura direta do framebuffer real. O runtime
  permaneceu intocado — grupo de correção de validação, não de expansão.
