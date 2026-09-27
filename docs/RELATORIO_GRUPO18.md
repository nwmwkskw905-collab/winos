# RELATORIO_GRUPO18.md — glDepthFunc (função de comparação do z-buffer)

Data: 2026-09-24 | Grupo 18 | Condução: EXECUTAR → OBSERVAR O 1º BLOQUEADOR REAL
→ IMPLEMENTAR SOMENTE ELE → TESTAR → REGREDIR (FASE 8: um bloqueador por grupo).
Fonte de verdade: código-fonte e testes deste repositório. Sem GTA V / MX Bikes /
qualquer jogo comercial.

## 1) `make c-test` antes/depois

- **Antes (baseline, FASE 1):** **3001 verificações, 0 falhas** (estado fechado
  do Grupo 17).
- **Depois (fechamento do Grupo 18):** **3169 verificações, 0 falhas**
  (+168 verificações novas do `test_gl11`; nenhum teste anterior removido ou
  alterado para mascarar falha).

## 2) Testes Swift antes/depois

- **Antes:** `swift test` — Executed 63 tests, with 0 failures (0 unexpected).
- **Depois:** **Executed 63 tests, with 0 failures (0 unexpected)** — os 63
  testes Swift preservados; nenhum novo necessário (o caminho de integração
  PE↔runtime não mudou).

## 3) Warnings (`gcc -Wall -Wextra -Werror=implicit-function-declaration`)

**0 avisos** antes e depois (`build/gcc.err` vazio em todas as compilações da
sessão; equivalente da casa para `clang -Wall -Wextra`).

## 4) Analyzer (`gcc -fanalyzer`, equivalente de `clang --analyze`)

**0 diagnósticos** antes e depois (`build/analyzer18.err`, 0 linhas; exit 0).

## 5) Baseline dos 15 PEs (FASE 1, antes de qualquer alteração)

15 PEs reais x86-64 no loader real, **todos rc=42 com logs byte-idênticos ao
baseline certificado**: hello_real 28 · hello_user 43 · hello_app 180 ·
hello_gdi 70 · hello_gl 72 · hello_gl2 75 · hello_gl3 98 · hello_gl4 118 ·
hello_gl5 76 · hello_gl6 81 · hello_gl7 194 · hello_gl8 131 · hello_gl9 55 ·
hello_gl10 200 · hello_input 105. Confirmado também que o hello_gl10 preserva o
comportamento certificado de `glDeleteTextures` (ciclo de texturas a 42).

## 6) Descrição do hello_gl11 (`realpe/hello_gl11.c` + `.exe`)

Programa gráfico natural um passo além do hello_gl10: **galeria em 2 fases**
(320×240) com parede de fundo (color array + `glDrawElements`
`GL_UNSIGNED_INT`, gradiente amarelo→azul), assoalho texturizado (2×1
ciano|amarelo, vertex+texcoord arrays + `GL_UNSIGNED_SHORT`), **tapete coplanar
ao assoalho** (decal: o MESMO triângulo do assoalho redesenhado com cor
sólida), sombra translucida (blend 50%), duas caixas iluminadas (vertex+normal
arrays; INT e SHORT; materiais verde/azul), HUD (ortho+blend) e marcador de fase
(color array + `glDrawArrays`). Transição com ciclo real de recursos
(`glDeleteTextures` do revestimento → `glGenTextures` do novo); fase 2 move o
tapete para a outra metade, troca o revestimento (2×1 amarelo|branco) e
reposiciona a caixa; teardown com `glDeleteTextures`. Saídas: 20–22 contexto,
30/50 glGetError, 31–41 e 51–61 asserts de pixel por fase, 42 = sucesso.
**O tapete coplanar exige que o teste de profundidade aceite igualdade** — com
`GL_LESS` puro (o único modo do motor) o decal de profundidade igual seria
descartado; configurar `glDepthFunc(GL_LEQUAL)` é a inicialização natural de
qualquer engine gráfico dessa época para decals. Não há chamada artificial.

## 7) Primeiro bloqueador descoberto

**`OPENGL32.dll!glDepthFunc`** — API OpenGL Win32 (não é instrução x64),
necessária à cena como função de comparação do z-buffer (decals coplanares).

## 8) Evidência antes da implementação

Executado `hello_gl11.exe` no runtime **intocado** (baseline 3001/0 certificado
na FASE 1; auditoria da FASE 2 mediu o catálogo de 46 exports sem
`glDepthFunc`; nenhuma linha de implementação existia):

```
prepare falhou: EXECUTION STOPPED
Reason: Unsupported Win32 API | Module: OPENGL32.dll | Function: glDepthFunc
Address: (—) | Architecture: x86-64
```

- **Import que falhou:** `OPENGL32.dll!glDepthFunc`
- **Local da falha:** `pr_peproc_prepare` — resolução da tabela de imports
  (pré-execução de código do convidado)
- **Exit code:** sem exit de convidado (parada no prepare)
- **Chamadas anteriores que passaram:** o loader resolveu com sucesso toda a
  superfície certificada usada pela cena (GetDC/ReleaseDC, wglCreateContext,
  wglMakeCurrent, wglDeleteContext, glViewport, glClearDepth, glClearColor,
  glEnable/glDisable, glMatrixMode/glLoadIdentity/glPushMatrix/glPopMatrix/
  glOrtho/glFrustum, glLightfv, glMaterialfv, glClear, glGenTextures,
  glDeleteTextures, glBindTexture, glTexImage2D, glColor3f, glColor4f,
  glVertexPointer, glNormalPointer, glColorPointer, glTexCoordPointer,
  glEnableClientState, glDisableClientState, glDrawElements, glDrawArrays,
  glBegin, glEnd, glVertex3f, glBlendFunc, glFinish, glGetError, glReadPixels,
  SwapBuffers). `glDepthFunc` foi o primeiro e único import não suportado
  efetivamente atingido — lacuna provada por execução, não por adivinhação.

## 9) Arquivos exatamente modificados

- `Sources/PorticoRuntime/include/portico/pr_gl.h` — +declaração
  `pr_gl_depth_func`.
- `Sources/PorticoRuntime/src/pr_gl.c` — +enums dos 8 modos (0x0200..0x0207),
  +campo `depth_func` no contexto, +default `GL_LESS` no `wglCreateContext`,
  +função `pr_gl_depth_func`, +comparador `depth_passes` e a linha do z-buffer
  parametrizada (nenhuma outra função alterada).
- `Sources/PorticoRuntime/src/pr_win32.c` — +wrapper `f_glDepthFunc` e +1
  entrada no catálogo (`g_catalog` 46 → **47** exports OPENGL32).
- `Tests/PorticoRuntimeTests/test_gl11.c` — **NOVO** (suite do Grupo 18).
- `Tests/PorticoRuntimeTests/test_main.c` — +registro `RUN(test_gl11)`.
- `realpe/hello_gl11.c` — **NOVO** e
  `Tests/PorticoRuntimeTests/data/hello_gl11.exe` — **NOVO** PE compilado
  (MinGW GCC 14.2, `-O2`, sem avisos).
- `tools/dbg_diag.c` — **NOVO** driver de diagnóstico (roda um PE e imprime
  diagnostico/exit code; sobrevive ao `make`).
- `docs/RELATORIO_GRUPO18.md` — **NOVO** (este relatório).
- Preservados intactos: os 15 PE-fonte históricos e binários
  (`hello_input..hello_gl10`), `Sources/PorticoCore/Graphics/*` (63 testes
  Swift), builders v0–v13, relatórios do Grupo 8 ao Grupo 17.

## 10) Implementação realizada (somente o primeiro bloqueador)

**`void pr_gl_depth_func(pr_gl_state* s, unsigned func)`** — espelho da família
de estado (`pr_gl_enable`): sem contexto → silencioso; aceita exatamente os 8
modos de comparação GL (`NEVER/LESS/EQUAL/LEQUAL/GREATER/NOTEQUAL/GEQUAL/
ALWAYS`, enum 0x0200..0x0207); qualquer outro enum → `GL_INVALID_ENUM` (0x500).
Estado `depth_func` por contexto, inicializado a **`GL_LESS`** no
`wglCreateContext` — o default preserva o comportamento certificado.

**Rastreador parametrizado:** a linha do z-buffer
(`pr_gl.c`, função de rasterização) passou de `z >= depth → descarta`
(`GL_LESS` embutido) para `!depth_passes(g->depth_func, z, depth[idx])`, com
`GL_LESS` implementado como `!(z >= d)` — **expressão original bit a bit**
(inclusive a semântica para NaN). Os demais modos: comparações IEEE diretas
(`==`, `<=`, `>`, `!=`, `>=`; `NEVER` rejeita; `ALWAYS` aceita). As
profundidades do motor estão em espaço janela [0,1] (`z_win=(1-z_olho)/2` no
ortho de teste) e a profundidade do `glClearDepth` participa da comparação como
valor de referência — comportamento observável e testado.

**Wrapper (`pr_win32.c`)** `f_glDepthFunc` (1 argumento de 4 bytes, sem
ponteiro): `ctx` NULL → `PR_ERR_INVALID`; <1 argumento → `PR_ERR_INVALID`;
chama o motor com o enum. Catálogo: `IMPL_NOTE("opengl32.dll", "glDepthFunc",
f_glDepthFunc, 4, "8 modos de comparacao; default GL_LESS (G18)")`.

**NÃO implementado neste grupo (FASE 8):** `glDepthMask` e qualquer outra API
não atingida; e o **segundo bloqueador** descoberto na FASE 6 (item 14).

## 11) Testes adicionados (`test_gl11.c`, +168 verificações)

1. **Preservação do default certificado** (sem nunca chamar `glDepthFunc`):
   igualdade é REJEITADA (o redesenho coplanar não aparece) e menor profundidade
   vence — o comportamento antigo do motor, provado observavelmente.
2. **Caso válido — decal coplanar observável** (o tapete do PE): `GL_LEQUAL`
   aceita o redesenho de MESMA profundidade; a segunda cor aparece. Se fosse
   no-op, a primeira cor continuaria.
3. **Os 8 modos de comparação:** tabela completa 8 modos × 9 pares de
   profundidade (72 casos), com o 1º desenho gravando a profundidade via
   `GL_ALWAYS` e o 2º sob o modo testado — cor final observada em pixel contra
   a tabela de referência.
4. **A profundidade do clear participa:** o mesmo desenho é rejeitado com
   `glClearDepth(1.0)` e aceito com `glClearDepth(0.0)` sob `GL_GREATER`;
   comparação contra profundidade gravada também coberta.
5. **Caso inválido:** enum 0x9999 → `glGetError == 0x500`, consumido em seguida
   (0), com o estado PRESERVADO (`GL_LEQUAL` continua aceitando o coplanar).
6. **Estado inválido:** depth test desligado ignora o modo (sempre desenha,
   observado por pixel); sem `wglMakeCurrent` → retorno silencioso `PR_OK`.
7. **Memória inválida / ponteiro inválido / overflow:** N/A por construção (API
   de 1 inteiro, sem endereço, sem carga de memória — documentado no teste);
   o caso de wrapper aplicável está coberto: <1 argumento → `PR_ERR_INVALID`.
8. **`glGetError`:** consumido após cada caso (0 após válidos; 0x500 após
   inválido, sem sujar o estado seguinte).
9. **hello_gl11.exe no loader real (dual-branch honesto):** hoje valida que o
   prepare PASSA (glDepthFunc resolvido) e que a execução para EXATAMENTE no
   2º bloqueador registrado (`EXECUTION STOPPED` + `0F 13`); quando o Grupo 19
   implementar a instrução, o MESMO teste passa automaticamente a validar o
   caminho completo (exit 42 + 11 pixels do quadro final). Forma de teste da
   casa para "gap registrado" (padrão do G15) — falha se a parada mudar de
   forma sem atualização consciente.

## 12) Resultado do hello_gl11.exe (PARCIAL EXPLICADO)

- **`pr_peproc_prepare` PASSA** — `glDepthFunc` importa e resolve no loader
  real (o 1º bloqueador está destravado no caminho real).
- **A execução completa NÃO chega a 42 neste grupo:** após a implementação do
  primeiro bloqueador apareceu um **segundo bloqueador** e, conforme a FASE 8
  ("PARAR. Não implementar o segundo neste grupo"), nada foi feito sobre ele.
  A execução real para com `EXECUTION STOPPED` na instrução x64 `0F 13`
  (registrada no item 14) ao compilar o material das caixas (`glMaterialfv`) —
  portanto a cena (inclusive o tapete coplanar com `glDepthFunc(GL_LEQUAL)`)
  está implementada e certificada no motor pelos testes unitários, mas o quadro
  final do PE permanece **PARCIAL** até o Grupo 19.
- Nenhum caminho especial, atalho ou hack foi criado para o PE; o PE é código
  real compilado por MinGW e executado pelo loader/CPU reais.

## 13) Resultado dos 15 PEs anteriores (mesmo commit)

**15×42 byte-idênticos ao baseline:** hello_real 28 · hello_user 43 ·
hello_app 180 · hello_gdi 70 · hello_gl 72 · hello_gl2 75 · hello_gl3 98 ·
hello_gl4 118 · hello_gl5 76 · hello_gl6 81 · hello_gl7 194 · hello_gl8 131 ·
hello_gl9 55 · hello_gl10 200 · hello_input 105. O caminho certificado do
`glDrawElements` (`GL_UNSIGNED_SHORT`/`GL_UNSIGNED_INT`) e o ciclo de
`glDeleteTextures` seguem byte-idênticos.

## 14) Próximo bloqueador descoberto (registrado para o Grupo 19)

**SIM — segundo bloqueador descoberto pela execução do hello_gl11 após a
implementação do primeiro (NÃO implementado, conforme FASE 8):**

- **Categoria:** instrução x64 (CPU), em `app.exe` (código do convidado)
- **Instrução:** `0F 13` = **MOVLPD/MOVLPS m64, xmm** (store de 64 bits da
  metade baixa de um registrador XMM para memória)
- **Local:** `RIP=0x00FE0A50` (RVA `0x1a50` no `.text`), bytes
  `0F 13 4C 24 20 FF D6 4C` — `movlps %xmm1, 0x20(%rsp)` após `unpcklps`,
  logo antes de `mov $0x404,%ecx` (`GL_FRONT`) e da chamada `glMaterialfv`:
  é o codegen do MinGW `-O2` para o init de `float mdif[4] = {r,g,b,1}` em
  `draw_box_lit()` do `hello_gl11.c`
- **Mensagem:** `EXECUTION STOPPED` + `Reason: instrução fora do subconjunto
  x64 (opcode 0F 13)` + `Technical: rip=0xFE0A50 opcode=0x0F addr=0x00000000
  bytes=0F 13 4C 24 20 FF D6 4C`
- **Estado:** parada honesta (sem travamento, sem sucesso falso); o
  `test_gl11` dual-branch garante que a parada continue sendo exatamente este
  gap (e valida o caminho 42+pixels automaticamente quando ele for fechado).

## 15) APIs/instruções ainda não implementadas (não afirmadas como prontas)

- **Instrução x64 `0F 13` (MOVLPD/MOVLPS m64, xmm)** — item 14 (único
  bloqueador efetivamente atingido restante no caminho do hello_gl11).
- APIs OpenGL ausentes do catálogo de 47 exports e **não atingidas** por este
  PE (portanto não são bloqueador confirmado, apenas ausentes):
  `glDepthMask`, `glShadeModel`, display lists (`glGenLists`/`glNewList`/
  `glEndList`/`glCallList`/`glDeleteLists`), `glTexEnvf`/`glTexEnvi`, família
  de fog, `glGetFloatv`/`glGetDoublev`, `glMultMatrixf`/`glLoadMatrixf`,
  `glAlphaFunc`, `glHint`, `glCullFace`, `glFrontFace`, `glColorMaterial`,
  `glLightModeli`/`glLightModelfv`, `glLineWidth`, `glPointSize`,
  `glTexSubImage2D`, `glCopyTexImage2D`, `glPolygonMode`, `glPolygonOffset`,
  `glScissor` — **nenhuma delas foi escolhida, declarada bloqueador ou
  implementada por antecipação**.
- Limites internos deliberados de caminhos certificados (não tocados):
  `glDrawElements` com `GL_UNSIGNED_BYTE` → 0x500 (decisão do G16).

## 16) Nenhuma funcionalidade anterior foi reimplementada (confirmação)

- `glDeleteTextures` (G17), `glDrawElements` com `GL_UNSIGNED_INT` e
  `GL_UNSIGNED_SHORT` (G12/G16), `glDisableClientState` (G15), `glTranslatef`/
  `glRotatef` (G10) e os demais 46 exports certificados: **não tocados** — a
  única mudança no caminho existente foi a **parametrização da comparação do
  z-buffer**, com o modo `GL_LESS` reproduzindo a expressão original bit a bit
  (provado pela preservação no teste 1 e pelos 15 PEs byte-idênticos).
- Loader PE/relocações/imports, captura XMM, framebuffer/z-buffer,
  `SurfaceBridge` e `MetalGameRenderer` (único renderer Metal): não refeitos.
- Nenhum stub, no-op de sucesso, valor inventado ou caminho paralelo de
  desenho; nenhum teste removido, enfraquecido ou alterado para mascarar
  falha; nada de proprietário.

## 17) Limitações conhecidas (não afirmar GPU)

- A função de comparação cobre os 8 modos GL sobre as profundidades do motor em
  espaço janela [0,1]; não se afirma conformidade total GL 1.1 nem GPU Windows
  — é o subconjunto exercitado por PEs reais e verificado por pixels.
- `glDepthMask` não existe (a escrita de profundidade é sempre feita quando o
  fragmento passa); `glClearDepth` é clampado a [0,1] pelo motor.
- `glDepthFunc`/`glGenTextures`/`glDeleteTextures` entre `glBegin`/`glEnd` não
  geram `GL_INVALID_OPERATION` — espelho deliberado da família de estado
  certificada (documentado no código e nos testes).
- O `hello_gl11` completo (exit 42) depende do fechamento do gap `0F 13`
  (Grupo 19); até lá o resultado é PARCIAL (item 12), com o estado do PE
  validado de forma dual-branch pelo `test_gl11`.
- `tools/dbg_diag.c` foi criado como driver de diagnóstico (não é teste nem
  funcionalidade de runtime).
