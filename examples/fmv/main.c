#include "vflash/video.h"
#include "vflash/audio.h"
#include "vflash/fs.h"
#include "vflash/input.h"
#include "vflash/clock.h"
#include "vflash/draw.h"
#include <stdbool.h>
#include <stdint.h>

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
#include "vflash/mmu.h"
#endif

/* Symbols inspected by emulator_smoke.py and regression harness */
volatile uint32_t boot_marker = 0;
volatile uint32_t startup_status = 0;
volatile uint32_t data_cookie = 0x13579BDFu;

volatile uint32_t fmv_status = 0;
volatile uint32_t fmv_video_opened = 0;
volatile uint32_t fmv_frames_decoded = 0;
volatile uint32_t fmv_audio_samples = 0;
volatile uint32_t fmv_state = 0;
volatile uint32_t fmv_pause_tested = 0;
volatile uint32_t fmv_stop_tested = 0;
volatile uint32_t fmv_restart_tested = 0;
volatile uint32_t fmv_all_passed = 0;

static vflash_framebuffer_t fb;
static vflash_video_player_t player;

#define VIDEO_DST_X 48
#define VIDEO_DST_Y 28

static void render_ui(const vflash_video_stats_t *stats, bool loop)
{
    /* Clear borders / telemetry area with dark navy */
    draw_rect(&fb, 0, 0, 320, VIDEO_DST_Y, 0x0A1020);
    draw_rect(&fb, 0, VIDEO_DST_Y, VIDEO_DST_X, 114, 0x0A1020);
    draw_rect(&fb, VIDEO_DST_X + stats->width, VIDEO_DST_Y, 320 - (VIDEO_DST_X + stats->width), 114, 0x0A1020);
    draw_rect(&fb, 0, VIDEO_DST_Y + stats->height + 2, 320, 240 - (VIDEO_DST_Y + stats->height + 2), 0x0A1020);

    /* Header banner */
    draw_text(&fb, 16, 8, "V.FLASH NATIVE FMV PLAYER (MJP)", 0x50C0FF, 1);

    /* Video frame border */
    draw_rect(&fb, VIDEO_DST_X - 1, VIDEO_DST_Y - 1, stats->width + 2, 1, 0x3060A0);
    draw_rect(&fb, VIDEO_DST_X - 1, VIDEO_DST_Y + stats->height, stats->width + 2, 1, 0x3060A0);
    draw_rect(&fb, VIDEO_DST_X - 1, VIDEO_DST_Y, 1, stats->height, 0x3060A0);
    draw_rect(&fb, VIDEO_DST_X + stats->width, VIDEO_DST_Y, 1, stats->height, 0x3060A0);

    /* Telemetry Panel */
    int ty = 148;
    const char *state_str = "STOPPED";
    uint32_t state_color = 0xFF4040;
    if (stats->state == VFLASH_VIDEO_PLAYING) {
        state_str = "PLAYING";
        state_color = 0x40FF60;
    } else if (stats->state == VFLASH_VIDEO_PAUSED) {
        state_str = "PAUSED";
        state_color = 0xFFD020;
    } else if (stats->state == VFLASH_VIDEO_FINISHED) {
        state_str = "FINISHED";
        state_color = 0x40C0FF;
    }

    draw_text(&fb, 16, ty, "STATUS:", 0xAAAAAA, 1);
    draw_text(&fb, 72, ty, state_str, state_color, 1);

    char buf[64];
    /* Format frame count */
    int f_cur = stats->current_frame;
    int f_tot = stats->frame_count;
    buf[0] = 'F'; buf[1] = 'R'; buf[2] = 'A'; buf[3] = 'M'; buf[4] = 'E'; buf[5] = ':'; buf[6] = ' ';
    buf[7] = '0' + (f_cur / 100) % 10;
    buf[8] = '0' + (f_cur / 10) % 10;
    buf[9] = '0' + (f_cur % 10);
    buf[10] = ' '; buf[11] = '/'; buf[12] = ' ';
    buf[13] = '0' + (f_tot / 100) % 10;
    buf[14] = '0' + (f_tot / 10) % 10;
    buf[15] = '0' + (f_tot % 10);
    buf[16] = '\0';
    draw_text(&fb, 170, ty, buf, 0xFFFFFF, 1);

    ty += 16;
    draw_text(&fb, 16, ty, "RES:", 0xAAAAAA, 1);
    draw_text(&fb, 52, ty, "224x112 @ 15 FPS", 0xE0E0E0, 1);

    draw_text(&fb, 170, ty, loop ? "LOOP: ON" : "LOOP: OFF", loop ? 0x60FF80 : 0x888888, 1);

    ty += 16;
    draw_text(&fb, 16, ty, "AUDIO: PCM 16-BIT 22.05 kHz", 0x80D0FF, 1);

    ty += 18;
    draw_text(&fb, 16, ty, "OK:Play/Pause  RED:Stop  YEL:Restart  BLU:Loop", 0x90A0B0, 1);
}

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu) ? 0x600D600Du : 0;
    boot_marker = 0x56464C53u;

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
    vflash_mmu_init();
    vflash_mmu_identity();
    vflash_mmu_enable();
#endif

    vflash_clock_init();
    vflash_video_init(&fb);
    vflash_input_init();

    draw_clear(&fb, 0x050810);
    draw_text(&fb, 80, 110, "INITIALIZING FILESYSTEM...", 0x80C0FF, 1);
    vflash_video_present(&fb);

    if (vflash_fs_init() != 0) {
        draw_clear(&fb, 0x300505);
        draw_text(&fb, 80, 110, "ERROR: FS INIT FAILED!", 0xFF5050, 1);
        vflash_video_present(&fb);
        while (1) {}
    }

    /* Open MJP Video */
    if (vflash_video_open(&player, "DISC.MJP", VIDEO_DST_X, VIDEO_DST_Y) != 0) {
        draw_clear(&fb, 0x300505);
        draw_text(&fb, 80, 110, "ERROR: OPEN DISC.MJP FAILED!", 0xFF5050, 1);
        vflash_video_present(&fb);
        while (1) {}
    }

    fmv_status = 0x464D5631; /* "FMV1" */
    fmv_video_opened = 1;

    bool loop_mode = true;
    vflash_video_set_loop(&player, loop_mode);
    vflash_video_play(&player);

    uint32_t iteration = 0;

    while (1) {
        iteration++;
        vflash_input_poll();
        const vflash_input_state_t *st = vflash_input_get_state();

        /* Controller interaction */
        if (st->pressed & (VFLASH_BTN_OK | VFLASH_BTN_GREEN)) {
            if (vflash_video_get_state(&player) == VFLASH_VIDEO_PLAYING) {
                vflash_video_pause(&player);
                fmv_pause_tested = 1;
            } else {
                vflash_video_play(&player);
            }
        }
        if (st->pressed & VFLASH_BTN_RED) {
            vflash_video_stop(&player);
            fmv_stop_tested = 1;
        }
        if (st->pressed & VFLASH_BTN_YELLOW) {
            vflash_video_restart(&player);
            fmv_restart_tested = 1;
        }
        if (st->pressed & VFLASH_BTN_BLUE) {
            loop_mode = !loop_mode;
            vflash_video_set_loop(&player, loop_mode);
        }

        /* Update video & audio */
        int frame_ready = vflash_video_update(&player, &fb);
        if (frame_ready) {
            fmv_frames_decoded++;
        }

        vflash_video_stats_t stats;
        vflash_video_get_stats(&player, &stats);
        fmv_state = (uint32_t)stats.state;
        fmv_audio_samples = stats.audio_samples_queued;

        if (fmv_frames_decoded >= 15 && fmv_audio_samples >= 1000) {
            fmv_all_passed = 1;
        }

        /* Render UI telemetry */
        render_ui(&stats, loop_mode);
        vflash_video_present(&fb);
    }
}

