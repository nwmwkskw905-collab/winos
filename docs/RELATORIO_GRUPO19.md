# RELATÓRIO GRUPO 19 — fechamento do primeiro bloqueador x64 real (`0F 13` MOVLPS/MOVLPD)

Data: 2026-09-24. Objetivo: implementar SOMENTE o bloqueador `0F 13` descoberto no
Grupo 18 na execução real de `hello_gl11.exe`, com testes unitários de fronteira e
regressão completa — sem antecipar nenhuma outra lacuna. Toda a metodologia seguiu
EXECUTAR → CONFIRMAR → IMPLEMENTAR SOMENTE ELE → TESTAR → REGREDIR.

## 1. `make c-test` antes/depois

- **Antes**: 3169 verificações, 0 falhas (baseline confirmado por execução).
- **Depois**: **3224 verificações, 0 falhas** (+55 verificações líquidas do Grupo 19).
- Nenhum teste anterior foi removido, enfraquecido ou alterado para mascarar falha;
  os únicos ajustes em testes foram os previstos no brief (novos testes de `0F 13` e
  atualização consciente do bloco dual-branch do `hello_gl11`).

## 2. Swift antes/depois

- **Antes**: XCTest 63 testes, 0 falhas.
- **Depois**: XCTest **63 testes, 0 falhas** (`swift test` da raiz; a suíte
  swift-testing vazia é o comportamento normal da plataforma Linux).
- `Sources/PorticoCore/Graphics/*.swift` não foram tocados.

## 3. Warnings `-Wall -Wextra` antes/depois

- **Antes**: 0. **Depois**: **0** (`build/gcc.err` vazio na compilação completa do
  runtime + testes + ferramentas, com `-Werror=implicit-function-declaration`).

## 4. Analyzer (`gcc -fanalyzer`) antes/depois

- **Antes**: 0 saídas. **Depois**: **0 saídas** (`build/analyzer19.err`).

## 5. Baseline e resultado dos PEs certificados

Bateria atual (`build/dbg_input`, duas execuções cada — saída **byte-idêntica** em
todos; logs = entradas de log):

| PE | rc | log | PE | rc | log |
|---|---|---|---|---|---|
| hello_app | 42 | 180 | hello_gl7 | 42 | 194 |
| hello_gdi | 42 | 88 | hello_gl8 | 42 | 131 |
| hello_gl | 42 | 72 | hello_gl9 | 42 | 55 |
| hello_gl2 | 42 | 75 | hello_gl10 | 42 | 200 |
| hello_gl3 | 42 | 98 | hello_input | 42 | 105 |
| hello_gl4 | 42 | 118 | hello_real | 42 | 28 |
| hello_gl5 | 42 | 76 | hello_user | 42 | 43 |
| hello_gl6 | 42 | 81 | | | |

- **15 PEs com rc=42 byte-idênticos** — o conjunto certificado, preservado.
- `hello_stdio.exe`: rc=**5** byte-idêntico = **código de SUCESSO projetado** do
  próprio PE (`return n > 0 ? 5 : 1`, com `n` = retorno do `printf` validado);
  execução completa (4336 instruções) com `ExitProcess` honesto.
- `hello_gl11.exe`: rc=31 (ver item 10). `hello_dll.dll` é DLL de suporte (carregada);
  `hello_sse.c` existe como fonte mas sem `.exe` construído (fora da bateria).

## 6. Confirmação da execução inicial do `0F 13` (evidência ANTES da implementação)

Execução real de `realpe/hello_gl11.exe`/`data/hello_gl11.exe`, parada honesta
registrada ANTES de qualquer mudança de runtime (re-executada neste grupo,
idêntica):

```
EXECUTION STOPPED | instrução fora do subconjunto x64 (opcode 0F 13) |
Module: app.exe | Address: 0x00FE0A50 |
Technical: rip=0xFE0A50 opcode=0x0F addr=0x00000000 bytes=0F 13 4C 24 20 FF D6 4C
```

- Instrução: `movlps %xmm1, 0x20(%rsp)` (`0F 13 4C 24 20`), **RVA 0x140001a50**,
  codegen MinGW `-O2` de `float mdif[4]={mr,mg,mb,1}` em `draw_box_lit`, imediatamente
  antes de `glMaterialfv` (`mov $0x404,%ecx` = `GL_FRONT` do `glMaterialfv`).
- O `prepare` passava: `glDepthFunc` (Grupo 18) era resolvido pelo loader real.
- Cross-check do binário vigente: `objdump -d` mostra **exatamente** `140001a50:
  0f 13 4c 24 20 movlps %xmm1,0x20(%rsp)` — única instrução `movlp*` do PE, nos
  mesmos RVA/bytes da evidência.

## 7. Arquivos modificados

- `Sources/PorticoRuntime/src/pr_cpu64.c` — handler `op2 == 0x13` dentro de
  `case 0x0F:` (inserido antes do bloco `0x14`), ~15 linhas.
- `Tests/PorticoRuntimeTests/test_cpu64ext.c` — bloco `GRUPO 19` com 9 grupos de
  teste da instrução + guard de escrita negada auxiliar.
- `Tests/PorticoRuntimeTests/test_gl11.c` — bloco do PE atualizado para três ramos
  honestos (ver item 10) com observação direta do framebuffer.
- **NENHUM PE alterado**: `realpe/*` e `data/*` intocados; nenhum `.exe` recompilado
  por este grupo; nenhum caminho/RIP-específico; nenhum NOP/bypass.

## 8. Implementação realizada (somente `0F 13`)

- **MOVLPS (`0F 13`) e MOVLPD (`66 0F 13`) — forma store**: grava os **64 bits
  baixos** do registrador XMM de origem (`reg` do ModRM) em **m64** no endereço
  efetivo do convidado, **exatos 8 bytes, little-endian** — a cópia `get_le`→`mem_write`
  preserva os bytes do registrador byte a byte.
- Somente **forma memória** (como no x64 real): `mod=11` (registrador) → fault honesto
  ("exige operando de memoria"), com opcode/RIP/bytes — encodificação inválida real,
  nunca mascarada como NOP.
- Validação de acesso pela infra existente (`mem_check`): fora do espaço de memória →
  fault com endereço; guard de páginas que nega escrita → fault com endereço.
- Prefixos `F2`/`F3` sobre `0F 13` → fault honesto ("prefixo invalido") — continuam
  fora do subconjunto.
- As duas variantes (com/sem `66`) são idênticas no store (SDM: apenas a sem-66 é
  arquitetural; manter as duas é necessidade de compatibilidade, documentada).
- **NÃO implementado** (continua fora do subconjunto, com fault honesto): `0F 12`
  (MOVUPS/MOVLPD/MOVLPS **load**), `0F 16` (MOVHPS/MOVHLPD/MOVSHPS), e qualquer
  outra instrução não exercitada. Nenhum tratamento por RIP; nenhum caminho
  específico para o `hello_gl11`; instruções certificadas intocadas.

## 9. Testes adicionados (em `test_cpu64ext.c`, seção `GRUPO 19`)

Nove grupos cobrindo todas as classes exigidas (execução de bytes reais + inspeção
de memória/registradores pela API pública `pr_cpu64_*`):

1. **Store correto + little-endian + endereço válido**: a sequência **exata** do PE
   (`0F 13 4C 24 20`) — os 8 bytes gravados casam byte a byte; sentinel seguinte
   intacto (exatos 8 bytes); **preservação** do XMM fonte (128 bits, inclusive
   127:64) e de outro XMM, GPRs e RFLAGS intactos (ausência de corrupção).
2. **Registradores XMM relevantes**: `xmm0`, `xmm9` (REX.R) e `xmm15` (REX.R),
   cada um × 3 bit patterns (zeros, todos-1s, padrão float `{0.5,1.0,0.25,1.0}`
   do `mdif` do PE) — 64 bits baixos sempre corretos.
3. **MOVLPD (`66 0F 13`)**: mesmo store dos 64 bits baixos.
4. **Memória exatamente suficiente**: store nos últimos 8 bytes do espaço (8 de 8) →
   PR_OK e bytes corretos.
5. **Endereço inválido**: store em endereço fora do espaço → `PR_ERR_FAULT`, motivo
   "fora do espaco", `fault.addr` correto, bytes `0F 13` no diagnóstico.
6. **Atravessando o limite de memória**: store em `mem_size-4` (4 de 8 dentro) → fault.
7. **Acesso negado pelo guard**: escrita negada → fault honesto ("escrita negada",
   `fault.addr` correto).
8. **Forma registrador (`0F 13 C1`)**: fault (encodificação inválida real).
9. **Prefixo inválido (`F3 0F 13`)**: fault ("prefixo") — fora do subconjunto.

## 10. Resultado do `hello_gl11` após o fechamento do `0F 13`

- O PE **executou integralmente** (1232 instruções, `steps=1` sem paradas) e
  terminou com **`ExitProcess` honesto** via `msvcrt!exit` — **nenhuma parada
  `EXECUTION STOPPED`**, nenhuma instrução ou API fora do subconjunto atingida.
- **Exit code: 31** = o **assert do próprio PE** (`if (!px_is(220, 100, 255, 0, 255))
  return 31;` do tapete da fase 1). Defeito de **validação** do PE (autoria G18),
  provado por geometria + observação direta do framebuffer real da fase 1 (o PE saiu
  antes do `SwapBuffers`, frame intacto via `glReadPixels`):
  - `(220,100)` está no **1º pixel da caixa B** (x_gl 220..300; o centro do pixel 220
    é 220.5, interior ao quadro) e a caixa B é desenhada **depois e mais perto** que o
    tapete → a sonda do assert 31 é **inatingível por construção**. Observado:
    `glReadPixels(220,100)` = **(0,0,255)** (face iluminada da caixa B).
  - O tapete coplanar está **realmente sendo renderizado**: `glReadPixels(235,85)`
    = **(255,0,255)** (magenta do tapete, sonda limpa) no frame do próprio PE.
  - Conclusão: o `glDepthFunc(GL_LEQUAL)` + decals coplanares **funcionam no frame
    real do PE**; a falha do assert 31 é só da coordenada de validação.
- `test_gl11.c` atualizado em três ramos honestos: (a) parada de runtime → exige
  `EXECUTION STOPPED`; (b) `rc==42` → valida a cena inteira (fase 2); (c) `rc==31` →
  asserta o defeito registrado (`ExitProcess` + código 31) **e** observa o frame real
  (tapete presente em (235,85); cobertura da caixa B em (220,100)). O ramo (b) entra
  automaticamente quando a sonda for corrigida.

## 11. Resultado dos 15 PEs (pós-implementação)

- **15/15 PEs com rc=42, saída byte-idêntica entre duas execuções cada** (tabela do
  item 5 — mesmos contadores de log do baseline: 28/43/180/72/75/98/118/76/81/194/
  131/55/200/105 + gdi 88).
- `hello_stdio` rc=5 (sucesso projetado) byte-idêntico; `hello_gl11` rc=31 (item 10).
- Caminhos certificados preservados: `glDepthFunc`, `glDeleteTextures`,
  `glDrawElements` `GL_UNSIGNED_SHORT`/`INT`, `glDisableClientState`, texturas,
  iluminação/material — todos verdes nos testes (`hello_gl10` log=200 intacto; os
  8 blocos de `test_gl11` verdes; catálogo de **47** exports de `opengl32.dll`
  preservado, `glDepthFunc` no loader).

## 12. Novo bloqueador descoberto

- **NENHUM.** A execução real do `hello_gl11` completa (1232 instruções até
  `ExitProcess`) **sem nenhuma parada** `EXECUTION STOPPED` — não há instrução x64
  nem API Win32/OpenGL não suportada atingida neste grupo. A pendência remanescente
  (rc=31) é o **defeito de validação do próprio PE** (item 10), registrado para o
  Grupo 20 corrigir **somente a sonda** do PE (nada de runtime). Padrão dos grupos
  anteriores: nenhuma API é escolhida/inventada por antecipação.

## 13. APIs/instruções ainda não implementadas (sem antecipação)

- CPU x64: `0F 12` (MOVUPS/MOVLPD/MOVLPS load), `0F 16` (MOVHPS/MOVHLPD/MOVSHPS) e
  todo o restante fora do subconjunto exigido — recusa honesta com opcode/RIP/bytes.
- OpenGL (não atingidas por execução real até aqui): `glDepthMask`, `glShadeModel`,
  `glGetFloatv`, `glGetDoublev`, `glFogf`, `glTexEnvf`, `glTexEnvi`, `glMultMatrixf`,
  `glLoadMatrixf`, `glAlphaFunc`, `glHint`, `glCullFace`, `glFrontFace`,
  `glColorMaterial`, `glLightModeli`, `glLightModelfv`, `glLineWidth`, `glPointSize`,
  `glTexSubImage2D`, `glCopyTexImage2D`, `glPolygonMode`, `glPolygonOffset`,
  `glScissor` — nenhuma foi implementada neste grupo.
- Fora de escopo e NÃO implementados: DirectX, threads, sincronização.

## 14. Confirmação de nenhuma funcionalidade anterior reimplementada

- Nenhuma instrução certificada foi alterada; nenhum stub/no-op/valor inventado;
  nenhuma camada falsa; nenhum PE recompilado ou alterado (`realpe/*`, `data/*`
  verificados); nenhum tratamento por RIP/endereço.
- Prova de preservação: os 15 PEs mantêm rc=42 e contadores de log byte-idênticos
  (incluindo `hello_gl10` log=200 = prova anti-no-op); a suíte C cresceu apenas por
  adição (3169→3224) com todos os testes G5–G18 verdes; Swift 63/0; 47 exports.

## 15. Limitações conhecidas

- `hello_gl11.exe` termina com rc=31 pelo defeito de sonda do assert 31 (atingível
  nunca — coberto pela caixa B); correção prevista = reposicionar a sonda de
  validação do PE (apenas validação; proibido reestruturar a cena para evitar
  codegen).
- `hello_sse.c` existe como fonte, sem `.exe` construído — fora da bateria.
- MOVLPD/MOVLPS: apenas a **forma store**; a forma load (`0F 12`) recusa com fault
  honesto; `0F 13` apenas m64,xmm (forma registrador = encodificação inválida).
- Nenhuma compatibilidade é declarada com GTA V, MX Bikes ou outros jogos
  comerciais (não testados); **não** é compatibilidade total com OpenGL 1.1; **não**
  há uso de GPU Windows; visibilidade real de GPU via Metal continua **não**
  implementada; nenhum código proprietário foi copiado (componente isolado; APIs
  Apple + open source com licenças documentadas em `docs/LICENSES.md`).

---

## DESFECHO FINAL

- **Testes C**: 3224 verificações, 0 falhas (antes: 3169/0).
- **Testes Swift**: 63 testes, 0 falhas (antes: 63/0).
- **Warnings `-Wall -Wextra`**: 0 (antes: 0).
- **Analyzer (`-fanalyzer`)**: 0 saídas (antes: 0).
- **PEs**: 15/15 com rc=42 byte-idênticos (duas execuções cada); `hello_stdio`
  byte-idêntico (rc=5 = sucesso projetado); `hello_gl11` byte-idêntico (rc=31).
- **Exit code do `hello_gl11`**: **31** (ExitProcess honesto após 1232 instruções —
  o `0F 13` fechou; o código 31 é o assert do próprio PE, ver item 10).
- **Próximo bloqueador**: **nenhum** descoberto nesta execução (zero paradas
  `EXECUTION STOPPED`); pendência registrada = defeito de sonda do PE (itens 10/12),
  para o Grupo 20 corrigir somente a validação.
- **Arquivos modificados**: `Sources/PorticoRuntime/src/pr_cpu64.c`,
  `Tests/PorticoRuntimeTests/test_cpu64ext.c`,
  `Tests/PorticoRuntimeTests/test_gl11.c`; criado: este relatório. Nenhum PE alterado.
- **Resumo técnico do comprovado**: o primeiro bloqueador x64 real da suíte —
  `0F 13` (`movlps %xmm1,0x20(%rsp)`, RVA 0x140001a50) — foi fechado com a **forma
  store** MOVLPS/MOVLPD m64,xmm (64 bits baixos, little-endian, exatos 8 bytes,
  validação de memória/guard herdada, fault honesto para forma registrador e
  prefixos inválidos), certificado por 9 grupos de testes de fronteira (incluindo
  endereço inválido, travessia de limite, acesso negado e preservação de bits/regs)
  e comprovado em execução real: `hello_gl11` passou do `EXECUTION STOPPED` em
  `0x00FE0A50` para execução completa até `ExitProcess`, com o tapete coplanar
  (`glDepthFunc(GL_LEQUAL)`) observado magenta no frame real do PE — mantendo
  byte-identicidade dos 15 PEs certificados e zero regressões (3224/63/0/0).
