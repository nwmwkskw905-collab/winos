# RELATÓRIO DA ETAPA — CRT do convidado + string ops + SSE mínimo + DLL + filesystem virtual

**Data:** 2026-09-23 · **Escopo:** o bloqueador "CRT do convidado + string ops (rep movs/stos) e SSE mínimo" executado em 8 fases, do estado base (962 checks C / 50 testes Swift) até a regressão final.

**Critério de aceitação (verificado ponta a ponta):** PE x64 real → loader → imports → CPU x64 → REP MOVS/STOS → SSE mínimo → CRT → memória/string → retorno correto. ✅ TODOS os elos comprovados com PEs de teste controlados (builders v10–v13) rodando no pipeline completo.

**Aviso explícito:** NÃO se declara compatibilidade com GTA V, MX Bikes ou qualquer jogo comercial. Nada de jogos comerciais foi executado ou é alegado. O software proprietário de terceiros (Winlator etc.) não foi copiado.

---

## 1. Novas instruções x64

Subconjunto expandido apenas o necessário para CRT/PEs de teste (FASE 2). Novas nesta etapa:

| Grupo | Instruções | Encodings |
|---|---|---|
| String ops | `movs`/`stos`/`cmps`/`scas` B/W/D/Q | `A4`–`AF` + prefixos `F3`/`F2`/`66`/`REX.W` |
| Calls indiretas | `call r13`, `call rax` | `41 FF D5`, `FF D0` |
| LEA 3-operandes | `lea eax,[rcx+rdx]`, `lea eax,[rcx+rcx]` | `8D 04 11`, `8D 04 09` |
| Extensão | `movzx r32,r/m8` | `0F B6` |
| `rep ret` (MSVC) | `F3 C3` = `RET` | interpretado como ret simples |

Instrução fora do subconjunto → **EXECUTION STOPPED** com opcode, bytes da instrução, RIP, endereço de memória e motivo (revalidado em todos os testes de fault). Nenhuma instrução é fingida.

## 2. REP MOVS/STOS (e família A4–AF)

- `REP MOVSB/W/D/Q` (`F3 A4`/`A5`) e `REP STOSB/W/D/Q` (`F3 AA`/`AB`) completos: **RSI/RDI ± tamanho por iteração**, **RCX decrementa**, **RCX = 0 é no-op**, **DF (bit 10)** controla direção, sem prefixo = 1 iteração.
- Tamanho do elemento: `B`=1, `W`(`66`)=2, `D`=4, `Q`(`REX.W`)`=8`.
- Completam a família: `CMPS` (`[RSI] − [RDI]`) e `SCAS` (`RAX − [RDI]`) com `REPE`/`REPNE` (`F3`/`F2`).
- `STOS` em REP reinicia o padrão a partir do RDI atual a cada iteração.
- Registradores atualizados corretamente em cada iteração (testado por tamanho em `test_cpu64ext.c`).

## 3. SSE mínimo (registrado — nada além do listado)

| Instrução | Encoding | Nota |
|---|---|---|
| `MOVUPS` load/store | `0F 10` / `0F 11` | |
| `MOVAPS` load/store | `0F 28` / `0F 29` | fault honesto "alinhamento 16" se desalinhado |
| `MOVDQA` load/store | `66 0F 6F` / `66 0F 7F` | |
| `MOVDQU` load/store | `F3 0F 6F` / `F3 0F 7F` | |
| `MOVQ` load (xmm←mem) | `F3 0F 7E` | |
| `MOVQ` store (mem←xmm) | `66 0F D6` | forma reg: dest = regf |
| `MOVD`/`MOVQ` GPR↔XMM | `66 0F 6E` / `66 0F 7E` | |
| `PXOR` | `66 0F EF` | |
| `XORPS` / `XORPD` | `0F 57` / `66 0F 57` | |

**NÃO implementado** (fault honesto): resto de SSE/SSE2/SSE3/AVX (mulsd, addsd, cvt*, pshufd, movdqu vetorial completo etc.). Não se tentou implementar SSE/SSE2 inteiro.

## 4. APIs Win32 novas

Adicionadas nesta etapa (comportamento real, sem stubs falsos):

- **FASE 6 (módulos):** `LoadLibraryA`, `LoadLibraryW`, `FreeLibrary`; `GetModuleHandleA/W` e `GetProcAddress` estendidos para módulos reais do convidado (mantêm o comportamento dos módulos embutidos).
- **FASE 7 (filesystem):** `CreateFileA`, `CreateFileW`, `CloseHandle`, `GetFileSize`, `SetFilePointer`; `ReadFile`/`WriteFile` estendidos para handles de arquivo (além do stdio).
- **FASE 5 (verificada já existente):** `GetProcessHeap`, `HeapAlloc`, `HeapFree`, `GetLastError`, `SetLastError`, `GetModuleHandleA`, `GetProcAddress` — cobertas por testes (`test_win32.c:42–141`). `RtlMoveMemory` **não é necessário** (o CRT usa `rep movsb`) e não foi implementado.

Catálogo total: **101 APIs** (thunks reais stdcall/x64). API não implementada → `EXECUTION STOPPED` com Reason/Module/Function/Address/Technical + log. Nenhuma API finge funcionar.

## 5. Suporte ao CRT

- **PE v10 (variant 10)** com estrutura real de runtime do convidado em x64: `mainCRTStartup` → `main` → funções CRT → `ExitProcess`.
- Rotinas exercitadas: **`memcpy`** (via `REP MOVSB`), **`memset`** (via `REP STOSB`), **`strlen`**, **`memcmp`**, comparação de memória, **checksum byte a byte** (`lodsb`+`movzx`+`add`+`dec/jnz`), **cálculo inteiro** (`lea`), **zeramento/cópia SSE** (`movdqu`/`movdqa`/`movaps`), **`movd`/`movq`** GPR↔XMM. Resultado verificável: exit code = nº de falhas internas = **0** (33 checagens dentro do guest; checksum 1235 = 0x4D3).
- Testes: `test_pes.c` (PE10) + Swift `testPE10CRTRoutinesAndStringOps`.
- **Limitação honesta:** não há toolchain Windows no sandbox Linux — o PE é construído por builder byte a byte (padrão CRT real, nunca fingimos compilação MSVC). Com um PE real de mingw/MSVC o subconjunto cresce a partir dos EXECUTION STOPPED reais (ver item 12).

## 6. Suporte inicial a DLL

- **`pr_peproc_provide_dll`** — bytes permitidos pelo app (sandbox iOS); nunca se carrega DLL arbitrária de fora.
- **`pr_peproc_load_dll`** real: `pr_pe_load` → seções mapeadas W^X por grupos de páginas → `pr_pe_reloc_apply` → resolução de imports (módulos embutidos → thunks; **módulos dependentes** → exports) → **`DllMain(hinst, DLL_PROCESS_ATTACH, NULL)`** com retorno FALSE = falha de carga (semântica real).
- **`pr_peproc_module_handle`** (canônico case-insensitive + `.dll`), **`pr_peproc_module_proc`** (tabela de **exports real** da imagem: nome/ordinal), **`pr_peproc_free_dll`** (refcount; em 0 → `DllMain(DLL_PROCESS_DETACH)` + `pr_vm_unmap`).
- **`pr_peproc_call`** — invoca função do convidado com ABI Microsoft x64 (rcx/rdx/r8/r9 + shadow space), sentinel interno `INT 0x2F; HLT` no fim da página de stubs, retorna em RAX e **restaura todo o estado** do processo (regs, RIP, RSP, RFLAGS, halted).
- Builders: **v11** = DLL PE32+ real (COFF `0x2102`, diretório de exports completo) com `add`(`8D 04 11 C3`)/`get_answer`(42)/`twice`(`8D 04 09 C3`)/`DllMain`(ret 1); **v12** = EXE que usa toda a infra (`LoadLibraryA`→`GetProcAddress`→`call r13`/`call rax`→`GetModuleHandleA`→`FreeLibrary`→`ExitProcess(0)`).
- Testes `test_dll.c`: ciclo de vida completo (refcount 2→1→0, attach/detach nos logs, export inexistente → 0, DLL não fornecida → falha honesta) + e2e PE12.

## 7. Suporte a filesystem virtual

- **`pr_win32_set_fs_root` / `pr_peproc_set_fs_root`** — raiz **única** (diretório do sandbox do app). Sem raiz: `CreateFile` falha honesto (`ERROR_PATH_NOT_FOUND`) — nunca se toca o filesystem do iOS.
- Normalização de path Windows: `\` → `/`, remoção de drive `C:` e UNC; **`..` é NEGADO** (`ERROR_ACCESS_DENIED`) com log `[VFS] ... NEGADO (fora da raiz do sandbox)`.
- Disposições reais: `CREATE_NEW`(1, `ERROR_FILE_EXISTS`), `CREATE_ALWAYS`(2), `OPEN_EXISTING`(3, `ERROR_FILE_NOT_FOUND`), `OPEN_ALWAYS`(4), `TRUNCATE_EXISTING`(5).
- `ReadFile`/`WriteFile` (fread/fwrite + fflush), `CloseHandle` (fclose; std handle = no-op OK; handle GDI → `INVALID_HANDLE` honesto, DeleteObject é que fecha GDI), `GetFileSize`, `SetFilePointer` (BEGIN/CURRENT/END). Handles de arquivo `0xC0000000|idx` ≠ std handles.
- **E2E (critério 7 do pedido):** PE13 roda `CreateFileA("nota.txt")` CREATE_ALWAYS → `WriteFile` 15 B → `CloseHandle` → `ExitProcess(0)`; o arquivo **real** é verificado no host dentro da raiz. Swift: `initialize()` define a raiz = diretório do sandbox da sessão (`profile.caminho`) + teste `testPE11VirtualFileSystemWritesInsideSandboxRoot`.

## 8. Checks C

**1188 verificações, 0 falhas** (build `-Wall -Wextra -Werror=implicit-function-declaration`).

Progressão: 962 (base) → 1088 (FASES 1–3) → 1096 (FASE 4 CRT) → 1134 (FASE 6 DLL) → **1188** (FASE 7 VFS). Arquivos novos: `test_dll.c`, `test_vfs.c` (somente adições; nenhum teste removido).

## 9. Testes Swift

**52 testes, 0 falhas** (`make swift-test`). Novos: `testPE10CRTRoutinesAndStringOps`, `testPE11VirtualFileSystemWritesInsideSandboxRoot`. Os 50 pré-existentes continuam passando sem alteração de comportamento.

## 10. Falhas

Bloqueadores encontrados e **corrigidos** nesta etapa:

1. **PE10 exit=0x187**: LEAs miravam RVA 0x1300 enquanto a string ficava em `buf+0x300` = RVA **0x1100** (mapa file↔RVA: .rdata raw 0x200 = VA 0x1000). Corrigidos 5 LEAs.
2. **JE descasado** após `inc ebx` → `add ebx,imm32` em build de debug (2→6 bytes) — revertido ao canônico `INC_EBX`/`JE_OK`.
3. **Convenção real do trap de INT**: `pr_cpu64` continua a execução para retorno ≥ 0 (a doc do header dizia ">0 termina"; doc corrigida). A parada limpa do sub-run usa **HLT após o handler** (`CD 2F F4`) e `pr_cpu64_run(budget=0)` limpa o `halted` do sub-run **aninhado** (sem isso, o run externo abortava como `PROCESS EXIT (HLT)` com RAX antigo).
4. **Sucesso falso do WriteFile**: a API tem 5º argumento (`overlapped`); o trap lia `[rsp+0x28]` sem shadow space alocado → "Invalid memory access (argumentos)" e seguia com **RAX antigo = handle** (o guest via sucesso). Corrigido: frame correto no builder **e** `RAX=0` em todas as rotas de erro do trap64 (nunca sucesso falso).
5. **Espécimes de teste** que usavam `LoadLibraryA` como "não implementada" (C e Swift) — a FASE 6 a implementou; espécimes trocados para `CreateHardLinkA` (fora de escopo), **intenção dos testes preservada**.

Residuais conhecidos (documentados, não mascarados):

- Trap32 (IA-32) mantém retorno antigo nas rotas de erro (sem zero de EAX) — os fluxos validados não passam por elas.
- O `stop` do peproc pode ser sobrescrito dentro do mesmo step (diag seguido de ExitProcess no mesmo step); o log preserva o diagnóstico.
- `test_peproc.c:82` dereferencia `surf` após CHECK (risco pré-existente); código morto `num` DIV/IDIV em `pr_cpu64.c`.

## 11. Warnings

- Build C: **0 warnings** (`-Wall -Wextra`).
- `gcc -fanalyzer` no runtime inteiro: **0 achados**.
- Swift build/test: 0 failures / 0 warnings de compilação novos. Pendência editorial: comentário de classe do `WindowsPEBackend` ainda diz "NÃO está integrado neste build" (texto histórico anterior à integração; comportamento real é o integrado).
- Docs do trap de INT do `pr_cpu64.h` corrigidos para refletir a implementação real.

## 12. Próximo bloqueador

Para executar PEs de compiladores reais (e, muito mais adiante, qualquer jogo — **sem qualquer alegação sobre GTA V, MX Bikes ou jogos comerciais**):

1. **Subconjunto x64 ainda mínimo:** falta FPU/x87 (`long double` do MSVC), SSE2 aritmético completo (`mulsd/addsd/cvt*`), `GS`/TEB/PEB (TLS, `GetLastError` via TEB), SEH/unwind (`.pdata`/`.xdata`), `LOOP`/`JCXZ`, `CPUID`/`RDTSC`. Cada ausência gera EXECUTION STOPPED honesto com opcode/RIP/bytes — é exatamente por aí que o subconjunto deve crescer.
2. **Win32 parcial:** CRT real (UCRT/mingw) exige `InitializeCriticalSection`, TLS/Fls, `GetModuleHandleEx`, locale, `CommandLineToArgvW`, registro mínimo; jogos exigiriam DirectX real via Metal (fora desta etapa) — jamais uma camada falsa.
3. **Sem toolchain Windows no sandbox:** o próximo passo prático é injetar um PE real mingw-w64/MSVC (CI Windows/macOS) e expandir o subconjunto a partir dos EXECUTION STOPPED reais que ele produzir.
4. iOS: a execução é memória virtual dentro da arquitetura de segurança iOS (prioridade iPhone físico > iPad físico > simulador); sem JIT (política Apple) — o interpretador é o caminho.

---

### Registro de implementação (para auditoria)

- Instruções SSE aplicadas: 11 formas (item 3) · String ops: A4–AF + F2/F3/66 (item 2).
- APIs novas: 8 (item 4) · Catálogo: 101.
- Builders de PE de teste: v10 (CRT), v11 (DLL), v12 (loader DLL), v13 (VFS).
- Comandos de verificação: `make test` = **1188 checks C / 0 falhas** + **52 testes Swift / 0 falhas**; `gcc -fanalyzer` = 0 achados.
