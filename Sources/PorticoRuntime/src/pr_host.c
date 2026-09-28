/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

/* PR_HOST: fachada de sessão, registro de backends e backend nativo pxp-interpreter. */
#include "portico/pr_host.h"
#include "portico/pr_cpu.h"
#include "portico/pr_cap.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#define MAX_BACKENDS 8

/* Mapa MMIO (compartilhado com pr_selftest.c). */
#define MMIO_BASE       0xF0000000u
#define OFF_GFX_OP      0x000
#define OFF_GFX_A0      0x004
#define OFF_GFX_A1      0x008
#define OFF_GFX_A2      0x00C
#define OFF_GFX_A3      0x010
#define OFF_GFX_SUBMIT  0x014
#define OFF_VTX_X       0x018
#define OFF_VTX_Y       0x01C
#define OFF_VTX_R       0x20
#define OFF_VTX_G       0x24
#define OFF_VTX_B       0x28
#define OFF_VTX_SUB     0x2C
#define OFF_TONE_FREQ   0x200
#define OFF_LOG_DATA    0x30C
#define OFF_LOG_FLUSH   0x310
#define OFF_SYS_HALT    0x314

/* ---- registro global de backends ---- */

static pr_host_backend_v1 g_backends[MAX_BACKENDS];
static size_t g_backend_count = 0;
static pthread_mutex_t g_reg_mu = PTHREAD_MUTEX_INITIALIZER;

pr_status pr_host_register_backend(const pr_host_backend_v1* backend) {
    if (!backend || backend->abi_version != PR_HOST_ABI_VERSION) return PR_ERR_INVALID;
    if (!backend->can_execute || !backend->start || !backend->frame) return PR_ERR_INVALID;
    pthread_mutex_lock(&g_reg_mu);
    pr_status st = PR_OK;
    if (g_backend_count >= MAX_BACKENDS) {
        st = PR_ERR_RANGE;
    } else {
        for (size_t i = 0; i < g_backend_count; i++) {
            if (g_backends[i].name && backend->name &&
                strcmp(g_backends[i].name, backend->name) == 0) {
                st = PR_ERR_STATE; /* já registrado */
                break;
            }
        }
        if (st == PR_OK) g_backends[g_backend_count++] = *backend;
    }
    pthread_mutex_unlock(&g_reg_mu);
    return st;
}

size_t pr_host_backend_count(void) {
    pthread_mutex_lock(&g_reg_mu);
    size_t n = g_backend_count;
    pthread_mutex_unlock(&g_reg_mu);
    return n;
}

const pr_host_backend_v1* pr_host_backend_at(size_t i) {
    pthread_mutex_lock(&g_reg_mu);
    const pr_host_backend_v1* r = (i < g_backend_count) ? &g_backends[i] : NULL;
    pthread_mutex_unlock(&g_reg_mu);
    return r;
}

/* ---- sessão ---- */

struct pr_host {
    pr_gfx_stream* gfx;
    pr_audio_ring* audio;
    pr_log* log;
    int log_owned;
    pr_host_state state;
    const pr_host_backend_v1* backend;
    void* ud;
    uint32_t target_w, target_h;
    float fps_cap;
    char last_msg[128];
};

pr_host* pr_host_create(void) {
    pr_host* h = (pr_host*)calloc(1, sizeof(pr_host));
    if (!h) return NULL;
    h->gfx = pr_gfx_stream_create(2048, 262144);
    h->audio = pr_audio_ring_create(16384);
    if (!h->gfx || !h->audio) {
        pr_host_destroy(h);
        return NULL;
    }
    h->state = PR_HOST_IDLE;
    h->target_w = 640;
    h->target_h = 360;
    return h;
}

void pr_host_destroy(pr_host* h) {
    if (!h) return;
    pr_host_stop(h);
    if (h->backend && h->backend->shutdown) h->backend->shutdown(h->backend->user);
    pr_gfx_stream_destroy(h->gfx);
    pr_audio_ring_destroy(h->audio);
    if (h->log_owned) pr_log_destroy(h->log);
    free(h);
}

pr_gfx_stream* pr_host_gfx(pr_host* h) { return h ? h->gfx : NULL; }
pr_audio_ring* pr_host_audio(pr_host* h) { return h ? h->audio : NULL; }
pr_log* pr_host_log(pr_host* h) { return h ? h->log : NULL; }
void pr_host_set_target(pr_host* h, uint32_t w, uint32_t hh, float fps_cap) {
    if (!h) return;
    h->target_w = w;
    h->target_h = hh;
    h->fps_cap = fps_cap;
}
void pr_host_set_ud(pr_host* h, void* ud) { if (h) h->ud = ud; }
void* pr_host_ud(pr_host* h) { return h ? h->ud : NULL; }

pr_host_state pr_host_state_of(const pr_host* h) {
    return h ? h->state : PR_HOST_IDLE;
}

pr_status pr_host_select_backend(pr_host* h, const pr_host_start_info* info,
                                 char* reason, size_t reason_len) {
    if (!h || !info) return PR_ERR_INVALID;
    if (reason && reason_len) reason[0] = 0;

    pr_pe_info pe;
    int have_pe = 0;
    if (info->is_pe) {
        if (pr_pe_scan(info->payload, info->payload_len, &pe) != PR_OK) {
            if (reason) snprintf(reason, reason_len, "imagem PE inválida");
            return PR_ERR_FORMAT;
        }
        have_pe = 1;
    }

    pthread_mutex_lock(&g_reg_mu);
    const pr_host_backend_v1* chosen = NULL;
    char local_reason[128];
    local_reason[0] = 0;
    for (size_t i = 0; i < g_backend_count; i++) {
        pr_host_backend_v1* b = &g_backends[i];
        char r[128];
        r[0] = 0;
        if (b->can_execute(b->user, have_pe ? &pe : NULL, r, sizeof(r))) {
            chosen = b;
            break;
        }
        if (r[0] && !local_reason[0]) {
            snprintf(local_reason, sizeof(local_reason), "%s", r);
        }
    }
    if (chosen) h->backend = chosen;
    pthread_mutex_unlock(&g_reg_mu);

    if (!chosen) {
        if (reason) {
            snprintf(reason, reason_len, "%s",
                     local_reason[0] ? local_reason
                                     : "nenhum backend de execução registrado");
        }
        return PR_ERR_UNSUPPORTED;
    }
    return PR_OK;
}

pr_status pr_host_start(pr_host* h, const pr_host_start_info* info, pr_log* log) {
    if (!h || !info || !info->payload || info->payload_len == 0) return PR_ERR_INVALID;
    if (h->state == PR_HOST_RUNNING) return PR_ERR_STATE;

    h->log = log;
    h->log_owned = 0;
    if (!h->log) {
        h->log = pr_log_create(1024);
        h->log_owned = 1;
    }

    char reason[128];
    pr_status st = pr_host_select_backend(h, info, reason, sizeof(reason));
    if (st != PR_OK) {
        snprintf(h->last_msg, sizeof(h->last_msg), "%s", reason);
        pr_log_write(h->log, PR_LOG_ERROR, "host", "sem backend: %s", reason);
        h->state = PR_HOST_FAILED;
        return st;
    }

    pr_gfx_stream_reset(h->gfx);
    pr_audio_ring_reset(h->audio);

    st = h->backend->start(h, h->backend->user, info, h->log);
    if (st != PR_OK) {
        snprintf(h->last_msg, sizeof(h->last_msg), "backend '%s' falhou ao iniciar: %s",
                 h->backend->name ? h->backend->name : "?", pr_status_str(st));
        pr_log_write(h->log, PR_LOG_ERROR, "host", "%s", h->last_msg);
        h->state = PR_HOST_FAILED;
        return st;
    }
    h->state = PR_HOST_RUNNING;
    pr_log_write(h->log, PR_LOG_INFO, "host", "sessão iniciada (backend: %s %s)",
                 h->backend->name ? h->backend->name : "?",
                 h->backend->version ? h->backend->version : "");
    return PR_OK;
}

pr_status pr_host_frame(pr_host* h, const pr_host_frame_in* in, pr_host_frame_out* out) {
    if (!h || !in || !out) return PR_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    if (h->state != PR_HOST_RUNNING) {
        out->state = h->state;
        snprintf(out->message, sizeof(out->message), "%s", h->last_msg);
        return PR_ERR_STATE;
    }
    pr_status st = h->backend->frame(h, h->backend->user, in, out);
    if (out->state == PR_HOST_FAILED || st == PR_ERR_FAULT) {
        h->state = PR_HOST_FAILED;
        snprintf(h->last_msg, sizeof(h->last_msg), "%s", out->message);
    } else if (out->halted || out->state == PR_HOST_STOPPED) {
        h->state = PR_HOST_STOPPED;
        out->state = PR_HOST_STOPPED;
        pr_log_write(h->log, PR_LOG_INFO, "host", "sessão encerrada pelo convidado");
    }
    out->state = h->state;
    return st;
}

void pr_host_stop(pr_host* h) {
    if (!h) return;
    if (h->backend && h->backend->stop) h->backend->stop(h, h->backend->user);
    if (h->state == PR_HOST_RUNNING) h->state = PR_HOST_STOPPED;
}

/* ==================== Backend nativo: pxp-interpreter ==================== */

typedef struct pxp_state {
    pr_cpu* cpu;
    pr_tone tone;
    pr_log* log;
    pr_host* host;
    /* registradores MMIO de gráficos */
    uint32_t gfx_op, gfx_a[4];
    /* vértice em construção (valores brutos) */
    uint32_t vtx[5];
    /* tom */
    uint32_t tone_freq_q, tone_vol_u8, tone_wave;
    /* log */
    char log_line[PR_LOG_MSG_MAX];
    size_t log_len;
    /* relógio */
    double time_ms;
    uint32_t frame_idx;
    uint32_t rng;
    int halt_req;
    int frame_done;
    uint32_t frames_presented;
    uint32_t target_w, target_h;
    pr_input_state input;
    float scratch[8192];
} pxp_state;

static uint32_t pxp_mmio_read(void* ud, uint32_t off) {
    pxp_state* s = (pxp_state*)ud;
    switch (off) {
        case 0x030: return s->target_w;
        case 0x034: return s->target_h;
        case 0x100: return s->input.buttons;
        case 0x104: return (uint32_t)(int32_t)(s->input.axes[0] * 65536.0f);
        case 0x108: return (uint32_t)(int32_t)(s->input.axes[1] * 65536.0f);
        case 0x10C: return (uint32_t)(int32_t)(s->input.axes[2] * 65536.0f);
        case 0x110: return (uint32_t)(int32_t)(s->input.axes[3] * 65536.0f);
        case 0x114: return (uint32_t)(s->input.trigger_lt * 255.0f);
        case 0x118: return (uint32_t)(s->input.trigger_rt * 255.0f);
        case 0x300: {
            uint64_t t = (uint64_t)s->time_ms;
            return (uint32_t)(t & 0xFFFFFFFFu);
        }
        case 0x304: {
            uint64_t t = (uint64_t)s->time_ms;
            return (uint32_t)(t >> 32);
        }
        case 0x308: return s->frame_idx;
        case 0x318: {
            /* xorshift32 */
            uint32_t x = s->rng ? s->rng : 0x9E3779B9u;
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            s->rng = x;
            return x;
        }
        default:
            return 0;
    }
}

static float q16_to_f(uint32_t v) {
    return (float)((int32_t)v) / 65536.0f;
}

static void pxp_mmio_write(void* ud, uint32_t off, uint32_t value) {
    pxp_state* s = (pxp_state*)ud;
    switch (off) {
        case OFF_GFX_OP: s->gfx_op = value; break;
        case OFF_GFX_A0: s->gfx_a[0] = value; break;
        case OFF_GFX_A1: s->gfx_a[1] = value; break;
        case OFF_GFX_A2: s->gfx_a[2] = value; break;
        case OFF_GFX_A3: s->gfx_a[3] = value; break;
        case OFF_GFX_SUBMIT: {
            pr_gfx_cmd cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.op = s->gfx_op;
            switch (s->gfx_op) {
                case PR_GFX_CLEAR:
                    cmd.a.clear.color.r = (float)s->gfx_a[0] / 255.0f;
                    cmd.a.clear.color.g = (float)s->gfx_a[1] / 255.0f;
                    cmd.a.clear.color.b = (float)s->gfx_a[2] / 255.0f;
                    cmd.a.clear.color.a = (float)s->gfx_a[3] / 255.0f;
                    break;
                case PR_GFX_SET_VIEWPORT:
                case PR_GFX_SET_SCISSOR:
                    cmd.a.rect.rect.x = q16_to_f(s->gfx_a[0]);
                    cmd.a.rect.rect.y = q16_to_f(s->gfx_a[1]);
                    cmd.a.rect.rect.w = q16_to_f(s->gfx_a[2]);
                    cmd.a.rect.rect.h = q16_to_f(s->gfx_a[3]);
                    break;
                case PR_GFX_DRAW_TRIANGLES:
                    cmd.a.draw.vertex_count = s->gfx_a[0];
                    cmd.a.draw.first_vertex = s->gfx_a[1];
                    break;
                case PR_GFX_PRESENT:
                    cmd.a.present.width = s->gfx_a[0];
                    cmd.a.present.height = s->gfx_a[1];
                    s->frame_done = 1;
                    s->frames_presented++;
                    pr_cpu_break(s->cpu); /* fim do frame: devolve o controle ao host */
                    break;
                case PR_GFX_SET_FILTER:
                    cmd.a.filter.filter = s->gfx_a[0];
                    break;
                default:
                    cmd.op = PR_GFX_NOP;
                    break;
            }
            pr_gfx_push(pr_host_gfx(s->host), &cmd);
            break;
        }
        case OFF_VTX_X: s->vtx[0] = value; break;
        case OFF_VTX_Y: s->vtx[1] = value; break;
        case OFF_VTX_R: s->vtx[2] = value; break;
        case OFF_VTX_G: s->vtx[3] = value; break;
        case OFF_VTX_B: s->vtx[4] = value; break;
        case OFF_VTX_SUB: {
            pr_vertex v;
            v.x = q16_to_f(s->vtx[0]);
            v.y = q16_to_f(s->vtx[1]);
            v.r = (float)s->vtx[2] / 255.0f;
            v.g = (float)s->vtx[3] / 255.0f;
            v.b = (float)s->vtx[4] / 255.0f;
            pr_gfx_push_vertex(pr_host_gfx(s->host), &v);
            break;
        }
        case OFF_TONE_FREQ: s->tone_freq_q = value;
            s->tone.freq_hz = q16_to_f(value);
            if (s->tone.freq_hz < 0) s->tone.freq_hz = 0;
            break;
        case OFF_TONE_FREQ + 4:
            s->tone_vol_u8 = value;
            s->tone.volume = (float)(value & 0xFF) / 255.0f;
            break;
        case OFF_TONE_FREQ + 8:
            s->tone_wave = value;
            s->tone.wave = (value == 1) ? PR_WAVE_SINE
                        : (value == 0) ? PR_WAVE_SQUARE : PR_WAVE_SILENCE;
            break;
        case OFF_LOG_DATA: {
            if (s->log_len + 1 < sizeof(s->log_line)) {
                s->log_line[s->log_len++] = (char)(value & 0xFF);
                s->log_line[s->log_len] = 0;
            }
            break;
        }
        case OFF_LOG_FLUSH: {
            if (s->log_len > 0) {
                pr_log_write(s->log, PR_LOG_INFO, "pxp", "%s", s->log_line);
            }
            s->log_len = 0;
            s->log_line[0] = 0;
            break;
        }
        case OFF_SYS_HALT: s->halt_req = 1; pr_cpu_break(s->cpu); break;
        default: break;
    }
}

static int pxp_trap(void* ud, pr_cpu* cpu, uint8_t vec) {
    (void)cpu;
    pxp_state* s = (pxp_state*)ud;
    pr_log_write(s->log, PR_LOG_WARN, "pxp", "INT %u no payload", (unsigned)vec);
    return 1; /* interrompe o frame atual */
}

static int pxp_can_execute(void* user, const pr_pe_info* pe, char* reason, size_t reason_len) {
    (void)user;
    if (pe != NULL) {
        if (reason) {
            snprintf(reason, reason_len,
                     "backend nativo não executa PEs Windows: requer camada Win32 "
                     "(não integrada; ver docs/BACKEND_INTEGRATION.md)");
        }
        return 0;
    }
    return 1; /* payloads nativos PXP */
}

static pr_status pxp_start(pr_host* h, void* user, const pr_host_start_info* info, pr_log* log) {
    (void)user;
    if (!info->payload || info->payload_len < 24) return PR_ERR_FORMAT;
    const uint8_t* d = (const uint8_t*)info->payload;
    if (memcmp(d, "PXP0", 4) != 0) {
        pr_log_write(log, PR_LOG_ERROR, "pxp", "payload não é PXP0");
        return PR_ERR_FORMAT;
    }
    uint32_t version, load, entry, code_size, bss_size;
    memcpy(&version, d + 4, 4);
    memcpy(&load, d + 8, 4);
    memcpy(&entry, d + 12, 4);
    memcpy(&code_size, d + 16, 4);
    memcpy(&bss_size, d + 20, 4);
    if (version != 1) return PR_ERR_UNSUPPORTED;
    if (24u + code_size > info->payload_len) return PR_ERR_FORMAT;

    pxp_state* s = (pxp_state*)calloc(1, sizeof(pxp_state));
    if (!s) return PR_ERR_NOMEM;
    s->host = h;
    s->log = log;
    s->rng = 0x9E3779B9u;
    s->target_w = info->target_w ? info->target_w : 640;
    s->target_h = info->target_h ? info->target_h : 360;
    pr_tone_init(&s->tone);

    s->cpu = pr_cpu_create(8u << 20); /* 8 MiB flat */
    if (!s->cpu) {
        free(s);
        return PR_ERR_NOMEM;
    }
    pr_status st = pr_cpu_load(s->cpu, load, d + 24, code_size);
    if (st != PR_OK) {
        pr_cpu_destroy(s->cpu);
        free(s);
        return st;
    }
    /* BSS zerada por calloc do pr_cpu_create. */
    (void)bss_size;

    pr_cpu_set_reg(s->cpu, PR_REG_ESP, 0x00700000u);
    pr_cpu_set_eip(s->cpu, entry);
    pr_cpu_set_mmio(s->cpu, 0, MMIO_BASE, 0x4000, pxp_mmio_read, pxp_mmio_write, s);
    pr_cpu_set_trap(s->cpu, pxp_trap, s);

    pr_host_set_ud(h, s);
    pr_log_write(log, PR_LOG_INFO, "pxp",
                 "payload carregado: entry=0x%08X code=%u B=%s",
                 entry, code_size, info->argv ? info->argv : "");
    return PR_OK;
}

static pr_status pxp_frame(pr_host* h, void* user, const pr_host_frame_in* in,
                           pr_host_frame_out* out) {
    (void)user;
    pxp_state* s = (pxp_state*)pr_host_ud(h);
    if (!s) return PR_ERR_STATE;

    s->input = in->input;
    s->time_ms = in->time_ms;
    s->frame_done = 0;

    /* Se o consumidor (renderer) drenou o frame anterior, recicla o stream.
     * Contrato: o consumidor deve consumir comandos+vértices e resetar após o
     * PRESENT (pr_gfx_stream_reset); aqui é a válvela de segurança. */
    if (pr_gfx_pending(pr_host_gfx(h)) == 0) {
        pr_gfx_stream_reset(pr_host_gfx(h));
    }

    uint64_t executed = 0;
    pr_status st = pr_cpu_run(s->cpu, 2000000, &executed);
    s->frame_idx++;

    /* áudio: renderiza o tom do frame (dt) para dentro do ring */
    {
        size_t frames = (size_t)(in->dt * 48000.0f);
        if (frames > 4096) frames = 4096;
        if (frames > 0) {
            pr_tone_render(&s->tone, s->scratch, frames);
            pr_audio_push(pr_host_audio(h), s->scratch, frames);
        }
    }

    out->frames_presented = s->frames_presented;
    if (st == PR_ERR_FAULT) {
        const pr_cpu_fault* f = pr_cpu_last_fault(s->cpu);
        out->state = PR_HOST_FAILED;
        out->status = st;
        snprintf(out->message, sizeof(out->message),
                 "falta no payload @0x%08X op=%02X: %s",
                 f ? f->eip : 0, f ? f->opcode : 0,
                 f ? f->reason : "?");
        pr_log_write(s->log, PR_LOG_ERROR, "pxp", "%s", out->message);
        return st;
    }
    if (s->halt_req || pr_cpu_halted(s->cpu)) {
        out->halted = 1;
        out->state = PR_HOST_STOPPED;
        out->status = PR_OK;
        return PR_OK;
    }
    if (s->frame_done) {
        out->state = PR_HOST_RUNNING;
        out->status = PR_OK;
        return PR_OK;
    }
    /* payload não apresentou frame (laço infinito sem present?) — reporta e continua */
    out->state = PR_HOST_RUNNING;
    out->status = PR_OK;
    if (executed >= 2000000) {
        pr_log_write(s->log, PR_LOG_WARN, "pxp",
                     "orçamento de instruções esgotado sem PRESENT neste frame");
    }
    return PR_OK;
}

static void pxp_stop(pr_host* h, void* user) {
    (void)user;
    pxp_state* s = (pxp_state*)pr_host_ud(h);
    if (!s) return;
    pr_cpu_destroy(s->cpu);
    s->cpu = NULL;
    free(s);
    pr_host_set_ud(h, NULL);
}

static void pxp_shutdown(void* user) { (void)user; }

static pr_host_backend_v1 g_pxp_backend = {
    PR_HOST_ABI_VERSION,
    "pxp-interpreter",
    "1.0.0",
    NULL,
    pxp_can_execute,
    pxp_start,
    pxp_frame,
    pxp_stop,
    pxp_shutdown,
};

void pr_host_register_builtin_backends(void) {
    pthread_mutex_lock(&g_reg_mu);
    int found = 0;
    for (size_t i = 0; i < g_backend_count; i++) {
        if (g_backends[i].name && strcmp(g_backends[i].name, "pxp-interpreter") == 0) {
            found = 1;
            break;
        }
    }
    pthread_mutex_unlock(&g_reg_mu);
    if (!found) pr_host_register_backend(&g_pxp_backend);
}
