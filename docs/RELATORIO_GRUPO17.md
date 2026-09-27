# RELATORIO_GRUPO17.md — glDeleteTextures (ciclo de vida de objetos de textura)

Data: 2026-09-24 | Grupo 17 | Condução: EXECUTAR → OBSERVAR O 1º BLOQUEADOR REAL
→ IMPLEMENTAR SOMENTE ELE → TESTAR → REGREDIR. Fonte de verdade: código-fonte e
testes deste repositório. Sem GTA V / MX Bikes / qualquer jogo comercial.

## 1) `make c-test` antes/depois

- **Antes (baseline G16, FASE 1 desta sessão):** compilação limpa (`-Wall -Wextra`,
  0 avisos) e **2961 verificações, 0 falhas**.
- **Depois (fechamento do Grupo 17):** compilação limpa (0 avisos) e
  **3001 verificações, 0 falhas** (+40 verificações novas do `test_gl10`,
  sem nenhuma remoção ou alteração de teste anterior).

## 2) Testes Swift

`swift test` (Swift 6.1.2 Linux): **Test Suite 'All tests' — Executed 63 tests,
with 0 failures (0 unexpected)**. Os 63 testes Swift preservados; nenhum teste
Swift novo foi necessário (o caminho de integração PE↔runtime não mudou).

## 3) `clang -Wall -Wextra` (equivalente da casa: `gcc -Wall -Wextra`)

**0 avisos** em todas as compilações desta sessão (`build/gcc.err` vazio; o build
usa `-Wall -Wextra -Werror=implicit-function-declaration`).

## 4) `clang --analyze` (equivalente da casa: `gcc -fanalyzer`)

**0 diagnósticos** (`build/analyzer17.err`, 0 linhas; saída do analyzer: exit 0).

## 5) Bateria dos 14 PEs no baseline (FASE 1, ANTES de qualquer mudança)

14 PEs reais x86-64 executados no loader real até o fim, **todos rc=42 com logs
byte-idênticos ao baseline certificado** (mesma convenção de verificação do
Grupo 15/16): hello_real 28 · hello_user 43 · hello_app 180 · hello_gdi 70 ·
hello_gl 72 · hello_gl2 75 · hello_gl3 98 · hello_gl4 118 · hello_gl5 76 ·
hello_gl6 81 · hello_gl7 194 · hello_gl8 131 · hello_gl9 55 · hello_input 105.

## 6) Resultado do novo `hello_gl10.exe` no baseline (ANTES de implementar)

Compilado com MinGW GCC 14.2 (`x86_64-w64-mingw32-gcc -O2`, sem avisos) e
executado no runtime **baseline intacto (2961/0)**: **EXECUTION STOPPED em
`pr_peproc_prepare`** — nunca houve sucesso falso; o PE não chegou a executar.

## 7) O 1º bloqueador encontrado, com evidência ANTES da implementação

- **DLL/import que falhou:** `OPENGL32.dll` (import `glDeleteTextures`)
- **API:** `glDeleteTextures` (API Win32/OpenGL; não é instrução x86-64)
- **Ponto da execução:** resolução da tabela de imports do `hello_gl10.exe` em
  `pr_peproc_prepare` (pré-execução de código do convidado)
- **Mensagem:** `EXECUTION STOPPED` + `Reason: Unsupported Win32 API` +
  `Module: OPENGL32.dll` + `Function: glDeleteTextures` + `Architecture: x86-64`
- **Exit code:** sem exit code de convidado (parada no prepare; `dbg_input`
  relata a parada honesta)
- **Chamadas anteriores que já passaram:** o loader resolveu com sucesso toda a
  superfície certificada usada pela cena (GetDC/ReleaseDC, wglCreateContext,
  wglMakeCurrent, glViewport, glClear/glClearColor, glMatrixMode/glLoadIdentity/
  glPushMatrix/glPopMatrix/glOrtho/glFrumstum, glLightfv, glMaterialfv,
  glGenTextures, glBindTexture, glTexImage2D, glTexCoordPointer, glVertexPointer,
  glNormalPointer, glColorPointer, glEnableClientState/glDisableClientState,
  glDrawElements, glDrawArrays, glBegin/glEnd/glVertex3f/glColor4f, glEnable/
  glDisable, glBlendFunc, glFinish, glGetError, glReadPixels, SwapBuffers).
  `glDeleteTextures` foi o primeiro (e único) import não suportado efetivamente
  atingido — lacuna confirmada por execução real, não por adivinhação.
- A evidência acima foi registrada **antes** de qualquer linha de implementação
  (a auditoria da FASE 2 confirmou que a API não constava do catálogo de 46
  exports; a execução provou que ela é efetivamente atingida).

## 8) Arquivos exatados modificados

- `Sources/PorticoRuntime/include/portico/pr_gl.h` — +1 declaração
  (`pr_gl_delete_textures`), ao lado do par `pr_gl_gen_textures`.
- `Sources/PorticoRuntime/src/pr_gl.c` — +corpo de `pr_gl_delete_textures`
  (nenhuma outra função alterada).
- `Sources/PorticoRuntime/src/pr_win32.c` — +wrapper `f_glDeleteTextures` e
  +1 entrada `IMPL_NOTE` no catálogo de `opengl32.dll` (46 → 47 exports).
- `Tests/PorticoRuntimeTests/test_gl10.c` — **NOVO** (suite do Grupo 17).
- `Tests/PorticoRuntimeTests/test_main.c` — +registro `RUN(test_gl10)`.
- `realpe/hello_gl10.c` — **NOVO** (PE natural em 2 fases) e
  `Tests/PorticoRuntimeTests/data/hello_gl10.exe` — **NOVO** PE compilado.
- `docs/RELATORIO_GRUPO17.md` — **NOVO** (este relatório).
- Preservados intactos: os 14 PE-fonte `realpe/hello_input..hello_gl9.c` e seus
  binários, `Sources/PorticoCore/Graphics/*` (63 testes Swift), builders v0–v13,
  todos os relatórios históricos (Grupo 8 a Grupo 16).

## 9) Implementação exata adicionada (somente o bloqueador)

**Motor (`pr_gl.c`) — `void pr_gl_delete_textures(pr_gl_state* s, int n, const
uint32_t* names)`, par real de `pr_gl_gen_textures`:**

- sem contexto atual ou `names` NULL → retorno silencioso (espelho exato do gen);
- `n <= 0` → `GL_INVALID_VALUE` (0x501) (espelho exato do gen);
- para cada nome: `0`, `>= GL10_MAX_TEX` ou ainda não criado → **ignorado sem
  erro** (semântica GL 1.1; o nome 0 é o objeto-default não-excluível);
- nome criado → `free` dos texels, slot zerado (volta a ser reusável pelo
  próximo `glGenTextures`) e, se o objeto excluído estava vinculado,
  **`tex_bound` volta a 0** (semântica GL 1.1 exata de desvinculação).

**Wrapper (`pr_win32.c`) — `f_glDeleteTextures` (2 args: count + ponteiro), com
segurança de memória do guest:** `ctx` NULL → `PR_ERR_INVALID`; <2 argumentos →
`PR_ERR_INVALID`; `count > UINT64_MAX/4` → `PR_ERR_RANGE` (anti-overflow do
bloco `count*4`); bloco de nomes não resolvível no guest → `PR_ERR_RANGE`
(nada é excluído); caso contrário resolve o bloco completo `count*4` e chama o
motor. Catálogo: `IMPL_NOTE("opengl32.dll", "glDeleteTextures",
f_glDeleteTextures, 8, "par real do gen (G17)")`.

## 10) Testes adicionados no mesmo grupo (`test_gl10.c`, +40 verificações)

1. **Válido observável:** gen+bind+texImage2D desenha azul; `glDeleteTextures` do
   objeto **vinculado**; desenhar de novo desenhando **branco (cor base)** — se a
   implementação fosse no-op, o triângulo continuaria azul; desvinculação real.
2. **Reciclagem + preservação:** novo `glGenTextures` reusa o slot liberado;
   ciclo gen→bind→texImage→draw volta a desenhar (vermelho) após o ciclo.
3. **Nomes ignorados (GL 1.1):** `0`, fora da faixa (9999) e não criado (7) →
   sem erro e sem destruir a textura atual (observável por desenho); nome
   repetido no mesmo array → 1ª ocorrência exclui, 2ª ignorada, sem erro.
4. **Inválido (motor):** `n < 0` e `n == 0` → `glGetError == 0x501` (espelho do
   gen), nada excluído.
5. **Ponteiro inválido:** endereço fora do guest → `PR_ERR_RANGE`, nada excluído.
6. **Memória insuficiente:** bloco curto (32 bytes pedidos, 12 válidos) →
   `PR_ERR_RANGE`, nada excluído.
7. **Overflow:** `count > UINT64_MAX/4` → `PR_ERR_RANGE` (anti-overflow), sem
   crash; <2 argumentos → `PR_ERR_INVALID`.
8. **Estado:** excluir objeto NÃO vinculado não mexe no binding atual (observado
   por desenho); sem `wglMakeCurrent` → retorno silencioso `PR_OK`.
9. **`glGetError`:** consumido após cada caso (0 após válidos; 0x500/0x501/0x502
   preservados nos caminhos pré-existentes).
10. **hello_gl10.exe no loader real:** `pr_peproc` real → exit **42** + 7
    verificações de pixel do quadro final (incluindo o resíduo da textura
    excluída em cor-base branco e as duas cores da textura nova).

## 11) Resultado do hello_gl10.exe após implementar (FASE 6)

**exit 42** pelo loader real (`pr_peproc`), log determinístico **200** bytes,
`glReadPixels`/superfície conferindo o quadro final da cena em 2 fases:
(115,80) **branco** = resíduo da textura **excluída** em cor-base · (245,80)
verde = textura nova · (300,110) amarelo = textura nova · (130,110) vermelho =
caixa iluminada · (45,45) HUD `#7F7F00` · (275,30) ciano = marcador fase 2 ·
(300,200) preto = fora do quadro. O PE exercita o bloqueador real duas vezes
(liberação de `tex1` no meio da cena e `tex2` no teardown) pelo caminho real de
imports do loader e de CPU — nenhum caminho especial, atalho ou hack.

## 12) Resultado do hello_gl9.exe e dos 14 PEs no mesmo commit (FASE 7)

Bateria completa **15 PEs × rc=42**: hello_real 28 · hello_user 43 · hello_app
180 · hello_gdi 70 · hello_gl 72 · hello_gl2 75 · hello_gl3 98 · hello_gl4 118 ·
hello_gl5 76 · hello_gl6 81 · hello_gl7 194 · hello_gl8 131 · **hello_gl9 55**
(byte-idêntico ao baseline G16, inclusive o anti-truncamento do
`glDrawElements` com `GL_UNSIGNED_INT`) · **hello_gl10 200 (novo)** ·
hello_input 105. Os 14 originais permanecem **byte-idênticos** ao baseline.

## 13) Resultado final de `make c-test`

**3001 verificações, 0 falhas** (compilação limpa, 0 avisos). Swift 63/0,
`gcc -fanalyzer` 0 diagnósticos. Nenhum teste existente foi removido, enfraquecido
ou alterado para mascarar falha; só foi somado.

## 14) Próximo bloqueador registrado para o Grupo 18

**Nenhum bloqueador descoberto nesta execução.** Com `glDeleteTextures`
implementado (única lacuna efetivamente atingida), o `hello_gl10.exe` conclui a
cena inteira e sai **42** — nenhuma segunda API/instrução não suportada foi
atingida. Em obediência à regra de não escolher/declarar API antecipadamente por
parecer provável, nenhuma candidata é nominada aqui; a descoberta do próximo
bloqueador cabe ao Grupo 18, por execução de um PE natural seguinte.

## 15) O que NÃO foi implementado (a implementação NÃO cobre)

- Nenhuma outra API OpenGL/Win32 além de `glDeleteTextures` (o inventário do PE
  foi a única fonte de escopo).
- Não foram tocados: `glGenTextures`/`glBindTexture`/`glTexImage2D` e seus
  wrappers (apenas espelhados), nenhum caminho de desenho, nenhuma API de matriz,
  luz, blend, leitura de estado ou GDI/User32.
- `glDeleteTextures` não possui variantes (ARB/DSA); nenhum alias foi adicionado.
- Não há verificação `in_begin` em `glDeleteTextures` — **espelho deliberado do
  par certificado** `glGenTextures` (que também não verifica): preservar o
  comportamento do par foi priorizado sobre divergência unilateral.

## 16) O que foi evitado reimplementar (proibido e não feito)

- `glDrawElements` (G12) e `glTranslatef`/`glRotatef` (G10) — não tocados; o
  caminho `GL_UNSIGNED_INT` do G16 segue byte-idêntico (hello_gl9 = 55).
- Os 46 exports certificados do catálogo, `glDisableClientState` (G15), o loader
  PE/relocações, a captura XMM, o framebuffer/z-buffer, o `SurfaceBridge` e o
  `MetalGameRenderer` (único renderer Metal) — nenhum refeito.
- Nenhum stub, no-op de sucesso, caminho paralelo de desenho, valor inventado ou
  casamento de saída hackeado; nenhuma interface pública redesenhada.

## 17) Limitações reais observadas (sem afirmar GPU)

- O ciclo de objetos de textura é por **contexto do motor** (slots `1..63`,
  texels em heap do host): não se afirma conformidade total GL 1.1 nem GPU
  Windows; é o subconjunto exercitado pelos PEs reais, verificado por pixels.
- O nome 0 comporta-se como o objeto-default não-excluível (ignorado por
  `glDeleteTextures`, como em GL 1.1), mas a semântica de amostragem dele é a do
  motor certificado (objeto sem definição usa a cor-base).
- `glDeleteTextures`/`glGenTextures` entre `glBegin`/`glEnd` não geram
  `GL_INVALID_OPERATION` (espelho do par certificado — item 15).
- A descoberta de bloqueadores é limitada ao que os PEs exercitam de fato; a
  ausência de um segundo bloqueador no hello_gl10 vale apenas para esta execução
  (item 14).
