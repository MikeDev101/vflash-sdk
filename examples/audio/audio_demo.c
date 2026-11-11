#include <stdint.h>
#include <stdbool.h>

#include "vflash/audio.h"
#include "vflash/video.h"
#include "vflash/draw.h"
#include "vflash/input.h"

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
#include "vflash/mmu.h"
#endif

/* Exported probe symbols for automated test validation */
volatile uint32_t startup_status;
volatile uint32_t boot_marker;
volatile uint32_t data_cookie = 0x13579BDFu;
volatile uint32_t video_status;
volatile uint32_t render_status;

__attribute__((used, section(".data"))) volatile uint32_t audio_demo_frames = 0;
__attribute__((used, section(".data"))) volatile uint32_t audio_demo_samples_played = 0;
__attribute__((used, section(".data"))) volatile uint32_t audio_demo_samples_written = 0;
__attribute__((used, section(".data"))) volatile uint32_t audio_demo_underruns = 0;
__attribute__((used, section(".data"))) volatile uint32_t audio_demo_queued_bytes = 0;
__attribute__((used, section(".data"))) volatile uint32_t audio_demo_active = 0;
__attribute__((used, section(".data"))) volatile uint32_t audio_demo_sample_rate = 44100;
__attribute__((used, section(".data"))) volatile uint32_t audio_demo_channels = 2;

static const int16_t s_sine_lut[256] = {
         0,    736,   1472,   2207,   2941,   3672,   4402,   5129,
      5853,   6573,   7289,   8001,   8709,   9410,  10107,  10797,
     11481,  12157,  12827,  13488,  14142,  14787,  15423,  16050,
     16667,  17274,  17871,  18457,  19032,  19595,  20147,  20686,
     21213,  21727,  22229,  22716,  23190,  23650,  24096,  24528,
     24944,  25346,  25732,  26103,  26458,  26797,  27120,  27426,
     27716,  27990,  28246,  28486,  28708,  28913,  29101,  29271,
     29424,  29558,  29675,  29774,  29856,  29919,  29964,  29991,
     30000,  29991,  29964,  29919,  29856,  29774,  29675,  29558,
     29424,  29271,  29101,  28913,  28708,  28486,  28246,  27990,
     27716,  27426,  27120,  26797,  26458,  26103,  25732,  25346,
     24944,  24528,  24096,  23650,  23190,  22716,  22229,  21727,
     21213,  20686,  20147,  19595,  19032,  18457,  17871,  17274,
     16667,  16050,  15423,  14787,  14142,  13488,  12827,  12157,
     11481,  10797,  10107,   9410,   8709,   8001,   7289,   6573,
      5853,   5129,   4402,   3672,   2941,   2207,   1472,    736,
         0,   -736,  -1472,  -2207,  -2941,  -3672,  -4402,  -5129,
     -5853,  -6573,  -7289,  -8001,  -8709,  -9410, -10107, -10797,
    -11481, -12157, -12827, -13488, -14142, -14787, -15423, -16050,
    -16667, -17274, -17871, -18457, -19032, -19595, -20147, -20686,
    -21213, -21727, -22229, -22716, -23190, -23650, -24096, -24528,
    -24944, -25346, -25732, -26103, -26458, -26797, -27120, -27426,
    -27716, -27990, -28246, -28486, -28708, -28913, -29101, -29271,
    -29424, -29558, -29675, -29774, -29856, -29919, -29964, -29991,
    -30000, -29991, -29964, -29919, -29856, -29774, -29675, -29558,
    -29424, -29271, -29101, -28913, -28708, -28486, -28246, -27990,
    -27716, -27426, -27120, -26797, -26458, -26103, -25732, -25346,
    -24944, -24528, -24096, -23650, -23190, -22716, -22229, -21727,
    -21213, -20686, -20147, -19595, -19032, -18457, -17871, -17274,
    -16667, -16050, -15423, -14787, -14142, -13488, -12827, -12157,
    -11481, -10797, -10107,  -9410,  -8709,  -8001,  -7289,  -6573,
     -5853,  -5129,  -4402,  -3672,  -2941,  -2207,  -1472,   -736,
};

static void u32_to_str(uint32_t val, char *buf)
{
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    static const uint32_t powers[] = {
        1000000000u, 100000000u, 10000000u, 1000000u,
        100000u, 10000u, 1000u, 100u, 10u, 1u
    };
    int out = 0;
    bool started = false;
    for (int i = 0; i < 10; ++i) {
        uint32_t p = powers[i];
        int digit = 0;
        while (val >= p) {
            val -= p;
            digit++;
        }
        if (digit > 0 || started || i == 9) {
            buf[out++] = (char)('0' + digit);
            started = true;
        }
    }
    buf[out] = '\0';
}

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu) ? 0x600D600Du : 0xBAD00001u;
    boot_marker = 0x56464C53u;

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
    vflash_mmu_identity();
    vflash_mmu_enable();
#endif

    vflash_framebuffer_t fb;
    vflash_video_init(&fb);
    video_status = 0x56494431u;
    render_status = 0x44524157u;

    vflash_audio_config_t config;
    config.sample_rate = 44100;
    config.channels = 2;
    config.format = VFLASH_AUDIO_FMT_S16LE;
    config.buffer_samples = 4096;

    if (!vflash_audio_init(&config)) {
        return;
    }

    audio_demo_active = 1;
    audio_demo_sample_rate = config.sample_rate;
    audio_demo_channels = config.channels;

    /* Left channel: 440 Hz (phase step = 167389 in 16.16) */
    /* Right channel: 880 Hz (phase step = 334778 in 16.16) */
    uint32_t phase_l = 0;
    uint32_t phase_r = 0;
    const uint32_t step_l = 167389;
    const uint32_t step_r = 334778;

    /* Temporary batch buffer for generating samples */
    int16_t batch[512 * 2];

    while (1) {
        audio_demo_frames++;

        /* Feed audio buffer if space is available */
        uint32_t avail_bytes = vflash_audio_get_available_bytes();
        while (avail_bytes >= sizeof(batch)) {
            for (uint32_t i = 0; i < 512; ++i) {
                batch[i * 2 + 0] = s_sine_lut[(phase_l >> 16) & 0xFF];
                batch[i * 2 + 1] = s_sine_lut[(phase_r >> 16) & 0xFF];
                phase_l += step_l;
                phase_r += step_r;
            }
            uint32_t written = vflash_audio_write(batch, sizeof(batch));
            if (written == 0) break;
            avail_bytes = vflash_audio_get_available_bytes();
        }

        vflash_audio_stats_t stats;
        vflash_audio_get_stats(&stats);

        audio_demo_samples_played = stats.samples_played;
        audio_demo_samples_written = stats.samples_written;
        audio_demo_underruns = stats.underrun_count;
        audio_demo_queued_bytes = stats.queued_bytes;

        /* Visual diagnostic rendering */
        draw_rect(&fb, 0, 0, 320, 240, 0x102030);

        draw_text(&fb, 16, 16, "V.FLASH NATIVE PCM AUDIO DEMO", 0xFFFFFF, 1);

        char num_buf[32];

        draw_text(&fb, 16, 40, "RATE: 44100 Hz  CHANNELS: 2 (STEREO)", 0x00FF88, 1);
        draw_text(&fb, 16, 56, "LEFT: 440 Hz    RIGHT: 880 Hz", 0xFFDD44, 1);

        draw_text(&fb, 16, 80, "SAMPLES PLAYED:", 0xFFFFFF, 1);
        u32_to_str(stats.samples_played, num_buf);
        draw_text(&fb, 160, 80, num_buf, 0x44DDFF, 1);

        draw_text(&fb, 16, 96, "SAMPLES WRITTEN:", 0xFFFFFF, 1);
        u32_to_str(stats.samples_written, num_buf);
        draw_text(&fb, 160, 96, num_buf, 0x44DDFF, 1);

        draw_text(&fb, 16, 112, "QUEUED BYTES:", 0xFFFFFF, 1);
        u32_to_str(stats.queued_bytes, num_buf);
        draw_text(&fb, 160, 112, num_buf, 0xFFDD44, 1);

        draw_text(&fb, 16, 128, "UNDERRUNS:", 0xFFFFFF, 1);
        u32_to_str(stats.underrun_count, num_buf);
        draw_text(&fb, 160, 128, num_buf, (stats.underrun_count == 0) ? 0x00FF88 : 0xFF4444, 1);

        /* Render simple animated stereo VU meters */
        uint32_t vu_l = (uint32_t)((s_sine_lut[(phase_l >> 16) & 0xFF] > 0 ? s_sine_lut[(phase_l >> 16) & 0xFF] : -s_sine_lut[(phase_l >> 16) & 0xFF]) * 100 / 30000);
        uint32_t vu_r = (uint32_t)((s_sine_lut[(phase_r >> 16) & 0xFF] > 0 ? s_sine_lut[(phase_r >> 16) & 0xFF] : -s_sine_lut[(phase_r >> 16) & 0xFF]) * 100 / 30000);

        draw_text(&fb, 16, 160, "L", 0xFFFFFF, 1);
        draw_rect(&fb, 32, 160, 200, 10, 0x304050);
        draw_rect(&fb, 32, 160, vu_l * 2, 10, 0x00FF88);

        draw_text(&fb, 16, 180, "R", 0xFFFFFF, 1);
        draw_rect(&fb, 32, 180, 200, 10, 0x304050);
        draw_rect(&fb, 32, 180, vu_r * 2, 10, 0x00FF88);

        vflash_video_present(&fb);
    }
}
