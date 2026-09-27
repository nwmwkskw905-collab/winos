# RELATÓRIO — GRUPO 38: validação real de VirtualAlloc + VirtualFree por PE x64

**Desfecho: VALIDADO POR PE REAL.** `hello_virt.exe` retornou **rc=42 na
PRIMEIRA execução** com o runtime INTOCADO — não houve nenhuma correção.
Declaração: **"Runtime permaneceu intocado; VirtualAlloc + VirtualFree foram
validados por PE real."**

---

## 1. Baseline

```text
C tests: 3392 / 0        Swift tests: 63 / 0
warnings:       0        analyzer:       0
PE battery: 15/15 verdes (runner canônico dbg_input):
  hello_real  rc=42 log=28   hello_gl    rc=42 log=72   hello_gl6   rc=42 log=81
  hello_user  rc=42 log=43   hello_gl2   rc=42 log=75   hello_gl7   rc=42 log=194
  hello_app   rc=42 log=180  hello_gl3   rc=42 log=98   hello_gl8   rc=42 log=131
  hello_gdi   rc=42 log=70   hello_gl4   rc=42 log=118  hello_gl9   rc=42 log=55
  hello_input rc=42 log=105  hello_gl5   rc=42 log=76   hello_gl10  rc=42 log=200
hello_gl11=42 · hello_sse=7 · hello_gl12=42 · hello_stdio=5
hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_mbwc=42
hello_heap=42 · hello_qpc=42
```

Notas de método: a bateria canônica roda `dbg_input <pe> noinput` (sem injeção
de eventos) para os 14 PEs e `dbg_input hello_input.exe` (com o roteiro de
eventos) para o `hello_input`; os números 28/43/… são contagens de **log**
esperadas. md5 do runtime no início: `4a23bc7bf8bc2d6e0ff55969290bfcd2`.

## 2. PE criado

- **Fonte**: `realpe/hello_virt.c` (71 linhas).
- **Binário**: `Tests/PorticoRuntimeTests/data/hello_virt.exe`.
- **Compilação**: `x86_64-w64-mingw32-gcc -O2 -s` (GCC 14-posix mingw-w64 — a
  mesma cadeia dos demais `hello_*`), **0 erros/0 avisos**.
- Escopo conforme FASE 7: somente `VirtualAlloc` + acesso à memória do guest +
  `VirtualFree`. Sem arquivos, GUI, OpenGL, sockets, threads, heap Win32,
  `VirtualProtect`, `VirtualQuery` ou outras APIs.

## 3. Imports

**KERNEL32.dll** — programáticos: `VirtualAlloc`, `VirtualFree` (os únicos
usados pelo código). O restante do import table é o conjunto padrão do **CRT
startup mingw-w64**, idêntico ao dos demais `hello_*` da família
(comparado item a item com `hello_qpc.exe` e `hello_heap.exe`):
`DeleteCriticalSection, EnterCriticalSection, GetLastError,
InitializeCriticalSection, IsDBCSLeadByteEx, LeaveCriticalSection,
MultiByteToWideChar, SetUnhandledExceptionFilter, Sleep, TlsGetValue,
VirtualProtect, VirtualQuery, WideCharToMultiByte`.

**msvcrt.dll** — CRT padrão do startup: `__C_specific_handler, ___lc_codepage_func,
___mb_cur_max_func, __getmainargs, __initenv, __iob_func, __set_app_type,
__setusermatherr, _amsg_exit, _cexit, _commode, _errno, _fmode, _initterm, _lock,
_onexit, _unlock, abort, calloc, exit, fprintf, fputc, free, fwrite, localeconv,
malloc, memcpy, memset, signal, strerror, strlen, strncmp, vfprintf, wcslen`.

**Conclusão**: nenhum import adicional desnecessário — além da linha de base do
CRT (que em toda a família já traz `VirtualProtect`/`VirtualQuery` como imports
do startup, **nunca chamados** por este PE — a fonte prova que não os utiliza;
a proibição do grupo refere-se ao código/fluxo testado). `__C_specific_handler`
e `signal` aparecem como stubs de diagnóstico (EXECUTION STOPPED se chamados) e
**não foram chamados**.

## 4. Fluxo executado

```text
p = VirtualAlloc(NULL, 4096, MEM_COMMIT|MEM_RESERVE (0x3000), PAGE_READWRITE (0x04))
    p == NULL -> 10
p[i] = 0xA5 ^ i  (i = 0..63, volatile)          <- store do PE no VA do guest
releitura byte a byte; divergência -> 11         <- load do PE
r = VirtualFree(p, 0, MEM_RELEASE (0x8000))
    r == FALSE -> 12
return 42
```

Registro da execução (probe de observação, 64 eventos): `exit(42) — processo
sinalizado como encerrado` · `[PROCESS] exit code 42 after 0.71 ms` ·
**exec = 2489 instruções**. Primeira API que falhou: **nenhuma**. Erro Win32:
**nenhum**.

## 5. Resultado de VirtualAlloc

- Chamada observada no log: `[WIN32] API kernel32.dll!VirtualAlloc` (uma única
  chamada, com os valores reais da ABI `0x3000`/`0x04`).
- **Retorno: ponteiro não nulo — VA do guest `0x00FEA000`** (região `w32virt`
  criada por `pr_vm_alloc`, abaixo da imagem em `0x00FF0000`, dentro do espaço
  de 16 MB do guest). Não se assumiu endereço específico.

## 6. Validação da memória do guest

- Escrita pelo próprio PE: `p[i] = 0xA5 ^ i` (64 bytes, `volatile`).
- Leitura pelo próprio PE: os 64 bytes conferiram byte a byte — **sem corrupção**
  (o contrato retornaria 11 em caso de divergência; retornou 42).
- Prova de caminho completo: `VirtualAlloc → VA do guest → tradução de memória →
  store do PE → load do PE` funcionou de verdade.
- Observação independente: o `printf` do PE (`virt: p=0xfea000 free=1`) trafegou
  pelo mesmo mecanismo de stdout do guest para o log.

## 7. Resultado de VirtualFree

- Chamada observada: `[WIN32] API kernel32.dll!VirtualFree(p, 0, MEM_RELEASE)` —
  única chamada, após a validação.
- **Retorno: TRUE** (`free=1` no registro do convidado; rc≠12) — a região
  `w32virt` foi aceita e desmapeada (`pr_vm_unmap`).

## 8. Blocker encontrado, se houver

**Nenhum blocker real no fluxo.** O PE passou na primeira execução
(`rc=42`, `log=64`). Nenhum dos pontos diagnosticados no Grupo 37 interferiu
(`VirtualProtect`/`VirtualQuery`/MBI fora do fluxo — permanecem intocados, como
determinado).

Anotação honesta de harness (não-runtime): a primeira tentativa de *sondagem*
pós-execução (`build/virt_probe`) sofreu use-after-free ao despejar o log
(dump inserido depois de `pr_log_destroy`) e encerrou com SIGSEGV **depois** do
`rc=42` já registrado pelo runtime. O bug era exclusivamente da ferramenta de
observação; corrigido apenas nela (§9).

## 9. Correção realizada, somente se necessária

**No runtime: NENHUMA.** md5 `Sources/PorticoRuntime/src+include` =
`4a23bc7bf8bc2d6e0ff55969290bfcd2` antes e depois — byte-for-byte idêntico.
Única correção do grupo: a sonda de observação `build/virt_probe` (harness de
teste, fora do runtime) — dump do log reposicionado antes da destruição.

## 10. Regressão

```text
make c-test            → 3392 verificações, 0 falhas
swift test             → Executed 63 tests, with 0 failures
gcc -Wall -Wextra      → 0 warnings
gcc -fanalyzer         → 0 diagnósticos
PE battery (15 PEs)    → 15/15 verdes (28/43/180/70 · 72/75/98/118/76/81/194/131/55/200)
                         + hello_input=42 log=105
hello_gl11=42 · hello_sse=7 · hello_gl12=42 · hello_stdio=5
hello_file=42 · hello_file_w=42 · hello_file_seek=42 · hello_mbwc=42
hello_heap=42 · hello_qpc=42 · hello_virt=42 (log=64)
```

Nenhum PE anterior regrediu. Nenhum teste unitário redundante foi criado (o
runtime não foi alterado).

## 11. Critérios de aceitação

| Critério | Resultado |
|---|---|
| `VirtualAlloc` chamado pelo PE | ✓ (`kernel32.dll!VirtualAlloc` no log) |
| ponteiro não nulo | ✓ |
| o endereço é VA do guest | ✓ `0x00FEA000` (região `w32virt` via `pr_vm_alloc`) |
| o PE escreve nesse endereço | ✓ (64 bytes `0xA5^i`, `volatile`) |
| o PE lê os dados escritos | ✓ (releitura conferida) |
| os 64 bytes permanecem corretos | ✓ (sem corrupção) |
| `VirtualFree` aceita a região | ✓ (região `w32virt` desmapeada) |
| `VirtualFree` retorna TRUE | ✓ (`free=1`) |
| o PE termina com `rc=42` | ✓ (exit code 42 após 0,71 ms) |
| não há corrupção | ✓ |
| a regressão permanece verde | ✓ (item 10) |

**Todos os critérios atendidos.**

## 12. Estado final

**VALIDADO POR PE REAL.**

> **Runtime permaneceu intocado; VirtualAlloc + VirtualFree foram validados por
> PE real.**

Fluxo mínimo `VirtualAlloc → memória do guest → escrita pelo PE → leitura pelo PE
→ VirtualFree` confirmado por `hello_virt.exe` na primeira execução. As
pendências documentadas no Grupo 37 (`VirtualQuery`/MBI, `VirtualProtect` por
PE real, flags adicionais) permanecem reservadas para etapas futuras, conforme
determinado.

Não há declaração de compatibilidade geral com aplicações Windows nem com jogos
comerciais.
