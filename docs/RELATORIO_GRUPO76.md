# RELATÓRIO GRUPO 76 — user32!GetSystemMetrics

## 1. Status
**G76 CONCLUÍDO** — `GetSystemMetrics` implementada com métricas plausíveis baseadas em superfície interna (320x240 nos testes) + valores típicos Windows. PE `hello_sysmetrics.exe` rc=76 log=48, 20/20. C 3399/0 (após ajuste de teste), Swift 63/63, battery 15/15.

## 2. Inventário
- `f_GetSystemMetrics`? **NÃO** antes.
- Catálogo: `TODO user32 GetSystemMetrics` L5187, sem IMPL.
- Testes: `test_win32.c` L190 checava `PR_ERR_UNSUPPORTED` e `implemented==32` + Swift `CompatLayerTests` L393 `32` e `XCTAssertFalse GetSystemMetrics`.
- PEs: nenhum importava.
- Conclusão: TODO real, simples, P2 (window).

## 3. Lacuna
`GetSystemMetrics` ausente → qualquer PE gráfico que consulta tamanho de tela falharia.

## 4. Objetivo
Implementar mínimo com valores determinísticos, sem UIKit ainda, mas preparado para futuro.

## 5. Implementação
`f_GetSystemMetrics` L~4250, 80+ linhas, switch com 60+ índices:
- 0 CXSCREEN, 1 CYSCREEN, 16 CXFULLSCREEN, 17 CYFULLSCREEN, 78 CXVIRTUALSCREEN, 79 CYVIRTUALSCREEN = `w/h` da `gdi_surface` (ou 320x240 fallback).
- 2/3/20/21 XVSCROLL/YHSCROLL =16
- 4 CYCAPTION=19, 5/6 BORDER=1, 7/8 DLGFRAME=3, 9/10 VTHUMB/HTHUMB=16, 11-14 ICON/CURSOR=32, 15 CYMENU=19, 19 MOUSEPRESENT=1, etc.
- Default 0.
- Não altera LastError (contrato Windows).
- Catálogo: `IMPL user32 GetSystemMetrics f_GetSystemMetrics 4` L~5188, antes do TODO.

## 6. Arquivos alterados
- `pr_win32.c`: +~80 linhas + 1 IMPL.
- `Tests/PorticoRuntimeTests/test_win32.c`: atualizado para `PR_OK` + `implemented==33`.
- `Tests/PorticoCoreTests/CompatLayerTests.swift`: `33` + `XCTAssertTrue GetSystemMetrics`.

## 7. PE criado
`realpe/hello_sysmetrics.c` → `hello_sysmetrics.exe` (15 KiB). Contratos: 10 CXSCREEN, 11 CYSCREEN, 12 FULLSCREEN, 13 XVSCROLL, 14 MOUSEPRESENT, 15 ICON/CURSOR, 16 CAPTION/MENU, 17 índice inválido, 18 LastError, 76 OK.

## 8. Imports
`USER32.dll`: `GetSystemMetrics` (hint 01b7), `KERNEL32`: `SetLastError`, `GetLastError`, `SetUnhandledExceptionFilter`, `TlsGetValue`, `VirtualProtect`, `VirtualQuery`.

## 9. ABI
`RCX=int nIndex`, retorno `EAX=int`. `mov $0,%ecx` etc, `call *IAT`.

## 10. Testes
- CXSCREEN/CYSCREEN >0 <=4096.
- FULLSCREEN >= CX/CY.
- XVSCROLL etc >0.
- MOUSEPRESENT==1.
- ICON/CURSOR==32.
- CAPTION>0.
- Índice 999==0.
- LastError preservado.

## 11. 20 execuções
20/20 rc=76 log=48.

## 12. C
3399/0 (era 3398, +1 check novo).

## 13. Swift
63/63 (após ajuste).

## 14. warnings
0.

## 15. analyzer
7/0 novos.

## 16. PE battery
15/15 28/43/180/70/72/75/98/118/76/81/194/131/55/200/105.

## 17. checkpoints G52–G76
25/25 (…75 75/45, 76 76/48).

## 18. checklist
47/47 — novo **47. GetSystemMetrics (G76)**.

## 19. MD5
Antes G75: `4b2d5a629aea3a971d1330ec64cf8e05`, após G76 intermediário (evolui para final `fe7acd0e...`). Final após G80: `fe7acd0ed9ff5c07990abd4b39aaea3f`.

## 20. filesystem
Só `fixture.txt`.

## 21. Limitações
- Valores fixos, não consultam UIKit/UIScreen (TODO futuro).
- Métricas de múltiplos monitores = 1 monitor.
- SM_CMONITORS=1, VIRTUALSCREEN = CX/YSCREEN.

## 22. Compatibilidade
GetSystemMetrics por import real, ABI Win64, métricas plausíveis, LastError preservado.

## 23. Próxima lacuna
`GetCurrentDirectoryW` (file W variant, simples) — G77.

## 24. Motivo
user32 TODO mais simples, P2 window, sem dependências, fecha TODOs user32.
