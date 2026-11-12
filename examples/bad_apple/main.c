#include "vflash/video.h"
#include "vflash/audio.h"
#include "vflash/fs.h"
#include "vflash/input.h"
#include "vflash/clock.h"
#include "vflash/draw.h"
#include "vflash/loading.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
#include "vflash/mmu.h"
#define MMU_LABEL "ON"
#else
#define MMU_LABEL "OFF"
#endif

/* Regression inspection symbols */
volatile uint32_t boot_marker __attribute__((used)) = 0;
volatile uint32_t startup_status __attribute__((used)) = 0;
volatile uint32_t data_cookie __attribute__((used)) = 0x13579BDFu;

volatile uint32_t bad_apple_status __attribute__((used)) = 0;
volatile uint32_t bad_apple_frames __attribute__((used)) = 0;
volatile uint32_t bad_apple_audio_samples __attribute__((used)) = 0;
volatile uint32_t bad_apple_passed __attribute__((used)) = 0;

/* Milestone timestamps (measured in milliseconds from boot) */
volatile uint32_t milestone_hw __attribute__((used)) = 0;
volatile uint32_t milestone_mount __attribute__((used)) = 0;
volatile uint32_t milestone_validate __attribute__((used)) = 0;
volatile uint32_t milestone_audio __attribute__((used)) = 0;
volatile uint32_t milestone_first_frame __attribute__((used)) = 0;
volatile uint32_t milestone_total_ms __attribute__((used)) = 0;

static vflash_framebuffer_t s_fb;
static vflash_video_player_t s_player;

/* Candidate profiles for in-engine profile cycling via D-pad */
static const char * const s_profile_files[] = {
    "DISC.MJP",
    "BA320_10.MJP",
    "BA320_15.MJP",
    "BA256_15.MJP",
    "BA256_20.MJP",
    "BA240_15.MJP",
    "BA240_20.MJP",
    "BA192_24.MJP",
    "BA160_30.MJP"
};
#define NUM_PROFILES (sizeof(s_profile_files) / sizeof(s_profile_files[0]))
static int s_current_profile_idx = 0;

static bool s_show_hud = true;
static uint32_t s_last_fps_ticks = 0;
static uint32_t s_fps_frame_count = 0;
static uint32_t s_fps_whole = 0;
static uint32_t s_fps_frac = 0;

/* Safe integer to string conversion */
static void int_to_str(uint32_t val, char *buf, int min_digits)
{
    char tmp[12];
    int pos = 0;
    if (val == 0) {
        tmp[pos++] = '0';
    } else {
        while (val > 0) {
            tmp[pos++] = '0' + (val % 10);
            val /= 10;
        }
    }
    while (pos < min_digits) {
        tmp[pos++] = '0';
    }
    for (int i = 0; i < pos; ++i) {
        buf[i] = tmp[pos - 1 - i];
    }
    buf[pos] = '\0';
}

static uint32_t str_len(const char *s)
{
    uint32_t l = 0;
    while (s && s[l]) l++;
    return l;
}

static void append_str(char *dst, const char *src)
{
    uint32_t dl = str_len(dst);
    uint32_t sl = str_len(src);
    for (uint32_t i = 0; i < sl; ++i) {
        dst[dl + i] = src[i];
    }
    dst[dl + sl] = '\0';
}

static void setup_video_centering(vflash_video_player_t *player)
{
    int dst_x = (int)(VFLASH_WIDTH - player->width) / 2;
    int dst_y = (int)(VFLASH_HEIGHT - player->height) / 2;
    if (dst_x < 0) dst_x = 0;
    if (dst_y < 0) dst_y = 0;
    player->dst_x = dst_x;
    player->dst_y = dst_y;
}

static void draw_hud_overlay(vflash_framebuffer_t *fb, const vflash_video_stats_t *stats)
{
    if (!s_show_hud) return;

    /* If video is smaller than screen (e.g. 256x192), render HUD cleanly in black letterbox borders */
    int top_y = (s_player.dst_y >= 20) ? 2 : 2;
    int bot_y = (s_player.dst_y >= 20) ? (VFLASH_HEIGHT - 20) : (VFLASH_HEIGHT - 32);

    /* Semi-opaque/dark background boxes for HUD readability */
    if (s_player.dst_y < 20) {
        draw_rect(fb, 0, 0, VFLASH_WIDTH, 22, 0x081018);
        draw_rect(fb, 0, VFLASH_HEIGHT - 24, VFLASH_WIDTH, 24, 0x081018);
    }

    /* Top HUD: Title, Resolution, FPS, MMU */
    char line1[64] = "BAD APPLE!! ";
    char num[16];
    int_to_str(stats->width, num, 1); append_str(line1, num);
    append_str(line1, "x");
    int_to_str(stats->height, num, 1); append_str(line1, num);
    append_str(line1, " @ ");
    int_to_str(stats->fps, num, 1); append_str(line1, num);
    append_str(line1, "fps  [MMU: ");
    append_str(line1, MMU_LABEL);
    append_str(line1, "]");
    draw_text(fb, 4, top_y, line1, 0x50C0FF, 1);

    /* Top HUD Line 2: Frame counter, decode latency, drops */
    char line2[64] = "F: ";
    int_to_str(stats->current_frame, num, 1); append_str(line2, num);
    append_str(line2, "/");
    int_to_str(stats->frame_count, num, 1); append_str(line2, num);
    append_str(line2, "  FPS: ");
    int_to_str(s_fps_whole, num, 1); append_str(line2, num);
    append_str(line2, ".");
    int_to_str(s_fps_frac, num, 1); append_str(line2, num);
    append_str(line2, "  DEC: ");
    int_to_str(stats->decode_time_us, num, 1); append_str(line2, num);
    append_str(line2, "us  DROP: ");
    int_to_str(stats->dropped_frames, num, 1); append_str(line2, num);
    draw_text(fb, 4, top_y + 10, line2, 0xFFFFFF, 1);

    /* Bottom HUD: Audio & Controls */
    vflash_audio_stats_t a_stats;
    vflash_audio_get_stats(&a_stats);

    char line3[64] = "AUDIO: ";
    int_to_str(a_stats.samples_played, num, 1); append_str(line3, num);
    append_str(line3, "  UNDERRUN: ");
    int_to_str(a_stats.underrun_count, num, 1); append_str(line3, num);
    append_str(line3, "  QUEUED: ");
    int_to_str(a_stats.queued_bytes, num, 1); append_str(line3, num);
    append_str(line3, "B");
    draw_text(fb, 4, bot_y, line3, 0x8FA4C4, 1);

    char line4[64] = "[RED]Pause  [GRN]Restart  [YEL]HUD  [<>]Profile";
    draw_text(fb, 4, bot_y + 10, line4, 0xFFD700, 1);
}

static bool load_profile(int profile_idx)
{
    const char *path = s_profile_files[profile_idx];
    if (s_player.fd >= 0) {
        vflash_video_close(&s_player);
    }
    if (vflash_video_open(&s_player, path, 0, 0) != 0) {
        return false;
    }
    setup_video_centering(&s_player);
    s_player.loop = true;
    s_current_profile_idx = profile_idx;
    return true;
}

void vflash_main(void)
{
#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
    vflash_mmu_init();
    vflash_mmu_identity();
    vflash_mmu_enable();
#endif

    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu) ? 0x600D600Du : 0;
    boot_marker = 0x56464C53u;
    uint16_t t_boot = vflash_clock_ticks();

    /* =========================================================================
     * STAGE 1: HARDWARE INITIALIZATION & RETAIL LOADING SCREEN
     * ========================================================================= */
    vflash_clock_init();
    vflash_loading_config_t load_cfg = {
        .title = "V.FLASH SDK",
        .subtitle = "Bad Apple!! Video Demo",
        .caption = "Open-Source Homebrew Platform",
        .copyright = "(C) 2026 SDK Contributors"
    };
    vflash_loading_init_ex(&load_cfg);
    milestone_hw = ((uint16_t)(vflash_clock_ticks() - t_boot)) / 10u;

    vflash_loading_set_progress(15);
    vflash_loading_set_status("Initializing hardware");
    vflash_loading_render();

    /* =========================================================================
     * STAGE 2: CONTROLLER INITIALIZATION
     * ========================================================================= */
    vflash_input_init();
    vflash_loading_set_progress(30);
    vflash_loading_set_status("Scanning controllers");
    vflash_loading_render();

    /* =========================================================================
     * STAGE 3: CD-ROM & ISO 9660 FILESYSTEM MOUNT
     * ========================================================================= */
    vflash_loading_set_progress(45);
    vflash_loading_set_status("Mounting V.DISC");
    vflash_loading_render();

    uint16_t t_mount_start = vflash_clock_ticks();
    int fs_rc = vflash_fs_init();
    milestone_mount = ((uint16_t)(vflash_clock_ticks() - t_mount_start)) / 10u;
    if (fs_rc != 0) {
        vflash_loading_set_status("V.DISC mount failed!");
        vflash_loading_render();
        while (1) {}
    }

    /* =========================================================================
     * STAGE 4: LOCATE & VALIDATE MJP VIDEO
     * ========================================================================= */
    vflash_loading_set_progress(60);
    vflash_loading_set_status("Locating video");
    vflash_loading_render();

    uint16_t t_mjp_start = vflash_clock_ticks();
    bool loaded = false;
    for (size_t i = 0; i < NUM_PROFILES; ++i) {
        if (load_profile((int)i)) {
            loaded = true;
            break;
        }
    }
    milestone_validate = ((uint16_t)(vflash_clock_ticks() - t_mjp_start)) / 10u;
    if (!loaded) {
        vflash_loading_set_status("No MJP video found!");
        vflash_loading_render();
        while (1) {}
    }

    /* =========================================================================
     * STAGE 5: AUDIO SUBSYSTEM INITIALIZATION
     * ========================================================================= */
    vflash_loading_set_progress(80);
    vflash_loading_set_status("Preparing audio");
    vflash_loading_render();

    uint16_t t_audio_start = vflash_clock_ticks();
    vflash_audio_init(NULL);
    milestone_audio = ((uint16_t)(vflash_clock_ticks() - t_audio_start)) / 10u;

    /* =========================================================================
     * STAGE 6: DECODE FIRST FRAME
     * ========================================================================= */
    vflash_loading_set_progress(95);
    vflash_loading_set_status("Decoding first frame");
    vflash_loading_render();

    uint16_t t_frame_start = vflash_clock_ticks();
    vflash_video_play(&s_player);
    milestone_first_frame = ((uint16_t)(vflash_clock_ticks() - t_frame_start)) / 10u;

    milestone_total_ms = ((uint16_t)(vflash_clock_ticks() - t_boot)) / 10u;
    vflash_loading_set_progress(100);
    vflash_loading_set_status("Ready");
    vflash_loading_render();

    /* =========================================================================
     * STAGE 7: TRANSITION TO PLAYBACK
     * ========================================================================= */
    vflash_loading_finish();

    /* Clear letterbox borders on both buffers for flicker-free playback */
    vflash_framebuffer_t back_fb;
    if (vflash_video_backbuffer(&back_fb)) {
        draw_clear(&back_fb, 0x000000);
    }
    vflash_video_present(&back_fb);
    if (vflash_video_backbuffer(&back_fb)) {
        draw_clear(&back_fb, 0x000000);
    }

    startup_status = 0x600D600Du;
    bad_apple_status = 0x42415031u; /* 'BAP1' */
    s_last_fps_ticks = vflash_clock_ticks();

    /* =========================================================================
     * MAIN PLAYBACK & DIAGNOSTIC LOOP
     * ========================================================================= */
    while (1) {
        vflash_input_poll();
        const vflash_input_state_t *st = vflash_input_get_state();

        if (st) {
            /* RED: Pause / Resume */
            if (st->pressed & VFLASH_BTN_RED) {
                if (s_player.state == VFLASH_VIDEO_PLAYING) {
                    vflash_video_pause(&s_player);
                } else {
                    vflash_video_play(&s_player);
                }
            }
            /* GREEN: Restart */
            if (st->pressed & VFLASH_BTN_GREEN) {
                vflash_video_restart(&s_player);
            }
            /* YELLOW: Toggle HUD */
            if (st->pressed & VFLASH_BTN_YELLOW) {
                s_show_hud = !s_show_hud;
                /* If turning HUD off, clear borders to erase HUD remnants */
                if (!s_show_hud && vflash_video_backbuffer(&back_fb)) {
                    draw_clear(&back_fb, 0x000000);
                }
            }
            /* BLUE: Return to loading screen demo */
            if (st->pressed & VFLASH_BTN_BLUE) {
                vflash_loading_init();
                vflash_loading_set_progress(50);
                vflash_loading_set_status("Testing loading screen");
                vflash_loading_render();
                for (volatile int d = 0; d < 500000; ++d) {}
                vflash_loading_finish();
            }
            /* OK: Play / Pause */
            if (st->pressed & VFLASH_BTN_OK) {
                if (s_player.state == VFLASH_VIDEO_PLAYING) {
                    vflash_video_pause(&s_player);
                } else {
                    vflash_video_play(&s_player);
                }
            }
            /* D-Pad LEFT / RIGHT: Switch profile */
            if (st->pressed & VFLASH_BTN_LEFT) {
                int next = (s_current_profile_idx - 1 + (int)NUM_PROFILES) % (int)NUM_PROFILES;
                if (load_profile(next)) {
                    vflash_video_play(&s_player);
                }
            }
            if (st->pressed & VFLASH_BTN_RIGHT) {
                int next = (s_current_profile_idx + 1) % (int)NUM_PROFILES;
                if (load_profile(next)) {
                    vflash_video_play(&s_player);
                }
            }
        }

        /* Render target buffer */
        vflash_framebuffer_t render_fb;
        if (!vflash_video_backbuffer(&render_fb)) {
            render_fb = s_fb;
        }

        int frame_ready = vflash_video_update(&s_player, &render_fb);
        if (frame_ready) {
            bad_apple_frames++;
            s_fps_frame_count++;

            /* FPS computation every 1.0 second (10,000 timer ticks) */
            uint16_t now = vflash_clock_ticks();
            uint16_t delta = (uint16_t)(now - s_last_fps_ticks);
            if (delta >= 10000) {
                uint32_t fps_scaled = (s_fps_frame_count * 100000u) / delta;
                s_fps_whole = fps_scaled / 10u;
                s_fps_frac = fps_scaled % 10u;
                s_fps_frame_count = 0;
                s_last_fps_ticks = now;
            }

            vflash_video_stats_t stats;
            vflash_video_get_stats(&s_player, &stats);
            bad_apple_audio_samples = stats.audio_samples_queued;

            if (bad_apple_frames >= 15 && bad_apple_audio_samples >= 1000) {
                bad_apple_passed = 1;
            }

            /* Draw telemetry HUD overlay */
            draw_hud_overlay(&render_fb, &stats);

            /* Present frame to screen (synchronized flip) */
            vflash_video_present(&render_fb);
        } else {
            vflash_audio_update();
        }
    }
}
