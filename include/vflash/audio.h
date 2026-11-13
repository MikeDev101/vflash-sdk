#ifndef VFLASH_AUDIO_H
#define VFLASH_AUDIO_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Native PCM audio sample formats.
//
typedef enum
{
    VFLASH_AUDIO_FMT_S16LE = 0, // Signed 16-bit little-endian PCM
    VFLASH_AUDIO_FMT_U8    = 1  // Unsigned 8-bit PCM
} vflash_audio_format_t;

//
// Audio output configuration passed to vflash_audio_init().
//
typedef struct
{
    uint32_t sample_rate;         // Sample rate in Hz (4000..48000)
    uint8_t channels;             // Channel count: 1 (mono) or 2 (stereo)
    vflash_audio_format_t format; // Sample format (VFLASH_AUDIO_FMT_S16LE)
    uint32_t buffer_samples;      // Prebuffer threshold (0 = default 4096)
} vflash_audio_config_t;

//
// Audio playback statistics and hardware DMA diagnostics.
//
typedef struct
{
    uint32_t samples_played;      // Cumulative samples consumed by hardware
    uint32_t samples_written;     // Cumulative samples written by caller
    uint32_t underrun_count;      // Number of ring-buffer underrun events
    uint32_t current_hw_addr;     // Current hardware voice 0 DMA address
    uint32_t queued_bytes;        // Bytes currently queued in ring buffer
    uint32_t available_bytes;     // Free writable bytes in ring buffer
    bool active;                  // True when audio subsystem is active
    bool paused;                  // True when playback is paused
} vflash_audio_stats_t;

//
// Initialize native V.Flash PCM audio output and hardware sound voices.
//
// config: Optional audio configuration; NULL selects 22050 Hz mono S16LE.
//
// Returns true on success, or false if a parameter is invalid.
//
bool vflash_audio_init(const vflash_audio_config_t *config);

//
// Write PCM samples into the audio ring buffer without blocking.
// Stereo samples must be interleaved [L0, R0, L1, R1, ...].
//
// samples:   Source PCM sample buffer.
// num_bytes: Byte length of sample data to write.
//
// Returns the number of bytes accepted into the ring buffer.
//
uint32_t vflash_audio_write(const void *samples, uint32_t num_bytes);

//
// Write all requested PCM bytes to the ring buffer, servicing audio until
// all data is accepted or a timeout occurs.
//
// samples:   Source PCM sample buffer.
// num_bytes: Byte length of sample data to write.
//
// Returns true on success, or false on timeout or invalid parameters.
//
bool vflash_audio_write_blocking(const void *samples, uint32_t num_bytes);

//
// Query the number of bytes currently queued for playback.
//
// Returns queued byte count in the active ring buffer.
//
uint32_t vflash_audio_get_queued_bytes(void);

//
// Query free writable space in the audio ring buffer.
//
// Returns available capacity in bytes.
//
uint32_t vflash_audio_get_available_bytes(void);

//
// Pause or resume hardware audio playback.
//
// pause: True to pause voices, false to resume playback.
//
void vflash_audio_pause(bool pause);

//
// Retrieve audio playback statistics and hardware diagnostics.
//
// stats: Output structure populated with current audio counters and flags.
//
void vflash_audio_get_stats(vflash_audio_stats_t *stats);

//
// Service the audio ring buffer, track hardware read progress, and handle
// underrun recovery.
//
void vflash_audio_update(void);

//
// Shut down native audio output and silence all hardware voices.
//
void vflash_audio_shutdown(void);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_AUDIO_H
