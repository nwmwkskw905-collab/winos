# RELATÓRIO — GRUPO 12 (auditoria do runtime + descoberta do próximo bloqueador real)

## 1. Quantidade atual de C checks
**2537 verificações, 0 falhas** (`make c-test`). Base G11: 2436 → +101 de `test_gl5.c` (glScalef: unidade/erros reais, render derivado, PE real, honestidade).

## 2. Quantidade atual de Swift tests
**63 testes, 0 falhas** (`swift test`). SurfaceBridge→BGRA8→Metal não mudou (a escala ocorre na transformação de vértices, antes do framebuffer) — nenhum teste Swift artificial foi criado.

## 3. Warnings
**0** sob `-Wall -Wextra` (verificado arquivo a arquivo com `-fsyntax-only` sobre `src/*.c` + `Tests/*.c` + driver). Corrigido no caminho 1 residual pré-existente: `test_gl3.c` variável `buf` não usada (herança do G10, fora da barra em G11 por contagem enganosa).

## 4. Analyzer findings
**0** (`gcc -fanalyzer` sobre `pr_gl.c`, `pr_win32.c`, `pr_peproc.c`).

## 5. APIs novas implementadas
**Exatamente UMA: `glScalef`** (escala de modelo, `M ← M·S`).
- Motor `pr_gl_scalef`: matriz de escala real, pós-multiplicação no matrix-mode corrente (MODELVIEW ou PROJECTION), `GL_INVALID_OPERATION` (0x502) dentro de `begin`; qualquer valor de float é legal (como o GL — não há erro de faixa).
- Wrapper `f_glScalef` + catálogo `IMPL_FX("opengl32.dll", "glScalef", f_glScalef, 12, 0x07, "3 floats em XMM0-2")` (ABI x64 idêntica ao `f_glTranslatef` certificado).
- Nada mais foi tocado; nenhuma API funcional foi reimplementada; nada é no-op.

## 6. Arquivos modificados
| Arquivo | Mudança |
|---|---|
| `Sources/PorticoRuntime/include/portico/pr_gl.h` | decl `pr_gl_scalef` |
| `Sources/PorticoRuntime/src/pr_gl.c` | corpo real de `pr_gl_scalef` |
| `Sources/PorticoRuntime/src/pr_win32.c` | `f_glScalef` + entrada de catálogo |
| `Tests/PorticoRuntimeTests/test_gl5.c` | **NOVO** (3 suites, 101 checks) |
| `Tests/PorticoRuntimeTests/test_main.c` | `void test_gl5(void); RUN(test_gl5);` |
| `Tests/PorticoRuntimeTests/test_gl3.c` | remoção de `buf` não usado (warn) |
| `realpe/hello_gl5.c` | **NOVO** (PE de descoberta/validação) |
| `Tests/PorticoRuntimeTests/data/hello_gl5.exe` | **NOVO** (MinGW x86_64) |

## 7. PE novo criado
**`hello_gl5.exe`** (`realpe/hello_gl5.c`), compilado com:
```
x86_64-w64-mingw32-gcc -O2 -o Tests/PorticoRuntimeTests/data/hello_gl5.exe realpe/hello_gl5.c -lgdi32 -lopengl32
```
Exercita **`glScalef` isoladamente** (única API nova; sem iluminação/blend/texturas): `glOrtho(-1,1,-1,1,-1,1)` + `glScalef(2, 0.5, 1)` + quad base (−0.4..0.4)² → esperado (−0.8..0.8)×(−0.2..0.2) = px x 32..288, y 96..144. **8 asserts de pixel** (exit 20..27) anti-fraude derivados (ponto-em-retângulo, margens ≥12 px):
(160,120) VERM, (264,120) VERM, (56,120) VERM, (160,156) PRETO, (160,84) PRETO, (312,120) PRETO, (160,130) VERM, (208,156) PRETO. Detecta: no-op (pontos 2–5/8 falham), escala uniforme errada (0,5,0,5 ou 2,2), eixos trocados. Certo → **42**.

## 8. Resultado de cada PE de regressão (bateria pós-implementação)
| PE | rc | |
|---|---|---|
| hello_real.exe | **42** | inalterado |
| hello_user.exe | **42** | inalterado |
| hello_app.exe | **42** | inalterado |
| hello_input.exe | **42** | com injeção determinística (roteiro G7) |
| hello_gdi.exe | **42** | inalterado |
| hello_gl.exe | **42** | inalterado |
| hello_gl2.exe | **42** | inalterado (usa glTranslatef/glRotatef/glDrawElements) |
| hello_gl3.exe | **42** | inalterado (md5 `c6013978cfb22cfb8312dec93aa090dc`) |
| hello_gl4.exe | **42** | inalterado (usa glTranslatef/glDrawElements) |
| **hello_gl5.exe** | **42** | **NOVO** |

## 9. O bloqueador real encontrado
**`OPENGL32.dll!glScalef`** — registrado por execução real ANTES de qualquer implementação (parada honesta no `pr_peproc_prepare`, `hello_gl5.exe` sobre o runtime inalterado):
```
Reason:    Unsupported Win32 API
Module:    OPENGL32.dll
Function:  glScalef
Technical: API conhecida do módulo mas sem implementação
```
Evidência complementar de código: `glScalef` não aparecia em `pr_gl.c`, `pr_gl.h`, `pr_win32.c`, em nenhum teste e em nenhum PE — última função de transformação básica (TRS: translate ✓ rotate ✓ **scale ✗**) ausente. Removido com a menor implementação correta possível (item 5).

## 10. Próximo bloqueador depois deste grupo
**Consultas `glGetString`/`glGetIntegerv`** (o ramo `glGet*` ainda genuinamente ausente — só `glGetError` existe). Todo app 3D real consulta `GL_VERSION`/`GL_EXTENSIONS`/`GL_MAX_TEXTURE_SIZE` no init; hoje a chamada pararia com erro honesto (verificado em `test_gl5_unidade`). Em seguida, na mesma linha GL: `glTexEnvf`, `glShadeModel`, `glMultMatrixf`/`glLoadMatrixf`, `glDepthFunc`/`glDepthMask`. Fora de GL (não forçar OpenGL se atingido primeiro): threads reais de guest (`CreateThread` existe no catálogo — profundidade limitada), CRT `fopen`/`fread`/`fseek` (só `fwrite`/`CreateFile`/`WriteFile` hoje), sincronização. A confirmação exige um novo PE que os alcance — nada foi implementado por antecipação.

## 11. glDrawElements, glTranslatef e glRotatef já estavam implementados?
**SIM — confirmado por código, testes e execução (não reimplementados neste grupo):**
| API | Desde | Motor | Testes | Usada por |
|---|---|---|---|---|
| `glDrawElements` | G9 | `pr_gl_draw_elements` (GL_TRIANGLES+GL_UNSIGNED_SHORT, semântica diferida, erros 0x500/0x501) | `test_gl2.c:124,183-189`, `test_gl3.c:309,324` | hello_gl3, hello_gl4 |
| `glTranslatef` | G9 | `pr_gl_translatef` (pós-multiplicação) | `test_gl2.c:100`, `test_gl3.c:322` | hello_gl3, hello_gl4 |
| `glRotatef` | G9 | `pr_gl_rotatef` (sin/cos reais; eixo nulo = identidade) | `test_gl2.c:191-193` | hello_gl3 |
Os três estão no catálogo `pr_win32.c` (`IMPL_FX`/`IMPL_NOTE`) e nos imports reais dos PEs (objdump). `glBlendFunc/glColor4f/glLightfv/glMaterialfv/glNormal3f/glNormalPointer` (G11) também confirmados funcionais.

## 12. Explicação da inconsistência no relatório do Grupo 11
O §10 e o §23 do `RELATORIO_GRUPO11.md` listaram `glDrawElements/glTranslatef/glRotatef` como "ainda ausentes" e como "próximo bloqueador" — **falso**, contradiz o código (G9) e os relatórios G9/G10 que os documentam como implementados, testados e usados. Causa: o item 23 do G11 foi escrito **por extrapolação** a partir de uma lista genérica de "lacunas GL 1.1 de um app médio", sem re-varrer o catálogo; o inventário do G11 (FASE 1) cobriu apenas as funções que o `hello_gl4.exe` importava (para achar o bloqueador da iluminação/blend), e a previsão final não foi re-auditada contra `pr_win32.c`. Dos 5 candidatos previstos, 3 já existiam; sobravam como lacunas genuínas apenas `glScalef` e `glGet*` — confirmando os únicos pontos válidos da previsão. Este grupo corrigiu o método: **auditoria de código + execução antes de afirmar** (e o relatório G11 permanece como registro histórico, não editado).

---
*Escopo controlado (FASE 6): nada de Direct3D/Vulkan/áudio completo/centenas de APIs; uma API nova apenas; sem código proprietário; não suportado continua em parada honesta/EXECUTION STOPPED. Sem afirmação de compatibilidade com jogos comerciais nem com OpenGL/D3D9 em geral.*
