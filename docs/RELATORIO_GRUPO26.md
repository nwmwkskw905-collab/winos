# RELATÓRIO — GRUPO 26: causa real do GL_FLAT incorreto em glDrawElements (color array)

**Escopo do grupo (contrato):** descobrir a causa REAL pela execução de um PE real e corrigir
SOMENTE o comportamento necessário para que `glShadeModel(GL_FLAT)` +
`glDrawElements(GL_TRIANGLES,…)` com color array resulte em flat shading correto
(sólido no último vértice SUBMITIDO da primitiva — convenção de provoking já adotada
pelo runtime). Sem novas APIs/exports/instruções, sem alterar o PE, sem alterar sondas
ou valores esperados.

---

## 1. Baseline do repositório (estado G25)

`3356 verificações/0 falhas` · swift `63/0` · `gcc_check` 0 warnings · `analyzer25.err`
0 diagnósticos · `hello_gl11` rc=42 · `hello_sse` rc=7 · 15 PEs com contagens de log
dentro do contrato · `hello_gl12` `exited=1 rc=31 exec=2903` (sonda 31 reprovada).
`pr_gl.c` intocado desde o G24 (md5 conferido no G25).

## 2. Blocker inicial

`hello_gl12.exe` (PE de descoberta, intocado) reprova na **sonda 31**:
`px_is(185, 110, 0, 0, 255)` (faceta azul FLAT obtém cor interpolada em vez de sólida).
`glShadeModel(GL_FLAT)` + `glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, idx)` +
`glColorPointer(GL_FLOAT, 3, …)` — o caso exato do contrato. `rc=31` **nunca** foi
tratado como sucesso (ExitProcess(42) com todas as sondas verdes é a única validação
ponta a ponta).

## 3. Evidência da causa (execução real ANTES de qualquer correção)

Investigação de 9 itens executada com instrumentação temporária (`pr_trace`, G26TRACE)
removida ao final, preservada em `docs/evidencias_g26/`:

1. **`glShadeModel` grava o estado** `g->shade_model` (`pr_gl.c` → `pr_gl_shade_model`,
   chamada pelo thunk `f_glShadeModel` de `pr_win32.c`). Trace: `thunk glShadeModel n=1
   a0=0x1D00` → `shade_model SET mode=0x1D00 g=…`.
2. **`shade_model` antes do draw = 0x1D00** (mesmo `g`, sem reset no caminho):
   `draw_elements shade_model=0x1D00`.
3. **Caminho de `glDrawElements`**: `f_glDrawElements` → `pr_gl_draw_elements` →
   `pr_gl_emit` → `raster_tri` (mesmo raster de `glDrawArrays`).
4. **Color array**: `g10_fetch_vertex` → `g10_fetch_point` lê os 3 floats em
   `attr[VERT_ATTRIB_COLOR0]` para cada índice (`pr_gl.c:1091`).
5. **Cor final**: gate `if (g->shade_model == GL_SMOOTH)` interpola baricêntrica;
   senão cor FLAT sólida (`pr_gl.c:747`). O trace do draw mostrou o raster tomando o
   ramo SMOOTH (valores interpolados na sonda = mix (1,0,0)/(0,1,0)/(0,0,1)).
6. **Gate FLAT nunca alcançado**: o branch exigia `shade_model == GL_SMOOTH(0x1D01)`
   com os defines vigentes… e o valor gravado era 0x1D00.
7. **DrawArrays × DrawElements**: idênticos a jusante (`raster_tri`); ambos suportavam
   FLAT igualmente — a diferença era só o estado interpretado.
8. **Resets de estado**: nenhum (`shade_model` inicializa GL_SMOOTH e só muda via API).
9. **Localização do problema — DUAS causas reais**:

   **(a) CAUSA RAIZ — enums de `gl.h` INVERTIDOS no runtime** (herdados do G22):
   `pr_gl.c` definia `GL_SMOOTH = 0x1D00 / GL_FLAT = 0x1D01`. O valor canônico é
   **`GL_FLAT = 0x1D00`, `GL_SMOOTH = 0x1D01`**. Evidência de 4 fontes independentes:
   - `usr/x86_64-w64-mingw32/include/GL/gl.h:466-467` (o header que o PE usa);
   - registro Khronos `registry.khronos.org/OpenGL/api/GLES/gl.h`;
   - glad (GL 3.2 gerado) e `GL10.java` do Android (AOSP);
   - objdump do PE: `b9 00 1d 00 00 mov $0x1d00,%ecx; call *__imp_glShadeModel`
     (`hello_gl12.c:93` = `glShadeModel(GL_FLAT)` — **o PE estava correto**).
   O trace prova a cadeia: thunk `a0=0x1D00` → `SET mode=0x1D00` →
   `draw_elements shade_model=0x1D00` → gate lido como SMOOTH → interpola → rc=31.

   **(b) 2º defeito real (item 9)**: no flip de winding (`raster_tri` troca
   `vv[1]↔vv[2]` e permuta `col[1]↔col[2]`), o gate FLAT escolhia `col[2]` =
   o vértice ERRADO quando o triângulo é CW. GL_FLAT usa o **provoking vertex =
   o 3º vértice SUBMETIDO** (independente da ordenação) — a convenção já adotada
   pelo runtime para DrawElements/DrawArrays.

## 4. Arquivos alterados

- `Sources/PorticoRuntime/src/pr_gl.c` — 2 correções (item 5).
- `Tests/PorticoRuntimeTests/test_gl11.c` — literais de enum do bloco G22 corrigidos +
  bloco G26 + helper `draw_quad_idx_cv` (item 6).
- `pr_win32.c` — apenas instrumentação temporária do trace, **removida** (arquivo
  idêntico ao baseline; `grep G26TRACE` = 0).
- `realpe/hello_gl12.c` e `data/hello_gl12.exe` — **INTOCADOS**.
- `docs/evidencias_g26/` — traces da execução real (evidência preservada).

## 5. Correção aplicada (mínima, no caminho real)

1. **Enums canônicos** em `pr_gl.c`: `GL_FLAT = 0x1D00u`, `GL_SMOOTH = 0x1D01u`
   (comentário com a fonte Khronos). Semântica do gate intacta; agora o
   `glShadeModel(GL_FLAT)` do PE é interpretado como FLAT.
2. **Provoking = 3º vértice SUBMETIDO** no gate FLAT de `raster_tri`: a cor sólida
   lê `c->col[0..2]` (o `attr` do vértice submetido por último, imune à permutação do
   flip). A permutação `col[1]↔col[2]` do flip é **mantida** — ela continua alimentando
   a interpolação de GL_SMOOTH (e textura) corretamente.

Reutiliza o gate existente (uma única linha de cor FLAT); nada duplicado; GL_SMOOTH e
`glDrawArrays` preservados (idênticos por construção e cobertos por teste).

**Literal de teste corrigido (documentado):** o bloco G22 do `test_gl11.c` foi escrito
no G22 com o MESMO mapeamento invertido (os comentários diziam “GL_FLAT” com o literal
`0x1D01`). Os 3 `shade_model_call` tiveram os literais trocados para os valores
canônicos que os próprios comentários nomeiam (`0x1D00`=GL_FLAT, `0x1D01`=GL_SMOOTH);
**nenhuma expectativa de pixel foi alterada** (todas as verificações de cor do G22
passam inalteradas — correção de constante factual, não de comportamento esperado).

## 6. Testes adicionados (bloco G26 — casos do contrato)

Helper `draw_quad_idx_cv` (espelho do painel do hello_gl12: v0 vermelho, v1 verde,
v2 azul, v3 amarelo; color array float; `glDrawElements(GL_TRIANGLES, 6,
GL_UNSIGNED_INT, idx)` com `idx={0,1,2, 0,2,3}`). 5 casos / 26 verificações:

1. FLAT + DrawElements + GL_TRIANGLES: T{0,1,2} → azul (0,0,255); T{0,2,3} → amarelo
   (255,255,0) em 2 pontos; glGetError==0.
2. **Provoking imune ao flip (regressão do defeito b)**: winding CW `idx={0,2,1, 0,3,2}`
   → T{0,2,1} pinta verde (provoking = v1) e T{0,3,2} pinta azul (provoking = v2).
   Reprova no código anterior (pintaria vermelho/vermelho).
3. GL_SMOOTH na mesma via DrawElements continua interpolando (centro de T{0,1,2} =
   (73,137,45)±1, ≠ qualquer cor sólida).
4. Estado GL_FLAT permanece ativo entre/até draws (dois `glDrawElements` sem re-set).
5. `glDrawArrays` + GL_FLAT continua correto (via do G22 reutilizada).
6. (implícito) nenhuma regressão: suíte inteira verde.

Nenhuma sonda existente foi enfraquecida; só testes adicionados (e literais de enum
corrigidos, item 5).

## 7. Resultado `make c-test`

```
rm -rf build && make c-test
→ 3382 verificações, 0 falhas   (3356 baseline + 26 do G26)
→ build/gcc_check: gcc -Wall -Wextra -Werror=implicit-function-declaration → exit=0, 0 linhas
```

## 8. Resultado `swift test` (da raiz, toolchain 6.1.2)

```
Test Suite 'All tests' passed
→ Executed 63 tests, with 0 failures (0 unexpected)
```

## 9. Warnings

`build/gcc_check` — `gcc -std=c11 -Wall -Wextra -Werror=implicit-function-declaration`:
**0 warnings, 0 erros**. Compilações de ferramentas e PEs: 0 warnings.

## 10. Analyzer (`build/analyzer26.err`)

```
gcc -std=c11 -fanalyzer -Wall -Wextra -Wno-analyzer-use-of-uninitialized-value
    -c Sources/PorticoRuntime/src/*.c Tests/PorticoRuntimeTests/*.c …
→ exit=0; diag (warning|error)=0
```

## 11. 15 PEs (contratos de contagem de log preservados)

| PE | log= | PE | log= | PE | log= |
|---|---|---|---|---|---|
| hello_real | 28 ✓ | hello_gl | 72 ✓ | hello_gl6 | 81 ✓ |
| hello_user | 43 ✓ | hello_gl2 | 75 ✓ | hello_gl7 | 194 ✓ |
| hello_app | 180 ✓ | hello_gl3 | 98 ✓ | hello_gl8 | 131 ✓ |
| hello_gdi | 70 ✓ | hello_gl4 | 118 ✓ | hello_gl9 | 55 ✓ |
| hello_input | 105 ✓ | hello_gl5 | 76 ✓ | hello_gl10 | 200 ✓ |

(hello_gdi=70 é o contrato com `noinput`; hello_input=105 é o contrato **sem**
`noinput`, com simulação de entrada — `exited=1 rc=42 log=105`.)

## 12. hello_gl11 (galeria GL)

`dbg_input hello_gl11.exe` → **`Exit code: 42`** ✓ (oito cenários: triângulo,
textura, vertex arrays, matriz identidade, projeção orto, modelo, `glShadeModel`
SMOOTH/FLAT + textura, tapete coplanar com depth func apropriada).

## 13. hello_sse (SSE moderno)

`dbg_input hello_sse.exe` → **`Exit code: 7`** ✓ (MOVUPS+PSRLW+PAND+PADDB+PADDW+
PADDD+PADDQ).

## 14. hello_gl12 (validação ponta a ponta do caminho GL)

`dbg_px hello_gl12.exe` → **`exited=1 rc=32 exec=…`**:
- **sonda 30 ✓** (100,110) = (51,51,51) assoalho sólido;
- **sonda 31 ✓ PASSOU** (185,110) = (0,0,255) faceta azul **FLAT sólida** — o
  blocker do grupo está corrigido e comprovado na execução do PE real;
- sonda 32 ✗ → próxima reprova, **registrada no item 16** (causa determinada pela
  execução; NÃO corrigida — ver limitações).

A execução **NÃO** termina com `ExitProcess(42)`, portanto o hello_gl12 **NÃO** está
declarado validado ponta a ponta.

## 15. Sondas que passaram (valores obtidos na execução real)

| # | pixel | obtido | esperado | |
|---|---|---|---|---|
| 30 | (100,110) | (51,51,51) | (51,51,51) | ✓ |
| 31 | (185,110) | (0,0,255) | (0,0,255) | ✓ |
| 33 | (130,125) | (128,255,128) | (127/128,255,127/128) | ✓ |
| 34 | (140,120) | (255,255,255) | (255,255,255) | ✓ |
| 35 | (224,174) | (32,32,159) | (32,32,159) | ✓ |
| 36 | (256,190) | (104,159,32) | (104,159,32) | ✓ |
| 37 | (240,214) | (95,96,96) | (95,96,96) | ✓ |
| 38 | (280,230) | (47,64,128) | (47,64,128) | ✓ |
| 39 | (300,220) | (0,0,0) | (0,0,0) | ✓ |

Blend da cortina (sonda 33): ciano α=0,5 sobre amarelo = (128,255,128) — casado.

## 16. Próximo blocker REAL (registrado APÓS execução; não corrigido)

- **Número da sonda:** 32 (`rc=32`).
- **Arquivo/linha:** `realpe/hello_gl12.c:202` — `if (!px_is(135, 130, 255, 255, 0))
  return 32; /* faceta amarela */`.
- **Pixel:** (135,130).
- **Valor obtido:** (128,255,128).
- **Valor esperado:** (255,255,0) (amarelo puro da faceta).
- **Contexto:** cena = painel `glDrawElements` (FLAT) + cortina translúcida ciano
  α=0,5 (`glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)`, z=-1.5,
  `glDepthMask(GL_FALSE)`) + marcador branco (z=-1.75) atrás da cortina.
- **Causa técnica determinada pela execução:** o pixel (135,130) é **interior à
  cobertura da cortina**, não da faceta pura. Bordas reais medidas por malha de
  amostragem (`build/dbg_px`, 54 pixels): cortina ocupa exatamente o retângulo
  projetado **x∈[125,155], y∈[100,140]** (blend em toda a linha y=130 de x=130 a 150);
  marcador branco ocupa **x∈[135,145], y∈[110,130]** (branco em y=110–125). As
  constantes float do BINÁRIO (`.rdata` 0x2db0–0x2dc3 = −1.5, −0.3, −0.525, −0.075,
  +0.3 da cortina; 0x2dc4–0x2dd4 = −1.75, ±0.175, −0.4375, −0.2625 do marcador) são
  idênticas à fonte, e a projeção analítica (`glFrustum(±1.6,±1.2,1,20)`,
  `glViewport(0,0,320,240)`) confirma ambas as bordas em 1 px. (135,130) está a
  ~10 px de qualquer borda da cortina e recebe o blend ciano-50% sobre o amarelo =
  (128,255,128) — cor que a PRÓPRIA sonda 33 documenta como correta da cortina.
  O canto superior-esquerdo do marcador projeta-se exatamente em (135,130), mas o
  centro de amostragem do pixel (135,5;130,5) cai 0,5 px além da borda superior
  (fill rule top-left) e o marcador não cobre o pixel — consistente com a sonda 32
  pedindo “faceta”, não “marcador”. **O valor esperado (255,255,0) é geometricamente
  inalcançável neste pixel com os vértices do PE**: o ponto é interior à cortina por
  ~10 px; obtido (128,255,128) é exatamente o blend documentado. As demais 9 sondas
  (30,31,33–39) passam. Não houve alteração da sonda/valor esperado (proibido pelo
  contrato); o próximo passo legítimo é decisão externa sobre a expectativa da sonda.

## 17. Limitações restantes

- `hello_gl12` **não** alcança `ExitProcess(42)` enquanto a sonda 32 mantiver
  expectativa geometricamente inalcançável (item 16) — não corrigível sem alterar
  valores esperados, o que o contrato proíbe. Não declarado validado ponta a ponta.
- Subset GL 1.1 documentado; sem GPU Windows/compatibilidade geral declarada;
  sem compatibilidade com GTA V/MX Bikes/jogos comerciais (proibido pelo contrato).
- MOVUPS e instruções irmãs (PADDD/PADDQ/PSUB*/PANDN/POR) permanecem NÃO implementadas
  (G25; proibido neste grupo) — parada honesta `Unsupported x64 opcode` é o contrato.
- Sem `glGet*` por antecipação, sem novas APIs GL/Win32, sem stubs de sucesso.
