# Integração do Backend Windows — STATUS: INTEGRADO (Fase 7)

> **2026-09-22**: o ponto de encaixe descrito abaixo foi PREENCHIDO. A execução
> real de PE32 funciona via `pr_peproc` (memória virtual + CPU IA-32 + Win32 +
> processo/exit). Este documento fica como referência do contrato de integração.

# Como plugar uma camada de compatibilidade Windows real

O Portico **não executa software Windows hoje** — e não finge que executa.
Este documento define exatamente onde e como encaixar um componente real.

## 1. A ABI: `pr_host_backend_v1` (C)

Definida em `Sources/PorticoRuntime/include/portico/pr_host.h`:

```c
typedef struct pr_host_backend_v1 {
    uint32_t    abi_version;   /* PR_HOST_ABI_VERSION = 1 */
    const char* name;          /* ex.: "wine-user" */
    const char* version;
    void*       user;
    int      (*can_execute)(void* user, const pr_pe_info* pe,
                            char* reason, size_t reason_len);
    pr_status (*start)(pr_host* h, void* user,
                       const pr_host_start_info* info, pr_log* log);
    pr_status (*frame)(pr_host* h, void* user,
                       const pr_host_frame_in* in, pr_host_frame_out* out);
    void     (*stop)(pr_host* h, void* user);
    void     (*shutdown)(void* user);
} pr_host_backend_v1;
```

Um backend:

1. em `can_execute`, aceita PEs (usa `pr_pe_info` para filtrar arquitetura);
2. em `start`, carrega o PE (use `pr_pe_scan`/`pr_pe_sections`/`pr_pe_rva_to_offset`
   para o mapeamento seções→arquivo), prepara o prefixo (via EnvironmentManager —
   o diretório `drive_c/` já é criado) e aplica as variáveis recebidas;
3. em `frame`, avança o guest e emite:
   - **gráficos**: comandos em `pr_host_gfx(h)` (`pr_gfx_push*`) — consumidos pelo Metal;
   - **áudio**: PCM estéreo float em `pr_host_audio(h)` (`pr_audio_push`);
   - **logs**: `pr_log_write` (drenados para a UI);
4. em `stop`, libera tudo; sinaliza término normal por `out->halted = 1`.

Registre com `pr_host_register_backend(&seu_backend)` — o `RuntimeManager`
passa a considerá-lo na seleção automática. Enquanto nenhum backend aceitar o
PE, o app reporta `NOT SUPPORTED` com o motivo agregado (comportamento atual).

## 2. O que um backend Windows precisa fornecer

| Bloco | Necessário | Estado no Portico |
|---|---|---|
| Loader PE (seções→memória, imports, relocations) | Sim | **Análise/carga prontos** (`pr_pe_load`, imports/exports por função, diagnóstico); relocations/IAT-binding pendentes |
| CPU x86-32/x86-64 | Sim | Interpretador IA-32 (subconjunto) como base; x86-64 e FPU pendentes |
| APIs Win32/NT (kernel32, ntdll, user32...) | Sim | **Dispatch real** (`pr_win32`): 28 APIs kernel32 implementadas; user32/gdi32/ole32/shell32/advapi32/ws2_32 catalogadas (UNSUPPORTED + log) |
| Tradução gráfica (D3D/GL → Metal) | Sim | Stream comum + superfície por pixels (`pr_surf`) + fence; frontends completos pendentes |
| Áudio (waveOut/DirectSound → AVAudioEngine) | Sim | Ring PCM pronto no host |
| Entrada (Win32 input → InputState) | Sim | `pr_input_state` pronto no host |

### Custos e avisos de licença na integração futura

Componentes open source candidatos **avaliados** (licenças conforme repositórios
oficiais; confirmar na integração):

| Componente | Licença | Papel possível | Notas |
|---|---|---|---|
| Wine | LGPL-2.1-or-later | APIs Win32/NT | LGPL exige disponibilidade do código-fonte das modificações e relink; isolar em módulo próprio |
| Box86 / Box64 | MIT | Tradução x86→ARM | Usa dynarec (JIT) por padrão — **inviável no iOS sem JIT**; só seria útil com backend de interpretação |
| FEX-Emu | MIT | Emulação x86/x64 | Também assume Linux/JIT; mesma restrição |
| Unicorn Engine | BSD-2-Clause | Emulação CPU (interpretação) | Viável em iOS por ser interpretação; desempenho limitado |
| QEMU (user-mode) | BSD-2-Clause (partes GPL) | Emulação | Pesado; partes GPL impõem obrigações; JIT precisa ser desativado |
| DXVK | Zlib | D3D9/10/11 → Vulkan | Depende de Vulkan |
| MoltenVK | Apache-2.0 | Vulkan → Metal | Fecharia DXVK→Metal; adiciona peso e requisitos |
| VKD3D-Proton | LGPL-2.1-or-later | D3D12 → Vulkan | Mesma via MoltenVK |
| Mesa/zink, ANGLE | MIT / BSD-3-Clause | GL → Vulkan/Metal | Via de tradução OpenGL |

**Caminho mais plausível hoje** (sem JIT): interpretador de CPU (Unicorn-classe
ou o interpretador próprio do Portico estendido) + subset Wine das APIs
necessárias pelo jogo alvo + frontend gráfico direto para o stream interno já
consumido pelo Metal. Desempenho de interpretação pura é baixo — é a realidade
da plataforma, não uma falha de projeto.

## 3. Regras

1. Respeitar as licenças (avisos, fonte das modificações para LGPL, notices).
2. Manter o componente isolado (diretório/módulo próprio, sem misturar com o
   código do app).
3. Registrar a origem e a versão em `docs/LICENSES.md`.
4. Nunca usar o app para burlar DRM/assinaturas/sandbox; somente conteúdo que
   o usuário tem direito de executar.
