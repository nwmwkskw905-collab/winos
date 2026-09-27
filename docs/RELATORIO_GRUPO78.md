# RELATÓRIO GRUPO 78 — kernel32!GetModuleFileNameW

## 1. Status
**G78 CONCLUÍDO** — Variante Unicode de `GetModuleFileNameA`. PE rc=78 log=37, 20/20.

## 2. Inventário
- A existe L2666, W NÃO.
- Catálogo só A.
- PEs: `hello_getmodulefilename.exe` usa A; W é natural para Unicode.

## 3. Implementação
`f_GetModuleFileNameW` L~2685: mesma validação de hModule (NULL ou guest_img_base), `module_name` → UTF-16 (ASCII widened), trunca em cap-1, NUL-termina, retorna chars sem NUL. `pr_win32_ptr(cap*2)`.

Catálogo: `IMPL GetModuleFileNameW f_GetModuleFileNameW 12`.

## 4. Arquivos
- `pr_win32.c`: +25 linhas +1 IMPL.

## 5. PE
`hello_getmodfilenamew.c` → `hello_getmodfilenamew.exe` (14 KiB). Contratos: 10 A falhou, 11 W falhou, 12 tamanhos, 13 conteúdo, 14 truncamento, 15 NUL, 16 handle inválido, 78 OK.

## 6. Imports
`KERNEL32`: `GetModuleFileNameA/W`, `SetLastError`, `GetLastError`.

## 7. ABI
RCX HMODULE, RDX LPWSTR, R8 DWORD nSize, retorno EAX.

## 8. Testes
- A e W mesmo tamanho.
- Conteúdo widened.
- Truncamento cap 4 → ret 3.
- Handle inválido 0x1234 → 0 + 126.

## 9. 20 execuções
20/20 rc=78 log=37.

## 10. C/Swift/warnings/analyzer/battery
3399/0, 63/63, 0, 7/0, 15/15.

## 11. checkpoints
27/27 (…77 77/33, 78 78/37).

## 12. checklist
49/49 — **49. GetModuleFileNameW (G78)**.

## 13. MD5
Final `fe7acd0ed9ff5c07990abd4b39aaea3f`.

## 14. filesystem
Só fixture.txt.

## 15. Limitações
- Só módulo principal (hModule NULL ou base), outros handles = MOD_NOT_FOUND.
- Conversão ASCII→UTF-16 simples.

## 16. Compatibilidade
GetModuleFileNameW por import real, truncamento/NUL/LastError.

## 17. Próxima
GetFileAttributesW (G79).

## 18. Motivo
Module W variant, P2, simples.
