# RELATÓRIO FINAL — GRUPO 5: PE real composto (hello_app.exe)

**Status: CONCLUÍDO e REGRESSÃO 100% VERDE.** Grupo 6 (próximos recursos) **NÃO foi iniciado**, conforme ordem.

---

## 1. O que o Grupo 5 entregou (demonstrado com PE real)

`realpe/hello_app.c` compilado com `x86_64-w64-mingw32-gcc 14-win32 -O2 -static` → `Tests/PorticoRuntimeTests/data/hello_app.exe` (248 190 bytes). Um único PE real MinGW que combina **tudo** dos Grupos 1–4 no mesmo caminho completo do app:

| Requisito | Evidência na execução |
|---|---|
| CRT | startup + `exit` + `_errno` etc. (msvcrt real via Catálogo) |
| stdio/STDOUT | `printf` formatado real |
| heap | `malloc/realloc/free` + `memcpy/strcat/strcmp` |
| DLL real | `LoadLibraryA("hello_dll.dll")` → `GetProcAddress("add3")` → chamada MS x64 → `FreeLibrary` |
| SSE2 | `poly(x)=3x²+2x+1` e `sqrt` em `double` |
| CPUID | vendor determinístico `PORTICO_VRTX` (sem vazar host) |
| RDTSC | `tsc>0=1` (contador determinístico +8/insn) |
| unwind | `.pdata`: **95 RUNTIME_FUNCTIONs** indexadas em carga |
| múltiplas funções | `poly`/`fib`/`heap_work`/`dll_work`/`main` |
| exit | **42** |

**Saída real (exata):**
```
x=24.75 r=4.975 fib=55 heap=11 dll=42
cpu=PORTICO_VRTX tsc>0=1
```
`[dbg] exited=1 exit_code=42` · diagnóstico final = registro normal `PROCESS EXIT / ExitProcess (msvcrt.dll)` — **sem bloqueador**.

**Primeiro (e único) novo bloqueador do Grupo 5 encontrado:** `msvcrt.dll!strcmp` (API conhecida sem implementação real) → EXECUTION STOPPED honesto com módulo/símbolo/RIP. Resolvido com implementação real (`strcmp/strcpy/strcat`, leitura limitada). Depois disso o composto completou sem novos bloqueadores. Ficaram com STOP honesto (registrados, não chamados pelo composto): `msvcrt.dll!__C_specific_handler`, `msvcrt.dll!signal`, `RDMSR/RDTSCP`, SSE packed aritmético, `PSHUFW/PSHUFLW/PSHUHW`, x87 aritmético.

## 2. Investigação das falhas de regressão (itens 1 e 2)

### 2.1 `c-test` — falha investigada: era de TESTE, não de produto
- **Causa 1**: 3 CHECKs em `test_cpu64.c` usavam `0F 31` como "opcode não implementado"; `0F 31` = **RDTSC, agora suportado por mandato** (Grupo 4) → o fault esperado não ocorria mais.
- **Causa 2 (o segfault)**: `test_cpu64.c:615` — `pr_cpu64_last_fault()` retornava `NULL` (sem fault, porque o opcode executou) e o teste dereferenciava `f->nbytes`; o `CHECK` do harness não aborta, daí o crash. Localizado com `-fsanitize=address` (SEGV em `test_cpu64`).
- **Correção (intenção preservada, nada mascarado)**: os blocos migraram para `0F 32` (**RDMSR**, ainda honestamente fora do subconjunto) mantendo os CHECKs de fault honesto (opcode/RIP/bytes) + guarda de NULL no bloco de bytes. O código de produto (RDTSC determinístico) **não foi alterado**.
- **Verificação**: build refeito do zero (`build/` regenerado) → **1370 verificações, 0 falhas**.

### 2.2 `swift test` — falha investigada: TESTE desatualizado (nem código, nem ambiente)
- `testX64SubsetFaultIsDiagnosed` gravava `data[0x400]=0x0F` contando com o byte seguinte do builder formar um opcode não suportado; com SSE2 real o par passou a ser **`0F 55` = ANDNPD (suportado)** → o diagnóstico esperado não era mais gerado naquele byte.
- **Correção (intenção preservada)**: pinar os dois bytes em `0F 32` (RDMSR) — a intenção do teste (opcode fora do subconjunto → fault com opcode/RIP/motivo) permanece idêntica.
- **Ruído de ambiente esclarecido**: a linha `✔ Test run with 0 tests passed` é do runner *swift-testing* (nenhum teste registrado nele); os 53 testes são XCTest e aparecem no `Executed 53 tests`. Não é falha.
- **Verificação**: `Executed 53 tests, with 0 failures (0 unexpected)` · `Test Suite 'All tests' passed`.

### 2.3 Achado REAL de código (único): corrigido minimalmente
`gcc -fanalyzer` (manual, arquivo a arquivo — não existe `make analyze`) apontou **1 finding real**: `-Wanalyzer-null-dereference` em `w32_log` (`pr_win32.c:177`, caminho `f_MessageBoxA` → `w32_log`): dereferência de `ctx` sem guarda NULL (CWE-476). **Correção**: `if (!ctx || !ctx->log) return;` — guarda defensiva, zero mudança de comportamento para uso válido. Nenhuma funcionalidade validada foi alterada.

## 3. Preservação exigida
- **Builders v0–v13** (`pr_winhello.c`): intactos — nenhum byte alterado nesta etapa; suites que os exercitam passam (1370 checks).
- **Teste permanente do hello_app.exe**: presente em `test_pe_real.c` (bloco GRUPO 5: exit 42, stdout 63 bytes com `x=24.75 r=4.975 fib=55 heap=11 dll=42` e `cpu=PORTICO_VRTX tsc>0=1`, com `hello_dll.dll` via `provide_dll`). Testes antigos **não foram removidos nem enfraquecidos**.

## 4. Certificação final da regressão (executada após todas as correções)

| Critério | Resultado |
|---|---|
| C tests (`make c-test`) | **1370 verificações, 0 falhas** ✅ |
| Swift tests (`swift test`, XCTest) | **53 testes, 0 falhas (0 unexpected)** ✅ |
| `gcc -Wall -Wextra` (src + tests) | **0 warnings** ✅ |
| `gcc -fanalyzer` manual (src, arquivo a arquivo) | **0 findings** ✅ |
| PE real `hello_real.exe` | **exit 42** ✅ |
| PE real `hello_user.exe` + `hello_dll.dll` | **exit 42** (provide_dll + DllMain attach/detach) ✅ |
| PE real `hello_app.exe` + `hello_dll.dll` | **exit 42 + saída exata acima** ✅ |
| bônus `hello_stdio.exe` | exit 5, stdout 81 B ✅ |

## 5. Próximo passo (somente após este relatório)
Grupo 6 — **não iniciado**. Recomendação registrada: camada gráfica mínima (GDI/BitBlt → superfície BGRA8 → estágio de submissão já isolado) para validar o pipeline de jogos, mantendo EXECUTION STOPPED honesto para tudo que não for implementado.

---
*Portico — WinOS (Arautos do Divino). Nenhum sucesso falso; nenhuma funcionalidade validada foi alterada para fazer teste passar; Grupo 5 fechado com regressão 100% verde.*
