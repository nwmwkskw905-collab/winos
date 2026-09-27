# G85 — Registry Virtual (Subfase C) — Relatório

## Resumo
Implementação completa do Registry virtualizado (advapi32.dll) com armazenamento em filesystem sandbox `fs_root/registry/<HKEY>/<path>`. Handles próprios `PR_WIN32_H_REG_BASE 0xF0F0F500` + 16 slots, resolução de HKEYs predefinidas com sign-extension (0xFFFFFFFF8000000x), normalização de subkeys `\`→`/` e bloqueio de traversal `..`.

## APIs Implementadas — GREEN

| API | Assinatura (x64) | Retorno | Evidência |
|-----|------------------|---------|-----------|
| RegCreateKeyExA | 9 args RCX HKEY, RDX LPCSTR, R8 DWORD, R9 LPSTR, stack: Options, sam, sec, PHKEY, LPDWORD | LONG 0=OK, 87=INVAL, 5=ACCESS, 8=NOMEM | PE hello_registry rc=85 cria `Software\WinOS_Test`, subkey `SubKey` |
| RegCreateKeyExW | idem W | 0 | PE hello_registry_w rc=86 cria `Software\WinOS_TestW` |
| RegOpenKeyExA | 5 args HKEY, LPCSTR, DWORD, REGSAM, PHKEY | 0/2=NOTFOUND/87 | Open após create, falha se inexistente |
| RegOpenKeyExW | idem W | 0/2 | testado |
| RegCloseKey | HKEY | 0/6=INVALID_HANDLE | Fecha handle próprio; predefined retorna 0; inválido 0x1234 → 6 |
| RegSetValueExA | HKEY, LPCSTR name, res, DWORD type, BYTE* data, DWORD cb | 0/87/5 | Set DWORD 0x12345678 + SZ "hello registry" |
| RegSetValueExW | idem W | 0 | Set DWORD + W SZ "wide hello" UTF-16 raw |
| RegQueryValueExA | HKEY, name, res, LPDWORD type, LPBYTE data, LPDWORD cb | 0/2/234=MORE_DATA | Query com type check, buffer null → size, buffer pequeno → 234 |
| RegQueryValueExW | idem W | 0/234 | Cross-encoding: ANSI→W converte, W→W copia raw; heurística is_utf16 = tmp[1]==0 |
| RegDeleteValueA/W | HKEY, LPCSTR | 0/2 | Delete + query falha 2 |
| RegDeleteKeyA/W | HKEY, LPCSTR subkey | 0/2/5=NOTEMPTY | Falha se não vazio (cnt>0 → 5), sucesso após limpar |
| RegEnumKeyExA | HKEY, DWORD idx, LPSTR name, LPDWORD len... | 0/234/259=NO_MORE | Enumera subdirs, ignora files |
| RegEnumKeyExW | idem W | 0/259 | UTF-16 output |
| RegEnumValueA | HKEY, idx, LPSTR name, len, res, LPDWORD type, LPBYTE data, LPDWORD cb | 0/234/259 | Enumera files, ignora .type e dirs, __default→"" |
| RegEnumValueW | idem W | 0/259 | UTF-16 name |

## Armazenamento
- `fs_root/registry/HKEY_CURRENT_USER/Software/WinOS_Test/` diretório
- Valor `TestDWORD` → arquivo `TestDWORD` binário 4 bytes + `TestDWORD.type` contendo DWORD type (1=SZ,4=DWORD)
- `__default` para valor padrão (nome NULL)
- `w32_mkdir_p` recursivo cria hierarquia; `w32_reg_normalize_subkey` rejeita `..`
- `reg_value_path` rejeita `/\..` no nome do valor

## Detalhes de Implementação
- **Sign-extension fix**: HKEYs predefinidas vêm como `0xFFFFFFFF8000000x` (LONG_PTR sign-extended). `w32_is_predefined_hkey` e `w32_predefined_to_str` agora usam `(uint32_t)h`.
- **Handles**: `reg_keys[16]` em `pr_win32_ctx`, `w32_reg_slot`, `w32_regs_shutdown` chamado em `pr_win32_destroy`.
- **Cross-encoding**: 
  - A query: se arquivo contém UTF-16 (tmp[1]==0) → converte para ANSI (pega bytes pares)
  - W query: se ANSI → converte para UTF-16, se UTF-16 → copia raw
- **Segurança**: traversal bloqueado tanto em subkey quanto em value name; `vfs_resolve` não usado para registry, mas `fs_root` obrigatório.
- **LastError**: set em falhas (INVAL 87, FILE_NOT_FOUND 2 via retorno, ACCESS 5, etc.)

## Testes
- `realpe/hello_registry.c` 350 linhas: Create, Set DWORD/SZ, Query, EnumValue (2), Create subkey, EnumKey, DeleteValue, DeleteKey, cleanup, invalid handle, traversal `..\outside` → falha, rc=85
- `realpe/hello_registry_w.c` 70 linhas: CreateW, SetW DWORD/SZ, QueryW, EnumValueW, EnumKeyExW, DeleteW, rc=86
- Build `dbg_input` O2 -Wall -Wextra warnings 0
- 20/20 execuções rc=85 log=52 e rc=86 log=42
- Bateria total 71 PEs (70 anteriores + registry_w) verde, sem regressão: todos rc esperados preservados (ex: hello_fileex 84, findfilea 83, etc.)
- Filesystem final limpo: `build/win_fs/fixture.txt` apenas após `rm -rf registry` e `winos_*.txt`

## Limitações Explícitas
- Não implementado: RegEnumKeyExW com filtro classe, RegCreateKey com SECURITY_ATTRIBUTES, SAM, Options, disposition detalhada (sempre 1), RegQueryValueEx com res, RegEnumValue com data parcial para SZ cross (simplificado), HKLM write sem privilégio (sempre permitido no sandbox), registry em memória volátil (filesystem), não há notificação/regnotify.
- `HKEY_PERFORMANCE_DATA` (0x80000004) não mapeado (não usado nos PEs).

## Próximos Candidatos Subfase B restantes
- GetFullPathNameA/W, MoveFileA/W, CopyFileA/W, SetFilePointerEx, FlushFileBuffers, HeapReAlloc, CreateMutexW, GetTempPathA/W, etc.

## Evidência de Build
```
gcc -std=c11 -Wall -Wextra -O2 -DPR_ENABLE_ZLIB=1 -ISources/PorticoRuntime/include Sources/PorticoRuntime/src/*.c tools/dbg_input.c -o build/dbg_input -lz -lm -lpthread
=> warnings 0
./build/dbg_input hello_registry.exe noinput => exited=1 rc=85 log=52 (20x)
./build/dbg_input hello_registry_w.exe noinput => exited=1 rc=86 log=42
```

## Arquivos
- `Sources/PorticoRuntime/src/pr_win32.c` — 16 novas funções Reg* + helpers + IMPLs
- `realpe/hello_registry.c` e `realpe/hello_registry_w.c`
- `Tests/PorticoRuntimeTests/data/hello_registry.exe` e `hello_registry_w.exe`
