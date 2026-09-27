# RELATÓRIO — GRUPO 28: descoberta e execução do próximo alvo real

**Desfecho: CASO B — o alvo escolhido (`hello_stdio.exe`) executou completamente,
sem blocker.** Nenhum código foi alterado. O próximo alvo é apenas **indicado**
(não implementado neste grupo, conforme a regra).

---

## 1. Estado inicial (baseline G27, verificado)

```text
C checks:       3382 / 0
Swift tests:      63 / 0
gcc warnings:      0
analyzer:          0
PEs:        15/15 verdes
hello_gl11:      rc=42
hello_sse:       rc=7
hello_gl12:      rc=42
```

## 2. Inventário (ETAPA 1 — sem alterar código)

**PEs disponíveis** (`Tests/PorticoRuntimeTests/data/`, fontes em `realpe/`):
18 EXE + 1 DLL — `hello_real`, `hello_user`, `hello_app`, `hello_gdi`, `hello_gl`
… `hello_gl12`, `hello_input`, `hello_sse`, `hello_stdio`, `hello_dll.dll`.
Varredura do repositório inteiro: **nenhum outro artefato executável** (sem .pxp —
o self-test PXP é payload embutido em `pr_selftest.c`, e não é PE Windows).

**Testes disponíveis**: 36 arquivos C (`test_pe*`, `test_cpu*`, `test_gl*`,
`test_win32*/wincompat`, `test_dll`, `test_vm`, `test_unwind`, `test_vfs`,
`test_zip`, `test_gfx_audio`, `test_surf`, `test_visual`, `test_host`, …) com
3382 verificações + 63 testes Swift (`PorticoCore`/`PorticoApp`).
Ferramentas de execução real: `dbg_diag`, `dbg_input`, `dbg_log`, `dbg_px`.

**Componentes já validados por execução real (PE)**: loader PE32+/relocações/
imports (incl. LoadLibraryA/GetProcAddress/FreeLibrary com `hello_dll.dll`),
CRT real MinGW (hello_real/stdio/app), heap, stdio formatado msvcrt
(printf/fprintf/fwrite/vfprintf via `test_pe_real` no harness peproc), janelas e
mensagens (user32), entrada, GDI (BitBlt/SetPixel/StretchBlt), OpenGL 1.1 subset
(49 exports; hello_gl..gl12 — GL_FLAT/SMOOTH, textura, arrays, DrawElements,
depth func/mask, blend), subset SSE (hello_sse), CPU x64 ampla (ALU/RFLAGS/x87/
pshufd/lock/FS-GS via CRT), processo/saída.

**Componentes ainda NÃO validados por execução real de PE**:
1. **E/S de arquivos do convidado no nível kernel32** (`CreateFileA/W` →
   `ReadFile`/`WriteFile`/`GetFileSize`/`CloseHandle` de arquivo sobre o prefixo)
   — handlers reais implementados (`f_CreateFileA/W`, `f_ReadFile`, `f_WriteFile`)
   e testados **apenas por testes unitários** (`test_win32`/`test_vfs`/
   `test_peproc`); **nenhum PE importa ou chama essas APIs** (verificado por
   varredura de strings de import em todos os binários);
2. MultiByteToWideChar/WideCharToMultiByte, `__chkstk`, SetFilePointer,
   ReadFileEx — TODOs honestos (não implementados);
3. WaitForSingleObject/CreateThread/TlsAlloc (threads/sync/TLS) — TODOs;
4. Registro (advapi32), COM (ole32), sockets (ws2_32), TextOutA/CreateFontA,
   GetSystemMetrics/SetTimer, ShellExecuteA/CommandLineToArgvW — TODOs;
5. msvcrt `fopen/fread/fseek` — fora do subconjunto (não implementados).

**DLLs/APIs implementadas** (`pr_win32.c`): **182 handlers** reais —
kernel32 (arquivos básico, heap, módulos, processo, ambiente, console, tempo),
user32 (janelas/mensagens/paint/timers via PostMessage), gdi32 (DC/bitmap/BitBlt/
SetPixel/GetPixel/StretchBlt/SwapBuffers), msvcrt (CRT acima), opengl32
(49 funções GL/wgl). Catálogo honesto com TODO/UNSUPPORTED para o resto.

## 3. Próximo alvo escolhido — `hello_stdio.exe`

Prioridades do G28, aplicadas em ordem:
1. **Já existe** ✓ — `realpe/hello_stdio.c` → `data/hello_stdio.exe` (PE real
   MinGW, CRT dinâmico);
2. **Exercita parte ainda não validada** ✓ — é o **único EXE do inventário cuja
   execução pelo caminho real das ferramentas nunca ocorreu** neste ciclo (o
   valida apenas o harness unitário `peproc` em `test_pe_real.c`); exercita o
   subsistema msvcrt de stdio formatado com **dois fluxos** — stdout
   (`printf`/`fwrite`/`fflush`/`fputc`) e **stderr (`vfprintf` via `vlogf`)**,
   este sem verificação de conteúdo em nenhum teste — mais a cadeia
   `__iob_func`/`exit`/`_onexit`/`_cexit`;
3. **Pode revelar blocker concreto** ✓ — roteamento de stdout/stderr e handles de
   console no caminho das ferramentas; o runtime mudou 15 grupos desde o `rc=5`
   antigo (risco real de regressão);
4. **Cobertura de apps reais** ✓ — stdio é universal em aplicativos Windows.

**Não** foi criado PE artificial: existe alvo adequado (regra do grupo respeitada).

## 4. Execução (ETAPA 3 — registro completo)

- **Ponto de entrada**: RVA `0x13F0` (PE32+ CUI, subsystem 5.2).
- **Imports/DLLs envolvidas**: `KERNEL32.dll` + `msvcrt.dll` (nenhuma outra).
- **APIs chamadas** (cadeia real observada): CRT `__getmainargs`/`_initterm` →
  `main` → `printf` (formatos `%d/%s/%x`), `fprintf(stdout)`, `fwrite`, `vfprintf`
  (stderr) → `printf("%p")` → `msvcrt!exit` → `ExitProcess`. Contagem do log
  estruturado do runtime: **155 eventos**.
- **Instruções relevantes**: **4.336 instruções x64 interpretadas** (`exec=4336`),
  `steps=1` (fatias de 1000), sem instrução fora do subconjunto.
- **Retorno**: **`rc=5`** = saída **projetada** do PE (`return n > 0 ? 5 : 1`,
  com `n = printf(...) > 0`) — contrato cumprido.
- **Exceções/mensagens de erro/RIP/opcode**: **nenhuma** — diagnóstico do
  encerramento: `Reason: ExitProcess · Module: msvcrt.dll · Function: exit ·
  Address: 0x00FC43CF · Exit code: 5`. Não houve parada honesta nem fault.
- **Estado relevante do runtime**: saída do convidado roteada para a captura de
  console/log (conteúdo validado na suíte: `n=42 s=winos hex=beef`, `f=7 A    3|`,
  `raw-bytes`, `err v 9`, `sum=42`, em `test_pe_real.c:111+`); handles de console
  (PR_WIN32_H_STDOUT/STDERR) exercitados por `f_WriteFile`/família msvcrt.
- **Execuções complementares** (bateria): `dbg_input … noinput` → `exited=1
  rc=5 log=155` (contrato de log = 155).

**Resultado: o alvo executou COMPLETAMENTE (CASO B).**

## 5. Blocker

**Não houver blocker.** Nada foi corrigido (nenhuma alteração de código no G28).

## 6. Regressão (após a execução — completa, tudo verde)

```text
make c-test            → 3382 verificações, 0 falhas
swift test             → Executed 63 tests, with 0 failures
gcc -Wall -Wextra      → 0 warnings (com -Werror=implicit-function-declaration)
gcc -fanalyzer (build/analyzer28.err) → 0 diagnósticos
PE battery (15 PEs)    → 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200 · input 105)
hello_gl11             → rc=42
hello_sse              → rc=7
hello_gl12             → rc=42
hello_stdio            → rc=5  (contrato do alvo, novo neste ciclo)
```

## 7. Estado final

- **Concluído**: inventário completo do projeto (ETAPA 1); escolha justificada do
  alvo (ETAPA 2); execução e registro completos de `hello_stdio.exe` (ETAPA 3);
  regressão total verde (ETAPA 5). O inventário de PEs está **100% executado e
  validado neste ciclo**: 15 PEs na bateria + gl11(42) + sse(7) + gl12(42) +
  stdio(5).
- **Em andamento**: nada (nenhum código alterado; nenhum blocker aberto).
- **Permanece não validado por execução real**: o item 2.1 do inventário —
  **E/S de arquivos do convidado (CreateFileA/W→ReadFile/WriteFile/GetFileSize)**
  — implementada e unit-testada, mas jamais exercitada por um PE; e os TODOs do
  item 2.2–2.5 (sem implementação, por falta de demanda real).
- **Próximo blocker real**: **não existe** (nenhum erro ocorreu).
- **Próximo alvo INDICADO (não executado aqui)**: **descoberta por execução real
  da E/S de arquivos do convidado** — um PE MinGW no modelo dos demais
  (`realpe/`, ex.: `hello_file`) chamando `CreateFileA/WriteFile/ReadFile/
  GetFileSize` sobre o prefixo, para elevar esse subsistema de "unit-testado" a
  "validado por execução real" (prioridades 2–4 do G28). A criação/execução desse
  alvo é decisão do **próximo grupo**.

## 8. Regra final

Nenhum próximo grupo foi criado artificialmente. O próximo grupo deve nascer do
resultado real deste G28: como **não há blocker aberto** e o parque de PEs
existentes está inteiramente validado, a continuação natural é a decisão sobre o
alvo indicado no item 7 (E/S de arquivos) — ou a parada honesta do projeto nesta
fronteira, caso se entenda que criar esse PE configuraria trabalho inventado.
Nenhuma declaração sobre GTA V, MX Bikes ou compatibilidade com jogos comerciais:
o progresso medido aqui é cobertura/validação real do runtime apenas.
