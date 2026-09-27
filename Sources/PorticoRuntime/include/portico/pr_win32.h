/* Portico Runtime — camada Win32: catálogo de APIs por subsistema + dispatch.
 *
 * PRINCÍPIO: só existe implementação quando o comportamento é REAL no ambiente
 * do Portico (em-processo, iOS). APIs conhecidas mas não implementadas ficam no
 * catálogo como UNSUPPORTED: a chamada registra log e retorna PR_ERR_UNSUPPORTED
 * — nunca finge sucesso. APIs desconhecidas também são logadas.
 *
 * Chamadas usam uma ABI neutra (argumentos posicionais em uint64_t) — o
 * modelamento de stdcall/x64 acontece quando a CPU executar PEs de verdade. */
#ifndef PORTICO_PR_WIN32_H
#define PORTICO_PR_WIN32_H

#include "pr_types.h"
#include "pr_log.h"
#include "pr_input.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pr_win32_status {
    PR_WIN32_IMPLEMENTED = 0,
    PR_WIN32_UNSUPPORTED = 1  /* catalogada; sem implementação real */
} pr_win32_status;

typedef struct pr_win32_ctx pr_win32_ctx;

/* Assinatura neutra das implementações. args[i] é o i-ésimo argumento.
 * Ponteiros de convidado são host pointers validados via pr_win32_ptr(). */
typedef uint64_t (*pr_win32_fn)(pr_win32_ctx* ctx, const uint64_t* args,
                                size_t nargs, pr_status* out_st);

typedef struct pr_win32_export {
    const char* module;   /* "kernel32.dll", "user32.dll", ... */
    const char* name;
    uint32_t    ordinal;
    pr_win32_status status;
    pr_win32_fn fn;       /* != NULL sse IMPLEMENTED */
    uint16_t    stdcall_bytes; /* bytes de argumentos ×4 (stdcall: callee limpa a pilha) */
    const char* note;     /* descrição técnica curta */
    int is_data;          /* 1 = símbolo de DADOS (célula RW no convidado;
                           * o slot IAT recebe o endereço da célula, não thunk) */
    uint8_t   arg_xmm;    /* GRUPO 8: máscara bits0..3 — argumento i é
                           * float/double em XMMi (ABI x64) em vez de GPR/pilha */
} pr_win32_export;

/* Reentrada no convidado a partir de APIs (ex.: _initterm/_onexit do CRT):
 * registada pelo pr_peproc; ABI Microsoft x64. Retorna 0 se ausente. */
typedef uint64_t (*pr_win32_guestcall_fn)(void* ud, uint64_t fn,
                                          const uint64_t args[4],
                                          uint64_t* out_ret);
void pr_win32_set_guestcall(pr_win32_ctx* ctx, pr_win32_guestcall_fn fn, void* ud);

/* Módulos/subsistemas organizados. */
typedef struct pr_win32_module_info {
    const char* name;
    size_t implemented;
    size_t cataloged;
} pr_win32_module_info;

/* ---- Catálogo (estático; consulta estilo GetProcAddress) ---- */
size_t pr_win32_catalog(const pr_win32_export** out);
const pr_win32_export* pr_win32_lookup(const char* module, const char* name);
/* Resolução por ordinal (importação por ordinal / MAKEINTRESOURCE).
 * Somente ordinais PÚBLICOS e estáveis do ABI Windows (winsock.def etc.);
 * ordinal desconhecido → NULL (nunca adivinhar — sucesso falso proibido). */
const pr_win32_export* pr_win32_lookup_ordinal(const char* module,
                                               uint32_t ordinal);
size_t pr_win32_modules(const pr_win32_module_info** out);

/* ---- Contexto de processo (estado Win32 do convidado) ---- */
pr_win32_ctx* pr_win32_create(pr_log* log /* pode ser NULL */);
void          pr_win32_destroy(pr_win32_ctx* ctx);

/* Espaço de memória do convidado: regiões host validadas para argumentos-ponteiro
 * (imagem carregada + heap + dados estáticos). endereço = host pointer. */
pr_status pr_win32_bind_image(pr_win32_ctx* ctx, void* base, size_t size,
                              const char* module_name);
void*     pr_win32_ptr(pr_win32_ctx* ctx, uint64_t addr, size_t len);
uint8_t*  pr_win32_scratch(pr_win32_ctx* ctx, size_t* out_size);

/* ---- Integração com o processo PE (memória virtual do convidado) ---- */
struct pr_vm;
struct pr_surf;

/* Com vm vinculada, ponteiros do convidado são endereços GUEST validados pela
 * memória virtual; HeapAlloc/VirtualAlloc alocam no espaço do convidado. */
pr_status pr_win32_bind_vm(pr_win32_ctx* ctx, struct pr_vm* vm,
                           uint32_t guest_image_base, const char* module_name);
/* Endereços GUEST dos thunks (stubs stdcall) por índice do catálogo — usados
 * por GetProcAddress/IAT. */
pr_status pr_win32_bind_thunks(pr_win32_ctx* ctx, const uint32_t* addrs,
                               size_t count);
/* Chamada por entrada do catálogo (usada pelo stub: INT + stdcall). */
pr_status pr_win32_call_entry(pr_win32_ctx* ctx, const pr_win32_export* e,
                              const uint64_t* args, size_t nargs, uint64_t* out_ret);
size_t    pr_win32_index_of(const pr_win32_export* e);
/* Superfície GDI do processo (criada sob demanda; apresentada pelo host). */
struct pr_surf* pr_win32_surface(pr_win32_ctx* ctx);

/* GRUPO 8: âncora do estado OpenGL (opaco) no ctx Win32 */
struct pr_gl_state;
struct pr_gl_state* pr_win32_gl_state(const pr_win32_ctx* ctx);
void pr_win32_gl_set_state(pr_win32_ctx* ctx, struct pr_gl_state* gl);
void pr_win32_gl_log(pr_win32_ctx* ctx, const char* msg);
/* Superfície de um objeto GDI (bitmap criada por CreateCompatibleBitmap, ou o
 * alvo atual do DC). NULL se o handle não for de bitmap/DC. */
struct pr_surf* pr_win32_object_surface(pr_win32_ctx* ctx, uint32_t handle);

/* ---- Ciclo de vida de módulos do convidado (DLLs PE32+ reais) — FASE 6 ----
 * Registrado pelo pr_peproc. Sem registrador, LoadLibrary de nome que não seja
 * módulo embutido falha com ERROR_MOD_NOT_FOUND (nunca finge carga). */
typedef struct pr_win32_modops {
    uint64_t (*load)(void* ud, const char* name);            /* 0 = falha */
    uint64_t (*find)(void* ud, const char* name);            /* 0 = não carregado */
    uint64_t (*proc)(void* ud, uint64_t handle, const char* name,
                     uint32_t ordinal);                      /* 0 = não encontrado */
    int      (*free)(void* ud, uint64_t handle);             /* 1 = ok */
    void* ud;
} pr_win32_modops;
void pr_win32_set_modops(pr_win32_ctx* ctx, const pr_win32_modops* ops);

/* ---- Sistema de arquivos virtual (FASE 7) ----
 * Raiz ÚNICA permitida (diretório do sandbox do app). Caminhos Windows são
 * normalizados para dentro da raiz; qualquer tentativa de escapar ("..",
 * absoluto fora da raiz) é recusada com ACCESS_DENIED + log. Sem raiz,
 * CreateFile falha honesto (não há área permitida). */
pr_status pr_win32_set_fs_root(pr_win32_ctx* ctx, const char* host_dir);

/* Dispatch: resolve e executa. Se catalogada/sem implementação → log + PR_ERR_UNSUPPORTED;
 * se desconhecida → log + PR_ERR_RANGE. */
pr_status pr_win32_call(pr_win32_ctx* ctx, const char* module, const char* name,
                        const uint64_t* args, size_t nargs, uint64_t* out_ret);

/* Estado (comportamento real de Win32 equivalente). */
uint32_t  pr_win32_last_error(const pr_win32_ctx* ctx);

/* ---- GRUPO 7: camada de entrada host (separada da representação Win32) ---- */
enum {
    PR_MOUSE_MOVE = 0, PR_MOUSE_LDOWN, PR_MOUSE_LUP, PR_MOUSE_RDOWN,
    PR_MOUSE_RUP, PR_MOUSE_MDOWN, PR_MOUSE_MUP, PR_MOUSE_WHEEL
};
enum {
    PR_TOUCH_BEGAN = 0, PR_TOUCH_MOVED, PR_TOUCH_ENDED, PR_TOUCH_CANCELLED
};
enum { PR_CTRL_BUTTON = 0, PR_CTRL_AXIS, PR_CTRL_TRIGGER };

/* virtual keys Win32 (subseto suportado pela tradução de entrada) */
enum {
    W32_VK_BACK = 0x08, W32_VK_TAB = 0x09, W32_VK_RETURN = 0x0D,
    W32_VK_SHIFT = 0x10, W32_VK_CONTROL = 0x11, W32_VK_MENU = 0x12,
    W32_VK_ESCAPE = 0x1B, W32_VK_SPACE = 0x20,
    W32_VK_LEFT = 0x25, W32_VK_UP = 0x26, W32_VK_RIGHT = 0x27,
    W32_VK_DOWN = 0x28,
    W32_VK_0 = 0x30, W32_VK_9 = 0x39,
    W32_VK_A = 0x41, W32_VK_Z = 0x5A,
    W32_VK_F1 = 0x70, W32_VK_F12 = 0x7B
};

pr_status pr_win32_input_key(pr_win32_ctx* ctx, uint32_t vk, int down);
pr_status pr_win32_input_char(pr_win32_ctx* ctx, uint32_t ch);
pr_status pr_win32_input_mouse(pr_win32_ctx* ctx, uint32_t kind,
                               int32_t x, int32_t y, int32_t wheel);
pr_status pr_win32_input_touch(pr_win32_ctx* ctx, uint32_t id,
                               uint32_t phase, int32_t x, int32_t y);
pr_status pr_win32_input_controller(pr_win32_ctx* ctx, uint32_t kind,
                                    uint32_t id, float value);
const pr_input_state* pr_win32_input_pad(const pr_win32_ctx* ctx);
pr_status pr_win32_advance_time(pr_win32_ctx* ctx, uint32_t ms);
uint32_t pr_win32_focus_hwnd(const pr_win32_ctx* ctx);
void      pr_win32_set_last_error(pr_win32_ctx* ctx, uint32_t err);
int       pr_win32_halted(const pr_win32_ctx* ctx);   /* ExitProcess chamado? */

/* Ambiente/cwd/linha de comando do processo (FASE 2/3). */
pr_status pr_win32_env_set(pr_win32_ctx* ctx, const char* name, const char* value);
pr_status pr_win32_set_cwd(pr_win32_ctx* ctx, const char* path);
pr_status pr_win32_set_cmdline(pr_win32_ctx* ctx, const char* cmd);
uint32_t  pr_win32_exit_code(const pr_win32_ctx* ctx);
/* Drena stdout/stderr capturados (WriteFile em handles de console). */
size_t    pr_win32_stdout_read(pr_win32_ctx* ctx, char* out, size_t cap);
/* Motivo específico da última falha de API (ex.: "formato %q nao suportado").
 * NULL se nenhum — os traps usam no EXECUTION STOPPED em vez do note genérico. */
const char* pr_win32_last_detail(const pr_win32_ctx* ctx);
/* Flush dos buffers stdio do convidado (stdout por linha / stderr). */
void        pr_win32_stdio_flush(pr_win32_ctx* ctx);
/* Contadores de chamadas (diagnóstico). */
size_t    pr_win32_calls_implemented(const pr_win32_ctx* ctx);
size_t    pr_win32_calls_unsupported(const pr_win32_ctx* ctx);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_WIN32_H */
