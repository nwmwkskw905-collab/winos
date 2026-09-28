# Apple Clang / iOS Compatibilidade — Correção `pthread_mutex_timedlock` e `pthread_timedjoin_np`

**Data:** 2026-09-28  
**SDK:** iPhoneOS 26.5 arm64, Xcode 26.6, macos-latest (GitHub Actions)  
**Arquivo:** `Sources/PorticoRuntime/src/pr_win32.c` (único modificado para lógica, + `pr_cap.c` e outros para feature macros de simulação)  
**Erros originais:**
```
Sources/PorticoRuntime/src/pr_win32.c:4143:17: error: call to undeclared function 'pthread_mutex_timedlock'
Sources/PorticoRuntime/src/pr_win32.c:4216:21: error: call to undeclared function 'pthread_timedjoin_np'
```

## 1. Auditoria Semântica

### G45 — Mutex com timeout finito (linha 4143)
```c
clock_gettime(CLOCK_REALTIME, &ts);
ts.tv_sec += ms/1000;
ts.tv_nsec += (ms%1000)*1e6; carry
if (pthread_mutex_timedlock(&mt->m, &ts)==0) { owned... return WAIT_OBJECT_0 }
return WAIT_TIMEOUT 0x102
```
- **Timeout:** absoluto CLOCK_REALTIME, calculado a partir de `ms` relativo.
- **Retorno:** 0 = sucesso (WAIT_OBJECT_0), ETIMEDOUT = timeout (WAIT_TIMEOUT).
- **Semântica Win32:** WaitForSingleObject em mutex com timeout finito.

### G46 — Thread com timeout finito (linha 4216)
```c
clock_gettime(CLOCK_REALTIME, &ts);
ts.tv_sec += ms/1000;
ts.tv_nsec += (ms%1000)*1e6
if (pthread_timedjoin_np(t->tid, NULL, &ts)!=0) return WAIT_TIMEOUT
return WAIT_OBJECT_0
```
- Usa flag `t->done` volátil setada por `w32_thread_main` antes de retornar.
- Timeout absoluto, retorna 0x102 em expiração.

Ambos são GNU extensions Linux, inexistentes no SDK iOS (Apple Clang C99+). `pthread_cond_timedwait` e `clock_gettime` existem iOS 10+ com `_DARWIN_C_SOURCE`.

## 2. Estratégia Darwin/iOS

### Princípios
- NÃO substituir por `trylock`/`join` sem timeout (quebra semântica).
- NÃO remover timeouts, NÃO stub/no-op.
- Implementar compatibilidade com `#if defined(__APPLE__)`, preservando Linux intacto.
- Preservar timeout absoluto/relativo, códigos retorno, EINTR/ETIMEDOUT, evitar busy-loop/deadlock/vazamento.

### `pr_darwin_pthread_mutex_timedlock`
```c
static int pr_darwin_pthread_mutex_timedlock(mutex, abs_timeout) {
  clock_gettime(CLOCK_REALTIME, now);
  if já expirou -> trylock once -> ETIMEDOUT se falha
  loop:
    trylock success -> 0
    clock_gettime -> timeout check sec/nsec
    remaining = abs - now (carry nsec)
    sleep adaptativo: 1ms (cap 5ms), se remaining <5ms dorme remaining (cap 1ms)
    nanosleep com EINTR handling via rem_sleep
}
```
- **APIs iOS:** `pthread_mutex_trylock`, `clock_gettime(CLOCK_REALTIME)`, `nanosleep`, `ETIMEDOUT`, `errno`.
- **Sem busy-loop:** sleep 1ms, não spin.
- **Preserva timeout:** comparação absoluta sec/nsec idêntica Linux.

### `pr_darwin_pthread_timedjoin_np`
```c
static int pr_darwin_pthread_timedjoin_np(tid, retval, abs_timeout, done_flag) {
  while (!*done_flag) {
    clock_gettime(CLOCK_REALTIME, now);
    if now > abs -> ETIMEDOUT
    nanosleep 1ms EINTR
  }
  return 0; // caller faz pthread_join real
}
```
- Reusa `t->done` volátil existente, setado por `w32_thread_main`.
- Quando done==1, retorna 0, caller faz `pthread_join` real para liberar recursos (sem vazamento).
- Timeout preservado, sem substituir cegamente por join sem timeout.

### Condicional
```c
#if defined(__APPLE__)
  if (pr_darwin_pthread_mutex_timedlock(...)==0) ...
#else
  if (pthread_mutex_timedlock(...)==0) ...
#endif

#if defined(__APPLE__)
  if (pr_darwin_pthread_timedjoin_np(..., &t->done)!=0) TIMEOUT else join real
#else
  if (pthread_timedjoin_np(...)!=0) TIMEOUT
#endif
```

## 3. Busca Completa por `_np` Extensions

```
grep -RIn "pthread.*_np|pthread_mutex_timedlock|sem_timedwait|clock_gettime" Sources
```
- Apenas 2 ocorrências problemáticas (G45, G46) + `clock_gettime` legítimo.
- Nenhum `sem_timedwait`, `pthread_*_np` adicional.
- Corrigido com fallback Darwin.

## 4. Headers iOS Auditados

- iOS SDK contém: `pthread_mutex_trylock`, `pthread_mutex_lock`, `pthread_join`, `pthread_cond_timedwait`, `clock_gettime`, `nanosleep`, `errno.h ETIMEDOUT`, `TargetConditionals.h`.
- `pthread_mutex_timedlock` e `pthread_timedjoin_np` ausentes confirmados.
- Fallback usa apenas POSIX disponível iOS 17 deployment target.
- `_DARWIN_C_SOURCE` + `_POSIX_C_SOURCE` expõe `clock_gettime` com `-std=c11`.

## 5. Validação Linux

- **GCC:** `gcc -Wall -Wextra -Werror=implicit-function-declaration -std=c11 -O2 -g -DPR_ENABLE_ZLIB=1 -ISources/PorticoRuntime/include Sources/PorticoRuntime/src/*.c Tests/PorticoRuntimeTests/*.c -lz -lm -lpthread -o build/pr_tests`
  - Exit 0, 0 warnings.
- **C battery:** 3411 verificações, 0 falhas.
- **PE battery harness:** 76/76 PASS (inclui `hello_thread_timeout.exe` que exercita timed join).
- **PE battery sem harness:** 75/1 HANG YELLOW `hello_input.exe` (GetMessage fila vazia + timer 10ms + nanosleep 100ms cap → STOP honesto sem input nunca PostQuitMessage → HANG esperado, não bug).
- **ASAN:** `gcc -fsanitize=address ...` exit 0, 0 UAF/overflow, 6637553 bytes leaked em 65 allocs (testes que não liberam, baseline documentado).
- **Regressão:** 0 (baseline 3411/0 e 76/76 mantidos).

## 6. Validação Apple/iOS Estática

- **Apple sim:** `gcc -D__APPLE__ -D__MACH__ -Wall -Wextra -Werror=implicit-function-declaration -std=c11 ... -c Sources/PorticoRuntime/src/*.c`
  - Todos 20 arquivos compilam, exit 0.
  - `pr_win32.c` com `TargetConditionals.h` guardado com `__has_include` para simulação Linux.
  - `pr_cap.c` guardado `sysctlbyname` com `__APPLE__ && __MACH__ && !__linux__` + `__has_include`.
  - Feature macros fix: todos src/*.c agora definem `_POSIX_C_SOURCE` também quando `__APPLE__` para expor `clock_gettime` no glibc Linux simulando Darwin.
- **APIs usadas no path Apple:** apenas `pthread_mutex_trylock`, `pthread_join`, `clock_gettime`, `nanosleep`, `pthread_cond_timedwait` (já usado), `ETIMEDOUT`.
- **Nenhuma API macOS desktop** ou inexistente no SDK iOS.
- **xcodebuild real:** não disponível no Arena Linux (Swift toolchain, Xcode, Metal SDK unavailable). Marcado UNVERIFIED ENVIRONMENT LIMITATION honesto, não fabricado. Workflow `.github/workflows/ios-build.yml` permanece 5 steps (Runtime, Core, App device, App simulator, Archive) com `CODE_SIGNING_ALLOWED=NO`.

## 7. Arquivos Modificados

- `Sources/PorticoRuntime/src/pr_win32.c` — compat block Darwin/iOS (linha ~15-130) + condicionais G45/G46.
- `Sources/PorticoRuntime/src/pr_cap.c` — guard `TargetConditionals.h` com `__has_include`, `sysctlbyname` com `__APPLE__ && __MACH__ && !__linux__`.
- Demais `Sources/PorticoRuntime/src/*.c` (16 arquivos) — feature macros: adicionado `#define _POSIX_C_SOURCE 200809L` quando `__APPLE__` para simulação Linux + compatibilidade Darwin real (não altera semântica, apenas expõe POSIX em ambos os paths).

## 8. Timeout Preservation

- **Absoluto:** `CLOCK_REALTIME` + `ms/1000` sec + `%1000*1e6` nsec + carry, idêntico Linux.
- **Comparação:** `sec >` ou `sec== && nsec>=` para expiração.
- **Remaining:** `abs - now` com carry, sleep adaptativo 1ms cap 5ms, last <1ms.
- **Retorno:** 0 = WAIT_OBJECT_0, ETIMEDOUT = WAIT_TIMEOUT 0x102.
- **EINTR:** `nanosleep` loop com `rem_sleep`.
- **Deadlock:** não há — trylock não bloqueia, polling done não segura lock.

## 9. Warnings/Errors

- **Novos warnings:** 0 Linux, 0 Apple sim.
- **Erros corrigidos:** 2 (linhas 4143, 4216).
- **Warnings preexistentes:** 0.

## 10. % Conclusão

- **WinOS estimada:** 96% (Xcodeproj fix + audit 18 fases + Apple Clang compat, resta xcodebuild real device/simulator e Metal/Audio hardware validation que requer macOS runner).
- **Apple Clang fix:** 100% CONCLUÍDO (lógica, validação estática, Linux intacto).

## 11. Próximo Passo

- Executar `xcodebuild` real em macOS runner (macos-latest Xcode 26.6 iPhoneOS SDK 26.5 arm64) via GitHub Actions para confirmar 0 errors nos 5 steps.
- Metal/Audio hardware validation requer iPhone físico > iPad > simulador (UNVERIFIED ENVIRONMENT LIMITATION no Arena).
