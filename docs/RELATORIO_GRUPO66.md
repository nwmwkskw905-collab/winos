# RELATÓRIO GRUPO 66 — kernel32!GetStartupInfoA validada por PE x64 real

## 1. Status
**CONCLUÍDO COM SUCESSO** — `GetStartupInfoA` validada por PE x64 real (`hello_getstartupinfo.exe`, 14848 bytes): import **por nome** resolvido pelo loader (hint **02f8**, IAT `0x140008190`); ABI **RCX = lpStartupInfo** comprovada por disassembly; estrutura real `STARTUPINFOA` (104 bytes, layout medido no toolchain) escrita na memória guest; `cb` sobrescrito com 104 (descoberto pelo teste, §15); campos examinados; standard handles coerentes com `GetStdHandle`; canary/markers intactos; caminho de erro para ponteiro inválido observado (LastError=87); segunda chamada byte a byte idêntica; **20/20 rc=66** (log=37); C **3398/0**; Swift **63/63**; warnings 0; analyzer **7** (pré-existentes); battery 15/15; checkpoints **G52–G65 14/14 verdes**.

## 2. API validada
`kernel32.dll!GetStartupInfoA(LPSTARTUPINFOA lpStartupInfo)` → **VOID** (sem retorno útil em EAX). Resultado = estrutura escrita no ponteiro guest. `GetStartupInfoW` = **0 implementações** (fora do escopo).

## 3. Inventário (§1 — read-only, antes de alterar)
Buscas por `GetStartupInfoA|f_GetStartupInfoA|GetStartupInfoW|f_GetStartupInfoW|STARTUPINFO|STARTUPINFOA|STARTUPINFOW|startup_info|startupinfo` em Sources/Tests/realpe/tools + catálogo:
1. handler: **NÃO existe**; 2. TODO: **NÃO existe** (a API não constava do catálogo); 3. registro no catálogo: **NENHUM**; 4. `stdcall_bytes`: N/A → criado **4** (1 arg × 4, padrão `GetStdHandle=4`); 5. estrutura equivalente: **NENHUMA** — únicos hits = `_startupinfo` do CRT (`__getmainargs`, `pr_win32.c` L1177/1221: struct `{newmode}` de 1 campo, distinta); 6/7. infra de startup: **NÃO existe** (sem desktop/title/flags/show-state de processo; só `f_ShowWindow` por **janela** L3681 `w->visible`, IMPL_NOTE user32 L4897); standard handles: `f_GetStdHandle` (L481–493) = `PR_WIN32_H_STDIN/OUT/ERR` (`0xF0F0F003/4/5`, L35–37); 8. CRT: **nenhum dos 52 PEs importa `GetStartupInfoA`** — nenhuma dependência atual.

## 4. Handler
**Criado**: `f_GetStartupInfoA` (antes de `f_GetProcAddress`): `*st=PR_OK`; `n<1`→`PR_ERR_INVALID`; `a[0]==0` ou `pr_win32_ptr(ctx, a[0], 104)` NULL → `last_error=87` (soft, VOID); `memset(si,0,104)`; escrita por offset (padrão `f_GetSystemInfo`): `cb=104`; strings/numéricos/reserved = 0; `dwFlags=0x100`; `hStd*=PR_WIN32_H_STD*` (64-bit zero-ext); `return 0`.

## 5. Catálogo
**1 registro novo** (não substituiu TODO): `IMPL("kernel32.dll", "GetStartupInfoA", f_GetStartupInfoA, 4)` após a linha de `GetModuleFileNameA`. TODOs: 31 antes = 31 depois (**0 removidos**). `GetStartupInfoW` = 0 registros.

## 6. Layout da STARTUPINFOA (§3 — verificado no toolchain, não inventado)
Probe compilado com `x86_64-w64-mingw32-gcc` (`sizeof`/`offsetof` extraídos de `build/si_layout.o`, seção `.rdata`, little-endian):

| Campo | Offset | Campo | Offset |
|---|---|---|---|
| (sizeof) | **104** | dwYCountChars | 52 |
| cb | 0 (DWORD) | dwFillAttribute | 56 |
| lpReserved | 8 (QWORD) | dwFlags | 60 (DWORD) |
| lpDesktop | 16 | wShowWindow | 64 (WORD) |
| lpTitle | 24 | cbReserved2 | 66 (WORD) |
| dwX | 32 | lpReserved2 | 72 (QWORD) |
| dwY | 36 | hStdInput | 80 (QWORD) |
| dwXSize | 40 | hStdOutput | 88 |
| dwYSize | 44 | hStdError | 96 |
| dwXCountChars | 48 | | |

Padding x64 (ponteiros alinhados em 8) confirmado pelo toolchain; PE usa `STARTUPINFOA` de `<windows.h>` do mesmo toolchain.

## 7. Infraestrutura existente
Reutilizada: `pr_win32_ptr` (validação guest→host, padrão `f_GetSystemInfo`); constantes `PR_WIN32_H_STDIN/OUT/ERR` de `f_GetStdHandle` (**sem segunda representação de handles**, §11). **Não** existe infra de startup (desktop/title/show-state) — não foi criada arquitetura de startup (§14): menor estrutura possível = a escrita direta por offset no buffer guest.

## 8. PE criado
`realpe/hello_getstartupinfo.c` → `Tests/PorticoRuntimeTests/data/hello_getstartupinfo.exe` (14848 B). Auxiliares já validadas: `SetLastError`/`GetLastError` (G52/G53-família) + `GetStdHandle` (G51, exigida §11 para comparação). Nenhum PE histórico alterado.

## 9. Compilação
```
x86_64-w64-mingw32-gcc -O2 -s realpe/hello_getstartupinfo.c -o Tests/PorticoRuntimeTests/data/hello_getstartupinfo.exe
```

## 10. Imports
`KERNEL32.dll`: `GetStartupInfoA` (hint **02f8**, IAT `0x140008190`), `GetStdHandle` (02fb/`0x140008198`), `GetLastError` (0283/`0x140008188`), `SetLastError` (0554/`0x1400081b0`) + CRT (`msvcrt.dll`).

## 11. Execução inicial (§19 — runtime intacto)
`./build/dbg_diag Tests/PorticoRuntimeTests/data/hello_getstartupinfo.exe` → `prepare falhou — EXECUTION STOPPED — Unsupported Win32 API — Module: KERNEL32.dll — Function: GetStartupInfoA — Technical: API conhecida do módulo mas sem implementação` (Address: (—) = bloqueado no prepare). Depois da implementação: `exited=1 rc=66 steps=1 exec=1340 — ExitProcess(msvcrt!exit)`.

## 12. Primeiro blocker
`Unsupported Win32 API | kernel32.dll | GetStartupInfoA` = lacuna real (API fora do catálogo) — único blocker; resolvido pela implementação mínima §14.

## 13. ABI/disassembly (evidência do PE real)
```
movl   $0x12345678,0x4938(%rip)  # 0x1400070c8   ; a.si.cb = SENTINEL (pré-chamada)
call   *%r12                                     ; SetLastError(0xDEAD) (IAT 0x1400081b0)
lea    0x8(%rbx),%rcx              ; *** RCX = lpStartupInfo = &a.si = 0x1400070c8 ***
                                   ;     (rbx = &a; si em +8 após pre[8]; bloco em .bss)
mov    0x59f2(%rip),%rbp # 0x140008190           ; rbp = IAT[GetStartupInfoA]
call   *%rbp                       ; *** chamada real (VOID) ***
mov    0x59e1(%rip),%rdi # 0x140008188           ; rdi = IAT[GetLastError]
call   *%rdi
mov    %eax,%edx
mov    $0xa,%eax                    ; código de falha 10
cmp    $0xdead,%edx                 ; LastError preservado?
jne    <fail 10>
mov    0x490a(%rip),%edx # 0x1400070c8           ; lê si.cb (DWORD)
...
cmpq   $0x0,0x48c5(%rip) # 0x140007110           ; lpReserved2 (@+72) == NULL
mov    0x48c4(%rip),%r14 # 0x140007118           ; r14 = hStdInput (@+80)
mov    0x593d(%rip),%r13 # 0x140008198           ; r13 = IAT[GetStdHandle]
mov    $0xfffffff6,%ecx             ; *** RCX = STD_INPUT_HANDLE = −10 (32-bit) ***
call   *%r13
cmp    %rax,%r14                    ; *** hStdInput == GetStdHandle(-10)? (64-bit) ***
je     ... / mov $0x10,%eax         ; fail 16
mov    $0xfffffff5,%ecx / call *%r13 ; −11 = STD_OUTPUT_HANDLE
mov    $0xfffffff4,%ecx / call *%r13 ; −12 = STD_ERROR_HANDLE
```
Buffer: `&a.si` = **`0x1400070c8`** (`.bss` de `static struct block a, b`; `pre[8]`+`si`+`can[8]`); tamanho reservado = 8+104+8. Consumo pós-chamada = leituras por offset (`mov` DWORD cb, `cmpq` qwords lp*/hStd*).

## 14. lpStartupInfo
`RCX` = ponteiro guest `0x1400070c8`; handler resolve via `pr_win32_ptr(ctx, a[0], 104)` e escreve **somente 104 bytes** por offsets medidos — nunca `memcpy((void*)guest,…)` direto (§15).

## 15. `cb` (§8 — descoberta, sem impor antecipadamente)
Teste preencheu a struct com `0xA5` e posicionou **sentinel `0x12345678`** em `cb` antes da chamada (para distinguir preserva vs sobrescreve). **Comportamento observado: sobrescreve com `sizeof(STARTUPINFOA)` = 104** (`cb != sentinel` e `cb == 104`; o PE aceita as duas hipóteses legítimas — 11 = qualquer outro valor — e o ramo observado foi "sobrescreve"). Referência Windows: `GetStartupInfo` é documentado como "retrieves the STARTUPINFO specified when the process was created" — a estrutura é preenchida **pelo sistema** (inclusive `cb`) = semântica de sobrescrever = **coerente**; sem divergência relevante declarada.

## 16. Campos numéricos (§9)
| Campo | Observado (contrato Portico) | Classificação |
|---|---|---|
| dwX, dwY | 0 | zero (sem infra de posição) |
| dwXSize, dwYSize | 0 | zero (sem infra de tamanho) |
| dwXCountChars, dwYCountChars | 0 | zero |
| dwFillAttribute | 0 | zero |
| dwFlags | `0x100` | **constante** (STARTF_USESTDHANDLES) |
| wShowWindow | 0 | zero/sem suporte (sem STARTF_USESHOWWINDOW) |
| cbReserved2 | 0 | zero (reservado) |

Nenhum valor configurável/fabricado — tudo zero ou a constante documentada em §19.

## 17. Strings (§10)
`lpReserved`, `lpDesktop`, `lpTitle` = **NULL** — registrado explicitamente: **o runtime não fornece esses campos** (sem desktop/título de processo no guest). Nenhuma string artificial criada. Verificação estrutural do PE: ponteiros == 0 (contrato 15 = ponteiro não-NULL seria falha). (Se fossem não-NULL, o PE exigiria endereço guest válido + NUL; vacuamente satisfeito.)

## 18. Standard handles (§11)
`hStdInput=0xF0F0F003`, `hStdOutput=0xF0F0F004`, `hStdError=0xF0F0F005` = **exatamente** `PR_WIN32_H_STDIN/OUT/ERR` (o que `f_GetStdHandle` devolve) — mesma representação, **sem segunda representação de handles**. PE compara `si.hStd* == GetStdHandle(STD_*_HANDLE)` (`$0xfffffff6/f5/f4` em ECX) → contratos 16. `GetStdHandle` **não** foi alterado.

## 19. Flags (§12)
`dwFlags = 0x00000100` = **`STARTF_USESTDHANDLES`** (documentação Windows: a flag declara que `hStd*` são os handles de std — "Sets the standard input, standard output, and standard error handles"; `hStd*` são ignorados sem ela) = contrato coerente e mínimo com os handles fornecidos. **Sem** `STARTF_USESHOWWINDOW`/`STARTF_USEPOSITION`/`STARTF_USESIZE`/`STARTF_USEFILLATTRIBUTE` (sem infra de janela/show-state de processo; `f_ShowWindow`/`w->visible` é por janela e não foi envolvido). Nenhuma implementação gráfica iniciada.

## 20. Reserved fields (§13)
`cbReserved2 = 0`, `lpReserved2 = 0` — nenhum dado inventado; PE valida `cbReserved2==0` e `lpReserved2==NULL` (contrato 12).

## 21. Canary (§7/§18)
`pre[8]=0x5A` **antes** e `can[8]=0xC3` **depois** de cada `STARTUPINFOA` (blocos `a` e `b`): intactos após chamada principal, chamada de ponteiro inválido e segunda chamada (contrato 14 = corrupção). Escrita estritamente limitada aos 104 bytes da estrutura.

## 22. LastError (§16)
Sucesso: `SetLastError(0xDEAD)` → após `GetStartupInfoA` = **`0xDEAD` preservado** (VOID sem caminho de LastError = observado, registrado). **Caminho de erro implementado e exercitado**: ponteiro guest inválido (`(STARTUPINFOA*)1`) → **`last_error = 87 (ERROR_INVALID_PARAMETER)`**, sem escrita, sem crash, processo continua (contrato 13). Sem equivalência com Windows declarada (§33).

## 23. Segunda chamada (§21)
`GetStartupInfoA(&a.si)` e `GetStartupInfoA(&b.si)` (b pré-preenchido 0xA5 + sentinel): **todas as 104 posições byte a byte idênticas** (contrato 17). Todos os campos são determinísticos (zeros/constantes) — nenhuma variação legítima identificada.

## 24. Correções realizadas (escopo mínimo)
1. `pr_win32.c` — `f_GetStartupInfoA` (novo, ~20 linhas).
2. `pr_win32.c` — catálogo: **1 linha nova** `IMPL("kernel32.dll", "GetStartupInfoA", f_GetStartupInfoA, 4)`.
Mais: fonte do PE. **Sem** GetStartupInfoW/GetModuleFileNameW/MultiByteToWideChar/WideCharToMultiByte/GetModuleHandleEx/CommandLineW novos; **sem** GetStdHandle alterado; **0 TODO removidos**; **sem** UI/DirectX/Metal/áudio.

## 25. MD5
| Arquivo | antes | depois |
|---|---|---|
| `pr_win32.c` | `9aa33bb8895b2510b77519ef80946f58` (= depois de G65) | `8090fb431a58da44f2a90e7531e2182b` |
| Combinado (todos .c/.h) | `5da2fe2a5c80192adccc25166c8391f2` (`build/g66_before.md5`) | `c6861da2ba86ee17420e3d32316256fc` |

Mudanças = estritamente as 2 de §24 + novo `realpe/hello_getstartupinfo.c` (+ `build/si_layout.c` = artefato de probe em build/, regenerável).

## 26. 20 execuções (§22)
**20/20** `exited=1 rc=66 log=37` (determinístico): struct/canary/cb/campos/handles/LastError/ponteiro inválido/identidade verificados em todas; sem crash, sem `Unsupported`, sem variação.

## 27. C (§23)
**3398 verificações, 0 falhas** (baseline exato de G60; nenhum CHECK adicionado — o PE cobre o caminho real; ver §34).

## 28. Swift (§24)
**Executed 63 tests, with 0 failures** ✓ (intocados).

## 29. Warnings (§25)
**0** (todos os `.c` com `-Wall -Wextra`).

## 30. Analyzer (§26)
**7/0** (pré-existentes: pr_cpu.c:126, pr_cpu64.c:401, pr_gfx.c:98, pr_peproc.c:831, pr_win32.c:3611/3615, pr_hello.c:885; **0 novos**).

## 31. PE battery (§27)
**15/15** — logs 28/43/180/70/72/75/98/118/76/81/194/131/55/200/105 (rc=42/5). Pipeline G1–G63 + coorte G51–G65 **15/15 verdes** (rc 42…65).

## 32. Checkpoints (§28)
**G52–G65 = 14/14 verdes**: G52=52/31, G53=53/38, G54=54/30, G55=55/36, G56=56/32, G57=57/33, G58=58/39, G59=59/40, G60=60/45, G61=61/33, G62=62/33, G63=63/33, G64=64/29, G65=65/35 (+G66=66/37 = 15/15). Checklist acumulada **37/37** (36 anteriores + **win32 startup info (G66 GetStartupInfoA)** ✓).

## 33. Limitações
1. Conteúdo mínimo determinístico: `lpReserved/lpDesktop/lpTitle = NULL`; numéricos zero; `wShowWindow = 0` sem `STARTF_USESHOWWINDOW` — **sem** desktop/título/posição/show-state reais (infra inexistente; não fabricada).
2. `hStd*` = constantes canônicas `PR_WIN32_H_STD*` (não HANDLEs de objeto); coerentes com `GetStdHandle`, não com o Windows real (HANDLEs herdados via `CreateProcess`).
3. Ponteiro inválido: Portico = erro suave `87` + sem escrita + continuidade; **Windows = Access Violation/exceção** — diferença de implementação documentada (§22), não declarada equivalente.
4. `cb` = sobrescrito com 104 (coerente com o preenchimento pelo sistema documentado); o teste aceita também preservação (contrato de descoberta §15).
5. `GetStartupInfoW` e família Unicode fora do escopo; `lpReserved2` sempre NULL (sem dados de reserva do CRT do Windows).

## 34. Cobertura efetivamente demonstrada
Só `GetStartupInfoA` por import real resolvido pelo loader + chamada real com ABI x64 documentada: RCX→estrutura 104 bytes escrita por offsets medidos no toolchain; `cb` (descoberta preserva/sobrescreve); todos os campos numéricos; strings NULL registradas; reserved zero/NULL; `hStd*` comparados com `GetStdHandle`; `dwFlags` documentado; canary/markers; LastError preservado em sucesso; caminho de erro `87` em ponteiro inválido; identidade entre 2 chamadas byte a byte. Nada além disso.

## 35. Próximo candidato (só indicação — não executar)
`kernel32!GetStartupInfoW` (família W — exige UTF-16, fora do escopo atual) ou `kernel32!GetFileAttributesA` (bytes, 1 argumento). NÃO implementados aqui.
