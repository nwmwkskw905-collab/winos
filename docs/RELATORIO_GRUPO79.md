# RELATÓRIO GRUPO 79 — kernel32!GetFileAttributesW

## 1. Status
**G79 CONCLUÍDO** — Variante Unicode de `GetFileAttributesA`. PE rc=79 log=38, 20/20.

## 2. Inventário
- A existe L~2730, W NÃO.
- Catálogo só A.
- File W variant P2.

## 3. Implementação
`f_GetFileAttributesW` L2760: `wstr_to_ascii` → win path → `vfs_resolve` → `stat` → 0x10 dir / 0x80 file / 0xFFFFFFFF + LastError. Forward decl `wstr_to_ascii` adicionada L498 para evitar implicit.

Catálogo: `IMPL GetFileAttributesW f_GetFileAttributesW 4`.

## 4. Arquivos
- `pr_win32.c`: +22 linhas +1 IMPL + forward decl.

## 5. PE
`hello_getfileattrw.c` → `hello_getfileattrw.exe` (14 KiB). Contratos: 10 fixture, 11 A falhou, 12 W falhou, 13 attr diff, 14 inexistente não INVALID, 15 LastError, 16 limpeza, 79 OK. Fixture `attr_g79.txt`.

## 6. Imports
`KERNEL32`: `CreateFileA`, `WriteFile`, `CloseHandle`, `GetFileAttributesA/W`, `DeleteFileA`, `SetLastError`, `GetLastError`.

## 7. ABI
RCX LPCWSTR, retorno EAX DWORD attrs.

## 8. Testes
- A e W mesmo attr (0x80).
- Inexistente → INVALID_FILE_ATTRIBUTES + LastError 2/3.
- Limpeza OK.

## 9. 20 execuções
20/20 rc=79 log=38, win_fs só fixture.txt (PE deleta).

## 10. C/Swift/battery
3399/0, 63/63, 15/15.

## 11. checkpoints
28/28.

## 12. checklist
50/50 — **50. GetFileAttributesW (G79)**.

## 13. MD5
Final `fe7acd0ed9ff5c07990abd4b39aaea3f`.

## 14. Limitações
- Conversão W→ASCII via `wstr_to_ascii` (só <128, '?' se >127).
- Atributos mínimos (dir 0x10, file 0x80).

## 15. Compatibilidade
GetFileAttributesW por import real, VFS seguro, LastError.

## 16. Próxima
DeleteFileW (G80).

## 17. Motivo
File W variant, segue GetFileAttributesA, P2.
