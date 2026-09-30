/* pr_waveout.c — waveOut audio stubs + minimal PCM ring para diagnóstico
 * FASE 10 — Audio pipeline real
 * Implementação mínima: waveOutOpen/Write/Close com log honesto + push para pr_audio_ring
 */

#include "portico/pr_types.h"
#include "portico/pr_log.h"
#include "portico/pr_audio.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct pr_waveout_device {
    int is_open;
    int format_channels;
    int format_samplerate;
    int format_bits;
    pr_audio_ring* ring;
    pr_log* log;
    unsigned total_bytes;
    unsigned total_writes;
} pr_waveout_device;

#define PR_WAVEOUT_MAX_DEVICES 4
static pr_waveout_device g_waveout[PR_WAVEOUT_MAX_DEVICES];

pr_status pr_waveout_init(pr_log* log) {
    memset(g_waveout, 0, sizeof(g_waveout));
    for (int i = 0; i < PR_WAVEOUT_MAX_DEVICES; i++) {
        g_waveout[i].log = log;
    }
    if (log) {
        pr_log_write(log, PR_LOG_INFO, "AUDIO", "AUDIO_INIT waveOut backend init, maxDevices=%d", PR_WAVEOUT_MAX_DEVICES);
    }
    return PR_OK;
}

int pr_waveout_open(int device_id, int channels, int samplerate, int bits, pr_audio_ring* ring) {
    if (device_id < 0 || device_id >= PR_WAVEOUT_MAX_DEVICES) device_id = 0;
    pr_waveout_device* dev = &g_waveout[device_id];
    if (dev->is_open) {
        if (dev->log) pr_log_write(dev->log, PR_LOG_WARN, "AUDIO", "WAVEOUT_OPEN device %d already open", device_id);
        return -1;
    }
    dev->is_open = 1;
    dev->format_channels = channels;
    dev->format_samplerate = samplerate;
    dev->format_bits = bits;
    dev->ring = ring;
    dev->total_bytes = 0;
    dev->total_writes = 0;
    if (dev->log) {
        pr_log_write(dev->log, PR_LOG_INFO, "AUDIO", "WAVEOUT_OPEN device=%d channels=%d samplerate=%d bits=%d", device_id, channels, samplerate, bits);
    }
    return device_id;
}

int pr_waveout_write(int device_id, const void* data, size_t bytes) {
    if (device_id < 0 || device_id >= PR_WAVEOUT_MAX_DEVICES) return -1;
    pr_waveout_device* dev = &g_waveout[device_id];
    if (!dev->is_open) return -1;
    dev->total_bytes += (unsigned)bytes;
    dev->total_writes++;
    
    /* Converte PCM para float e push para ring se disponível */
    if (dev->ring && data) {
        size_t frames = 0;
        if (dev->format_bits == 16) {
            frames = bytes / (dev->format_channels * 2);
            /* Converte 16-bit para float */
            float* tmp = (float*)malloc(frames * dev->format_channels * sizeof(float));
            if (tmp) {
                const short* src = (const short*)data;
                for (size_t i = 0; i < frames * (size_t)dev->format_channels; i++) {
                    tmp[i] = (float)src[i] / 32768.0f;
                }
                pr_audio_push(dev->ring, tmp, frames);
                free(tmp);
            }
        } else if (dev->format_bits == 8) {
            frames = bytes / dev->format_channels;
            float* tmp = (float*)malloc(frames * dev->format_channels * sizeof(float));
            if (tmp) {
                const unsigned char* src = (const unsigned char*)data;
                for (size_t i = 0; i < frames * (size_t)dev->format_channels; i++) {
                    tmp[i] = ((float)src[i] - 128.0f) / 128.0f;
                }
                pr_audio_push(dev->ring, tmp, frames);
                free(tmp);
            }
        }
    }
    
    if (dev->log && (dev->total_writes % 100 == 0)) {
        pr_log_write(dev->log, PR_LOG_DEBUG, "AUDIO", "WAVEOUT_WRITE device=%d bytes=%zu totalBytes=%u writes=%u", device_id, bytes, dev->total_bytes, dev->total_writes);
    }
    return 0;
}

int pr_waveout_close(int device_id) {
    if (device_id < 0 || device_id >= PR_WAVEOUT_MAX_DEVICES) return -1;
    pr_waveout_device* dev = &g_waveout[device_id];
    if (!dev->is_open) return -1;
    if (dev->log) {
        pr_log_write(dev->log, PR_LOG_INFO, "AUDIO", "WAVEOUT_CLOSE device=%d totalBytes=%u writes=%u", device_id, dev->total_bytes, dev->total_writes);
    }
    dev->is_open = 0;
    dev->ring = NULL;
    return 0;
}

void pr_waveout_get_stats(int device_id, unsigned* total_bytes, unsigned* total_writes, int* is_open) {
    if (device_id < 0 || device_id >= PR_WAVEOUT_MAX_DEVICES) return;
    pr_waveout_device* dev = &g_waveout[device_id];
    if (total_bytes) *total_bytes = dev->total_bytes;
    if (total_writes) *total_writes = dev->total_writes;
    if (is_open) *is_open = dev->is_open;
}

const char* pr_waveout_diagnostic(void) {
    return "waveOut: minimal implementation via pr_waveout.c — open/write/close logado, PCM 8/16-bit convertido para float ring (pr_audio_ring). Suporta stereo/mono, 44100/22050 Hz. DirectSound/XAudio2/WASAPI não implementados (requer FASE 10 completa).";
}
