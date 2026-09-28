#define _GNU_SOURCE   /* G46: pthread_timedjoin_np (join com prazo) Linux */

/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
/* Darwin também precisa POSIX para clock_gettime */
#define _POSIX_C_SOURCE 200809L
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_win32.h"
#include "portico/pr_gl.h"
#include "portico/pr_vm.h"
#include "portico/pr_cpu64.h"
#include "portico/pr_surf.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <errno.h>

#if defined(__APPLE__)
#if __has_include(<TargetConditionals.h>)
#include <TargetConditionals.h>
#endif
#endif

/* ============================================================
 * Compatibilidade Darwin/iOS — Apple Clang não possui
 * pthread_mutex_timedlock e pthread_timedjoin_np (GNU extensions).
 * Implementação preserva timeout, códigos de retorno, EINTR/ETIMEDOUT,
 * evita busy-loop e deadlock, mantém Linux intacto.
 * ============================================================ */
#if defined(__APPLE__)
/* Darwin/iOS: fallback para pthread_mutex_timedlock
 * Semântica: tenta trylock em loop com sleep 1ms, respeita abs_timeout (CLOCK_REALTIME),
 * retorna 0 em sucesso, ETIMEDOUT em timeout, preserva errno.
 * Evita busy-loop: sleep adaptativo 1ms (cap 5ms) + nanosleep com EINTR handling.
 */
static int pr_darwin_pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *abs_timeout) {
    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        /* fallback: try once */
        if (pthread_mutex_trylock(mutex) == 0) return 0;
        return ETIMEDOUT;
    }
    /* já expirou? */
    if (now.tv_sec > abs_timeout->tv_sec ||
        (now.tv_sec == abs_timeout->tv_sec && now.tv_nsec >= abs_timeout->tv_nsec)) {
        if (pthread_mutex_trylock(mutex) == 0) return 0;
        return ETIMEDOUT;
    }
    while (1) {
        if (pthread_mutex_trylock(mutex) == 0) return 0;
        if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
            /* erro clock: tenta mais uma vez sem bloqueio */
            return ETIMEDOUT;
        }
        if (now.tv_sec > abs_timeout->tv_sec ||
            (now.tv_sec == abs_timeout->tv_sec && now.tv_nsec >= abs_timeout->tv_nsec)) {
            return ETIMEDOUT;
        }
        /* calcula remaining para sleep adaptativo, evita busy-loop */
        struct timespec rem;
        rem.tv_sec = abs_timeout->tv_sec - now.tv_sec;
        rem.tv_nsec = abs_timeout->tv_nsec - now.tv_nsec;
        if (rem.tv_nsec < 0) {
            rem.tv_sec--;
            rem.tv_nsec += 1000000000L;
        }
        struct timespec sleep_ts;
        if (rem.tv_sec > 0 || rem.tv_nsec > 5000000L) {
            sleep_ts.tv_sec = 0;
            sleep_ts.tv_nsec = 1000000L; /* 1ms — responsivo sem busy-loop */
        } else {
            /* remaining <5ms: dorme remaining (cap 1ms para evitar oversleep) */
            sleep_ts.tv_sec = 0;
            sleep_ts.tv_nsec = rem.tv_nsec > 1000000L ? 1000000L : rem.tv_nsec;
            if (sleep_ts.tv_nsec <= 0) {
                return ETIMEDOUT;
            }
        }
        struct timespec rem_sleep;
        while (nanosleep(&sleep_ts, &rem_sleep) == -1 && errno == EINTR) {
            sleep_ts = rem_sleep;
        }
    }
}

/* Darwin/iOS: fallback para pthread_timedjoin_np
 * Semântica original Linux:
 *   - espera thread terminar até abs_timeout (CLOCK_REALTIME)
 *   - retorna 0 se thread terminou dentro do prazo (join real)
 *   - retorna ETIMEDOUT se prazo expirou com thread ainda executando
 *   - preserva códigos de retorno
 * Estratégia Darwin:
 *   - usa flag volatile done (w32_thread.done) setada por w32_thread_main antes de retornar
 *   - polling done com sleep 1ms + timeout check (evita busy-loop)
 *   - quando done==1, chama pthread_join para liberar recursos (join real, sem timeout)
 *   - mantém caminho Linux intacto fora de __APPLE__
 */
static int pr_darwin_pthread_timedjoin_np(pthread_t tid, void **retval, const struct timespec *abs_timeout, volatile int *done_flag) {
    (void)tid; /* tid usado apenas no join final; polling via done_flag */
    (void)retval;
    struct timespec now;
    while (done_flag && !*done_flag) {
        if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
            return ETIMEDOUT;
        }
        if (now.tv_sec > abs_timeout->tv_sec ||
            (now.tv_sec == abs_timeout->tv_sec && now.tv_nsec >= abs_timeout->tv_nsec)) {
            return ETIMEDOUT;
        }
        struct timespec sleep_ts = {0, 1000000L}; /* 1ms */
        struct timespec rem_sleep;
        while (nanosleep(&sleep_ts, &rem_sleep) == -1 && errno == EINTR) {
            sleep_ts = rem_sleep;
        }
    }
    /* thread terminou dentro do prazo (ou done_flag NULL): join real */
    return 0; /* caller fará pthread_join */
}
#endif /* __APPLE__ */

/* ============================================================
 * Catálogo de APIs por subsistema.
 * Somente PR_WIN32_IMPLEMENTED possui fn != NULL e comportamento real.
 * ============================================================ */

/* Handles pseudo do processo (tabela de handles real do contexto). */
#define PR_WIN32_H_PROCESS 0xF0F0F001u
#define W32_H_MSVCRT      0xB0B00008u
#define PR_WIN32_H_HEAP    0xF0F0F002u
#define PR_WIN32_H_STDIN   0xF0F0F003u
#define PR_WIN32_H_STDOUT  0xF0F0F004u
#define PR_WIN32_H_STDERR  0xF0F0F005u
#define PR_WIN32_H_THREAD_BASE 0xF0F0F100u  /* G42: handles de thread (8 slots) */
#define PR_WIN32_H_MUTEX_BASE  0xF0F0F200u  /* G44: handles de mutex (8 slots) */
#define PR_WIN32_H_EVENT_BASE  0xF0F0F300u  /* G81: handles de evento (8 slots) */
#define PR_WIN32_H_FIND_BASE   0xF0F0F400u  /* G82: handles de FindFirstFile (8 slots) */
#define PR_WIN32_H_REG_BASE    0xF0F0F500u  /* G84: handles de Registry (16 slots) */
#define PR_WIN32_H_SEM_BASE    0xF0F0F600u  /* G86: handles de semaphore (8 slots) */
#ifndef W32_ERROR_NOT_OWNER
#define W32_ERROR_NOT_OWNER 288u
#endif
#ifndef W32_ERROR_NOT_ENOUGH_MEMORY
#define W32_ERROR_NOT_ENOUGH_MEMORY 8u
#endif

/* Constantes Win32 usadas. */
#define W32_STD_INPUT_HANDLE  ((uint64_t)-10)
#define W32_STD_OUTPUT_HANDLE ((uint64_t)-11)
#define W32_STD_ERROR_HANDLE  ((uint64_t)-12)
#define W32_ERROR_INVALID_HANDLE     6u
#define W32_ERROR_MOD_NOT_FOUND      126u
#define W32_ERROR_PROC_NOT_FOUND     127u
#define W32_ERROR_INVALID_PARAMETER  87u
#define W32_ERROR_INSUFFICIENT_BUFFER 122u
#define W32_ERROR_ENVVAR_NOT_FOUND    203u
#define W32_ERROR_NO_MORE_FILES      18u
#define W32_ERROR_FILE_NOT_FOUND     2u
#define W32_ERROR_PATH_NOT_FOUND     3u
#define W32_ERROR_ACCESS_DENIED      5u
#define W32_ERROR_ALREADY_EXISTS     183u
#define W32_FILE_ATTRIBUTE_READONLY  0x1u
#define W32_FILE_ATTRIBUTE_HIDDEN    0x2u
#define W32_FILE_ATTRIBUTE_SYSTEM    0x4u
#define W32_FILE_ATTRIBUTE_DIRECTORY 0x10u
#define W32_FILE_ATTRIBUTE_ARCHIVE   0x20u
#define W32_FILE_ATTRIBUTE_NORMAL    0x80u

/* ---- memória alocada (heap + VirtualAlloc) rastreada p/ validação ---- */
typedef struct w32_block { void* p; size_t size; } w32_block;

typedef struct w32_gblock { uint32_t addr; uint32_t size; } w32_gblock;

typedef struct gdi_obj {
    int used;
    uint32_t handle;
    uint32_t kind;    /* 1 = DC, 2 = brush, 3 = bitmap, 4 = pen, 5 = font */
    uint32_t color;   /* COLORREF (brush/pen) */
    uint32_t sel;     /* DC: brush selecionada */
    uint32_t sel_bmp; /* DC: bitmap selecionada (0 = superfície do processo) */
    uint32_t sel_pen; /* DC: pen selecionada */
    uint32_t sel_font;/* DC: font selecionada */
    int32_t cur_x, cur_y; /* DC: current position */
    uint32_t w, h;    /* bitmap: dimensões */
    struct pr_surf* surf; /* bitmap: buffer XRGB8888 próprio (stride real) */
} gdi_obj;

#define GDI_MAX 64
#define W32_GDI_HDC_BASE   0xD1100000u
#define W32_GDI_HBRUSH_BASE 0xD1200000u
#define W32_GDI_HBITMAP_BASE 0xD1300000u
#define W32_GDI_HPEN_BASE    0xD1500000u
#define W32_GDI_HFONT_BASE   0xD1600000u
#define W32_STOCK_WHITE_BRUSH 0xD120FFFFu

/* ---- Janelas/mensagens (Win32 real → superfície interna do WinOS) ---- */
#define W32_HWND_BASE        0xD1400000u
#define W32_MAX_CLASSES      8
#define W32_MAX_WINDOWS      8
#define W32_MSGQ_MAX         32
#define W32_WM_CREATE        0x0001u
#define W32_WM_DESTROY       0x0002u
#define W32_WM_MOVE          0x0003u
#define W32_WM_SIZE          0x0005u
#define W32_WM_PAINT         0x000Fu
#define W32_WM_CLOSE         0x0010u
#define W32_WM_QUIT          0x0012u
#define W32_WM_ERASEBKGND    0x0014u
#define W32_WM_SHOWWINDOW    0x0018u
#define W32_WM_KEYDOWN       0x0100u
#define W32_WM_KEYUP         0x0101u
#define W32_WM_CHAR          0x0102u
#define W32_WM_TIMER         0x0113u
#define W32_WM_MOUSEMOVE     0x0200u
#define W32_WM_LBUTTONDOWN   0x0201u
#define W32_WM_LBUTTONUP     0x0202u
#define W32_WM_RBUTTONDOWN   0x0204u
#define W32_WM_RBUTTONUP     0x0205u
#define W32_WM_MBUTTONDOWN   0x0207u
#define W32_WM_MBUTTONUP     0x0208u
#define W32_WM_MOUSEWHEEL    0x020Au
#define W32_MK_LBUTTON       0x0001u
#define W32_MK_RBUTTON       0x0002u
#define W32_MK_SHIFT         0x0004u
#define W32_MK_CONTROL       0x0008u
#define W32_MK_MBUTTON       0x0010u
#define W32_MAX_TIMERS       16
#define W32_TOUCH_MAX        10
#define W32_INPUT_PEND_MAX   32
#define W32_ERROR_INVALID_WINDOW_HANDLE 1400u
#define W32_WS_VISIBLE       0x10000000u
#define W32_ERROR_CLASS_ALREADY_EXISTS 1411u
#define W32_ERROR_CANNOT_FIND_WND_CLASS 1407u

typedef struct w32_class {
    int used;
    uint64_t wndproc;   /* WNDPROC do convidado (endereço guest/stub) */
    uint64_t hbr_bg;    /* brush da classe (fundo) */
    char name[64];
} w32_class;

typedef struct w32_window {
    int used;
    uint32_t handle;
    int cls;            /* índice em classes[] */
    int32_t x, y;
    uint32_t cw, ch;    /* área cliente (sem área não-cliente neste build) */
    int visible;
    int update_pending; /* região de update (InvalidateRect/criação) */
    int erase_bg;       /* apagar fundo no próximo BeginPaint/DefWindowProc */
    int destroyed;
    char title[64];
} w32_window;

typedef struct w32_msgq_ent {
    uint32_t hwnd, message;
    uint64_t wparam, lparam;
} w32_msgq_ent;

typedef struct w32_timer {
    int used;
    uint32_t hwnd, id, elapse;
    uint64_t next_ms;
    uint64_t proc;   /* TIMERPROC do convidado (0 = postar WM_TIMER) */
} w32_timer;

typedef struct w32_touch {
    int used;
    uint32_t id;
    int32_t x, y;
} w32_touch;

typedef struct w32_inp_msg {
    uint32_t hwnd, message;
    uint64_t wp, lp;
} w32_inp_msg;

/* ---- G42: threads reais do convidado (CreateThread/Wait/CloseHandle) ---- */
#define W32_MAX_THREADS 8
#define W32_MAX_MUTEXES 8
#define W32_MAX_EVENTS 8
#define W32_MAX_FINDS 8
#define W32_MAX_REG_KEYS 16
#define W32_MAX_SEMS 8
typedef struct w32_mutex {
    int used, owned;        /* owned = held por alguma thread (rec >= 1) */
    unsigned rec;           /* contagem recursiva (semântica Windows) */
    pthread_mutex_t m;      /* exclusão mútua REAL entre pthreads host */
    pthread_t owner;
} w32_mutex;

typedef struct w32_event {
    int used;
    int manual;             /* 1 = manual-reset, 0 = auto-reset */
    int signaled;
    pthread_mutex_t m;
    pthread_cond_t c;
} w32_event;

typedef struct w32_sem {
    int used;
    int count;
    int max_count;
    pthread_mutex_t m;
    pthread_cond_t c;
} w32_sem;

typedef struct w32_find {
    int used;
    char dir_host[256];     /* host dir path */
    char pattern[192];      /* wildcard pattern */
    void* dir;              /* DIR* (opaque) */
} w32_find;

typedef struct w32_reg_key {
    int used;
    char full_path[256];    /* virtual registry path: HKEY_CURRENT_USER/Software/... */
} w32_reg_key;

typedef struct w32_thread {
    int used, done, failed, joined;
    pthread_t tid;
    uint64_t exit_code;
    uint32_t stack_base, stack_size;
    pr_cpu64* cpu;
    pr_win32_ctx* ctx;
} w32_thread;

struct pr_win32_ctx {
    pr_log* log;
    uint32_t last_error;
    int halted;
    uint32_t exit_code;
    void* img_base; size_t img_size; char module_name[64];
    uint8_t* scratch; size_t scratch_size;
    char* out_buf; size_t out_len, out_cap;
    w32_block* blocks; size_t nblocks, blocks_cap;
    size_t calls_ok, calls_unsup;
    uint64_t start_ns;
    /* processo PE (endereços guest) */
    pr_vm* vm;
    uint32_t guest_img_base;
    uint32_t* thunk_addrs; size_t thunk_count;
    w32_gblock* gblocks; size_t ngblocks, gblocks_cap;
    uint32_t guest_cmdline;
    uint32_t guest_cmdline_w;   /* linha UTF-16 em guest_cmdline+256 */
    uint64_t glstr[4];          /* G13: cache guest de glGetString (estável) */
    char cwd[96];               /* diretório de trabalho do processo */
    char env_names[8][64];      /* ambiente do processo (Portico) */
    char env_vals[8][128];
    size_t env_count;
    pr_surf* gdi_surface;
    gdi_obj gdi[GDI_MAX];
    /* FASE 6/7: módulos do convidado + filesystem virtual */
    pr_win32_modops modops;
    char fs_root[256];
    FILE* files[8];
    pr_log* fs_sink;   /* log para decisões do VFS (negadas/permitidas) */
    /* stdio real (msvcrt): vetor iob[3] no convidado + buffers básicos */
    uint64_t iob_base;          /* guest: FILE[3] (stdin/stdout/stderr), 48B cada */
    char sbuf[2][2048];         /* buffers: [0]=stdout (line), [1]=stderr (direto) */
    size_t slen[2];
    char diag_detail[160];      /* motivo específico da última falha da API */
    /* CRT real (msvcrt): reentrada, atexit, TLS, CS, filtro de exceção */
    pr_win32_guestcall_fn guest_call;
    void* guest_ud;
    uint64_t onexit_fns[32];
    size_t nonexit;
    uint64_t tls_slots[64];
    uint64_t tls_used;   /* bitmap TLS: bits 60..63 reservados (CRT); 0..59 alocaveis */
    uint64_t uef_filter;
    char cmdline[192];      /* linha de comando do processo (host) */
    uint64_t app_type;
    uint64_t matherr_fn;
    /* G42: threads do convidado — concorrência real via pthread */
    w32_thread threads[W32_MAX_THREADS];
    w32_mutex mutexes[W32_MAX_MUTEXES];
    w32_event events[W32_MAX_EVENTS];
    w32_sem sems[W32_MAX_SEMS];
    w32_find finds[W32_MAX_FINDS];
    w32_reg_key reg_keys[W32_MAX_REG_KEYS];
    uint32_t thread_sentinel;   /* VA do byte HLT de retorno natural da thread */
    uint32_t thread_sentinel_sz;
    /* janelas + fila de mensagens (Win32 → superfície interna) */
    w32_class  classes[W32_MAX_CLASSES];
    w32_window windows[W32_MAX_WINDOWS];
    w32_msgq_ent msgq[W32_MSGQ_MAX];
    int msgq_head, msgq_len;
    int quit_posted;
    uint32_t quit_code;
    /* GRUPO 7: entrada host (separada) + temporizadores */
    uint32_t focus_hwnd;
    uint8_t key_down[256], key_toggle[256];
    uint32_t mouse_buttons;
    int32_t mouse_x, mouse_y;
    w32_timer timers[W32_MAX_TIMERS];
    uint64_t time_offset_ms;
    w32_touch touches[W32_TOUCH_MAX];
    w32_inp_msg inp_pend[W32_INPUT_PEND_MAX];
    int inp_pend_len;
    struct pr_input_state pad;
    struct pr_gl_state* gl;   /* GRUPO 8: OpenGL 1.1 software (opaco) */
};

static uint64_t mono_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

void* pr_win32_ptr(pr_win32_ctx* ctx, uint64_t addr, size_t len) {
    if (!ctx || addr == 0) return NULL;
    /* endereço GUEST (processo PE vinculado) */
    if (ctx->vm && addr < 0x100000000ull) {
        void* g = pr_vm_translate(ctx->vm, (uint32_t)addr, len, PR_VM_PROT_R);
        if (g) return g;
        /* guest inválido: NÃO tenta interpretar como host pointer */
        return NULL;
    }
    uint8_t* p = (uint8_t*)(uintptr_t)addr;
    /* imagem carregada */
    if (ctx->img_base) {
        uint8_t* b = (uint8_t*)ctx->img_base;
        if (p >= b && p + len <= b + ctx->img_size) return p;
    }
    /* scratch */
    if (ctx->scratch && p >= ctx->scratch && p + len <= ctx->scratch + ctx->scratch_size)
        return p;
    /* blocos de heap/alloc */
    for (size_t i = 0; i < ctx->nblocks; i++) {
        uint8_t* b = (uint8_t*)ctx->blocks[i].p;
        if (p >= b && p + len <= b + ctx->blocks[i].size) return p;
    }
    return NULL;
}

static int track_block(pr_win32_ctx* ctx, void* p, size_t size) {
    if (ctx->nblocks == ctx->blocks_cap) {
        size_t cap = ctx->blocks_cap ? ctx->blocks_cap * 2 : 16;
        w32_block* nb = (w32_block*)realloc(ctx->blocks, cap * sizeof(w32_block));
        if (!nb) return 0;
        ctx->blocks = nb;
        ctx->blocks_cap = cap;
    }
    ctx->blocks[ctx->nblocks].p = p;
    ctx->blocks[ctx->nblocks].size = size;
    ctx->nblocks++;
    return 1;
}

static w32_block* find_block(pr_win32_ctx* ctx, void* p) {
    for (size_t i = 0; i < ctx->nblocks; i++)
        if (ctx->blocks[i].p == p) return &ctx->blocks[i];
    return NULL;
}

static void untrack_block(pr_win32_ctx* ctx, void* p) {
    for (size_t i = 0; i < ctx->nblocks; i++) {
        if (ctx->blocks[i].p == p) {
            ctx->blocks[i] = ctx->blocks[ctx->nblocks - 1];
            ctx->nblocks--;
            return;
        }
    }
}

static void w32_log(pr_win32_ctx* ctx, pr_log_level lv, const char* fmt, ...) {
    if (!ctx || !ctx->log) return;   /* guarda NULL: achado do -fanalyzer */
    char msg[PR_LOG_MSG_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    pr_log_write(ctx->log, lv, "win32", "%s", msg);
}

/* ============================================================
 * Implementações reais (kernel32)
 * ============================================================ */

static uint64_t f_GetTickCount64(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)a; (void)n; *st = PR_OK;
    return (mono_ns() - ctx->start_ns) / 1000000ull;
}

static uint64_t f_QueryPerformanceFrequency(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint64_t* out = (uint64_t*)pr_win32_ptr(ctx, a[0], sizeof(uint64_t));
    if (!out) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; *st = PR_OK; return 0; }
    *out = 1000000000ull; /* contador em nanossegundos */
    return 1;
}

static uint64_t f_QueryPerformanceCounter(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint64_t* out = (uint64_t*)pr_win32_ptr(ctx, a[0], sizeof(uint64_t));
    if (!out) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; *st = PR_OK; return 0; }
    *out = mono_ns();
    return 1;
}

static uint64_t f_GetProcessHeap(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)a; (void)n; *st = PR_OK;
    return PR_WIN32_H_HEAP;
}

static uint64_t f_HeapAlloc(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3 || a[0] != PR_WIN32_H_HEAP) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    size_t size = (size_t)a[2];
    if (size == 0) size = 1;
    if (ctx->vm) {
        uint32_t gaddr = 0;
        if (pr_vm_alloc(ctx->vm, size, PR_VM_PROT_R | PR_VM_PROT_W, "w32heap",
                        &gaddr, NULL) != PR_OK) {
            *st = PR_ERR_NOMEM;
            return 0;
        }
        if (ctx->ngblocks == ctx->gblocks_cap) {
            size_t cap = ctx->gblocks_cap ? ctx->gblocks_cap * 2 : 16;
            w32_gblock* nb = (w32_gblock*)realloc(ctx->gblocks, cap * sizeof(w32_gblock));
            if (!nb) { *st = PR_ERR_NOMEM; return 0; }
            ctx->gblocks = nb;
            ctx->gblocks_cap = cap;
        }
        ctx->gblocks[ctx->ngblocks].addr = gaddr;
        ctx->gblocks[ctx->ngblocks].size = (uint32_t)size;
        ctx->ngblocks++;
        return (uint64_t)gaddr;
    }
    void* p = calloc(1, size); /* HEAP_ZERO_MEMORY equivalente a calloc p/ previsibilidade */
    if (!p) { *st = PR_ERR_NOMEM; return 0; }
    if (!track_block(ctx, p, size)) { free(p); *st = PR_ERR_NOMEM; return 0; }
    return (uint64_t)(uintptr_t)p;
}

static w32_gblock* find_gblock(pr_win32_ctx* ctx, uint32_t gaddr) {
    for (size_t i = 0; i < ctx->ngblocks; i++)
        if (ctx->gblocks[i].addr == gaddr) return &ctx->gblocks[i];
    return NULL;
}

static uint64_t f_HeapFree(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3 || a[0] != PR_WIN32_H_HEAP) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    if (ctx->vm) {
        uint32_t gaddr = (uint32_t)a[2];
        w32_gblock* b = find_gblock(ctx, gaddr);
        if (!b) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
        pr_vm_unmap(ctx->vm, b->addr, b->size);
        *b = ctx->gblocks[--ctx->ngblocks];
        return 1;
    }
    void* p = (void*)(uintptr_t)a[2];
    w32_block* b = find_block(ctx, p);
    if (!b) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    untrack_block(ctx, p);
    free(p);
    return 1;
}

static uint64_t f_HeapSize(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3 || a[0] != PR_WIN32_H_HEAP) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return (uint64_t)-1; }
    if (ctx->vm) {
        w32_gblock* b = find_gblock(ctx, (uint32_t)a[2]);
        if (!b) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return (uint64_t)-1; }
        return b->size;
    }
    w32_block* b = find_block(ctx, (void*)(uintptr_t)a[2]);
    if (!b) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return (uint64_t)-1; }
    return b->size;
}

static uint64_t f_HeapReAlloc(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3 || a[0] != PR_WIN32_H_HEAP) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    uint64_t old_ptr;
    size_t new_size;
    if (n >= 4) {
        old_ptr = a[2];
        new_size = (size_t)a[3];
    } else {
        old_ptr = a[2];
        new_size = (size_t)a[1];
    }
    if (new_size==0) new_size=1;
    if (old_ptr==0) {
        uint64_t na[3] = { a[0], 0, (uint64_t)new_size };
        return f_HeapAlloc(ctx, na, 3, st);
    }
    if (ctx->vm) {
        w32_gblock* b0 = find_gblock(ctx, (uint32_t)old_ptr);
        if (!b0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
        uint32_t old_addr = b0->addr;
        size_t old_size = b0->size;
        uint32_t new_addr = 0;
        if (pr_vm_alloc(ctx->vm, new_size, PR_VM_PROT_R|PR_VM_PROT_W, "w32heap", &new_addr, NULL)!=PR_OK) { *st=PR_ERR_NOMEM; return 0; }
        if (ctx->ngblocks == ctx->gblocks_cap) {
            size_t cap = ctx->gblocks_cap ? ctx->gblocks_cap*2 : 16;
            w32_gblock* nb = (w32_gblock*)realloc(ctx->gblocks, cap*sizeof(w32_gblock));
            if (!nb) { pr_vm_unmap(ctx->vm, new_addr, new_size); *st=PR_ERR_NOMEM; return 0; }
            ctx->gblocks = nb; ctx->gblocks_cap = cap;
        }
        ctx->gblocks[ctx->ngblocks].addr = new_addr;
        ctx->gblocks[ctx->ngblocks].size = (uint32_t)new_size;
        ctx->ngblocks++;
        uint8_t* src = pr_win32_ptr(ctx, old_addr, old_size < new_size ? old_size : new_size);
        uint8_t* dst = pr_win32_ptr(ctx, new_addr, old_size < new_size ? old_size : new_size);
        if (src && dst) memcpy(dst, src, old_size < new_size ? old_size : new_size);
        pr_vm_unmap(ctx->vm, old_addr, old_size);
        /* G88 fix: b0 pode ter sido invalidado por realloc acima, re-busca */
        w32_gblock* b = find_gblock(ctx, (uint32_t)old_ptr);
        if (b) {
            *b = ctx->gblocks[--ctx->ngblocks];
        } else {
            /* fallback: procura por old_addr que já foi unmapped, então remove via linear scan */
            for (size_t i=0;i<ctx->ngblocks;i++) {
                if (ctx->gblocks[i].addr == old_addr) {
                    ctx->gblocks[i] = ctx->gblocks[--ctx->ngblocks];
                    break;
                }
            }
        }
        return (uint64_t)new_addr;
    }
    void* p = (void*)(uintptr_t)old_ptr;
    w32_block* b = find_block(ctx, p);
    if (!b) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    void* np = calloc(1, new_size);
    if (!np) { *st=PR_ERR_NOMEM; return 0; }
    size_t copy = b->size < new_size ? b->size : new_size;
    memcpy(np, p, copy);
    untrack_block(ctx, p); free(p);
    if (!track_block(ctx, np, new_size)) { free(np); *st=PR_ERR_NOMEM; return 0; }
    return (uint64_t)(uintptr_t)np;
}
static uint64_t f_LocalAlloc(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    size_t size = (size_t)a[1];
    if (size==0) size=1;
    uint64_t na[3] = { PR_WIN32_H_HEAP, 0, (uint64_t)size };
    return f_HeapAlloc(ctx, na, 3, st);
}
static uint64_t f_LocalFree(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0) { *st=PR_OK; return 0; }
    uint64_t ha[3] = { PR_WIN32_H_HEAP, 0, a[0] };
    uint64_t r = f_HeapFree(ctx, ha, 3, st);
    if (r==0) return a[0];
    return 0;
}
static uint64_t f_LocalReAlloc(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    uint64_t old_ptr = a[0];
    size_t new_size = (size_t)a[1];
    if (new_size==0) new_size=1;
    if (old_ptr==0) {
        uint64_t na[3] = { PR_WIN32_H_HEAP, 0, (uint64_t)new_size };
        return f_HeapAlloc(ctx, na, 3, st);
    }
    uint64_t ha[3] = { PR_WIN32_H_HEAP, (uint64_t)new_size, old_ptr };
    return f_HeapReAlloc(ctx, ha, 3, st);
}
static uint64_t f_LocalSize(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint64_t ha[3] = { PR_WIN32_H_HEAP, 0, a[0] };
    return f_HeapSize(ctx, ha, 3, st);
}
static uint64_t f_GlobalAlloc(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    return f_LocalAlloc(ctx, a, n, st);
}
static uint64_t f_GlobalFree(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    return f_LocalFree(ctx, a, n, st);
}
static uint64_t f_GlobalReAlloc(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    return f_LocalReAlloc(ctx, a, n, st);
}
static uint64_t f_GlobalSize(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    return f_LocalSize(ctx, a, n, st);
}

static int page_to_prot(uint32_t p);
static uint32_t prot_to_page(int prot);

static uint64_t f_VirtualAlloc(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    size_t size = (size_t)a[1];
    if (size == 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if (ctx->vm) {
        uint32_t gaddr = 0;
        /* G39 (primeiro blocker real do hello_virt_protect): respeitar
         * flProtect (a[3]) quando a conversao e valida; antes a regiao nascia
         * R|W|X fixo e o lpflOldProtect do VirtualProtect reportava 0x40 em
         * vez do estado real da regiao. Valores invalidos mantem o
         * comportamento anterior (R|W|X). */
        int prot = PR_VM_PROT_R | PR_VM_PROT_W | PR_VM_PROT_X;
        if (n >= 4) {
            int req = page_to_prot((uint32_t)a[3]);
            if (req >= 0) prot = req;
        }
        if (pr_vm_alloc(ctx->vm, size, prot, "w32virt", &gaddr, NULL) != PR_OK) {
            *st = PR_ERR_NOMEM;
            return 0;
        }
        return (uint64_t)gaddr;
    }
    void* p = calloc(1, size);
    if (!p) { *st = PR_ERR_NOMEM; return 0; }
    if (!track_block(ctx, p, size)) { free(p); *st = PR_ERR_NOMEM; return 0; }
    return (uint64_t)(uintptr_t)p;
}

static uint64_t f_VirtualFree(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (ctx->vm) {
        /* VirtualFree guest: desmapeia região w32virt que começa em a[0] */
        uint32_t gaddr = (uint32_t)a[0];
        pr_vm_region regs[PR_VM_MAX_REGIONS];
        size_t nr = pr_vm_regions(ctx->vm, regs, PR_VM_MAX_REGIONS);
        for (size_t i = 0; i < nr; i++) {
            if (regs[i].base == gaddr && strcmp(regs[i].tag, "w32virt") == 0) {
                pr_vm_unmap(ctx->vm, gaddr, regs[i].size);
                return 1;
            }
        }
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    void* p = (void*)(uintptr_t)a[0];
    w32_block* b = find_block(ctx, p);
    if (!b) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    untrack_block(ctx, p);
    free(p);
    return 1;
}

static uint64_t f_GetStdHandle(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    /* nStdHandle é DWORD no Win32: o valor chega zero-extended (0xFFFFFFF6) via
     * ABI real; comparar em 32 bits cobre o caminho do PE e o dos testes (-10). */
    switch ((uint32_t)a[0]) {
        case (uint32_t)W32_STD_INPUT_HANDLE:  return PR_WIN32_H_STDIN;
        case (uint32_t)W32_STD_OUTPUT_HANDLE: return PR_WIN32_H_STDOUT;
        case (uint32_t)W32_STD_ERROR_HANDLE:  return PR_WIN32_H_STDERR;
        default: ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0;
    }
}

static int w32_is_file_handle(uint64_t h);
static int w32_file_idx(uint64_t h);
static void wstr_to_ascii(pr_win32_ctx* ctx, uint64_t addr, char* dst, size_t cap);

/* ---- console sink compartilhado (WriteFile e stdio do msvcrt) ---- */
static void out_append(pr_win32_ctx* ctx, const char* buf, size_t len,
                       const char* tag) {
    if (!len) return;
    if (ctx->out_len + len + 1 > ctx->out_cap) {
        size_t cap = ctx->out_cap ? ctx->out_cap * 2 : 256;
        while (cap < ctx->out_len + len + 1) cap *= 2;
        char* nb = (char*)realloc(ctx->out_buf, cap);
        if (!nb) return;
        ctx->out_buf = nb;
        ctx->out_cap = cap;
    }
    memcpy(ctx->out_buf + ctx->out_len, buf, len);
    ctx->out_len += len;
    ctx->out_buf[ctx->out_len] = 0;
    w32_log(ctx, PR_LOG_DEBUG, "console(%s): %.*s", tag,
            (int)(len > 120 ? 120 : len), buf);
}

/* ---- stdio do msvcrt: FILE[3] (stdin/stdout/stderr) + buffers básicos ----
 * FILE do msvcrt x64 = 48B ({_ptr,_cnt,_base,_flag,_file,_charbuf,_bufsiz,_tmpfname}).
 * Identidade: FILE* == iob_base + idx*48. stdout = bufferizado por linha
 * (flush em '\n' e no exit); stderr = não bufferizado (como o msvcrt). */
#define W32_IOB_STRIDE 48
#define W32_FOPEN_READ  0x0001
#define W32_FOPEN_WRITE 0x0002

static void stdio_flush(pr_win32_ctx* ctx, int idx) {
    if (idx < 0 || idx > 1 || ctx->slen[idx] == 0) return;
    out_append(ctx, ctx->sbuf[idx], ctx->slen[idx], idx == 1 ? "stderr" : "stdout");
    ctx->slen[idx] = 0;
}

static void stdio_emit(pr_win32_ctx* ctx, int idx, const char* p, size_t len) {
    if (idx == 1) { out_append(ctx, p, len, "stderr"); return; }  /* _IONBF */
    while (len) {
        size_t room = 2048 - ctx->slen[idx];
        size_t take = len < room ? len : room;
        memcpy(ctx->sbuf[idx] + ctx->slen[idx], p, take);
        ctx->slen[idx] += take;
        p += take;
        len -= take;
        /* line-buffered: flush a cada '\n' (comportamento real do console) */
        if (ctx->slen[idx] == 2048 ||
            memchr(ctx->sbuf[idx], '\n', ctx->slen[idx]))
            stdio_flush(ctx, idx);
    }
}

void pr_win32_stdio_flush(pr_win32_ctx* ctx) {
    if (!ctx) return;
    stdio_flush(ctx, 0);
    stdio_flush(ctx, 1);
}

/* FILE* -> índice do stream (0/1/2); -1 = FILE* desconhecido */
static int stdio_stream_of(pr_win32_ctx* ctx, uint64_t file) {
    if (!ctx->iob_base) return -1;
    if (file == ctx->iob_base) return 0;
    if (file == ctx->iob_base + W32_IOB_STRIDE) return 1;
    if (file == ctx->iob_base + 2 * W32_IOB_STRIDE) return 2;
    return -1;
}

static void diagf(pr_win32_ctx* ctx, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(ctx->diag_detail, sizeof(ctx->diag_detail), fmt, ap);
    va_end(ap);
    w32_log(ctx, PR_LOG_ERROR, "msvcrt: %s", ctx->diag_detail);
}

const char* pr_win32_last_detail(const pr_win32_ctx* ctx) {
    return ctx && ctx->diag_detail[0] ? ctx->diag_detail : NULL;
}

/* ---- formatador real (%d %i %u %x %X %o %c %s %p %%; flags/width/prec; h..ll) ---- */
typedef struct w32_va {
    pr_win32_ctx* ctx;
    const uint64_t* regs;   /* slots pré-coletados (fprintf/printf) */
    size_t nregs;
    uint64_t guest_va;      /* va_list do convidado (vfprintf) */
    size_t slot;
} w32_va;

static int va_next(w32_va* v, uint64_t* out) {
    if (v->regs) {
        if (v->slot >= v->nregs) return 0;
        *out = v->regs[v->slot++];
        return 1;
    }
    if (v->slot >= 32) return 0;   /* limite honesto de variádicos */
    uint64_t val = 0;
    const void* p = pr_win32_ptr(v->ctx, v->guest_va + v->slot * 8, 8);
    if (!p) return 0;
    memcpy(&val, p, 8);
    v->slot++;
    *out = val;
    return 1;
}

static int w32_format(char* dst, size_t cap, const char* fmt, w32_va* va) {
    size_t out = 0;
#define PUT(s, l) do { size_t _l = (l); if (out + _l >= cap) return -2; \
                       memcpy(dst + out, (s), _l); out += _l; } while (0)
    for (size_t i = 0; fmt[i]; i++) {
        if (fmt[i] != '%') { PUT(fmt + i, 1); continue; }
        i++;
        if (fmt[i] == '%') { PUT("%", 1); continue; }
        /* flags */
        int left = 0, plus = 0, space = 0, zero = 0, alt = 0;
        while (fmt[i] && strchr("-+ 0#", fmt[i])) {
            if (fmt[i] == '-') left = 1;
            else if (fmt[i] == '+') plus = 1;
            else if (fmt[i] == ' ') space = 1;
            else if (fmt[i] == '0') zero = 1;
            else alt = 1;
            i++;
        }
        /* largura */
        long width = 0;
        if (fmt[i] == '*') {
            uint64_t av;
            if (!va_next(va, &av)) return -1;
            width = (long)(int32_t)(uint32_t)av;
            if (width < 0) { left = 1; width = -width; }
            i++;
        } else {
            while (fmt[i] >= '0' && fmt[i] <= '9') { width = width * 10 + (fmt[i] - '0'); i++; }
        }
        /* precisão */
        long prec = -1;
        if (fmt[i] == '.') {
            i++;
            prec = 0;
            if (fmt[i] == '*') {
                uint64_t av;
                if (!va_next(va, &av)) return -1;
                prec = (long)(int32_t)(uint32_t)av;
                i++;
            } else {
                while (fmt[i] >= '0' && fmt[i] <= '9') { prec = prec * 10 + (fmt[i] - '0'); i++; }
            }
        }
        /* comprimento */
        int lenmod = 0;   /* 0 def, 1=h, 2=hh, 3=l, 4=ll/z */
        if (fmt[i] == 'h') { lenmod = fmt[i + 1] == 'h' ? 2 : 1; i += (lenmod == 2 ? 2 : 1); }
        else if (fmt[i] == 'l') { lenmod = fmt[i + 1] == 'l' ? 4 : 3; i += (lenmod == 4 ? 2 : 1); }
        else if (fmt[i] == 'z' || fmt[i] == 't') { lenmod = 4; i++; }

        char spec = fmt[i];
        if (!spec) return -1;
        uint64_t av = 0;
        if (!va_next(va, &av)) return -1;

        char chunk[512];
        int clen = 0;
        if (spec == 'd' || spec == 'i') {
            long long sv = (lenmod == 4) ? (long long)av
                        : (lenmod == 3) ? (long)(int32_t)(uint32_t)av
                        : (lenmod == 2) ? (signed char)(uint8_t)av
                        : (lenmod == 1) ? (short)(uint16_t)av
                        : (int)(int32_t)(uint32_t)av;
            char f2[64];
            snprintf(f2, sizeof(f2), "%%%s%s%s%s*ld",
                     left ? "-" : "", plus ? "+" : "", space ? " " : "",
                     zero ? "0" : "");
            /* l suficiente (64-bit em todos os hosts alvo) */
            snprintf(f2, sizeof(f2), "%%%s%s%s%s*lld",
                     left ? "-" : "", plus ? "+" : "", space ? " " : "",
                     zero ? "0" : "");
            clen = snprintf(chunk, sizeof(chunk), f2, (int)width, sv);
        } else if (spec == 'u' || spec == 'x' || spec == 'X' || spec == 'o') {
            unsigned long long uv = (lenmod >= 3) ? av
                        : (lenmod == 2) ? (uint8_t)av
                        : (lenmod == 1) ? (uint16_t)av
                        : (uint32_t)av;
            char conv[2] = { spec == 'X' ? 'X' : (spec == 'o' ? 'o' : (spec == 'u' ? 'u' : 'x')), 0 };
            if (spec == 'x') conv[0] = 'x';
            char f2[64];
            snprintf(f2, sizeof(f2), "%%%s%s%s%s*ll%s",
                     left ? "-" : "", alt ? "#" : "", zero ? "0" : "",
                     "", conv);
            clen = snprintf(chunk, sizeof(chunk), f2, (int)width, uv);
        } else if (spec == 'c') {
            char ch = (char)(uint8_t)av;
            if (left) {
                clen = snprintf(chunk, sizeof(chunk), "%-*c", (int)width, ch);
            } else {
                clen = snprintf(chunk, sizeof(chunk), "%*c", (int)width, ch);
            }
        } else if (spec == 's') {
            const char* s = av ? (const char*)pr_win32_ptr(va->ctx, av, 1) : NULL;
            if (!s && av) return -1;
            if (!s) s = "(null)";
            size_t sl = strlen(s);
            if (prec >= 0 && (size_t)prec < sl) sl = (size_t)prec;
            if (!left && (size_t)width > sl) {
                for (long k = 0; k < width - (long)sl; k++) PUT(" ", 1);
            }
            PUT(s, sl);
            if (left && (size_t)width > sl) {
                for (long k = 0; k < width - (long)sl; k++) PUT(" ", 1);
            }
            continue;
        } else if (spec == 'p') {
            clen = snprintf(chunk, sizeof(chunk), "%016llX", (unsigned long long)av);
        } else if (spec == 'f' || spec == 'F') {
            /* double via slot (MS x64: variádicos float também nos inteiros) */
            double dv;
            memcpy(&dv, &av, sizeof dv);
            char f2[64];
            snprintf(f2, sizeof(f2), "%%%s%s%s%s*.*f",
                     left ? "-" : "", plus ? "+" : "", space ? " " : "",
                     zero ? "0" : "");
            clen = snprintf(chunk, sizeof(chunk), f2, (int)width,
                            prec < 0 ? 6 : (int)prec, dv);
        } else {
            diagf(va->ctx, "formato %%%c nao suportado no stdio do msvcrt", spec);
            return -3;
        }
        if (clen < 0) return -2;
        PUT(chunk, (size_t)clen);
    }
#undef PUT
    dst[out] = 0;
    return (int)out;
}

static uint64_t f_WriteFile(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    uint64_t handle = a[0];
    if (w32_is_file_handle(handle)) {
        int k = w32_file_idx(handle);
        if (k < 0 || !ctx->files[k]) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
        const void* src = pr_win32_ptr(ctx, a[1], a[2] ? (size_t)a[2] : 1);
        if (a[2] && !src) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
        size_t put = a[2] ? fwrite(src, 1, (size_t)a[2], ctx->files[k]) : 0;
        fflush(ctx->files[k]);
        if (n >= 4 && a[3]) {
            /* ABI Win32: lpNumberOfBytesWritten = LPDWORD (4 bytes) */
            uint32_t* written = (uint32_t*)pr_win32_ptr(ctx, a[3], sizeof(uint32_t));
            if (!written) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
            *written = (uint32_t)put;
        }
        return 1;
    }
    if (handle != PR_WIN32_H_STDOUT && handle != PR_WIN32_H_STDERR) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    size_t len = (size_t)a[2];
    const char* buf = (const char*)pr_win32_ptr(ctx, a[1], len ? len : 1);
    if (len && !buf) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    /* Comportamento real: anexa à captura de console do processo. */
    if (len) {
        if (ctx->out_len + len + 1 > ctx->out_cap) {
            size_t cap = ctx->out_cap ? ctx->out_cap * 2 : 256;
            while (cap < ctx->out_len + len + 1) cap *= 2;
            char* nb = (char*)realloc(ctx->out_buf, cap);
            if (!nb) { *st = PR_ERR_NOMEM; return 0; }
            ctx->out_buf = nb;
            ctx->out_cap = cap;
        }
        memcpy(ctx->out_buf + ctx->out_len, buf, len);
        ctx->out_len += len;
        ctx->out_buf[ctx->out_len] = 0;
        w32_log(ctx, PR_LOG_DEBUG, "console(%s): %.*s",
                handle == PR_WIN32_H_STDERR ? "stderr" : "stdout",
                (int)(len > 120 ? 120 : len), buf);
    }
    /* ABI Win32: lpNumberOfBytesWritten = LPDWORD (4 bytes) */
    uint32_t* written = n >= 5 ? (uint32_t*)pr_win32_ptr(ctx, a[3], sizeof(uint32_t)) : NULL;
    if (a[3] != 0 && !written) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if (written) *written = (uint32_t)len;
    return 1;
}

static int w32_is_file_handle(uint64_t h);
static int w32_file_idx(uint64_t h);

static uint64_t f_ReadFile(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    if (w32_is_file_handle(a[0])) {
        int k = w32_file_idx(a[0]);
        if (k < 0 || !ctx->files[k]) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
        void* dst = pr_win32_ptr(ctx, a[1], a[2] ? (size_t)a[2] : 1);
        if (a[2] && !dst) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
        size_t got = a[2] ? fread(dst, 1, (size_t)a[2], ctx->files[k]) : 0;
        if (n >= 4 && a[3]) {
            /* ABI Win32: lpNumberOfBytesRead = LPDWORD (4 bytes) */
            uint32_t* written = (uint32_t*)pr_win32_ptr(ctx, a[3], sizeof(uint32_t));
            if (!written) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
            *written = (uint32_t)got;
        }
        return 1;
    }
    if (a[0] != PR_WIN32_H_STDIN) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    /* Comportamento real: stdin sem dados → EOF (TRUE com 0 bytes). */
    /* ABI Win32: lpNumberOfBytesRead = LPDWORD (4 bytes) */
    uint32_t* written = (uint32_t*)pr_win32_ptr(ctx, a[3], sizeof(uint32_t));
    if (a[3] != 0 && !written) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if (written) *written = 0;
    return 1;
}

static uint64_t f_ReadFileEx(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* G75: ReadFileEx — E/S assíncrona mínima com OVERLAPPED + completion routine.
     * Contrato Windows (x64): BOOL ReadFileEx(HANDLE hFile, LPVOID lpBuffer,
     *   DWORD nNumberOfBytesToRead, LPOVERLAPPED lpOverlapped,
     *   LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine);
     * OVERLAPPED x64 = 32 bytes: Internal(0), InternalHigh(8), Offset(16), OffsetHigh(20),
     *   Pointer(16, union), hEvent(24). Usamos Offset/OffsetHigh para seek.
     * Implementação Portico: leitura síncrona imediata + callback via guest_call
     * (mesma infraestrutura de SetTimer/TIMERPROC). Sem guest_call = leitura feita
     * sem callback (log WARN), nunca sucesso falso. */
    *st = PR_OK;
    if (n < 5) { *st = PR_ERR_INVALID; return 0; }
    uint64_t hFile = a[0];
    uint64_t lpBuffer = a[1];
    uint64_t toRead = a[2];
    uint64_t lpOverlapped = a[3];
    uint64_t lpCompletion = a[4];

    if (lpOverlapped == 0) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    uint8_t* ov = (uint8_t*)pr_win32_ptr(ctx, lpOverlapped, 32);
    if (!ov) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    void* dst = NULL;
    if (toRead) {
        dst = pr_win32_ptr(ctx, lpBuffer, (size_t)toRead);
        if (!dst) {
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return 0;
        }
    } else {
        if (lpBuffer) {
            /* Windows permite buffer NULL com 0 bytes; validamos se não-NULL */
            dst = pr_win32_ptr(ctx, lpBuffer, 1);
            if (!dst) {
                ctx->last_error = W32_ERROR_INVALID_PARAMETER;
                return 0;
            }
        }
    }

    size_t got = 0;
    if (w32_is_file_handle(hFile)) {
        int k = w32_file_idx(hFile);
        if (k < 0 || !ctx->files[k]) {
            ctx->last_error = W32_ERROR_INVALID_HANDLE;
            return 0;
        }
        FILE* f = ctx->files[k];
        uint32_t offLow = 0, offHigh = 0;
        memcpy(&offLow, ov + 16, 4);
        memcpy(&offHigh, ov + 20, 4);
        uint64_t fileOff = ((uint64_t)offHigh << 32) | (uint64_t)offLow;
        /* Seek para offset do OVERLAPPED (Portico: arquivos sempre seekable no VFS).
         * Falha de seek = mantém posição atual (comportamento honesto, não inventa). */
        if (fseek(f, (long)fileOff, SEEK_SET) != 0) {
            /* Para offsets > LONG_MAX, tenta com fseeko se disponível; fallback = atual */
            /* Não falha a API por causa de seek — lê da posição atual */
        }
        if (toRead && dst) {
            got = fread(dst, 1, (size_t)toRead, f);
        }
        uint64_t internal = 0; /* STATUS_SUCCESS */
        uint64_t internalHigh = (uint64_t)got;
        memcpy(ov, &internal, 8);
        memcpy(ov + 8, &internalHigh, 8);
    } else if (hFile == PR_WIN32_H_STDIN) {
        got = 0;
        uint64_t internal = 0;
        uint64_t internalHigh = 0;
        memcpy(ov, &internal, 8);
        memcpy(ov + 8, &internalHigh, 8);
    } else {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }

    if (lpCompletion) {
        if (ctx->guest_call) {
            uint64_t args[4] = { 0, (uint64_t)got, lpOverlapped, 0 };
            uint64_t ret = 0;
            ctx->guest_call(ctx->guest_ud, lpCompletion, args, &ret);
        } else {
            w32_log(ctx, PR_LOG_WARN,
                    "[WIN32] ReadFileEx: completion %016llX sem reentrada — %zu bytes lidos sem callback",
                    (unsigned long long)lpCompletion, got);
        }
    }

    ctx->last_error = 0;
    return 1;
}

static uint64_t f_SetLastError(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    ctx->last_error = (uint32_t)a[0];
    return 0;
}

static uint64_t f_GetLastError(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)a; (void)n; *st = PR_OK;
    return ctx->last_error;
}


static uint64_t f_lstrlenA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1 || a[0] == 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    /* valida região com varredura limitada */
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t len = 0;
    while (pr_win32_ptr(ctx, a[0] + len, 1) && p[len]) len++;
    return len;
}

static uint64_t f_lstrcpyA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    const char* src = (const char*)pr_win32_ptr(ctx, a[1], 1);
    if (!src) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t len = 0;
    while (pr_win32_ptr(ctx, a[1] + len, 1) && src[len]) len++;
    char* dst = (char*)pr_win32_ptr(ctx, a[0], len + 1);
    if (!dst) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    memcpy(dst, src, len + 1);
    return a[0];
}

static uint64_t f_lstrcpynA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3 || a[2] == 0) { *st = PR_ERR_INVALID; return 0; }
    size_t cap = (size_t)a[2];
    const char* src = (const char*)pr_win32_ptr(ctx, a[1], 1);
    char* dst = (char*)pr_win32_ptr(ctx, a[0], cap);
    if (!src || !dst) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t i = 0;
    while (i + 1 < cap && pr_win32_ptr(ctx, a[1] + i, 1) && src[i]) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
    return a[0];
}

static uint64_t f_lstrcmpA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    const char* q = (const char*)pr_win32_ptr(ctx, a[1], 1);
    if (!p || !q) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t i = 0;
    for (;;) {
        if (!pr_win32_ptr(ctx, a[0] + i, 1) || !pr_win32_ptr(ctx, a[1] + i, 1)) {
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return 0;
        }
        unsigned char c1 = (unsigned char)p[i];
        unsigned char c2 = (unsigned char)q[i];
        if (c1 != c2) return (uint64_t)(int64_t)((int)c1 - (int)c2);
        if (c1 == 0) return 0;
        i++;
    }
}

/* ---- kernel32: sincronização/TLS/memória p/ CRT real (MinGW) ---- */

static uint64_t f_InitializeCriticalSection(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint64_t* cs = (uint64_t*)pr_win32_ptr(ctx, a[0], 48);
    if (!cs) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    /* CRITICAL_SECTION x64 (48B): zeroed = não tomada; comportamento real em
     * processo single-thread (Enter/Leave nunca bloqueiam de verdade). */
    for (int i = 0; i < 6; i++) cs[i] = 0;
    return 0;   /* VOID */
}

static uint64_t f_EnterCriticalSection(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint64_t* cs = (uint64_t*)pr_win32_ptr(ctx, a[0], 48);
    if (!cs) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    cs[2]++;            /* RecursionCount */
    cs[3] = 1;          /* OwningThread = única thread */
    return 0;
}

static uint64_t f_LeaveCriticalSection(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint64_t* cs = (uint64_t*)pr_win32_ptr(ctx, a[0], 48);
    if (!cs) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if (cs[2] > 0) cs[2]--;
    if (cs[2] == 0) cs[3] = 0;
    return 0;
}

static uint64_t f_DeleteCriticalSection(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint64_t* cs = (uint64_t*)pr_win32_ptr(ctx, a[0], 48);
    if (!cs) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    for (int i = 0; i < 6; i++) cs[i] = 0;
    return 0;
}

static uint64_t f_SetUnhandledExceptionFilter(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    uint64_t old = ctx->uef_filter;
    ctx->uef_filter = n >= 1 ? a[0] : 0;
    return old;
}

static uint64_t f_Sleep(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    struct timespec ts;
    ts.tv_sec = (time_t)(a[0] / 1000);
    ts.tv_nsec = (long)(a[0] % 1000) * 1000000L;
    nanosleep(&ts, NULL);
    return 0;
}

static uint64_t f_TlsGetValue(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint64_t idx = a[0];
    if (idx >= 64) { ctx->last_error = 6; return 0; }   /* ERROR_INVALID_HANDLE */
    ctx->last_error = 0;
    return ctx->tls_slots[idx];
}

static uint64_t f_VirtualQuery(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    if (!ctx->vm) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    uint8_t* mbi = (uint8_t*)pr_win32_ptr(ctx, a[1], 48);   /* MEMORY_BASIC_INFORMATION x64 */
    if (!mbi || a[2] < 48) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    uint32_t addr = (uint32_t)a[0];
    /* G40: MBI x64 = layout real do winnt.h (struct compilada pelo MinGW x64):
     * BaseAddress@0(8) AllocationBase@8(8) AllocationProtect@16(4) [pad@20]
     * RegionSize@24(8) State@32(4) Protect@36(4) Type@40(4) [pad@44] = 48 bytes.
     * Antes os campos saíam como 6x uint64_t sequenciais (offsets deslocados). */
    if (!pr_vm_translate(ctx->vm, addr, 1, PR_VM_PROT_R)) {
        /* região livre: acha o próximo mapeado para dar o RegionSize real */
        uint32_t end = addr;
        while (end < 0x01000000u && !pr_vm_translate(ctx->vm, end, 1, PR_VM_PROT_R)) end += 0x1000;
        uint64_t b = addr & ~0xFFFu;
        uint64_t rs = (end < 0x01000000u ? end : 0x01000000u) - (addr & ~0xFFFu);
        uint32_t state = 0x10000;   /* MEM_FREE */
        memset(mbi, 0, 48);
        memcpy(mbi + 0, &b, 8);
        memcpy(mbi + 24, &rs, 8);
        memcpy(mbi + 32, &state, 4);
        return 48;
    }
    uint32_t base = addr & ~0xFFFu;
    uint32_t end = base + 0x1000;
    while (end < 0x01000000u && pr_vm_translate(ctx->vm, end, 1, PR_VM_PROT_R)) end += 0x1000;
    int prot = 0;
    {
        pr_vm_region regs[PR_VM_MAX_REGIONS];
        size_t nr = pr_vm_regions(ctx->vm, regs, PR_VM_MAX_REGIONS);
        for (size_t i = 0; i < nr; i++) {
            if (regs[i].base <= base && base < regs[i].base + regs[i].size) {
                prot = regs[i].prot;
                break;
            }
        }
    }
    uint32_t pagep = prot_to_page(prot);   /* Protect/AllocationProtect reais */
    uint64_t b = base, ab = base, rs = end - base;
    uint32_t allocp = pagep, state = 0x1000 /* MEM_COMMIT */,
             prot32 = pagep, type = 0x20000 /* MEM_PRIVATE */;
    memset(mbi, 0, 48);
    memcpy(mbi + 0, &b, 8);
    memcpy(mbi + 8, &ab, 8);
    memcpy(mbi + 16, &allocp, 4);
    memcpy(mbi + 24, &rs, 8);
    memcpy(mbi + 32, &state, 4);
    memcpy(mbi + 36, &prot32, 4);
    memcpy(mbi + 40, &type, 4);
    return 48;
}

/* ---- msvcrt.dll: fatia real do CRT do MinGW ---- */

static uint64_t f_msvcrt_memcpy(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    size_t sz = (size_t)a[2];
    if (sz == 0) return a[0];
    void* dst = pr_win32_ptr(ctx, a[0], sz);
    const void* src = (const void*)pr_win32_ptr(ctx, a[1], sz);
    if (!dst || !src) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    memmove(dst, src, sz);
    return a[0];
}

static uint64_t f_msvcrt_strlen(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    const char* s = (const char*)pr_win32_ptr(ctx, a[0], 1);
    if (!s) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t i = 0;
    while (pr_win32_ptr(ctx, a[0] + i, 1) && s[i]) i++;
    return i;
}

static uint64_t f_msvcrt_strncmp(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    const char* s1 = (const char*)pr_win32_ptr(ctx, a[0], 1);
    const char* s2 = (const char*)pr_win32_ptr(ctx, a[1], 1);
    if (!s1 || !s2) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t lim = (size_t)a[2], i = 0;
    for (; i < lim; i++) {
        int c1 = pr_win32_ptr(ctx, a[0] + i, 1) ? (unsigned char)s1[i] : 0;
        int c2 = pr_win32_ptr(ctx, a[1] + i, 1) ? (unsigned char)s2[i] : 0;
        if (c1 != c2 || c1 == 0) return (uint64_t)(int64_t)(c1 - c2);
    }
    return 0;
}

static uint64_t heap_alloc_impl(pr_win32_ctx* ctx, uint64_t size, pr_status* st) {
    uint64_t h[3] = { PR_WIN32_H_HEAP, 0, size ? size : 1 };
    return f_HeapAlloc(ctx, h, 3, st);
}

static uint64_t f_msvcrt_malloc(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)a; (void)n;
    return heap_alloc_impl(ctx, n >= 1 ? a[0] : 1, st);
}

static uint64_t f_msvcrt_calloc(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    return heap_alloc_impl(ctx, a[0] * a[1], st);   /* já vem zerado */
}

static uint64_t f_msvcrt_free(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    uint64_t h[3] = { PR_WIN32_H_HEAP, 0, n >= 1 ? a[0] : 0 };
    if (!h[2]) { *st = PR_OK; return 0; }
    return f_HeapFree(ctx, h, 3, st);
}

static uint64_t f_msvcrt_realloc(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    uint64_t ptr = n >= 1 ? a[0] : 0;
    uint64_t size = n >= 2 ? a[1] : 0;
    if (!ptr) return heap_alloc_impl(ctx, size, st);          /* realloc(NULL,n) */
    if (!size) { f_msvcrt_free(ctx, &ptr, 1, st); return 0; } /* realloc(p,0) = free */
    uint64_t hs[3] = { PR_WIN32_H_HEAP, 0, ptr };
    uint64_t old_sz = f_HeapSize(ctx, hs, 3, st);
    if (*st != PR_OK) return 0;
    uint64_t np = heap_alloc_impl(ctx, size, st);
    if (*st != PR_OK || np == 0) return 0;
    uint64_t copy = old_sz < size ? old_sz : size;
    uint8_t* src = pr_win32_ptr(ctx, ptr, (size_t)copy);
    uint8_t* dst = pr_win32_ptr(ctx, np, (size_t)copy);
    if (src && dst) memmove(dst, src, (size_t)copy);
    f_msvcrt_free(ctx, &ptr, 1, st);
    *st = PR_OK;
    return np;
}

/* _lock/_unlock: trancas de runtime do msvcrt (contadores por indice).
 * Estado por processo (uma instancia de runtime por convidado — documentado). */
static uint64_t s_crt_locks[32];
static uint64_t f_msvcrt_lock(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx;
    s_crt_locks[(n >= 1 ? (size_t)a[0] : 0) & 31]++;
    *st = PR_OK;
    return 0;
}
static uint64_t f_msvcrt_unlock(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx;
    size_t i = (n >= 1 ? (size_t)a[0] : 0) & 31;
    if (s_crt_locks[i] > 0) s_crt_locks[i]--;
    *st = PR_OK;
    return 0;
}

static uint64_t msvcrt_do_exit(pr_win32_ctx* ctx, uint32_t code) {
    /* saída do msvcrt: flush dos streams antes de tudo (comportamento real) */
    pr_win32_stdio_flush(ctx);
    /* exit/_amsg_exit/abort: roda handlers _onexit reais e encerra */
    if (ctx->nonexit && ctx->guest_call) {
        for (size_t i = ctx->nonexit; i > 0; i--) {
            uint64_t ret = 0;
            ctx->guest_call(ctx->guest_ud, ctx->onexit_fns[i - 1], NULL, &ret);
        }
    } else if (ctx->nonexit) {
        w32_log(ctx, PR_LOG_WARN,
                "msvcrt exit: %u handler(s) _onexit sem reentrada — não executados",
                (unsigned)ctx->nonexit);
    }
    ctx->exit_code = code;
    ctx->halted = 1;
    w32_log(ctx, PR_LOG_INFO, "exit(%u) — processo sinalizado como encerrado", code);
    return 0;
}

static uint64_t f_msvcrt_exit(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    return msvcrt_do_exit(ctx, n >= 1 ? (uint32_t)a[0] : 0);
}

static uint64_t f_msvcrt_abort(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)a; (void)n; *st = PR_OK;
    return msvcrt_do_exit(ctx, 3);
}

static uint64_t f_msvcrt_amsg_exit(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    return msvcrt_do_exit(ctx, n >= 1 ? (uint32_t)a[0] : 255);
}

static uint64_t f_msvcrt_cexit(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)a; (void)n; *st = PR_OK;
    /* _cexit: roda atexit SEM encerrar (comportamento real) */
    if (ctx->guest_call) {
        for (size_t i = ctx->nonexit; i > 0; i--) {
            uint64_t ret = 0;
            ctx->guest_call(ctx->guest_ud, ctx->onexit_fns[i - 1], NULL, &ret);
        }
    }
    ctx->nonexit = 0;
    return 0;
}

static uint64_t f_msvcrt_onexit(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (ctx->nonexit >= 32) return 0;   /* cheio: NULL = falha (real) */
    ctx->onexit_fns[ctx->nonexit++] = a[0];
    return a[0];   /* _onexit devolve o próprio ponteiro */
}

static uint64_t f_msvcrt_initterm(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    /* _initterm(void** start, void** end): chama cada ctor (ABI x64, 0 args) */
    uint64_t p = a[0], end = a[1];
    while (p + 8 <= end) {
        const uint64_t* slot = (const uint64_t*)pr_win32_ptr(ctx, p, 8);
        if (!slot) { *st = PR_ERR_FAULT; return 0; }
        uint64_t fn = *slot;
        if (fn) {
            if (!ctx->guest_call) {
                w32_log(ctx, PR_LOG_ERROR,
                        "_initterm: ctor em 0x%llX sem reentrada — não executado",
                        (unsigned long long)fn);
                *st = PR_ERR_UNSUPPORTED;
                return 0;
            }
            uint64_t ret = 0;
            ctx->guest_call(ctx->guest_ud, fn, NULL, &ret);
        }
        p += 8;
    }
    return 0;
}

static uint64_t f_msvcrt_getmainargs(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* __getmainargs(int* argc, char*** argv, char*** env, int doinit, _startupinfo*) */
    *st = PR_OK;
    if (n < 5) { *st = PR_ERR_INVALID; return 0; }
    int32_t* pargc = (int32_t*)pr_win32_ptr(ctx, a[0], 4);
    uint64_t* pargv = (uint64_t*)pr_win32_ptr(ctx, a[1], 8);
    uint64_t* penv = (uint64_t*)pr_win32_ptr(ctx, a[2], 8);
    if (!pargc || !pargv || !penv) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    /* parse honesto da cmdline (argv[0] + args separados por espaços) */
    const char* cmd = ctx->cmdline[0] ? ctx->cmdline : "portico";
    char words[8][64];
    int argc = 0;
    for (const char* q = cmd; *q && argc < 8; ) {
        while (*q == ' ') q++;
        if (!*q) break;
        size_t wl = 0;
        while (*q && *q != ' ' && wl < 63) words[argc][wl++] = *q++;
        words[argc][wl] = 0;
        argc++;
    }
    /* argv: vetor (argc+1) ponteiros + strings numa única área do heap */
    size_t str_bytes = 0;
    for (int i = 0; i < argc; i++) str_bytes += strlen(words[i]) + 1;
    uint64_t area = heap_alloc_impl(ctx, (size_t)(argc + 1) * 8u + str_bytes + 16u, st);
    if (!area) return 0;
    uint64_t strs = area + (uint64_t)(argc + 1) * 8u;
    uint64_t* vec = (uint64_t*)pr_win32_ptr(ctx, area, (size_t)(argc + 1) * 8u);
    if (!vec) { *st = PR_ERR_FAULT; return 0; }
    uint64_t off = 0;
    for (int i = 0; i < argc; i++) {
        size_t wl = strlen(words[i]) + 1;
        char* dst = (char*)pr_win32_ptr(ctx, strs + off, wl);
        if (!dst) { *st = PR_ERR_FAULT; return 0; }
        memcpy(dst, words[i], wl);
        vec[i] = strs + off;
        off += wl;
    }
    vec[argc] = 0;
    *pargc = argc;
    *pargv = area;
    uint64_t env_area = heap_alloc_impl(ctx, 8, st);
    if (!env_area) return 0;
    uint64_t* ev = (uint64_t*)pr_win32_ptr(ctx, env_area, 8);
    if (ev) ev[0] = 0;
    *penv = env_area;
    /* _startupinfo: { newmode } — devolve newmode padrão */
    if (a[4]) {
        int32_t* si = (int32_t*)pr_win32_ptr(ctx, a[4], 4);
        if (si) si[0] = 0;
    }
    return 0;
}

static uint64_t f_msvcrt_set_app_type(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    ctx->app_type = n >= 1 ? a[0] : 0;
    return 0;
}

static uint64_t f_msvcrt_setusermatherr(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    ctx->matherr_fn = n >= 1 ? a[0] : 0;
    return 0;
}

static uint64_t f_msvcrt_iob_func(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* __iob_func(): vetor de 3 FILE (stdin/stdout/stderr) em memória do
     * convidado. FILE do msvcrt x64 = 48 bytes; o CRT faz iob[0..2] por
     * aritmética de ponteiros — devolvemos a BASE do vetor (comportamento
     * real). Blocos do FILE são zerados; fwrite/fprintf (stubs honestos)
     * param com EXECUTION STOPPED se chamados nesta etapa. */
    (void)a; (void)n;
    *st = PR_OK;
    if (!ctx->vm) { *st = PR_ERR_STATE; return 0; }
    uint64_t cell = ctx->tls_slots[63];   /* convenção interna: [63] = iob */
    if (!cell) {
        cell = heap_alloc_impl(ctx, 3 * 48, st);
        if (!cell) return 0;
        ctx->tls_slots[63] = cell;
    }
    ctx->iob_base = cell;
    /* inicializa os 3 FILE reais (flag/_file); buffering é gerido pelo
     * runtime (stdout por linha, stderr direto) */
    for (int i = 0; i < 3; i++) {
        uint8_t* f = pr_win32_ptr(ctx, cell + (uint64_t)i * W32_IOB_STRIDE, 48);
        if (!f) break;
        memset(f, 0, 48);
        *(uint32_t*)(f + 24) = i == 0 ? W32_FOPEN_READ : W32_FOPEN_WRITE; /* _flag */
        *(uint32_t*)(f + 28) = (uint32_t)i;                              /* _file */
        *(int32_t*)(f + 36) = i == 2 ? 0 : 2048;                         /* _bufsiz */
    }
    return cell;
}

/* fprintf/printf: formatam e emitem em stdout/stderr (identidade pelo FILE*).
 * Suporte real: %d %i %u %x %X %o %c %s %p %% %f, flags/width/prec, * e h..ll.
 * Especificador/FILE* fora do suporte = PR_ERR_UNSUPPORTED (EXECUTION STOPPED
 * com o motivo em pr_win32_last_detail) — nunca sucesso falso. */
static uint64_t stdio_vformat(pr_win32_ctx* ctx, uint64_t file, int def_stream,
                              uint64_t gfmt, w32_va* va, pr_status* st) {
    *st = PR_OK;
    ctx->diag_detail[0] = 0;
    int idx = def_stream;
    if (file) {
        idx = stdio_stream_of(ctx, file);
        if (idx < 0 || idx == 0) {
            diagf(ctx, "FILE* %016llX nao e stdout/stderr do __iob_func (stdin nao escreve)",
                  (unsigned long long)file);
            *st = PR_ERR_UNSUPPORTED;
            return 0;
        }
    }
    const char* fmt = gfmt ? (const char*)pr_win32_ptr(ctx, gfmt, 1) : NULL;
    if (!fmt) { diagf(ctx, "formato nulo ou fora do espaco"); *st = PR_ERR_INVALID; return 0; }
    size_t cap = 4096;
    char* buf = (char*)malloc(cap);
    if (!buf) { *st = PR_ERR_NOMEM; return 0; }
    int len = w32_format(buf, cap, fmt, va);
    if (len == -1) {
        if (!ctx->diag_detail[0])
            diagf(ctx, "argumentos variadic insuficientes para o formato");
        free(buf);
        *st = PR_ERR_UNSUPPORTED;
        return 0;
    }
    if (len == -3) { free(buf); *st = PR_ERR_UNSUPPORTED; return 0; }
    if (len == -2) {
        diagf(ctx, "formato excede buffer interno (4096) nesta etapa");
        free(buf);
        *st = PR_ERR_UNSUPPORTED;
        return 0;
    }
    stdio_emit(ctx, idx == 2 ? 1 : 0, buf, (size_t)len);
    free(buf);
    return (uint64_t)len;
}

static uint64_t f_msvcrt_fprintf(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* fprintf(FILE*, fmt, ...) — variádicos em a[2..] (até 12 coletados) */
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    w32_va va = { ctx, a + 2, n > 2 ? n - 2 : 0, 0, 0 };
    return stdio_vformat(ctx, a[0], -1, a[1], &va, st);
}

static uint64_t f_msvcrt_printf(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* printf(fmt, ...) — implícito stdout */
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    w32_va va = { ctx, a + 1, n > 1 ? n - 1 : 0, 0, 0 };
    return stdio_vformat(ctx, 0, 0, a[0], &va, st);
}

static uint64_t f_msvcrt_vfprintf(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* vfprintf(FILE*, fmt, va_list) — variádicos lidos do va_list do convidado */
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    w32_va va = { ctx, NULL, 0, a[2], 0 };
    return stdio_vformat(ctx, a[0], -1, a[1], &va, st);
}

static uint64_t f_msvcrt_fwrite(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* fwrite(ptr, size, nmemb, FILE*) — escrita real; retorna nmemb escrito */
    *st = PR_OK;
    ctx->diag_detail[0] = 0;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    int idx = stdio_stream_of(ctx, a[3]);
    if (idx < 0 || idx == 0) {
        diagf(ctx, "fwrite: FILE* %016llX nao e stdout/stderr do __iob_func",
              (unsigned long long)a[3]);
        *st = PR_ERR_UNSUPPORTED;
        return 0;
    }
    uint64_t total = a[1] * a[2];
    if (!total) return 0;
    const char* p = pr_win32_ptr(ctx, a[0], (size_t)total);
    if (!p) { diagf(ctx, "fwrite: origem fora do espaco do convidado"); *st = PR_ERR_INVALID; return 0; }
    stdio_emit(ctx, idx == 2 ? 1 : 0, p, (size_t)total);
    return a[2];
}

/* ---- apoio ao formatter ANSI do MinGW (imports reais exigidos) ---- */

static uint64_t f_msvcrt_puts(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* puts(const char *str): escreve str + '\n' no stdout pela MESMA infra de
     * printf/fwrite (stdio_emit + captura de console/log) — sem segunda infra.
     * Contrato CRT documentado (G73 §6): nao-negativo em sucesso (adotado 0 —
     * o valor nao e especificado pelo CRT; consistente entre chamadas) e
     * EOF (-1) em erro. Ponteiro nulo/fora do espaco = EOF SEM crash (contrato
     * Portico — Windows/MSVCRT acessaria memoria invalida). Nao toca LastError. */
    *st = PR_OK;
    ctx->diag_detail[0] = 0;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (!a[0]) {
        diagf(ctx, "puts: string nula");
        return (uint64_t)-1;                     /* EOF */
    }
    /* localizar o terminador NUL byte a byte no espaco do convidado
     * (limite 0x4000 = mesma convencao de f_msvcrt_wcslen) */
    size_t len = 0;
    for (;;) {
        const char* q = (const char*)pr_win32_ptr(ctx, a[0] + len, 1);
        if (!q) {
            diagf(ctx, "puts: string fora do espaco do convidado");
            return (uint64_t)-1;                 /* EOF */
        }
        if (*q == 0) break;
        len++;
        if (len >= 0x4000) {
            diagf(ctx, "puts: string sem terminador NUL (limite 0x4000)");
            return (uint64_t)-1;                 /* EOF */
        }
    }
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], len ? len : 1);
    if (!p) return (uint64_t)-1;                 /* EOF */
    /* um unico emit (conteudo + '\n') = uma linha de console completa */
    char* buf = (char*)malloc(len + 1);
    if (!buf) { *st = PR_ERR_NOMEM; return 0; }
    memcpy(buf, p, len);
    buf[len] = '\n';
    stdio_emit(ctx, 0, buf, len + 1);            /* 0 = stdout */
    free(buf);
    return 0;                                    /* nao-negativo (contrato: 0) */
}

static uint64_t f_memset(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    void* p = pr_win32_ptr(ctx, a[0], (size_t)(a[2] ? a[2] : 1));
    if (!p) { *st = PR_ERR_FAULT; return 0; }
    memset(p, (int)(uint8_t)a[1], (size_t)a[2]);
    return a[0];
}

static uint64_t f_msvcrt_wcslen(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    const uint16_t* w = pr_win32_ptr(ctx, a[0], 2);
    if (!w) { *st = PR_ERR_FAULT; return 0; }
    size_t i = 0;
    while (i < 0x4000) {
        const uint16_t* wp = pr_win32_ptr(ctx, a[0] + i * 2, 2);
        if (!wp || *wp == 0) break;
        i++;
    }
    return i;
}

static uint64_t f_msvcrt_fputc(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    ctx->diag_detail[0] = 0;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    int idx = stdio_stream_of(ctx, a[1]);
    if (idx < 0 || idx == 0) {
        diagf(ctx, "fputc: FILE* %016llX nao e stdout/stderr do __iob_func",
              (unsigned long long)a[1]);
        *st = PR_ERR_UNSUPPORTED;
        return 0;
    }
    char c = (char)(uint8_t)a[0];
    stdio_emit(ctx, idx == 2 ? 1 : 0, &c, 1);
    return (uint64_t)(uint8_t)c;
}

/* errno do msvcrt: _errno() devolve int* (célula por processo) */
static uint64_t f_msvcrt_errno(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)a; (void)n;
    *st = PR_OK;
    if (!ctx->vm) { *st = PR_ERR_STATE; return 0; }
    uint64_t cell = ctx->tls_slots[62];   /* convenção interna: [62] = errno */
    if (!cell) {
        cell = heap_alloc_impl(ctx, 8, st);
        if (!cell) return 0;
        void* p = pr_win32_ptr(ctx, cell, 8);
        if (p) memset(p, 0, 8);
        ctx->tls_slots[62] = cell;
    }
    return cell;
}

static uint64_t f_msvcrt_strerror(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* tabela de mensagens determinística (subconjunto do msvcrt) */
    *st = PR_OK;
    if (!ctx->vm) { *st = PR_ERR_STATE; return 0; }
    uint64_t cell = ctx->tls_slots[61];   /* convenção interna: [61] = strerror pool */
    if (!cell) {
        cell = heap_alloc_impl(ctx, 512, st);
        if (!cell) return 0;
        char* p = pr_win32_ptr(ctx, cell, 512);
        if (!p) { *st = PR_ERR_FAULT; return 0; }
        static const char* msgs[] = {
            "No error", "Operation not permitted", "No such file or directory",
            "No such process", "Interrupted function call", "Input/output error",
            "No such device or address", "Arg list too long", "Exec format error",
            "Bad file descriptor", "No child processes", "Resource temporarily unavailable",
            "Not enough memory", "Permission denied", "Bad address",
            "Resource busy", "File exists", "Improper link",
            "No such device", "Not a directory", "Is a directory",
            "Invalid argument", "Too many open files in system", "Too many open files",
            "Inappropriate I/O control operation", "File too large", "No space left on device",
            "Invalid seek", "Read-only file system", "Too many links",
            "Broken pipe", "Mathematics argument out of domain of function",
            "Mathematics result not representable", "Resource deadlock avoided",
        };
        char* w = p;
        size_t off = 0;
        int idx = n >= 1 ? (int)(int32_t)a[0] : 0;
        for (size_t i = 0; i < sizeof msgs / sizeof msgs[0]; i++) {
            size_t l = strlen(msgs[i]) + 1;
            memcpy(w + off, msgs[i], l);
            off += l;
        }
        memcpy(w + 500, "Unknown error", 14);
        ctx->tls_slots[61] = cell;
        /* offset da mensagem pedida */
        uint64_t want = 0;
        if (idx >= 0 && (size_t)idx < sizeof msgs / sizeof msgs[0]) {
            want = 0;
            for (int i = 0; i < idx; i++) want += strlen(msgs[i]) + 1;
        } else {
            want = 500;
        }
        return cell + want;
    }
    char* p = pr_win32_ptr(ctx, cell, 512);
    if (!p) { *st = PR_ERR_FAULT; return 0; }
    static const char* msgs[] = {
        "No error", "Operation not permitted", "No such file or directory",
        "No such process", "Interrupted function call", "Input/output error",
        "No such device or address", "Arg list too long", "Exec format error",
        "Bad file descriptor", "No child processes", "Resource temporarily unavailable",
        "Not enough memory", "Permission denied", "Bad address",
        "Resource busy", "File exists", "Improper link",
        "No such device", "Not a directory", "Is a directory",
        "Invalid argument", "Too many open files in system", "Too many open files",
        "Inappropriate I/O control operation", "File too large", "No space left on device",
        "Invalid seek", "Read-only file system", "Too many links",
        "Broken pipe", "Mathematics argument out of domain of function",
        "Mathematics result not representable", "Resource deadlock avoided",
    };
    int idx = n >= 1 ? (int)(int32_t)a[0] : 0;
    uint64_t want = 0;
    if (idx >= 0 && (size_t)idx < sizeof msgs / sizeof msgs[0]) {
        for (int i = 0; i < idx; i++) want += strlen(msgs[i]) + 1;
    } else {
        want = 500;
    }
    return cell + want;
}

/* locale "C" fixa e determinística: lconv com decimal_point="." etc. */
static uint64_t f_msvcrt_localeconv(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)a; (void)n;
    *st = PR_OK;
    if (!ctx->vm) { *st = PR_ERR_STATE; return 0; }
    uint64_t cell = ctx->tls_slots[60];   /* convenção interna: [60] = lconv */
    if (!cell) {
        /* 10 ponteiros (80B) + 8 chars + 8B strings internas */
        cell = heap_alloc_impl(ctx, 160, st);
        if (!cell) return 0;
        uint8_t* p = pr_win32_ptr(ctx, cell, 160);
        if (!p) { *st = PR_ERR_FAULT; return 0; }
        memset(p, 0, 160);
        uint64_t s_dot   = cell + 128;  memcpy(p + 128, ".", 2);
        uint64_t s_empty = cell + 130;  memcpy(p + 130, "", 1);
        uint64_t s_c = 0;
        /* ordem de campos do lconv (msvcrt x64) */
        uint64_t* slots = (uint64_t*)p;
        slots[0] = s_dot;      /* decimal_point */
        slots[1] = s_empty;    /* thousands_sep */
        s_c = s_empty;
        slots[2] = s_c;        /* grouping */
        slots[3] = s_empty;    /* int_curr_symbol */
        slots[4] = s_empty;    /* currency_symbol */
        slots[5] = s_dot;      /* mon_decimal_point */
        slots[6] = s_empty;    /* mon_thousands_sep */
        slots[7] = s_empty;    /* mon_grouping */
        slots[8] = s_empty;    /* positive_sign */
        slots[9] = s_empty;    /* negative_sign */
        p[80] = 127;           /* int_frac_digits = CHAR_MAX (sem moeda) */
        p[81] = 127;           /* frac_digits */
        ctx->tls_slots[60] = cell;
    }
    return cell;
}

/* codepage fixa do ambiente: 1252 (ANSI/SBCS) — determinística, sem host */
static uint64_t f_lc_codepage(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)a; (void)n; *st = PR_OK;
    return 1252;
}
static uint64_t f_mb_cur_max(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)a; (void)n; *st = PR_OK;
    return 1;   /* SBCS (CP1252) */
}

static uint64_t f_IsDBCSLeadByteEx(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx;
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    uint32_t cp = (uint32_t)a[0];
    uint8_t b = (uint8_t)a[1];
    if (cp == 65001) return (b >= 0xC0) ? 1 : 0;   /* UTF-8 lead */
    return 0;                                       /* CP1252 é SBCS */
}

/* CP1252: Latin-1 com a tabela real de 0x80–0x9F */
static const uint16_t w32_cp1252_hi[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178,
};

static uint64_t f_MultiByteToWideChar(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    ctx->diag_detail[0] = 0;
    if (n < 6) { *st = PR_ERR_INVALID; return 0; }
    uint32_t cp = (uint32_t)a[0];
    /* a[1]=flags, a[2]=src, a[3]=srclen, a[4]=dst, a[5]=dstlen */
    int32_t slen = (int32_t)a[3];
    const char* src;
    if (slen < 0) {
        /* modo NUL-terminated (cbMultiByte = -1): varredura segura ate NUL */
        src = (const char*)pr_win32_ptr(ctx, a[2], 1);
        if (!src) { *st = PR_ERR_FAULT; return 0; }
        slen = 0;
        while (slen < 0x4000) {
            const char* q = (const char*)pr_win32_ptr(ctx, a[2] + (uint64_t)slen, 1);
            if (!q || !*q) { slen++; break; }
            slen++;
        }
    } else {
        src = (const char*)pr_win32_ptr(ctx, a[2], a[3] ? (size_t)(uint32_t)a[3] : 1);
        if (!src) { *st = PR_ERR_FAULT; return 0; }
    }
    if (cp != 0 && cp != 1252 && cp != 65001) {
        diagf(ctx, "MultiByteToWideChar: codepage %u fora do suporte (ACP/1252/UTF-8)", cp);
        *st = PR_ERR_UNSUPPORTED;
        return 0;
    }
    /* conta UTF-8 codepoints se preciso (sem parcial) */
    int32_t need = 0;
    if (cp == 65001) {
        for (int32_t i = 0; i < slen; ) {
            unsigned char c = (unsigned char)src[i];
            int adv = c < 0x80 ? 1 : c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 1;
            if (i + adv > slen) adv = 1;
            i += adv;
            need++;
        }
    } else {
        need = slen;
    }
    int32_t cap = (int32_t)(uint32_t)a[5];
    /* Consulta de tamanho: lpWideCharStr e ignorado quando cchWideChar == 0
     * (contrato Windows). Lacuna G74 demonstrada pelo hello_unicode_probe:
     * cap 0 com dst nao-NULL validava o buffer de 0 bytes e devolvia 0. */
    if (!a[4] || cap == 0) return (uint64_t)need;   /* so consulta */
    uint16_t* dst = (uint16_t*)pr_win32_ptr(ctx, a[4], (size_t)(uint32_t)a[5] * 2);
    if (!dst) { *st = PR_ERR_FAULT; return 0; }
    int32_t w = 0;
    for (int32_t i = 0; i < slen && w < cap; ) {
        unsigned char c = (unsigned char)src[i];
        uint16_t u;
        if (cp == 65001 && c >= 0xC0) {
            int adv = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
            if (i + adv <= slen) {
                uint32_t cpv = 0;
                if (adv == 2) cpv = ((c & 0x1F) << 6) | (src[i + 1] & 0x3F);
                else if (adv == 3) cpv = ((c & 0x0F) << 12) | ((src[i + 1] & 0x3F) << 6) | (src[i + 2] & 0x3F);
                else cpv = ((c & 0x07) << 18) | ((src[i + 1] & 0x3F) << 12) | ((src[i + 2] & 0x3F) << 6) | (src[i + 3] & 0x3F);
                if (cpv > 0xFFFF) { diagf(ctx, "MultiByteToWideChar: UTF-8 fora do BMP"); *st = PR_ERR_UNSUPPORTED; return 0; }
                u = (uint16_t)cpv;
                i += adv;
            } else { u = c; i += 1; }
        } else if (c >= 0x80 && c < 0xA0) {
            u = w32_cp1252_hi[c - 0x80];
            i += 1;
        } else {
            u = c;
            i += 1;
        }
        dst[w++] = u;
    }
    return (uint64_t)w;
}

static uint64_t f_WideCharToMultiByte(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    ctx->diag_detail[0] = 0;
    if (n < 6) { *st = PR_ERR_INVALID; return 0; }
    uint32_t cp = (uint32_t)a[0];
    int32_t slen = (int32_t)(uint32_t)a[3];
    /* modo NUL-terminated (cchWideChar = -1): valida 1 unidade; a varredura
     * segura ate NUL acontece abaixo (slen < 0). */
    size_t vbytes = (slen < 0) ? 2u : (a[3] ? (size_t)(uint32_t)a[3] * 2 : 2u);
    const uint16_t* src = pr_win32_ptr(ctx, a[2], vbytes);
    if (!src) { *st = PR_ERR_FAULT; return 0; }
    if (slen < 0) {
        slen = 0;
        const uint16_t* wp = src;
        while (slen < 0x4000) {
            const uint16_t* q = pr_win32_ptr(ctx, a[2] + (uint64_t)slen * 2, 2);
            if (!q || !*q) { slen++; break; }
            slen++;
            (void)wp;
        }
    }
    if (cp != 0 && cp != 1252 && cp != 65001) {
        diagf(ctx, "WideCharToMultiByte: codepage %u fora do suporte (ACP/1252/UTF-8)", cp);
        *st = PR_ERR_UNSUPPORTED;
        return 0;
    }
    char* dst = NULL;
    if (a[4]) {
        dst = pr_win32_ptr(ctx, a[4], (size_t)(uint32_t)a[5] ? (size_t)(uint32_t)a[5] : 1);
        if (!dst) { *st = PR_ERR_FAULT; return 0; }
    }
    int32_t cap = (int32_t)(uint32_t)a[5];
    if (!dst || cap == 0) return (uint64_t)slen;   /* só consulta (SBCS 1:1) */
    int32_t w = 0;
    for (int32_t i = 0; i < slen && w < cap; i++) {
        uint16_t u = src[i];
        if (u == 0) { dst[w++] = 0; break; }
        if (cp == 65001) {
            if (u < 0x80) { dst[w++] = (char)u; }
            else if (u < 0x800) {
                if (w + 2 > cap) break;
                dst[w++] = (char)(0xC0 | (u >> 6));
                dst[w++] = (char)(0x80 | (u & 0x3F));
            } else {
                if (w + 3 > cap) break;
                dst[w++] = (char)(0xE0 | (u >> 12));
                dst[w++] = (char)(0x80 | ((u >> 6) & 0x3F));
                dst[w++] = (char)(0x80 | (u & 0x3F));
            }
        } else if (u >= 0x100) {
            /* varre a tabela 1252 ao contrário */
            int found = -1;
            for (int k = 0; k < 32; k++)
                if (w32_cp1252_hi[k] == u) { found = k; break; }
            if (found < 0) { dst[w++] = '?'; }
            else dst[w++] = (char)(0x80 + found);
        } else {
            dst[w++] = (char)u;
        }
    }
    return (uint64_t)w;
}

static uint64_t f_msvcrt_fflush(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    ctx->diag_detail[0] = 0;
    if (n >= 1 && a[0]) {
        int idx = stdio_stream_of(ctx, a[0]);
        if (idx < 0 || idx == 0) {
            diagf(ctx, "fflush: FILE* %016llX nao e stdout/stderr do __iob_func",
                  (unsigned long long)a[0]);
            *st = PR_ERR_UNSUPPORTED;
            return 0;
        }
        stdio_flush(ctx, idx == 2 ? 1 : 0);
    } else {
        pr_win32_stdio_flush(ctx);   /* fflush(NULL): todos */
    }
    return 0;
}

static uint64_t f_ExitProcess(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    ctx->exit_code = n >= 1 ? (uint32_t)a[0] : 0;
    ctx->halted = 1;
    w32_log(ctx, PR_LOG_INFO, "ExitProcess(%u) — processo sinalizado como encerrado", ctx->exit_code);
    return 0;
}

static uint64_t f_TerminateProcess(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    ctx->exit_code = n >= 2 ? (uint32_t)a[1] : 0;
    ctx->halted = 1;
    w32_log(ctx, PR_LOG_INFO, "TerminateProcess(code=%u)", ctx->exit_code);
    return 1;
}

static uint64_t f_GetCurrentProcessId(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)a; (void)n; *st = PR_OK;
    return (uint64_t)getpid();
}

static uint64_t f_GetCurrentThreadId(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)a; (void)n; *st = PR_OK;
    return (uint64_t)(uintptr_t)pthread_self();
}

static uint64_t f_IsDebuggerPresent(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)a; (void)n; *st = PR_OK;
    return 0; /* real: processo não está sob depuração */
}

/* Módulos builtin da camada de compatibilidade (handles estáveis). */
#define W32_H_KERNEL32 0xB0B00001u
#define W32_H_USER32   0xB0B00002u
#define W32_H_ADVAPI32 0xB0B00003u
#define W32_H_WS2_32   0xB0B00004u
#define W32_H_GDI32    0xB0B00005u
#define W32_H_OLE32    0xB0B00006u
#define W32_H_SHELL32  0xB0B00007u

static uint64_t module_handle(const char* module) {
    if (!module) return 0;
    char buf[32];
    size_t i = 0;
    for (; module[i] && i < sizeof(buf) - 1; i++) {
        char c = module[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        buf[i] = c;
    }
    buf[i] = 0;
    if (!strcmp(buf, "kernel32.dll") || !strcmp(buf, "kernel32")) return W32_H_KERNEL32;
    if (!strcmp(buf, "user32.dll")   || !strcmp(buf, "user32"))   return W32_H_USER32;
    if (!strcmp(buf, "advapi32.dll") || !strcmp(buf, "advapi32")) return W32_H_ADVAPI32;
    if (!strcmp(buf, "ws2_32.dll")   || !strcmp(buf, "ws2_32"))   return W32_H_WS2_32;
    if (!strcmp(buf, "gdi32.dll")    || !strcmp(buf, "gdi32"))    return W32_H_GDI32;
    if (!strcmp(buf, "ole32.dll")    || !strcmp(buf, "ole32"))    return W32_H_OLE32;
    if (!strcmp(buf, "shell32.dll")  || !strcmp(buf, "shell32"))  return W32_H_SHELL32;
    if (!strcmp(buf, "msvcrt.dll")  || !strcmp(buf, "msvcrt"))  return W32_H_MSVCRT;
    return 0;
}

/* ---- FASE 7: filesystem virtual (raiz única = sandbox do app) ---- */
#define W32_FILE_BASE      0xC0000000u
#define W32_ERROR_FILE_EXISTS     80u
#define W32_INVALID_HANDLE_VALUE  0xFFFFFFFFFFFFFFFFull
#define W32_INVALID_SET_FILE_PTR  0xFFFFFFFFu
#define W32_SEEK_BEGIN 0
#define W32_SEEK_CUR   1
#define W32_SEEK_END   2

static int w32_is_file_handle(uint64_t h) {
    return (h & 0xFFFF0000ull) == W32_FILE_BASE;
}
static int w32_file_idx(uint64_t h) {
    unsigned k = (unsigned)(h & 0xFFFFu);
    return (k >= 1 && k <= 8) ? (int)(k - 1) : -1;
}

/* Normaliza path Windows para dentro de fs_root. Recusa ".." que escapam da raiz,
 * mas permite ".." interno que permanece dentro (ex: sub/../sub/file). Sandbox
 * — NUNCA lê o iOS. Retorna 1 e preenche out em sucesso; 0 = acesso negado. */
static int vfs_resolve(pr_win32_ctx* ctx, const char* win, char* out, size_t cap) {
    if (!ctx->fs_root[0]) return 0;
    char norm[192];
    size_t o = 0, i = 0;
    /* pula drive "C:" e UNC "\\" */
    if (((win[0] >= 'A' && win[0] <= 'Z') || (win[0] >= 'a' && win[0] <= 'z'))
        && win[1] == ':') i = 2;
    while (win[i] == '\\' || win[i] == '/') i++;
    for (; win[i] && o + 1 < sizeof(norm); i++) {
        char ch = win[i] == '\\' ? '/' : win[i];
        norm[o++] = ch;
    }
    norm[o] = 0;
    /* canonicalização com pilha: recusa escape acima da raiz */
    char stack[16][64];
    int depth = 0;
    char tmp[192];
    snprintf(tmp, sizeof(tmp), "%s", norm);
    char* p = tmp;
    while (*p) {
        char* slash = strchr(p, '/');
        if (slash) *slash = 0;
        if (p[0]==0 || (p[0]=='.' && p[1]==0)) {
            /* . ou vazio = ignora */
        } else if (p[0]=='.' && p[1]=='.' && p[2]==0) {
            if (depth==0) return 0; /* tenta escapar da raiz */
            depth--;
        } else {
            if (depth>=16) return 0;
            if (strlen(p) >= sizeof(stack[0])) return 0;
            snprintf(stack[depth], sizeof(stack[depth]), "%s", p);
            depth++;
        }
        if (!slash) break;
        p = slash+1;
    }
    char canon[192]="";
    size_t co=0;
    for (int d=0; d<depth; d++) {
        size_t l=strlen(stack[d]);
        if (co+l+1 >= sizeof(canon)) return 0;
        if (co>0) canon[co++]='/';
        memcpy(canon+co, stack[d], l);
        co+=l;
    }
    canon[co]=0;
    int w = snprintf(out, cap, "%s/", ctx->fs_root);
    if (w < 0 || (size_t)w >= cap) return 0;
    snprintf(out + w, cap - (size_t)w, "%s", canon[0] ? canon : "");
    return 1;
}

static uint64_t f_CreateFileA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 5) { *st = PR_ERR_INVALID; return W32_INVALID_HANDLE_VALUE; }
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return W32_INVALID_HANDLE_VALUE; }
    char win[192];
    size_t i = 0;
    while (p[i] && i < sizeof(win) - 1 && pr_win32_ptr(ctx, a[0] + i, 1)) { win[i] = p[i]; i++; }
    win[i] = 0;

    char host[256];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) {
        ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED
                                          : W32_ERROR_PATH_NOT_FOUND;
        if (ctx->fs_sink)
            pr_log_write(ctx->fs_sink, PR_LOG_WARN, "vfs",
                         "[VFS] CreateFile '%s' NEGADO (fora da raiz do sandbox)", win);
        return W32_INVALID_HANDLE_VALUE;
    }
    int k = -1;
    for (int s2 = 0; s2 < 8; s2++) if (!ctx->files[s2]) { k = s2; break; }
    if (k < 0) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return W32_INVALID_HANDLE_VALUE; }

    uint64_t acc = a[1];
    int wr = (acc & 0x40000000ull) != 0;
    int rd = (acc & 0x80000000ull) != 0;
    if (!rd && !wr) rd = 1;
    uint32_t disp = (uint32_t)a[4];
    FILE* f = NULL;
    int exists = 0;
    { FILE* probe = fopen(host, "rb"); if (probe) { exists = 1; fclose(probe); } }

    switch (disp) {
        case 1: /* CREATE_NEW */
            if (exists) { ctx->last_error = W32_ERROR_FILE_EXISTS; return W32_INVALID_HANDLE_VALUE; }
            f = fopen(host, wr && rd ? "w+b" : wr ? "wb" : "w+b");
            break;
        case 2: /* CREATE_ALWAYS */
            f = fopen(host, wr && rd ? "w+b" : wr ? "wb" : "w+b");
            break;
        case 3: /* OPEN_EXISTING */
            if (!exists) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return W32_INVALID_HANDLE_VALUE; }
            f = fopen(host, wr ? "r+b" : "rb");
            break;
        case 4: /* OPEN_ALWAYS */
            f = fopen(host, exists ? (wr ? "r+b" : "rb") : (wr ? "w+b" : "w+b"));
            break;
        case 5: /* TRUNCATE_EXISTING */
            if (!exists) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return W32_INVALID_HANDLE_VALUE; }
            f = fopen(host, wr ? "w+b" : "wb");
            break;
        default:
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return W32_INVALID_HANDLE_VALUE;
    }
    if (!f) {
        ctx->last_error = exists ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
        return W32_INVALID_HANDLE_VALUE;
    }
    ctx->files[k] = f;
    if (ctx->fs_sink)
        pr_log_write(ctx->fs_sink, PR_LOG_INFO, "vfs",
                     "[VFS] CreateFile '%s' -> %s (disp=%u)", win, host, disp);
    return W32_FILE_BASE | (uint64_t)(k + 1);
}

static uint64_t f_CreateFileW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 5) { *st = PR_ERR_INVALID; return W32_INVALID_HANDLE_VALUE; }
    /* UTF-16 -> UTF-8 (BMP). Surrogates/não-BMP: recusado, nunca adivinhado. */
    char win[192];
    size_t i = 0, u = 0;
    for (;;) {
        const uint16_t* w = (const uint16_t*)pr_win32_ptr(ctx, a[0] + (uint64_t)u * 2, 2);
        if (!w || *w == 0) break;
        uint16_t c = *w;
        if (c >= 0xD800 && c <= 0xDFFF) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return W32_INVALID_HANDLE_VALUE; }
        char enc[3]; size_t nb;
        if (c < 0x80) { enc[0] = (char)c; nb = 1; }
        else if (c < 0x800) { enc[0] = (char)(0xC0 | (c >> 6)); enc[1] = (char)(0x80 | (c & 0x3F)); nb = 2; }
        else { enc[0] = (char)(0xE0 | (c >> 12)); enc[1] = (char)(0x80 | ((c >> 6) & 0x3F)); enc[2] = (char)(0x80 | (c & 0x3F)); nb = 3; }
        if (i + nb >= sizeof(win)) break;
        for (size_t t = 0; t < nb; t++) win[i++] = enc[t];
        u++;
    }
    win[i] = 0;
    uint64_t b[7];
    for (int j = 1; j < 7 && (size_t)j < n; j++) b[j] = a[j];
    /* reusa o corpo ASCII: grava o nome convertido na scratch do vm? —
     * caminho honesto: resolve aqui e reaproveita o core via CreateFileA
     * com um descritor temporário na própria função A (mesma lógica). */
    char host[256];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) {
        ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED
                                          : W32_ERROR_PATH_NOT_FOUND;
        return W32_INVALID_HANDLE_VALUE;
    }
    (void)b;
    uint64_t aa[7] = { 0, a[1], a[2], a[3], a[4], a[5], a[6] };
    /* nome precisa estar na memória do convidado p/ f_CreateFileA —
     * implementação direta: duplica o core simples */
    int k = -1;
    for (int s2 = 0; s2 < 8; s2++) if (!ctx->files[s2]) { k = s2; break; }
    if (k < 0) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return W32_INVALID_HANDLE_VALUE; }
    uint64_t acc = aa[1];
    int wr = (acc & 0x40000000ull) != 0;
    int rd = (acc & 0x80000000ull) != 0;
    if (!rd && !wr) rd = 1;
    uint32_t disp = (uint32_t)aa[4];
    FILE* f = NULL;
    int exists = 0;
    { FILE* probe = fopen(host, "rb"); if (probe) { exists = 1; fclose(probe); } }
    switch (disp) {
        case 1: if (exists) { ctx->last_error = W32_ERROR_FILE_EXISTS; return W32_INVALID_HANDLE_VALUE; }
                f = fopen(host, wr && rd ? "w+b" : "w+b"); break;
        case 2: f = fopen(host, wr && rd ? "w+b" : wr ? "wb" : "w+b"); break;
        case 3: if (!exists) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return W32_INVALID_HANDLE_VALUE; }
                f = fopen(host, wr ? "r+b" : "rb"); break;
        case 4: f = fopen(host, exists ? (wr ? "r+b" : "rb") : "w+b"); break;
        case 5: if (!exists) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return W32_INVALID_HANDLE_VALUE; }
                f = fopen(host, wr ? "w+b" : "wb"); break;
        default: ctx->last_error = W32_ERROR_INVALID_PARAMETER; return W32_INVALID_HANDLE_VALUE;
    }
    if (!f) { ctx->last_error = W32_ERROR_PATH_NOT_FOUND; return W32_INVALID_HANDLE_VALUE; }
    ctx->files[k] = f;
    return W32_FILE_BASE | (uint64_t)(k + 1);
}

/* ---- G42: threads reais do convidado ---- */
static int w32_thr_guard(void* ud, uint64_t addr, size_t len, int acc) {
    return pr_vm_check((pr_vm*)ud, (uint32_t)addr, len, acc);
}

static int w32_thread_slot(pr_win32_ctx* ctx, uint64_t h) {
    if (h < PR_WIN32_H_THREAD_BASE ||
        h >= PR_WIN32_H_THREAD_BASE + W32_MAX_THREADS) return -1;
    int i = (int)(h - PR_WIN32_H_THREAD_BASE);
    return ctx->threads[i].used ? i : -1;
}

static void w32_thread_cleanup(w32_thread* t) {
    if (!t->joined) { pthread_join(t->tid, NULL); t->joined = 1; }
    if (t->cpu) pr_cpu64_destroy(t->cpu);
    if (t->stack_base && t->ctx && t->ctx->vm)
        pr_vm_unmap(t->ctx->vm, t->stack_base, t->stack_size);
    memset(t, 0, sizeof *t);
}

static void w32_threads_shutdown(pr_win32_ctx* ctx) {
    for (int i = 0; i < W32_MAX_THREADS; i++)
        if (ctx->threads[i].used) w32_thread_cleanup(&ctx->threads[i]);
}

/* Corpo da thread guest: executa a função até o retorno natural (byte HLT da
 * página sentinela) com CPU64 própria sobre a MESMA memória do processo.
 * Concorrência real: roda em pthread paralelo à thread principal. */
static void* w32_thread_main(void* arg) {
    w32_thread* t = (w32_thread*)arg;
    uint64_t exec = 0;
    pr_status r = pr_cpu64_run(t->cpu, 200000000ull, &exec);
    if (r == PR_OK && pr_cpu64_halted(t->cpu)) {
        t->exit_code = pr_cpu64_reg(t->cpu, PR_R64_RAX);
    } else {
        const pr_cpu64_fault* f = pr_cpu64_last_fault(t->cpu);
        t->failed = 1;
        if (t->ctx && t->ctx->log)
            pr_log_write(t->ctx->log, PR_LOG_ERROR, "win32",
                         "[WIN32] thread guest parada: %s (rip=0x%llx)",
                         f && f->reason[0] ? f->reason : "budget esgotado",
                         (unsigned long long)(f ? f->rip : 0));
    }
    t->done = 1;
    return NULL;
}

/* ---- G44: mutex Win32 (CreateMutexA/ReleaseMutex/Wait/CloseHandle) ---- */
static int w32_mutex_slot(pr_win32_ctx* ctx, uint64_t h) {
    if (h < PR_WIN32_H_MUTEX_BASE ||
        h >= PR_WIN32_H_MUTEX_BASE + W32_MAX_MUTEXES) return -1;
    int i = (int)(h - PR_WIN32_H_MUTEX_BASE);
    return ctx->mutexes[i].used ? i : -1;
}

static void w32_mutexes_shutdown(pr_win32_ctx* ctx) {
    for (int i = 0; i < W32_MAX_MUTEXES; i++) {
        w32_mutex* mt = &ctx->mutexes[i];
        if (!mt->used) continue;
        if (mt->owned) { mt->rec = 0; mt->owned = 0; pthread_mutex_unlock(&mt->m); }
        pthread_mutex_destroy(&mt->m);
        memset(mt, 0, sizeof *mt);
    }
}

/* ---- G81: eventos Win32 (CreateEvent/SetEvent/ResetEvent/Wait/CloseHandle) ---- */
static int w32_event_slot(pr_win32_ctx* ctx, uint64_t h) {
    if (h < PR_WIN32_H_EVENT_BASE ||
        h >= PR_WIN32_H_EVENT_BASE + W32_MAX_EVENTS) return -1;
    int i = (int)(h - PR_WIN32_H_EVENT_BASE);
    return ctx->events[i].used ? i : -1;
}

static void w32_events_shutdown(pr_win32_ctx* ctx) {
    for (int i = 0; i < W32_MAX_EVENTS; i++) {
        w32_event* ev = &ctx->events[i];
        if (!ev->used) continue;
        pthread_cond_destroy(&ev->c);
        pthread_mutex_destroy(&ev->m);
        memset(ev, 0, sizeof *ev);
    }
}

static int w32_sem_slot(pr_win32_ctx* ctx, uint64_t h) {
    if (h < PR_WIN32_H_SEM_BASE || h >= PR_WIN32_H_SEM_BASE + W32_MAX_SEMS) return -1;
    int i = (int)(h - PR_WIN32_H_SEM_BASE);
    return ctx->sems[i].used ? i : -1;
}
static void w32_sems_shutdown(pr_win32_ctx* ctx) {
    for (int i = 0; i < W32_MAX_SEMS; i++) {
        w32_sem* s = &ctx->sems[i];
        if (!s->used) continue;
        pthread_cond_destroy(&s->c);
        pthread_mutex_destroy(&s->m);
        memset(s, 0, sizeof *s);
    }
}

/* ---- G82: FindFirstFile (enumeração de diretório via VFS) ---- */
#include <dirent.h>
#include <ctype.h>
static int w32_find_slot(pr_win32_ctx* ctx, uint64_t h) {
    if (h < PR_WIN32_H_FIND_BASE ||
        h >= PR_WIN32_H_FIND_BASE + W32_MAX_FINDS) return -1;
    int i = (int)(h - PR_WIN32_H_FIND_BASE);
    return ctx->finds[i].used ? i : -1;
}
static void w32_finds_shutdown(pr_win32_ctx* ctx) {
    for (int i = 0; i < W32_MAX_FINDS; i++) {
        w32_find* f = &ctx->finds[i];
        if (!f->used) continue;
        if (f->dir) { closedir((DIR*)f->dir); f->dir = NULL; }
        memset(f, 0, sizeof *f);
    }
}
static int w32_reg_slot(pr_win32_ctx* ctx, uint64_t h) {
    if (h < PR_WIN32_H_REG_BASE || h >= PR_WIN32_H_REG_BASE + W32_MAX_REG_KEYS) return -1;
    int i = (int)(h - PR_WIN32_H_REG_BASE);
    return ctx->reg_keys[i].used ? i : -1;
}
static void w32_regs_shutdown(pr_win32_ctx* ctx) {
    for (int i=0;i<W32_MAX_REG_KEYS;i++) {
        w32_reg_key* k=&ctx->reg_keys[i];
        if (!k->used) continue;
        memset(k,0,sizeof *k);
    }
}
#define W32_HKEY_CLASSES_ROOT 0x80000000u
#define W32_HKEY_CURRENT_USER 0x80000001u
#define W32_HKEY_LOCAL_MACHINE 0x80000002u
#define W32_HKEY_USERS 0x80000003u
#define W32_HKEY_CURRENT_CONFIG 0x80000005u
static int w32_is_predefined_hkey(uint64_t h) {
    uint32_t low = (uint32_t)h;
    return low==W32_HKEY_CLASSES_ROOT || low==W32_HKEY_CURRENT_USER || low==W32_HKEY_LOCAL_MACHINE || low==W32_HKEY_USERS || low==W32_HKEY_CURRENT_CONFIG;
}
static const char* w32_predefined_to_str(uint64_t h) {
    uint32_t low = (uint32_t)h;
    if (low==W32_HKEY_CLASSES_ROOT) return "HKEY_CLASSES_ROOT";
    if (low==W32_HKEY_CURRENT_USER) return "HKEY_CURRENT_USER";
    if (low==W32_HKEY_LOCAL_MACHINE) return "HKEY_LOCAL_MACHINE";
    if (low==W32_HKEY_USERS) return "HKEY_USERS";
    if (low==W32_HKEY_CURRENT_CONFIG) return "HKEY_CURRENT_CONFIG";
    return NULL;
}
static int w32_reg_normalize_subkey(const char* in, char* out, size_t cap) {
    /* reject .. and normalize \ -> / */
    if (!in || !out) return 0;
    size_t o=0;
    for (size_t i=0; in[i] && o+1<cap; i++) {
        char ch = in[i]=='\\' ? '/' : in[i];
        out[o++]=ch;
    }
    out[o]=0;
    /* reject .. */
    for (size_t k=0; out[k]; k++) {
        if (out[k]=='.' && out[k+1]=='.' && (k==0 || out[k-1]=='/') && (out[k+2]==0 || out[k+2]=='/')) return 0;
    }
    return 1;
}
static int w32_reg_resolve(pr_win32_ctx* ctx, uint64_t hkey, const char* subkey, char* out_full, size_t full_cap, char* out_host, size_t host_cap) {
    char base[192]={0};
    if (w32_is_predefined_hkey(hkey)) {
        const char* s = w32_predefined_to_str(hkey);
        if (!s) return 0;
        snprintf(base, sizeof(base), "%s", s);
    } else {
        int idx = w32_reg_slot(ctx, hkey);
        if (idx<0) return 0;
        snprintf(base, sizeof(base), "%s", ctx->reg_keys[idx].full_path);
    }
    char norm_sub[192]={0};
    if (subkey && subkey[0]) {
        if (!w32_reg_normalize_subkey(subkey, norm_sub, sizeof(norm_sub))) return 0;
    }
    if (norm_sub[0]) {
        snprintf(out_full, full_cap, "%s/%s", base, norm_sub);
    } else {
        snprintf(out_full, full_cap, "%s", base);
    }
    /* host path = fs_root/registry/<full> */
    if (!ctx->fs_root[0]) return 0;
    snprintf(out_host, host_cap, "%s/registry/%s", ctx->fs_root, out_full);
    return 1;
}
static int w32_mkdir_p(const char* path) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char* p=tmp+1; *p; p++) {
        if (*p=='/') {
            *p='\0';
            mkdir(tmp, 0777);
            *p='/';
        }
    }
    return mkdir(path, 0777)==0 || errno==EEXIST;
}
#define W32_REG_SZ 1
#define W32_REG_BINARY 3
#define W32_REG_DWORD 4
#define W32_REG_QWORD 11
#define W32_ERROR_MORE_DATA 234u
#define W32_ERROR_NO_MORE_ITEMS 259u

/* wildcard case-insensitive: * = zero+ chars, ? = single char */
static int w32_wildcard_match(const char* str, const char* pat) {
    /* iterative backtracking */
    const char *s = str, *p = pat;
    const char *star = NULL, *ss = s;
    while (*s) {
        if (*p == '?' || tolower((unsigned char)*p) == tolower((unsigned char)*s)) {
            s++; p++;
        } else if (*p == '*') {
            star = p++;
            ss = s;
        } else if (star) {
            p = star + 1;
            s = ++ss;
        } else {
            return 0;
        }
    }
    while (*p == '*') p++;
    return *p == '\0';
}
static void w32_filetime_from_time_t(time_t t, uint32_t* low, uint32_t* high) {
    /* FILETIME = 100ns since 1601-01-01 */
    const long long EPOCH_DIFF = 11644473600LL;
    long long ft = ((long long)t + EPOCH_DIFF) * 10000000LL;
    *low = (uint32_t)(ft & 0xFFFFFFFFLL);
    *high = (uint32_t)((ft >> 32) & 0xFFFFFFFFLL);
}
#define W32_FIND_DATAW_SIZE 592
#define W32_FIND_DATA_SIZE 592 /* legacy alias for W */
#define W32_FIND_OFF_ATTR 0
#define W32_FIND_OFF_FT_CREATE 4
#define W32_FIND_OFF_FT_ACCESS 12
#define W32_FIND_OFF_FT_WRITE 20
#define W32_FIND_OFF_SIZE_HIGH 28
#define W32_FIND_OFF_SIZE_LOW 32
#define W32_FIND_OFF_RES0 36
#define W32_FIND_OFF_RES1 40
#define W32_FIND_OFF_CFILE 44
#define W32_FIND_OFF_CALT 564
#define W32_FIND_DATAA_SIZE 320
#define W32_FIND_DATAA_OFF_ATTR 0
#define W32_FIND_DATAA_OFF_FT_CREATE 4
#define W32_FIND_DATAA_OFF_FT_ACCESS 12
#define W32_FIND_DATAA_OFF_FT_WRITE 20
#define W32_FIND_DATAA_OFF_SIZE_HIGH 28
#define W32_FIND_DATAA_OFF_SIZE_LOW 32
#define W32_FIND_DATAA_OFF_RES0 36
#define W32_FIND_DATAA_OFF_RES1 40
#define W32_FIND_DATAA_OFF_CFILE 44
#define W32_FIND_DATAA_OFF_CALT 304
static int w32_fill_find_data(pr_win32_ctx* ctx, uint64_t guest_addr, const char* name, struct stat* stb) {
    uint8_t* base = (uint8_t*)pr_win32_ptr(ctx, guest_addr, W32_FIND_DATA_SIZE);
    if (!base) return 0;
    memset(base, 0, W32_FIND_DATA_SIZE);
    uint32_t attr = S_ISDIR(stb->st_mode) ? W32_FILE_ATTRIBUTE_DIRECTORY : W32_FILE_ATTRIBUTE_NORMAL;
    memcpy(base + W32_FIND_OFF_ATTR, &attr, 4);
    uint32_t low, high;
    w32_filetime_from_time_t(stb->st_mtime, &low, &high);
    memcpy(base + W32_FIND_OFF_FT_CREATE, &low, 4);
    memcpy(base + W32_FIND_OFF_FT_CREATE + 4, &high, 4);
    memcpy(base + W32_FIND_OFF_FT_ACCESS, &low, 4);
    memcpy(base + W32_FIND_OFF_FT_ACCESS + 4, &high, 4);
    memcpy(base + W32_FIND_OFF_FT_WRITE, &low, 4);
    memcpy(base + W32_FIND_OFF_FT_WRITE + 4, &high, 4);
    uint32_t size_low = (uint32_t)(stb->st_size & 0xFFFFFFFFULL);
    uint32_t size_high = (uint32_t)((stb->st_size >> 32) & 0xFFFFFFFFULL);
    if (S_ISDIR(stb->st_mode)) { size_low = 0; size_high = 0; }
    memcpy(base + W32_FIND_OFF_SIZE_HIGH, &size_high, 4);
    memcpy(base + W32_FIND_OFF_SIZE_LOW, &size_low, 4);
    /* cFileName UTF-16 */
    size_t max_chars = 260;
    size_t i = 0;
    for (; i < max_chars - 1 && name[i]; i++) {
        uint16_t wc = (uint8_t)name[i]; /* ASCII -> UTF-16 */
        base[W32_FIND_OFF_CFILE + i * 2] = (uint8_t)(wc & 0xFF);
        base[W32_FIND_OFF_CFILE + i * 2 + 1] = (uint8_t)(wc >> 8);
    }
    base[W32_FIND_OFF_CFILE + i * 2] = 0;
    base[W32_FIND_OFF_CFILE + i * 2 + 1] = 0;
    /* cAlternateFileName already zero */
    return 1;
}
static int w32_fill_find_dataA(pr_win32_ctx* ctx, uint64_t guest_addr, const char* name, struct stat* stb) {
    uint8_t* base = (uint8_t*)pr_win32_ptr(ctx, guest_addr, W32_FIND_DATAA_SIZE);
    if (!base) return 0;
    memset(base, 0, W32_FIND_DATAA_SIZE);
    uint32_t attr = S_ISDIR(stb->st_mode) ? W32_FILE_ATTRIBUTE_DIRECTORY : W32_FILE_ATTRIBUTE_NORMAL;
    memcpy(base + W32_FIND_DATAA_OFF_ATTR, &attr, 4);
    uint32_t low, high;
    w32_filetime_from_time_t(stb->st_mtime, &low, &high);
    memcpy(base + W32_FIND_DATAA_OFF_FT_CREATE, &low, 4);
    memcpy(base + W32_FIND_DATAA_OFF_FT_CREATE + 4, &high, 4);
    memcpy(base + W32_FIND_DATAA_OFF_FT_ACCESS, &low, 4);
    memcpy(base + W32_FIND_DATAA_OFF_FT_ACCESS + 4, &high, 4);
    memcpy(base + W32_FIND_DATAA_OFF_FT_WRITE, &low, 4);
    memcpy(base + W32_FIND_DATAA_OFF_FT_WRITE + 4, &high, 4);
    uint32_t size_low = (uint32_t)(stb->st_size & 0xFFFFFFFFULL);
    uint32_t size_high = (uint32_t)((stb->st_size >> 32) & 0xFFFFFFFFULL);
    if (S_ISDIR(stb->st_mode)) { size_low = 0; size_high = 0; }
    memcpy(base + W32_FIND_DATAA_OFF_SIZE_HIGH, &size_high, 4);
    memcpy(base + W32_FIND_DATAA_OFF_SIZE_LOW, &size_low, 4);
    /* cFileName ANSI */
    size_t max_chars = 260;
    size_t i = 0;
    for (; i < max_chars - 1 && name[i]; i++) {
        base[W32_FIND_DATAA_OFF_CFILE + i] = (uint8_t)name[i];
    }
    base[W32_FIND_DATAA_OFF_CFILE + i] = 0;
    /* cAlternate already zero */
    return 1;
}

/* subconjunto honesto: lpSecurityAttributes=NULL e lpName=NULL (mutex nomeado
 * = fora do escopo do G44 — não fingimos nomes entre processos) */
static uint64_t f_CreateMutexA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    if (a[0] || a[2]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    int i;
    for (i = 0; i < W32_MAX_MUTEXES; i++) if (!ctx->mutexes[i].used) break;
    if (i == W32_MAX_MUTEXES) { ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY; return 0; }
    w32_mutex* mt = &ctx->mutexes[i];
    memset(mt, 0, sizeof *mt);
    if (pthread_mutex_init(&mt->m, NULL) != 0) {
        ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY;
        return 0;
    }
    mt->used = 1;
    if (a[1]) {
        /* bInitialOwner=TRUE: owned REAL pela thread chamadora (trava de fato) */
        pthread_mutex_lock(&mt->m);
        mt->owner = pthread_self();
        mt->owned = 1;
        mt->rec = 1;
    }
    return PR_WIN32_H_MUTEX_BASE + (uint64_t)i;
}

static uint64_t f_ReleaseMutex(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    int mi = w32_mutex_slot(ctx, a[0]);
    if (mi < 0) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    w32_mutex* mt = &ctx->mutexes[mi];
    if (!mt->owned || !pthread_equal(mt->owner, pthread_self())) {
        ctx->last_error = W32_ERROR_NOT_OWNER;   /* ERROR_NOT_OWNER = 288 */
        return 0;                                 /* FALSE: nunca sucesso falso */
    }
    mt->rec--;
    if (mt->rec == 0) {
        mt->owned = 0;
        pthread_mutex_unlock(&mt->m);
    }
    return 1;                                     /* TRUE */
}

static uint64_t f_CreateMutexW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    return f_CreateMutexA(ctx, a, n, st);
}
static uint64_t f_CreateSemaphoreA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    if (a[0] != 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if (a[3] != 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    int32_t init = (int32_t)a[1];
    int32_t maxc = (int32_t)a[2];
    if (init < 0 || maxc <=0 || init > maxc) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    int i; for (i=0;i<W32_MAX_SEMS;i++) if (!ctx->sems[i].used) break;
    if (i==W32_MAX_SEMS) { ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY; return 0; }
    w32_sem* s = &ctx->sems[i];
    memset(s,0,sizeof *s);
    if (pthread_mutex_init(&s->m,NULL)!=0) { ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY; return 0; }
    if (pthread_cond_init(&s->c,NULL)!=0) { pthread_mutex_destroy(&s->m); ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY; return 0; }
    s->used=1; s->count=init; s->max_count=maxc;
    return PR_WIN32_H_SEM_BASE + (uint64_t)i;
}
static uint64_t f_CreateSemaphoreW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    return f_CreateSemaphoreA(ctx, a, n, st);
}
static uint64_t f_ReleaseSemaphore(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    int si = w32_sem_slot(ctx, a[0]);
    if (si<0) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    w32_sem* s = &ctx->sems[si];
    int32_t rel = (int32_t)a[1];
    if (rel<=0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    pthread_mutex_lock(&s->m);
    int prev = s->count;
    if (s->count + rel > s->max_count) { pthread_mutex_unlock(&s->m); ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    s->count += rel;
    if (a[2]) {
        int32_t* prev_out = (int32_t*)pr_win32_ptr(ctx, a[2], 4);
        if (prev_out) *prev_out = prev;
    }
    for (int i=0;i<rel;i++) pthread_cond_signal(&s->c);
    pthread_mutex_unlock(&s->m);
    return 1;
}
static uint64_t f_InitializeSRWLock(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint8_t* p = (uint8_t*)pr_win32_ptr(ctx, a[0], 8);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    memset(p, 0, 8);
    return 0;
}
static uint64_t f_AcquireSRWLockExclusive(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    /* SRWLock em single-thread: no-op, pois não há concorrência real guest */
    uint8_t* p = (uint8_t*)pr_win32_ptr(ctx, a[0], 8);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    return 0;
}
static uint64_t f_AcquireSRWLockShared(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint8_t* p = (uint8_t*)pr_win32_ptr(ctx, a[0], 8);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    return 0;
}
static uint64_t f_ReleaseSRWLockExclusive(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint8_t* p = (uint8_t*)pr_win32_ptr(ctx, a[0], 8);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    return 0;
}
static uint64_t f_ReleaseSRWLockShared(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint8_t* p = (uint8_t*)pr_win32_ptr(ctx, a[0], 8);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    return 0;
}

static uint64_t f_CreateEventA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* G81: CreateEventA — evento manual/auto, initial state, unnamed apenas.
     * Contrato: HANDLE CreateEventA(LPSECURITY_ATTRIBUTES, BOOL bManualReset,
     *   BOOL bInitialState, LPCSTR lpName);
     * Subconjunto: lpSecurityAttributes=NULL, lpName=NULL (named fora do escopo).
     * Retorna handle ou NULL + LastError. */
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    if (a[0] != 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if (a[3] != 0) {
        /* Named events exigem namespace global — fora do escopo G81, documentado */
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    int i;
    for (i = 0; i < W32_MAX_EVENTS; i++) if (!ctx->events[i].used) break;
    if (i == W32_MAX_EVENTS) { ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY; return 0; }
    w32_event* ev = &ctx->events[i];
    memset(ev, 0, sizeof *ev);
    if (pthread_mutex_init(&ev->m, NULL) != 0) {
        ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY;
        return 0;
    }
    if (pthread_cond_init(&ev->c, NULL) != 0) {
        pthread_mutex_destroy(&ev->m);
        ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY;
        return 0;
    }
    ev->used = 1;
    ev->manual = a[1] ? 1 : 0;
    ev->signaled = a[2] ? 1 : 0;
    return PR_WIN32_H_EVENT_BASE + (uint64_t)i;
}

static uint64_t f_CreateEventW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* G81: CreateEventW — variante Unicode, reutiliza wstr_to_ascii para nome */
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    if (a[0] != 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if (a[3] != 0) {
        char name[64];
        wstr_to_ascii(ctx, a[3], name, sizeof(name));
        if (name[0]) {
            /* Named events fora do escopo */
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return 0;
        }
    }
    int i;
    for (i = 0; i < W32_MAX_EVENTS; i++) if (!ctx->events[i].used) break;
    if (i == W32_MAX_EVENTS) { ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY; return 0; }
    w32_event* ev = &ctx->events[i];
    memset(ev, 0, sizeof *ev);
    if (pthread_mutex_init(&ev->m, NULL) != 0) {
        ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY;
        return 0;
    }
    if (pthread_cond_init(&ev->c, NULL) != 0) {
        pthread_mutex_destroy(&ev->m);
        ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY;
        return 0;
    }
    ev->used = 1;
    ev->manual = a[1] ? 1 : 0;
    ev->signaled = a[2] ? 1 : 0;
    return PR_WIN32_H_EVENT_BASE + (uint64_t)i;
}

static uint64_t f_SetEvent(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    int ei = w32_event_slot(ctx, a[0]);
    if (ei < 0) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    w32_event* ev = &ctx->events[ei];
    pthread_mutex_lock(&ev->m);
    ev->signaled = 1;
    if (ev->manual) {
        pthread_cond_broadcast(&ev->c);
    } else {
        pthread_cond_signal(&ev->c);
    }
    pthread_mutex_unlock(&ev->m);
    return 1;
}

static uint64_t f_ResetEvent(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    int ei = w32_event_slot(ctx, a[0]);
    if (ei < 0) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    w32_event* ev = &ctx->events[ei];
    pthread_mutex_lock(&ev->m);
    ev->signaled = 0;
    pthread_mutex_unlock(&ev->m);
    return 1;
}

static uint64_t f_FindFirstFileW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return W32_INVALID_HANDLE_VALUE; }
    uint64_t path_va = a[0];
    uint64_t out_va = a[1];
    if (path_va == 0 || out_va == 0) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return W32_INVALID_HANDLE_VALUE;
    }
    if (!pr_win32_ptr(ctx, out_va, W32_FIND_DATA_SIZE)) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return W32_INVALID_HANDLE_VALUE;
    }
    char win[192];
    wstr_to_ascii(ctx, path_va, win, sizeof(win));
    if (!win[0]) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return W32_INVALID_HANDLE_VALUE;
    }
    /* split dir + pattern */
    char dir_part[192] = {0};
    char pattern[192] = {0};
    int last_sep = -1;
    for (int i = 0; win[i]; i++) {
        if (win[i] == '/' || win[i] == '\\') last_sep = i;
    }
    if (last_sep >= 0) {
        size_t dl = (size_t)last_sep;
        if (dl >= sizeof(dir_part)) dl = sizeof(dir_part) - 1;
        memcpy(dir_part, win, dl);
        dir_part[dl] = 0;
        const char* fp = win + last_sep + 1;
        size_t pl = strlen(fp);
        if (pl >= sizeof(pattern)) pl = sizeof(pattern) - 1;
        memcpy(pattern, fp, pl);
        pattern[pl] = 0;
    } else {
        snprintf(pattern, sizeof(pattern), "%s", win);
    }
    if (pattern[0] == '\0') {
        strcpy(pattern, "*");
    }
    /* special: *.* -> * for practical compat */
    if (strcmp(pattern, "*.*") == 0) {
        strcpy(pattern, "*");
    }
    char host_dir[256];
    if (dir_part[0] == '\0') {
        if (!vfs_resolve(ctx, "", host_dir, sizeof(host_dir))) {
            ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
            return W32_INVALID_HANDLE_VALUE;
        }
    } else {
        if (!vfs_resolve(ctx, dir_part, host_dir, sizeof(host_dir))) {
            ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
            if (ctx->fs_sink)
                pr_log_write(ctx->fs_sink, PR_LOG_WARN, "vfs",
                             "[VFS] FindFirst '%s' NEGADO (fora da raiz)", win);
            return W32_INVALID_HANDLE_VALUE;
        }
    }
    struct stat dst;
    if (stat(host_dir, &dst) != 0) {
        ctx->last_error = W32_ERROR_PATH_NOT_FOUND;
        return W32_INVALID_HANDLE_VALUE;
    }
    if (!S_ISDIR(dst.st_mode)) {
        /* dir_part is actually a file? try its parent */
        ctx->last_error = W32_ERROR_PATH_NOT_FOUND;
        return W32_INVALID_HANDLE_VALUE;
    }
    int slot = -1;
    for (int i = 0; i < W32_MAX_FINDS; i++) if (!ctx->finds[i].used) { slot = i; break; }
    if (slot < 0) {
        ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY;
        return W32_INVALID_HANDLE_VALUE;
    }
    DIR* d = opendir(host_dir);
    if (!d) {
        ctx->last_error = W32_ERROR_PATH_NOT_FOUND;
        return W32_INVALID_HANDLE_VALUE;
    }
    w32_find* f = &ctx->finds[slot];
    memset(f, 0, sizeof *f);
    f->used = 1;
    snprintf(f->dir_host, sizeof(f->dir_host), "%s", host_dir);
    snprintf(f->pattern, sizeof(f->pattern), "%s", pattern);
    f->dir = d;

    struct dirent* ent;
    while ((ent = readdir((DIR*)f->dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (!w32_wildcard_match(ent->d_name, f->pattern)) continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", f->dir_host, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!w32_fill_find_data(ctx, out_va, ent->d_name, &st)) {
            closedir((DIR*)f->dir);
            memset(f, 0, sizeof *f);
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return W32_INVALID_HANDLE_VALUE;
        }
        return PR_WIN32_H_FIND_BASE + (uint64_t)slot;
    }
    /* no match */
    closedir((DIR*)f->dir);
    memset(f, 0, sizeof *f);
    ctx->last_error = W32_ERROR_FILE_NOT_FOUND;
    return W32_INVALID_HANDLE_VALUE;
}

static uint64_t f_FindNextFileW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    uint64_t h = a[0];
    uint64_t out_va = a[1];
    if (out_va == 0) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    if (!pr_win32_ptr(ctx, out_va, W32_FIND_DATA_SIZE)) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    int idx = w32_find_slot(ctx, h);
    if (idx < 0) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    w32_find* f = &ctx->finds[idx];
    if (!f->dir) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    struct dirent* ent;
    while ((ent = readdir((DIR*)f->dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (!w32_wildcard_match(ent->d_name, f->pattern)) continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", f->dir_host, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!w32_fill_find_data(ctx, out_va, ent->d_name, &st)) {
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return 0;
        }
        return 1;
    }
    ctx->last_error = W32_ERROR_NO_MORE_FILES;
    return 0;
}

static uint64_t f_FindClose(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    int idx = w32_find_slot(ctx, a[0]);
    if (idx < 0) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    w32_find* f = &ctx->finds[idx];
    if (f->dir) { closedir((DIR*)f->dir); f->dir = NULL; }
    memset(f, 0, sizeof *f);
    return 1;
}

static uint64_t f_FindFirstFileA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return W32_INVALID_HANDLE_VALUE; }
    uint64_t path_va = a[0];
    uint64_t out_va = a[1];
    if (path_va == 0 || out_va == 0) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return W32_INVALID_HANDLE_VALUE;
    }
    if (!pr_win32_ptr(ctx, out_va, W32_FIND_DATAA_SIZE)) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return W32_INVALID_HANDLE_VALUE;
    }
    const char* p = (const char*)pr_win32_ptr(ctx, path_va, 1);
    if (!p) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return W32_INVALID_HANDLE_VALUE;
    }
    char win[192];
    size_t i = 0;
    while (p[i] && i < sizeof(win) - 1 && pr_win32_ptr(ctx, path_va + i, 1)) { win[i] = p[i]; i++; }
    win[i] = 0;
    if (!win[0]) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return W32_INVALID_HANDLE_VALUE;
    }
    char dir_part[192] = {0};
    char pattern[192] = {0};
    int last_sep = -1;
    for (int j = 0; win[j]; j++) {
        if (win[j] == '/' || win[j] == '\\') last_sep = j;
    }
    if (last_sep >= 0) {
        size_t dl = (size_t)last_sep;
        if (dl >= sizeof(dir_part)) dl = sizeof(dir_part) - 1;
        memcpy(dir_part, win, dl);
        dir_part[dl] = 0;
        const char* fp = win + last_sep + 1;
        size_t pl = strlen(fp);
        if (pl >= sizeof(pattern)) pl = sizeof(pattern) - 1;
        memcpy(pattern, fp, pl);
        pattern[pl] = 0;
    } else {
        snprintf(pattern, sizeof(pattern), "%s", win);
    }
    if (pattern[0] == '\0') strcpy(pattern, "*");
    if (strcmp(pattern, "*.*") == 0) strcpy(pattern, "*");
    char host_dir[256];
    if (dir_part[0] == '\0') {
        if (!vfs_resolve(ctx, "", host_dir, sizeof(host_dir))) {
            ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
            return W32_INVALID_HANDLE_VALUE;
        }
    } else {
        if (!vfs_resolve(ctx, dir_part, host_dir, sizeof(host_dir))) {
            ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
            if (ctx->fs_sink)
                pr_log_write(ctx->fs_sink, PR_LOG_WARN, "vfs",
                             "[VFS] FindFirstA '%s' NEGADO (fora da raiz)", win);
            return W32_INVALID_HANDLE_VALUE;
        }
    }
    struct stat dst;
    if (stat(host_dir, &dst) != 0) {
        ctx->last_error = W32_ERROR_PATH_NOT_FOUND;
        return W32_INVALID_HANDLE_VALUE;
    }
    if (!S_ISDIR(dst.st_mode)) {
        ctx->last_error = W32_ERROR_PATH_NOT_FOUND;
        return W32_INVALID_HANDLE_VALUE;
    }
    int slot = -1;
    for (int k = 0; k < W32_MAX_FINDS; k++) if (!ctx->finds[k].used) { slot = k; break; }
    if (slot < 0) {
        ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY;
        return W32_INVALID_HANDLE_VALUE;
    }
    DIR* d = opendir(host_dir);
    if (!d) {
        ctx->last_error = W32_ERROR_PATH_NOT_FOUND;
        return W32_INVALID_HANDLE_VALUE;
    }
    w32_find* f = &ctx->finds[slot];
    memset(f, 0, sizeof *f);
    f->used = 1;
    snprintf(f->dir_host, sizeof(f->dir_host), "%s", host_dir);
    snprintf(f->pattern, sizeof(f->pattern), "%s", pattern);
    f->dir = d;
    struct dirent* ent;
    while ((ent = readdir((DIR*)f->dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (!w32_wildcard_match(ent->d_name, f->pattern)) continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", f->dir_host, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!w32_fill_find_dataA(ctx, out_va, ent->d_name, &st)) {
            closedir((DIR*)f->dir);
            memset(f, 0, sizeof *f);
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return W32_INVALID_HANDLE_VALUE;
        }
        return PR_WIN32_H_FIND_BASE + (uint64_t)slot;
    }
    closedir((DIR*)f->dir);
    memset(f, 0, sizeof *f);
    ctx->last_error = W32_ERROR_FILE_NOT_FOUND;
    return W32_INVALID_HANDLE_VALUE;
}

static uint64_t f_FindNextFileA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    uint64_t h = a[0];
    uint64_t out_va = a[1];
    if (out_va == 0) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    if (!pr_win32_ptr(ctx, out_va, W32_FIND_DATAA_SIZE)) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    int idx = w32_find_slot(ctx, h);
    if (idx < 0) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    w32_find* f = &ctx->finds[idx];
    if (!f->dir) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    struct dirent* ent;
    while ((ent = readdir((DIR*)f->dir)) != NULL) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
        if (!w32_wildcard_match(ent->d_name, f->pattern)) continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", f->dir_host, ent->d_name);
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (!w32_fill_find_dataA(ctx, out_va, ent->d_name, &st)) {
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return 0;
        }
        return 1;
    }
    ctx->last_error = W32_ERROR_NO_MORE_FILES;
    return 0;
}

/* ---- G84: Registry virtualizado (advapi32) ---- */
static uint64_t f_RegCreateKeyExA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 5) { *st = PR_ERR_INVALID; return 2; }
    uint64_t hkey = a[0];
    uint64_t subkey_va = a[1];
    uint64_t res_va = a[2];
    uint64_t class_va = a[3];
    uint64_t options = a[4];
    uint64_t sam = n>5 ? a[5] : 0;
    uint64_t sec_va = n>6 ? a[6] : 0;
    uint64_t out_hkey_va = n>7 ? a[7] : 0;
    uint64_t disp_va = n>8 ? a[8] : 0;
    (void)res_va; (void)class_va; (void)options; (void)sam; (void)sec_va;
    if (subkey_va==0 || out_hkey_va==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    const char* p = (const char*)pr_win32_ptr(ctx, subkey_va, 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    char subkey[192]; size_t i=0;
    while (p[i] && i < sizeof(subkey)-1 && pr_win32_ptr(ctx, subkey_va+i,1)) { subkey[i]=p[i]; i++; } subkey[i]=0;
    if (!subkey[0]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    char full[256], host[512];
    if (!w32_reg_resolve(ctx, hkey, subkey, full, sizeof(full), host, sizeof(host))) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 87;
    }
    if (!w32_mkdir_p(host)) {
        ctx->last_error = W32_ERROR_ACCESS_DENIED;
        return 5;
    }
    int slot=-1;
    for (int k=0;k<W32_MAX_REG_KEYS;k++) if (!ctx->reg_keys[k].used) { slot=k; break; }
    if (slot<0) { ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY; return 8; }
    w32_reg_key* rk=&ctx->reg_keys[slot];
    memset(rk,0,sizeof *rk);
    rk->used=1;
    snprintf(rk->full_path, sizeof(rk->full_path), "%s", full);
    uint64_t* out = (uint64_t*)pr_win32_ptr(ctx, out_hkey_va, 8);
    if (!out) { memset(rk,0,sizeof *rk); ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    *out = PR_WIN32_H_REG_BASE + (uint64_t)slot;
    if (disp_va) {
        uint32_t* disp = (uint32_t*)pr_win32_ptr(ctx, disp_va, 4);
        if (disp) *disp = 1; /* REG_CREATED_NEW_KEY */
    }
    return 0; /* ERROR_SUCCESS */
}
static uint64_t f_RegCreateKeyExW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 5) { *st = PR_ERR_INVALID; return 2; }
    uint64_t hkey = a[0];
    uint64_t subkey_va = a[1];
    uint64_t out_hkey_va = n>7 ? a[7] : 0;
    uint64_t disp_va = n>8 ? a[8] : 0;
    if (subkey_va==0 || out_hkey_va==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    char subkey[192];
    wstr_to_ascii(ctx, subkey_va, subkey, sizeof(subkey));
    if (!subkey[0]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    char full[256], host[512];
    if (!w32_reg_resolve(ctx, hkey, subkey, full, sizeof(full), host, sizeof(host))) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 87;
    }
    if (!w32_mkdir_p(host)) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 5; }
    int slot=-1;
    for (int k=0;k<W32_MAX_REG_KEYS;k++) if (!ctx->reg_keys[k].used) { slot=k; break; }
    if (slot<0) { ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY; return 8; }
    w32_reg_key* rk=&ctx->reg_keys[slot];
    memset(rk,0,sizeof *rk);
    rk->used=1;
    snprintf(rk->full_path, sizeof(rk->full_path), "%s", full);
    uint64_t* out = (uint64_t*)pr_win32_ptr(ctx, out_hkey_va, 8);
    if (!out) { memset(rk,0,sizeof *rk); ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    *out = PR_WIN32_H_REG_BASE + (uint64_t)slot;
    if (disp_va) {
        uint32_t* disp = (uint32_t*)pr_win32_ptr(ctx, disp_va, 4);
        if (disp) *disp = 1;
    }
    return 0;
}
static uint64_t f_RegOpenKeyExA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 2; }
    uint64_t hkey = a[0];
    uint64_t subkey_va = a[1];
    uint64_t options = a[2];
    uint64_t sam = a[3];
    uint64_t out_va = n>4 ? a[4] : 0;
    (void)options; (void)sam;
    if (out_va==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    char subkey[192]={0};
    if (subkey_va) {
        const char* p = (const char*)pr_win32_ptr(ctx, subkey_va, 1);
        if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
        size_t i=0; while (p[i] && i < sizeof(subkey)-1 && pr_win32_ptr(ctx, subkey_va+i,1)) { subkey[i]=p[i]; i++; } subkey[i]=0;
    }
    char full[256], host[512];
    if (!w32_reg_resolve(ctx, hkey, subkey[0]?subkey:NULL, full, sizeof(full), host, sizeof(host))) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 87;
    }
    struct stat stb;
    if (stat(host, &stb)!=0 || !S_ISDIR(stb.st_mode)) {
        ctx->last_error = W32_ERROR_FILE_NOT_FOUND;
        return 2;
    }
    int slot=-1;
    for (int k=0;k<W32_MAX_REG_KEYS;k++) if (!ctx->reg_keys[k].used) { slot=k; break; }
    if (slot<0) { ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY; return 8; }
    w32_reg_key* rk=&ctx->reg_keys[slot];
    memset(rk,0,sizeof *rk);
    rk->used=1;
    snprintf(rk->full_path, sizeof(rk->full_path), "%s", full);
    uint64_t* out = (uint64_t*)pr_win32_ptr(ctx, out_va, 8);
    if (!out) { memset(rk,0,sizeof *rk); ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    *out = PR_WIN32_H_REG_BASE + (uint64_t)slot;
    return 0;
}
static uint64_t f_RegOpenKeyExW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 2; }
    uint64_t hkey = a[0];
    uint64_t subkey_va = a[1];
    uint64_t out_va = n>4 ? a[4] : 0;
    if (out_va==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    char subkey[192]={0};
    if (subkey_va) wstr_to_ascii(ctx, subkey_va, subkey, sizeof(subkey));
    char full[256], host[512];
    if (!w32_reg_resolve(ctx, hkey, subkey[0]?subkey:NULL, full, sizeof(full), host, sizeof(host))) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 87;
    }
    struct stat stb;
    if (stat(host, &stb)!=0 || !S_ISDIR(stb.st_mode)) {
        ctx->last_error = W32_ERROR_FILE_NOT_FOUND;
        return 2;
    }
    int slot=-1;
    for (int k=0;k<W32_MAX_REG_KEYS;k++) if (!ctx->reg_keys[k].used) { slot=k; break; }
    if (slot<0) { ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY; return 8; }
    w32_reg_key* rk=&ctx->reg_keys[slot];
    memset(rk,0,sizeof *rk);
    rk->used=1;
    snprintf(rk->full_path, sizeof(rk->full_path), "%s", full);
    uint64_t* out = (uint64_t*)pr_win32_ptr(ctx, out_va, 8);
    if (!out) { memset(rk,0,sizeof *rk); ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    *out = PR_WIN32_H_REG_BASE + (uint64_t)slot;
    return 0;
}
static uint64_t f_RegCloseKey(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 87; }
    uint64_t h = a[0];
    if (w32_is_predefined_hkey(h)) return 0;
    int idx = w32_reg_slot(ctx, h);
    if (idx<0) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 6; }
    memset(&ctx->reg_keys[idx],0,sizeof(ctx->reg_keys[idx]));
    return 0;
}
static int reg_value_path(const char* key_host, const char* value_name, char* out, size_t cap, char* out_type, size_t type_cap) {
    char vname[128];
    if (!value_name || !value_name[0]) snprintf(vname, sizeof(vname), "__default");
    else {
        /* reject / \ .. */
        for (size_t i=0; value_name[i]; i++) if (value_name[i]=='/' || value_name[i]=='\\') return 0;
        if (strstr(value_name, "..")) return 0;
        snprintf(vname, sizeof(vname), "%s", value_name);
    }
    snprintf(out, cap, "%s/%s", key_host, vname);
    snprintf(out_type, type_cap, "%s/%s.type", key_host, vname);
    return 1;
}
static uint64_t f_RegSetValueExA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 5) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    uint64_t name_va = a[1];
    uint64_t res = a[2];
    uint64_t type = a[3];
    uint64_t data_va = a[4];
    uint64_t cb = n>5 ? a[5] : 0;
    (void)res;
    char key_full[256], key_host[512];
    if (!w32_reg_resolve(ctx, hkey, NULL, key_full, sizeof(key_full), key_host, sizeof(key_host))) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 6;
    }
    struct stat stb;
    if (stat(key_host, &stb)!=0) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 2; }
    char vname[128]={0};
    if (name_va) {
        const char* p = (const char*)pr_win32_ptr(ctx, name_va, 1);
        if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
        size_t i=0; while (p[i] && i < sizeof(vname)-1 && pr_win32_ptr(ctx, name_va+i,1)) { vname[i]=p[i]; i++; } vname[i]=0;
    }
    char val_path[512], type_path[512];
    if (!reg_value_path(key_host, vname[0]?vname:NULL, val_path, sizeof(val_path), type_path, sizeof(type_path))) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87;
    }
    const uint8_t* data_ptr = NULL;
    if (cb>0) {
        data_ptr = (const uint8_t*)pr_win32_ptr(ctx, data_va, (size_t)cb);
        if (!data_ptr) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    }
    FILE* f = fopen(val_path, "wb");
    if (!f) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 5; }
    if (cb>0 && data_ptr) fwrite(data_ptr,1,(size_t)cb,f);
    fclose(f);
    FILE* tf = fopen(type_path, "wb");
    if (tf) { uint32_t t = (uint32_t)type; fwrite(&t,1,4,tf); fclose(tf); }
    return 0;
}
static uint64_t f_RegSetValueExW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 5) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    uint64_t name_va = a[1];
    uint64_t type = a[3];
    uint64_t data_va = a[4];
    uint64_t cb = n>5 ? a[5] : 0;
    char key_full[256], key_host[512];
    if (!w32_reg_resolve(ctx, hkey, NULL, key_full, sizeof(key_full), key_host, sizeof(key_host))) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 6;
    }
    struct stat stb;
    if (stat(key_host, &stb)!=0) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 2; }
    char vname[128]={0};
    if (name_va) wstr_to_ascii(ctx, name_va, vname, sizeof(vname));
    char val_path[512], type_path[512];
    if (!reg_value_path(key_host, vname[0]?vname:NULL, val_path, sizeof(val_path), type_path, sizeof(type_path))) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87;
    }
    const uint8_t* data_ptr = NULL;
    if (cb>0) {
        data_ptr = (const uint8_t*)pr_win32_ptr(ctx, data_va, (size_t)cb);
        if (!data_ptr) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
    }
    FILE* f = fopen(val_path, "wb");
    if (!f) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 5; }
    if (cb>0 && data_ptr) fwrite(data_ptr,1,(size_t)cb,f);
    fclose(f);
    FILE* tf = fopen(type_path, "wb");
    if (tf) { uint32_t t = (uint32_t)type; fwrite(&t,1,4,tf); fclose(tf); }
    return 0;
}
static uint64_t f_RegQueryValueExA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 5) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    uint64_t name_va = a[1];
    uint64_t res_va = a[2];
    uint64_t type_va = a[3];
    uint64_t data_va = a[4];
    uint64_t cb_va = n>5 ? a[5] : 0;
    (void)res_va;
    char key_full[256], key_host[512];
    if (!w32_reg_resolve(ctx, hkey, NULL, key_full, sizeof(key_full), key_host, sizeof(key_host))) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE; return 6;
    }
    char vname[128]={0};
    if (name_va) {
        const char* p = (const char*)pr_win32_ptr(ctx, name_va, 1);
        if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
        size_t i=0; while (p[i] && i < sizeof(vname)-1 && pr_win32_ptr(ctx, name_va+i,1)) { vname[i]=p[i]; i++; } vname[i]=0;
    }
    char val_path[512], type_path[512];
    if (!reg_value_path(key_host, vname[0]?vname:NULL, val_path, sizeof(val_path), type_path, sizeof(type_path))) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87;
    }
    FILE* tf = fopen(type_path, "rb");
    uint32_t ftype = W32_REG_SZ;
    if (tf) { fread(&ftype,1,4,tf); fclose(tf); }
    FILE* f = fopen(val_path, "rb");
    if (!f) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 2; }
    fseek(f,0,SEEK_END); long sz = ftell(f); fseek(f,0,SEEK_SET);
    uint8_t tmp[1024];
    if (sz > (long)sizeof(tmp)) sz = (long)sizeof(tmp);
    fread(tmp,1,(size_t)sz,f);
    fclose(f);
    int is_utf16 = 0;
    if (ftype==W32_REG_SZ && sz>=2 && sz%2==0) {
        /* heuristic: if second byte 0 and contains 0,0 terminator, treat as UTF-16 */
        if (tmp[1]==0) is_utf16 = 1;
    }
    uint32_t out_sz;
    if (ftype==W32_REG_SZ && is_utf16) {
        /* stored as UTF-16, need ANSI */
        size_t wlen = 0;
        for (size_t i=0;i+1<(size_t)sz;i+=2) { if (tmp[i]==0 && tmp[i+1]==0) break; wlen++; }
        out_sz = (uint32_t)(wlen+1);
    } else {
        out_sz = (uint32_t)sz;
    }
    if (type_va) {
        uint32_t* tp = (uint32_t*)pr_win32_ptr(ctx, type_va, 4);
        if (tp) *tp = ftype;
    }
    if (cb_va) {
        uint32_t* cbp = (uint32_t*)pr_win32_ptr(ctx, cb_va, 4);
        if (!cbp) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
        if (data_va==0) { *cbp = out_sz; return 0; }
        if (*cbp < out_sz) { *cbp = out_sz; return W32_ERROR_MORE_DATA; }
        *cbp = out_sz;
    }
    if (data_va) {
        uint8_t* out = (uint8_t*)pr_win32_ptr(ctx, data_va, out_sz);
        if (!out) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
        if (ftype==W32_REG_SZ && is_utf16) {
            size_t wlen = 0;
            for (size_t i=0;i+1<(size_t)sz;i+=2) { if (tmp[i]==0 && tmp[i+1]==0) break; out[wlen++]=tmp[i]; }
            out[wlen]=0;
        } else {
            memcpy(out, tmp, out_sz);
        }
    }
    return 0;
}
static uint64_t f_RegQueryValueExW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 5) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    uint64_t name_va = a[1];
    uint64_t type_va = a[3];
    uint64_t data_va = a[4];
    uint64_t cb_va = n>5 ? a[5] : 0;
    char key_full[256], key_host[512];
    if (!w32_reg_resolve(ctx, hkey, NULL, key_full, sizeof(key_full), key_host, sizeof(key_host))) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE; return 6;
    }
    char vname[128]={0};
    if (name_va) wstr_to_ascii(ctx, name_va, vname, sizeof(vname));
    char val_path[512], type_path[512];
    if (!reg_value_path(key_host, vname[0]?vname:NULL, val_path, sizeof(val_path), type_path, sizeof(type_path))) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87;
    }
    FILE* tf = fopen(type_path, "rb");
    uint32_t ftype = W32_REG_SZ;
    if (tf) { fread(&ftype,1,4,tf); fclose(tf); }
    FILE* f = fopen(val_path, "rb");
    if (!f) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 2; }
    fseek(f,0,SEEK_END); long sz = ftell(f); fseek(f,0,SEEK_SET);
    uint8_t tmp[1024];
    if (sz> (long)sizeof(tmp)) sz = (long)sizeof(tmp);
    fread(tmp,1,(size_t)sz,f);
    fclose(f);
    int is_utf16 = 0;
    if (ftype==W32_REG_SZ && sz>=2 && sz%2==0 && tmp[1]==0) is_utf16 = 1;
    uint32_t out_sz;
    if (ftype==W32_REG_SZ) {
        if (is_utf16) out_sz = (uint32_t)sz;
        else out_sz = (uint32_t)((strlen((char*)tmp)+1)*2);
    } else {
        out_sz = (uint32_t)sz;
    }
    if (type_va) {
        uint32_t* tp = (uint32_t*)pr_win32_ptr(ctx, type_va, 4);
        if (tp) *tp = ftype;
    }
    if (cb_va) {
        uint32_t* cbp = (uint32_t*)pr_win32_ptr(ctx, cb_va, 4);
        if (!cbp) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
        if (data_va==0) { *cbp = out_sz; return 0; }
        if (*cbp < out_sz) { *cbp = out_sz; return W32_ERROR_MORE_DATA; }
        *cbp = out_sz;
    }
    if (data_va) {
        uint8_t* out = (uint8_t*)pr_win32_ptr(ctx, data_va, out_sz);
        if (!out) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 87; }
        if (ftype==W32_REG_SZ) {
            if (is_utf16) memcpy(out, tmp, out_sz);
            else {
                size_t len = strlen((char*)tmp);
                for (size_t i=0;i<=len;i++) { out[i*2]=tmp[i]; out[i*2+1]=0; }
            }
        } else {
            memcpy(out, tmp, out_sz);
        }
    }
    return 0;
}
static uint64_t f_RegDeleteValueA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    uint64_t name_va = a[1];
    char key_full[256], key_host[512];
    if (!w32_reg_resolve(ctx, hkey, NULL, key_full, sizeof(key_full), key_host, sizeof(key_host))) return 6;
    char vname[128]={0};
    if (name_va) {
        const char* p = (const char*)pr_win32_ptr(ctx, name_va, 1);
        if (!p) return 87;
        size_t i=0; while (p[i] && i < sizeof(vname)-1 && pr_win32_ptr(ctx, name_va+i,1)) { vname[i]=p[i]; i++; } vname[i]=0;
    }
    char val_path[512], type_path[512];
    if (!reg_value_path(key_host, vname[0]?vname:NULL, val_path, sizeof(val_path), type_path, sizeof(type_path))) return 87;
    if (unlink(val_path)!=0) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 2; }
    unlink(type_path);
    return 0;
}
static uint64_t f_RegDeleteValueW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    uint64_t name_va = a[1];
    char key_full[256], key_host[512];
    if (!w32_reg_resolve(ctx, hkey, NULL, key_full, sizeof(key_full), key_host, sizeof(key_host))) return 6;
    char vname[128]={0};
    if (name_va) wstr_to_ascii(ctx, name_va, vname, sizeof(vname));
    char val_path[512], type_path[512];
    if (!reg_value_path(key_host, vname[0]?vname:NULL, val_path, sizeof(val_path), type_path, sizeof(type_path))) return 87;
    if (unlink(val_path)!=0) return 2;
    unlink(type_path);
    return 0;
}
static uint64_t f_RegDeleteKeyA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    uint64_t subkey_va = a[1];
    char subkey[192]={0};
    if (subkey_va) {
        const char* p = (const char*)pr_win32_ptr(ctx, subkey_va, 1);
        if (!p) return 87;
        size_t i=0; while (p[i] && i < sizeof(subkey)-1 && pr_win32_ptr(ctx, subkey_va+i,1)) { subkey[i]=p[i]; i++; } subkey[i]=0;
    }
    char full[256], host[512];
    if (!w32_reg_resolve(ctx, hkey, subkey[0]?subkey:NULL, full, sizeof(full), host, sizeof(host))) return 87;
    /* check empty */
    DIR* d = opendir(host);
    if (!d) return 2;
    struct dirent* ent;
    int cnt=0;
    while ((ent=readdir(d))!=NULL) { if (strcmp(ent->d_name,".")!=0 && strcmp(ent->d_name,"..")!=0) cnt++; }
    closedir(d);
    if (cnt>0) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 5; }
    if (rmdir(host)!=0) return 5;
    return 0;
}
static uint64_t f_RegDeleteKeyW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    uint64_t subkey_va = a[1];
    char subkey[192]={0};
    if (subkey_va) wstr_to_ascii(ctx, subkey_va, subkey, sizeof(subkey));
    char full[256], host[512];
    if (!w32_reg_resolve(ctx, hkey, subkey[0]?subkey:NULL, full, sizeof(full), host, sizeof(host))) return 87;
    DIR* d = opendir(host);
    if (!d) return 2;
    struct dirent* ent; int cnt=0;
    while ((ent=readdir(d))!=NULL) { if (strcmp(ent->d_name,".")!=0 && strcmp(ent->d_name,"..")!=0) cnt++; }
    closedir(d);
    if (cnt>0) return 5;
    if (rmdir(host)!=0) return 5;
    return 0;
}
static uint64_t f_RegEnumKeyExA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    uint64_t idx = a[1];
    uint64_t name_va = a[2];
    uint64_t name_len_va = a[3];
    char key_full[256], key_host[512];
    if (!w32_reg_resolve(ctx, hkey, NULL, key_full, sizeof(key_full), key_host, sizeof(key_host))) return 6;
    if (name_va==0 || name_len_va==0) return 87;
    uint32_t* lenp = (uint32_t*)pr_win32_ptr(ctx, name_len_va, 4);
    if (!lenp) return 87;
    DIR* d = opendir(key_host);
    if (!d) return 2;
    struct dirent* ent;
    uint32_t cur=0;
    while ((ent=readdir(d))!=NULL) {
        if (strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0) continue;
        char full[1024]; snprintf(full, sizeof(full), "%s/%s", key_host, ent->d_name);
        struct stat stb; if (stat(full,&stb)!=0) continue;
        if (!S_ISDIR(stb.st_mode)) continue;
        if (cur==idx) {
            size_t namelen = strlen(ent->d_name);
            if (*lenp <= namelen) { *lenp = (uint32_t)namelen+1; closedir(d); return W32_ERROR_MORE_DATA; }
            char* out = (char*)pr_win32_ptr(ctx, name_va, namelen+1);
            if (!out) { closedir(d); return 87; }
            strcpy(out, ent->d_name);
            *lenp = (uint32_t)namelen;
            closedir(d);
            return 0;
        }
        cur++;
    }
    closedir(d);
    return W32_ERROR_NO_MORE_ITEMS;
}
static uint64_t f_RegEnumValueA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 6) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    uint64_t idx = a[1];
    uint64_t name_va = a[2];
    uint64_t name_len_va = a[3];
    uint64_t type_va = a[5];
    uint64_t data_va = n>6 ? a[6] : 0;
    uint64_t cb_va = n>7 ? a[7] : 0;
    char key_full[256], key_host[512];
    if (!w32_reg_resolve(ctx, hkey, NULL, key_full, sizeof(key_full), key_host, sizeof(key_host))) return 6;
    if (name_va==0 || name_len_va==0) return 87;
    uint32_t* lenp = (uint32_t*)pr_win32_ptr(ctx, name_len_va, 4);
    if (!lenp) return 87;
    DIR* d = opendir(key_host);
    if (!d) return 2;
    struct dirent* ent;
    uint32_t cur=0;
    while ((ent=readdir(d))!=NULL) {
        if (strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0) continue;
        if (strstr(ent->d_name, ".type")) continue;
        char full[1024]; snprintf(full, sizeof(full), "%s/%s", key_host, ent->d_name);
        struct stat stb; if (stat(full,&stb)!=0) continue;
        if (S_ISDIR(stb.st_mode)) continue;
        if (cur==idx) {
            const char* vname = ent->d_name;
            if (strcmp(vname,"__default")==0) vname="";
            size_t namelen = strlen(vname);
            if (*lenp <= namelen) { *lenp = (uint32_t)namelen+1; closedir(d); return W32_ERROR_MORE_DATA; }
            char* out = (char*)pr_win32_ptr(ctx, name_va, namelen+1);
            if (!out) { closedir(d); return 87; }
            strcpy(out, vname);
            *lenp = (uint32_t)namelen;
            /* type */
            char type_path[1024]; snprintf(type_path, sizeof(type_path), "%s/%s.type", key_host, ent->d_name);
            FILE* tf = fopen(type_path,"rb");
            uint32_t ftype = W32_REG_SZ;
            if (tf) { fread(&ftype,1,4,tf); fclose(tf); }
            if (type_va) { uint32_t* tp = (uint32_t*)pr_win32_ptr(ctx, type_va,4); if (tp) *tp=ftype; }
            if (cb_va && data_va) {
                uint32_t* cbp = (uint32_t*)pr_win32_ptr(ctx, cb_va,4);
                if (cbp) {
                    FILE* f = fopen(full,"rb");
                    if (f) { fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
                        if (*cbp < (uint32_t)sz) { *cbp=(uint32_t)sz; fclose(f); closedir(d); return W32_ERROR_MORE_DATA; }
                        uint8_t* outd = (uint8_t*)pr_win32_ptr(ctx, data_va, sz);
                        if (outd) fread(outd,1,sz,f);
                        *cbp=(uint32_t)sz;
                        fclose(f);
                    }
                }
            } else if (cb_va) {
                uint32_t* cbp = (uint32_t*)pr_win32_ptr(ctx, cb_va,4);
                if (cbp) {
                    struct stat st; stat(full,&st); *cbp=(uint32_t)st.st_size;
                }
            }
            closedir(d);
            return 0;
        }
        cur++;
    }
    closedir(d);
    return W32_ERROR_NO_MORE_ITEMS;
}
static uint64_t f_RegEnumKeyExW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    uint64_t idx = a[1];
    uint64_t name_va = a[2];
    uint64_t name_len_va = a[3];
    char key_full[256], key_host[512];
    if (!w32_reg_resolve(ctx, hkey, NULL, key_full, sizeof(key_full), key_host, sizeof(key_host))) return 6;
    if (name_va==0 || name_len_va==0) return 87;
    uint32_t* lenp = (uint32_t*)pr_win32_ptr(ctx, name_len_va, 4);
    if (!lenp) return 87;
    DIR* d = opendir(key_host);
    if (!d) return 2;
    struct dirent* ent;
    uint32_t cur=0;
    while ((ent=readdir(d))!=NULL) {
        if (strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0) continue;
        char full[1024]; snprintf(full, sizeof(full), "%s/%s", key_host, ent->d_name);
        struct stat stb; if (stat(full,&stb)!=0) continue;
        if (!S_ISDIR(stb.st_mode)) continue;
        if (cur==idx) {
            size_t namelen = strlen(ent->d_name);
            size_t need = namelen+1;
            if (*lenp < need) { *lenp = (uint32_t)need; closedir(d); return W32_ERROR_MORE_DATA; }
            /* write UTF-16 */
            uint8_t* out = (uint8_t*)pr_win32_ptr(ctx, name_va, need*2);
            if (!out) { closedir(d); return 87; }
            for (size_t i=0;i<=namelen;i++) { out[i*2]= (uint8_t)ent->d_name[i]; out[i*2+1]=0; }
            *lenp = (uint32_t)namelen;
            closedir(d);
            return 0;
        }
        cur++;
    }
    closedir(d);
    return W32_ERROR_NO_MORE_ITEMS;
}
static uint64_t f_RegEnumValueW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 6) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    uint64_t idx = a[1];
    uint64_t name_va = a[2];
    uint64_t name_len_va = a[3];
    uint64_t type_va = a[5];
    uint64_t data_va = n>6 ? a[6] : 0;
    uint64_t cb_va = n>7 ? a[7] : 0;
    char key_full[256], key_host[512];
    if (!w32_reg_resolve(ctx, hkey, NULL, key_full, sizeof(key_full), key_host, sizeof(key_host))) return 6;
    if (name_va==0 || name_len_va==0) return 87;
    uint32_t* lenp = (uint32_t*)pr_win32_ptr(ctx, name_len_va, 4);
    if (!lenp) return 87;
    DIR* d = opendir(key_host);
    if (!d) return 2;
    struct dirent* ent;
    uint32_t cur=0;
    while ((ent=readdir(d))!=NULL) {
        if (strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0) continue;
        if (strstr(ent->d_name, ".type")) continue;
        char full[1024]; snprintf(full, sizeof(full), "%s/%s", key_host, ent->d_name);
        struct stat stb; if (stat(full,&stb)!=0) continue;
        if (S_ISDIR(stb.st_mode)) continue;
        if (cur==idx) {
            const char* vname = ent->d_name;
            if (strcmp(vname,"__default")==0) vname="";
            size_t namelen = strlen(vname);
            size_t need = namelen+1;
            if (*lenp < need) { *lenp = (uint32_t)need; closedir(d); return W32_ERROR_MORE_DATA; }
            uint8_t* out = (uint8_t*)pr_win32_ptr(ctx, name_va, need*2);
            if (!out) { closedir(d); return 87; }
            for (size_t i=0;i<=namelen;i++) { out[i*2]=(uint8_t)vname[i]; out[i*2+1]=0; }
            *lenp = (uint32_t)namelen;
            char type_path[1024]; snprintf(type_path, sizeof(type_path), "%s/%s.type", key_host, ent->d_name);
            FILE* tf = fopen(type_path,"rb");
            uint32_t ftype = W32_REG_SZ;
            if (tf) { fread(&ftype,1,4,tf); fclose(tf); }
            if (type_va) { uint32_t* tp = (uint32_t*)pr_win32_ptr(ctx, type_va,4); if (tp) *tp=ftype; }
            if (cb_va && data_va) {
                uint32_t* cbp = (uint32_t*)pr_win32_ptr(ctx, cb_va,4);
                if (cbp) {
                    FILE* f = fopen(full,"rb");
                    if (f) {
                        fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
                        uint32_t out_sz = (uint32_t)sz;
                        if (ftype==W32_REG_SZ) out_sz = (uint32_t)((strlen(vname)+1)*2); /* actually read content */
                        /* For simplicity, handle SZ conversion */
                        uint8_t tmp[512]; fread(tmp,1,sz,f); fclose(f);
                        if (ftype==W32_REG_SZ) {
                            size_t slen = strlen((char*)tmp);
                            out_sz = (uint32_t)((slen+1)*2);
                            if (*cbp < out_sz) { *cbp=out_sz; closedir(d); return W32_ERROR_MORE_DATA; }
                            uint8_t* outd = (uint8_t*)pr_win32_ptr(ctx, data_va, out_sz);
                            if (outd) for (size_t i=0;i<=slen;i++) { outd[i*2]=tmp[i]; outd[i*2+1]=0; }
                            *cbp=out_sz;
                        } else {
                            if (*cbp < (uint32_t)sz) { *cbp=(uint32_t)sz; closedir(d); return W32_ERROR_MORE_DATA; }
                            uint8_t* outd = (uint8_t*)pr_win32_ptr(ctx, data_va, sz);
                            if (outd) memcpy(outd, tmp, sz);
                            *cbp=(uint32_t)sz;
                        }
                    }
                }
            } else if (cb_va) {
                uint32_t* cbp = (uint32_t*)pr_win32_ptr(ctx, cb_va,4);
                if (cbp) { struct stat st; stat(full,&st); *cbp=(uint32_t)st.st_size; }
            }
            closedir(d);
            return 0;
        }
        cur++;
    }
    closedir(d);
    return W32_ERROR_NO_MORE_ITEMS;
}

static uint64_t f_RegQueryInfoKeyA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 12) { *st = PR_ERR_INVALID; return 87; }
    uint64_t hkey = a[0];
    char key_full[256], key_host[512];
    if (!w32_reg_resolve(ctx, hkey, NULL, key_full, sizeof(key_full), key_host, sizeof(key_host))) return 6;
    DIR* d = opendir(key_host);
    if (!d) return 2;
    uint32_t subkeys=0, values=0;
    struct dirent* ent;
    while ((ent=readdir(d))!=NULL) {
        if (strcmp(ent->d_name,".")==0 || strcmp(ent->d_name,"..")==0) continue;
        if (strstr(ent->d_name, ".type")) continue;
        char full[1024]; snprintf(full, sizeof(full), "%s/%s", key_host, ent->d_name);
        struct stat stb; if (stat(full,&stb)!=0) continue;
        if (S_ISDIR(stb.st_mode)) subkeys++; else values++;
    }
    closedir(d);
    if (a[2]) { uint32_t* p = (uint32_t*)pr_win32_ptr(ctx, a[2], 4); if (p) *p = subkeys; }
    if (a[6]) { uint32_t* p = (uint32_t*)pr_win32_ptr(ctx, a[6], 4); if (p) *p = values; }
    return 0;
}
static uint64_t f_RegQueryInfoKeyW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    return f_RegQueryInfoKeyA(ctx, a, n, st);
}
static uint64_t f_RegFlushKey(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 87; }
    char key_full[256], key_host[512];
    if (!w32_reg_resolve(ctx, a[0], NULL, key_full, sizeof(key_full), key_host, sizeof(key_host))) return 6;
    /* No-op: filesystem sync is immediate, but we ensure dir exists */
    struct stat stb; if (stat(key_host, &stb)!=0) return 2;
    return 0;
}

/* ---- G44: despacho Win32 a partir de threads guest (INT 0x2E nas workers) ----
 * Mesma maquinaria de proc_api_trap64 (peproc): idx do stub = índice do catálogo
 * (pr_win32_catalog + pr_win32_call_entry), ABI x64 (RCX/RDX/R8/R9 ou XMM0-3;
 * 5º+ em [rsp+0x28...]). Retorno 0 = continua (stub segue para `ret`); 1 = parada
 * honesta (nunca sucesso falso). */
static int w32_thr_apitrap(void* ud, pr_cpu64* cpu, uint8_t vector) {
    pr_win32_ctx* ctx = (pr_win32_ctx*)ud;
    if (vector != 0x2E) {
        if (ctx->log)
            pr_log_write(ctx->log, PR_LOG_ERROR, "win32",
                         "[WIN32] Unsupported interrupt | vector INT 0x%02X | (thread guest)", vector);
        pr_cpu64_set_reg(cpu, PR_R64_RAX, 0);
        return 1;
    }
    uint64_t rsp = pr_cpu64_reg(cpu, PR_R64_RSP);
    size_t idx = (size_t)(pr_cpu64_reg(cpu, PR_R64_RAX) & 0xFFFFFFFFu);
    const pr_win32_export* catalog = NULL;
    size_t ncat = pr_win32_catalog(&catalog);
    if (idx >= ncat) {
        if (ctx->log)
            pr_log_write(ctx->log, PR_LOG_ERROR, "win32",
                         "[WIN32] Unsupported Win32 API | (despacho) | (indice invalido)");
        pr_cpu64_set_reg(cpu, PR_R64_RAX, 0);
        return 1;
    }
    const pr_win32_export* e = &catalog[idx];
    if (e->status != PR_WIN32_IMPLEMENTED || !e->fn) {
        if (ctx->log)
            pr_log_write(ctx->log, PR_LOG_ERROR, "win32",
                         "[WIN32] Unsupported Win32 API | %s | %s", e->module, e->name);
        pr_cpu64_set_reg(cpu, PR_R64_RAX, 0);
        return 1;
    }
    size_t nargs = e->stdcall_bytes / 4;
    uint64_t args[12];
    if (nargs > 12) nargs = 12;
    static const int argreg[4] = { PR_R64_RCX, PR_R64_RDX, 8, 9 };
    for (size_t i = 0; i < nargs; i++) {
        uint64_t v = 0;
        if (i < 4) {
            if ((e->arg_xmm >> i) & 1u) {
                uint8_t xb[16];
                pr_cpu64_xmm(cpu, (int)i, xb);
                memcpy(&v, xb, 8);
            } else {
                v = pr_cpu64_reg(cpu, argreg[i]);
            }
        } else if (pr_vm_read(ctx->vm, (uint32_t)(rsp + 0x28 + (i - 4) * 8), &v, 8) != PR_OK) {
            if (ctx->log)
                pr_log_write(ctx->log, PR_LOG_ERROR, "win32",
                             "[WIN32] Invalid memory access | %s | %s (argumentos da API)",
                             e->module, e->name);
            pr_cpu64_set_reg(cpu, PR_R64_RAX, 0);
            return 1;
        }
        args[i] = v;
    }
    if (ctx->log)
        pr_log_write(ctx->log, PR_LOG_INFO, "win32", "[WIN32] API %s!%s", e->module, e->name);
    uint64_t ret = 0;
    pr_status st = pr_win32_call_entry(ctx, e, args, nargs, &ret);
    if (pr_win32_halted(ctx)) return 1;
    if (st == PR_ERR_UNSUPPORTED) {
        if (ctx->log)
            pr_log_write(ctx->log, PR_LOG_ERROR, "win32",
                         "[WIN32] Unsupported Win32 API | %s | %s", e->module, e->name);
        pr_cpu64_set_reg(cpu, PR_R64_RAX, 0);
        return 1;
    }
    pr_cpu64_set_reg(cpu, PR_R64_RAX, ret);
    return 0;
}

static uint64_t f_CreateThread(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    /* subconjunto honesto: lpSecurityAttributes=NULL e dwCreationFlags=0 */
    if (!ctx->vm || (n >= 1 && a[0]) || (n >= 5 && a[4])) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    uint64_t start = a[2], param = a[3];
    if (!start) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t stk = (n >= 2 && a[1] >= 0x4000) ? (size_t)a[1] : 0x10000u;
    stk = (stk + 0xFFF) & ~(size_t)0xFFF;
    int i;
    for (i = 0; i < W32_MAX_THREADS; i++) if (!ctx->threads[i].used) break;
    if (i == W32_MAX_THREADS) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    uint32_t space = 0;
    uint8_t* mem = pr_vm_backing(ctx->vm, &space);
    if (!mem || space == 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    /* página sentinela compartilhada: um byte HLT = retorno natural da função */
    if (!ctx->thread_sentinel) {
        uint32_t sb = 0;
        void* sh = NULL;
        if (pr_vm_alloc(ctx->vm, 0x1000, PR_VM_PROT_R | PR_VM_PROT_W,
                        "w32thsnt", &sb, &sh) != PR_OK || !sh) {
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return 0;
        }
        *(uint8_t*)sh = 0xF4u;                          /* HLT */
        if (pr_vm_protect(ctx->vm, sb, 0x1000, PR_VM_PROT_R | PR_VM_PROT_X) != PR_OK) {
            pr_vm_unmap(ctx->vm, sb, 0x1000);
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return 0;
        }
        ctx->thread_sentinel = sb;
        ctx->thread_sentinel_sz = 0x1000;
    }
    uint32_t sbase = 0;
    if (pr_vm_alloc(ctx->vm, stk, PR_VM_PROT_R | PR_VM_PROT_W,
                    "w32thrd", &sbase, NULL) != PR_OK) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    /* frame Microsoft x64 na entrada da função: [rsp]=retorno, rsp%16==8 */
    uint64_t top = (uint64_t)sbase + stk;
    uint64_t rsp = (top - 0x38) & ~0xFull;
    uint64_t frame = rsp - 0x28;
    uint64_t retaddr = ctx->thread_sentinel;
    if (pr_vm_write(ctx->vm, (uint32_t)frame, &retaddr, 8) != PR_OK) {
        pr_vm_unmap(ctx->vm, sbase, (uint32_t)stk);
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    pr_cpu64* cpu = pr_cpu64_create_on(mem, space);
    if (!cpu) {
        pr_vm_unmap(ctx->vm, sbase, (uint32_t)stk);
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    pr_cpu64_set_guard(cpu, w32_thr_guard, ctx->vm);
    pr_cpu64_set_trap(cpu, w32_thr_apitrap, ctx);
    pr_cpu64_set_rflags(cpu, 0x202);
    pr_cpu64_set_rip(cpu, start);
    pr_cpu64_set_reg(cpu, PR_R64_RAX, 0);
    pr_cpu64_set_reg(cpu, PR_R64_RCX, param);
    pr_cpu64_set_reg(cpu, PR_R64_RDX, 0);
    pr_cpu64_set_reg(cpu, 8, 0);            /* R8 */
    pr_cpu64_set_reg(cpu, 9, 0);            /* R9 */
    pr_cpu64_set_reg(cpu, PR_R64_RBP, 0);
    pr_cpu64_set_reg(cpu, PR_R64_RSP, frame);
    w32_thread* t = &ctx->threads[i];
    memset(t, 0, sizeof *t);
    t->used = 1;
    t->cpu = cpu;
    t->ctx = ctx;
    t->stack_base = sbase;
    t->stack_size = (uint32_t)stk;
    if (pthread_create(&t->tid, NULL, w32_thread_main, t) != 0) {
        t->used = 0;
        w32_thread_cleanup(t);
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    if (n >= 6 && a[5]) {
        uint32_t* out = (uint32_t*)pr_win32_ptr(ctx, a[5], 4);
        if (out) *out = (uint32_t)i + 1;    /* lpThreadId = LPDWORD (4 bytes) */
    }
    return PR_WIN32_H_THREAD_BASE + (uint64_t)i;
}

static uint64_t f_ExitThread(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint32_t code = (uint32_t)a[0];
    /* Encontra thread atual via pthread_self */
    for (int i=0;i<W32_MAX_THREADS;i++) {
        if (!ctx->threads[i].used) continue;
        if (pthread_equal(ctx->threads[i].tid, pthread_self())) {
            ctx->threads[i].exit_code = code;
            ctx->threads[i].done = 1;
            pthread_exit(NULL);
        }
    }
    /* Se não for thread guest, apenas retorna */
    return 0;
}
static uint64_t f_GetExitCodeThread(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    int ti = w32_thread_slot(ctx, a[0]);
    if (ti<0) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    uint32_t* out = (uint32_t*)pr_win32_ptr(ctx, a[1], 4);
    if (!out) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    w32_thread* t = &ctx->threads[ti];
    if (!t->done) *out = 259; /* STILL_ACTIVE */
    else *out = t->exit_code;
    return 1;
}
static uint64_t f_GetCurrentThread(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)a; (void)n; *st = PR_OK;
    /* pseudo-handle -2 */
    return (uint64_t)(int64_t)-2;
}
static uint64_t f_OpenThread(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    uint32_t tid = (uint32_t)a[2];
    /* procura thread por id (i+1) */
    for (int i=0;i<W32_MAX_THREADS;i++) {
        if (!ctx->threads[i].used) continue;
        if ((uint32_t)(i+1)==tid) return PR_WIN32_H_THREAD_BASE + (uint64_t)i;
    }
    ctx->last_error = W32_ERROR_INVALID_PARAMETER;
    return 0;
}
static uint64_t f_SuspendThread(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return (uint64_t)-1; }
    int ti = w32_thread_slot(ctx, a[0]);
    if (ti<0) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return (uint64_t)-1; }
    /* Single-thread model: não suspende de verdade, retorna 0 (prev suspend count) */
    return 0;
}
static uint64_t f_ResumeThread(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return (uint64_t)-1; }
    int ti = w32_thread_slot(ctx, a[0]);
    if (ti<0) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return (uint64_t)-1; }
    return 0;
}

static uint64_t f_WaitForSingleObject(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    /* G81: eventos — sinalização real com condvar */
    {
        int ei = w32_event_slot(ctx, a[0]);
        if (ei >= 0) {
            w32_event* ev = &ctx->events[ei];
            uint32_t ms = (uint32_t)a[1];
            pthread_mutex_lock(&ev->m);
            if (ev->signaled) {
                if (!ev->manual) ev->signaled = 0; /* auto-reset consome */
                pthread_mutex_unlock(&ev->m);
                return 0; /* WAIT_OBJECT_0 */
            }
            if (ms == 0) {
                pthread_mutex_unlock(&ev->m);
                return 0x102u; /* WAIT_TIMEOUT */
            }
            if (ms == 0xFFFFFFFFu) {
                while (!ev->signaled) {
                    pthread_cond_wait(&ev->c, &ev->m);
                }
                if (!ev->manual) ev->signaled = 0;
                pthread_mutex_unlock(&ev->m);
                return 0;
            }
            /* timeout finito */
            {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += (time_t)(ms / 1000u);
                ts.tv_nsec += (long)(ms % 1000u) * 1000000L;
                if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
                int rc = 0;
                while (!ev->signaled && rc == 0) {
                    rc = pthread_cond_timedwait(&ev->c, &ev->m, &ts);
                }
                if (ev->signaled) {
                    if (!ev->manual) ev->signaled = 0;
                    pthread_mutex_unlock(&ev->m);
                    return 0;
                }
                pthread_mutex_unlock(&ev->m);
                return 0x102u; /* WAIT_TIMEOUT */
            }
        }
    }
    /* G44: handles de mutex — exclusão mútua real entre threads guest */
    {
        int mi = w32_mutex_slot(ctx, a[0]);
        if (mi >= 0) {
            w32_mutex* mt = &ctx->mutexes[mi];
            uint32_t ms = (uint32_t)a[1];
            if (mt->owned && pthread_equal(mt->owner, pthread_self())) {
                mt->rec++;                          /* recursivo (semântica Windows) */
                return 0;                           /* WAIT_OBJECT_0 */
            }
            if (ms == 0xFFFFFFFFu) {
                pthread_mutex_lock(&mt->m);         /* bloqueio REAL entre pthreads */
                mt->owner = pthread_self();
                mt->owned = 1;
                mt->rec = 1;
                return 0;                           /* WAIT_OBJECT_0 */
            }
            if (ms == 0) {
                /* poll imediato */
                if (pthread_mutex_trylock(&mt->m) == 0) {
                    mt->owner = pthread_self();
                    mt->owned = 1;
                    mt->rec = 1;
                    return 0;                       /* WAIT_OBJECT_0 */
                }
                return 0x102u;                      /* WAIT_TIMEOUT */
            }
            /* G45: timeout finito — bloqueio REAL limitado no tempo (mesma base
             * de tempo de Sleep/QPC: nanossegundos reais monotônicos de parede)
             * Darwin/iOS: pthread_mutex_timedlock não existe no SDK iOS (Apple Clang).
             * Fallback preserva timeout via trylock + nanosleep 1ms, sem busy-loop,
             * com EINTR handling e códigos de retorno compatíveis. */
            {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += (time_t)(ms / 1000u);
                ts.tv_nsec += (long)(ms % 1000u) * 1000000L;
                if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
#if defined(__APPLE__)
                if (pr_darwin_pthread_mutex_timedlock(&mt->m, &ts) == 0) {
                    mt->owner = pthread_self();
                    mt->owned = 1;
                    mt->rec = 1;
                    return 0;                       /* WAIT_OBJECT_0 */
                }
                return 0x102u;                      /* WAIT_TIMEOUT */
#else
                if (pthread_mutex_timedlock(&mt->m, &ts) == 0) {
                    mt->owner = pthread_self();
                    mt->owned = 1;
                    mt->rec = 1;
                    return 0;                       /* WAIT_OBJECT_0 */
                }
                return 0x102u;                      /* WAIT_TIMEOUT */
#endif
            }
        }
    }
    /* G86: semaphore — contagem real */
    {
        int si = w32_sem_slot(ctx, a[0]);
        if (si >= 0) {
            w32_sem* s = &ctx->sems[si];
            uint32_t ms = (uint32_t)a[1];
            pthread_mutex_lock(&s->m);
            if (s->count > 0) {
                s->count--;
                pthread_mutex_unlock(&s->m);
                return 0;
            }
            if (ms == 0) { pthread_mutex_unlock(&s->m); return 0x102u; }
            if (ms == 0xFFFFFFFFu) {
                while (s->count <= 0) pthread_cond_wait(&s->c, &s->m);
                s->count--;
                pthread_mutex_unlock(&s->m);
                return 0;
            }
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += (time_t)(ms/1000u);
            ts.tv_nsec += (long)(ms%1000u)*1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec+=1; ts.tv_nsec-=1000000000L; }
            int rc=0;
            while (s->count<=0 && rc==0) rc = pthread_cond_timedwait(&s->c, &s->m, &ts);
            if (s->count>0) { s->count--; pthread_mutex_unlock(&s->m); return 0; }
            pthread_mutex_unlock(&s->m);
            return 0x102u;
        }
    }
    uint64_t h = a[0];
    if (h == (uint64_t)(int64_t)-2) {
        /* GetCurrentThread pseudo-handle: já está corrente, retorno imediato */
        return 0;
    }
    int i = w32_thread_slot(ctx, h);
    if (i < 0) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0xFFFFFFFFull;               /* WAIT_FAILED */
    }
    w32_thread* t = &ctx->threads[i];
    uint32_t ms = (uint32_t)a[1];
    if (!t->joined) {
        if (ms == 0xFFFFFFFFu) {            /* INFINITE: join real (preservado) */
            pthread_join(t->tid, NULL);
            t->joined = 1;
        } else if (ms == 0) {
            /* poll imediato, sem bloqueio */
            if (!t->done) return 0x102u;    /* WAIT_TIMEOUT */
            pthread_join(t->tid, NULL);
            t->joined = 1;
        } else {
            /* G46: timeout finito em handle de thread — bloqueio REAL limitado ao
             * prazo. Reusa a infraestrutura de join já existente na variante com
             * prazo (pthread_timedjoin_np Linux); no Darwin/iOS usa polling de
             * t->done com timeout preservado (sem busy-loop, 1ms sleep, EINTR handling).
             * Thread termina dentro do prazo => WAIT_OBJECT_0; prazo expira com a
             * thread ainda executando => WAIT_TIMEOUT. */
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += (time_t)(ms / 1000u);
            ts.tv_nsec += (long)(ms % 1000u) * 1000000L;
            if (ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
#if defined(__APPLE__)
            /* Darwin/iOS: pthread_timedjoin_np não existe no SDK iOS (Apple Clang).
             * Fallback: pr_darwin_pthread_timedjoin_np polling t->done, depois join real. */
            if (pr_darwin_pthread_timedjoin_np(t->tid, NULL, &ts, &t->done) != 0) {
                return 0x102u;              /* WAIT_TIMEOUT: thread ainda executando */
            }
            pthread_join(t->tid, NULL);
            t->joined = 1;
#else
            if (pthread_timedjoin_np(t->tid, NULL, &ts) != 0) {
                return 0x102u;              /* WAIT_TIMEOUT: thread ainda executando */
            }
            t->joined = 1;
#endif
        }
    }
    return 0;                               /* WAIT_OBJECT_0 */
}

/* ---- G47: WaitForMultipleObjects — SOMENTE o subconjunto validado:
 * WaitForMultipleObjects(2, thread_handles, TRUE, INFINITE) -> WAIT_OBJECT_0.
 * Fora do subconjunto: WAIT_FAILED + erro honesto (nunca sucesso falso).
 * Preserva os caminhos de thread/mutex/file de Wait/CloseHandle (intocados). */
static uint64_t f_WaitForMultipleObjects(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    uint64_t nCount = a[0], bWaitAll = a[2], ms = a[3];
    if (nCount != 2 || !bWaitAll || ms != 0xFFFFFFFFull) {
        /* subconjunto: nCount==2, bWaitAll==TRUE, dwMilliseconds==INFINITE */
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0xFFFFFFFFull;               /* WAIT_FAILED */
    }
    uint64_t* hs = (uint64_t*)pr_win32_ptr(ctx, a[1], 16);   /* HANDLE[2] x64 */
    if (!hs) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0xFFFFFFFFull;
    }
    int i0 = w32_thread_slot(ctx, hs[0]);
    int i1 = w32_thread_slot(ctx, hs[1]);
    if (i0 < 0 || i1 < 0) {
        /* tipo conhecido mas fora do subconjunto (mutex/file/std) => parametro;
         * handle desconhecido => INVALID_HANDLE */
        uint64_t bad = (i0 < 0) ? hs[0] : hs[1];
        int known = w32_mutex_slot(ctx, bad) >= 0 || w32_is_file_handle(bad) ||
                    (bad >= PR_WIN32_H_STDIN && bad <= PR_WIN32_H_STDERR);
        ctx->last_error = known ? W32_ERROR_INVALID_PARAMETER
                                : W32_ERROR_INVALID_HANDLE;
        return 0xFFFFFFFFull;
    }
    if (i0 == i1) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0xFFFFFFFFull;
    }
    /* espera real por AMBAS (join real); thread já sinalizada = sem espera */
    w32_thread* t0 = &ctx->threads[i0];
    w32_thread* t1 = &ctx->threads[i1];
    if (!t0->joined) { pthread_join(t0->tid, NULL); t0->joined = 1; }
    if (!t1->joined) { pthread_join(t1->tid, NULL); t1->joined = 1; }
    return 0;                               /* WAIT_OBJECT_0 (wait-all sucesso) */
}

static uint64_t f_CloseHandle(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (w32_is_file_handle(a[0])) {
        int k = w32_file_idx(a[0]);
        if (k < 0 || !ctx->files[k]) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
        fclose(ctx->files[k]);
        ctx->files[k] = NULL;
        return 1;
    }
    /* G44: handles de mutex — destruição real; double-close = INVALID_HANDLE */
    {
        int mi = w32_mutex_slot(ctx, a[0]);
        if (mi >= 0) {
            w32_mutex* mt = &ctx->mutexes[mi];
            /* abandono de mutex (WAIT_ABANDONED) fora do escopo: se ainda owned,
             * força a liberação antes de destruir (evita UB do pthread) */
            if (mt->owned) { mt->rec = 0; mt->owned = 0; pthread_mutex_unlock(&mt->m); }
            pthread_mutex_destroy(&mt->m);
            memset(mt, 0, sizeof *mt);
            return 1;
        }
    }
    /* G81: handles de evento — destruição real */
    {
        int ei = w32_event_slot(ctx, a[0]);
        if (ei >= 0) {
            w32_event* ev = &ctx->events[ei];
            pthread_cond_destroy(&ev->c);
            pthread_mutex_destroy(&ev->m);
            memset(ev, 0, sizeof *ev);
            return 1;
        }
    }
    /* G86: semaphore */
    {
        int si = w32_sem_slot(ctx, a[0]);
        if (si >= 0) {
            w32_sem* s = &ctx->sems[si];
            pthread_cond_destroy(&s->c);
            pthread_mutex_destroy(&s->m);
            memset(s, 0, sizeof *s);
            return 1;
        }
    }
    /* G82: handles de FindFirstFile — permite CloseHandle como fallback, mas FindClose é o correto */
    {
        int fi = w32_find_slot(ctx, a[0]);
        if (fi >= 0) {
            w32_find* ff = &ctx->finds[fi];
            if (ff->dir) { closedir((DIR*)ff->dir); ff->dir = NULL; }
            memset(ff, 0, sizeof *ff);
            return 1;
        }
    }
    /* G42: handles de thread — join/conclusão + liberação de recursos */
    {
        int ti = w32_thread_slot(ctx, a[0]);
        if (ti >= 0) {
            w32_thread_cleanup(&ctx->threads[ti]);
            return 1;
        }
    }
    /* pseudo-handles: GetCurrentThread (-2) e GetCurrentProcess (-1) */
    if (a[0]==(uint64_t)(int64_t)-1 || a[0]==(uint64_t)(int64_t)-2) return 1;
    /* std handles: fechamento é no-op bem-sucedido (não encerra o stdio) */
    switch (a[0]) {
        case PR_WIN32_H_STDIN: case PR_WIN32_H_STDOUT: case PR_WIN32_H_STDERR:
            return 1;
        default: break;
    }
    ctx->last_error = W32_ERROR_INVALID_HANDLE;   /* handles GDI usam DeleteObject */
    return 0;
}

static uint64_t f_GetFileSize(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return W32_INVALID_SET_FILE_PTR; }
    int k = w32_is_file_handle(a[0]) ? w32_file_idx(a[0]) : -1;
    if (k < 0 || !ctx->files[k]) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return W32_INVALID_SET_FILE_PTR; }
    FILE* f = ctx->files[k];
    long cur = ftell(f);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, cur, SEEK_SET);
    if (n >= 2 && a[1]) {
        /* ABI Win32: lpFileSizeHigh = LPDWORD (4 bytes); NULL preservado acima */
        uint32_t* hi = (uint32_t*)pr_win32_ptr(ctx, a[1], sizeof(uint32_t));
        if (!hi) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return W32_INVALID_SET_FILE_PTR; }
        *hi = 0;
    }
    return (uint64_t)(uint32_t)sz;
}

static uint64_t f_SetFilePointer(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return W32_INVALID_SET_FILE_PTR; }
    int k = w32_is_file_handle(a[0]) ? w32_file_idx(a[0]) : -1;
    if (k < 0 || !ctx->files[k]) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return W32_INVALID_SET_FILE_PTR; }
    FILE* f = ctx->files[k];
    long dist = (long)(int32_t)(uint32_t)a[1];   /* LONG */
    if (n >= 3 && a[2]) {
        const uint64_t* hi = (const uint64_t*)pr_win32_ptr(ctx, a[2], sizeof(uint64_t));
        if (!hi) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return W32_INVALID_SET_FILE_PTR; }
        dist |= (long)((*hi) << 32);
    }
    int whence;
    switch ((uint32_t)a[3]) {
        case W32_SEEK_BEGIN: whence = SEEK_SET; break;
        case W32_SEEK_CUR:   whence = SEEK_CUR; break;
        case W32_SEEK_END:   whence = SEEK_END; break;
        default: ctx->last_error = W32_ERROR_INVALID_PARAMETER; return W32_INVALID_SET_FILE_PTR;
    }
    if (fseek(f, dist, whence) != 0) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return W32_INVALID_SET_FILE_PTR; }
    return (uint64_t)(uint32_t)ftell(f);
}

/* ---- FASE 6: LoadLibrary/FreeLibrary (módulos reais via modops) ---- */

static uint64_t load_module_by_name(pr_win32_ctx* ctx, const char* name) {
    uint64_t h = module_handle(name);          /* embutidos: sempre carregados */
    if (h) return h;
    if (ctx->modops.load) {
        h = ctx->modops.load(ctx->modops.ud, name);
        if (h) return h;
    }
    ctx->last_error = W32_ERROR_MOD_NOT_FOUND;
    return 0;
}

static uint64_t f_LoadLibraryA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char name[64];
    size_t i = 0;
    while (p[i] && i < sizeof(name) - 1 && pr_win32_ptr(ctx, a[0] + i, 1)) {
        name[i] = p[i]; i++;
    }
    name[i] = 0;
    return load_module_by_name(ctx, name);
}

static uint64_t f_LoadLibraryW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    /* UTF-16 → ASCII (subset honesto; não-ASCII é recusado, nunca adivinhado) */
    char name[64];
    size_t i = 0;
    while (i < sizeof(name) - 1) {
        const uint16_t* w = (const uint16_t*)pr_win32_ptr(ctx, a[0] + (uint64_t)i * 2, 2);
        if (!w || *w == 0) break;
        if (*w > 127) {
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return 0;
        }
        name[i] = (char)*w;
        i++;
    }
    name[i] = 0;
    return load_module_by_name(ctx, name);
}

static uint64_t f_FreeLibrary(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    switch ((uint32_t)a[0]) {
        case W32_H_KERNEL32: case W32_H_USER32: case W32_H_ADVAPI32:
        case W32_H_WS2_32:   case W32_H_GDI32:  case W32_H_OLE32:
        case W32_H_SHELL32:
            return 1;   /* embutidos: handle estável; não há descarga (documentado) */
        default: break;
    }
    if (ctx->modops.free && ctx->modops.free(ctx->modops.ud, a[0])) return 1;
    ctx->last_error = W32_ERROR_INVALID_HANDLE;
    return 0;
}

void pr_win32_set_guestcall(pr_win32_ctx* ctx, pr_win32_guestcall_fn fn, void* ud) {
    if (!ctx) return;
    ctx->guest_call = fn;
    ctx->guest_ud = ud;
}

void pr_win32_set_modops(pr_win32_ctx* ctx, const pr_win32_modops* ops) {
    if (!ctx) return;
    if (ops) ctx->modops = *ops;
    else memset(&ctx->modops, 0, sizeof(ctx->modops));
}

pr_status pr_win32_set_fs_root(pr_win32_ctx* ctx, const char* host_dir) {
    if (!ctx || !host_dir || !host_dir[0]) return PR_ERR_INVALID;
    ctx->fs_sink = ctx->log;
    snprintf(ctx->fs_root, sizeof(ctx->fs_root), "%s", host_dir);
    size_t l = strlen(ctx->fs_root);
    while (l > 1 && ctx->fs_root[l - 1] == '/') ctx->fs_root[--l] = 0;
    return PR_OK;
}

static uint64_t f_GetModuleHandleA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1 || a[0] == 0) {
        /* módulo principal = imagem PE carregada (0 se nenhuma) */
        if (ctx->vm) return ctx->guest_img_base;
        return (uint64_t)(uintptr_t)ctx->img_base;
    }
    char name[64];
    if (f_lstrlenA(ctx, a, 1, st) == 0 && !pr_win32_ptr(ctx, a[0], 1)) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    size_t i = 0;
    while (p && p[i] && i < sizeof(name) - 1 && pr_win32_ptr(ctx, a[0] + i, 1)) {
        name[i] = p[i]; i++;
    }
    name[i] = 0;
    uint64_t h = module_handle(name);
    if (h == 0 && ctx->modops.find) h = ctx->modops.find(ctx->modops.ud, name);
    if (h == 0) ctx->last_error = W32_ERROR_MOD_NOT_FOUND;
    return h;
}

static uint64_t f_GetModuleFileNameA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    /* hModule: NULL = modulo principal; a base da imagem tambem e aceita.
     * Outros handles nao sao resolvidos nesta versao (menor subconjunto). */
    if (a[0] != 0 && a[0] != (uint64_t)ctx->guest_img_base &&
        a[0] != (uint64_t)(uintptr_t)ctx->img_base) {
        ctx->last_error = W32_ERROR_MOD_NOT_FOUND;
        return 0;
    }
    const char* name = ctx->module_name;
    if (!name[0]) { ctx->last_error = W32_ERROR_MOD_NOT_FOUND; return 0; }
    uint32_t cap = (uint32_t)a[2];
    if (cap == 0) return 0;
    char* dst = (char*)pr_win32_ptr(ctx, a[1], cap);
    if (!dst) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t len = 0;
    while (name[len]) len++;
    /* Portico: sempre NUL-termina; trunca em cap-1; retorna chars sem NUL */
    size_t cpy = len < (size_t)(cap - 1) ? len : (size_t)(cap - 1);
    for (size_t i = 0; i < cpy; i++) dst[i] = name[i];
    dst[cpy] = 0;
    return (uint64_t)cpy;
}

static uint64_t f_GetModuleFileNameW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* G78: GetModuleFileNameW — variante Unicode.
     * Contrato: DWORD GetModuleFileNameW(HMODULE hModule, LPWSTR lpFilename, DWORD nSize);
     * nSize = capacidade em WCHARs (inclui NUL). Retorna chars sem NUL, trunca em nSize-1. */
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    if (a[0] != 0 && a[0] != (uint64_t)ctx->guest_img_base &&
        a[0] != (uint64_t)(uintptr_t)ctx->img_base) {
        ctx->last_error = W32_ERROR_MOD_NOT_FOUND;
        return 0;
    }
    const char* name = ctx->module_name;
    if (!name[0]) { ctx->last_error = W32_ERROR_MOD_NOT_FOUND; return 0; }
    uint32_t cap = (uint32_t)a[2];
    if (cap == 0) return 0;
    uint8_t* wdst = (uint8_t*)pr_win32_ptr(ctx, a[1], (size_t)cap * 2);
    if (!wdst) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t len = 0;
    while (name[len]) len++;
    size_t cpy = len < (size_t)(cap - 1) ? len : (size_t)(cap - 1);
    for (size_t i = 0; i < cpy; i++) {
        wdst[2*i] = (uint8_t)name[i];
        wdst[2*i+1] = 0;
    }
    wdst[2*cpy] = 0;
    wdst[2*cpy+1] = 0;
    return (uint64_t)cpy;
}

static uint64_t f_GetStartupInfoA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (a[0] == 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    /* STARTUPINFOA x64 = 104 bytes (layout verificado no toolchain MinGW-w64) */
    uint8_t* si = (uint8_t*)pr_win32_ptr(ctx, a[0], 104);
    if (!si) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    memset(si, 0, 104);
    si[0] = 104;                       /* cb = sizeof(STARTUPINFOA) */
    /* lpReserved/lpDesktop/lpTitle = NULL; dwX..dwFillAttribute = 0;
     * wShowWindow = 0; cbReserved2 = 0; lpReserved2 = 0 (sem infra de startup) */
    uint32_t fl = 0x100;               /* STARTF_USESTDHANDLES */
    memcpy(si + 60, &fl, 4);
    uint64_t hin = PR_WIN32_H_STDIN, hout = PR_WIN32_H_STDOUT, herr = PR_WIN32_H_STDERR;
    memcpy(si + 80, &hin, 8);          /* hStdInput  */
    memcpy(si + 88, &hout, 8);         /* hStdOutput */
    memcpy(si + 96, &herr, 8);         /* hStdError  */
    return 0;                          /* VOID: sem retorno em EAX */
}

static uint64_t f_GetFileAttributesA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (a[0] == 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0xFFFFFFFFull; }
    /* string guest validada byte a byte (padrao f_CreateFileA) */
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0xFFFFFFFFull; }
    char win[192];
    size_t i = 0;
    while (p[i] && i < sizeof(win) - 1 && pr_win32_ptr(ctx, a[0] + i, 1)) { win[i] = p[i]; i++; }
    win[i] = 0;
    char host[256];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) {
        ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED
                                          : W32_ERROR_PATH_NOT_FOUND;
        return 0xFFFFFFFFull;
    }
    struct stat stb;
    if (stat(host, &stb) != 0) {
        ctx->last_error = W32_ERROR_FILE_NOT_FOUND;
        return 0xFFFFFFFFull;
    }
    /* mapeamento minimo: diretorio=0x10, arquivo=0x80 (NORMAL) */
    return S_ISDIR(stb.st_mode) ? 0x10ull : 0x80ull;
}

static uint64_t f_GetFileAttributesW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* G79: GetFileAttributesW — variante Unicode */
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0xFFFFFFFFull; }
    if (a[0] == 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0xFFFFFFFFull; }
    char win[192];
    wstr_to_ascii(ctx, a[0], win, sizeof(win));
    if (!win[0]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0xFFFFFFFFull; }
    char host[256];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) {
        ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
        return 0xFFFFFFFFull;
    }
    struct stat stb;
    if (stat(host, &stb) != 0) {
        ctx->last_error = W32_ERROR_FILE_NOT_FOUND;
        return 0xFFFFFFFFull;
    }
    return S_ISDIR(stb.st_mode) ? 0x10ull : 0x80ull;
}

static uint64_t f_DeleteFileA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (a[0] == 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    /* string guest validada byte a byte (padrao f_CreateFileA/f_GetFileAttributesA) */
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char win[192];
    size_t i = 0;
    while (p[i] && i < sizeof(win) - 1 && pr_win32_ptr(ctx, a[0] + i, 1)) { win[i] = p[i]; i++; }
    win[i] = 0;
    char host[256];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) {
        ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED
                                          : W32_ERROR_PATH_NOT_FOUND;
        return 0;
    }
    struct stat stb;
    if (stat(host, &stb) != 0) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 0; }
    if (S_ISDIR(stb.st_mode)) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    if (unlink(host) != 0) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    return 1;                          /* BOOL TRUE */
}

static uint64_t f_DeleteFileW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* G80: DeleteFileW — variante Unicode */
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (a[0] == 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char win[192];
    wstr_to_ascii(ctx, a[0], win, sizeof(win));
    if (!win[0]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char host[256];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) {
        ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
        return 0;
    }
    struct stat stb;
    if (stat(host, &stb) != 0) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 0; }
    if (S_ISDIR(stb.st_mode)) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    if (unlink(host) != 0) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    return 1;
}

#define W32_ATTR_DATA_SIZE 36
static int w32_fill_attr_data(pr_win32_ctx* ctx, uint64_t out_va, struct stat* stb) {
    uint8_t* base = (uint8_t*)pr_win32_ptr(ctx, out_va, W32_ATTR_DATA_SIZE);
    if (!base) return 0;
    memset(base, 0, W32_ATTR_DATA_SIZE);
    uint32_t attr = S_ISDIR(stb->st_mode) ? W32_FILE_ATTRIBUTE_DIRECTORY : W32_FILE_ATTRIBUTE_NORMAL;
    memcpy(base + 0, &attr, 4);
    uint32_t low, high;
    w32_filetime_from_time_t(stb->st_mtime, &low, &high);
    memcpy(base + 4, &low, 4);
    memcpy(base + 8, &high, 4);
    memcpy(base + 12, &low, 4);
    memcpy(base + 16, &high, 4);
    memcpy(base + 20, &low, 4);
    memcpy(base + 24, &high, 4);
    uint32_t size_low = (uint32_t)(stb->st_size & 0xFFFFFFFFULL);
    uint32_t size_high = (uint32_t)((stb->st_size >> 32) & 0xFFFFFFFFULL);
    if (S_ISDIR(stb->st_mode)) { size_low = 0; size_high = 0; }
    memcpy(base + 28, &size_high, 4);
    memcpy(base + 32, &size_low, 4);
    return 1;
}

static uint64_t f_GetFileAttributesExA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0 || a[2]==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if ((uint32_t)a[1] != 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; } /* only standard */
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char win[192]; size_t i=0;
    while (p[i] && i < sizeof(win)-1 && pr_win32_ptr(ctx, a[0]+i,1)) { win[i]=p[i]; i++; } win[i]=0;
    char host[256];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) {
        ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
        return 0;
    }
    struct stat stb;
    if (stat(host, &stb)!=0) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 0; }
    if (!w32_fill_attr_data(ctx, a[2], &stb)) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    return 1;
}
static uint64_t f_GetFileAttributesExW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0 || a[2]==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if ((uint32_t)a[1] != 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char win[192];
    wstr_to_ascii(ctx, a[0], win, sizeof(win));
    if (!win[0]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char host[256];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) {
        ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
        return 0;
    }
    struct stat stb;
    if (stat(host, &stb)!=0) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 0; }
    if (!w32_fill_attr_data(ctx, a[2], &stb)) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    return 1;
}
static uint64_t f_CreateDirectoryA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if (a[1]!=0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; } /* security not supported */
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char win[192]; size_t i=0;
    while (p[i] && i < sizeof(win)-1 && pr_win32_ptr(ctx, a[0]+i,1)) { win[i]=p[i]; i++; } win[i]=0;
    char host[256];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) {
        ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
        return 0;
    }
    struct stat stb;
    if (stat(host, &stb)==0) { ctx->last_error = W32_ERROR_ALREADY_EXISTS; return 0; }
    if (mkdir(host, 0777)!=0) { ctx->last_error = W32_ERROR_PATH_NOT_FOUND; return 0; }
    return 1;
}
static uint64_t f_CreateDirectoryW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if (a[1]!=0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char win[192];
    wstr_to_ascii(ctx, a[0], win, sizeof(win));
    if (!win[0]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char host[256];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) {
        ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
        return 0;
    }
    struct stat stb;
    if (stat(host, &stb)==0) { ctx->last_error = W32_ERROR_ALREADY_EXISTS; return 0; }
    if (mkdir(host, 0777)!=0) { ctx->last_error = W32_ERROR_PATH_NOT_FOUND; return 0; }
    return 1;
}
static uint64_t f_RemoveDirectoryA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char win[192]; size_t i=0;
    while (p[i] && i < sizeof(win)-1 && pr_win32_ptr(ctx, a[0]+i,1)) { win[i]=p[i]; i++; } win[i]=0;
    char host[256];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) {
        ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
        return 0;
    }
    struct stat stb;
    if (stat(host, &stb)!=0) { ctx->last_error = W32_ERROR_PATH_NOT_FOUND; return 0; }
    if (!S_ISDIR(stb.st_mode)) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    if (rmdir(host)!=0) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    return 1;
}
static uint64_t f_RemoveDirectoryW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char win[192];
    wstr_to_ascii(ctx, a[0], win, sizeof(win));
    if (!win[0]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char host[256];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) {
        ctx->last_error = ctx->fs_root[0] ? W32_ERROR_ACCESS_DENIED : W32_ERROR_PATH_NOT_FOUND;
        return 0;
    }
    struct stat stb;
    if (stat(host, &stb)!=0) { ctx->last_error = W32_ERROR_PATH_NOT_FOUND; return 0; }
    if (!S_ISDIR(stb.st_mode)) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    if (rmdir(host)!=0) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    return 1;
}

static uint64_t f_TlsAlloc(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)a; (void)n;
    *st = PR_OK;
    /* 0..59 alocaveis; 60..63 reservados internos (lconv/strerror/errno/iob) */
    for (uint64_t i = 0; i < 60; i++) {
        if (!(ctx->tls_used & (1ull << i))) {
            ctx->tls_used |= 1ull << i;
            return i;                      /* DWORD: indice TLS livre */
        }
    }
    ctx->last_error = W32_ERROR_NOT_ENOUGH_MEMORY;
    return 0xFFFFFFFFull;                  /* TLS_OUT_OF_INDEXES */
}

static uint64_t f_TlsSetValue(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    uint64_t idx = a[0];
    /* Opcao B: exige indice alocado por TlsAlloc (bit em tls_used);
     * 60..63 reservados do CRT (lconv/strerror/errno/iob) nunca aceitos;
     * erro da familia TLS (6 = ERROR_INVALID_HANDLE, mesmo de TlsGetValue) */
    if (idx > 59 || !(ctx->tls_used & (1ull << idx))) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;                          /* BOOL FALSE */
    }
    ctx->tls_slots[idx] = a[1];            /* LPVOID 64-bit intacto */
    return 1;                              /* BOOL TRUE */
}

static uint64_t f_TlsFree(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint64_t idx = a[0];
    /* 60..63 reservados do CRT (lconv/strerror/errno/iob) nunca liberados;
     * exige indice alocado (bit em tls_used) — mesma convencao de TlsSetValue;
     * erro da familia TLS (6 = ERROR_INVALID_HANDLE) */
    if (idx > 59 || !(ctx->tls_used & (1ull << idx))) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;                          /* BOOL FALSE */
    }
    ctx->tls_used &= ~(1ull << idx);       /* libera o indice (reusavel) */
    ctx->tls_slots[idx] = 0;               /* sem lixo observavel */
    return 1;                              /* BOOL TRUE */
}

static uint64_t f_GetProcAddress(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    /* mapa handle → módulo */
    const char* mod = NULL;
    switch ((uint32_t)a[0]) {
        case W32_H_KERNEL32: mod = "kernel32.dll"; break;
        case W32_H_USER32:   mod = "user32.dll"; break;
        case W32_H_ADVAPI32: mod = "advapi32.dll"; break;
        case W32_H_WS2_32:   mod = "ws2_32.dll"; break;
        case W32_H_GDI32:    mod = "gdi32.dll"; break;
        case W32_H_OLE32:    mod = "ole32.dll"; break;
        case W32_H_SHELL32:  mod = "shell32.dll"; break;
        default: break;
    }
    if (!mod) {
        /* handle de DLL carregada (FASE 6): resolve via exports reais */
        if (ctx->modops.proc) {
            uint64_t r;
            if (a[1] < 0x10000) {
                r = ctx->modops.proc(ctx->modops.ud, a[0], NULL, (uint32_t)a[1]);
            } else {
                char nm[64];
                const char* p2 = (const char*)pr_win32_ptr(ctx, a[1], 1);
                if (!p2) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
                size_t j = 0;
                while (p2[j] && j < sizeof(nm) - 1 && pr_win32_ptr(ctx, a[1] + j, 1)) {
                    nm[j] = p2[j]; j++;
                }
                nm[j] = 0;
                r = ctx->modops.proc(ctx->modops.ud, a[0], nm, 0);
            }
            if (r) return r;
        }
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    /* forma por ordinal (MAKEINTRESOURCE): valor < 0x10000 */
    if (a[1] < 0x10000) {
        const pr_win32_export* eo =
            pr_win32_lookup_ordinal(mod, (uint32_t)a[1]);
        if (!eo || eo->status != PR_WIN32_IMPLEMENTED) {
            ctx->last_error = W32_ERROR_PROC_NOT_FOUND;
            return 0;
        }
        size_t io = pr_win32_index_of(eo);
        return (io < ctx->thunk_count) ? ctx->thunk_addrs[io] : 0;
    }
    char name[64];
    const char* p = (const char*)pr_win32_ptr(ctx, a[1], 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    size_t i = 0;
    while (p[i] && i < sizeof(name) - 1 && pr_win32_ptr(ctx, a[1] + i, 1)) {
        name[i] = p[i]; i++;
    }
    name[i] = 0;
    const pr_win32_export* e = pr_win32_lookup(mod, name);
    if (!e || e->status != PR_WIN32_IMPLEMENTED) {
        ctx->last_error = W32_ERROR_PROC_NOT_FOUND;
        return 0;
    }
    size_t idx = pr_win32_index_of(e);
    /* processo PE: retorna o endereço GUEST do stub stdcall (thunk real) */
    if (ctx->vm && ctx->thunk_addrs && idx < ctx->thunk_count) {
        return ctx->thunk_addrs[idx];
    }
    /* sem vm: token de ligação estável (entrada do catálogo) — NÃO é código. */
    return (uint64_t)(uintptr_t)e;
}

static uint64_t f_GetCommandLineA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)a; (void)n; *st = PR_OK;
    if (ctx->vm && ctx->guest_cmdline) return ctx->guest_cmdline;
    return (uint64_t)(uintptr_t)ctx->scratch; /* scratch[0..] contém a linha */
}

/* ---- helpers para as APIs expandidas (FASE 3) ---- */

/* protótipos: helpers GDI são definidos mais abaixo neste arquivo */
static gdi_obj* gdi_find(pr_win32_ctx* ctx, uint32_t handle, uint32_t kind);
static pr_surf* gdi_target(pr_win32_ctx* ctx, const gdi_obj* dc);

static int env_ieq(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void ansi_to_buf(pr_win32_ctx* ctx, uint64_t addr, char* dst, size_t cap) {
    size_t i = 0;
    dst[0] = 0;
    if (!addr) return;
    while (i + 1 < cap) {
        const char* p = (const char*)pr_win32_ptr(ctx, addr + i, 1);
        if (!p || !*p) break;
        dst[i] = *p;
        i++;
    }
    dst[i] = 0;
}

static void wstr_to_ascii(pr_win32_ctx* ctx, uint64_t addr, char* dst, size_t cap) {
    size_t i = 0;
    dst[0] = 0;
    if (!addr) return;
    while (i + 1 < cap) {
        const uint8_t* p = (const uint8_t*)pr_win32_ptr(ctx, addr + 2 * i, 2);
        if (!p) break;
        uint16_t u = (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
        if (u == 0) break;
        dst[i] = (u < 128) ? (char)u : '?';
        i++;
    }
    dst[i] = 0;
}

/* COLORREF 0x00BBGGRR <-> XRGB interno 0x00RRGGBB */
static uint32_t cr_to_xrgb(uint32_t cr) {
    return ((cr & 0xFFu) << 16) | (cr & 0xFF00u) | ((cr >> 16) & 0xFFu);
}
static uint32_t xrgb_to_cr(uint32_t x) {
    return ((x & 0xFFu) << 16) | (x & 0xFF00u) | ((x >> 16) & 0xFFu);
}

/* PAGE_* -> PR_VM_PROT (-1 = máscara inválida) */
static int page_to_prot(uint32_t p) {
    switch (p & 0xFFu) {
        case 0x01u: return 0;
        case 0x02u: return PR_VM_PROT_R;
        case 0x04u: case 0x08u: return PR_VM_PROT_R | PR_VM_PROT_W;
        case 0x10u: return PR_VM_PROT_X;
        case 0x20u: return PR_VM_PROT_R | PR_VM_PROT_X;
        case 0x40u: case 0x80u: return PR_VM_PROT_R | PR_VM_PROT_W | PR_VM_PROT_X;
        default: return -1;
    }
}
static uint32_t prot_to_page(int prot) {
    if (prot == 0) return 0x01u;
    if (prot == PR_VM_PROT_R) return 0x02u;
    if (prot == (PR_VM_PROT_R | PR_VM_PROT_X)) return 0x20u;
    if (prot == (PR_VM_PROT_R | PR_VM_PROT_W | PR_VM_PROT_X)) return 0x40u;
    if (prot == PR_VM_PROT_X) return 0x10u;
    return 0x04u;
}

static uint64_t f_GetModuleHandleW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1 || a[0] == 0) {
        if (ctx->vm) return ctx->guest_img_base;
        return (uint64_t)(uintptr_t)ctx->img_base;
    }
    char name[64];
    wstr_to_ascii(ctx, a[0], name, sizeof(name));
    if (!name[0]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    uint64_t h = module_handle(name);
    if (h == 0 && ctx->modops.find) h = ctx->modops.find(ctx->modops.ud, name);
    if (h == 0) ctx->last_error = W32_ERROR_MOD_NOT_FOUND;
    return h;
}

static uint64_t f_GetCommandLineW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)a; (void)n; *st = PR_OK;
    if (ctx->vm && ctx->guest_cmdline_w) return ctx->guest_cmdline_w;
    const char* s = (const char*)ctx->scratch;
    uint8_t* w = ctx->scratch + 256;
    size_t i = 0;
    while (s[i] && i < 120) { w[2 * i] = (uint8_t)s[i]; w[2 * i + 1] = 0; i++; }
    w[2 * i] = 0; w[2 * i + 1] = 0;
    return (uint64_t)(uintptr_t)w;
}

static uint64_t f_GetEnvironmentVariableA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    char name[64];
    ansi_to_buf(ctx, a[0], name, sizeof(name));
    if (!name[0]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    const char* val = NULL;
    for (size_t k = 0; k < ctx->env_count; k++) {
        if (env_ieq(ctx->env_names[k], name)) { val = ctx->env_vals[k]; break; }
    }
    if (!val) { ctx->last_error = W32_ERROR_ENVVAR_NOT_FOUND; return 0; }
    size_t need = strlen(val) + 1;
    size_t cap = (size_t)a[2];
    if (a[1] == 0 || cap < need) {
        ctx->last_error = W32_ERROR_INSUFFICIENT_BUFFER;
        return (uint64_t)need;
    }
    char* buf = (char*)pr_win32_ptr(ctx, a[1], need);
    if (!buf) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    memcpy(buf, val, need);
    return (uint64_t)(need - 1);
}

static uint64_t f_GetEnvironmentVariableW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    char name[64];
    wstr_to_ascii(ctx, a[0], name, sizeof(name));
    if (!name[0]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    const char* val = NULL;
    for (size_t k = 0; k < ctx->env_count; k++) {
        if (env_ieq(ctx->env_names[k], name)) { val = ctx->env_vals[k]; break; }
    }
    if (!val) { ctx->last_error = W32_ERROR_ENVVAR_NOT_FOUND; return 0; }
    size_t chars = strlen(val) + 1;          /* inclui NUL */
    size_t cap = (size_t)a[2];                /* capacidade em CARACTERES */
    if (a[1] == 0 || cap < chars) {
        ctx->last_error = W32_ERROR_INSUFFICIENT_BUFFER;
        return (uint64_t)chars;
    }
    uint8_t* buf = (uint8_t*)pr_win32_ptr(ctx, a[1], chars * 2);
    if (!buf) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    for (size_t i = 0; i < chars; i++) {
        buf[2 * i] = (uint8_t)val[i];
        buf[2 * i + 1] = 0;
    }
    return (uint64_t)(chars - 1);
}

static uint64_t f_GetSystemInfo(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1 || a[0] == 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    uint8_t* info = (uint8_t*)pr_win32_ptr(ctx, a[0], 48);
    if (!info) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    memset(info, 0, 48);
    uint32_t max_addr = ctx->vm ? (uint32_t)(pr_vm_space(ctx->vm) - 1) : 0x00EFFFFFu;
    uint32_t page = 4096, nproc = 1, ptype = 8664, gran = 65536;
    uint64_t min_addr = 0x10000ull, maxa = max_addr, mask = 1ull;
    info[0] = 9; info[1] = 0;                    /* PROCESSOR_ARCHITECTURE_AMD64 */
    memcpy(info + 4, &page, 4);
    memcpy(info + 8, &min_addr, 8);
    memcpy(info + 16, &maxa, 8);
    memcpy(info + 24, &mask, 8);
    memcpy(info + 32, &nproc, 4);
    memcpy(info + 36, &ptype, 4);
    memcpy(info + 40, &gran, 4);
    info[44] = 15; info[45] = 0;                 /* wProcessorLevel=15 */
    return 1;
}

static uint64_t f_VirtualProtect(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    uint64_t addr64 = a[0];
    uint32_t size = (uint32_t)a[1];
    int newp = page_to_prot((uint32_t)a[2]);
    if (newp < 0 || size == 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    uint32_t oldpage;
    if (ctx->vm) {
        uint32_t addr = (uint32_t)addr64;
        pr_vm_region regs[PR_VM_MAX_REGIONS];
        size_t nr = pr_vm_regions(ctx->vm, regs, PR_VM_MAX_REGIONS);
        const pr_vm_region* found = NULL;
        for (size_t i = 0; i < nr; i++) {
            if (regs[i].base <= addr && addr + size <= regs[i].base + regs[i].size) {
                found = &regs[i];
                break;
            }
        }
        if (!found) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
        oldpage = prot_to_page(found->prot);
        if (pr_vm_protect(ctx->vm, addr, size, newp) != PR_OK) {
            ctx->last_error = W32_ERROR_INVALID_PARAMETER;
            return 0;
        }
    } else {
        oldpage = 0x04u;                         /* blocos host nascem PAGE_READWRITE */
        int ok = 0;
        for (size_t i = 0; i < ctx->nblocks && !ok; i++) {
            uint8_t* b = (uint8_t*)ctx->blocks[i].p;
            if ((uint8_t*)(uintptr_t)addr64 >= b &&
                (uint8_t*)(uintptr_t)addr64 + size <= b + ctx->blocks[i].size) ok = 1;
        }
        if (!ok) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    }
    if (a[3]) {
        uint32_t* out = (uint32_t*)pr_win32_ptr(ctx, a[3], 4);
        if (!out) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
        *out = oldpage;
    }
    return 1;
}

static uint64_t f_GetCurrentDirectoryA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    const char* cwd = ctx->cwd[0] ? ctx->cwd : "\\";
    size_t need = strlen(cwd) + 1;
    size_t cap = (size_t)a[0];
    if (a[1] == 0 || cap < need) {
        ctx->last_error = W32_ERROR_INSUFFICIENT_BUFFER;
        return (uint64_t)need;
    }
    char* buf = (char*)pr_win32_ptr(ctx, a[1], need);
    if (!buf) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    memcpy(buf, cwd, need);
    return (uint64_t)(need - 1);
}

static uint64_t f_GetCurrentDirectoryW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* G77: GetCurrentDirectoryW — variante Unicode de GetCurrentDirectoryA.
     * Contrato: DWORD GetCurrentDirectoryW(DWORD nBufferLength, LPWSTR lpBuffer);
     * nBufferLength = capacidade em WCHARs (inclui NUL). Retorna chars sem NUL,
     * ou need (com NUL) se buffer pequeno, ou 0 em falha. */
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    const char* cwd = ctx->cwd[0] ? ctx->cwd : "\\";
    size_t need_chars = strlen(cwd) + 1; /* inclui NUL */
    size_t cap = (size_t)a[0];
    if (a[1] == 0 || cap < need_chars) {
        ctx->last_error = W32_ERROR_INSUFFICIENT_BUFFER;
        return (uint64_t)need_chars;
    }
    uint8_t* wbuf = (uint8_t*)pr_win32_ptr(ctx, a[1], need_chars * 2);
    if (!wbuf) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    for (size_t i = 0; i < need_chars; i++) {
        wbuf[2*i] = (uint8_t)cwd[i];
        wbuf[2*i+1] = 0;
    }
    return (uint64_t)(need_chars - 1);
}

/* G86: Filesystem avançado */
static uint64_t f_SetCurrentDirectoryA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char win[192]; size_t i=0;
    while (p[i] && i < sizeof(win)-1 && pr_win32_ptr(ctx, a[0]+i,1)) { win[i]=p[i]; i++; } win[i]=0;
    if (!win[0]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char host[512];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) { ctx->last_error = W32_ERROR_PATH_NOT_FOUND; return 0; }
    struct stat stb; if (stat(host,&stb)!=0 || !S_ISDIR(stb.st_mode)) { ctx->last_error = W32_ERROR_PATH_NOT_FOUND; return 0; }
    size_t cw = sizeof(ctx->cwd)-1;
    if (strlen(win) > cw) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    snprintf(ctx->cwd, sizeof(ctx->cwd), "%s", win);
    return 1;
}
static uint64_t f_SetCurrentDirectoryW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char win[192]; wstr_to_ascii(ctx, a[0], win, sizeof(win));
    if (!win[0]) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char host[512];
    if (!vfs_resolve(ctx, win, host, sizeof(host))) { ctx->last_error = W32_ERROR_PATH_NOT_FOUND; return 0; }
    struct stat stb; if (stat(host,&stb)!=0 || !S_ISDIR(stb.st_mode)) { ctx->last_error = W32_ERROR_PATH_NOT_FOUND; return 0; }
    size_t cw = sizeof(ctx->cwd)-1;
    if (strlen(win) > cw) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    snprintf(ctx->cwd, sizeof(ctx->cwd), "%s", win);
    return 1;
}
static uint64_t f_GetFullPathNameA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    uint64_t name_va = a[0];
    uint64_t nBufferLength = a[1];
    uint64_t lpBuffer_va = a[2];
    uint64_t lpFilePart_va = a[3];
    if (name_va==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    const char* p = (const char*)pr_win32_ptr(ctx, name_va, 1);
    if (!p) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char win[192]; size_t i=0;
    while (p[i] && i < sizeof(win)-1 && pr_win32_ptr(ctx, name_va+i,1)) { win[i]=p[i]; i++; } win[i]=0;
    char full[512];
    if (win[0]=='/' || win[0]=='\\' || (win[1]==':' )) {
        snprintf(full, sizeof(full), "%s", win);
    } else {
        const char* cwd = ctx->cwd[0] ? ctx->cwd : "";
        if (cwd[0]) snprintf(full, sizeof(full), "%s\\%s", cwd, win);
        else snprintf(full, sizeof(full), "%s", win);
    }
    size_t need = strlen(full)+1;
    if (lpBuffer_va==0 || nBufferLength < need) {
        if (lpFilePart_va) {
            /* file part not implemented fully, set null */
        }
        return (uint64_t)need-1;
    }
    char* out = (char*)pr_win32_ptr(ctx, lpBuffer_va, need);
    if (!out) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    memcpy(out, full, need);
    if (lpFilePart_va) {
        char* last = strrchr(full, '\\');
        if (!last) last = strrchr(full, '/');
        uint64_t* fp = (uint64_t*)pr_win32_ptr(ctx, lpFilePart_va, 8);
        if (fp) {
            if (last) *fp = lpBuffer_va + (last - full) + 1;
            else *fp = lpBuffer_va;
        }
    }
    return (uint64_t)need-1;
}
static uint64_t f_GetFullPathNameW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    uint64_t name_va = a[0];
    uint64_t nBufferLength = a[1];
    if (name_va==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char win[192]; wstr_to_ascii(ctx, name_va, win, sizeof(win));
    char full[512];
    if (win[0]=='/' || win[0]=='\\' || win[1]==':') snprintf(full, sizeof(full), "%s", win);
    else {
        const char* cwd = ctx->cwd[0] ? ctx->cwd : "";
        if (cwd[0]) snprintf(full, sizeof(full), "%s\\%s", cwd, win);
        else snprintf(full, sizeof(full), "%s", win);
    }
    size_t need = strlen(full)+1;
    if (a[2]==0 || nBufferLength < need) return (uint64_t)need-1;
    uint8_t* wbuf = (uint8_t*)pr_win32_ptr(ctx, a[2], need*2);
    if (!wbuf) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    for (size_t i=0;i<need;i++) { wbuf[2*i]=(uint8_t)full[i]; wbuf[2*i+1]=0; }
    return (uint64_t)need-1;
}
static uint64_t f_MoveFileA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0 || a[1]==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    const char* p0 = (const char*)pr_win32_ptr(ctx, a[0], 1);
    const char* p1 = (const char*)pr_win32_ptr(ctx, a[1], 1);
    if (!p0 || !p1) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char src[192], dst[192]; size_t i=0;
    while (p0[i] && i < sizeof(src)-1 && pr_win32_ptr(ctx, a[0]+i,1)) { src[i]=p0[i]; i++; } src[i]=0;
    i=0; while (p1[i] && i < sizeof(dst)-1 && pr_win32_ptr(ctx, a[1]+i,1)) { dst[i]=p1[i]; i++; } dst[i]=0;
    char hsrc[512], hdst[512];
    if (!vfs_resolve(ctx, src, hsrc, sizeof(hsrc)) || !vfs_resolve(ctx, dst, hdst, sizeof(hdst))) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    if (rename(hsrc, hdst)!=0) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 0; }
    return 1;
}
static uint64_t f_MoveFileW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0 || a[1]==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char src[192], dst[192];
    wstr_to_ascii(ctx, a[0], src, sizeof(src));
    wstr_to_ascii(ctx, a[1], dst, sizeof(dst));
    char hsrc[512], hdst[512];
    if (!vfs_resolve(ctx, src, hsrc, sizeof(hsrc)) || !vfs_resolve(ctx, dst, hdst, sizeof(hdst))) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    if (rename(hsrc, hdst)!=0) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 0; }
    return 1;
}
static uint64_t f_CopyFileA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0 || a[1]==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    const char* p0 = (const char*)pr_win32_ptr(ctx, a[0], 1);
    const char* p1 = (const char*)pr_win32_ptr(ctx, a[1], 1);
    if (!p0 || !p1) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char src[192], dst[192]; size_t i=0;
    while (p0[i] && i < sizeof(src)-1 && pr_win32_ptr(ctx, a[0]+i,1)) { src[i]=p0[i]; i++; } src[i]=0;
    i=0; while (p1[i] && i < sizeof(dst)-1 && pr_win32_ptr(ctx, a[1]+i,1)) { dst[i]=p1[i]; i++; } dst[i]=0;
    int failIfExists = (int)a[2];
    char hsrc[512], hdst[512];
    if (!vfs_resolve(ctx, src, hsrc, sizeof(hsrc)) || !vfs_resolve(ctx, dst, hdst, sizeof(hdst))) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    struct stat stb;
    if (failIfExists && stat(hdst,&stb)==0) { ctx->last_error = W32_ERROR_ALREADY_EXISTS; return 0; }
    FILE* fin = fopen(hsrc, "rb");
    if (!fin) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 0; }
    FILE* fout = fopen(hdst, "wb");
    if (!fout) { fclose(fin); ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    char buf[4096]; size_t r;
    while ((r=fread(buf,1,sizeof(buf),fin))>0) fwrite(buf,1,r,fout);
    fclose(fin); fclose(fout);
    return 1;
}
static uint64_t f_CopyFileW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    if (a[0]==0 || a[1]==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    char src[192], dst[192];
    wstr_to_ascii(ctx, a[0], src, sizeof(src));
    wstr_to_ascii(ctx, a[1], dst, sizeof(dst));
    int failIfExists = (int)a[2];
    char hsrc[512], hdst[512];
    if (!vfs_resolve(ctx, src, hsrc, sizeof(hsrc)) || !vfs_resolve(ctx, dst, hdst, sizeof(hdst))) { ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    struct stat stb;
    if (failIfExists && stat(hdst,&stb)==0) { ctx->last_error = W32_ERROR_ALREADY_EXISTS; return 0; }
    FILE* fin = fopen(hsrc, "rb");
    if (!fin) { ctx->last_error = W32_ERROR_FILE_NOT_FOUND; return 0; }
    FILE* fout = fopen(hdst, "wb");
    if (!fout) { fclose(fin); ctx->last_error = W32_ERROR_ACCESS_DENIED; return 0; }
    char buf[4096]; size_t r;
    while ((r=fread(buf,1,sizeof(buf),fin))>0) fwrite(buf,1,r,fout);
    fclose(fin); fclose(fout);
    return 1;
}
static uint64_t f_GetTempPathA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    uint64_t nBufferLength = a[0];
    uint64_t lpBuffer_va = a[1];
    const char* tmp = "C:\\Temp\\";
    size_t need = strlen(tmp)+1;
    if (lpBuffer_va==0 || nBufferLength < need) return (uint64_t)need-1;
    char* out = (char*)pr_win32_ptr(ctx, lpBuffer_va, need);
    if (!out) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    memcpy(out, tmp, need);
    return (uint64_t)need-1;
}
static uint64_t f_GetTempPathW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    uint64_t nBufferLength = a[0];
    const char* tmp = "C:\\Temp\\";
    size_t need = strlen(tmp)+1;
    if (a[1]==0 || nBufferLength < need) return (uint64_t)need-1;
    uint8_t* wbuf = (uint8_t*)pr_win32_ptr(ctx, a[1], need*2);
    if (!wbuf) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    for (size_t i=0;i<need;i++) { wbuf[2*i]=(uint8_t)tmp[i]; wbuf[2*i+1]=0; }
    return (uint64_t)need-1;
}
static uint64_t f_FlushFileBuffers(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint64_t h = a[0];
    int idx = w32_file_idx(h);
    if (idx<0) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    if (!ctx->files[idx]) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    fflush(ctx->files[idx]);
    return 1;
}
static uint64_t f_SetFilePointerEx(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    uint64_t h = a[0];
    int64_t liDistance = (int64_t)a[1];
    uint64_t lpNewFilePointer_va = a[2];
    uint64_t dwMoveMethod = a[3];
    int idx = w32_file_idx(h);
    if (idx<0 || !ctx->files[idx]) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    int whence = SEEK_SET;
    if (dwMoveMethod==1) whence=SEEK_CUR;
    else if (dwMoveMethod==2) whence=SEEK_END;
    if (fseek(ctx->files[idx], (long)liDistance, whence)!=0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    long pos = ftell(ctx->files[idx]);
    if (lpNewFilePointer_va) {
        int64_t* out = (int64_t*)pr_win32_ptr(ctx, lpNewFilePointer_va, 8);
        if (out) *out = (int64_t)pos;
    }
    return 1;
}
static uint64_t f_GetDiskFreeSpaceExA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    uint64_t freeBytesAvailable_va = a[1];
    uint64_t totalBytes_va = a[2];
    uint64_t totalFreeBytes_va = a[3];
    uint64_t freeAvail = 1024ULL*1024ULL*1024ULL;
    uint64_t total = 10ULL*1024ULL*1024ULL*1024ULL;
    if (freeBytesAvailable_va) {
        uint64_t* p = (uint64_t*)pr_win32_ptr(ctx, freeBytesAvailable_va, 8);
        if (p) *p = freeAvail;
    }
    if (totalBytes_va) {
        uint64_t* p = (uint64_t*)pr_win32_ptr(ctx, totalBytes_va, 8);
        if (p) *p = total;
    }
    if (totalFreeBytes_va) {
        uint64_t* p = (uint64_t*)pr_win32_ptr(ctx, totalFreeBytes_va, 8);
        if (p) *p = freeAvail;
    }
    return 1;
}
static uint64_t f_GetDiskFreeSpaceExW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    return f_GetDiskFreeSpaceExA(ctx, a, n, st);
}

static uint64_t f_DeleteDC(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    memset(dc, 0, sizeof(*dc));
    return 1;
}

static uint64_t f_SetPixel(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    pr_surf* tgt = gdi_target(ctx, dc);
    if (!tgt) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    int32_t x = (int32_t)(uint32_t)a[1], y = (int32_t)(uint32_t)a[2];
    uint32_t sw = pr_surf_width(tgt), sh = pr_surf_height(tgt);
    if (x < 0 || y < 0 || (uint32_t)x >= sw || (uint32_t)y >= sh) return 0xFFFFFFFFu;
    uint32_t* px = pr_surf_pixels(tgt);
    size_t pitch = pr_surf_pitch(tgt) / 4;
    uint32_t old = px[(size_t)y * pitch + (size_t)x];
    px[(size_t)y * pitch + (size_t)x] = cr_to_xrgb((uint32_t)a[3]);
    return xrgb_to_cr(old);
}

static uint64_t f_GetPixel(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    pr_surf* tgt = gdi_target(ctx, dc);
    if (!tgt) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    int32_t x = (int32_t)(uint32_t)a[1], y = (int32_t)(uint32_t)a[2];
    uint32_t sw = pr_surf_width(tgt), sh = pr_surf_height(tgt);
    if (x < 0 || y < 0 || (uint32_t)x >= sw || (uint32_t)y >= sh) return 0xFFFFFFFFu; /* CLR_INVALID */
    uint32_t* px = pr_surf_pixels(tgt);
    size_t pitch = pr_surf_pitch(tgt) / 4;
    return xrgb_to_cr(px[(size_t)y * pitch + (size_t)x]);
}

static uint64_t f_StretchBlt(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 11) {
        w32_log(ctx, PR_LOG_WARN, "gdi32!StretchBlt: argumentos insuficientes n=%u", (unsigned)n);
        *st = PR_ERR_INVALID;
        return 0;
    }
    gdi_obj* ddc = gdi_find(ctx, (uint32_t)a[0], 1);
    gdi_obj* sdc = gdi_find(ctx, (uint32_t)a[5], 1);
    if (!ddc || !sdc) {
        w32_log(ctx, PR_LOG_WARN, "gdi32!StretchBlt: DC invalido dst=0x%08X src=0x%08X",
                (uint32_t)a[0], (uint32_t)a[5]);
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    uint32_t rop = (uint32_t)a[10];
    if (rop != 0x00CC0020u) { /* SRCCOPY */
        w32_log(ctx, PR_LOG_WARN, "gdi32!StretchBlt: rop 0x%08X nao suportado (somente SRCCOPY)", rop);
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    pr_surf* dst = gdi_target(ctx, ddc);
    pr_surf* src = gdi_target(ctx, sdc);
    if (!dst || !src) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    int32_t dx = (int32_t)(uint32_t)a[1], dy = (int32_t)(uint32_t)a[2];
    int32_t dw0 = (int32_t)(uint32_t)a[3], dh0 = (int32_t)(uint32_t)a[4];
    int32_t sx0 = (int32_t)(uint32_t)a[6], sy0 = (int32_t)(uint32_t)a[7];
    int32_t sw0 = (int32_t)(uint32_t)a[8], sh0 = (int32_t)(uint32_t)a[9];
    if (dw0 <= 0 || dh0 <= 0 || sw0 <= 0 || sh0 <= 0) {
        w32_log(ctx, PR_LOG_WARN, "gdi32!StretchBlt: dimensoes invalidas dst %dx%d src %dx%d",
                (int)dw0, (int)dh0, (int)sw0, (int)sh0);
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    const uint32_t* sp = pr_surf_pixels(src);
    uint32_t* dp = pr_surf_pixels(dst);
    size_t spitch = pr_surf_pitch(src) / 4, dpitch = pr_surf_pitch(dst) / 4;
    int32_t sw = (int32_t)pr_surf_width(src), sh = (int32_t)pr_surf_height(src);
    int32_t dw = (int32_t)pr_surf_width(dst), dh = (int32_t)pr_surf_height(dst);
    /* vizinho mais próximo SRCCOPY (documentado em LIMITATIONS) */
    for (int32_t j = 0; j < dh0; j++) {
        int32_t y = dy + j;
        if (y < 0 || y >= dh) continue;
        int32_t sy = sy0 + (int32_t)(((int64_t)j * sh0) / dh0);
        if (sy < 0 || sy >= sh) continue;
        for (int32_t i = 0; i < dw0; i++) {
            int32_t x = dx + i;
            if (x < 0 || x >= dw) continue;
            int32_t sx = sx0 + (int32_t)(((int64_t)i * sw0) / dw0);
            if (sx < 0 || sx >= sw) continue;
            dp[(size_t)y * dpitch + (size_t)x] = sp[(size_t)sy * spitch + (size_t)sx];
        }
    }
    if (ctx->log) {
        pr_log_write(ctx->log, PR_LOG_INFO, "gdi",
                     "[GDI] StretchBlt src %dx%d (%d,%d) dst %dx%d (%d,%d) rop=SRCCOPY",
                     (int)sw0, (int)sh0, (int)sx0, (int)sy0,
                     (int)dw0, (int)dh0, (int)dx, (int)dy);
        if (dst == ctx->gdi_surface)
            pr_log_write(ctx->log, PR_LOG_INFO, "gdi", "[GDI] surface updated");
    }
    return 1;
}

static uint64_t f_MessageBoxA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    char text[96], caption[64];
    ansi_to_buf(ctx, a[1], text, sizeof(text));
    ansi_to_buf(ctx, a[2], caption, sizeof(caption));
    w32_log(ctx, PR_LOG_WARN,
            "user32!MessageBoxA sem UI neste build (sem janelas): caption=\"%s\" "
            "text=\"%s\" type=0x%08X -> retorna IDOK",
            caption, text, (uint32_t)a[3]);
    return 1; /* IDOK */
}

static uint64_t f_MessageBoxW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    char text[96], caption[64];
    wstr_to_ascii(ctx, a[1], text, sizeof(text));
    wstr_to_ascii(ctx, a[2], caption, sizeof(caption));
    w32_log(ctx, PR_LOG_WARN,
            "user32!MessageBoxW sem UI neste build (sem janelas): caption=\"%s\" "
            "text=\"%s\" type=0x%08X -> retorna IDOK",
            caption, text, (uint32_t)a[3]);
    return 1; /* IDOK */
}


static uint64_t f_OutputDebugStringA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    const char* p = (const char*)pr_win32_ptr(ctx, a[0], 1);
    if (p) w32_log(ctx, PR_LOG_DEBUG, "OutputDebugStringA: %.160s", p);
    return 0;
}

static uint64_t f_GetVersion(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)a; (void)n; *st = PR_OK;
    /* Versão declarada do ambiente de compatibilidade (documentada):
     * 5.0 build 2195 — Windows 2000 (alvo clássico de compatibilidade). */
    return (2195u << 16) | 5u;
}

static uint64_t f_GetVersionExA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    /* OSVERSIONINFOA: DWORD dwOSVersionInfoSize; DWORD major, minor, build, platform; */
    uint32_t* info = (uint32_t*)pr_win32_ptr(ctx, a[0], 5 * sizeof(uint32_t));
    if (!info) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    info[1] = 5;      /* major */
    info[2] = 0;      /* minor */
    info[3] = 2195;   /* build */
    info[4] = 2;      /* VER_PLATFORM_WIN32_NT */
    return 1;
}


/* ============================================================
 * GDI32 real: device context + pincel sobre a superfície virtual.
 * ============================================================ */

static gdi_obj* gdi_alloc(pr_win32_ctx* ctx, uint32_t base, uint32_t kind) {
    for (int i = 0; i < GDI_MAX; i++) {
        if (!ctx->gdi[i].used) {
            memset(&ctx->gdi[i], 0, sizeof(ctx->gdi[i]));
            ctx->gdi[i].used = 1;
            ctx->gdi[i].handle = base + (uint32_t)i;
            ctx->gdi[i].kind = kind;
            ctx->gdi[i].color = 0x00FFFFFF; /* branco */
            ctx->gdi[i].sel = W32_STOCK_WHITE_BRUSH;
            return &ctx->gdi[i];
        }
    }
    return NULL;
}

static gdi_obj* gdi_find(pr_win32_ctx* ctx, uint32_t handle, uint32_t kind) {
    for (int i = 0; i < GDI_MAX; i++) {
        if (ctx->gdi[i].used && ctx->gdi[i].handle == handle &&
            ctx->gdi[i].kind == kind)
            return &ctx->gdi[i];
    }
    return NULL;
}

static uint32_t gdi_brush_color(pr_win32_ctx* ctx, const gdi_obj* dc) {
    if (dc->sel == W32_STOCK_WHITE_BRUSH) return 0x00FFFFFF;
    gdi_obj* b = gdi_find(ctx, dc->sel, 2);
    return b ? b->color : 0x00FFFFFF;
}
static uint32_t gdi_pen_color(pr_win32_ctx* ctx, const gdi_obj* dc) {
    gdi_obj* p = gdi_find(ctx, dc->sel_pen, 4);
    return p ? p->color : 0x00000000;
}

/* Alvo de desenho do DC: bitmap selecionada ou superfície do processo. */
static pr_surf* gdi_target(pr_win32_ctx* ctx, const gdi_obj* dc) {
    if (!dc) return NULL;
    if (dc->sel_bmp) {
        gdi_obj* b = gdi_find(ctx, dc->sel_bmp, 3);
        if (b) return b->surf;
    }
    return ctx->gdi_surface;
}

/* Recorta um blit às bordas de origem/destino (ajustando sx/sy/dx/dy juntos).
 * Retorna 0 se nada ficou visível. */
static int gdi_clip_blit(int32_t* sx, int32_t* sy, int32_t* dx, int32_t* dy,
                         int32_t* w, int32_t* h,
                         int32_t sw, int32_t sh, int32_t dw, int32_t dh) {
    if (*w <= 0 || *h <= 0) return 0;
    if (*sx < 0) { *w += *sx; *dx -= *sx; *sx = 0; }
    if (*sy < 0) { *h += *sy; *dy -= *sy; *sy = 0; }
    if (*dx < 0) { *w += *dx; *sx -= *dx; *dx = 0; }
    if (*dy < 0) { *h += *dy; *sy -= *dy; *dy = 0; }
    if (*sx + *w > sw) *w = sw - *sx;
    if (*sy + *h > sh) *h = sh - *sy;
    if (*dx + *w > dw) *w = dw - *dx;
    if (*dy + *h > dh) *h = dh - *dy;
    return (*w > 0 && *h > 0);
}

/* Superfície de um objeto GDI (bitmap ou alvo do DC) — p/ testes e o host. */
struct pr_surf* pr_win32_object_surface(pr_win32_ctx* ctx, uint32_t handle) {
    if (!ctx) return NULL;
    gdi_obj* bmp = gdi_find(ctx, handle, 3);
    if (bmp) return bmp->surf;
    gdi_obj* dc = gdi_find(ctx, handle, 1);
    if (dc) return gdi_target(ctx, dc);
    return NULL;
}

static uint64_t f_CreateCompatibleBitmap(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    uint32_t hdc = (uint32_t)a[0];
    int32_t w = (int32_t)(uint32_t)a[1];
    int32_t hgt = (int32_t)(uint32_t)a[2];
    if (hdc && !gdi_find(ctx, hdc, 1)) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    if (w < 1 || hgt < 1 || w > 4096 || hgt > 4096) {
        w32_log(ctx, PR_LOG_WARN, "gdi32!CreateCompatibleBitmap: dimensoes invalidas %dx%d", (int)w, (int)hgt);
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    gdi_obj* b = gdi_alloc(ctx, W32_GDI_HBITMAP_BASE, 3);
    if (!b) { *st = PR_ERR_NOMEM; return 0; }
    b->w = (uint32_t)w;
    b->h = (uint32_t)hgt;
    b->surf = pr_surf_create((uint32_t)w, (uint32_t)hgt, PR_SURF_XRGB8888);
    if (!b->surf) { memset(b, 0, sizeof(*b)); *st = PR_ERR_NOMEM; return 0; }
    if (ctx->log)
        pr_log_write(ctx->log, PR_LOG_INFO, "gdi",
                     "[GDI] bitmap created %ux%u handle=0x%08X",
                     (unsigned)w, (unsigned)hgt, b->handle);
    return b->handle;
}

static uint64_t f_BitBlt(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 9) {
        w32_log(ctx, PR_LOG_WARN, "gdi32!BitBlt: argumentos insuficientes n=%u", (unsigned)n);
        *st = PR_ERR_INVALID;
        return 0;
    }
    gdi_obj* ddc = gdi_find(ctx, (uint32_t)a[0], 1);
    gdi_obj* sdc = gdi_find(ctx, (uint32_t)a[5], 1);
    if (!ddc || !sdc) {
        w32_log(ctx, PR_LOG_WARN, "gdi32!BitBlt: DC invalido dst=0x%08X src=0x%08X",
                (uint32_t)a[0], (uint32_t)a[5]);
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    uint32_t rop = (uint32_t)a[8];
    if (rop != 0x00CC0020u) { /* SRCCOPY */
        w32_log(ctx, PR_LOG_WARN, "gdi32!BitBlt: rop 0x%08X nao suportado (somente SRCCOPY)", rop);
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    pr_surf* dst = gdi_target(ctx, ddc);
    pr_surf* src = gdi_target(ctx, sdc);
    if (!dst || !src) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    int32_t dx = (int32_t)(uint32_t)a[1], dy = (int32_t)(uint32_t)a[2];
    int32_t w = (int32_t)(uint32_t)a[3], h = (int32_t)(uint32_t)a[4];
    int32_t sx = (int32_t)(uint32_t)a[6], sy = (int32_t)(uint32_t)a[7];
    if (w <= 0 || h <= 0) {
        w32_log(ctx, PR_LOG_WARN, "gdi32!BitBlt: largura/altura invalidas %dx%d", (int)w, (int)h);
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    int32_t sw = (int32_t)pr_surf_width(src), sh = (int32_t)pr_surf_height(src);
    int32_t dw = (int32_t)pr_surf_width(dst), dh = (int32_t)pr_surf_height(dst);
    if (!gdi_clip_blit(&sx, &sy, &dx, &dy, &w, &h, sw, sh, dw, dh)) {
        if (ctx->log)
            pr_log_write(ctx->log, PR_LOG_INFO, "gdi",
                         "[GDI] BitBlt clip total %dx%d (%d,%d)->(%d,%d)",
                         (int)w, (int)h, (int)sx, (int)sy, (int)dx, (int)dy);
        return 1;
    }
    const uint8_t* sp = (const uint8_t*)pr_surf_pixels(src);
    uint8_t* dp = (uint8_t*)pr_surf_pixels(dst);
    size_t spitch = pr_surf_pitch(src), dpitch = pr_surf_pitch(dst);
    for (int32_t row = 0; row < h; row++) {
        memcpy(dp + (size_t)(dy + row) * dpitch + (size_t)dx * 4,
               sp + (size_t)(sy + row) * spitch + (size_t)sx * 4,
               (size_t)w * 4);
    }
    if (ctx->log) {
        pr_log_write(ctx->log, PR_LOG_INFO, "gdi",
                     "[GDI] BitBlt %ux%u (%d,%d)->(%d,%d) rop=SRCCOPY",
                     (unsigned)w, (unsigned)h, (int)sx, (int)sy, (int)dx, (int)dy);
        if (dst == ctx->gdi_surface)
            pr_log_write(ctx->log, PR_LOG_INFO, "gdi", "[GDI] surface updated");
    }
    return 1;
}

static uint64_t f_CreateCompatibleDC(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    /* somente CreateCompatibleDC(NULL) = memory DC sobre a superfície do processo */
    if (n >= 1 && a[0] != 0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if (!ctx->gdi_surface) {
        ctx->gdi_surface = pr_surf_create(320, 240, PR_SURF_XRGB8888);
        if (!ctx->gdi_surface) { *st = PR_ERR_NOMEM; return 0; }
    }
    gdi_obj* dc = gdi_alloc(ctx, W32_GDI_HDC_BASE, 1);
    if (!dc) { *st = PR_ERR_NOMEM; return 0; }
    return dc->handle;
}

static uint64_t f_CreateSolidBrush(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* b = gdi_alloc(ctx, W32_GDI_HBRUSH_BASE, 2);
    if (!b) { *st = PR_ERR_NOMEM; return 0; }
    b->color = (uint32_t)a[0] & 0x00FFFFFF; /* COLORREF 0x00BBGGRR */
    return b->handle;
}

static uint64_t f_SelectObject(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    gdi_obj* br = gdi_find(ctx, (uint32_t)a[1], 2);
    if (br) {
        uint32_t prev = dc->sel;
        dc->sel = br->handle;
        return prev;
    }
    gdi_obj* bmp = gdi_find(ctx, (uint32_t)a[1], 3);
    if (bmp) {
        uint32_t prev = dc->sel_bmp;
        dc->sel_bmp = bmp->handle;
        return prev;
    }
    gdi_obj* pen = gdi_find(ctx, (uint32_t)a[1], 4);
    if (pen) {
        uint32_t prev = dc->sel_pen;
        dc->sel_pen = pen->handle;
        return prev;
    }
    gdi_obj* font = gdi_find(ctx, (uint32_t)a[1], 5);
    if (font) {
        uint32_t prev = dc->sel_font;
        dc->sel_font = font->handle;
        return prev;
    }
    if (a[1]==W32_STOCK_WHITE_BRUSH) {
        uint32_t prev = dc->sel;
        dc->sel = W32_STOCK_WHITE_BRUSH;
        return prev;
    }
    ctx->last_error = W32_ERROR_INVALID_PARAMETER;
    return 0;
}

static uint64_t f_DeleteObject(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint32_t h = (uint32_t)a[0];
    if (h==W32_STOCK_WHITE_BRUSH) return 1;
    gdi_obj* b = gdi_find(ctx, h, 2);
    if (b) { memset(b, 0, sizeof(*b)); return 1; }
    gdi_obj* bmp = gdi_find(ctx, h, 3);
    if (bmp) {
        pr_surf_destroy(bmp->surf);
        memset(bmp, 0, sizeof(*bmp));
        return 1;
    }
    gdi_obj* pen = gdi_find(ctx, h, 4);
    if (pen) { memset(pen,0,sizeof(*pen)); return 1; }
    gdi_obj* font = gdi_find(ctx, h, 5);
    if (font) { memset(font,0,sizeof(*font)); return 1; }
    /* DeleteObject em DC é inválido no Win32 (use DeleteDC) */
    ctx->last_error = W32_ERROR_INVALID_PARAMETER;
    return 0;
}

static uint64_t f_PatBlt(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 6) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    pr_surf* tgt = gdi_target(ctx, dc);
    if (!tgt) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    uint32_t rop = (uint32_t)a[5];
    if (rop != 0x00F00021u) { /* PATCOPY */
        w32_log(ctx, PR_LOG_WARN, "gdi32!PatBlt: rop 0x%08X nao suportado (somente PATCOPY)", rop);
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    uint32_t colorref = gdi_brush_color(ctx, dc);
    pr_color c;
    c.r = (float)(colorref & 0xFF) / 255.0f;
    c.g = (float)((colorref >> 8) & 0xFF) / 255.0f;
    c.b = (float)((colorref >> 16) & 0xFF) / 255.0f;
    c.a = 1.0f;
    pr_rect r;
    r.x = (float)(int32_t)a[1];
    r.y = (float)(int32_t)a[2];
    r.w = (float)(int32_t)a[3];
    r.h = (float)(int32_t)a[4];
    pr_surf_fill_rect(tgt, r, c);
    if (ctx->log && tgt == ctx->gdi_surface)
        pr_log_write(ctx->log, PR_LOG_INFO, "gdi", "[GDI] surface updated");
    return 1;
}

static uint64_t f_GetDeviceCaps(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc || !ctx->gdi_surface) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    switch ((uint32_t)a[1]) {
        case 8:  return pr_surf_width(ctx->gdi_surface);   /* HORZRES */
        case 10: return pr_surf_height(ctx->gdi_surface);  /* VERTRES */
        case 6:  return 32;                                /* BITSPIXEL */
        default: return 0;
    }
}

static uint64_t f_MoveToEx(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    int32_t x = (int32_t)a[1], y = (int32_t)a[2];
    if (a[3]) {
        int32_t* out = (int32_t*)pr_win32_ptr(ctx, a[3], 8);
        if (out) { out[0]=dc->cur_x; out[1]=dc->cur_y; }
    }
    dc->cur_x = x; dc->cur_y = y;
    return 1;
}
static uint64_t f_LineTo(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    pr_surf* tgt = gdi_target(ctx, dc);
    if (!tgt) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    int32_t x1 = dc->cur_x, y1 = dc->cur_y;
    int32_t x2 = (int32_t)a[1], y2 = (int32_t)a[2];
    uint32_t pen = gdi_pen_color(ctx, dc);
    uint32_t* px = pr_surf_pixels(tgt);
    size_t pitch = pr_surf_pitch(tgt)/4;
    uint32_t sw = pr_surf_width(tgt), sh = pr_surf_height(tgt);
    uint32_t col = ((pen & 0xFF)<<16) | (pen & 0xFF00) | ((pen>>16 & 0xFF)) | 0xFF000000u;
    /* Bresenham */
    int dx = abs(x2-x1), sx = x1<x2?1:-1;
    int dy = -abs(y2-y1), sy = y1<y2?1:-1;
    int err = dx+dy;
    int x=x1, y=y1;
    for (;;) {
        if (x>=0 && y>=0 && (uint32_t)x<sw && (uint32_t)y<sh) px[(size_t)y*pitch + (size_t)x]=col;
        if (x==x2 && y==y2) break;
        int e2 = 2*err;
        if (e2>=dy) { err+=dy; x+=sx; }
        if (e2<=dx) { err+=dx; y+=sy; }
    }
    dc->cur_x = x2; dc->cur_y = y2;
    return 1;
}
static uint64_t f_Rectangle(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 5) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    pr_surf* tgt = gdi_target(ctx, dc);
    if (!tgt) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    int32_t l = (int32_t)a[1], t = (int32_t)a[2], r = (int32_t)a[3], b = (int32_t)a[4];
    if (l>r) { int32_t tmp=l; l=r; r=tmp; }
    if (t>b) { int32_t tmp=t; t=b; b=tmp; }
    uint32_t brush = gdi_brush_color(ctx, dc);
    pr_color bc; bc.r=(float)(brush & 0xFF)/255.0f; bc.g=(float)((brush>>8)&0xFF)/255.0f; bc.b=(float)((brush>>16)&0xFF)/255.0f; bc.a=1.0f;
    pr_rect fr; fr.x=(float)l+1; fr.y=(float)t+1; fr.w=(float)(r-l-1); fr.h=(float)(b-t-1);
    if (fr.w>0 && fr.h>0) pr_surf_fill_rect(tgt, fr, bc);
    /* border with pen */
    uint32_t pen = gdi_pen_color(ctx, dc);
    uint32_t* px = pr_surf_pixels(tgt);
    size_t pitch = pr_surf_pitch(tgt)/4;
    uint32_t sw = pr_surf_width(tgt), sh = pr_surf_height(tgt);
    uint32_t col = ((pen & 0xFF)<<16) | (pen & 0xFF00) | ((pen>>16 & 0xFF)) | 0xFF000000u;
    for (int32_t x=l;x<r;x++) { if (x>=0 && (uint32_t)x<sw) { if (t>=0 && (uint32_t)t<sh) px[(size_t)t*pitch + (size_t)x]=col; if (b-1>=0 && (uint32_t)(b-1)<sh) px[(size_t)(b-1)*pitch + (size_t)x]=col; } }
    for (int32_t y=t;y<b;y++) { if (y>=0 && (uint32_t)y<sh) { if (l>=0 && (uint32_t)l<sw) px[(size_t)y*pitch + (size_t)l]=col; if (r-1>=0 && (uint32_t)(r-1)<sw) px[(size_t)y*pitch + (size_t)(r-1)]=col; } }
    return 1;
}
static uint64_t f_CreatePen(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* p = gdi_alloc(ctx, W32_GDI_HPEN_BASE, 4);
    if (!p) { *st = PR_ERR_NOMEM; return 0; }
    p->color = (uint32_t)a[2];
    return p->handle;
}
static uint64_t f_CreateFontA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)a; *st = PR_OK;
    if (n < 14) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* f = gdi_alloc(ctx, W32_GDI_HFONT_BASE, 5);
    if (!f) { *st = PR_ERR_NOMEM; return 0; }
    return f->handle;
}
static uint64_t f_CreateFontW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    return f_CreateFontA(ctx, a, n, st);
}
static uint64_t f_TextOutA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    pr_surf* tgt = gdi_target(ctx, dc);
    if (!tgt) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    int32_t x = (int32_t)a[1], y = (int32_t)a[2];
    const char* s = (const char*)pr_win32_ptr(ctx, a[3], 1);
    if (!s) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    int len = (int)a[4];
    if (len<0) len=0;
    if (len>256) len=256;
    /* placeholder: draw small filled rect for each char */
    uint32_t* px = pr_surf_pixels(tgt);
    size_t pitch = pr_surf_pitch(tgt)/4;
    uint32_t sw = pr_surf_width(tgt), sh = pr_surf_height(tgt);
    uint32_t col = 0xFF000000u;
    for (int i=0;i<len;i++) {
        int32_t cx = x + i*8;
        for (int dy=0;dy<8;dy++) for (int dx=0;dx<6;dx++) {
            int32_t px1=cx+dx, py1=y+dy;
            if (px1>=0 && py1>=0 && (uint32_t)px1<sw && (uint32_t)py1<sh) px[(size_t)py1*pitch + (size_t)px1]=col;
        }
    }
    return 1;
}
static uint64_t f_TextOutW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    pr_surf* tgt = gdi_target(ctx, dc);
    if (!tgt) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    int32_t x = (int32_t)a[1], y = (int32_t)a[2];
    int len = (int)a[4];
    if (len<0) len=0;
    if (len>256) len=256;
    uint32_t* px = pr_surf_pixels(tgt);
    size_t pitch = pr_surf_pitch(tgt)/4;
    uint32_t sw = pr_surf_width(tgt), sh = pr_surf_height(tgt);
    uint32_t col = 0xFF000000u;
    for (int i=0;i<len;i++) {
        int32_t cx = x + i*8;
        for (int dy=0;dy<8;dy++) for (int dx=0;dx<6;dx++) {
            int32_t px1=cx+dx, py1=y+dy;
            if (px1>=0 && py1>=0 && (uint32_t)px1<sw && (uint32_t)py1<sh) px[(size_t)py1*pitch + (size_t)px1]=col;
        }
    }
    return 1;
}
static uint64_t f_DrawTextA(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    pr_surf* tgt = gdi_target(ctx, dc);
    if (!tgt) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    const int32_t* rc = (const int32_t*)pr_win32_ptr(ctx, a[2], 16);
    if (!rc) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    /* reuse TextOut logic: draw placeholder */
    int32_t x = rc[0], y = rc[1];
    const char* s = (const char*)pr_win32_ptr(ctx, a[1], 1);
    if (!s) return 0;
    int len = (int)a[2]; /* actually nCount is a[2]? spec: HDC, LPCSTR, int, LPRECT, UINT */
    /* Our n: 0=HDC,1=lpchText,2=cchText,3=lprc,4=format */
    len = (int)a[2];
    if (len<0) { len = (int)strlen(s); }
    if (len>256) len=256;
    uint32_t* px = pr_surf_pixels(tgt);
    size_t pitch = pr_surf_pitch(tgt)/4;
    uint32_t sw = pr_surf_width(tgt), sh = pr_surf_height(tgt);
    uint32_t col = 0xFF000000u;
    for (int i=0;i<len;i++) {
        int32_t cx = x + i*8;
        for (int dy=0;dy<8;dy++) for (int dx=0;dx<6;dx++) {
            int32_t px1=cx+dx, py1=y+dy;
            if (px1>=0 && py1>=0 && (uint32_t)px1<sw && (uint32_t)py1<sh) px[(size_t)py1*pitch + (size_t)px1]=col;
        }
    }
    return rc[3]-rc[1]; /* height */
}
static uint64_t f_DrawTextW(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    const int32_t* rc = (const int32_t*)pr_win32_ptr(ctx, a[2], 16);
    if (!rc) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    return rc[3]-rc[1];
}
static uint64_t f_CreateDIBSection(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    *st = PR_OK;
    if (n < 6) { *st = PR_ERR_INVALID; return 0; }
    uint32_t hdc = (uint32_t)a[0];
    gdi_obj* dc = gdi_find(ctx, hdc, 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    const uint8_t* bmi = (const uint8_t*)pr_win32_ptr(ctx, a[1], 40);
    if (!bmi) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    int32_t width = *(int32_t*)(bmi+4);
    int32_t height = *(int32_t*)(bmi+8);
    if (width<=0 || height==0) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    if (height<0) height=-height;
    if (width>1024 || height>1024) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    gdi_obj* bmp = gdi_alloc(ctx, W32_GDI_HBITMAP_BASE, 3);
    if (!bmp) { *st = PR_ERR_NOMEM; return 0; }
    bmp->w = (uint32_t)width; bmp->h = (uint32_t)height;
    bmp->surf = pr_surf_create((uint32_t)width, (uint32_t)height, PR_SURF_XRGB8888);
    if (!bmp->surf) { memset(bmp,0,sizeof(*bmp)); *st=PR_ERR_NOMEM; return 0; }
    if (a[4]) {
        void** ppv = (void**)pr_win32_ptr(ctx, a[4], 8);
        if (ppv) *ppv = (void*)(uintptr_t)pr_surf_pixels(bmp->surf);
    }
    return bmp->handle;
}

/* ---- ws2_32: ordem de bytes (winsock) ----
 * Os suportes atuais são little-endian (x86/ARM Apple/Linux); htons e cia.
 * trocam os bytes para ordem de rede (big-endian). */

static uint64_t f_htons(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)n; *st = PR_OK;
    uint16_t v = (uint16_t)(a[0] & 0xFFFF);
    return (uint64_t)(uint16_t)((v << 8) | (v >> 8));
}

static uint64_t f_ntohs(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    return f_htons(ctx, a, n, st); /* inverso é a mesma operação */
}

static uint64_t f_htonl(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)ctx; (void)n; *st = PR_OK;
    uint32_t v = (uint32_t)a[0];
    return ((v & 0x000000FFu) << 24) | ((v & 0x0000FF00u) << 8) |
           ((v & 0x00FF0000u) >> 8) | ((v & 0xFF000000u) >> 24);
}

static uint64_t f_ntohl(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    return f_htonl(ctx, a, n, st); /* inverso é a mesma operação */
}

/* ============================================================
 * Catálogo estático
 * ============================================================ */

#define IMPL(mod, name, fn, b) { mod, name, 0, PR_WIN32_IMPLEMENTED, fn, (uint16_t)(b), "comportamento real", 0, 0 }
#define IMPL_ORD(mod, name, fn, b, ord) { mod, name, (ord), PR_WIN32_IMPLEMENTED, fn, (uint16_t)(b), "comportamento real (ordinal publico)", 0, 0 }
#define TODO(mod, name, note) { mod, name, 0, PR_WIN32_UNSUPPORTED, NULL, 0, note, 0, 0 }
/* ---- strings C (msvcrt): comportamento real, leitura limitada ---- */
static uint64_t f_msvcrt_strcmp(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)n; *st = PR_OK;
    const char* s1 = (const char*)pr_win32_ptr(ctx, a[0], 1);
    const char* s2 = (const char*)pr_win32_ptr(ctx, a[1], 1);
    if (!s1 || !s2) { *st = PR_ERR_FAULT; return 0; }
    size_t l1 = strnlen(s1, 4096), l2 = strnlen(s2, 4096);
    size_t k = (l1 < l2 ? l1 : l2) + 1;
    return (uint64_t)(int64_t)strncmp(s1, s2, k);
}

static uint64_t f_msvcrt_strcpy(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)n; *st = PR_OK;
    char* d = (char*)pr_win32_ptr(ctx, a[0], 1);
    const char* s = (const char*)pr_win32_ptr(ctx, a[1], 1);
    if (!d || !s) { *st = PR_ERR_FAULT; return 0; }
    size_t l = strnlen(s, 4095);
    memcpy(d, s, l + 1);
    return a[0];
}

static uint64_t f_msvcrt_strcat(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    (void)n; *st = PR_OK;
    char* d = (char*)pr_win32_ptr(ctx, a[0], 1);
    const char* s = (const char*)pr_win32_ptr(ctx, a[1], 1);
    if (!d || !s) { *st = PR_ERR_FAULT; return 0; }
    size_t dl = strnlen(d, 4095), sl = strnlen(s, 4095);
    if (dl + sl >= 4096) { *st = PR_ERR_RANGE; return 0; }
    memcpy(d + dl, s, sl + 1);
    return a[0];
}

/* ===================== Janelas + mensagens (FASE 1/2 do GRUPO 6) =====================
 * Modelo real mínimo: a 1ª janela cria a superfície do processo
 * (ctx->gdi_surface — o mesmo buffer consumido pelo Metal). WndProc é
 * executada pela reentrada do convidado (guest_call — mesma do atexit);
 * sem reentrada, dispatch = EXECUTION STOPPED honesto. */

static const uint32_t w32_syscolor[18] = {
    /* COLORREF 0x00BBGGRR — paleta determinística para (COLOR_xxx+1) */
    0x00C0C0C0, 0x00808000, 0x00800000, 0x00C0C0C0, 0x00C0C0C0, 0x00FFFFFF,
    0x00000000, 0x00000000, 0x00000000, 0x00FFFFFF, 0x00C0C0C0, 0x00C0C0C0,
    0x00808080, 0x00800000, 0x00FFFFFF, 0x00C0C0C0, 0x00808080, 0x00808080
};

static w32_window* wnd_find(pr_win32_ctx* ctx, uint32_t hwnd) {
    if (!ctx) return NULL;
    for (int i = 0; i < W32_MAX_WINDOWS; i++)
        if (ctx->windows[i].used && ctx->windows[i].handle == hwnd)
            return &ctx->windows[i];
    return NULL;
}

static w32_class* cls_find(pr_win32_ctx* ctx, const char* name) {
    if (!ctx || !name) return NULL;
    for (int i = 0; i < W32_MAX_CLASSES; i++)
        if (ctx->classes[i].used && strncmp(ctx->classes[i].name, name, 63) == 0)
            return &ctx->classes[i];
    return NULL;
}

static void msg_post(pr_win32_ctx* ctx, uint32_t hwnd, uint32_t message,
                     uint64_t wp, uint64_t lp) {
    if (ctx->msgq_len >= W32_MSGQ_MAX) {
        w32_log(ctx, PR_LOG_WARN,
                "[WIN32] fila de mensagens cheia — msg 0x%08X perdida", message);
        return;
    }
    int idx = (ctx->msgq_head + ctx->msgq_len) % W32_MSGQ_MAX;
    ctx->msgq[idx].hwnd = hwnd;
    ctx->msgq[idx].message = message;
    ctx->msgq[idx].wparam = wp;
    ctx->msgq[idx].lparam = lp;
    ctx->msgq_len++;
}

/* ---- GRUPO 7: relógio, temporizadores e fila de entrada host ---- */

static uint64_t w32_now_ms(pr_win32_ctx* ctx) {
    return (mono_ns() - ctx->start_ns) / 1000000ull + ctx->time_offset_ms;
}

/* Dispara timers vencidos (WM_TIMER real com coalesce; TIMERPROC via
 * reentrada). Retorna -1 se TIMERPROC não puder ser executado (STOP). */
static int timers_pump(pr_win32_ctx* ctx) {
    uint64_t now = w32_now_ms(ctx);
    for (int i = 0; i < W32_MAX_TIMERS; i++) {
        w32_timer* tm = &ctx->timers[i];
        if (!tm->used || tm->next_ms > now) continue;
        tm->next_ms = now + tm->elapse;   /* coalesce de atrasos */
        if (tm->proc) {
            if (!ctx->guest_call) {
                diagf(ctx, "user32!WM_TIMER: TIMERPROC %016llX sem reentrada "
                           "de convidado", (unsigned long long)tm->proc);
                return -1;
            }
            uint64_t args[4] = { tm->hwnd, W32_WM_TIMER, tm->id, now };
            uint64_t ret = 0;
            ctx->guest_call(ctx->guest_ud, tm->proc, args, &ret);
        } else {
            /* no máximo 1 WM_TIMER pendente por timer (coalesce real) */
            int pend = 0;
            for (int k = 0; k < ctx->msgq_len; k++) {
                int idx = (ctx->msgq_head + k) % W32_MSGQ_MAX;
                if (ctx->msgq[idx].hwnd == tm->hwnd &&
                    ctx->msgq[idx].message == W32_WM_TIMER &&
                    ctx->msgq[idx].wparam == tm->id) { pend = 1; break; }
            }
            if (!pend) msg_post(ctx, tm->hwnd, W32_WM_TIMER, tm->id, 0);
        }
    }
    return 0;
}

/* Entrada sem foco: retida e entregue à primeira janela com foco
 * (determinístico p/ testes; a sessão iOS injeta antes do primeiro frame). */
static void input_post_or_park(pr_win32_ctx* ctx, uint32_t message,
                               uint64_t wp, uint64_t lp) {
    uint32_t hwnd = ctx->focus_hwnd;
    if (hwnd) { msg_post(ctx, hwnd, message, wp, lp); return; }
    if (ctx->inp_pend_len >= W32_INPUT_PEND_MAX) {
        w32_log(ctx, PR_LOG_WARN,
                "[INPUT] fila de entrada cheia — evento 0x%08X perdido", message);
        return;
    }
    w32_inp_msg* e = &ctx->inp_pend[ctx->inp_pend_len++];
    e->hwnd = 0; e->message = message; e->wp = wp; e->lp = lp;
}

static void input_deliver_parked(pr_win32_ctx* ctx, uint32_t hwnd) {
    for (int i = 0; i < ctx->inp_pend_len; i++)
        msg_post(ctx, hwnd, ctx->inp_pend[i].message,
                 ctx->inp_pend[i].wp, ctx->inp_pend[i].lp);
    ctx->inp_pend_len = 0;
}

static uint32_t vk_to_char(pr_win32_ctx* ctx, uint32_t vk) {
    int shift = ctx->key_down[W32_VK_SHIFT] || ctx->key_down[0xA0] ||
                ctx->key_down[0xA1];
    if (vk == W32_VK_SPACE) return 0x20;
    if (vk == W32_VK_RETURN) return 0x0D;
    if (vk == W32_VK_TAB) return 0x09;
    if (vk == W32_VK_BACK) return 0x08;
    if (vk == W32_VK_ESCAPE) return 0x1B;
    if (vk >= 0x30 && vk <= 0x39) return vk;              /* dígitos */
    if (vk >= 0x41 && vk <= 0x5A) return shift ? vk : vk + 32;
    return 0;
}

static uint32_t mouse_mk(pr_win32_ctx* ctx) {
    uint32_t mk = ctx->mouse_buttons;
    if (ctx->key_down[W32_VK_SHIFT]) mk |= W32_MK_SHIFT;
    if (ctx->key_down[W32_VK_CONTROL]) mk |= W32_MK_CONTROL;
    return mk;
}

static uint32_t brush_color_of(pr_win32_ctx* ctx, uint64_t h) {
    if (h == W32_STOCK_WHITE_BRUSH) return 0x00FFFFFF;
    gdi_obj* b = gdi_find(ctx, (uint32_t)h, 2);
    if (b) return b->color;
    if (h >= 1 && h <= 18) return w32_syscolor[h - 1];   /* (COLOR_xxx+1) */
    return 0x00FFFFFF;
}

static void fill_bg(pr_win32_ctx* ctx, uint64_t hbr, uint32_t w, uint32_t h) {
    if (!ctx->gdi_surface) return;
    uint32_t colorref = brush_color_of(ctx, hbr);
    pr_color c;
    c.r = (float)(colorref & 0xFF) / 255.0f;
    c.g = (float)((colorref >> 8) & 0xFF) / 255.0f;
    c.b = (float)((colorref >> 16) & 0xFF) / 255.0f;
    c.a = 1.0f;
    pr_rect r; r.x = 0; r.y = 0; r.w = (float)w; r.h = (float)h;
    pr_surf_fill_rect(ctx->gdi_surface, r, c);
}

/* Envia mensagem síncrona à WndProc (mesma semântica do SendMessage). */
static uint64_t wnd_send(pr_win32_ctx* ctx, w32_window* w, uint32_t message,
                         uint64_t wp, uint64_t lp, pr_status* st) {
    w32_class* c = &ctx->classes[w->cls];
    if (!ctx->guest_call) {
        diagf(ctx, "user32!DispatchMessage/SendMessage: WndProc %016llX "
                   "sem reentrada de convidado (guest_call ausente)",
              (unsigned long long)c->wndproc);
        *st = PR_ERR_UNSUPPORTED;
        return 0;
    }
    uint64_t args[4] = { w->handle, message, wp, lp };
    uint64_t ret = 0;
    if (!ctx->guest_call(ctx->guest_ud, c->wndproc, args, &ret)) {
        diagf(ctx, "user32!WndProc %016llX msg 0x%08X: falha na execucao",
              (unsigned long long)c->wndproc, message);
        *st = PR_ERR_STATE;
        return 0;
    }
    return ret;
}

static uint64_t wnd_destroy(pr_win32_ctx* ctx, w32_window* w, pr_status* st) {
    if (!w || w->destroyed) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    wnd_send(ctx, w, W32_WM_DESTROY, 0, 0, st);   /* WndProc pode PostQuitMessage */
    w->used = 0;
    w->destroyed = 1;
    if (ctx->log)
        pr_log_write(ctx->log, PR_LOG_INFO, "gdi",
                     "[GDI] window destroyed hwnd=0x%08X", w->handle);
    return 1;
}

/* MSG x64 (48B): hwnd@0(8) message@8(4) pad(4) wParam@16(8) lParam@24(8)
 * time@32(4) pt.x@36(4) pt.y@40(4) pad(4). */
static void msg_write(pr_win32_ctx* ctx, uint64_t gmsg,
                      uint32_t hwnd, uint32_t message,
                      uint64_t wp, uint64_t lp) {
    uint8_t* m = (uint8_t*)pr_win32_ptr(ctx, gmsg, 48);
    if (!m) return;
    memset(m, 0, 48);
    memcpy(m + 0, &hwnd, 4);
    memcpy(m + 8, &message, 4);
    memcpy(m + 16, &wp, 8);
    memcpy(m + 24, &lp, 8);
    uint32_t tm = (uint32_t)((mono_ns() - ctx->start_ns) / 1000000ull);
    memcpy(m + 32, &tm, 4);
}

static uint64_t f_DefWindowProcA(pr_win32_ctx* ctx, const uint64_t* a,
                                 size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    uint32_t hwnd = (uint32_t)a[0], msg = (uint32_t)a[1];
    w32_window* w = wnd_find(ctx, hwnd);
    switch (msg) {
    case W32_WM_ERASEBKGND:
        if (w) {
            fill_bg(ctx, ctx->classes[w->cls].hbr_bg, w->cw, w->ch);
            w->erase_bg = 0;
        }
        return 1;   /* fundo apagado */
    case W32_WM_PAINT:  /* real: valida a região (BeginPaint/EndPaint implícitos) */
        if (w) { w->update_pending = 0; w->erase_bg = 0; }
        return 0;
    case W32_WM_CLOSE:  /* real: DefWindowProc chama DestroyWindow */
        if (w && !w->destroyed) wnd_destroy(ctx, w, st);
        return 0;
    case W32_WM_CREATE:
    case W32_WM_DESTROY:
    case W32_WM_MOVE:
    case W32_WM_SIZE:
    case W32_WM_SHOWWINDOW:
    case 0x0007u: /* WM_SETFOCUS */
    case 0x0008u: /* WM_KILLFOCUS */
        return 0;
    default:
        w32_log(ctx, PR_LOG_DEBUG,
                "[WIN32] DefWindowProcA msg 0x%08X (sem tratamento especifico)", msg);
        return 0;
    }
}

static uint64_t f_RegisterClassA(pr_win32_ctx* ctx, const uint64_t* a,
                                 size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    const uint8_t* cls = (const uint8_t*)pr_win32_ptr(ctx, a[0], 72);
    if (!cls) { diagf(ctx, "user32!RegisterClassA: lpWndClass fora do espaco");
                *st = PR_ERR_INVALID; return 0; }
    uint64_t wndproc = 0, hbr = 0;
    memcpy(&wndproc, cls + 8, 8);
    memcpy(&hbr, cls + 48, 8);
    uint64_t gname = 0;
    memcpy(&gname, cls + 64, 8);
    const char* name = (const char*)pr_win32_ptr(ctx, gname, 1);
    if (!name || !strnlen(name, 63)) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0;
    }
    if (cls_find(ctx, name)) {
        ctx->last_error = W32_ERROR_CLASS_ALREADY_EXISTS; return 0;
    }
    if (!wndproc) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    for (int i = 0; i < W32_MAX_CLASSES; i++) {
        if (!ctx->classes[i].used) {
            ctx->classes[i].used = 1;
            ctx->classes[i].wndproc = wndproc;
            ctx->classes[i].hbr_bg = hbr;
            snprintf(ctx->classes[i].name, 64, "%s", name);
            w32_log(ctx, PR_LOG_INFO,
                    "[GDI] class registered \"%s\" wndproc=0x%016llX",
                    name, (unsigned long long)wndproc);
            return (uint64_t)(i + 1);   /* ATOM */
        }
    }
    ctx->last_error = W32_ERROR_INVALID_PARAMETER;
    return 0;
}

static uint64_t f_CreateWindowExA(pr_win32_ctx* ctx, const uint64_t* a,
                                  size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 12) { *st = PR_ERR_INVALID; return 0; }
    const char* cname = (const char*)pr_win32_ptr(ctx, a[1], 1);
    w32_class* c = cname ? cls_find(ctx, cname) : NULL;
    if (!c) {
        ctx->last_error = W32_ERROR_CANNOT_FIND_WND_CLASS;
        w32_log(ctx, PR_LOG_WARN,
                "[WIN32] CreateWindowExA: classe nao registrada");
        return 0;
    }
    int32_t x = (int32_t)(uint32_t)a[4], y = (int32_t)(uint32_t)a[5];
    int32_t cw = (int32_t)(uint32_t)a[6], ch = (int32_t)(uint32_t)a[7];
    if (cw <= 0 || ch <= 0 || cw > 8192 || ch > 8192) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        w32_log(ctx, PR_LOG_WARN,
                "[WIN32] CreateWindowExA: dimensoes invalidas %dx%d", cw, ch);
        return 0;
    }
    /* superfície do processo: criada pela 1ª janela (contrato C↔Swift↔Metal) */
    if (!ctx->gdi_surface) {
        ctx->gdi_surface = pr_surf_create((uint32_t)cw, (uint32_t)ch,
                                          PR_SURF_XRGB8888);
        if (!ctx->gdi_surface) { *st = PR_ERR_NOMEM; return 0; }
    } else if (pr_surf_width(ctx->gdi_surface) != (uint32_t)cw ||
               pr_surf_height(ctx->gdi_surface) != (uint32_t)ch) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        w32_log(ctx, PR_LOG_WARN,
                "[WIN32] CreateWindowExA: %dx%d != superficie %dx%d "
                "(dimensoes fixas na criacao; processo = janela unica)", cw, ch,
                (int)pr_surf_width(ctx->gdi_surface),
                (int)pr_surf_height(ctx->gdi_surface));
        return 0;
    }
    int slot = -1;
    for (int i = 0; i < W32_MAX_WINDOWS; i++)
        if (!ctx->windows[i].used) { slot = i; break; }
    if (slot < 0) { *st = PR_ERR_NOMEM; return 0; }
    w32_window* w = &ctx->windows[slot];
    memset(w, 0, sizeof(*w));
    w->used = 1;
    w->handle = W32_HWND_BASE + (uint32_t)slot;
    w->cls = (int)(c - ctx->classes);
    w->x = x; w->y = y;
    w->cw = (uint32_t)cw; w->ch = (uint32_t)ch;
    w->visible = (a[3] & W32_WS_VISIBLE) ? 1 : 0;
    w->update_pending = 1;
    w->erase_bg = 1;
    const char* title = (const char*)pr_win32_ptr(ctx, a[2], 1);
    if (title) snprintf(w->title, 64, "%s", title);
    /* real: WM_CREATE/WM_SIZE antes de retornar; WM_CREATE=-1 aborta */
    uint64_t r = wnd_send(ctx, w, W32_WM_CREATE, 0, a[11], st);
    if (*st != PR_OK) { w->used = 0; return 0; }
    if (r == (uint64_t)(int64_t)-1) { w->used = 0; return 0; }
    r = wnd_send(ctx, w, W32_WM_SIZE, 0 /*SIZE_RESTORED*/,
                 ((uint64_t)(cw & 0xFFFF) << 16) | (uint64_t)(ch & 0xFFFF), st);
    if (*st != PR_OK) { w->used = 0; return 0; }
    if (w->visible) {
        r = wnd_send(ctx, w, W32_WM_SHOWWINDOW, 1, 0, st);
        if (*st != PR_OK) { w->used = 0; return 0; }
    }
    if (!ctx->focus_hwnd) ctx->focus_hwnd = w->handle;
    input_deliver_parked(ctx, w->handle);
    w32_log(ctx, PR_LOG_INFO,
            "[GDI] window created %dx%d \"%s\" hwnd=0x%08X", cw, ch,
            w->title, w->handle);
    return w->handle;
}

static uint64_t f_ShowWindow(pr_win32_ctx* ctx, const uint64_t* a,
                             size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    w32_window* w = wnd_find(ctx, (uint32_t)a[0]);
    if (!w) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    int prev = w->visible;
    w->visible = ((uint32_t)a[1] != 0) ? 1 : 0;   /* SW_HIDE = 0 */
    if (w->visible) {
        w->update_pending = 1;
        w->erase_bg = 1;
    }
    wnd_send(ctx, w, W32_WM_SHOWWINDOW, (uint64_t)w->visible, 0, st);
    return (uint64_t)prev;
}

static uint64_t f_GetClientRect(pr_win32_ctx* ctx, const uint64_t* a,
                                size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    w32_window* w = wnd_find(ctx, (uint32_t)a[0]);
    if (!w) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    uint8_t* rc = (uint8_t*)pr_win32_ptr(ctx, a[1], 16);
    if (!rc) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    int32_t vals[4] = { 0, 0, (int32_t)w->cw, (int32_t)w->ch };
    memcpy(rc, vals, 16);
    return 1;
}

static uint64_t f_GetDC(pr_win32_ctx* ctx, const uint64_t* a,
                        size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint32_t hwnd = (uint32_t)a[0];
    if (hwnd) {
        w32_window* w = wnd_find(ctx, hwnd);
        if (!w) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    }
    if (!ctx->gdi_surface) {
        ctx->gdi_surface = pr_surf_create(320, 240, PR_SURF_XRGB8888);
        if (!ctx->gdi_surface) { *st = PR_ERR_NOMEM; return 0; }
    }
    gdi_obj* dc = gdi_alloc(ctx, W32_GDI_HDC_BASE, 1);
    if (!dc) { *st = PR_ERR_NOMEM; return 0; }
    return dc->handle;
}

static uint64_t f_ReleaseDC(pr_win32_ctx* ctx, const uint64_t* a,
                            size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[1], 1);
    if (!dc) return 0;
    memset(dc, 0, sizeof(*dc));
    return 1;
}

static uint64_t f_InvalidateRect(pr_win32_ctx* ctx, const uint64_t* a,
                                 size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    w32_window* w = wnd_find(ctx, (uint32_t)a[0]);
    if (!w) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    w->update_pending = 1;
    if (a[2]) w->erase_bg = 1;   /* bErase */
    /* nota: lprect parcial tratado como janela inteira neste subset */
    return 1;
}

static uint64_t f_UpdateWindow(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    w32_window* w = wnd_find(ctx, (uint32_t)a[0]);
    if (!w) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    if (w->update_pending && !w->destroyed) {
        if (w->erase_bg) {
            uint64_t r = wnd_send(ctx, w, W32_WM_ERASEBKGND, 0, 0, st);
            (void)r;
            if (*st != PR_OK) return 0;
        }
        uint64_t r = wnd_send(ctx, w, W32_WM_PAINT, 0, 0, st);
        (void)r;
        if (*st != PR_OK) return 0;
    }
    return 1;
}

/* PAINTSTRUCT x64 (72B): hdc@0(8) fErase@8(4) rcPaint@12(16) fRestore@28(4)
 * fIncUpdate@32(4) rgbReserved[32]@36 */
static uint64_t f_BeginPaint(pr_win32_ctx* ctx, const uint64_t* a,
                             size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    w32_window* w = wnd_find(ctx, (uint32_t)a[0]);
    if (!w) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    if (!ctx->gdi_surface) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    int erased = 0;
    if (w->erase_bg) {
        fill_bg(ctx, ctx->classes[w->cls].hbr_bg, w->cw, w->ch);
        w->erase_bg = 0;
        erased = 1;
    }
    gdi_obj* dc = gdi_alloc(ctx, W32_GDI_HDC_BASE, 1);
    if (!dc) { *st = PR_ERR_NOMEM; return 0; }
    uint8_t* ps = (uint8_t*)pr_win32_ptr(ctx, a[1], 72);
    if (ps) {
        memset(ps, 0, 72);
        uint32_t dh = dc->handle;
        memcpy(ps + 0, &dh, 4);
        int32_t fe = erased;
        memcpy(ps + 8, &fe, 4);
        int32_t rcp[4] = { 0, 0, (int32_t)w->cw, (int32_t)w->ch };
        memcpy(ps + 12, rcp, 16);
    }
    return dc->handle;
}

static uint64_t f_EndPaint(pr_win32_ctx* ctx, const uint64_t* a,
                           size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    w32_window* w = wnd_find(ctx, (uint32_t)a[0]);
    if (!w) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    const uint8_t* ps = (const uint8_t*)pr_win32_ptr(ctx, a[1], 72);
    if (ps) {
        uint32_t dh = 0;
        memcpy(&dh, ps, 4);
        gdi_obj* dc = gdi_find(ctx, dh, 1);
        if (dc) memset(dc, 0, sizeof(*dc));
    }
    w->update_pending = 0;   /* real: EndPaint valida a região */
    return 1;
}

static uint64_t f_DestroyWindow(pr_win32_ctx* ctx, const uint64_t* a,
                                size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    w32_window* w = wnd_find(ctx, (uint32_t)a[0]);
    if (!w) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    return wnd_destroy(ctx, w, st);
}

static uint64_t f_PostQuitMessage(pr_win32_ctx* ctx, const uint64_t* a,
                                  size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    (void)n;
    ctx->quit_posted = 1;
    ctx->quit_code = (uint32_t)a[0];
    msg_post(ctx, 0, W32_WM_QUIT, ctx->quit_code, 0);   /* real: posta WM_QUIT */
    w32_log(ctx, PR_LOG_INFO,
            "[WIN32] PostQuitMessage code=%u", ctx->quit_code);
    return 0;
}

static uint64_t f_GetMessageA(pr_win32_ctx* ctx, const uint64_t* a,
                              size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    if (!pr_win32_ptr(ctx, a[0], 48)) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER; return (uint64_t)-1;
    }
    uint32_t fhwnd = (uint32_t)a[1];
    uint32_t fmin = (uint32_t)a[2], fmax = (uint32_t)a[3];
    for (int attempt = 0; attempt < 4; attempt++) {
        if (timers_pump(ctx) != 0) { *st = PR_ERR_UNSUPPORTED; return 0; }
        for (int i = 0; i < ctx->msgq_len; i++) {
            int idx = (ctx->msgq_head + i) % W32_MSGQ_MAX;
            w32_msgq_ent e = ctx->msgq[idx];
            if (fhwnd && e.hwnd && e.hwnd != fhwnd) continue;
            if (fmin && e.message < fmin) continue;
            if (fmax && e.message > fmax) continue;
            msg_write(ctx, a[0], e.hwnd, e.message, e.wparam, e.lparam);
            ctx->msgq_head = (idx + 1) % W32_MSGQ_MAX;
            ctx->msgq_len--;
            /* real: WM_QUIT consumido → return 0 (wParam = código de saída) */
            return (e.message == W32_WM_QUIT) ? 0 : 1;
        }
        if (ctx->quit_posted) {
            /* WM_QUIT já consumido; mantido fixo (sem entrada neste build) */
            msg_write(ctx, a[0], 0, W32_WM_QUIT, ctx->quit_code, 0);
            return 0;
        }
        /* fila vazia: espera LIMITADA pelo próximo timer (nanosleep, sem
         * busy-wait). Sem timer = STOP honesto (pump bloqueante). */
        uint64_t now = w32_now_ms(ctx), next = 0;
        int armed = 0;
        for (int ti = 0; ti < W32_MAX_TIMERS; ti++) {
            if (!ctx->timers[ti].used) continue;
            armed = 1;
            if (!next || ctx->timers[ti].next_ms < next)
                next = ctx->timers[ti].next_ms;
        }
        if (armed) {
            uint64_t wait = next > now ? next - now : 0;
            if (wait > 100) wait = 100;
            struct timespec ts;
            ts.tv_sec = (time_t)(wait / 1000);
            ts.tv_nsec = (long)((wait % 1000) * 1000000ull);
            nanosleep(&ts, NULL);
            continue;
        }
        break;
    }
    diagf(ctx, "user32!GetMessageA: fila vazia e sem WM_QUIT/WM_TIMER — pump "
               "bloqueante sem entrada nao suportado neste build");
    *st = PR_ERR_UNSUPPORTED;
    return 0;
}

static uint64_t f_PeekMessageA(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 5) { *st = PR_ERR_INVALID; return 0; }
    if (!pr_win32_ptr(ctx, a[0], 48)) {
        ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0;
    }
    if (timers_pump(ctx) != 0) { *st = PR_ERR_UNSUPPORTED; return 0; }
    if (ctx->msgq_len == 0) {
        if (ctx->quit_posted) {
            /* WM_QUIT permanece após PostQuitMessage (sem input neste build) */
            msg_write(ctx, a[0], 0, W32_WM_QUIT, ctx->quit_code, 0);
            return 1;
        }
        return 0;   /* real: FALSE sem mensagens */
    }
    int idx = ctx->msgq_head;
    w32_msgq_ent e = ctx->msgq[idx];
    msg_write(ctx, a[0], e.hwnd, e.message, e.wparam, e.lparam);
    if (a[4] /* PM_REMOVE */) {
        ctx->msgq_head = (idx + 1) % W32_MSGQ_MAX;
        ctx->msgq_len--;
    }
    return 1;
}

static uint64_t f_TranslateMessage(pr_win32_ctx* ctx, const uint64_t* a,
                                   size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    const uint8_t* m = (const uint8_t*)pr_win32_ptr(ctx, a[0], 48);
    if (!m) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    uint32_t hwnd = 0, message = 0;
    uint64_t wp = 0, lp = 0;
    memcpy(&hwnd, m + 0, 4);
    memcpy(&message, m + 8, 4);
    memcpy(&wp, m + 16, 8);
    memcpy(&lp, m + 24, 8);
    /* real: WM_KEYDOWN imprimível → WM_CHAR na mesma janela */
    if (message == W32_WM_KEYDOWN) {
        uint32_t ch = vk_to_char(ctx, (uint32_t)wp);
        if (ch) msg_post(ctx, hwnd, W32_WM_CHAR, ch, lp);
    }
    return 1;
}

static uint64_t f_DispatchMessageA(pr_win32_ctx* ctx, const uint64_t* a,
                                   size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    const uint8_t* m = (const uint8_t*)pr_win32_ptr(ctx, a[0], 48);
    if (!m) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    uint32_t hwnd = 0, message = 0;
    uint64_t wp = 0, lp = 0;
    memcpy(&hwnd, m + 0, 4);
    memcpy(&message, m + 8, 4);
    memcpy(&wp, m + 16, 8);
    memcpy(&lp, m + 24, 8);
    if (message == W32_WM_QUIT) return 0;
    w32_window* w = wnd_find(ctx, hwnd);
    if (!w) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    return wnd_send(ctx, w, message, wp, lp, st);
}

static uint64_t f_FillRect(pr_win32_ctx* ctx, const uint64_t* a,
                           size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    gdi_obj* dc = gdi_find(ctx, (uint32_t)a[0], 1);
    if (!dc) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    pr_surf* tgt = gdi_target(ctx, dc);
    if (!tgt) { ctx->last_error = W32_ERROR_INVALID_HANDLE; return 0; }
    const uint8_t* rcp = (const uint8_t*)pr_win32_ptr(ctx, a[1], 16);
    if (!rcp) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    int32_t rc[4];
    memcpy(rc, rcp, 16);
    uint32_t colorref = brush_color_of(ctx, a[2]);
    pr_color c;
    c.r = (float)(colorref & 0xFF) / 255.0f;
    c.g = (float)((colorref >> 8) & 0xFF) / 255.0f;
    c.b = (float)((colorref >> 16) & 0xFF) / 255.0f;
    c.a = 1.0f;
    pr_rect r;
    r.x = (float)rc[0];
    r.y = (float)rc[1];
    r.w = (float)(rc[2] - rc[0]);
    r.h = (float)(rc[3] - rc[1]);
    pr_surf_fill_rect(tgt, r, c);
    if (ctx->log && tgt == ctx->gdi_surface)
        pr_log_write(ctx->log, PR_LOG_INFO, "gdi", "[GDI] surface updated");
    return 1;
}

/* ===================== GRUPO 7: entrada, foco e timers ===================== */

static uint64_t f_PostMessageA(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    uint32_t hwnd = (uint32_t)a[0];
    if (hwnd && !wnd_find(ctx, hwnd)) {
        ctx->last_error = W32_ERROR_INVALID_WINDOW_HANDLE;
        return 0;
    }
    msg_post(ctx, hwnd, (uint32_t)a[1], a[2], a[3]);   /* hwnd=0 = thread msg */
    return 1;
}

static uint64_t f_SetFocus(pr_win32_ctx* ctx, const uint64_t* a,
                           size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint32_t hwnd = (uint32_t)a[0];
    if (hwnd && !wnd_find(ctx, hwnd)) {
        ctx->last_error = W32_ERROR_INVALID_WINDOW_HANDLE;
        return 0;
    }
    uint32_t prev = ctx->focus_hwnd;
    ctx->focus_hwnd = hwnd;
    if (hwnd) input_deliver_parked(ctx, hwnd);
    return prev;
}

static uint64_t f_GetFocus(pr_win32_ctx* ctx, const uint64_t* a,
                           size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    (void)a; (void)n;
    *st = PR_OK;
    return ctx->focus_hwnd;
}

static uint64_t f_SetTimer(pr_win32_ctx* ctx, const uint64_t* a,
                           size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    uint32_t hwnd = (uint32_t)a[0], id = (uint32_t)a[1];
    uint32_t elapse = (uint32_t)a[2];
    uint64_t proc = a[3];
    if (elapse == 0) elapse = 1;   /* real: mínimo de ~1 ms */
    if (!hwnd && !proc) {
        /* real: sem janela exige TIMERPROC */
        ctx->last_error = W32_ERROR_INVALID_PARAMETER;
        return 0;
    }
    if (hwnd && !wnd_find(ctx, hwnd)) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    w32_timer* tm = NULL;
    for (int i = 0; i < W32_MAX_TIMERS; i++) {
        if (ctx->timers[i].used && ctx->timers[i].hwnd == hwnd &&
            ctx->timers[i].id == id) { tm = &ctx->timers[i]; break; }
    }
    if (!tm) {
        for (int i = 0; i < W32_MAX_TIMERS; i++) {
            if (!ctx->timers[i].used) {
                tm = &ctx->timers[i];
                if (!id) id = 0x7000u + (uint32_t)i;   /* id gerado (hwnd=0) */
                break;
            }
        }
    }
    if (!tm) { *st = PR_ERR_NOMEM; return 0; }
    tm->used = 1;
    tm->hwnd = hwnd;
    tm->id = id;
    tm->elapse = elapse;
    tm->proc = proc;
    tm->next_ms = w32_now_ms(ctx) + elapse;
    w32_log(ctx, PR_LOG_INFO,
            "[WIN32] SetTimer id=%u elapse=%u hwnd=0x%08X", id, elapse, hwnd);
    return id;
}

static uint64_t f_KillTimer(pr_win32_ctx* ctx, const uint64_t* a,
                            size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    uint32_t hwnd = (uint32_t)a[0], id = (uint32_t)a[1];
    for (int i = 0; i < W32_MAX_TIMERS; i++) {
        if (ctx->timers[i].used && ctx->timers[i].hwnd == hwnd &&
            ctx->timers[i].id == id) {
            memset(&ctx->timers[i], 0, sizeof(ctx->timers[i]));
            return 1;
        }
    }
    ctx->last_error = W32_ERROR_INVALID_PARAMETER;
    return 0;
}

static uint64_t f_GetSystemMetrics(pr_win32_ctx* ctx, const uint64_t* a, size_t n, pr_status* st) {
    /* G76: GetSystemMetrics — métricas de tela mínimas para GUI.
     * Contrato Windows: int GetSystemMetrics(int nIndex);
     * Retorna métrica ou 0 se índice desconhecido. Não seta LastError.
     * Portico: valores plausíveis baseados em superfície interna (320x240 nos testes)
     * e métricas típicas Windows. Para UIKit futuro, pode consultar UIScreen. */
    (void)ctx;
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint32_t idx = (uint32_t)a[0];
    int32_t w = 0, h = 0;
    if (ctx && ctx->gdi_surface) {
        w = (int32_t)pr_surf_width(ctx->gdi_surface);
        h = (int32_t)pr_surf_height(ctx->gdi_surface);
        if (w <= 0) w = 320;
        if (h <= 0) h = 240;
    } else {
        w = 320;
        h = 240;
    }
    switch (idx) {
        case 0:  return (uint64_t)(w);          /* SM_CXSCREEN */
        case 1:  return (uint64_t)(h);          /* SM_CYSCREEN */
        case 2:  return 16;                     /* SM_CXVSCROLL */
        case 3:  return 16;                     /* SM_CYHSCROLL */
        case 4:  return 19;                     /* SM_CYCAPTION */
        case 5:  return 1;                      /* SM_CXBORDER */
        case 6:  return 1;                      /* SM_CYBORDER */
        case 7:  return 3;                      /* SM_CXDLGFRAME */
        case 8:  return 3;                      /* SM_CYDLGFRAME */
        case 9:  return 16;                     /* SM_CYVTHUMB */
        case 10: return 16;                     /* SM_CXHTHUMB */
        case 11: return 32;                     /* SM_CXICON */
        case 12: return 32;                     /* SM_CYICON */
        case 13: return 32;                     /* SM_CXCURSOR */
        case 14: return 32;                     /* SM_CYCURSOR */
        case 15: return 19;                     /* SM_CYMENU */
        case 16: return (uint64_t)(w);          /* SM_CXFULLSCREEN */
        case 17: return (uint64_t)(h);          /* SM_CYFULLSCREEN */
        case 18: return 0;                      /* SM_CYKANJIWINDOW */
        case 19: return 1;                      /* SM_MOUSEPRESENT */
        case 20: return 16;                     /* SM_CYVSCROLL */
        case 21: return 16;                     /* SM_CXHSCROLL */
        case 22: return 0;                      /* SM_DEBUG */
        case 23: return 0;                      /* SM_SWAPBUTTON */
        case 24: return 0;                      /* SM_RESERVED1 */
        case 25: return 0;                      /* SM_RESERVED2 */
        case 26: return 0;                      /* SM_RESERVED3 */
        case 27: return 0;                      /* SM_RESERVED4 */
        case 28: return 16;                     /* SM_CXMIN */
        case 29: return 16;                     /* SM_CYMIN */
        case 30: return (uint64_t)(w);          /* SM_CXSIZE */
        case 31: return (uint64_t)(19);         /* SM_CYSIZE */
        case 32: return 3;                      /* SM_CXFRAME */
        case 33: return 3;                      /* SM_CYFRAME */
        case 34: return (uint64_t)(w);          /* SM_CXMINTRACK */
        case 35: return (uint64_t)(h);          /* SM_CYMINTRACK */
        case 36: return 16;                     /* SM_CXDOUBLECLK */
        case 37: return 16;                     /* SM_CYDOUBLECLK */
        case 38: return 32;                     /* SM_CXICONSPACING */
        case 39: return 32;                     /* SM_CYICONSPACING */
        case 40: return 0;                      /* SM_MENUDROPALIGNMENT */
        case 41: return 0;                      /* SM_PENWINDOWS */
        case 42: return 0;                      /* SM_DBCSENABLED */
        case 43: return 0;                      /* SM_CMOUSEBUTTONS */
        case 44: return 0;                      /* SM_SECURE */
        case 45: return 0;                      /* SM_CXEDGE */
        case 46: return 0;                      /* SM_CYEDGE */
        case 47: return 0;                      /* SM_CXMINSPACING */
        case 48: return 0;                      /* SM_CYMINSPACING */
        case 49: return 16;                     /* SM_CXSMICON */
        case 50: return 16;                     /* SM_CYSMICON */
        case 51: return 19;                     /* SM_CYSMCAPTION */
        case 52: return 16;                     /* SM_CXSMSIZE */
        case 53: return 16;                     /* SM_CYSMSIZE */
        case 54: return 16;                     /* SM_CXMENUSIZE */
        case 55: return 19;                     /* SM_CYMENUSIZE */
        case 56: return 0;                      /* SM_ARRANGE */
        case 57: return 16;                     /* SM_CXMINIMIZED */
        case 58: return 16;                     /* SM_CYMINIMIZED */
        case 59: return (uint64_t)(w);          /* SM_CXMAXTRACK */
        case 60: return (uint64_t)(h);          /* SM_CYMAXTRACK */
        case 61: return (uint64_t)(w);          /* SM_CXMAXIMIZED */
        case 62: return (uint64_t)(h);          /* SM_CYMAXIMIZED */
        case 63: return 0;                      /* SM_NETWORK */
        case 64: return 0;                      /* SM_CLEANBOOT */
        case 65: return 16;                     /* SM_CXDRAG */
        case 66: return 16;                     /* SM_CYDRAG */
        case 67: return 0;                      /* SM_SHOWSOUNDS */
        case 68: return 16;                     /* SM_CXMENUCHECK */
        case 69: return 16;                     /* SM_CYMENUCHECK */
        case 70: return 0;                      /* SM_SLOWMACHINE */
        case 71: return 0;                      /* SM_MIDEASTENABLED */
        case 76: return 1;                      /* SM_MOUSEWHEELPRESENT */
        case 78: return (uint64_t)(w);          /* SM_CXVIRTUALSCREEN */
        case 79: return (uint64_t)(h);          /* SM_CYVIRTUALSCREEN */
        case 80: return 0;                      /* SM_XVIRTUALSCREEN */
        case 81: return 0;                      /* SM_YVIRTUALSCREEN */
        case 82: return 1;                      /* SM_CMONITORS */
        case 83: return 0;                      /* SM_SAMEDISPLAYFORMAT */
        default: return 0;
    }
}

static uint64_t f_GetKeyState(pr_win32_ctx* ctx, const uint64_t* a,
                              size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint32_t vk = (uint32_t)a[0] & 0xFF;
    int16_t r = (int16_t)((ctx->key_down[vk] ? 0x8000 : 0) |
                          (ctx->key_toggle[vk] & 1));
    return (uint64_t)(int64_t)r;
}

static uint64_t f_GetAsyncKeyState(pr_win32_ctx* ctx, const uint64_t* a,
                                   size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    uint32_t vk = (uint32_t)a[0] & 0xFF;
    int16_t r = (int16_t)(ctx->key_down[vk] ? 0x8000 : 0);
    return (uint64_t)(int64_t)r;
}

/* ---- API pública da camada de entrada host (separada do Win32) ---- */

pr_status pr_win32_input_key(pr_win32_ctx* ctx, uint32_t vk, int down) {
    if (!ctx || vk > 255) return PR_ERR_INVALID;
    vk &= 0xFF;
    ctx->key_down[vk] = down ? 1 : 0;
    if (down && (vk == 0x14u || vk == 0x90u || vk == 0x91u))
        ctx->key_toggle[vk] ^= 1u;   /* CAPS/NUM/SCROLL */
    uint64_t lp = 1 | (down ? 0ull : (1ull << 30));   /* repeat=1; previous */
    input_post_or_park(ctx, down ? W32_WM_KEYDOWN : W32_WM_KEYUP, vk, lp);
    return PR_OK;
}

pr_status pr_win32_input_char(pr_win32_ctx* ctx, uint32_t ch) {
    if (!ctx) return PR_ERR_INVALID;
    input_post_or_park(ctx, W32_WM_CHAR, ch, 1);
    return PR_OK;
}

pr_status pr_win32_input_mouse(pr_win32_ctx* ctx, uint32_t kind,
                               int32_t x, int32_t y, int32_t wheel) {
    if (!ctx) return PR_ERR_INVALID;
    ctx->mouse_x = x;
    ctx->mouse_y = y;
    uint64_t lp = ((uint64_t)((uint32_t)y & 0xFFFF) << 16) |
                  ((uint32_t)x & 0xFFFF);
    switch (kind) {
    case PR_MOUSE_MOVE:
        input_post_or_park(ctx, W32_WM_MOUSEMOVE, mouse_mk(ctx), lp);
        break;
    case PR_MOUSE_LDOWN:
        ctx->mouse_buttons |= W32_MK_LBUTTON;
        input_post_or_park(ctx, W32_WM_LBUTTONDOWN, mouse_mk(ctx), lp);
        break;
    case PR_MOUSE_LUP:
        ctx->mouse_buttons &= ~W32_MK_LBUTTON;
        input_post_or_park(ctx, W32_WM_LBUTTONUP, mouse_mk(ctx), lp);
        break;
    case PR_MOUSE_RDOWN:
        ctx->mouse_buttons |= W32_MK_RBUTTON;
        input_post_or_park(ctx, W32_WM_RBUTTONDOWN, mouse_mk(ctx), lp);
        break;
    case PR_MOUSE_RUP:
        ctx->mouse_buttons &= ~W32_MK_RBUTTON;
        input_post_or_park(ctx, W32_WM_RBUTTONUP, mouse_mk(ctx), lp);
        break;
    case PR_MOUSE_MDOWN:
        ctx->mouse_buttons |= W32_MK_MBUTTON;
        input_post_or_park(ctx, W32_WM_MBUTTONDOWN, mouse_mk(ctx), lp);
        break;
    case PR_MOUSE_MUP:
        ctx->mouse_buttons &= ~W32_MK_MBUTTON;
        input_post_or_park(ctx, W32_WM_MBUTTONUP, mouse_mk(ctx), lp);
        break;
    case PR_MOUSE_WHEEL: {
        uint64_t wp = ((uint64_t)((uint32_t)wheel & 0xFFFF) << 16) |
                      mouse_mk(ctx);
        input_post_or_park(ctx, W32_WM_MOUSEWHEEL, wp, lp);
        break;
    }
    default:
        return PR_ERR_INVALID;
    }
    return PR_OK;
}

pr_status pr_win32_input_touch(pr_win32_ctx* ctx, uint32_t id,
                               uint32_t phase, int32_t x, int32_t y) {
    if (!ctx) return PR_ERR_INVALID;
    /* toque primário = menor slot ativo (promoção real para mouse) */
    int slot = -1, primary = -1;
    for (int i = 0; i < W32_TOUCH_MAX; i++) {
        if (ctx->touches[i].used && ctx->touches[i].id == id) { slot = i; break; }
    }
    for (int i = 0; i < W32_TOUCH_MAX; i++)
        if (ctx->touches[i].used) { primary = i; break; }

    switch (phase) {
    case PR_TOUCH_BEGAN:
        if (slot < 0) {
            for (int i = 0; i < W32_TOUCH_MAX; i++)
                if (!ctx->touches[i].used) { slot = i; break; }
            if (slot < 0) return PR_ERR_NOMEM;
            ctx->touches[slot].used = 1;
            ctx->touches[slot].id = id;
            if (primary < 0) primary = slot;
        }
        ctx->touches[slot].x = x;
        ctx->touches[slot].y = y;
        if (slot == primary) {
            pr_win32_input_mouse(ctx, PR_MOUSE_MOVE, x, y, 0);
            pr_win32_input_mouse(ctx, PR_MOUSE_LDOWN, x, y, 0);
        }
        break;
    case PR_TOUCH_MOVED:
        if (slot < 0) return PR_ERR_STATE;
        ctx->touches[slot].x = x;
        ctx->touches[slot].y = y;
        if (slot == primary)
            pr_win32_input_mouse(ctx, PR_MOUSE_MOVE, x, y, 0);
        break;
    case PR_TOUCH_ENDED:
    case PR_TOUCH_CANCELLED:
        if (slot < 0) return PR_ERR_STATE;
        if (slot == primary)
            pr_win32_input_mouse(ctx, PR_MOUSE_LUP, x, y, 0);
        memset(&ctx->touches[slot], 0, sizeof(ctx->touches[slot]));
        break;
    default:
        return PR_ERR_INVALID;
    }
    return PR_OK;
}

pr_status pr_win32_input_controller(pr_win32_ctx* ctx, uint32_t kind,
                                    uint32_t id, float value) {
    if (!ctx) return PR_ERR_INVALID;
    /* infraestrutura XInput-futura: estado consultável, sem mensagens */
    switch (kind) {
    case PR_CTRL_BUTTON:
        if (id == 0 || (id & (id - 1)) != 0 || id > (1u << 11)) return PR_ERR_INVALID;
        if (value != 0.0f) ctx->pad.buttons |= id;
        else ctx->pad.buttons &= ~id;
        break;
    case PR_CTRL_AXIS:
        if (id >= PR_AXIS_MAX) return PR_ERR_INVALID;
        ctx->pad.axes[id] = value < -1.0f ? -1.0f : (value > 1.0f ? 1.0f : value);
        break;
    case PR_CTRL_TRIGGER:
        if (id > 1) return PR_ERR_INVALID;
        if (value < 0.0f) value = 0.0f;
        if (value > 1.0f) value = 1.0f;
        if (id == 0) ctx->pad.trigger_lt = value;
        else ctx->pad.trigger_rt = value;
        break;
    default:
        return PR_ERR_INVALID;
    }
    return PR_OK;
}

const struct pr_input_state* pr_win32_input_pad(const pr_win32_ctx* ctx) {
    return ctx ? &ctx->pad : NULL;
}

pr_status pr_win32_advance_time(pr_win32_ctx* ctx, uint32_t ms) {
    if (!ctx) return PR_ERR_INVALID;
    ctx->time_offset_ms += ms;
    return timers_pump(ctx) == 0 ? PR_OK : PR_ERR_UNSUPPORTED;
}

uint32_t pr_win32_focus_hwnd(const pr_win32_ctx* ctx) {
    return ctx ? ctx->focus_hwnd : 0;
}

/* ================= GRUPO 8: OpenGL 1.1 (subconjunto real) ================= */
/* Rasterização por software em pr_gl.c; este arquivo só despacha (f_). Os
 * argumentos float/double chegam como bits (XMM capturado no trap x64 ou
 * bits nos testes) — reinterpretados aqui. */

static pr_gl_state* gls(pr_win32_ctx* ctx) {
    if (!ctx->gl) ctx->gl = pr_gl_new(ctx);
    return ctx->gl;
}

static float a_f32(const uint64_t* a, size_t i) {
    uint32_t bits = (uint32_t)(a[i] & 0xFFFFFFFFu);
    float f;
    memcpy(&f, &bits, 4);
    return f;
}
static double a_f64(const uint64_t* a, size_t i) {
    double d;
    memcpy(&d, &a[i], 8);
    return d;
}

static uint64_t f_wglCreateContext(pr_win32_ctx* ctx, const uint64_t* a,
                                   size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (!gdi_find(ctx, (uint32_t)a[0], 1)) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    return pr_gl_wgl_create(gls(ctx), (uint32_t)a[0]);
}

static uint64_t f_wglMakeCurrent(pr_win32_ctx* ctx, const uint64_t* a,
                                 size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    if (!gdi_find(ctx, (uint32_t)a[0], 1)) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    return (uint64_t)pr_gl_wgl_make_current(gls(ctx), (uint32_t)a[0],
                                            (uint32_t)a[1]);
}

static uint64_t f_wglDeleteContext(pr_win32_ctx* ctx, const uint64_t* a,
                                   size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    return (uint64_t)pr_gl_wgl_delete(gls(ctx), (uint32_t)a[0]);
}

static uint64_t f_SwapBuffers(pr_win32_ctx* ctx, const uint64_t* a,
                              size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    if (!gdi_find(ctx, (uint32_t)a[0], 1)) {
        ctx->last_error = W32_ERROR_INVALID_HANDLE;
        return 0;
    }
    if (!ctx->gdi_surface) {
        ctx->gdi_surface = pr_surf_create(320, 240, PR_SURF_XRGB8888);
        if (!ctx->gdi_surface) { *st = PR_ERR_NOMEM; return 0; }
    }
    return (uint64_t)pr_gl_swap_buffers(gls(ctx), ctx->gdi_surface);
}

static uint64_t f_glViewport(pr_win32_ctx* ctx, const uint64_t* a,
                             size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_viewport(gls(ctx), (int)(int32_t)a[0], (int)(int32_t)a[1],
                   (int)(int32_t)a[2], (int)(int32_t)a[3]);
    return 0;
}

static uint64_t f_glClearColor(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_clear_color(gls(ctx), a_f32(a, 0), a_f32(a, 1),
                      a_f32(a, 2), a_f32(a, 3));
    return 0;
}

static uint64_t f_glClearDepth(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_clear_depth(gls(ctx), a_f64(a, 0));
    return 0;
}

static uint64_t f_glClear(pr_win32_ctx* ctx, const uint64_t* a,
                          size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_clear(gls(ctx), (unsigned)a[0]);
    return 0;
}

static uint64_t f_glEnable(pr_win32_ctx* ctx, const uint64_t* a,
                           size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_enable(gls(ctx), (unsigned)a[0]);
    return 0;
}

static uint64_t f_glShadeModel(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_shade_model(gls(ctx), (unsigned)a[0]);
    return 0;
}

static uint64_t f_glDepthMask(pr_win32_ctx* ctx, const uint64_t* a,
                              size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_depth_mask(gls(ctx), a[0] != 0);
    return 0;
}

static uint64_t f_glDepthFunc(pr_win32_ctx* ctx, const uint64_t* a,
                              size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_depth_func(gls(ctx), (unsigned)a[0]);
    return 0;
}

static uint64_t f_glBegin(pr_win32_ctx* ctx, const uint64_t* a,
                          size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_begin(gls(ctx), (unsigned)a[0]);
    return 0;
}

static uint64_t f_glColor3f(pr_win32_ctx* ctx, const uint64_t* a,
                            size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_color3f(gls(ctx), a_f32(a, 0), a_f32(a, 1), a_f32(a, 2));
    return 0;
}

static uint64_t f_glVertex3f(pr_win32_ctx* ctx, const uint64_t* a,
                             size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_vertex3f(gls(ctx), a_f32(a, 0), a_f32(a, 1), a_f32(a, 2));
    return 0;
}

static uint64_t f_glEnd(pr_win32_ctx* ctx, const uint64_t* a,
                        size_t n, pr_status* st) {
    (void)a; (void)n;
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    pr_gl_end(gls(ctx));
    return 0;
}

static uint64_t f_glFinish(pr_win32_ctx* ctx, const uint64_t* a,
                           size_t n, pr_status* st) {
    (void)a; (void)n;
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    pr_gl_finish(gls(ctx));
    return 0;
}

static uint64_t f_glTexCoord2f(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_tex_coord2f(gls(ctx), a_f32(a, 0), a_f32(a, 1));
    return 0;
}

static uint64_t f_glGetIntegerv(pr_win32_ctx* ctx, const uint64_t* a,
                                size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    unsigned name = (unsigned)a[0];
    size_t need = (name == 0x0BA2u) ? 16 : 4;   /* GL_VIEWPORT escreve 4 ints */
    void* hp = pr_win32_ptr(ctx, a[1], need);
    if (!hp) { *st = PR_ERR_FAULT; return 0; }
    pr_gl_get_integer_v(gls(ctx), name, (int*)hp);
    return 0;
}

static uint64_t f_glGetString(pr_win32_ctx* ctx, const uint64_t* a,
                              size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    unsigned name = (unsigned)a[0];
    const char* s = pr_gl_get_string(gls(ctx), name);
    if (!s) return 0;
    /* ponteiro de convidado ESTÁVEL (heap do processo; padrão GetCommandLine) */
    if (name >= 0x1F00u && name <= 0x1F03u) {
        unsigned slot = name - 0x1F00u;
        if (!ctx->glstr[slot]) {
            size_t len = strlen(s);
            uint64_t guest = heap_alloc_impl(ctx, (uint64_t)len + 1u, st);
            void* hp = guest ? pr_win32_ptr(ctx, guest, len + 1) : NULL;
            if (!hp) { *st = PR_ERR_FAULT; return 0; }
            memcpy(hp, s, len + 1);
            ctx->glstr[slot] = guest;
        }
        return ctx->glstr[slot];
    }
    return 0;
}

static uint64_t f_glGetError(pr_win32_ctx* ctx, const uint64_t* a,
                             size_t n, pr_status* st) {
    (void)a; (void)n;
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    return pr_gl_get_error(gls(ctx));
}

static uint64_t f_glReadPixels(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 7) { *st = PR_ERR_INVALID; return 0; }
    int x = (int)(int32_t)a[0], y = (int)(int32_t)a[1];
    int w = (int)(int32_t)a[2], h = (int)(int32_t)a[3];
    void* out = NULL;
    if (w > 0 && h > 0) {
        out = pr_win32_ptr(ctx, a[6], (size_t)w * (size_t)h * 4);
        if (!out) { ctx->last_error = W32_ERROR_INVALID_PARAMETER; return 0; }
    }
    pr_gl_read_pixels(gls(ctx), x, y, w, h, (unsigned)a[4], (unsigned)a[5], out);
    return 0;
}

/* ============ GRUPO 9: GL matrizes + vertex arrays (despacho) ============ */

static void* gl_resolve(void* ud, uint64_t addr, size_t bytes) {
    return pr_win32_ptr((pr_win32_ctx*)ud, addr, bytes);
}

static uint64_t f_glMatrixMode(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_matrix_mode(gls(ctx), (unsigned)a[0]);
    return 0;
}

static uint64_t f_glLoadIdentity(pr_win32_ctx* ctx, const uint64_t* a,
                                 size_t n, pr_status* st) {
    (void)a; (void)n;
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    pr_gl_load_identity(gls(ctx));
    return 0;
}

static uint64_t f_glOrtho(pr_win32_ctx* ctx, const uint64_t* a,
                          size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 6) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_ortho(gls(ctx), a_f64(a, 0), a_f64(a, 1), a_f64(a, 2),
                a_f64(a, 3), a_f64(a, 4), a_f64(a, 5));
    return 0;
}

static uint64_t f_glTranslatef(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_translatef(gls(ctx), a_f32(a, 0), a_f32(a, 1), a_f32(a, 2));
    return 0;
}

static uint64_t f_glRotatef(pr_win32_ctx* ctx, const uint64_t* a,
                            size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_rotatef(gls(ctx), a_f32(a, 0), a_f32(a, 1),
                  a_f32(a, 2), a_f32(a, 3));
    return 0;
}

/* ---- GRUPO 10: matrix stack + glFrustum + glDrawArrays + texturas ---- */

static uint64_t f_glPushMatrix(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    (void)a; (void)n;
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    pr_gl_push_matrix(gls(ctx));
    return 0;
}

static uint64_t f_glPopMatrix(pr_win32_ctx* ctx, const uint64_t* a,
                              size_t n, pr_status* st) {
    (void)a; (void)n;
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    pr_gl_pop_matrix(gls(ctx));
    return 0;
}

static uint64_t f_glFrustum(pr_win32_ctx* ctx, const uint64_t* a,
                            size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 6) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_frustum(gls(ctx), a_f64(a, 0), a_f64(a, 1), a_f64(a, 2),
                  a_f64(a, 3), a_f64(a, 4), a_f64(a, 5));
    return 0;
}

static uint64_t f_glGenTextures(pr_win32_ctx* ctx, const uint64_t* a,
                                size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    uint32_t* out = (uint32_t*)pr_win32_ptr(ctx, a[1], (size_t)a[0] * 4);
    if (!out) { *st = PR_ERR_RANGE; return 0; }
    pr_gl_gen_textures(gls(ctx), (int)(int32_t)a[0], out);
    return 0;
}

static uint64_t f_glDeleteTextures(pr_win32_ctx* ctx, const uint64_t* a,
                                   size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    if (a[0] > UINT64_MAX / 4u) { *st = PR_ERR_RANGE; return 0; }  /* G17 */
    const uint32_t* names = (const uint32_t*)pr_win32_ptr(
        ctx, a[1], (size_t)a[0] * 4u);
    if (!names) { *st = PR_ERR_RANGE; return 0; }
    pr_gl_delete_textures(gls(ctx), (int)(int32_t)a[0], names);
    return 0;
}

static uint64_t f_glBindTexture(pr_win32_ctx* ctx, const uint64_t* a,
                                size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_bind_texture(gls(ctx), (unsigned)a[0], (uint32_t)a[1]);
    return 0;
}

static uint64_t f_glTexParameteri(pr_win32_ctx* ctx, const uint64_t* a,
                                  size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_tex_parameteri(gls(ctx), (unsigned)a[0], (unsigned)a[1],
                         (int)(int32_t)a[2]);
    return 0;
}

static uint64_t f_glTexImage2D(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 9) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_tex_image2d(gls(ctx), (unsigned)a[0], (int)(int32_t)a[1],
                      (int)(int32_t)a[2], (int)(int32_t)a[3],
                      (int)(int32_t)a[4], (int)(int32_t)a[5],
                      (unsigned)a[6], (unsigned)a[7], a[8], gl_resolve, ctx);
    return 0;
}

static uint64_t f_glTexCoordPointer(pr_win32_ctx* ctx, const uint64_t* a,
                                    size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_tex_coord_pointer(gls(ctx), (int)(int32_t)a[0], (unsigned)a[1],
                            (int)(int32_t)a[2], a[3]);
    return 0;
}

static uint64_t f_glDrawArrays(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_draw_arrays(gls(ctx), (unsigned)a[0], (int)(int32_t)a[1],
                      (int)(int32_t)a[2], gl_resolve, ctx);
    return 0;
}

static uint64_t f_glDisable(pr_win32_ctx* ctx, const uint64_t* a,
                            size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_disable(gls(ctx), (unsigned)a[0]);
    return 0;
}

/* ---- GRUPO 11: iluminação fixed-function + blending/alpha ---- */

static uint64_t f_glNormal3f(pr_win32_ctx* ctx, const uint64_t* a,
                             size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_normal3f(gls(ctx), a_f32(a, 0), a_f32(a, 1), a_f32(a, 2));
    return 0;
}

static uint64_t f_glColor4f(pr_win32_ctx* ctx, const uint64_t* a,
                            size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_color4f(gls(ctx), a_f32(a, 0), a_f32(a, 1), a_f32(a, 2),
                  a_f32(a, 3));
    return 0;
}

static uint64_t f_glNormalPointer(pr_win32_ctx* ctx, const uint64_t* a,
                                  size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_normal_pointer(gls(ctx), (unsigned)a[0], (int)(int32_t)a[1], a[2]);
    return 0;
}

static uint64_t f_glLightfv(pr_win32_ctx* ctx, const uint64_t* a,
                            size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    const float* v = (const float*)gl_resolve(ctx, a[2], 16);
    if (!v) { *st = PR_ERR_RANGE; return 0; }
    pr_gl_light_fv(gls(ctx), (unsigned)a[0], (unsigned)a[1], v);
    return 0;
}

static uint64_t f_glMaterialfv(pr_win32_ctx* ctx, const uint64_t* a,
                               size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    const float* v = (const float*)gl_resolve(ctx, a[2], 16);
    if (!v) { *st = PR_ERR_RANGE; return 0; }
    pr_gl_material_fv(gls(ctx), (unsigned)a[0], (unsigned)a[1], v);
    return 0;
}

static uint64_t f_glBlendFunc(pr_win32_ctx* ctx, const uint64_t* a,
                              size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 2) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_blend_func(gls(ctx), (unsigned)a[0], (unsigned)a[1]);
    return 0;
}

static uint64_t f_glScalef(pr_win32_ctx* ctx, const uint64_t* a,
                           size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 3) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_scalef(gls(ctx), a_f32(a, 0), a_f32(a, 1), a_f32(a, 2));
    return 0;
}

static uint64_t f_glEnableClientState(pr_win32_ctx* ctx, const uint64_t* a,
                                      size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_enable_client_state(gls(ctx), (unsigned)a[0]);
    return 0;
}

static uint64_t f_glDisableClientState(pr_win32_ctx* ctx, const uint64_t* a,
                                       size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 1) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_disable_client_state(gls(ctx), (unsigned)a[0]);
    return 0;
}

static uint64_t f_glVertexPointer(pr_win32_ctx* ctx, const uint64_t* a,
                                  size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_vertex_pointer(gls(ctx), (int)(int32_t)a[0], (unsigned)a[1],
                         (int)(int32_t)a[2], a[3]);
    return 0;
}

static uint64_t f_glColorPointer(pr_win32_ctx* ctx, const uint64_t* a,
                                 size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_color_pointer(gls(ctx), (int)(int32_t)a[0], (unsigned)a[1],
                        (int)(int32_t)a[2], a[3]);
    return 0;
}

static uint64_t f_glDrawElements(pr_win32_ctx* ctx, const uint64_t* a,
                                 size_t n, pr_status* st) {
    if (!ctx) { if (st) *st = PR_ERR_INVALID; return 0; }
    *st = PR_OK;
    if (n < 4) { *st = PR_ERR_INVALID; return 0; }
    pr_gl_draw_elements(gls(ctx), (unsigned)a[0], (int)(int32_t)a[1],
                        (unsigned)a[2], a[3], gl_resolve, ctx);
    return 0;
}

#define IMPL_NOTE(mod, name, fn, b, note) { mod, name, 0, PR_WIN32_IMPLEMENTED, fn, (uint16_t)(b), note, 0, 0 }
#define IMPL_FX(mod, name, fn, b, xmm, note) { mod, name, 0, PR_WIN32_IMPLEMENTED, fn, (uint16_t)(b), note, 0, (uint8_t)(xmm) }
#define DATA_SYM(mod, name) { mod, name, 0, PR_WIN32_IMPLEMENTED, NULL, 0xFFFFu, "dados exportados (celula RW no convidado)", 1, 0 }

static const pr_win32_export g_catalog[] = {
    /* kernel32 — implementadas */
    IMPL("kernel32.dll", "GetTickCount64", f_GetTickCount64, 0),
    IMPL("kernel32.dll", "QueryPerformanceFrequency", f_QueryPerformanceFrequency, 4),
    IMPL("kernel32.dll", "QueryPerformanceCounter", f_QueryPerformanceCounter, 4),
    IMPL("kernel32.dll", "GetProcessHeap", f_GetProcessHeap, 0),
    IMPL("kernel32.dll", "HeapAlloc", f_HeapAlloc, 12),
    IMPL("kernel32.dll", "HeapFree", f_HeapFree, 12),
    IMPL("kernel32.dll", "HeapSize", f_HeapSize, 12),
    IMPL("kernel32.dll", "VirtualAlloc", f_VirtualAlloc, 16),
    IMPL("kernel32.dll", "VirtualFree", f_VirtualFree, 12),
    IMPL("kernel32.dll", "GetStdHandle", f_GetStdHandle, 4),
    IMPL("kernel32.dll", "WriteFile", f_WriteFile, 20),
    IMPL("kernel32.dll", "ReadFile", f_ReadFile, 20),
    IMPL("kernel32.dll", "SetLastError", f_SetLastError, 4),
    IMPL("kernel32.dll", "GetLastError", f_GetLastError, 0),
    IMPL("kernel32.dll", "lstrlenA", f_lstrlenA, 4),
    IMPL("kernel32.dll", "lstrcpyA", f_lstrcpyA, 8),
    IMPL("kernel32.dll", "lstrcpynA", f_lstrcpynA, 12),
    IMPL("kernel32.dll", "lstrcmpA", f_lstrcmpA, 8),
    IMPL("kernel32.dll", "ExitProcess", f_ExitProcess, 4),
    IMPL("kernel32.dll", "TerminateProcess", f_TerminateProcess, 8),
    IMPL("kernel32.dll", "GetCurrentProcessId", f_GetCurrentProcessId, 0),
    IMPL("kernel32.dll", "GetCurrentThreadId", f_GetCurrentThreadId, 0),
    IMPL("kernel32.dll", "IsDebuggerPresent", f_IsDebuggerPresent, 0),
    IMPL("kernel32.dll", "GetModuleHandleA", f_GetModuleHandleA, 4),
    IMPL("kernel32.dll", "GetProcAddress", f_GetProcAddress, 8),
    IMPL("kernel32.dll", "LoadLibraryA", f_LoadLibraryA, 4),
    IMPL("kernel32.dll", "LoadLibraryW", f_LoadLibraryW, 4),
    IMPL("kernel32.dll", "FreeLibrary", f_FreeLibrary, 4),
    IMPL("kernel32.dll", "VirtualQuery", f_VirtualQuery, 24),
    IMPL("kernel32.dll", "IsDBCSLeadByteEx", f_IsDBCSLeadByteEx, 8),
    IMPL("kernel32.dll", "MultiByteToWideChar", f_MultiByteToWideChar, 24),
    IMPL("kernel32.dll", "WideCharToMultiByte", f_WideCharToMultiByte, 32),
    IMPL("kernel32.dll", "VirtualProtect", f_VirtualProtect, 16),
    IMPL("kernel32.dll", "TlsGetValue", f_TlsGetValue, 4),
    IMPL("kernel32.dll", "Sleep", f_Sleep, 4),
    IMPL("kernel32.dll", "SetUnhandledExceptionFilter", f_SetUnhandledExceptionFilter, 4),
    IMPL("kernel32.dll", "DeleteCriticalSection", f_DeleteCriticalSection, 4),
    IMPL("kernel32.dll", "LeaveCriticalSection", f_LeaveCriticalSection, 4),
    IMPL("kernel32.dll", "EnterCriticalSection", f_EnterCriticalSection, 4),
    IMPL("kernel32.dll", "InitializeCriticalSection", f_InitializeCriticalSection, 4),
    IMPL("kernel32.dll", "CreateFileA", f_CreateFileA, 28),
    IMPL("kernel32.dll", "CreateFileW", f_CreateFileW, 28),
    IMPL("kernel32.dll", "CloseHandle", f_CloseHandle, 4),
    IMPL("kernel32.dll", "CreateThread", f_CreateThread, 24),
    IMPL("kernel32.dll", "WaitForSingleObject", f_WaitForSingleObject, 8),
    IMPL("kernel32.dll", "WaitForMultipleObjects", f_WaitForMultipleObjects, 16),
    IMPL("kernel32.dll", "CreateMutexA", f_CreateMutexA, 12),
    IMPL("kernel32.dll", "ReleaseMutex", f_ReleaseMutex, 4),
    IMPL("kernel32.dll", "CreateEventA", f_CreateEventA, 16),
    IMPL("kernel32.dll", "CreateEventW", f_CreateEventW, 16),
    IMPL("kernel32.dll", "SetEvent", f_SetEvent, 4),
    IMPL("kernel32.dll", "ResetEvent", f_ResetEvent, 4),
    IMPL("kernel32.dll", "FindFirstFileW", f_FindFirstFileW, 8),
    IMPL("kernel32.dll", "FindNextFileW", f_FindNextFileW, 8),
    IMPL("kernel32.dll", "FindFirstFileA", f_FindFirstFileA, 8),
    IMPL("kernel32.dll", "FindNextFileA", f_FindNextFileA, 8),
    IMPL("kernel32.dll", "FindClose", f_FindClose, 4),
    IMPL("kernel32.dll", "GetFileSize", f_GetFileSize, 8),
    IMPL("kernel32.dll", "SetFilePointer", f_SetFilePointer, 16),
    IMPL("kernel32.dll", "GetCommandLineA", f_GetCommandLineA, 0),
    IMPL("kernel32.dll", "OutputDebugStringA", f_OutputDebugStringA, 4),
    IMPL("kernel32.dll", "GetVersion", f_GetVersion, 0),
    IMPL("kernel32.dll", "GetVersionExA", f_GetVersionExA, 4),
    IMPL("kernel32.dll", "GetModuleHandleW", f_GetModuleHandleW, 4),
    IMPL("kernel32.dll", "GetCommandLineW", f_GetCommandLineW, 0),
    IMPL_NOTE("kernel32.dll", "GetEnvironmentVariableA", f_GetEnvironmentVariableA, 12, "ambiente do processo (Portico)"),
    IMPL_NOTE("kernel32.dll", "GetEnvironmentVariableW", f_GetEnvironmentVariableW, 12, "ambiente do processo (Portico)"),
    IMPL_NOTE("kernel32.dll", "GetSystemInfo", f_GetSystemInfo, 4, "valores do espaco do Portico"),
    IMPL("kernel32.dll", "GetCurrentDirectoryA", f_GetCurrentDirectoryA, 8),
    IMPL("kernel32.dll", "GetCurrentDirectoryW", f_GetCurrentDirectoryW, 8),
    IMPL("kernel32.dll", "ReadFileEx", f_ReadFileEx, 20),
    IMPL("kernel32.dll", "GetModuleFileNameA", f_GetModuleFileNameA, 12),
    IMPL("kernel32.dll", "GetModuleFileNameW", f_GetModuleFileNameW, 12),
    IMPL("kernel32.dll", "GetStartupInfoA", f_GetStartupInfoA, 4),
    IMPL("kernel32.dll", "GetFileAttributesA", f_GetFileAttributesA, 4),
    IMPL("kernel32.dll", "GetFileAttributesW", f_GetFileAttributesW, 4),
    IMPL("kernel32.dll", "GetFileAttributesExA", f_GetFileAttributesExA, 12),
    IMPL("kernel32.dll", "GetFileAttributesExW", f_GetFileAttributesExW, 12),
    IMPL("kernel32.dll", "CreateDirectoryA", f_CreateDirectoryA, 8),
    IMPL("kernel32.dll", "CreateDirectoryW", f_CreateDirectoryW, 8),
    IMPL("kernel32.dll", "RemoveDirectoryA", f_RemoveDirectoryA, 4),
    IMPL("kernel32.dll", "RemoveDirectoryW", f_RemoveDirectoryW, 4),
    IMPL("kernel32.dll", "DeleteFileA", f_DeleteFileA, 4),
    IMPL("kernel32.dll", "DeleteFileW", f_DeleteFileW, 4),

    TODO("kernel32.dll", "SetFilePointer", "E/S de arquivos"),
    TODO("kernel32.dll", "CloseHandle", "fechamento de handles genéricos"),
    IMPL("kernel32.dll", "TlsAlloc", f_TlsAlloc, 0),
    IMPL("kernel32.dll", "TlsSetValue", f_TlsSetValue, 8),
    IMPL("kernel32.dll", "TlsFree", f_TlsFree, 4),
    TODO("kernel32.dll", "MultiByteToWideChar", "conversão de código de página"),
    TODO("kernel32.dll", "WideCharToMultiByte", "conversão de código de página"),
    /* user32 */
    IMPL_NOTE("user32.dll", "MessageBoxA", f_MessageBoxA, 16, "sem UI: registra texto e retorna IDOK"),
    IMPL_NOTE("user32.dll", "MessageBoxW", f_MessageBoxW, 16, "sem UI: registra texto e retorna IDOK"),
    IMPL_NOTE("user32.dll", "RegisterClassA", f_RegisterClassA, 4, "classes reais; hbrBackground = brush ou (COLOR_xxx+1)"),
    IMPL_NOTE("user32.dll", "CreateWindowExA", f_CreateWindowExA, 48, "janela real -> superficie interna; sem area nao-cliente"),
    IMPL_NOTE("user32.dll", "ShowWindow", f_ShowWindow, 8, "SW_HIDE/visivel; envia WM_SHOWWINDOW"),
    IMPL("user32.dll", "GetClientRect", f_GetClientRect, 8),
    IMPL("user32.dll", "GetDC", f_GetDC, 4),
    IMPL("user32.dll", "ReleaseDC", f_ReleaseDC, 8),
    IMPL_NOTE("user32.dll", "InvalidateRect", f_InvalidateRect, 12, "regiao parcial tratada como janela inteira"),
    IMPL_NOTE("user32.dll", "UpdateWindow", f_UpdateWindow, 4, "WM_ERASEBKGND+WM_PAINT sincronos (SendMessage)"),
    IMPL_NOTE("user32.dll", "BeginPaint", f_BeginPaint, 8, "fundo apagado aqui com a brush da classe (subset do DefWindowProc)"),
    IMPL("user32.dll", "EndPaint", f_EndPaint, 8),
    IMPL("user32.dll", "DestroyWindow", f_DestroyWindow, 4),
    IMPL("user32.dll", "PostQuitMessage", f_PostQuitMessage, 4),
    IMPL_NOTE("user32.dll", "GetMessageA", f_GetMessageA, 16, "fila real+WM_TIMER; espera limitada por timer; vazia = EXECUTION STOPPED"),
    IMPL_NOTE("user32.dll", "PeekMessageA", f_PeekMessageA, 20, "fila real; WM_QUIT permanece pos-PostQuitMessage"),
    IMPL_NOTE("user32.dll", "TranslateMessage", f_TranslateMessage, 4, "WM_KEYDOWN imprimivel -> WM_CHAR (ASCII; sem mapa OEM)"),
    IMPL_NOTE("user32.dll", "DispatchMessageA", f_DispatchMessageA, 4, "WndProc via reentrada do convidado (guest_call)"),
    IMPL_NOTE("user32.dll", "DefWindowProcA", f_DefWindowProcA, 16, "WM_ERASEBKGND/WM_PAINT/WM_CLOSE reais; demais retornam 0"),
    IMPL("user32.dll", "FillRect", f_FillRect, 12),
    IMPL_NOTE("user32.dll", "PostMessageA", f_PostMessageA, 16, "fila real; hwnd=0 = mensagem de thread"),
    IMPL_NOTE("user32.dll", "PostMessageW", f_PostMessageA, 16, "alias W (sem strings no caminho)"),
    IMPL_NOTE("user32.dll", "GetMessageW", f_GetMessageA, 16, "alias W"),
    IMPL_NOTE("user32.dll", "PeekMessageW", f_PeekMessageA, 20, "alias W"),
    IMPL_NOTE("user32.dll", "DispatchMessageW", f_DispatchMessageA, 4, "alias W"),
    IMPL_NOTE("user32.dll", "DefWindowProcW", f_DefWindowProcA, 16, "alias W"),
    IMPL("user32.dll", "SetFocus", f_SetFocus, 4),
    IMPL("user32.dll", "GetFocus", f_GetFocus, 0),
    IMPL_NOTE("user32.dll", "SetTimer", f_SetTimer, 16, "WM_TIMER real; TIMERPROC via reentrada"),
    IMPL("user32.dll", "KillTimer", f_KillTimer, 8),
    IMPL_NOTE("user32.dll", "GetKeyState", f_GetKeyState, 4, "bit15=pressionada; bit0=toggle"),
    IMPL_NOTE("user32.dll", "GetAsyncKeyState", f_GetAsyncKeyState, 4, "bit15=pressionada (thread unica)"),
    IMPL("user32.dll", "GetSystemMetrics", f_GetSystemMetrics, 4),
    TODO("user32.dll", "GetSystemMetrics", "métricas de tela (UIKit)"),
    TODO("user32.dll", "SetTimer", "timers de janela"),
    /* advapi32 — Registry virtualizado (G84) */
    IMPL("advapi32.dll", "RegCreateKeyExA", f_RegCreateKeyExA, 36),
    IMPL("advapi32.dll", "RegCreateKeyExW", f_RegCreateKeyExW, 36),
    IMPL("advapi32.dll", "RegOpenKeyExA", f_RegOpenKeyExA, 20),
    IMPL("advapi32.dll", "RegOpenKeyExW", f_RegOpenKeyExW, 20),
    IMPL("advapi32.dll", "RegCloseKey", f_RegCloseKey, 4),
    IMPL("advapi32.dll", "RegSetValueExA", f_RegSetValueExA, 24),
    IMPL("advapi32.dll", "RegSetValueExW", f_RegSetValueExW, 24),
    IMPL("advapi32.dll", "RegQueryValueExA", f_RegQueryValueExA, 24),
    IMPL("advapi32.dll", "RegQueryValueExW", f_RegQueryValueExW, 24),
    IMPL("advapi32.dll", "RegDeleteValueA", f_RegDeleteValueA, 8),
    IMPL("advapi32.dll", "RegDeleteValueW", f_RegDeleteValueW, 8),
    IMPL("advapi32.dll", "RegDeleteKeyA", f_RegDeleteKeyA, 8),
    IMPL("advapi32.dll", "RegDeleteKeyW", f_RegDeleteKeyW, 8),
    IMPL("advapi32.dll", "RegEnumKeyExA", f_RegEnumKeyExA, 32),
    IMPL("advapi32.dll", "RegEnumKeyExW", f_RegEnumKeyExW, 32),
    IMPL("advapi32.dll", "RegEnumValueA", f_RegEnumValueA, 32),
    IMPL("advapi32.dll", "RegEnumValueW", f_RegEnumValueW, 32),
    /* gdi32 — implementadas (superfície virtual → pr_surf → Metal) */
    IMPL("gdi32.dll", "CreateCompatibleDC", f_CreateCompatibleDC, 4),
    IMPL("gdi32.dll", "CreateSolidBrush", f_CreateSolidBrush, 4),
    IMPL("gdi32.dll", "SelectObject", f_SelectObject, 8),
    IMPL("gdi32.dll", "DeleteObject", f_DeleteObject, 4),
    IMPL("gdi32.dll", "PatBlt", f_PatBlt, 24),
    IMPL("gdi32.dll", "GetDeviceCaps", f_GetDeviceCaps, 8),
    /* ws2_32 — ordem de bytes (ordinais públicos e estáveis do winsock.def:
     * htonl @8, htons @9, ntohl @14, ntohs @15) */
    IMPL_ORD("ws2_32.dll", "htonl", f_htonl, 4, 8),
    IMPL_ORD("ws2_32.dll", "htons", f_htons, 4, 9),
    IMPL_ORD("ws2_32.dll", "ntohl", f_ntohl, 4, 14),
    IMPL_ORD("ws2_32.dll", "ntohs", f_ntohs, 4, 15),
    IMPL("gdi32.dll", "CreateCompatibleBitmap", f_CreateCompatibleBitmap, 12),
    IMPL("gdi32.dll", "BitBlt", f_BitBlt, 36),
    IMPL("gdi32.dll", "DeleteDC", f_DeleteDC, 4),
    IMPL("gdi32.dll", "SetPixel", f_SetPixel, 16),
    IMPL("gdi32.dll", "GetPixel", f_GetPixel, 12),
    IMPL_NOTE("gdi32.dll", "StretchBlt", f_StretchBlt, 44, "SRCCOPY vizinho mais próximo"),
    IMPL_NOTE("gdi32.dll", "SwapBuffers", f_SwapBuffers, 4, "apresenta framebuffer GL na superficie XRGB"),
    /* gdi32 — catalogadas (sem implementação real) */
    TODO("gdi32.dll", "CreateFontA", "fontes GDI"),
    TODO("gdi32.dll", "TextOutA", "texto GDI (requer fonte rasterizada completa)"),
    /* ---- opengl32: subconjunto REAL do GL 1.1 por software (GRUPO 8) ----
     * Somente as funções exercitadas pelo PE de controle (hello_gl.exe).
     * Toda a base restante do GL permanece fora do catálogo → chamada =
     * EXECUTION STOPPED honesto (stub de diagnóstico do loader). */
    IMPL_NOTE("opengl32.dll", "wglCreateContext", f_wglCreateContext, 4, "contexto GL1.1 software (320x240 XRGB + z)"),
    IMPL_NOTE("opengl32.dll", "wglMakeCurrent", f_wglMakeCurrent, 8, "contexto atual por estado (thread unica)"),
    IMPL("opengl32.dll", "wglDeleteContext", f_wglDeleteContext, 4),
    IMPL_FX("opengl32.dll", "glClearColor", f_glClearColor, 16, 0x0F, "4 floats em XMM0-3 (ABI x64)"),
    IMPL_FX("opengl32.dll", "glClearDepth", f_glClearDepth, 8, 0x01, "1 double em XMM0 (ABI x64)"),
    IMPL_NOTE("opengl32.dll", "glClear", f_glClear, 4, "GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT"),
    IMPL_NOTE("opengl32.dll", "glEnable", f_glEnable, 4, "subconjunto: GL_DEPTH_TEST"),
    IMPL("opengl32.dll", "glViewport", f_glViewport, 16),
    IMPL_NOTE("opengl32.dll", "glBegin", f_glBegin, 4, "subconjunto: GL_TRIANGLES"),
    IMPL_FX("opengl32.dll", "glColor3f", f_glColor3f, 12, 0x07, "3 floats em XMM0-2 (ABI x64)"),
    IMPL_FX("opengl32.dll", "glVertex3f", f_glVertex3f, 12, 0x07, "3 floats em XMM0-2 (clip-space)"),
    IMPL_NOTE("opengl32.dll", "glEnd", f_glEnd, 0, "rasteriza triangulos (barycentrico + z-buffer GL_LESS)"),
    IMPL("opengl32.dll", "glFinish", f_glFinish, 0),
    IMPL("opengl32.dll", "glGetError", f_glGetError, 0),
    IMPL_FX("opengl32.dll", "glTexCoord2f", f_glTexCoord2f, 8, 0x03, "2 floats em XMM0-1"),
    IMPL("opengl32.dll", "glGetIntegerv", f_glGetIntegerv, 8),
    IMPL("opengl32.dll", "glGetString", f_glGetString, 4),
    IMPL_NOTE("opengl32.dll", "glReadPixels", f_glReadPixels, 28, "GL_RGBA + GL_UNSIGNED_BYTE"),
    IMPL_NOTE("opengl32.dll", "glMatrixMode", f_glMatrixMode, 4, "GL_MODELVIEW|GL_PROJECTION"),
    IMPL("opengl32.dll", "glLoadIdentity", f_glLoadIdentity, 0),
    IMPL_FX("opengl32.dll", "glOrtho", f_glOrtho, 48, 0x0F, "6 doubles: 4 em XMM0-3, 2 na pilha (ABI x64)"),
    IMPL_FX("opengl32.dll", "glTranslatef", f_glTranslatef, 12, 0x07, "3 floats em XMM0-2"),
    IMPL_FX("opengl32.dll", "glRotatef", f_glRotatef, 16, 0x0F, "4 floats em XMM0-3"),
    IMPL_FX("opengl32.dll", "glScalef", f_glScalef, 12, 0x07, "3 floats em XMM0-2"),
    IMPL_NOTE("opengl32.dll", "glEnableClientState", f_glEnableClientState, 4, "GL_VERTEX_ARRAY|GL_COLOR_ARRAY"),
    IMPL_NOTE("opengl32.dll", "glDisableClientState", f_glDisableClientState, 4, "par real do enable (G15)"),
    IMPL_NOTE("opengl32.dll", "glVertexPointer", f_glVertexPointer, 16, "formato: size=3, GL_FLOAT (stride 0/12)"),
    IMPL_NOTE("opengl32.dll", "glColorPointer", f_glColorPointer, 16, "formato: size=3, GL_FLOAT (stride 0/12)"),
    IMPL_NOTE("opengl32.dll", "glDrawElements", f_glDrawElements, 16, "GL_TRIANGLES + GL_UNSIGNED_SHORT (ate 4096 idx)"),
    IMPL_NOTE("opengl32.dll", "glPushMatrix", f_glPushMatrix, 0, "pilhas reais MV 32 / PROJ 4"),
    IMPL_NOTE("opengl32.dll", "glPopMatrix", f_glPopMatrix, 0, "STACK_OVERFLOW 0x503 / UNDERFLOW 0x504"),
    IMPL_FX("opengl32.dll", "glFrustum", f_glFrustum, 48, 0x3F, "6 doubles: 4 em XMM0-3, 2 na pilha (ABI x64)"),
    IMPL_NOTE("opengl32.dll", "glGenTextures", f_glGenTextures, 8, "nomes 1..63 por contexto"),
    IMPL_NOTE("opengl32.dll", "glDeleteTextures", f_glDeleteTextures, 8, "par real do gen (G17)"),
    IMPL_NOTE("opengl32.dll", "glBindTexture", f_glBindTexture, 8, "GL_TEXTURE_2D; bind cria objeto (GL 1.1)"),
    IMPL_NOTE("opengl32.dll", "glTexParameteri", f_glTexParameteri, 12, "GL_NEAREST + GL_REPEAT apenas"),
    IMPL_NOTE("opengl32.dll", "glTexImage2D", f_glTexImage2D, 36, "TEXTURE_2D level 0 RGB/UNSIGNED_BYTE"),
    IMPL_NOTE("opengl32.dll", "glTexCoordPointer", f_glTexCoordPointer, 16, "size 2 GL_FLOAT stride 0/8+"),
    IMPL_NOTE("opengl32.dll", "glDrawArrays", f_glDrawArrays, 12, "GL_TRIANGLES + UV perspectiva correta"),
    IMPL_NOTE("opengl32.dll", "glDisable", f_glDisable, 4, "espelho de glEnable"),
    IMPL_NOTE("opengl32.dll", "glDepthFunc", f_glDepthFunc, 4, "8 modos de comparacao; default GL_LESS (G18)"),
    IMPL_NOTE("opengl32.dll", "glDepthMask", f_glDepthMask, 4, "escritas no z-buffer; flag != 0 = true; afeta glClear de profundidade (G21)"),
    IMPL_NOTE("opengl32.dll", "glShadeModel", f_glShadeModel, 4, "GL_SMOOTH (default) + GL_FLAT (cor do ultimo vertice); enum invalido -> GL_INVALID_ENUM (G22)"),
    IMPL_NOTE("opengl32.dll", "glBlendFunc", f_glBlendFunc, 8, "GL_ZERO|GL_ONE|GL_SRC_ALPHA|GL_ONE_MINUS_SRC_ALPHA"),
    IMPL_FX("opengl32.dll", "glColor4f", f_glColor4f, 16, 0x0F, "4 floats em XMM0-3 (alpha para blend)"),
    IMPL_NOTE("opengl32.dll", "glLightfv", f_glLightfv, 12, "8 luzes; POSITION pelo MV corrente; spot: fora"),
    IMPL_NOTE("opengl32.dll", "glMaterialfv", f_glMaterialfv, 12, "GL_FRONT: AMBIENT|DIFFUSE|SPECULAR|EMISSION|SHININESS"),
    IMPL_FX("opengl32.dll", "glNormal3f", f_glNormal3f, 12, 0x07, "3 floats em XMM0-2"),
    IMPL_NOTE("opengl32.dll", "glNormalPointer", f_glNormalPointer, 12, "GL_FLOAT size 3 (array de normais)"),
    /* ole32 */
    TODO("ole32.dll", "CoInitialize", "COM básico"),
    TODO("ole32.dll", "CoInitializeEx", "COM básico"),
    TODO("ole32.dll", "CoCreateInstance", "COM (CreateDXGIFactory etc.)"),
    TODO("ole32.dll", "CoUninitialize", "COM básico"),
    /* shell32 */
    TODO("shell32.dll", "ShellExecuteA", "abrir URLs/arquivos (sandbox)"),
    TODO("shell32.dll", "CommandLineToArgvW", "parse de linha de comando"),
    /* ws2_32 */
    TODO("ws2_32.dll", "WSAStartup", "sockets (Network.framework)"),
    TODO("ws2_32.dll", "WSACleanup", "sockets"),
    TODO("ws2_32.dll", "socket", "sockets"),
    TODO("ws2_32.dll", "connect", "sockets"),
    TODO("ws2_32.dll", "send", "sockets"),
    TODO("ws2_32.dll", "recv", "sockets"),
    TODO("ws2_32.dll", "closesocket", "sockets"),
    TODO("ws2_32.dll", "getaddrinfo", "DNS"),
    /* ---- msvcrt.dll: fatia real do CRT MinGW (startup CRT real) ---- */
    IMPL("msvcrt.dll", "__set_app_type", f_msvcrt_set_app_type, 4),
    IMPL("msvcrt.dll", "__setusermatherr", f_msvcrt_setusermatherr, 4),
    IMPL("msvcrt.dll", "__getmainargs", f_msvcrt_getmainargs, 40),
    IMPL("msvcrt.dll", "__iob_func", f_msvcrt_iob_func, 0),
    DATA_SYM("msvcrt.dll", "__initenv"),
    DATA_SYM("msvcrt.dll", "_fmode"),
    DATA_SYM("msvcrt.dll", "_commode"),
    IMPL("msvcrt.dll", "_initterm", f_msvcrt_initterm, 16),
    IMPL("msvcrt.dll", "_onexit", f_msvcrt_onexit, 4),
    IMPL("msvcrt.dll", "_cexit", f_msvcrt_cexit, 0),
    IMPL("msvcrt.dll", "_amsg_exit", f_msvcrt_amsg_exit, 4),
    IMPL("msvcrt.dll", "exit", f_msvcrt_exit, 4),
    IMPL("msvcrt.dll", "abort", f_msvcrt_abort, 0),
    IMPL("msvcrt.dll", "malloc", f_msvcrt_malloc, 8),
    IMPL("msvcrt.dll", "calloc", f_msvcrt_calloc, 16),
    IMPL("msvcrt.dll", "free", f_msvcrt_free, 4),
    IMPL("msvcrt.dll", "realloc", f_msvcrt_realloc, 16),
    IMPL("msvcrt.dll", "_lock", f_msvcrt_lock, 4),
    IMPL("msvcrt.dll", "_unlock", f_msvcrt_unlock, 4),
    IMPL("msvcrt.dll", "memcpy", f_msvcrt_memcpy, 24),
    IMPL("msvcrt.dll", "strlen", f_msvcrt_strlen, 8),
    IMPL("msvcrt.dll", "memset", f_memset, 24),
    IMPL("msvcrt.dll", "wcslen", f_msvcrt_wcslen, 8),
    IMPL("msvcrt.dll", "fputc", f_msvcrt_fputc, 16),
    IMPL("msvcrt.dll", "_errno", f_msvcrt_errno, 0),
    IMPL("msvcrt.dll", "strerror", f_msvcrt_strerror, 4),
    IMPL("msvcrt.dll", "strcmp", f_msvcrt_strcmp, 8),
    IMPL("msvcrt.dll", "strcpy", f_msvcrt_strcpy, 8),
    IMPL("msvcrt.dll", "strcat", f_msvcrt_strcat, 8),
    IMPL("msvcrt.dll", "localeconv", f_msvcrt_localeconv, 0),
    IMPL("msvcrt.dll", "___lc_codepage_func", f_lc_codepage, 0),
    IMPL("msvcrt.dll", "___mb_cur_max_func", f_mb_cur_max, 0),
    IMPL("msvcrt.dll", "strncmp", f_msvcrt_strncmp, 24),
    TODO("msvcrt.dll", "__C_specific_handler", "personalidade SEH — so em excecoes; nao suportada nesta etapa"),
    TODO("msvcrt.dll", "signal", "tratamento de sinais — nao suportado nesta etapa"),
    IMPL("msvcrt.dll", "printf", f_msvcrt_printf, 48),
    IMPL("msvcrt.dll", "puts", f_msvcrt_puts, 8),
    IMPL_NOTE("msvcrt.dll", "fprintf", f_msvcrt_fprintf, 48, "stdio formatado real (%d %i %u %x %X %o %c %s %p %f)"),
    IMPL("msvcrt.dll", "fflush", f_msvcrt_fflush, 8),
    IMPL("msvcrt.dll", "fwrite", f_msvcrt_fwrite, 32),
    IMPL("msvcrt.dll", "vfprintf", f_msvcrt_vfprintf, 24),
    /* G86: Filesystem avançado — adicionados no final para preservar índices históricos */
    IMPL("kernel32.dll", "SetCurrentDirectoryA", f_SetCurrentDirectoryA, 4),
    IMPL("kernel32.dll", "SetCurrentDirectoryW", f_SetCurrentDirectoryW, 4),
    IMPL("kernel32.dll", "GetFullPathNameA", f_GetFullPathNameA, 16),
    IMPL("kernel32.dll", "GetFullPathNameW", f_GetFullPathNameW, 16),
    IMPL("kernel32.dll", "MoveFileA", f_MoveFileA, 8),
    IMPL("kernel32.dll", "MoveFileW", f_MoveFileW, 8),
    IMPL("kernel32.dll", "CopyFileA", f_CopyFileA, 12),
    IMPL("kernel32.dll", "CopyFileW", f_CopyFileW, 12),
    IMPL("kernel32.dll", "GetTempPathA", f_GetTempPathA, 8),
    IMPL("kernel32.dll", "GetTempPathW", f_GetTempPathW, 8),
    IMPL("kernel32.dll", "FlushFileBuffers", f_FlushFileBuffers, 4),
    IMPL("kernel32.dll", "SetFilePointerEx", f_SetFilePointerEx, 16),
    IMPL("kernel32.dll", "GetDiskFreeSpaceExA", f_GetDiskFreeSpaceExA, 16),
    IMPL("kernel32.dll", "GetDiskFreeSpaceExW", f_GetDiskFreeSpaceExW, 16),
    IMPL("kernel32.dll", "HeapReAlloc", f_HeapReAlloc, 12),
    IMPL("kernel32.dll", "LocalAlloc", f_LocalAlloc, 8),
    IMPL("kernel32.dll", "LocalFree", f_LocalFree, 4),
    IMPL("kernel32.dll", "LocalReAlloc", f_LocalReAlloc, 12),
    IMPL("kernel32.dll", "LocalSize", f_LocalSize, 4),
    IMPL("kernel32.dll", "GlobalAlloc", f_GlobalAlloc, 8),
    IMPL("kernel32.dll", "GlobalFree", f_GlobalFree, 4),
    IMPL("kernel32.dll", "GlobalReAlloc", f_GlobalReAlloc, 12),
    IMPL("kernel32.dll", "GlobalSize", f_GlobalSize, 4),
    IMPL("kernel32.dll", "CreateMutexW", f_CreateMutexW, 12),
    IMPL("kernel32.dll", "CreateSemaphoreA", f_CreateSemaphoreA, 16),
    IMPL("kernel32.dll", "CreateSemaphoreW", f_CreateSemaphoreW, 16),
    IMPL("kernel32.dll", "ReleaseSemaphore", f_ReleaseSemaphore, 12),
    IMPL("kernel32.dll", "InitializeSRWLock", f_InitializeSRWLock, 4),
    IMPL("kernel32.dll", "AcquireSRWLockExclusive", f_AcquireSRWLockExclusive, 4),
    IMPL("kernel32.dll", "AcquireSRWLockShared", f_AcquireSRWLockShared, 4),
    IMPL("kernel32.dll", "ReleaseSRWLockExclusive", f_ReleaseSRWLockExclusive, 4),
    IMPL("kernel32.dll", "ReleaseSRWLockShared", f_ReleaseSRWLockShared, 4),
    IMPL("kernel32.dll", "ExitThread", f_ExitThread, 4),
    IMPL("kernel32.dll", "GetExitCodeThread", f_GetExitCodeThread, 8),
    IMPL("kernel32.dll", "GetCurrentThread", f_GetCurrentThread, 0),
    IMPL("kernel32.dll", "OpenThread", f_OpenThread, 12),
    IMPL("kernel32.dll", "SuspendThread", f_SuspendThread, 4),
    IMPL("kernel32.dll", "ResumeThread", f_ResumeThread, 4),
    IMPL("advapi32.dll", "RegQueryInfoKeyA", f_RegQueryInfoKeyA, 48),
    IMPL("advapi32.dll", "RegQueryInfoKeyW", f_RegQueryInfoKeyW, 48),
    IMPL("advapi32.dll", "RegFlushKey", f_RegFlushKey, 4),
    IMPL("gdi32.dll", "MoveToEx", f_MoveToEx, 16),
    IMPL("gdi32.dll", "LineTo", f_LineTo, 12),
    IMPL("gdi32.dll", "Rectangle", f_Rectangle, 20),
    IMPL("gdi32.dll", "CreatePen", f_CreatePen, 12),
    IMPL("gdi32.dll", "CreateFontA", f_CreateFontA, 56),
    IMPL("gdi32.dll", "CreateFontW", f_CreateFontW, 56),
    IMPL("gdi32.dll", "TextOutA", f_TextOutA, 20),
    IMPL("gdi32.dll", "TextOutW", f_TextOutW, 20),
    IMPL("gdi32.dll", "DrawTextA", f_DrawTextA, 20),
    IMPL("gdi32.dll", "DrawTextW", f_DrawTextW, 20),
    IMPL("gdi32.dll", "CreateDIBSection", f_CreateDIBSection, 24),

};

size_t pr_win32_catalog(const pr_win32_export** out) {
    if (out) *out = g_catalog;
    return sizeof(g_catalog) / sizeof(g_catalog[0]);
}

static int ieq(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return 0;
        a++; b++;
    }
    return *a == *b;
}

/* compara módulo ignorando sufixo .dll e caixa */
static int mod_eq(const char* a, const char* b) {
    char ba[32], bb[32];
    size_t i;
    for (i = 0; a[i] && i < sizeof(ba) - 1; i++) {
        char c = a[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        ba[i] = c;
    }
    ba[i] = 0;
    for (i = 0; b[i] && i < sizeof(bb) - 1; i++) {
        char c = b[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        bb[i] = c;
    }
    bb[i] = 0;
    if (!strcmp(ba, bb)) return 1;
    /* com/sem .dll */
    size_t la = strlen(ba), lb = strlen(bb);
    if (la > 4 && !strcmp(ba + la - 4, ".dll")) ba[la - 4] = 0;
    if (lb > 4 && !strcmp(bb + lb - 4, ".dll")) bb[lb - 4] = 0;
    return !strcmp(ba, bb);
}

const pr_win32_export* pr_win32_lookup_ordinal(const char* module,
                                               uint32_t ordinal) {
    if (!module || ordinal == 0 || ordinal > 0xFFFF) return NULL;
    for (size_t i = 0; i < sizeof(g_catalog) / sizeof(g_catalog[0]); i++) {
        if (g_catalog[i].ordinal == ordinal &&
            mod_eq(g_catalog[i].module, module))
            return &g_catalog[i];
    }
    return NULL;
}

const pr_win32_export* pr_win32_lookup(const char* module, const char* name) {
    if (!module || !name) return NULL;
    for (size_t i = 0; i < sizeof(g_catalog) / sizeof(g_catalog[0]); i++) {
        if (mod_eq(g_catalog[i].module, module) && ieq(g_catalog[i].name, name))
            return &g_catalog[i];
    }
    return NULL;
}

static const char* g_mod_names[] = {
    "kernel32.dll", "user32.dll", "advapi32.dll", "ws2_32.dll",
    "gdi32.dll", "ole32.dll", "shell32.dll"
};
static pr_win32_module_info g_mod_infos[7];

size_t pr_win32_modules(const pr_win32_module_info** out) {
    size_t nm = sizeof(g_mod_names) / sizeof(g_mod_names[0]);
    for (size_t m = 0; m < nm; m++) {
        g_mod_infos[m].name = g_mod_names[m];
        g_mod_infos[m].implemented = 0;
        g_mod_infos[m].cataloged = 0;
    }
    for (size_t i = 0; i < sizeof(g_catalog) / sizeof(g_catalog[0]); i++) {
        for (size_t m = 0; m < nm; m++) {
            if (mod_eq(g_catalog[i].module, g_mod_names[m])) {
                g_mod_infos[m].cataloged++;
                if (g_catalog[i].status == PR_WIN32_IMPLEMENTED)
                    g_mod_infos[m].implemented++;
                break;
            }
        }
    }
    if (out) *out = g_mod_infos;
    return nm;
}

/* ---- Contexto ---- */

pr_win32_ctx* pr_win32_create(pr_log* log) {
    pr_win32_ctx* ctx = (pr_win32_ctx*)calloc(1, sizeof(pr_win32_ctx));
    if (!ctx) return NULL;
    ctx->log = log;
    ctx->scratch_size = 512;
    ctx->scratch = (uint8_t*)calloc(1, ctx->scratch_size);
    if (!ctx->scratch) { free(ctx); return NULL; }
    ctx->start_ns = mono_ns();
    /* slots TLS 60..63 = reservados internos (lconv/strerror/errno/iob) */
    ctx->tls_used = (1ull << 60) | (1ull << 61) | (1ull << 62) | (1ull << 63);
    /* linha de comando padrão do processo */
    const char* cmdline = "portico";
    memcpy(ctx->scratch, cmdline, strlen(cmdline) + 1);
    return ctx;
}

void pr_win32_destroy(pr_win32_ctx* ctx) {
    if (ctx) w32_threads_shutdown(ctx);
    if (ctx) w32_mutexes_shutdown(ctx);
    if (ctx) w32_events_shutdown(ctx);
    if (ctx) w32_sems_shutdown(ctx);
    if (ctx) w32_finds_shutdown(ctx);
    if (ctx) w32_regs_shutdown(ctx);
    if (!ctx) return;
    pr_gl_release(ctx);
    for (size_t i = 0; i < ctx->nblocks; i++) free(ctx->blocks[i].p);
    free(ctx->blocks);
    free(ctx->gblocks);
    free(ctx->thunk_addrs);
    for (size_t i = 0; i < GDI_MAX; i++) {
        if (ctx->gdi[i].used && ctx->gdi[i].kind == 3) pr_surf_destroy(ctx->gdi[i].surf);
    }
    pr_surf_destroy(ctx->gdi_surface);
    free(ctx->scratch);
    free(ctx->out_buf);
    free(ctx);
}

pr_status pr_win32_bind_image(pr_win32_ctx* ctx, void* base, size_t size,
                              const char* module_name) {
    if (!ctx || !base || size == 0) return PR_ERR_INVALID;
    ctx->img_base = base;
    ctx->img_size = size;
    ctx->module_name[0] = 0;
    if (module_name) {
        size_t j = 0;
        while (module_name[j] && j + 1 < sizeof(ctx->module_name)) {
            ctx->module_name[j] = module_name[j];
            j++;
        }
        ctx->module_name[j] = 0;
    }
    return PR_OK;
}

uint8_t* pr_win32_scratch(pr_win32_ctx* ctx, size_t* out_size) {
    if (out_size) *out_size = ctx ? ctx->scratch_size : 0;
    return ctx ? ctx->scratch : NULL;
}

uint32_t pr_win32_last_error(const pr_win32_ctx* ctx) { return ctx ? ctx->last_error : 0; }
void pr_win32_set_last_error(pr_win32_ctx* ctx, uint32_t err) {
    if (ctx) ctx->last_error = err;
}
int pr_win32_halted(const pr_win32_ctx* ctx) { return ctx ? ctx->halted : 0; }
uint32_t pr_win32_exit_code(const pr_win32_ctx* ctx) { return ctx ? ctx->exit_code : 0; }

size_t pr_win32_stdout_read(pr_win32_ctx* ctx, char* out, size_t cap) {
    if (!ctx || !out || cap == 0) return 0;
    size_t n = ctx->out_len < cap - 1 ? ctx->out_len : cap - 1;
    memcpy(out, ctx->out_buf, n);
    out[n] = 0;
    /* drena */
    if (n < ctx->out_len) {
        memmove(ctx->out_buf, ctx->out_buf + n, ctx->out_len - n);
        ctx->out_len -= n;
    } else {
        ctx->out_len = 0;
    }
    return n;
}

size_t pr_win32_calls_implemented(const pr_win32_ctx* ctx) { return ctx ? ctx->calls_ok : 0; }
size_t pr_win32_calls_unsupported(const pr_win32_ctx* ctx) { return ctx ? ctx->calls_unsup : 0; }

pr_status pr_win32_call(pr_win32_ctx* ctx, const char* module, const char* name,
                        const uint64_t* args, size_t nargs, uint64_t* out_ret) {
    if (out_ret) *out_ret = 0;
    if (!ctx || !module || !name) return PR_ERR_INVALID;
    const pr_win32_export* e = pr_win32_lookup(module, name);
    if (!e) {
        ctx->calls_unsup++;
        w32_log(ctx, PR_LOG_ERROR,
                "api desconhecida: %s!%s (fora do catálogo; ver docs)", module, name);
        return PR_ERR_RANGE;
    }
    if (e->status != PR_WIN32_IMPLEMENTED || !e->fn) {
        ctx->calls_unsup++;
        w32_log(ctx, PR_LOG_WARN,
                "%s!%s nao suportada neste build (%s)", module, name,
                e->note ? e->note : "");
        return PR_ERR_UNSUPPORTED;
    }
    pr_status st = PR_OK;
    uint64_t ret = e->fn(ctx, args, nargs, &st);
    if (out_ret) *out_ret = ret;
    ctx->calls_ok++;
    return st;
}

/* ---- Integração com o processo PE ---- */

pr_status pr_win32_bind_vm(pr_win32_ctx* ctx, pr_vm* vm, uint32_t guest_image_base,
                           const char* module_name) {
    if (!ctx || !vm) return PR_ERR_INVALID;
    ctx->vm = vm;
    ctx->guest_img_base = guest_image_base;
    ctx->module_name[0] = 0;
    if (module_name) {
        size_t j = 0;
        while (module_name[j] && j + 1 < sizeof(ctx->module_name)) {
            ctx->module_name[j] = module_name[j];
            j++;
        }
        ctx->module_name[j] = 0;
    }
    /* página estática do convidado (linha de comando) */
    uint32_t g = 0;
    if (pr_vm_alloc(vm, 0x1000, PR_VM_PROT_R, "w32static", &g, NULL) == PR_OK) {
        ctx->guest_cmdline = g;
        size_t slen = strlen((const char*)ctx->scratch);
        pr_vm_loader_write(vm, g, ctx->scratch, slen + 1);
        /* versão UTF-16 da linha em g+256 */
        ctx->guest_cmdline_w = g + 256;
        uint8_t wb[256];
        size_t i = 0;
        for (; i < slen && i < 120; i++) { wb[2 * i] = ctx->scratch[i]; wb[2 * i + 1] = 0; }
        wb[2 * i] = 0; wb[2 * i + 1] = 0;
        pr_vm_loader_write(vm, ctx->guest_cmdline_w, wb, 2 * (i + 1));
    }
    return PR_OK;
}

pr_status pr_win32_env_set(pr_win32_ctx* ctx, const char* name, const char* value) {
    if (!ctx || !name || !value) return PR_ERR_INVALID;
    for (size_t k = 0; k < ctx->env_count; k++) {
        if (env_ieq(ctx->env_names[k], name)) {
            snprintf(ctx->env_vals[k], sizeof(ctx->env_vals[k]), "%s", value);
            return PR_OK;
        }
    }
    if (ctx->env_count >= 8) return PR_ERR_NOMEM;
    snprintf(ctx->env_names[ctx->env_count], sizeof(ctx->env_names[0]), "%s", name);
    snprintf(ctx->env_vals[ctx->env_count], sizeof(ctx->env_vals[0]), "%s", value);
    ctx->env_count++;
    return PR_OK;
}

pr_status pr_win32_set_cwd(pr_win32_ctx* ctx, const char* path) {
    if (!ctx || !path) return PR_ERR_INVALID;
    snprintf(ctx->cwd, sizeof(ctx->cwd), "%s", path);
    return PR_OK;
}

pr_status pr_win32_set_cmdline(pr_win32_ctx* ctx, const char* cmd) {
    if (!ctx || !cmd) return PR_ERR_INVALID;
    size_t len = strlen(cmd);
    if (len + 1 > 250) return PR_ERR_RANGE;
    memcpy(ctx->scratch, cmd, len + 1);
    uint8_t* w = ctx->scratch + 256;
    size_t i = 0;
    for (; i <= len; i++) { w[2 * i] = (uint8_t)cmd[i]; w[2 * i + 1] = 0; }
    if (ctx->vm && ctx->guest_cmdline) {
        pr_vm_loader_write(ctx->vm, ctx->guest_cmdline, ctx->scratch, len + 1);
        if (ctx->guest_cmdline_w)
            pr_vm_loader_write(ctx->vm, ctx->guest_cmdline_w, w, 2 * (len + 1));
    }
    return PR_OK;
}

pr_status pr_win32_bind_thunks(pr_win32_ctx* ctx, const uint32_t* addrs, size_t count) {
    if (!ctx || (!addrs && count)) return PR_ERR_INVALID;
    free(ctx->thunk_addrs);
    ctx->thunk_addrs = NULL;
    ctx->thunk_count = 0;
    if (count == 0) return PR_OK;
    ctx->thunk_addrs = (uint32_t*)calloc(count, sizeof(uint32_t));
    if (!ctx->thunk_addrs) return PR_ERR_NOMEM;
    memcpy(ctx->thunk_addrs, addrs, count * sizeof(uint32_t));
    ctx->thunk_count = count;
    return PR_OK;
}

size_t pr_win32_index_of(const pr_win32_export* e) {
    if (!e) return (size_t)-1;
    return (size_t)(e - g_catalog);
}

pr_status pr_win32_call_entry(pr_win32_ctx* ctx, const pr_win32_export* e,
                              const uint64_t* args, size_t nargs, uint64_t* out_ret) {
    if (out_ret) *out_ret = 0;
    if (!ctx || !e) return PR_ERR_INVALID;
    return pr_win32_call(ctx, e->module, e->name, args, nargs, out_ret);
}

pr_surf* pr_win32_surface(pr_win32_ctx* ctx) {
    return ctx ? ctx->gdi_surface : NULL;
}

struct pr_gl_state* pr_win32_gl_state(const pr_win32_ctx* ctx) {
    return ctx ? ctx->gl : NULL;
}
void pr_win32_gl_set_state(pr_win32_ctx* ctx, struct pr_gl_state* gl) {
    if (ctx) ctx->gl = gl;
}
void pr_win32_gl_log(pr_win32_ctx* ctx, const char* msg) {
    if (ctx && msg) w32_log(ctx, PR_LOG_INFO, "%s", msg);
}
