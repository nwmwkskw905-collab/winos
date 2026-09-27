# RELATÓRIO GRUPO 73 — msvcrt!puts validada por PE x64 real

## 1. Status
**G73 CONCLUÍDO COM SUCESSO** — `msvcrt.dll!puts` implementada sobre a infraestrutura de stdout **existente** (`stdio_emit` — o mesmo caminho de `printf`/`fwrite`/`vfprintf`/`fputc`; **nenhuma segunda infraestrutura**) e validada por PE x64 real `hello_puts.exe`. Prova de saída **byte a byte exata (539 bytes idênticos)** via dreno da captura de console do runtime (`pr_win32_stdout_read`): `"hello\n"` (newline real, não `"hello"`), `puts("")` = linha vazia, string de 300 bytes, espaços preservados, ASCII `ABC123_-+=.,!` byte a byte, ordem `A\nB\nC\n`. Retorno: **0 (não-negativo) em sucesso / EOF (-1) em erro**; ponteiro inválido/NULL = **EOF sem crash**; **LastError preservado** (`0xDEAD`) — observado, não imposto. **20/20 rc=73** (log=334, idênticos); C **3398/0**; Swift **63/63**; warnings 0; analyzer **7/0 novos**; battery 15/15 (fingerprints byte-idênticos); **G52–G73 = 22/22 verdes**; checklist **44/44**; `build/win_fs` = só `fixture.txt`.

## 2. Inventário read-only (§1 — ANTES de qualquer alteração)
Buscas (`puts|f_puts|printf|f_printf|fwrite|f_vfprintf|stdout|stderr|__iob_func|PR_WIN32_H_STDOUT|PR_WIN32_H_STDERR`) em `Sources/`, `Tests/`, `realpe/`, `tools/` + catálogo:

1. `f_puts`? **NÃO** · 2. `puts` parcial? **NÃO** · 3. TODO para `puts`? **NÃO** (30 TODOs nenhum do CRT) · 4. `IMPL` para `puts`? **NÃO** · 5. PE histórico importando `puts`? **NENHUM atualmente** (o PE do G72 importou `puts` no 1º build — blocker documentado lá — e foi ajustado para `printf`; hoje os 58 PEs não importam `puts`) → **lacuna real confirmada nos dois sentidos** (0 handler + 0 TODO).
6. **Como printf/fwrite/vfprintf escrevem**: `stdio_vformat` → `stdio_emit(ctx, idx, buf, len)` (L532) = buffer de linha 2048 B/stream, flush a cada `'\n'` (comportamento real de console) → `out_append` (L500) = captura em `ctx->out_buf` + `w32_log(DEBUG, "console(stdout|stderr): ...")`. `fwrite`/`fputc` também vão direto a `stdio_emit`.
7. **Infra de stdout**: `PR_WIN32_H_STDOUT=0xF0F0F004`/`PR_WIN32_H_STDERR=0xF0F0F005` (L37); `__iob_func` = 3 FILEs (48 B) em memória do convidado via `tls_slots[63]` (convenção iob); `stdio_stream_of` = identidade FILE*→stream.
8. **Helper reutilizável**: **`stdio_emit`** (saída) + **`pr_win32_ptr`** (validação de ponteiro do convidado sem crash) + `diagf` (diagnóstico) — todos reutilizados.
9. **Mecanismo de log**: `w32_log` → `pr_log_write` (ring buffer; `pr_log_count` = "log=N" dos drivers) + captura integral em `ctx->out_buf` drenável por **`pr_win32_stdout_read`** (API existente, pr_win32.h L180).
10. Testes unitários de saída textual: nenhum dedicado aos handlers stdio (a cobertura real é por PEs).
11. **Reutilização sem segunda infra**: confirmada — `f_msvcrt_puts` usa exatamente o trio `pr_win32_ptr` + `stdio_emit` + `diagf`.

## 3. API validada (§2)
`int puts(const char *string)` → `int`. Confirmado pelo PE real/disassembly: **argumento em `RCX`** (`lea 0x40(%rsp),%rcx` = ponteiro **64-bit** para a string do convidado), **retorno em `EAX`** (`mov %eax,%esi` logo após a chamada), chamada via **IAT real de `msvcrt.dll`** (`call 0x140007b78` → thunk `jmp *0x57b2(%rip) # 0x14000d330` = IAT[puts], hint **048c**). 1 argumento = **8 bytes** no catálogo (padrão do projeto para 1 ponteiro — idêntico a `strlen`/8; confirmado pela ABI, não copiado).

## 4. Infraestrutura reutilizada (§1/§3)
`stdio_emit` (stdout, um único emit com `conteúdo+'\n'` = uma linha completa de console), `pr_win32_ptr` (validação byte a byte), `diagf`, captura `out_buf`/log. **NÃO** criado segundo stdout/buffer/roteiro de logging; **NÃO** alterada a arquitetura de logging; `printf`/`fwrite`/`vfprintf`/`fputc`/`GetLastError`/`SetLastError` **intocados**.

## 5. Contrato/decisões de implementação (§3/§6)
| Caso | Contrato implementado | Base |
|---|---|---|
| Sucesso | escreve `str + '\n'` (um emit) em **stdout**; retorna **0** (não-negativo) | §6: CRT = "não-negativo em sucesso" (valor **não especificado** — adotado 0, consistente entre chamadas; **não** inventado p/ o teste) |
| String vazia | emite só `'\n'` (linha vazia) | §4 |
| Erro (NULL/fora do espaço/NUL ausente) | **EOF (-1)** via `EAX`; **sem crash**; `diagf` no log | §3.7 + §6 ("EOF em erro" = canal do CRT) |
| LastError | **não tocado** (API CRT) | §7 (observar, não impor) |
| Limite de varredura | `0x4000` (mesma convenção de `f_msvcrt_wcslen`) | não inventar família nova |
| Modificação da entrada | **nunca** — só leitura (`memcpy` para buffer temporário) | §12 |

**Divergência Windows/MSVCRT registrada**: em Windows, `puts(NULL)` causa acesso inválido (crash/SEH); o Portico devolve **EOF sem crash** (requisito §3.7). O valor de retorno de sucesso no CRT real é "qualquer não-negativo" — adotamos **0** explicitamente como contrato Portico.

## 6. Alterações realizadas (mínimas)
1. `pr_win32.c` — `f_msvcrt_puts` (novo, ~35 linhas) antes de `f_memset`.
2. `pr_win32.c` — catálogo: **1 linha** `IMPL("msvcrt.dll", "puts", f_msvcrt_puts, 8)` após `printf`.
3. `realpe/hello_puts.c` + `Tests/PorticoRuntimeTests/data/hello_puts.exe` (PE novo — único PE do grupo).
4. `tools/dbg_input.c` — arg auxiliar **opcional** `dumpout` (drena `pr_win32_stdout_read` e imprime o console entre marcadores) — **teste auxiliar necessário** à prova de saída (§19 permitido); sem ele a saída histórica do driver permanece **byte-idêntica** (primeira linha `exited/rc/log` inalterada; fingerprints preservados). **Nenhum PE histórico modificado** (§11).

## 7. Catálogo (§14)
`puts handler = 1` (`static uint64_t f_msvcrt_puts`), `puts catalog entry = 1`, refs `f_msvcrt_puts` = 2 (handler+catálogo), **TODOs = 30** (inalterados), **sem duplicação**.

## 8. PE criado (§5)
`realpe/hello_puts.c` → `Tests/PorticoRuntimeTests/data/hello_puts.exe` (40448 B). Casos A–F + canários + ponteiro inválido/NULL + LastError observado. Contratos: 10 = caso A com retorno negativo; 11 = canário prefixo corrompido; 12 = string modificada; 13 = canário sufixo corrompido; 14 = erro sem EOF; 15 = retorno inconsistente em B–F; 73 = OK.

## 9. Compilação
```
x86_64-w64-mingw32-gcc -O2 -s realpe/hello_puts.c -o Tests/PorticoRuntimeTests/data/hello_puts.exe
```
**0 warnings** (ajustes no PE durante o desenvolvimento, contratos intactos: ponteiros intencionais via `volatile ULONG_PTR` — `unsigned long` = 32-bit em Win64 LLP64 causava `-Wint-to-pointer-cast`; nenhum impacto no runtime).

## 10. Imports (§9 — por nome; loader real; sem resolução manual)
`msvcrt.dll`: **`puts` (hint 048c / IAT `0x14000d330`)**, `fprintf`, `vfprintf`, `malloc`, `memcpy`, `memset`, `signal`. `KERNEL32.dll`: `GetLastError` (0283), `SetLastError` (0554). `puts` **não** foi substituído por `printf` nos casos testados (printf só nos diagnósticos `[*]`).

## 11. Execução inicial (§10)
**Runtime intacto (ANTES da implementação)** — parada honesta:
```
prepare falhou: EXECUTION STOPPED — Reason: Unsupported Win32 API
Module: msvcrt.dll — Function: puts — Architecture: x86-64
Technical: API conhecida do módulo mas sem implementação
```
Confirma que `puts` era **realmente uma lacuna** antes da alteração. Depois da implementação: `exited=1 rc=73 log=334` na primeira execução real.

## 12. Blocker
`Unsupported Win32 API | msvcrt.dll | puts` = lacuna real do grupo (§2) — resolvida por `f_msvcrt_puts` (§6). **Nenhum outro blocker** apareceu na validação (escopo não precisou expandir — §Objetivo).

## 13. ABI/disassembly (§8 — obrigatório)
```
140007d1c:  b9 ad de 00 00        mov    $0xdead,%ecx        ; SetLastError(0xDEAD)
140007d21:  ff 15 e9 54 00 00     call   *0x54e9(%rip)       # 0x14000d210  ; IAT SetLastError
140007d27:  48 8d 4c 24 40        lea    0x40(%rsp),%rcx     ; *** RCX = ponteiro 64-bit da string ***
140007d2c:  e8 47 fe ff ff        call   0x140007b78         ; *** puts (thunk) ***
140007d31:  89 c6                 mov    %eax,%esi           ; *** retorno int consumido de EAX ***
140007d33:  ff 15 af 54 00 00     call   *0x54af(%rip)       # 0x14000d1e8  ; IAT GetLastError
140007d39:  89 f2                 mov    %esi,%edx           ; (diagnóstico: ret)
```
```
thunk:   140007b78:  ff 25 b2 57 00 00  jmp *0x57b2(%rip)  # 0x14000d330   ; IAT[puts]
imports: 0000d330  <none>  048c  puts                                    ; por nome
```
Provado: **RCX = ponteiro de string (64-bit, `lea` de pilha — sem truncamento)**; **chamada pela IAT real de `msvcrt!puts`** (9 call sites → mesmo thunk); **retorno consumido de `EAX`** (`mov %eax,%esi`); convenção esperada (1 arg RCX, retorno EAX); nenhuma convenção divergente.

## 14. Prova da saída/newline (§4 — saída observável do runtime)
A prova vem da **captura de console do runtime** drenada por `pr_win32_stdout_read` (§2.9). Verificação **byte a byte** (python): os **539 bytes** drenados são **exatamente** o fluxo esperado — `EXATO: True`:
```
hello\n                    ← puts("hello") = "hello\n" (NÃO "hello")
[*] A ret=0 last=0000DEAD\n
\n                          ← puts("") = linha vazia
[*] B ret=0\n
xxxx(...300...)x\n          ← string grande + newline
[*] C ret=0\n
WinOS Portico Runtime\n    ← espaços preservados
[*] D ret=0\n
ABC123_-+=.,!\n             ← ASCII byte a byte
[*] E ret=0\n
A\nB\nC\n                   ← múltiplas chamadas na ordem exata
[*] F ret=0,0,0\n
WinOS\n                    ← char text[] = "WinOS" (§12)
[*] X (invalid ptr) ret=-1\n
[*] N (null ptr) ret=-1\n
[*] puts: A-F + canaries + EOF em erro OK\n
```
**Não** se presumiu o newline do `printf` — cada `puts` emitiu exatamente `string+'\n'` (um emit = uma linha completa; §5 verificado caso a caso).

## 15. Casos A–F (§5)
**A "hello"** → `hello\n`, ret 0 ✓ · **B ""** → `\n`, ret 0 ✓ · **C 300×'x'** → `x…x\n`, ret 0 ✓ (sem limite de buffer minúsculo — varredura por NUL no espaço do convidado) · **D "WinOS Portico Runtime"** → espaços preservados ✓ · **E "ABC123_-+=.,!"** → byte a byte ✓ · **F `puts("A"); puts("B"); puts("C")`** → `A\nB\nC\n` na ordem exata ✓. Retornos das 9 chamadas de sucesso: todos **0** (consistente, contrato 15).

## 16. Retorno (§6)
Contrato adotado = comportamento padrão do CRT: **não-negativo em sucesso (0), EOF (-1) em erro** — documentado (§5) e validado nos dois lados (retornos 0 observados; `-1` observado nos erros). **Nenhum valor arbitrário** inventado para o teste; divergência do valor específico em relação a CRTs reais (que podem devolver a contagem) registrada como contrato Portico (§5).

## 17. LastError (§7)
`SetLastError(0xDEAD)` → `puts("hello")` → `GetLastError()` = **`0x0000DEAD`** (impresso pelo próprio PE: `[*] A ret=0 last=0000DEAD`) = **preservado** — comportamento **observado** (o handler não toca `last_error`); `GetLastError`/`SetLastError` **não** modificados. Como é API CRT, não se atribui convenção Win32 de erro (erros usam o canal EOF do CRT).

## 18. Ponteiro inválido (§13)
Testado com segurança pelo PE (a validação é estrutural em `f_msvcrt_puts` via `pr_win32_ptr`): `puts((char*)0x1)` → **`-1` (EOF), sem crash do host**; `puts(NULL)` → **`-1` (EOF), sem crash** (contrato Portico — divergência do Windows registrada §5). LastError **não** alterado nos erros (observado: `0xDEAD` permaneceria; convenção Portico da família = só APIs Win32 mexem em LastError).

## 19. Não-corrupção de memória (§12)
Canários de 8 bytes antes/depois do buffer (`0xA1..0x18` / `0x28..0x9F`) **intactos** após todas as chamadas (contratos 11/13). `char text[] = "WinOS"` permanece `"WinOS\0"` após `puts` (contrato 12 — verificado byte a byte, inclusive o 6º byte NUL); strings dos casos B/C também verificadas intactas. `puts` **não modifica** a string guest (só leitura + `memcpy` para buffer temporário).

## 20. C (§15)
**3398 verificações, 0 falhas** (nenhum CHECK adicionado — cobertura real é o PE x64; §15 registra a diferença apenas se legítima — não houve).

## 21. Swift (§15)
**Executed 63 tests, with 0 failures** ✓.

## 22. Warnings (§15)
**0** (`-Wall -Wextra -Werror=implicit-function-declaration` no runtime e drivers; build MinGW do PE também 0).

## 23. Analyzer (§15)
**7 total / 0 novos / 7 pré-existentes** (pr_cpu.c NonNull; pr_cpu64.c BitwiseShift; pr_gfx.c NonNull; pr_peproc.c DeadStores; pr_win32.c ×2 DeadStores; pr_winhello.c DeadStores) — idêntico ao G72; nada mascarado.

## 24. PE battery (§16)
**15/15 rc=42** — logs **28/43/180/88/72/75/98/118/76/81/194/131/55/200/105** (hello_real/user/app/gdi/gl..gl10/input) = **fingerprints históricos byte-idênticos**. Nenhum PE histórico apresentou regressão (§11).

## 25. 20 execuções (§17)
**20/20** `exited=1 rc=73 log=334` **idênticos** (rc/exited/steps/exec/log constantes); saída textual byte-idêntica entre execuções (verificação §14 reproduzida); nenhum crash; nenhum `Unsupported`; nada não determinístico.

## 26. Checkpoints G52–G73 (§18)
**22/22 verdes**: G52=52/31 … G69=69/55, G70=70/94, G71=71/59, G72=72/274, **G73=73/334**.

## 27. Checklist acumulada (§18)
**44/44** — item acrescentado: **44. msvcrt puts (G73 puts por PE x64 real)** ✓ (43 anteriores inalteradas).

## 28. MD5 (§19)
| Arquivo | antes | depois | o que mudou |
|---|---|---|---|
| `pr_win32.c` | `eb41583c355dd665ad189cc70dcca6f7` (= pós-G72, reverso exato confirmado) | `75b1c8f954370b73490e9d1ccc6e84a5` | `f_msvcrt_puts` + 1 linha de catálogo (§6) |
| Combinado (todos .c/.h) | `cfa85db99488c278ea886979870db5ad` | `a59e7cef7d2ef7ccd3296edb01a51651` | `pr_win32.c` + `realpe/hello_puts.c` + arg `dumpout` em `tools/dbg_input.c` |

Escopo restrito ao G73 confirmado (§19): handler `puts`, catálogo, PE novo, 1 teste auxiliar necessário (`dumpout`).

## 29. Limpeza do filesystem (§20)
`hello_puts` **não cria arquivos** (nenhuma API de arquivo). Após todos os testes: `build/win_fs` = **só `fixture.txt`** (estado equivalente ao pré-G73; verificado por inspeção explícita).

## 30. Limitações
1. `puts` escreve em **stdout** (via `stdio_emit`); suporte a `stdin`/`fprintf` em FILEs arbitrários continua fora do runtime (comportamento existente).
2. Contrato de retorno = **0/EOF** (contrato Portico §5) — não se afirma bit-identicidade com o valor retornado por msvcrt/UCRT reais.
3. `puts(NULL)` = EOF sem crash = **divergência documentada** vs Windows (crash).
4. Varredura de string limitada a **0x4000** bytes (convenção da família `wcslen`).
5. Guest não consegue reler o próprio stdout: a prova de newline é pela captura observável do runtime (§4) — o PE valida contrato/retornos/canários.
6. Apenas `puts` implementado — **nenhuma outra API CRT** adicionada (§Objetivo); `gets`/`putchar`/`_write` etc. continuam fora.
7. Sem compatibilidade geral Windows/Winlar/GTA V/MX Bikes/jogos.

## 31. Compatibilidade efetivamente demonstrada
Somente o que o PE real demonstrou: `msvcrt!puts` resolvida pelo loader real (import por nome, IAT `0x14000d330`); ABI `RCX=char*` 64-bit / `EAX=int`; saída `string+'\n'` byte a byte (A–F, vazia, 300 B, espaços, ASCII); ordem entre chamadas; retornos 0/EOF; ponteiro inválido/NULL sem crash; canários e string intactos; LastError preservado; 20/20 estável. **Sem** declaração de compatibilidade geral.

## 32. Próximo candidato (somente indicação — NÃO executar G74; estado real pós-G73)
Lacunas reais restantes (nenhuma observada por blocker novo neste grupo): **(1) `kernel32!ReadFileEx`** (TODO real; E/S assíncrona — complexidade alta); **(2) família Unicode `MultiByteToWideChar`/`WideCharToMultiByte`** (atenção: `f_MultiByteToWideChar` é parcial **pré-existente** — inventário obrigatório, trabalhar só na lacuna real); **(3)** outras APIs CRT sem handler (ex.: `putchar`/`_write`) — só se exigidas por PE real. **G73 indica: `kernel32!ReadFileEx`** (é o TODO real mais antigo) **ou a família Unicode** (menor); a escolha deve ser confirmada pelo inventário do próximo grupo.
