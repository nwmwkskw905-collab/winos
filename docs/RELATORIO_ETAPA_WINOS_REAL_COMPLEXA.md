# RELATÓRIO DA ETAPA — Windows x64 real mais complexo (WinOS)

**Objetivo cumprido:** sair de "executa PE real simples" para "executar programas Windows x64 reais mais complexos", com base sólida antes da camada gráfica dos jogos.

---

## 1. Funcionalidades implementadas

### GRUPO 1 — STDIO/CRT real
- Engine CRT real em `pr_win32.c` (`w32_format` determinístico): `printf/fprintf/vfprintf/fflush/fputc`, FILE por `__iob_func` (índices 0–2), stdout line-bufferado (2048), stderr direto; flush na saída do processo.
- Formatos: `%d i u x X o c s p f F %%`, flags `-+ 0#`, width/prec (`*`), comprimentos `h hh l ll z t`; conversão não suportada → `UNSUPPORTED` (nunca sucesso falso).
- Apoio CRT (implementação real): `IsDBCSLeadByteEx/MultiByteToWideChar/WideCharToMultiByte` (CP 0/1252/65001), `memset/wcslen/fputc/_errno/strerror/localeconv/___lc_codepage_func/___mb_cur_max_func`.
- Convenção TLS: `[60]=lconv [61]=strerror [62]=errno [63]=iob`.
- Recém-adicionado na etapa composta: `strcmp/strcpy/strcat` (comportamento real, leitura limitada 4096).

### GRUPO 2 — SEH/UNWIND x64 (mínimo mandado)
- `pr_pe_data_dir` (pe.h/pe.c) — leitura de diretórios de dados do PE32+.
- `pr_unwind_index/parse/lookup/compute` (pr_unwind.c): `.pdata` → RUNTIME_FUNCTION → UNWIND_INFO → UWOP 0–8 (incl. FRAME/CHAIN), cálculo de offset de stack.
- `pr_peproc`: índice `unw[256]` + `pr_peproc_unwind_count/at/find`.
- Sem dispatch de exceção (documentado: `.pdata` indexado + verificação de consistência; dispatch = etapa futura).

### GRUPO 3 — SSE2 real (só instruções comprovadamente necessárias)
- Arquitetura em `pr_cpu64.c` estruturada p/ expansão (prefixos F2/F3/66 + 0F, ModRM RIP-rel, load/store de 16 bytes, scalar/element helpers).
- Escalar `sqrts/add/sub/mul/div` (F2/F3 `0x51/58/59/5C/5E`), `cvtsi2sd` `0x2A`, `cvtsd/cvtss` `0x5A/2C/2D`, `comisd/ss` `0x2E/2F`, `unpcklpd` `0x14`, `punpckldq` `0x62`, `movups/movupd/movaps/movdqa/movd/movq` `0x10/11/28/29/6E/6F/D6/E6/EF`, `andpd/orpd/xorpd/andnpd` `0x54–57`, `PSHUFD` `66 0F 70`.
- x87 mínima p/ CRT do MinGW: `FLD/FST/FSTP` m32/m64/m80, `FSQRT`, `FNINIT/FNCLEX/FNSTSW AX/FNSTCW/FLDCW`, pilha `fst[8]`/`fpu_top` determinística.
- **FORA do subconjunto (FAULT honesto)**: aritmético SSE packed, `PSHUFW/PSHUFLW/PSHUHW`, aritmético x87, `LOOP/JCXZ`, `RDTSCP`.
- Regra registrada: `0x5A` é SÓ conversão (cvtsd/cvtss) — nunca em whitelist aritmética (bug real encontrado e corrigido).

### GRUPO 4 — CPUID e RDTSC (determinísticos, sem expor host)
- `CPUID (0F A2)`: folha 0 → EAX=1, vendor `"PORTICO_VRTX"` (EBX `PORT`, EDX `ICO_`, ECX `VRTX`); folha 1 → EAX `0x000006F0`, EDX = bits TSC(4)|CMOV(15)|SSE(25)|SSE2(26); demais folhas → 0. Nunca vaza CPU host.
- `RDTSC (0F 31)`: contador interno `tsc` (campo novo de `pr_cpu64`), +8 por instrução — determinístico e monótono; `RDTSCP` continua FAULT honesto.
- `BSF/TZCNT/BSR (0F BC/BD, F3 0F BC)` e `CMOVcc (0F 40–4F)`, `CMPXCHG (0F B0/B1)`, `INT 2E`, `FS/GS base`, PSHUFD etc. (acumulado desta etapa).

### GRUPO 5 — PE real composto (MinGW)
- `realpe/hello_app.c` compilado com `x86_64-w64-mingw32-gcc 14-win32 -O2 -static` → `hello_app.exe` (248 190 bytes).
- Combina, num só PE real: CRT + stdio formatado + heap (`malloc/realloc/free`) + DLL real (`LoadLibraryA/GetProcAddress/chamada MS x64/FreeLibrary`) + SSE2 (doubles) + `CPUID/RDTSC` + múltiplas funções com `.pdata/.xdata` reais (95 RUNTIME_FUNCTIONs indexadas).
- Execução pelo **mesmo caminho completo do app** (`pr_peproc` + win32 + provide_dll).

## 2. Instruções novas (conjunto total desta etapa)
SSE2: `sqrtsd/ss addsd/ss subsd/ss mulsd/ss divsd/ss cvtsi2sd cvtsd2ss cvtss2sd cvttsd2si cvttss2si comisd/ss ucomisd/ss unpcklpd punpckldq movups movupd movaps movdqa movd movq andpd orpd xorpd andnpd pshufd` · x87: `fld fst fstp (m32/m64/m80) fsqrt fninit fnclex fnstsw fnstcw fldcw` · gerais: `cpuid rdtsc bsf tzcnt bsr cmovcc cmpxchg int 2e sahf/lahf` (parcial) · prefixes F2/F3/66 + ModRM RIP-rel.

## 3. APIs novas
- **CRT/stdio**: `__iob_func printf fprintf vfprintf fflush fputc fwrite _errno strerror localeconv ___lc_codepage_func ___mb_cur_max_func wcslen memset strcmp strcpy strcat IsDBCSLeadByteEx MultiByteToWideChar WideCharToMultiByte`.
- Importadas pelo composto (registradas): `__initenv _commode _fmode` (células RW de dados ✓), `__C_specific_handler` (stub de diagnóstico honesto — chamada = EXECUTION STOPPED), `signal` (idem), msvcrt `printf strlen strncmp memcpy malloc realloc free exit strcat strcmp`.

## 4. DLLs novas
- `hello_dll.dll` (MinGW, `add3` cdecl + DllMain): carga real via `LoadLibraryA`, chamada pela convensão MS x64, `FreeLibrary` — compartilhada entre processos via `provide_dll` (sandbox do app). Nenhuma DLL do sistema nova foi necessária além de `msvcrt/kernel32` (Catálogo).

## 5. Testes adicionados
| Arquivo | Conteúdo |
|---|---|
| `test_pe_real.c` §5/§5b | hello_stdio (exit 5, stdout 81 B com 5 formatos) e hello_sse (exit 7, saída exata + .pdata 95) |
| `test_unwind.c` | índice de .pdata, parse UNWIND_INFO, lookup/compute + 4 códigos (nova suíte) |
| `test_cpuid.c` | vendor, folha 1, folhas zero, TSC determinístico (2 CPUs = igual), monótono (nova suíte) |
| `test_cpu64.c` | blocos SSE2/x87/CPUID/RDTSC; blocos de fault migrados de `0F 31` → `0F 32` (RDMSR) preservando a intenção (fault honesto com opcode/RIP/nbytes) |
| `test_pe_real.c` GRUPO 5 | hello_app: exit 42 + stdout `x=24.75 r=4.975 fib=55 heap=11 dll=42` + `cpu=PORTICO_VRTX tsc>0=1` |
| `WindowsExecutionTests.swift` | fault pinado em `0F 32` (intenção diagnóstica preservada) |

## 6. Regressão (final da etapa)
- **C (`make c-test`): 1370 verificações, 0 falhas** (todos os blocos adicionados; builders v0–v13 preservados).
- **Swift (`swift test`): 53 testes, 0 falhas**.
- `gcc -fanalyzer`: **0 warnings**. `-Wall -Wextra`: **0 warnings**. Builds MinGW 14-win32 sem warnings.

## 7. PEs reais executados
| PE | Resultado |
|---|---|
| `hello_real.exe` | exit 42 |
| `hello_user.exe` + `hello_dll.dll` | exit 42 (DllMain attach/detach corretos) |
| `hello_stdio.exe` | exit 5, stdout 81 B perfeito (5 formatos) |
| `hello_sse.exe` | exit 7, stdout `x=24.75 total=30.0 q=10.0 r=3.162 fs=12.0 cvt=36.75 cmp=1 ip=99`, .pdata 95 |
| **`hello_app.exe` + `hello_dll.dll`** | **exit 42**, stdout `x=24.75 r=4.975 fib=55 heap=11 dll=42` / `cpu=PORTICO_VRTX tsc>0=1` — **PE composto completo** |

## 8. Primeiro novo bloqueador encontrado (registro honesto)
1. **`msvcrt.dll!strcmp` — API conhecida sem implementação real** → EXECUTION STOPPED com módulo/símbolo/RIP; resolvido nesta etapa com implementação real.
2. Após isso, o PE composto **não encontrou mais bloqueadores**: completou com exit 42 no caminho completo.
3. Bloqueadores **registrados e honestos** (presentes, não chamados pelo composto, chamada = STOP): `msvcrt.dll!__C_specific_handler` (dispatch de exceção SEH completo — fora do mínimo mandado), `msvcrt.dll!signal`, `RDMSR/RDTSCP`, SSE packed aritmético, PSHUFW/LW/HW, aritmético x87.

## 9. Próximo passo recomendado
1. **Camada gráfica mínima para pipeline de jogos**: GDI (`CreateCompatibleDC/SelectObject/BitBlt` já parcial em `test_visual`) + superfície BGRA8 → estágio de submissão já isolado (`submissionReady`) — validar com PE visual real, sem DirectX/Metal falsos.
2. Dispatch de exceção SEH (`__C_specific_handler` real + handler chain sobre o unwind já indexado) quando um PE real exigir.
3. Só então interface final e, por último, os jogos (GTA V/MX Bikes) — com logging por frame e loop de diagnóstico de bloqueadores.

---
*Portico — WinOS (Arautos do Divino). Tudo validado por execução real; nenhum sucesso falso; toda API ausente = EXECUTION STOPPED com diagnóstico completo.*
