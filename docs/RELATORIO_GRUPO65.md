# RELATÓRIO GRUPO 65 — kernel32!GetModuleFileNameA (variante A) validada por PE x64 real

## 1. Status
**CONCLUÍDO COM SUCESSO** — `GetModuleFileNameA` validada por PE x64 real (`hello_getmodulefilename.exe`, 14848 bytes): import **por nome** resolvido pelo loader (hint **0297**, IAT `0x140008188`); retorno **DWORD** consumido via `mov %eax,…`; escrita real no buffer guest com NUL, canaries intactos; conteúdo objetivo `"app.exe"` = **representação real do módulo principal mantida pelo loader** (§6); truncamento, `nSize=0`, identidade e LastError testados; **20/20 rc=65** (log=35); C **3398/0**; Swift **63/63**; warnings 0; analyzer **7** (pré-existentes); battery 15/15; checkpoints **G52–G64 13/13 verdes**.

## 2. API validada
`kernel32.dll!GetModuleFileNameA(HMODULE hModule, LPSTR lpFilename, DWORD nSize) → DWORD`. Variante A estritamente bytes. `GetModuleFileNameW` = **0 implementações** (família W inteira fora do escopo, §14).

## 3. Inventário antes da alteração (§1)
Sources/Tests/realpe/tools + catálogo kernel32, read-only:
- `GetModuleFileNameA`: **somente** `TODO("kernel32.dll", "GetModuleFileNameA", "caminho do módulo no FS do convidado")` em `pr_win32.c` **L4859**; handler **NÃO existia** (família inteira = 0).
- `f_GetModuleFileNameA` / `f_GetModuleFileNameW` / `GetModuleFileNameW`: **0 ocorrências**.
- `guest_cmdline`/`guest_cmdline_w`: `pr_win32_ctx` L201–202 (preenchidos em `pr_win32_bind_vm` com `"portico"`).
- Estruturas: `pr_win32_ctx` L186 (`img_base`, `img_size`, `module_name[64]`), L194 (`guest_img_base`), L208 (`fs_root[256]`); `pr_pe_loaded` (`pr_pe.h` L73–91, `module_name[PR_PE_MAX_NAME=64]` = "nome do arquivo sugerido"); `proc_mod` L34; `pr_peproc` L45.
- FS virtual: `fs_root[256]` + `pr_win32_set_fs_root` (L2492) + normalizador Windows→host L1781–1804.
- APIs existentes de nome/caminho: `f_GetModuleHandleA` (catálogo L4818), `module_handle()` L1740 (name→`W32_H_*`), `f_GetProcAddress` L4819.

## 4. Handler encontrado/criado
**Criado**: `f_GetModuleFileNameA` (inserido antes de `f_GetProcAddress`, `pr_win32.c`): valida `n<3`; `hModule` NULL ou igual à base da imagem (`guest_img_base`/`img_base`) = módulo principal (demais handles → 0 + `W32_ERROR_MOD_NOT_FOUND=126`); lê `ctx->module_name` (vazio → 0+126); `cap==0` → 0 sem escrever; escrita via `pr_win32_ptr(ctx, a[1], cap)` (buffer inválido → 0+87); **sempre NUL-termina**, trunca em `cap−1`, retorna chars sem NUL.

## 5. Registro no catálogo
Linha do TODO **substituída por** `IMPL("kernel32.dll", "GetModuleFileNameA", f_GetModuleFileNameA, 12)` (3 args × 4 bytes = 12, convenção `stdcall_bytes` das vizinhas: GetModuleHandleA=4, GetProcAddress=8). **1 entrada** no catálogo.

## 6. Infraestrutura de módulo/caminho encontrada (§2/§3)
A representação **real** do módulo principal já existia: `pr_peproc.c` **L693** = `pr_pe_load(p->file, p->file_len, &p->img, "app.exe")` → `p->img.module_name` = `"app.exe"` (comentário do campo: "nome do arquivo sugerido"). O setter `pr_win32_bind_image(ctx, base, size, module_name)` (L5165, `pr_win32.h` L77) já gravava `ctx->module_name`, mas estava **sem chamadores**; `pr_win32_bind_vm(ctx, vm, guest_image_base)` (L5239) tinha **1 chamador** (peproc L997) e não recebia nome. **Menor extensão localizada** (§3, sem infra paralela): `pr_win32_bind_vm` passou a receber `const char* module_name` (4º parâmetro) e preenche `ctx->module_name` (cópia limitada idêntica à de `pr_win32_bind_image`); chamador único passa `p->img.module_name`. `fs_root` **não** é usado pelo handler (o módulo principal não tem caminho no FS: é carregado de bytes; o nome real = o mantido pelo loader).

## 7. PE criado
`realpe/hello_getmodulefilename.c` → `Tests/PorticoRuntimeTests/data/hello_getmodulefilename.exe` (14848 B). Import direto por nome; auxiliares já validadas (`SetLastError`/`GetLastError`). Nenhum PE histórico alterado.

## 8. Comando de compilação
```
x86_64-w64-mingw32-gcc -O2 -s realpe/hello_getmodulefilename.c -o Tests/PorticoRuntimeTests/data/hello_getmodulefilename.exe
```

## 9. Imports
`KERNEL32.dll`: `GetModuleFileNameA` (hint **0297**, IAT `0x140008188`), `GetLastError` (0283), `SetLastError` (0554) + CRT (`msvcrt.dll`).

## 10. Execução inicial
**ANTES de alterar o runtime** (§16): `./build/dbg_diag …/hello_getmodulefilename.exe` → `EXECUTION STOPPED — Unsupported Win32 API — Module: kernel32.dll — Function: GetModuleFileNameA — Technical: caminho do módulo no FS do convidado` (steps=200000). Depois da implementação: `exited=1 rc=65 steps=1 exec=522 — ExitProcess(msvcrt!exit)`.

## 11. Primeiro blocker
`Unsupported Win32 API | kernel32.dll | GetModuleFileNameA` = **lacuna real do catálogo** (único blocker do grupo; resolvido pela implementação mínima §13).

## 12. ABI/disassembly (call site real)
```
mov    $0xdead,%ecx            ; SetLastError(0xDEAD) pré-condição
lea    0x140(%rsp),%rsi        ; rsi = &buffer
call   *%rbp                   ; IAT SetLastError (0x1400081a0)
mov    %rsi,%rdx               ; *** RDX = lpFilename = &buffer ***
xor    %ecx,%ecx               ; *** RCX = hModule = NULL ***
mov    0x5ab0(%rip),%rbx       ; rbx = IAT[GetModuleFileNameA] = 0x140008188
mov    $0x104,%r8d             ; *** R8D = nSize = 0x104 = 260 ***
call   *%rbx                   ; *** chamada real ***
mov    $0xb,%edx               ; código de falha 11 pré-carregado
lea    -0x1(%rax),%ecx         ; *** retorno consumido como DWORD ***
cmp    $0x102,%ecx             ; validação 0 < n < 260
ja     <fail 11>
mov    %eax,%edx               ; *** mov %eax = DWORD ***
cmpb   $0x0,0x140(%rsp,%rdx,1) ; buffer[n] == NUL
```

## 13. hModule
`RCX` recebido como 64-bit; NULL (`xor %ecx,%ecx`) = módulo do processo (§6). Handler aceita NULL ou a base da imagem (`guest_img_base` = o que `f_GetModuleHandleA(NULL)` devolve); **demais handles não resolvidos** (0+126) — limitação documentada (§31).

## 14. lpFilename
`RDX` = ponteiro guest; escrita via `pr_win32_ptr(ctx, a[1], cap)` (validação VM: faixa 0..4GiB − `PROC_SPACE`), bytes diretos — nunca ponteiro host como memória guest.

## 15. nSize
`R8D` = capacidade em bytes; `cap==0` → retorno 0 sem escrever; escrita respeita estritamente `nSize` (até `cap−1` + NUL em `[cap−1]`).

## 16. Retorno DWORD
`mov %eax,…` = DWORD (zero-ext em RAX; consumidor usa EAX): nº de chars escritos **sem** NUL (sucesso: `len`; truncamento: `cap−1`; `nSize=0`/erro: 0).

## 17. Conteúdo retornado
**(1) Portico observado (contrato objetivo do PE)**: `n==7`, bytes exatos = `"app.exe"` — **nome real do módulo principal mantido pelo loader** (`pr_pe_load(..., "app.exe")` → `img.module_name` → `ctx->module_name`); NUL em `buffer[n]`; comparação byte a byte (§9: sem comparação vaga). Prefixo/sufixo estrutural: sufixo `.exe` verificado pelos bytes. **(2) Windows documentado (só referência)**: "fully qualified path for the file that contains the specified module"; `hModule=NULL` → path do executável do processo (MSDN `libloaderapi!GetModuleFileNameA`). **(3) Diferenças conhecidas**: Portico retorna o **nome canônico** do módulo (`"app.exe"`), **não** caminho completo qualificado — o PE principal é carregado de bytes e não possui path no FS do convidado; não há semântica de curto/longo nem prefixo `\\?\`.

## 18. Buffer pequeno (truncamento) + nSize=0 + LastError — tabela 3 vias
PE: `unsigned char small[8]` pré-preenchido `0xEE` + canary `0xCC` após; chamada com `nSize=4`:
**(1) Portico observado**: `r==3`; `small[0..2]=="app"`; `small[3]==0` (**sempre NUL**); `small[4..7]==0xEE` (**nada além de nSize**); canary intacto; `GetLastError()==0xDEAD` **preservado**. `nSize=0` → retorno `0`, buffer intocado.
**(2) Windows documentado (referência)**: Vista+ = string truncada a nSize chars **incluindo NUL**, retorno **nSize**, LastError **ERROR_INSUFFICIENT_BUFFER (234)**; **Windows XP** = truncada a nSize **sem NUL**, retorno nSize, LastError permanece **ERROR_SUCCESS**; `nSize=0` = retorno 0 e "last error code is ERROR_SUCCESS" (MSDN).
**(3) Diferenças conhecidas**: Portico = terceira variante própria: sempre NUL em `cap−1`, retorno **`cap−1`** (≠ nSize), **LastError não é alterado** em truncamento (≠234 do Vista+); `nSize=0` preserva LastError (≠ leitura ERROR_SUCCESS do texto MSDN). Semântica de truncamento **não** declarada equivalente ao Windows.

## 19. Canary
`big.can[4]==0xCC` após `big.b[260]` intacto; `small` markers `0xEE` em `[4..7]` intactos + canary `small.can==0xCC` — sem qualquer escrita além do permitido (contrato 13 = corrupção).

## 20. LastError
`SetLastError(0xDEAD)` antes de cada chamada de escrita: sucesso → `GetLastError()==0xDEAD` (contrato 10); truncamento → preservado (§18); caminhos de erro do handler documentados: handle não-principal/nome vazio → `126 (ERROR_MOD_NOT_FOUND)`; buffer inválido → `87 (ERROR_INVALID_PARAMETER)` (não exercitados pelo PE de validação).

## 21. Múltiplas chamadas (identidade)
2 chamadas consecutivas idênticas (`nSize=260`): `n2==n`, bytes idênticos, NUL em `[n2]` (contrato 15).

## 22. Correções realizadas (escopo mínimo)
1. `pr_win32.c` — `f_GetModuleFileNameA` (novo, ~25 linhas).
2. `pr_win32.c` L4859 — TODO → `IMPL(..., 12)` (1 linha).
3. `pr_win32.c` L5239 — `pr_win32_bind_vm`: +4º parâmetro `const char* module_name` + cópia limitada p/ `ctx->module_name` (extensão mínima §3, reutilizando o campo já existente e o padrão de cópia de `pr_win32_bind_image`).
4. `pr_win32.h` L88 — declaração correspondente.
5. `pr_peproc.c` L997 — chamador único: +`p->img.module_name`.
Mais: fonte do PE. **Sem** mudanças colaterais; **sem** `MultiByteToWideChar`/`WideCharToMultiByte`/`GetModuleFileNameW`/`GetModuleHandleW`/`GetModuleHandleEx`/APIs de caminho (0 novos — `f_MultiByteToWideChar` L1545 é pré-existente, intocado).

## 23. MD5 (§20)
| Arquivo | antes | depois |
|---|---|---|
| `pr_win32.c` | `51b9e448750e2176f283c7baa5878dbb` (reverso exato das 5 edições) | `9aa33bb8895b2510b77519ef80946f58` |
| Combinado (todos .c/.h) | `a0901f348f7482a45288d060469ac386` (registrado em `build/g65_before.md5`) | `5da2fe2a5c80192adccc25166c8391f2` |

Mudanças = estritamente as 5 de §22 + novo `realpe/hello_getmodulefilename.c`. Motivos: §22.1 handler (API nova); §22.2 catálogo (registro); §22.3–5 fio loader→ctx (extensão mínima localizada §3).

## 24. 20 execuções
**20/20** `exited=1 rc=65 log=35` (determinístico): n/tamanho/string/canary/truncamento/LastError verificados em todas (o PE só emite 65 após todos os contratos); sem crash, sem `Unsupported`, sem variação.

## 25. Regressão C
**3398 verificações, 0 falhas** (baseline exato de G60; nenhum CHECK adicionado).

## 26. Regressão Swift
**Executed 63 tests, with 0 failures** ✓ (intocados).

## 27. Warnings
**0** (todos os `.c` com `-Wall -Wextra`).

## 28. Analyzer
**7/0** (pré-existentes: pr_cpu.c:126, pr_cpu64.c:401, pr_gfx.c:98, pr_peproc.c:831, pr_win32.c:3611/3615, pr_hello.c:885; **0 novos**).

## 29. PE battery
**15/15** — logs 28/43/180/70/72/75/98/118/76/81/194/131/55/200/105 (rc=42/5). Pipeline G1–G63 + nova coorte G51–G64 **14/14 verdes** (rc 42…64).

## 30. Checklist acumulada (36/36)
1. APIs documentadas/origem ✓ 2. código-fonte separado em Portico ✓ 3. scripts: toolchain apple → clang ✓ 4. limpeza retorno ✓ 5. build v0–v13 preservados ✓ 6. validação real iOS 18.5 ✓ 7. fat iOS IPA ✓ 8. surface bridge ✓ 9. shader metálico ✓ 10. z-buffer ✓ 11. pipeline x86_64 ✓ 12. pipeline i386 ✓ 13. boas-vindas ✓ 14. Win32 Boot — GetCommandLineA etc ✓ 15. desmontagem própria ✓ 16. shaders Metal preservados ✓ 17. sem GL1.1 completo ✓ 18. metal layers ✓ 19. sem bibliotecas mistas ✓ 20. dbg_diag/dbg_log/PE builder ✓ 21. bateria 15 PEs ✓ 22. PE stdout ✓ 23. win32 IsDebuggerPresent ✓ 24. win32 Render ✓ 25. win32 metadinhas ✓ 26. win32 LastError ✓ 27. win32 TLS ✓ 28. win32 WinSock ✓ 29. win32 DirectX ✓ 30. win32 Glaze ✓ 31. win32 Paths ✓ 32. PE patcher ✓ 33. win32 cmdline (G56/G57) ✓ 34. win32 strings (G58–G60) ✓ 35. win32 handle sync (G61–G64) ✓ **36. win32 module name (G65 GetModuleFileNameA) ✓**.
Checkpoints §22: G52=52/31, G53=53/38, G54=54/30, G55=55/36, G56=56/32, G57=57/33, G58=58/39, G59=59/40, G60=60/45, G61=61/33, G62=62/33, G63=63/33, G64=64/29 — **13/13 verdes** (+G65=65/35 = 14/14).

## 31. Limitações
1. Conteúdo = **nome canônico** do módulo (`"app.exe"`), não caminho completo do FS (§17.3) — o PE principal é carregado de bytes; não há equivalência de conteúdo com o Windows.
2. Truncamento/`nSize=0`/LastError = **comportamento Portico documentado** (§18.3), ≠ Windows Vista+/XP — não declarado equivalente.
3. `hModule` = NULL (ou base da imagem) apenas; handles de DLL (`W32_H_*`) não resolvidos (0+126) — menor subconjunto; sem `GetModuleHandleW`/`GetModuleHandleEx`.
4. `GetModuleFileNameW` e família Unicode **não implementadas** (escopo G65).
5. Nome limitado a 63 chars (`module_name[64]`).

## 32. Cobertura efetivamente demonstrada
Só `GetModuleFileNameA` por import real resolvido pelo loader + chamada real do PE com a ABI x64 documentada: módulo principal (NULL), buffer suficiente (conteúdo exato `"app.exe"` + NUL + canary + LastError), identidade entre chamadas, truncamento com `nSize=4` (retorno `3`, NUL, markers/canary, LastError), `nSize=0`. Nada além disso.

## 33. Próximo candidato (só indicação — §24: não executar)
`kernel32!GetModuleFileNameW` (família W; exige UTF-16 — proibido em G65) ou `kernel32!GetStartupInfoA` (bytes, 1 argumento). NÃO implementados aqui.
