/* Portico Runtime — processo Windows PE: a cadeia real de execução.
 *
 *   PE Loader → memória virtual (pr_vm) → imports → thunks stdcall (stubs
 *   x86 reais: mov eax, idx; int 0x2E; ret N) → Win32 (pr_win32) → CPU
 *   (pr_cpu) → encerramento do processo (ExitProcess).
 *
 * Diagnóstico de falhas no formato EXECUTION STOPPED (Reason/Module/Function).
 * Sem simulação: o que não é suportado falha com motivo claro. */
#ifndef PORTICO_PR_PEPROC_H
#define PORTICO_PR_PEPROC_H

#include "pr_types.h"
#include "pr_pe.h"
#include "pr_cpu.h"
#include "pr_vm.h"
#include "pr_win32.h"
#include "pr_log.h"
#include "pr_gfx.h"
#include "pr_surf.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pr_peproc pr_peproc;

typedef enum pr_peproc_stop {
    PR_PEPROC_STOP_NONE = 0,
    PR_PEPROC_STOP_EXIT,           /* ExitProcess — fim normal */
    PR_PEPROC_STOP_API_UNSUPPORTED,/* API Win32 não suportada */
    PR_PEPROC_STOP_MISSING_DLL,    /* DLL importada ausente */
    PR_PEPROC_STOP_MISSING_API,    /* API importada sem implementação */
    PR_PEPROC_STOP_BAD_MEMORY,     /* acesso inválido */
    PR_PEPROC_STOP_BAD_INSN,       /* instrução fora do subconjunto */
    PR_PEPROC_STOP_BAD_EXECUTABLE, /* imagem inválida/não suportada */
    PR_PEPROC_STOP_ARCH_UNSUPPORTED /* arquitetura sem backend de CPU */
} pr_peproc_stop;

/* Cria o processo a partir dos bytes do PE (análise + carga na memória virtual).
 * PR_ERR_FORMAT/PR_ERR_UNSUPPORTED com o diagnóstico preenchido. */
pr_status pr_peproc_create(const void* pe_data, size_t pe_len, pr_log* log,
                           pr_peproc** out);
void pr_peproc_destroy(pr_peproc* p);

/* Resolve imports (DLL/API → thunk), monta pilha e prepara a CPU no entry
 * point. PR_ERR_UNSUPPORTED grava o diagnóstico (EXECUTION STOPPED). */
pr_status pr_peproc_prepare(pr_peproc* p);

/* Executa até `budget` instruções. PR_OK mesmo com paradas de processo —
 * consulte pr_peproc_state. PR_ERR_FAULT p/ falha de runtime (diagnóstico). */
pr_status pr_peproc_step(pr_peproc* p, uint64_t budget, uint64_t* executed);

pr_peproc_stop pr_peproc_state(const pr_peproc* p);
int      pr_peproc_exited(const pr_peproc* p);   /* 1 se ExitProcess */
uint32_t pr_peproc_exit_code(const pr_peproc* p);
/* Texto completo "EXECUTION STOPPED\n\nReason:..." ("" se rodando). */
const char* pr_peproc_diagnostic(const pr_peproc* p);

pr_cpu*       pr_peproc_cpu(pr_peproc* p);
pr_win32_ctx* pr_peproc_win32(pr_peproc* p);
pr_vm*        pr_peproc_vm(pr_peproc* p);
pr_log*       pr_peproc_log(pr_peproc* p);
pr_surf*      pr_peproc_surface(pr_peproc* p); /* GDI; NULL se nenhuma */
/* Apresenta a superfície do processo no stream (se existir e estiver suja). */
pr_status     pr_peproc_present(pr_peproc* p, pr_gfx_stream* stream,
                                uint32_t target_w, uint32_t target_h);

/* ---- FASE 6: DLLs PE32+ (módulos do convidado) ----
 * provide_dll registra bytes PERMITIDOS (host/app — nunca filesystem do iOS);
 * load_dll mapeia de verdade (seções+relocations+imports+DllMain com refcount);
 * module_proc percorre a tabela de exports REAL da imagem; free_dll faz unload
 * com DllMain(DLL_PROCESS_DETACH) quando o refcount chega a 0. */
pr_status pr_peproc_provide_dll(pr_peproc* p, const char* name,
                                const void* dll_data, size_t dll_len);
pr_status pr_peproc_load_dll(pr_peproc* p, const char* name, uint64_t* out_handle);
uint64_t  pr_peproc_module_handle(pr_peproc* p, const char* name);
uint64_t  pr_peproc_module_proc(pr_peproc* p, uint64_t handle, const char* name,
                                uint32_t ordinal);
pr_status pr_peproc_free_dll(pr_peproc* p, uint64_t handle);
/* Invoca função do convidado (ABI Microsoft x64: rcx/rdx/r8/r9) até o
 * sentinel de retorno interno; *out_ret = RAX. Estado do processo é
 * restaurado; falha honesta em fault/estouro de budget. */
pr_status pr_peproc_call(pr_peproc* p, uint64_t fn, const uint64_t args[4],
                         uint64_t* out_ret);
/* FASE 7: raiz única do filesystem virtual (diretório do sandbox do app).
 * Sem raiz, CreateFile falha honesto — NUNCA acessa o filesystem do iOS. */
pr_status pr_peproc_set_fs_root(pr_peproc* p, const char* host_dir);

/* SEH/unwind x64: índice .pdata (RUNTIME_FUNCTION + UNWIND_INFO) da imagem. */
#include "portico/pr_unwind.h"
size_t     pr_peproc_unwind_count(const pr_peproc* p);
pr_status  pr_peproc_unwind_at(pr_peproc* p, size_t index, pr_unwind_info* out);
int        pr_peproc_unwind_find(pr_peproc* p, uint32_t rva, pr_unwind_info* out);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_PEPROC_H */
