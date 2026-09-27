# Relatório — Windows Compatibility: PE32+ / Relocations / Ordinal

Data: 2026-09-22 · Portico · incremento incremental de compatibilidade

## Build final

| Métrica | Valor |
|---|---|
| Checks C (12 suítes) | **545** |
| Testes Swift (5 suítes) | **42** |
| Falhas | **0** |
| Warnings (gcc -Wall -Wextra -Wshadow) | **0** |

## Status dos componentes

| Item | Status | Detalhe |
|---|---|---|
| PE32+ (loader) | **WORKING** | validação, headers, sections, entry, image base 64-bit, imports 8B, relocs DIR64, alignment, proteções |
| PE32+ (execução x64) | **NOT AVAILABLE** | sem backend de CPU x64; recusa com `Architecture: x86-64` (sem simulação) |
| Relocations | **WORKING** | HIGHLOW (PE32) + DIR64 (PE32+); validação/limites/diagnóstico; e2e em base relocalizada |
| Imports (nome) | **WORKING** | PE → Import Table → DLL Resolver → Win32 Dispatcher → Implementação |
| Imports (ordinal) | **WORKING (parcial)** | ordinais públicos (winsock.def); desconhecido → EXECUTION STOPPED honesto |
| CPU | **PARTIAL** | interpretador IA-32 (subconjunto) + guard R/W/X; FPU/SSE/x64 conforme a demanda dos PEs (ainda não exigidos) |
| Win32 | **PARTIAL** | 38 APIs reais (kernel32 28 + gdi32 6 + ws2_32 4); demais → UNSUPPORTED + log |
| GDI32 | **PARTIAL** | DC + pincel + PatBlt; bitmap/BitBlt = próximo incremento |
| Metal | **SUPPORTED (parcial)** | pipeline + self-test intactos; desenhar `GfxFrame.surface` = pendência app-side |
| GDI → Metal | **PARTIAL** | superfície → `GfxFrame.surface` testado; textura→tela no renderer iOS = próximo passo |
| Diagnóstico | **WORKING** | EXECUTION STOPPED / Reason / Module / Function / **Address** / **Architecture** |
| PE visual de teste (bitmap+BitBlt→Metal) | **NÃO** | próximo marco (itens 7–9 do plano) |

## Provas executadas (novas)

1. **Relocation E2E**: PE32 com base preferida 0x02000000 (fora do espaço) →
   relocado; slot de dados e o `disp` do `call [abs]` ajustados por HIGHLOW;
   **o programa roda pelo caminho realocalizado e retorna exit 42**.
2. **DIR64**: PE32+ com base 0x140000000 → relocado; slot u64 ajustado.
3. **Sem tabela + base ocupada** → recusa: "Unsupported executable (relocations)".
4. **Ordinal E2E**: `ws2_32!#9` (htons, ordinal público do winsock.def) +
   `ExitProcess` → **exit code 0x0102** (= htons(0x0201)).
5. **Ordinal desconhecido #999** → EXECUTION STOPPED / "por ordinal" / "#999".
6. **PE32+ com imports** (thunks 8 bytes) carrega e resolve; `step` →
   recusa `Architecture: x86-64` com o formato completo de diagnóstico.
7. **Alignment inválido** → `PR_ERR_FORMAT` na carga.

## Limitações restantes

- Execução x64 (interpretador/FPU/SSE conforme exigido pelos PEs reais);
- Ordinais além do winsock.def (kernel32/user32/gdi32 não têm ordinais
  públicos estáveis — sem correspondência → recusa honesta);
- Win32: MessageBoxA, bitmap, BitBlt, janelas/mensagens (itens 6–8 do plano);
- `MetalGameRenderer` desenhar `GfxFrame.surface` (item 8/12).

Nenhum jogo Windows comercial é declarado como funcionando; o próximo marco é
o PE visual de teste (bitmap + BitBlt → superfície → Metal → tela → exit).
