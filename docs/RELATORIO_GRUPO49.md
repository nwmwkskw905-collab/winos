# Relatório do Grupo 49 — Validação real por PE x64 — ws2_32: htonl/htons/ntohl/ntohs

## 1. STATUS

**CONCLUÍDO** — as quatro funções `ws2_32` foram validadas por PE x64 real (`hello_winsock_ord.exe` → **rc=42**), com binding de import **por nome**, 20/20 execuções determinísticas, **nenhum blocker**, **nenhuma alteração no runtime** (byte-for-byte idêntico). Ressalva honesta de medida no item 15 (analyzer) e no item 19.

## 2. BASELINE (estado final do G48 — confirmada antes de qualquer mudança)

- C tests: `3392 verificações, 0 falhas` ✓ · Swift: `Executed 63 tests, with 0 failures` ✓ · warnings (receita do projeto, `gcc -Wall -Wextra`): 0 ✓
- PE battery 15/15 (logs): `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105` ✓
- hello_wait_multiple=42 · hello_thread_timeout=42 · hello_mutex_timeout=42 · hello_thread_shared=42 · hello_thread=42 · hello_mutex=42 · hello_cs=42 · hello_virt=42 · hello_virt_protect=42 · hello_virt_query=42 · hello_qpc=42 · hello_heap=42 · hello_mbwc=42 · hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_stdio=5 · hello_gl12=42 · hello_sse=7 ✓
- **MD5 antes: `c9e8afa71214412be92bff3de0b35278`** = esperado ✓

## 3. PE CRIADO

- Fonte: `realpe/hello_winsock_ord.c` — **fluxo mínimo exato do briefing** (`winsock2.h` + 5 asserções + `return 42`). Nenhuma outra API adicionada.
- Binário: `Tests/PorticoRuntimeTests/data/hello_winsock_ord.exe` (14.336 bytes).

## 4. TOOLCHAIN

`x86_64-w64-mingw32-gcc -O2 -s realpe/hello_winsock_ord.c -o Tests/PorticoRuntimeTests/data/hello_winsock_ord.exe -lws2_32` (MinGW-w64 posix).

## 5. IMPORTS REAIS (`x86_64-w64-mingw32-objdump -p`)

| DLL | Funções | Natureza |
|---|---|---|
| `KERNEL32.dll` | 10 (DeleteCriticalSection, EnterCriticalSection, GetLastError, InitializeCriticalSection, LeaveCriticalSection, SetUnhandledExceptionFilter, Sleep, TlsGetValue, VirtualProtect, VirtualQuery) | CRT de startup MinGW |
| `msvcrt.dll` | 24 (`__C_specific_handler`, `__getmainargs`, `__initenv`, `__iob_func`, `__set_app_type`, `__setusermatherr`, `_amsg_exit`, `_cexit`, `_commode`, `_fmode`, `_initterm`, `_onexit`, `abort`, `calloc`, `exit`, `fprintf`, `free`, `fwrite`, `malloc`, `memcpy`, `signal`, `strlen`, `strncmp`, `vfprintf`) | CRT |
| **`WS2_32.dll`** | **`htonl`, `htons`, `ntohl`, `ntohs`** | **alvo do grupo** |

`objdump -d`: **0** instruções `bswap` no código (as chamadas são reais — sem inlining).

## 6. NOME vs ORDINAL

```
vma:     Ordinal  Hint  Member-Name
000082b8  <none>  00b3  htonl
000082c0  <none>  00b4  htons
000082c8  <none>  00bb  ntohl
000082d0  <none>  00bc  ntohs
```

**Binding: POR NOME** (coluna `Ordinal` = `<none>`; nome + hint em todas as quatro).
⇒ Pela regra do briefing: **a validação por PE das quatro funções está CONCLUÍDA; a validação específica do binding por ordinal NÃO está concluída.** O PE **não** foi modificado artificialmente para forçar ordinal.

## 7. PRIMEIRA EXECUÇÃO (runtime completamente intocado)

`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_winsock_ord.exe`:

```
exited=1 rc=42 steps=1 exec=350
PROCESS EXIT — Reason: ExitProcess — Module: msvcrt.dll — Function: exit
Address: 0x00FF63CF — Architecture: x86-64 — Exit code: 42
```

- `exited=1`, **`rc=42`** — passou **direto**, sem ponto de parada, sem fault.
- Log do processo: **33 entradas** (determinístico nas 21 execuções).
- Símbolo/import da parada: nenhuma parada — fluxo completo; término via `msvcrt!exit`.
- **MD5 antes/depois da execução: idêntico** — o PE não alterou nada.

## 8. BLOCKER

**Nenhum.** Nenhuma instrução fora do subconjunto CPU foi observada; nenhum gap registrado; nada foi antecipado.

## 9. CORREÇÃO

**Nenhuma correção necessária** — nem no runtime nem no PE. (Registro de transparência: uma inspeção inicial truncada de saída deu a falsa impressão de que `WS2_32.dll` faltava nas imports; a inspeção completa — seção 5 — mostrou as quatro importações presentes. Nenhuma mudança foi feita com base no falso alarme.)

## 10. RESULTADO FINAL

| Verificação | Resultado |
|---|---|
| `htonl(0x12345678) == 0x78563412` | ✓ (contrato 10 nunca disparado) |
| `ntohl(0x12345678) == 0x78563412` | ✓ (contrato 11 nunca disparado) |
| `htons(0x1234) == 0x3412` | ✓ (contrato 12 nunca disparado) |
| `ntohs(0x1234) == 0x3412` | ✓ (contrato 13 nunca disparado) |
| `htonl(ntohl(0xAABBCCDD)) == 0xAABBCCDD` | ✓ (contrato 14 nunca disparado) |
| Fluxo completo | ✓ **rc=42** |
| Crash/fault/corrupção | ✓ nenhum (exit limpo; runtime byte-idêntico) |

Contrato de retorno usado: somente 10/11/12/13/14/42 (nenhum outro código criado).

## 11. QUANTIDADE DE EXECUÇÕES CONSECUTIVAS

**20/20 com `rc=42`** (`exited=1 rc=42 log=33` em todas) — determinístico.

## 12. C TESTS (regressão)

`3392 verificações, 0 falhas` ✓

## 13. SWIFT TESTS (regressão)

`Executed 63 tests, with 0 failures` ✓

## 14. WARNINGS (regressão)

`0` (receita do projeto: `gcc -std=c11 -Wall -Wextra` sobre os 20 `.c` do runtime) ✓

## 15. ANALYZER (regressão) — declaração honesta de medida

Receita: `clang --analyze -std=c11` por arquivo do runtime. Resultado: **7 achados — todos PRÉ-EXISTENTES, idênticos antes/depois** (o runtime é byte-for-byte idêntico, MD5 §17, portanto nenhum foi introduzido neste grupo; **zero novos**):

1. `pr_cpu.c:126` — Null pointer passed to 2nd parameter expecting 'nonnull' [core.NonNullParamChecker]
2. `pr_cpu64.c:401` — Right operand is negative in left shift [core.BitwiseShift]
3. `pr_gfx.c:98` — Null pointer passed to 2nd parameter expecting 'nonnull' [core.NonNullParamChecker]
4. `pr_peproc.c:831` — Value stored to 'o' is never read [deadcode.DeadStores]
5. `pr_win32.c:3611` — Value stored to 'r' is never read [deadcode.DeadStores]
6. `pr_win32.c:3615` — Value stored to 'r' is never read [deadcode.DeadStores]
7. `pr_winhello.c:885` — Value stored to 'extra' is never read [deadcode.DeadStores]

Nota: o número `0` registrado em grupos anteriores provém de uma receita com `scan-build`, indisponível neste ambiente (a ferramenta não existe no toolchain atual; a medição anterior saía vazia). A receita reproduzível atual (`clang --analyze`) acusa os 7 achados acima **já presentes no estado baseline** (fora do escopo deste grupo, cuja regra proibia alterar o runtime). Item de aceitação "analyzer permanecer 0" satisfeito em substância (**nenhum achado novo; contagem inalterada**) — declarado literalmente aqui para não afirmar medida falsa.

## 16. PE BATTERY (regressão)

`15/15` — logs `28/43/180/70/72/75/98/118/76/81/194/131/55/200/105` (hello_real/user/app/gdi/gl/gl2..gl10/input) ✓
Checklist completa: hello_wait_multiple=42 · hello_thread_timeout=42 · hello_mutex_timeout=42 · hello_thread_shared=42 · hello_thread=42 · hello_mutex=42 · hello_cs=42 · hello_virt=42 · hello_virt_protect=42 · hello_virt_query=42 · hello_qpc=42 · hello_heap=42 · hello_mbwc=42 · hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_stdio=5 · hello_gl12=42 · hello_sse=7 ✓

## 17. MD5 ANTES/DEPOIS

- Antes: `c9e8afa71214412be92bff3de0b35278`
- Depois (21 execuções + regressão): `c9e8afa71214412be92bff3de0b35278`
- **IDÊNTICO** — nenhum arquivo do runtime foi alterado (somente `realpe/hello_winsock_ord.c` foi criado e o `.exe` gerado em `data/`).

## 18. LIMITAÇÕES RESTANTES

- **Binding por ordinal público (`IMPL_ORD`) ainda sem PE real** — o import lib do MinGW emitiu binding por nome; conforme o briefing, não se forçou ordinal artificialmente.
- Sockets (`WSAStartup/WSACleanup/socket/connect/send/recv/closesocket/getaddrinfo`) continuam TODO — não são alvo e não foram tocados.
- `ntohl`/`ntohs` reaproveitam os handlers de `htonl`/`htons` (documentado no runtime; o round-trip valida o par completo).
- `__C_specific_handler` e `signal` aparecem nos imports do startup msvcrt (nunca despachados — bind msvcrt é só no call) — não validados e não são escopo.
- Os 7 achados do analyzer (§15) permanecem (correção exigiria alterar o runtime — proibido neste grupo).

## 19. DECLARAÇÃO EXPLÍCITA DO QUE FOI E NÃO FOI VALIDADO

**VALIDADO por PE x64 real:**
- `ws2_32!htonl` → `0x78563412` ✓
- `ws2_32!ntohl` → `0x78563412` ✓
- `ws2_32!htons` → `0x3412` ✓
- `ws2_32!ntohs` → `0x3412` ✓
- round-trip `htonl(ntohl(0xAABBCCDD))` → `0xAABBCCDD` ✓
- Dispatch real dos quatro handlers do runtime (chamadas reais, sem inlining), **via binding de import por NOME** ✓
- Determinismo: 20/20 rc=42; sem crash/fault/corrupção ✓

**NÃO VALIDADO (declarado):**
- **Binding de import por ORDINAL** — não validado (o PE usou somente binding por nome; não se afirma validação por ordinal).
- Sockets, `WSAStartup` e demais APIs não exercitadas — não validados.
- **Não se afirma compatibilidade geral com Windows nem com jogos comerciais.**
