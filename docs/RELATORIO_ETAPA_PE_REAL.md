# Relatório — Etapa PE REAL (toolchain Windows x64)

Data: 2026-09-24. Objetivo da etapa: **provar execução de um executável Windows x64 REAL de toolchain externo** (não PEs sintéticos dos builders internos), com o pipeline honesto: PE real → loader → imports → CPU → CRT → primeiro bloqueador (EXECUTION STOPPED) → implementar SÓ o necessário → repetir.

---

## Os 12 itens

### 1. PE real
**`hello_real.exe`** (fonte `realpe/hello_real.c`): `int main(void) { ...cálculo inteiro + memcpy/strlen...; return 42; }` compilado sem jogos nem atalhos. PE32+ CUI de verdade: ImageBase `0x140000000`, entry RVA `0x13F0`, 18 seções (`.text .data .rdata .pdata .xdata .bss .idata .tls .reloc` + 9 de debug), SizeOfImage 0x20000, .bss com raw=0, seções com nomes longos `/4` etc. Binário de teste fixado em `Tests/PorticoRuntimeTests/data/hello_real.exe`.
Também nesta etapa: **`hello_dll.dll`** (`realpe/hello_dll.c`, `-shared`, exporta `add3`) e **`hello_user.exe`** (`realpe/hello_user.c`), fixados em `Tests/PorticoRuntimeTests/data/`.

### 2. Toolchain
**MinGW-w64 GCC 14 (`x86_64-w64-mingw32-gcc (GCC) 14-win32`)** — o mesmo tipo de toolchain usado por ports de jogos Windows. Flags: `-O2 -static -D_WIN32_WINNT=0x0600` (EXE), `-O2 -shared` (DLL). Observação honesta: mesmo com `-static` o EXE importa **KERNEL32×11 + msvcrt.dll×24** — o CRT real do MinGW sempre importa msvcrt.

### 3. Entry point
Entry RVA `0x13F0` = **`mainCRTStartup` real do MinGW** (mappado em `base+0x13F0`; no teste, base relocalizada `0x00FE0000` → entry `0x00FE13F0`). Antes do `main()`, o CRT real roda `_initterm`, `__set_app_type`, `__getmainargs`, `__setusermatherr`, `_onexit`, configuração de FPU/TEB.

### 4. Primeiras instruções
Execução real começou no CRT (log `[CPU] execution started`). Sequência observada do startup MinGW (além de código straight-line):
- `65 48 8B 04 25 30 00 00 00` — `mov rax, gs:[0x30]` (TEB Self; depois PEB em TEB+0x60);
- `F0 48 0F B1 33` — `lock cmpxchg [rbx], rsi` (inicialização thread-safe do CRT);
- `DB E3` — `fninit` (x87);
- `66 0F 70 C0 00` — `pshufd xmm0, xmm0, 0` + `movups` (vetorização do CRT/memcpy).

### 5. Primeiro bloqueador
Ordem real dos bloqueadores encontrados (cada um virou EXECUTION STOPPED detalhado e foi resolvido sob demanda):
1. **prepare**: `KERNEL32!DeleteCriticalSection` (e toda a lista KERNEL32×11 + msvcrt×24) sem implementação — o binder rejeitava até as APIs *catalogadas*;
2. **CPU**: `segmento FS/GS (TEB) fora do subconjunto x64` em `mov rax, gs:[0x30]` @ `0x00FE119A`;
3. `lock cmpxchg` (opcode `0F B1` com prefixo `F0`) fora do subconjunto;
4. `fninit` (x87 `DB E3`) fora do subconjunto;
5. `pshufd` (`66 0F 70`) fora do subconjunto SSE.
O primeiro bloqueador do PIPELINE COMPLETO foi o item 5.1 (imports); o primeiro bloqueador de **instrução** foi o `gs:[0x30]` (TEB).

### 6. Instruções adicionadas (só as exigidas pelo binário real)
| Instrução | Encoding | Observação |
|---|---|---|
| FS/GS com base flat | `64/65` + ModRM/SIB | `pr_cpu64_set_fs/gs_base`; `ea_of` soma `seg_base`; último prefixo vence |
| LOCK | `F0` | aceito como no-op (VM mono-core, atomicidade passo-a-passo — documentado) |
| CMPXCHG | `0F B0/B1` (+`F0`) | flags como `CMP`; ZF→grava fonte; senão devolve destino em AL/AX/EAX/RAX |
| WAIT | `9B` | combinado com FNINIT (FINIT) |
| FNINIT/FNCLEX | `DB E3`/`DB E2` | **x87 = só estado de controle** (cw/sw/tw); aritmética x87 fora do subconjunto (fault honesto) |
| FNSTCW/FLDCW | `D9 /7`, `D9 /5` | control word m16 |
| FNSTSW | `DD /7` (m16), `DF E0` (AX) | status word |
| PSHUFD | `66 0F 70 /r ib` | shuffle de dwords; irmãos PSHUF* fora (fault honesto) |

Não foi implementado: SSE aritmético, CPUID, RDTSC, LOOP/JCXZ, x87 aritmético, SEH/.pdata — **nenhum foi exigido pelos PEs reais desta etapa** (regra: não implementar sem exigência).

### 7. APIs adicionadas (comportamento real, sem sucesso falso)
**kernel32** (9 novas): `InitializeCriticalSection`, `EnterCriticalSection`, `LeaveCriticalSection`, `DeleteCriticalSection` (CRITICAL_SECTION x64 48B com RecursionCount/OwningThread), `SetUnhandledExceptionFilter`, `Sleep` (nanosleep), `TlsGetValue` (slots 0–63; índice ≥64 → last_error=6), `VirtualQuery` (MBI 48B com probe de páginas), `VirtualProtect` (já existia — preservada a implementação validada).
**msvcrt.dll** (novo módulo no catálogo — 28 entradas): reais: `__getmainargs` (parse de argv real), `__set_app_type`, `__setusermatherr`, `_initterm` (roda ctors via reentrada do convidado), `_onexit` (lista 32, LIFO como atexit), `exit`/`_cexit`/`_amsg_exit`/`abort` (rodam handlers `_onexit` reais e sinalizam `halted` + exit_code — mesmo mecanismo de `ExitProcess`), `malloc`/`calloc`/`free`/`realloc` (heap real do VM via `HeapAlloc`), `memcpy`/`strlen`/`strncmp`, `__iob_func` (bloco FILE 3×48B), `_lock`/`_unlock` (contadores por índice). Dados (células RW, `DATA_SYM`): `_fmode`, `_commode`, `__initenv` — o slot IAT recebe o endereço da célula (GetProcAddress retorna a célula correta). Honestos (stub de diagnóstico = EXECUTION STOPPED na chamada): `__C_specific_handler`, `signal`, `fprintf`, `fwrite`, `vfprintf`.
**Arquitetura**: binder de imports aceita `PR_WIN32_UNSUPPORTED` catalogado → stub de diagnóstico + warn no log; import *desconhecido* continua falhando no prepare (nunca se adivinha). Trap `r>0` = parada pedida pelo trap (exit ou EXECUTION STOPPED) — corrigido o vazamento que continuava executando após `exit()`.

### 8. Suporte DLL real
**SIM — completo com DLL real do MinGW**: `hello_dll.dll` (`-shared`, export `add3`, `DllMain`) via `LoadLibraryA → GetProcAddress → chamada ABI MS x64 (rcx/rdx/r8/r9) → FreeLibrary`. `hello_user.exe` sai com **42** se `add3(39,0)==42`. Log real do ciclo: `dll mapped hello_dll.dll base=0x00FBC000` → `_initterm/_lock/calloc/_unlock` (CRT da DLL) → `DllMain hello_dll.dll attach ret=1` → `GetProcAddress` → `FreeLibrary` → `dll unloaded hello_dll.dll`. A DLL tem imports reais próprios (KERNEL32×9 + msvcrt×13 — incluindo `_lock/_unlock/realloc`, implementados nesta etapa por exigência do binário real). Relocations + imports da DLL resolvidos de verdade; DLLs não fornecidas pelo app = recusa honesta com log.

### 9. Checks C
**1207 verificações, 0 falhas** (`make c-test`) — inclui 19 checks novos permanentes (`test_pe_real.c`): carga do PE real, pipeline completo até `exit(42)`, DATA_SYM ≥3, `fprintf` continua honesto, e o cenário FASE 5 inteiro (provide + mapped + DllMain attach + exit 42 + unload). Nenhum teste removido; apenas adicionados. Suítes preservadas: CPU/PE/loader/zip/gfx/audio/host/vfs/win32/surf/vm/peproc/wincompat/cpu64/visual/win32x/dll/pes/cpu64ext + pe-real.

### 10. Testes Swift
**53 testes, 0 falhas** (`make swift-test`, Swift 6.1.2) — 52 preservados + 1 novo (`testRealMinGWPEExecutesToExitCode42`): o PE real roda pelo **app stack completo** (`WindowsPEBackend` + `RuntimeSessionContext` + `stepFrame`) até `PROCESS EXIT` com `exitCode == 42`.

### 11. Warnings
**0 warnings**: `gcc -std=c11 -Wall -Wextra -fanalyzer` sobre toda a `Sources/PorticoRuntime/src` = 0 achados; `make c-test` compila com `-Wall -Wextra` limpo. Nenhum `-Wmissing-field-initializers` após completar as macros do catálogo (`is_data` posicional).

### 12. Próximo bloqueador técnico
Para um PE real **maior** (ex.: um "hello world" com `printf`, ou jogos como MX Bikes — GTA V exige DirectX 11/12 + GPU, fora do objetivo honesto desta etapa), o próximo bloqueador é o **stdio formatado real do msvcrt** (`fprintf/fwrite/vfprintf` — hoje stubs honestos; precisam de `FILE*` real + formatação `%d/%s/%f`...), seguido de: **SEH real** (`__C_specific_handler` + `.pdata`/`.xdata` unwind), **mais SSE2 aritmético** (`addsd/mulsd/cvtsi2sd...` — programas com `double`), **CPUID/RDTSC** (alguns CRTs/proteções), e **FS/GS além do TEB** (TLS dinâmico). Cada um continuará pelo mesmo método: EXECUTION STOPPED detalhado → implementar SÓ o que o binário real exigir.

---

## Verificação executada nesta etapa
```
x86_64-w64-mingw32-gcc (GCC) 14-win32
build/hello_real.exe  → prepare OK → CRT real → main() → exit 42   ✔ (exit_code=42)
build/hello_user.exe + build/hello_dll.dll → LoadLibraryA/GetProcAddress/chamada/FreeLibrary → exit 42 ✔
make c-test     → 1207 verificações, 0 falhas        ✔
make swift-test → 53 testes, 0 falhas                 ✔
gcc -fanalyzer  → 0 warnings                          ✔
```

## Limitações conhecidas (honestas)
- x87: apenas estado de controle (FNINIT/FNCLEX/FLDCW/FNSTCW/FNSTSW); aritmética x87 = EXECUTION STOPPED.
- SSE: movs/xor/pshufd; aritmética packed/scalar, shuffles extras, MOVSS/MOVSD = EXECUTION STOPPED.
- stdio formatado (`fprintf/fwrite/vfprintf`), `signal`, SEH (`__C_specific_handler`): stubs de diagnóstico (chamada = EXECUTION STOPPED com nome + parâmetros + log).
- `_lock/_unlock`: contadores por processo (documentado; mono-thread do convidado nesta etapa).
- LOCK prefix: no-op (VM mono-core).
- Sem JIT (decisão de arquitetura), sem DirectX, sem camada gráfica falsa.
