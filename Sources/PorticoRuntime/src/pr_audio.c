/* Feature-test macros: expõe POSIX/BSD nos headers do sistema com -std=c11. */
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#else
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE 1
#endif

#include "portico/pr_audio.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdatomic.h>

#define PR_AUDIO_RATE 48000.0

struct pr_audio_ring {
    float* buf;                 /* interleaved estéreo */
    size_t cap_frames;          /* potência de 2 */
    _Atomic size_t head;        /* escrita (frames absolutos) */
    _Atomic size_t tail;        /* leitura (frames absolutos) */
};

static size_t pow2_ceil(size_t v) {
    size_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

pr_audio_ring* pr_audio_ring_create(size_t frames) {
    if (frames < 256) frames = 256;
    pr_audio_ring* r = (pr_audio_ring*)calloc(1, sizeof(pr_audio_ring));
    if (!r) return NULL;
    r->cap_frames = pow2_ceil(frames);
    r->buf = (float*)calloc(r->cap_frames * 2, sizeof(float));
    if (!r->buf) { free(r); return NULL; }
    atomic_store(&r->head, 0);
    atomic_store(&r->tail, 0);
    return r;
}

void pr_audio_ring_destroy(pr_audio_ring* r) {
    if (!r) return;
    free(r->buf);
    free(r);
}

void pr_audio_ring_reset(pr_audio_ring* r) {
    if (!r) return;
    atomic_store(&r->head, 0);
    atomic_store(&r->tail, 0);
}

size_t pr_audio_available(const pr_audio_ring* r) {
    if (!r) return 0;
    size_t h = atomic_load_explicit(&((_Atomic size_t*)&r->head)[0], memory_order_acquire);
    size_t t = atomic_load_explicit(&((_Atomic size_t*)&r->tail)[0], memory_order_acquire);
    return h - t;
}

size_t pr_audio_space(const pr_audio_ring* r) {
    if (!r) return 0;
    return r->cap_frames - pr_audio_available(r);
}

size_t pr_audio_push(pr_audio_ring* r, const float* interleaved, size_t frames) {
    if (!r || !interleaved || frames == 0) return 0;
    size_t space = pr_audio_space(r);
    if (frames > space) frames = space;
    size_t h = atomic_load(&r->head);
    for (size_t i = 0; i < frames; i++) {
        size_t slot = (h + i) & (r->cap_frames - 1);
        r->buf[slot * 2]     = interleaved[i * 2];
        r->buf[slot * 2 + 1] = interleaved[i * 2 + 1];
    }
    atomic_store_explicit(&r->head, h + frames, memory_order_release);
    return frames;
}

size_t pr_audio_pull(pr_audio_ring* r, float* interleaved, size_t frames) {
    if (!r || !interleaved || frames == 0) return 0;
    size_t avail = pr_audio_available(r);
    if (frames > avail) frames = avail;
    size_t t = atomic_load(&r->tail);
    for (size_t i = 0; i < frames; i++) {
        size_t slot = (t + i) & (r->cap_frames - 1);
        interleaved[i * 2]     = r->buf[slot * 2];
        interleaved[i * 2 + 1] = r->buf[slot * 2 + 1];
    }
    atomic_store_explicit(&r->tail, t + frames, memory_order_release);
    return frames;
}

void pr_tone_init(pr_tone* t) {
    if (!t) return;
    t->wave = PR_WAVE_SILENCE;
    t->freq_hz = 0.0f;
    t->volume = 0.0f;
    t->phase = 0.0;
}

void pr_tone_render(pr_tone* t, float* out, size_t frames) {
    if (!t || !out) return;
    double inc = (double)t->freq_hz / PR_AUDIO_RATE;
    float vol = t->volume;
    if (vol < 0.f) vol = 0.f;
    if (vol > 1.f) vol = 1.f;
    for (size_t i = 0; i < frames; i++) {
        float s = 0.0f;
        switch (t->wave) {
            case PR_WAVE_SQUARE:
                s = (t->phase < 0.5) ? vol : -vol;
                break;
            case PR_WAVE_SINE:
                s = (float)sin(t->phase * 2.0 * 3.14159265358979323846) * vol;
                break;
            case PR_WAVE_SILENCE:
            default:
                s = 0.0f;
                break;
        }
        out[i * 2] = s;
        out[i * 2 + 1] = s;
        t->phase += inc;
        if (t->phase >= 1.0) t->phase -= 1.0;
    }
}
