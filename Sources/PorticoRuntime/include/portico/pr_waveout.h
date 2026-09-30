#ifndef PR_WAVEOUT_H
#define PR_WAVEOUT_H

#include "pr_types.h"
#include "pr_log.h"
#include "pr_audio.h"

#ifdef __cplusplus
extern "C" {
#endif

pr_status pr_waveout_init(pr_log* log);
int pr_waveout_open(int device_id, int channels, int samplerate, int bits, pr_audio_ring* ring);
int pr_waveout_write(int device_id, const void* data, size_t bytes);
int pr_waveout_close(int device_id);
void pr_waveout_get_stats(int device_id, unsigned* total_bytes, unsigned* total_writes, int* is_open);
const char* pr_waveout_diagnostic(void);

#ifdef __cplusplus
}
#endif

#endif /* PR_WAVEOUT_H */
