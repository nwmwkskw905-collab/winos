# RELATÓRIO GRUPO 21 — descoberta e implementação exclusiva do próximo bloqueador real (`glDepthMask`)

Data: 2026-09-24. Objetivo único: avançar o runtime somente até encontrar o
próximo bloqueador real de execução e implementar APENAS esse primeiro
bloqueador. Método: EXECUTAR → OBSERVAR → IDENTIFICAR O PRIMEIRO BLOQUEADOR →
IMPLEMENTAR SOMENTE ELE → REGREDIR → PARAR NO PRÓXIMO. Nenhum runtime foi
modificado sem evidência de execução.

## 1. Baseline antes

- `make c-test`: 3232 verificações, 0 falhas.
- `swift test`: 63 testes, 0 falhas.
- 15 PEs históricos: todos rc=42. `hello_gl11`: rc=42 (log=265).
- Sem `EXECUTION STOPPED`; warnings `-Wall -Wextra` = 0; analyzer = 0;
  `OPENGL32.dll` = 47 exports. Tudo confirmado por execução neste grupo.

## 2. PE usado para descoberta

Dois PEs reais, nesta ordem, cobrindo caminhos nunca exercitados:

1. **`hello_sse.exe`** (utilizado: `realpe/hello_sse.c` existia sem `.exe`):
   aritmética SSE2 real (`volatile` impede fold) — `poly` (mulsd/addsd), laço de
   soma de quadrados (divsd), `sqrt` (sqrtsd), laço float (mulss/addss),
   `(double)fs` (cvtss2sd), comparação (comisd) e `(long)(x*4)` (cvttsd2si),
   com `printf %f/%.2f/%.3f`. **Executou 982 instruções até o fim** com
   `ExitProcess(7)` (7 = código de sucesso projetado: `ip==99 && cmp==1`) —
   ampliou a cobertura e **NÃO encontrou bloqueador** (SSE2 escalar/cvt já está
   no subconjunto).
2. **`hello_gl12.exe`** (criado: `realpe/hello_gl12.c`) — galeria fase 3, um
   passo além do hello_gl11, com 3 caminhos ainda não certificados:
   (a) `gen_tex()`: textura procedural 8×8 com `switch` de 4 modos + fusão
   inteira de pixels (codegen real: tabela de saltos/SSE empacotado);
   (b) prisma facetado com **`glShadeModel(GL_FLAT)`** sobre color array;
   (c) cortina translúcida com **`glDepthMask(GL_FALSE)`** + marcador atrás
   (z=-1.75 entre cortina -1,5 e painel -2) — o marcador só aparece se a
   cortina não gravar profundidade. Mais: assoalho sólido, painel de textura em
   orto (8×8 texels de 8 px) e **9 sondas determinísticas** (cores sólidas ou
   blend ±1, margens ≥3 px, amostragem de centro de pixel — lições do G20).
   Build: `x86_64-w64-mingw32-gcc -O2 -lopengl32 -lgdi32 -luser32` (0 avisos).

## 3. Primeiro bloqueador encontrado

**`OPENGL32.dll!glDepthMask`** — a primeira parada real da execução do
`hello_gl12.exe`, descoberta ANTES de qualquer implementação (a prioridade 1
da descoberta — instruções x64 — foi exercitada primeiro pelo `gen_tex()` e
pelo `hello_sse` e não produziu parada; o primeiro bloqueador real é de API).

## 4. Evidência observável

Execução de `hello_gl12.exe` no loader real (`build/dbg_diag`), parada honesta
registrada verbatim antes da implementação:

```
prepare falhou
EXECUTION STOPPED

Reason:
Unsupported Win32 API

Module:
OPENGL32.dll

Function:
glDepthMask

Address:
(—)

Architecture:
x86-64

Technical:
API conhecida do módulo mas sem implementação
```

- **PE**: `hello_gl12.exe`. **DLL/API**: `OPENGL32.dll!glDepthMask`.
- **Motivo exato**: import não resolvido na fase `prepare` (a API é conhecida
  do catálogo do módulo, mas não tem implementação). **RIP/RVA/bytes**: não se
  aplicam (parada de import, antes da execução de instruções).

## 5. Implementação realizada (somente `glDepthMask`)

Semântica implementada (espelho do padrão certificado do `glDepthFunc`/G18):

- `void glDepthMask(GLboolean flag)` — liga/desliga **escritas no z-buffer**.
  `flag != 0` vale como true (semântica GLboolean; qualquer valor aceito —
  **não há enum**, portanto nenhum `GL_INVALID_ENUM` é inventado).
- Afeta (1) a gravação de profundidade por **fragmento** no rasterizador
  (`if (g->depth_write) g->depth[idx] = z;`) e (2) o `glClear(GL_DEPTH_BUFFER_BIT)`
  (semântica GL real: clear de profundidade respeita a máscara). A
  inicialização de profundidade do `wglCreateContext` NÃO é mascarada
  (não é escrita de usuário).
- Sem contexto corrente → silencioso (padrão da família de estado);
  wrapper com `ctx == NULL` ou `n < 1` → `PR_ERR_INVALID` (honesto).
- Enums/formatos: apenas o booleano; nenhum comportamento além do necessário.
- Catálogo: 48º export de `opengl32.dll` (`IMPL_NOTE` com nota
  "escritas no z-buffer; flag != 0 = true; afeta glClear de profundidade (G21)").
- Caminhos não relacionados mantidos intactos (default `depth_write = 1`
  preserva bit a bit todo o comportamento certificado).

## 6. Arquivos modificados

- `Sources/PorticoRuntime/include/portico/pr_gl.h` — declaração
  `pr_gl_depth_mask` (somente o necessário).
- `Sources/PorticoRuntime/src/pr_gl.c` — campo `depth_write` (+default 1),
  gate da escrita de fragmento, gate do clear de profundidade, função
  `pr_gl_depth_mask`.
- `Sources/PorticoRuntime/src/pr_win32.c` — wrapper `f_glDepthMask` +
  entrada no catálogo (48º export).
- `Tests/PorticoRuntimeTests/test_gl11.c` — bloco `== G21: glDepthMask ==`
  (somente adição).
- `realpe/hello_gl12.c` + `Tests/PorticoRuntimeTests/data/hello_gl12.exe` —
  **novos** (PE de descoberta).
- `Tests/PorticoRuntimeTests/data/hello_sse.exe` — **novo** (build do fonte
  existente; o `.c` não foi alterado).
- `docs/RELATORIO_GRUPO21.md` — este relatório.
- Nenhum outro arquivo de runtime; nenhuma alteração no `hello_gl11` (geometria
  e sondas intactas) nem nos 15 PEs históricos.

## 7. Testes adicionados

Bloco `== G21: glDepthMask ==` em `test_gl11.c` (7 verificações, só adição):

1. **Caso válido principal — write-off + restauração**: RED (longe) grava
   depth; `glDepthMask(0)` + GREEN (perto) aparece mas **não grava**;
   restaura + BLUE (intermediário) passa porque o depth ficou intacto.
2. **Controle write-on**: a mesma terceira camada é rejeitada com a máscara
   ligada (default certificado preservado).
3. **Flag não-booleano** (`glDepthMask(2)`) vale como true.
4. **`glClear` de profundidade respeita a máscara**: clear mascarado não limpa
   (camada seguinte rejeitada pelo depth antigo).
5. **Controle do clear**: com máscara ligada, o clear limpa normalmente.
6. **Casos inválidos**: wrapper sem argumentos (`n < 1`) e sem `ctx` →
   `PR_ERR_INVALID`; sem contexto corrente → silencioso, sem crash.
7. **`glGetError == 0`** ao final (nenhum erro GL novo).
- Segurança de memória: API sem ponteiro ⇒ casos de ponteiro/overflow
  **N/A por construção** (documentado no G18; espelhado aqui).

## 8. Resultados antes/depois

| item | antes | depois |
|---|---|---|
| C checks | 3232/0 | **3245/0** (+7) |
| Swift | 63/0 | **63/0** |
| warnings | 0 | **0** |
| analyzer | 0 | **0** |
| 15 PEs | 15/15 rc=42 byte-idênticos | **15/15 rc=42 byte-idênticos** |
| hello_gl11 | rc=42 (log=265) | **rc=42 (log=265)** |
| exports opengl32 | 47 | **48** |
| hello_gl12 | STOP `glDepthMask` | **STOP `glShadeModel`** (1º resolvido) |
| hello_sse | rc=7 (sucesso projetado) | **rc=7** (log=209, ×2 idêntico) |

## 9. C checks

**3245 verificações, 0 falhas** (antes 3232/0). Nenhum teste anterior removido,
enfraquecido ou alterado; única mudança = o bloco G21 adicionado.

## 10. Swift tests

**63 testes, 0 falhas** (antes e depois). Nenhum `.swift` tocado. Toolchain
6.1.2 reinstalado com `scripts/setup_toolchain.sh` (não persiste entre sessões).

## 11. Warnings

**0** (`-Wall -Wextra -Werror=implicit-function-declaration`); build MinGW dos
PEs novos também com 0 avisos.

## 12. Analyzer

**0 saídas** (`build/analyzer21.err` vazio).

## 13. Resultado dos 15 PEs

**15/15 com rc=42, byte-idênticos** (duas execuções cada; contadores de log
imutáveis: 28/43/180/88/72/75/98/118/76/81/194/131/55/200/105). A implementação
não alterou nenhum caminho certificado.

## 14. Resultado do hello_gl11

**rc=42** preservado (`exited=1 rc=42 log=265`) — geometria e sondas intocadas.

## 15. Quantidade de instruções executadas

- `hello_gl12.exe`: **0** — parada na fase `prepare` (resolução de imports);
  nenhuma instrução do PE chegou a executar (o blocker é de API, não de CPU).
- `hello_sse.exe`: **982** instruções até `ExitProcess(7)`.
- `hello_gl11.exe`: 3256 (medição do G20, preservado).

## 16. Determinismo

- `hello_gl12.exe`: **2 execuções byte-idênticas** (o STOP `glShadeModel`
  reproduz exatamente, campo a campo).
- `hello_sse.exe`: **2 execuções byte-idênticas** (`rc=7 log=209`).
- 15 PEs: 2 execuções cada, byte-idênticos.

## 17. Segundo bloqueador (encontrado — NÃO implementado, conforme a regra)

```
NEXT BLOCKER:
* PE: hello_gl12.exe (realpe/hello_gl12.c)
* RIP/RVA: (—) — parada na fase prepare (resolução de imports; o código do
  PE não chegou a executar)
* bytes: (—) — bloqueador de API/import, não de CPU
* API/instrução: OPENGL32.dll!glShadeModel
* motivo: API conhecida do módulo mas sem implementação (import não resolvido)
* evidência: EXECUTION STOPPED verbatim
  "prepare falhou | Unsupported Win32 API | Module: OPENGL32.dll |
   Function: glShadeModel | Address: (—) | Technical: API conhecida do
   módulo mas sem implementação" — 2 execuções byte-idênticas
```

Este é o ponto de partida do **Grupo 22**. `glShadeModel` **NÃO** foi
implementado neste grupo (regra 12).

## 18. Limitações restantes

- `glShadeModel` não implementado (G22); o `hello_gl12` completa o caminho a
  `rc=42` apenas depois dele (e dos próximos bloqueadores que a execução
  revelar — a cena ainda exercitará `glDepthMask` em runtime, que agora existe).
- Continuam fora do subconjunto x64 (com fault honesto): `0F 12`, `0F 16` e
  demais instruções não exercitadas.
- APIs antecipadas seguem **não implementadas** (glGet*, glFogf, glTexEnv*,
  glMultMatrixf, glLoadMatrixf, glAlphaFunc, glHint, glCullFace, glFrontFace,
  glColorMaterial, glLightModel*, glLineWidth, glPointSize, glTexSubImage2D,
  glCopyTexImage2D, glPolygonMode, glPolygonOffset, glScissor); DirectX,
  threads e sincronização fora de escopo.
- `hello_stdio` (rc=5) e `hello_sse` (rc=7) = códigos de SUCESSO projetados,
  fora da bateria dos 15.
- Nenhuma compatibilidade declarada com GTA V, MX Bikes ou jogos comerciais
  (não testados); **não** é compatibilidade total com OpenGL 1.1; **não** há
  uso de GPU Windows; visibilidade de GPU via Metal real segue não
  implementada; nenhum código proprietário foi copiado.
- Toolchains não persistem entre sessões (reinstalar via
  `scripts/setup_toolchain.sh` e `apt-get install gcc-mingw-w64-x86-64`).

---

## FECHAMENTO EXPLÍCITO

- **PRIMEIRO bloqueador real do grupo**: `OPENGL32.dll!glDepthMask` (import
  não resolvido no `prepare` do `hello_gl12.exe` — parada honesta
  `Unsupported Win32 API`).
- **O que foi implementado**: somente `glDepthMask` — escritas no z-buffer
  (fragmento + `glClear` de profundidade), `flag != 0` = true, sem erros
  inventados, 48º export, 7 testes C novos (válidos, inválidos, controle).
- **O que NÃO foi implementado**: `glShadeModel` e toda a lista antecipada de
  APIs/instruções; nenhum segundo bloqueador; nenhuma alteração de geometria ou
  sondas do hello_gl11; nenhum retorno artificial.
- **Próximo bloqueador real (Grupo 22)**: **`OPENGL32.dll!glShadeModel`**
  (motivo: "API conhecida do módulo mas sem implementação"; PE
  `hello_gl12.exe`; evidência determinística ×2 registrada no item 17).
