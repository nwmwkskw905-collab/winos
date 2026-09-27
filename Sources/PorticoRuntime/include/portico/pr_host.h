/* Portico Runtime — PR_HOST: fachada de sessão de execução e ABI de backend plugável.
 *
 * Um "backend de execução" executa o software importado dentro do processo do app
 * (o iOS NÃO permite fork/exec de binários — ver docs/LIMITATIONS.md). A ABI abaixo
 * é o ponto de encaixe real de futuras camadas de compatibilidade Windows
 * (ex.: componente classe-Wine compilado para iOS), sem nenhuma falsa simulação:
 * enquanto nenhum backend real for registrado, PEs Windows são rejeitados com
 * motivo explícito (PR_ERR_UNSUPPORTED + texto).
 *
 * O backend nativo de referência ("pxp-interpreter") executa payloads PXP
 * (código IA-32 interpretado) — usado pelo Self-Test do app e pelos testes. */
#ifndef PORTICO_PR_HOST_H
#define PORTICO_PR_HOST_H

#include "pr_types.h"
#include "pr_pe.h"
#include "pr_gfx.h"
#include "pr_audio.h"
#include "pr_input.h"
#include "pr_log.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PR_HOST_ABI_VERSION 1u

typedef enum pr_host_state {
    PR_HOST_IDLE = 0,
    PR_HOST_RUNNING = 1,
    PR_HOST_STOPPED = 2,
    PR_HOST_FAILED = 3
} pr_host_state;

typedef struct pr_host_start_info {
    const void* payload;      /* bytes PXP ou PE */
    size_t      payload_len;
    int         is_pe;        /* 1 = imagem PE (Windows); 0 = PXP nativo */
    const char* argv;         /* argumentos (texto; pode ser NULL) */
    const char* const* env_keys;
    const char* const* env_vals;
    size_t      env_count;
    uint32_t    target_w;     /* resolução alvo do host */
    uint32_t    target_h;
    float       fps_cap;      /* 0 = sem limite aqui */
} pr_host_start_info;

typedef struct pr_host_frame_in {
    pr_input_state input;
    float dt;                 /* segundos desde o frame anterior */
    double time_ms;           /* relógio da sessão */
} pr_host_frame_in;

typedef struct pr_host_frame_out {
    pr_host_state state;
    uint32_t frames_presented; /* total na sessão */
    int      halted;           /* payload executou HLT */
    pr_status status;          /* PR_OK, PR_ERR_FAULT, ... */
    char     message[128];     /* motivo legível quando FAILED */
} pr_host_frame_out;

typedef struct pr_host pr_host;

/* ---- ABI do backend (v1) ---- */
typedef struct pr_host_backend_v1 {
    uint32_t    abi_version;   /* PR_HOST_ABI_VERSION */
    const char* name;          /* ex.: "pxp-interpreter" */
    const char* version;       /* ex.: "1.0.0" */
    void*       user;
    /* Pode executar esta imagem? 1=sim; 0=não e grava o motivo. */
    int  (*can_execute)(void* user, const pr_pe_info* pe, char* reason, size_t reason_len);
    pr_status (*start)(pr_host* h, void* user, const pr_host_start_info* info, pr_log* log);
    /* Roda um frame; backend emite comandos em pr_host_gfx(h) e PCM em pr_host_audio(h). */
    pr_status (*frame)(pr_host* h, void* user, const pr_host_frame_in* in, pr_host_frame_out* out);
    void (*stop)(pr_host* h, void* user);
    void (*shutdown)(void* user);
} pr_host_backend_v1;

/* Registro global de backends (até 8). */
pr_status pr_host_register_backend(const pr_host_backend_v1* backend);
size_t    pr_host_backend_count(void);
const pr_host_backend_v1* pr_host_backend_at(size_t i);
/* Registra o backend nativo pxp-interpreter (idempotente). */
void pr_host_register_builtin_backends(void);

/* ---- Sessão ---- */
pr_host* pr_host_create(void);
void     pr_host_destroy(pr_host* h);

/* Seleciona backend compatível com o payload; PR_ERR_UNSUPPORTED com motivo se nenhum. */
pr_status pr_host_select_backend(pr_host* h, const pr_host_start_info* info,
                                 char* reason, size_t reason_len);
pr_status pr_host_start(pr_host* h, const pr_host_start_info* info, pr_log* log);
pr_status pr_host_frame(pr_host* h, const pr_host_frame_in* in, pr_host_frame_out* out);
void      pr_host_stop(pr_host* h);
pr_host_state pr_host_state_of(const pr_host* h);

pr_gfx_stream*   pr_host_gfx(pr_host* h);
pr_audio_ring*   pr_host_audio(pr_host* h);
pr_log*          pr_host_log(pr_host* h);
void             pr_host_set_target(pr_host* h, uint32_t w, uint32_t height, float fps_cap);
/* Estado privado por sessão para o backend ativo. */
void             pr_host_set_ud(pr_host* h, void* ud);
void*            pr_host_ud(pr_host* h);

/* Constrói o payload PXP do Self-Test (código IA-32 interpretado).
 * Retorna buffer malloc (liberar com free). */
pr_status pr_selftest_payload_build(void** out_data, size_t* out_len);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_HOST_H */
