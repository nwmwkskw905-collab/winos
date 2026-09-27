# RELATÓRIO GRUPO 77 — kernel32!GetCurrentDirectoryW

## 1. Status
**G77 CONCLUÍDO** — Variante Unicode de `GetCurrentDirectoryA`. PE `hello_getcurrentdirw.exe` rc=77 log=33, 20/20.

## 2. Inventário
- `f_GetCurrentDirectoryA` SIM L3095, `f_GetCurrentDirectoryW` NÃO.
- Catálogo: só A, sem W.
- W variants são P2 file, fundamentais para programas Unicode.
- Escolha: implementar W simples.

## 3. Lacuna
`GetCurrentDirectoryW` ausente → PEs Unicode que consultam cwd falhariam.

## 4. Implementação
`f_GetCurrentDirectoryW` L3111: mesma lógica de A, mas escreve UTF-16 (ASCII→WCHAR, cada byte + 0). `nBufferLength` em WCHARs, retorna chars sem NUL, need com NUL se buffer pequeno, `ERROR_INSUFFICIENT_BUFFER` 122. `pr_win32_ptr` com `cap*2`.

Catálogo: `IMPL kernel32 GetCurrentDirectoryW f_GetCurrentDirectoryW 8`.

## 5. Arquivos alterados
- `pr_win32.c`: +18 linhas + 1 IMPL.

## 6. PE criado
`hello_getcurrentdirw.c` → `hello_getcurrentdirw.exe` (14 KiB). Contratos: 10 A falhou, 11 W falhou, 12 conteúdo, 13 tamanho, 14 small need, 15 LastError, 16 NUL, 17 NULL need, 77 OK. Cwd default "\" (1 char) → need 2, test com cap 1 para forçar insufficient.

## 7. Imports
`KERNEL32`: `GetCurrentDirectoryA/W`, `SetLastError`, `GetLastError`.

## 8. ABI
RCX DWORD nBufferLength, RDX LPWSTR, retorno EAX DWORD.

## 9. Testes
- A e W retornam mesmo tamanho.
- Conteúdo W == A widened.
- NUL terminador.
- Buffer pequeno (1) retorna need>1 + 122.
- NULL buffer retorna need.

## 10. 20 execuções
20/20 rc=77 log=33.

## 11. C
3399/0.

## 12. Swift
63/63.

## 13. warnings
0.

## 14. analyzer
7/0.

## 15. PE battery
15/15.

## 16. checkpoints G52–G77
26/26.

## 17. checklist
48/48 — **48. GetCurrentDirectoryW (G77)**.

## 18. MD5
Antes ~4b2d5a..., depois evolui para final fe7acd0e... Final: `fe7acd0ed9ff5c07990abd4b39aaea3f`.

## 19. filesystem
Só fixture.txt.

## 20. Limitações
- Conversão ASCII→UTF-16 simples (só <128), sem CP1252.
- Cwd fallback "\".

## 21. Compatibilidade
GetCurrentDirectoryW por import real, ABI, need/NUL/LastError.

## 22. Próxima
GetModuleFileNameW (G78).

## 23. Motivo
File W variant mais simples, segue G77, P2.
