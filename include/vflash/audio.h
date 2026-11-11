#ifndef VFLASH_AUDIO_H
#define VFLASH_AUDIO_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Native PCM Audio Formats */
typedef enum {
    VFLASH_AUDIO_FMT_S16LE = 0, /* Signed 16-bit little-endian PCM */
    VFLASH_AUDIO_FMT_U8    = 1, /* Unsigned 8-bit PCM */
} vflash_audio_format_t;

/* Audio Configuration Structure */
typedef struct {
    uint32_t sample_rate;         /* Sample rate in Hz: 11025, 22050, 44100, etc. */
    uint8_t  channels;            /* 1 = Mono, 2 = Stereo */
    vflash_audio_format_t format; /* Audio format (e.g. VFLASH_AUDIO_FMT_S16LE) */
    uint32_t buffer_samples;      /* Ring buffer capacity in samples (0 = default 4096) */
} vflash_audio_config_t;

/* Audio Playback Statistics & Diagnostics */
typedef struct {
    uint32_t samples_played;
    uint32_t samples_written;
    uint32_t underrun_count;
    uint32_t current_hw_addr;
    uint32_t queued_bytes;
    uint32_t available_bytes;
    bool     active;
    bool     paused;
} vflash_audio_stats_t;

/**
 * Initialize native V.Flash PCM audio output.
 * Configures the hardware sound voices and ring buffers.
 * Returns true on success, false on invalid parameter.
 */
bool vflash_audio_init(const vflash_audio_config_t *config);

/**
 * Write PCM samples into the audio playback buffer.
 * For stereo, samples must be interleaved [Left0, Right0, Left1, Right1, ...].
 * Non-blocking: writes as many bytes as can currently fit in the ring buffer.
 * Returns the number of bytes written.
 */
uint32_t vflash_audio_write(const void *samples, uint32_t num_bytes);

/**
 * Blocking write: writes all requested bytes to the playback buffer,
 * waiting (servicing audio) until all data has been accepted.
 * Returns true on success, false on timeout or invalid parameter.
 */
bool vflash_audio_write_blocking(const void *samples, uint32_t num_bytes);

/**
 * Returns the number of bytes currently queued in the playback buffer waiting to play.
 */
uint32_t vflash_audio_get_queued_bytes(void);

/**
 * Returns the available space in bytes in the playback buffer.
 */
uint32_t vflash_audio_get_available_bytes(void);

/**
 * Pause or resume playback.
 */
void vflash_audio_pause(bool pause);

/**
 * Retrieve audio playback statistics and hardware diagnostics.
 */
void vflash_audio_get_stats(vflash_audio_stats_t *stats);

/**
 * Periodic audio update tick.
 * Advances buffer tracking, handles underrun silence filling, and updates hardware status.
 */
void vflash_audio_update(void);

/**
 * Shut down native audio output and stop all active voices.
 */
void vflash_audio_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif /* VFLASH_AUDIO_H */

