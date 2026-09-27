/* Portico Runtime — áudio: ring buffer SPSC (produtor = runtime,
 * consumidor = AudioBackend AVAudioEngine no iOS) e síntese de tom do self-test. */
#ifndef PORTICO_PR_AUDIO_H
#define PORTICO_PR_AUDIO_H

#include "pr_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pr_audio_ring pr_audio_ring;

/* frames = nº de quadros estéreo (1 quadro = 2 amostras float interleaved). */
pr_audio_ring* pr_audio_ring_create(size_t frames);
void    pr_audio_ring_destroy(pr_audio_ring* r);
void    pr_audio_ring_reset(pr_audio_ring* r);
/* Produz (retorna nº de quadros realmente aceitos). */
size_t  pr_audio_push(pr_audio_ring* r, const float* interleaved, size_t frames);
/* Consome (retorna nº de quadros lidos). */
size_t  pr_audio_pull(pr_audio_ring* r, float* interleaved, size_t frames);
size_t  pr_audio_available(const pr_audio_ring* r);
size_t  pr_audio_space(const pr_audio_ring* r);

/* Sintetizador simples (para payloads/backends que reportam "tom" em vez de PCM). */
typedef enum pr_waveform { PR_WAVE_SQUARE = 0, PR_WAVE_SINE = 1, PR_WAVE_SILENCE = 2 } pr_waveform;

typedef struct pr_tone {
    pr_waveform wave;
    float freq_hz;
    float volume;   /* 0..1 */
    double phase;   /* estado interno */
} pr_tone;

void  pr_tone_init(pr_tone* t);
/* Gera `frames` estéreo em 48 kHz no buffer de saída. */
void  pr_tone_render(pr_tone* t, float* out, size_t frames);

#ifdef __cplusplus
}
#endif
#endif /* PORTICO_PR_AUDIO_H */
