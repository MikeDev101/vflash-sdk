#include "vflash/audio.h"
#include <stddef.h>

#define SOUND_BASE 0xB0000000u
#define SOUND_REG(offset) (*(volatile uint32_t *)(uintptr_t)(SOUND_BASE + (offset)))
#define VOICE_REG(v, offset) SOUND_REG(((uint32_t)(v) << 6) + (offset))

#define VFLASH_AUDIO_RING_SAMPLES 4096u
#define VFLASH_AUDIO_RING_MASK    (VFLASH_AUDIO_RING_SAMPLES - 1u)

/* Static DMA ring buffers in SDRAM */
static int16_t s_ring_left[VFLASH_AUDIO_RING_SAMPLES] __attribute__((aligned(64)));
static int16_t s_ring_right[VFLASH_AUDIO_RING_SAMPLES] __attribute__((aligned(64)));

static bool s_active = false;
static bool s_paused = false;
static uint8_t s_channels = 2;
static uint32_t s_sample_rate = 44100;
static uint32_t s_start_addr = 0;
static uint32_t s_end_addr = 0;

static uint32_t s_write_idx = 0;
static uint32_t s_last_hw_idx = 0;
static uint32_t s_queued_samples = 0;

static uint32_t s_samples_played = 0;
static uint32_t s_samples_written = 0;
static uint32_t s_underrun_count = 0;

static void reset_hardware_voices(void)
{
    /* Key-off all voices */
    SOUND_REG(0x1018) = 0xFFFFFFFFu;
    SOUND_REG(0x101C) = 0xFFFFFFFFu;

    /* Zero all 64 voice register blocks */
    for (uint32_t v = 0; v < 64; ++v) {
        VOICE_REG(v, 0x00) = 0;
        VOICE_REG(v, 0x04) = 0;
        VOICE_REG(v, 0x08) = 0;
        VOICE_REG(v, 0x0C) = 0;
        VOICE_REG(v, 0x10) = 0;
        VOICE_REG(v, 0x14) = 0xFFFFFFFFu;
        VOICE_REG(v, 0x18) = 0xFFFFFFFFu;
        VOICE_REG(v, 0x1C) = 0xFFFFFFFFu;
        VOICE_REG(v, 0x20) = 0;
        VOICE_REG(v, 0x24) = 0;
    }

    /* Set Master Volume to Maximum (0x7FFF / 0x7FFF) */
    SOUND_REG(0x1188) = 0x7FFFu;
    SOUND_REG(0x118A) = 0x7FFFu;
    SOUND_REG(0x1184) = 0xFFFFFFFFu;
}

bool vflash_audio_init(const vflash_audio_config_t *config)
{
    if (!config) return false;
    if (config->channels != 1 && config->channels != 2) return false;
    if (config->format != VFLASH_AUDIO_FMT_S16LE) return false;
    if (config->sample_rate < 4000 || config->sample_rate > 48000) return false;

    s_channels = config->channels;
    s_sample_rate = config->sample_rate;

    /* Clear static ring buffers */
    for (uint32_t i = 0; i < VFLASH_AUDIO_RING_SAMPLES; ++i) {
        s_ring_left[i] = 0;
        s_ring_right[i] = 0;
    }

    reset_hardware_voices();

    s_start_addr = (uint32_t)(uintptr_t)s_ring_left;
    s_end_addr = s_start_addr + (VFLASH_AUDIO_RING_SAMPLES << 1);

    /* Frequency step calculation: 4096 (1<<12) corresponds to unity rate (44100 Hz).
     * For arbitrary rates: (sample_rate * 4096) / 44100 = (sample_rate * 6087) >> 16 */
    uint32_t step = (s_sample_rate == 44100u) ? 4096u : ((s_sample_rate * 6087u) >> 16);
    if (step == 0) step = 1;
    if (step > 8191) step = 8191;

    /* Configure Voice 0 (Left channel or Centered Mono) */
    VOICE_REG(0, 0x00) = s_start_addr;
    VOICE_REG(0, 0x04) = s_end_addr;
    VOICE_REG(0, 0x08) = s_start_addr; /* Loop address */
    VOICE_REG(0, 0x0C) = 0x1F4u;       /* 16-bit signed PCM, looping enabled */
    VOICE_REG(0, 0x10) = step;
    VOICE_REG(0, 0x14) = 0xFFFFFFFFu;
    VOICE_REG(0, 0x18) = 0xFFFFFFFFu;
    VOICE_REG(0, 0x1C) = 0xFFFFFFFFu;
    VOICE_REG(0, 0x20) = (s_channels == 1) ? 0x7F7Fu : 0x007Fu; /* Mono centered vs Left only */
    VOICE_REG(0, 0x24) = 0;

    /* Configure Voice 1 (Right channel for Stereo) */
    if (s_channels == 2) {
        uint32_t right_start = (uint32_t)(uintptr_t)s_ring_right;
        uint32_t right_end = right_start + (VFLASH_AUDIO_RING_SAMPLES << 1);

        VOICE_REG(1, 0x00) = right_start;
        VOICE_REG(1, 0x04) = right_end;
        VOICE_REG(1, 0x08) = right_start;
        VOICE_REG(1, 0x0C) = 0x1F4u;   /* 16-bit signed PCM, looping enabled */
        VOICE_REG(1, 0x10) = step;
        VOICE_REG(1, 0x14) = 0xFFFFFFFFu;
        VOICE_REG(1, 0x18) = 0xFFFFFFFFu;
        VOICE_REG(1, 0x1C) = 0xFFFFFFFFu;
        VOICE_REG(1, 0x20) = 0x7F00u; /* Right channel only */
        VOICE_REG(1, 0x24) = 0;
    }

    s_write_idx = 0;
    s_last_hw_idx = 0;
    s_queued_samples = 0;
    s_samples_played = 0;
    s_samples_written = 0;
    s_underrun_count = 0;
    s_paused = false;
    s_active = true;

    /* Trigger hardware key-on */
    SOUND_REG(0x1010) = (s_channels == 2) ? 0x03u : 0x01u;

    return true;
}

void vflash_audio_update(void)
{
    if (!s_active || s_paused) return;

    /* Read current DMA hardware fetch address */
    uint32_t hw_addr = VOICE_REG(0, 0x28);
    uint32_t hw_idx = 0;
    if (hw_addr >= s_start_addr && hw_addr < s_end_addr) {
        hw_idx = (hw_addr - s_start_addr) >> 1;
    }

    /* Calculate consumed samples */
    uint32_t consumed = (hw_idx >= s_last_hw_idx) ?
                        (hw_idx - s_last_hw_idx) :
                        (VFLASH_AUDIO_RING_SAMPLES - s_last_hw_idx + hw_idx);

    if (consumed > 0) {
        s_samples_played += consumed;

        /* Zero out consumed buffer space so underruns play silence */
        uint32_t idx = s_last_hw_idx;
        while (idx != hw_idx) {
            s_ring_left[idx] = 0;
            if (s_channels == 2) {
                s_ring_right[idx] = 0;
            }
            idx = (idx + 1u) & VFLASH_AUDIO_RING_MASK;
        }
        s_last_hw_idx = hw_idx;

        if (consumed >= s_queued_samples) {
            if (s_queued_samples > 0) {
                s_underrun_count++;
            }
            s_queued_samples = 0;
            /* Align write pointer slightly ahead of hardware read index */
            s_write_idx = (hw_idx + 64u) & VFLASH_AUDIO_RING_MASK;
        } else {
            s_queued_samples -= consumed;
        }
    }
}

uint32_t vflash_audio_get_queued_bytes(void)
{
    vflash_audio_update();
    return s_queued_samples * (uint32_t)s_channels * sizeof(int16_t);
}

uint32_t vflash_audio_get_available_bytes(void)
{
    vflash_audio_update();
    uint32_t max_samples = VFLASH_AUDIO_RING_SAMPLES - 128u;
    uint32_t avail_samples = (max_samples > s_queued_samples) ? (max_samples - s_queued_samples) : 0;
    return avail_samples * (uint32_t)s_channels * sizeof(int16_t);
}

uint32_t vflash_audio_write(const void *samples, uint32_t num_bytes)
{
    if (!s_active || !samples || num_bytes == 0) return 0;

    vflash_audio_update();

    uint32_t frame_bytes = (s_channels == 2) ? 4u : 2u;
    uint32_t requested_frames = (s_channels == 2) ? (num_bytes >> 2) : (num_bytes >> 1);
    if (requested_frames == 0) return 0;

    uint32_t max_samples = VFLASH_AUDIO_RING_SAMPLES - 128u;
    uint32_t avail_frames = (max_samples > s_queued_samples) ? (max_samples - s_queued_samples) : 0;
    uint32_t frames_to_write = (requested_frames < avail_frames) ? requested_frames : avail_frames;
    if (frames_to_write == 0) return 0;

    const int16_t *src = (const int16_t *)samples;
    uint32_t w = s_write_idx;

    if (s_channels == 2) {
        for (uint32_t i = 0; i < frames_to_write; ++i) {
            s_ring_left[w] = *src++;
            s_ring_right[w] = *src++;
            w = (w + 1u) & VFLASH_AUDIO_RING_MASK;
        }
    } else {
        for (uint32_t i = 0; i < frames_to_write; ++i) {
            s_ring_left[w] = *src++;
            w = (w + 1u) & VFLASH_AUDIO_RING_MASK;
        }
    }

    s_write_idx = w;
    s_queued_samples += frames_to_write;
    s_samples_written += frames_to_write;

    return frames_to_write * frame_bytes;
}

bool vflash_audio_write_blocking(const void *samples, uint32_t num_bytes)
{
    if (!s_active || !samples) return false;

    const uint8_t *ptr = (const uint8_t *)samples;
    uint32_t remaining = num_bytes;

    while (remaining > 0) {
        uint32_t written = vflash_audio_write(ptr, remaining);
        if (written > 0) {
            ptr += written;
            remaining -= written;
        } else {
            vflash_audio_update();
        }
    }

    return true;
}

void vflash_audio_pause(bool pause)
{
    if (!s_active || s_paused == pause) return;

    s_paused = pause;
    if (s_paused) {
        /* Key off hardware voices */
        SOUND_REG(0x1018) = (s_channels == 2) ? 0x03u : 0x01u;
    } else {
        /* Resume key on */
        SOUND_REG(0x1010) = (s_channels == 2) ? 0x03u : 0x01u;
    }
}

void vflash_audio_get_stats(vflash_audio_stats_t *stats)
{
    if (!stats) return;
    vflash_audio_update();

    stats->samples_played = s_samples_played;
    stats->samples_written = s_samples_written;
    stats->underrun_count = s_underrun_count;
    stats->current_hw_addr = VOICE_REG(0, 0x28);
    stats->queued_bytes = s_queued_samples * (uint32_t)s_channels * sizeof(int16_t);
    stats->available_bytes = vflash_audio_get_available_bytes();
    stats->active = s_active;
    stats->paused = s_paused;
}

void vflash_audio_shutdown(void)
{
    if (!s_active) return;
    reset_hardware_voices();
    s_active = false;
    s_paused = false;
    s_queued_samples = 0;
}
