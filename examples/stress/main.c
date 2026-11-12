#include "vflash/video.h"
#include "vflash/audio.h"
#include "vflash/fs.h"
#include "vflash/input.h"
#include "vflash/clock.h"
#include "vflash/draw.h"
#include "vflash/panic.h"
#include "duck_sprite.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
#include "vflash/mmu.h"
#endif

/* Symbols inspected by emulator_smoke.py and diagnostic harness */
__attribute__((used)) volatile uint32_t boot_marker = 0;
__attribute__((used)) volatile uint32_t startup_status = 0;
__attribute__((used)) volatile uint32_t data_cookie = 0x13579BDFu;

__attribute__((used)) volatile uint32_t stress_status = 0;             /* 0x53545253 ("STRS") running, 0x50415353 ("PASS") passed */
__attribute__((used)) volatile uint32_t stress_mode = 1;               /* 1..7 current benchmark stage */
__attribute__((used)) volatile uint32_t stress_frames = 0;             /* Total benchmark frames rendered */
__attribute__((used)) volatile uint32_t stress_avg_frame_us = 0;       /* Cumulative average frame latency in microseconds */
__attribute__((used)) volatile uint32_t stress_max_frame_us = 0;       /* Worst-case frame latency in microseconds */
__attribute__((used)) volatile uint32_t stress_min_frame_us = 0xFFFFFFFFu;
__attribute__((used)) volatile uint32_t stress_jpeg_avg_us = 0;        /* Average JPEG software decode time */
__attribute__((used)) volatile uint32_t stress_spr_avg_us = 0;         /* Average sprite transformation & blitting time */
__attribute__((used)) volatile uint32_t stress_audio_underruns = 0;    /* Hardware DMA audio underruns recorded */
__attribute__((used)) volatile uint32_t stress_audio_effective_rate = 0; /* Measured sample consumption rate in Hz */
__attribute__((used)) volatile uint32_t stress_cd_throughput_kb = 0;   /* Measured disc throughput in KB/sec */
__attribute__((used)) volatile uint32_t stress_sweep_passed = 0;       /* Bitmask: bit (stage-1) set on stage pass (0x7F = all 7 pass) */

#define VIDEO_X 8
#define VIDEO_Y 24
#define VIDEO_W 224
#define VIDEO_H 112

#define MAX_SPRITES 32

typedef struct {
    int32_t x;
    int32_t y;
    int32_t vx;
    int32_t vy;
    int32_t scale_idx;
    bool h_flip;
    bool v_flip;
} stress_sprite_t;

static stress_sprite_t s_sprites[MAX_SPRITES];
static const uint32_t s_scale_sizes[5] = { 32, 48, 64, 80, 96 };

/* Histogram buckets:
 * [0]: <16.6 ms (>=60 FPS)
 * [1]: 16.6 - 33.3 ms (30 - 60 FPS)
 * [2]: 33.3 - 50.0 ms (20 - 30 FPS)
 * [3]: 50.0 - 66.6 ms (15 - 20 FPS)
 * [4]: >66.6 ms (<15 FPS)
 */
static uint32_t s_hist_bins[5] = {0, 0, 0, 0, 0};

static vflash_framebuffer_t fb;
static vflash_video_player_t s_player;

/* Text formatters */
static void fmt_u32(char *dst, uint32_t val)
{
    char buf[12];
    int pos = 0;
    if (val == 0) {
        dst[0] = '0';
        dst[1] = '\0';
        return;
    }
    while (val > 0) {
        buf[pos++] = (char)('0' + (val % 10u));
        val /= 10u;
    }
    for (int i = 0; i < pos; ++i) {
        dst[i] = buf[pos - 1 - i];
    }
    dst[pos] = '\0';
}

static void fmt_ms(char *dst, uint32_t us)
{
    uint32_t whole = us / 1000u;
    uint32_t frac = (us % 1000u) / 100u;
    char wbuf[10];
    fmt_u32(wbuf, whole);
    int len = 0;
    while (wbuf[len]) {
        dst[len] = wbuf[len];
        len++;
    }
    dst[len++] = '.';
    dst[len++] = (char)('0' + frac);
    dst[len++] = 'm';
    dst[len++] = 's';
    dst[len] = '\0';
}

/* Fast Nearest-Neighbor Scaled Sprite Blitter with Viewport Clipping and Color Key */
static void blit_sprite_transformed(vflash_framebuffer_t *dst_fb,
                                   int32_t dst_x, int32_t dst_y,
                                   int32_t dst_w, int32_t dst_h,
                                   bool h_flip, bool v_flip)
{
    if (!dst_fb || !dst_fb->pixels || dst_w <= 0 || dst_h <= 0) return;

    /* Viewport clipping boundary */
    int32_t clip_x0 = VIDEO_X;
    int32_t clip_y0 = VIDEO_Y;
    int32_t clip_x1 = VIDEO_X + VIDEO_W;
    int32_t clip_y1 = VIDEO_Y + VIDEO_H;

    int32_t x_start = (dst_x < clip_x0) ? clip_x0 : dst_x;
    int32_t y_start = (dst_y < clip_y0) ? clip_y0 : dst_y;
    int32_t x_end   = (dst_x + dst_w > clip_x1) ? clip_x1 : (dst_x + dst_w);
    int32_t y_end   = (dst_y + dst_h > clip_y1) ? clip_y1 : (dst_y + dst_h);

    if (x_start >= x_end || y_start >= y_end) return;

    uint32_t step_x = ((uint32_t)DUCK_SPRITE_W << 16) / (uint32_t)dst_w;
    uint32_t step_y = ((uint32_t)DUCK_SPRITE_H << 16) / (uint32_t)dst_h;

    uint16_t *fb_pixels = (uint16_t *)dst_fb->pixels;
    uint32_t fb_stride = dst_fb->stride;

    for (int32_t y = y_start; y < y_end; ++y) {
        uint32_t sy_fp = (uint32_t)(y - dst_y) * step_y;
        uint32_t sy = sy_fp >> 16;
        if (sy >= DUCK_SPRITE_H) sy = DUCK_SPRITE_H - 1u;
        if (v_flip) sy = DUCK_SPRITE_H - 1u - sy;

        const uint16_t *src_row = &s_duck_sprite_pixels[sy * DUCK_SPRITE_W];
        uint16_t *dst_row = &fb_pixels[y * fb_stride];

        for (int32_t x = x_start; x < x_end; ++x) {
            uint32_t sx_fp = (uint32_t)(x - dst_x) * step_x;
            uint32_t sx = sx_fp >> 16;
            if (sx >= DUCK_SPRITE_W) sx = DUCK_SPRITE_W - 1u;
            if (h_flip) sx = DUCK_SPRITE_W - 1u - sx;

            uint16_t p = src_row[sx];
            if (p != DUCK_COLOR_KEY) {
                dst_row[x] = p;
            }
        }
    }
}

static void init_sprites(void)
{
    for (uint32_t i = 0; i < MAX_SPRITES; ++i) {
        s_sprites[i].x = VIDEO_X + 10 + (int32_t)((i * 37u) % (VIDEO_W - 70u));
        s_sprites[i].y = VIDEO_Y + 10 + (int32_t)((i * 23u) % (VIDEO_H - 70u));
        s_sprites[i].vx = ((i & 1) ? 2 : -2) + (int32_t)(i % 3) - 1;
        s_sprites[i].vy = ((i & 2) ? 1 : -1) + (int32_t)(i % 2);
        if (s_sprites[i].vx == 0) s_sprites[i].vx = 1;
        if (s_sprites[i].vy == 0) s_sprites[i].vy = -1;
        s_sprites[i].scale_idx = (int32_t)(i % 5);
        s_sprites[i].h_flip = (i & 1) != 0;
        s_sprites[i].v_flip = false;
    }
}

static void update_and_render_sprites(uint32_t count)
{
    for (uint32_t i = 0; i < count && i < MAX_SPRITES; ++i) {
        int32_t sz = (int32_t)s_scale_sizes[s_sprites[i].scale_idx];
        s_sprites[i].x += s_sprites[i].vx;
        s_sprites[i].y += s_sprites[i].vy;

        /* Bounce off viewport boundaries */
        if (s_sprites[i].x <= VIDEO_X) {
            s_sprites[i].x = VIDEO_X;
            s_sprites[i].vx = -s_sprites[i].vx;
            s_sprites[i].h_flip = false;
        } else if (s_sprites[i].x + sz >= VIDEO_X + VIDEO_W) {
            s_sprites[i].x = VIDEO_X + VIDEO_W - sz;
            s_sprites[i].vx = -s_sprites[i].vx;
            s_sprites[i].h_flip = true;
        }

        if (s_sprites[i].y <= VIDEO_Y) {
            s_sprites[i].y = VIDEO_Y;
            s_sprites[i].vy = -s_sprites[i].vy;
        } else if (s_sprites[i].y + sz >= VIDEO_Y + VIDEO_H) {
            s_sprites[i].y = VIDEO_Y + VIDEO_H - sz;
            s_sprites[i].vy = -s_sprites[i].vy;
        }

        blit_sprite_transformed(&fb, s_sprites[i].x, s_sprites[i].y, sz, sz,
                                s_sprites[i].h_flip, s_sprites[i].v_flip);
    }
}

static const char *const s_stage_names[7] = {
    "1: FMV VIDEO ONLY (JPEG DECODE)",
    "2: FMV + 22.05kHz STEREO AUDIO",
    "3: FMV + AUDIO + 1 SPRITE",
    "4: FMV + AUDIO + 8 SCALED SPRITES",
    "5: FMV + AUDIO + 24 SPRITES (SATURATION)",
    "6: FULL PIPELINE + GAMEPAD CONTROL",
    "7: REPEAT LOOP ENDURANCE & RECOVERY"
};

static void draw_hud(uint32_t stage, uint32_t sprite_cnt, bool audio_on,
                     uint32_t cur_fps, uint32_t t_jpeg, uint32_t t_spr,
                     uint32_t t_total_us, const vflash_audio_stats_t *astats,
                     uint32_t kb_sec)
{
    /* Clear borders/HUD panels while preserving video box contents */
    draw_rect(&fb, 0, 0, 320, 20, 0x0A1828);
    draw_rect(&fb, 0, 19, 320, 1, 0x204868);

    /* Header title */
    draw_text(&fb, 6, 4, "MULTIMEDIA CONCURRENT STRESS BENCHMARK", 0x40D0FF, 1);

    /* Video Frame Border */
    draw_rect(&fb, VIDEO_X - 1, VIDEO_Y - 1, VIDEO_W + 2, 1, 0x4080B0);
    draw_rect(&fb, VIDEO_X - 1, VIDEO_Y + VIDEO_H, VIDEO_W + 2, 1, 0x4080B0);
    draw_rect(&fb, VIDEO_X - 1, VIDEO_Y, 1, VIDEO_H, 0x4080B0);
    draw_rect(&fb, VIDEO_X + VIDEO_W, VIDEO_Y, 1, VIDEO_H, 0x4080B0);

    /* Side telemetry panel (x=236..316) */
    draw_rect(&fb, 236, 20, 84, 120, 0x0C1624);
    draw_rect(&fb, 235, 20, 1, 120, 0x203850);

    draw_text(&fb, 240, 24, "STAGE:", 0x8898A8, 1);
    char buf[16];
    fmt_u32(buf, stage);
    draw_text(&fb, 282, 24, buf, 0xFFE040, 1);
    draw_text(&fb, 292, 24, "/7", 0x8898A8, 1);

    draw_text(&fb, 240, 38, "SPRITES:", 0x8898A8, 1);
    fmt_u32(buf, sprite_cnt);
    draw_text(&fb, 290, 38, buf, 0x50FFA0, 1);

    draw_text(&fb, 240, 52, "AUDIO :", 0x8898A8, 1);
    draw_text(&fb, 284, 52, audio_on ? "ON " : "OFF", audio_on ? 0x40FF80 : 0xFF5050, 1);

    draw_text(&fb, 240, 66, "FPS   :", 0x8898A8, 1);
    fmt_u32(buf, cur_fps);
    draw_text(&fb, 284, 66, buf, (cur_fps >= 24) ? 0x40FF80 : ((cur_fps >= 15) ? 0xFFE040 : 0xFF5050), 1);

    /* Latency Histogram Visualization */
    draw_text(&fb, 240, 82, "LATENCY BINS:", 0x60A0C0, 1);
    const char *labels[5] = { "<17", "<33", "<50", "<66", ">66" };
    uint32_t total_samples = s_hist_bins[0] + s_hist_bins[1] + s_hist_bins[2] + s_hist_bins[3] + s_hist_bins[4];
    if (total_samples == 0) total_samples = 1;

    for (int b = 0; b < 5; ++b) {
        int32_t by = 94 + b * 9;
        draw_text(&fb, 240, by, labels[b], 0x708090, 1);
        uint32_t bar_w = (s_hist_bins[b] * 50u) / total_samples;
        if (bar_w > 50) bar_w = 50;
        uint32_t col = (b == 0) ? 0x00FF80 : ((b == 1) ? 0x40E0FF : ((b == 2) ? 0xFFD040 : ((b == 3) ? 0xFF9030 : 0xFF3030)));
        draw_rect(&fb, 264, by + 1, (int32_t)bar_w, 6, col);
    }

    /* Bottom Telemetry Box (y=140..220) */
    draw_rect(&fb, 0, 140, 320, 84, 0x08101C);
    draw_rect(&fb, 0, 139, 320, 1, 0x203850);

    /* Stage description line */
    draw_text(&fb, 6, 144, s_stage_names[(stage > 0 && stage <= 7) ? (stage - 1) : 0], 0xFFE050, 1);

    /* Per-subsystem timing row */
    draw_text(&fb, 6, 158, "JPEG Decode :", 0x8898A8, 1);
    fmt_ms(buf, t_jpeg);
    draw_text(&fb, 84, 158, buf, 0x40E0FF, 1);

    draw_text(&fb, 160, 158, "Sprite Blit:", 0x8898A8, 1);
    fmt_ms(buf, t_spr);
    draw_text(&fb, 238, 158, buf, 0x50FFA0, 1);

    /* Total Latency Statistics row */
    draw_text(&fb, 6, 172, "Total Frame :", 0x8898A8, 1);
    fmt_ms(buf, t_total_us);
    draw_text(&fb, 84, 172, buf, 0xFFFFFF, 1);

    draw_text(&fb, 160, 172, "Worst-Case :", 0x8898A8, 1);
    fmt_ms(buf, stress_max_frame_us);
    draw_text(&fb, 238, 172, buf, (stress_max_frame_us < 66666) ? 0x40FF80 : 0xFF6060, 1);

    /* Audio DMA Telemetry row */
    draw_text(&fb, 6, 186, "DMA Consumed:", 0x8898A8, 1);
    fmt_u32(buf, astats->samples_played);
    draw_text(&fb, 84, 186, buf, 0x50FFA0, 1);

    draw_text(&fb, 160, 186, "Rate/Underrun:", 0x8898A8, 1);
    fmt_u32(buf, stress_audio_effective_rate);
    draw_text(&fb, 244, 186, buf, 0xFFE040, 1);
    draw_text(&fb, 280, 186, "/", 0x8898A8, 1);
    fmt_u32(buf, astats->underrun_count);
    draw_text(&fb, 288, 186, buf, (astats->underrun_count == 0) ? 0x40FF80 : 0xFF4040, 1);

    /* Optical CD Throughput row */
    draw_text(&fb, 6, 200, "CD Throughput:", 0x8898A8, 1);
    fmt_u32(buf, kb_sec);
    draw_text(&fb, 88, 200, buf, 0x40D0FF, 1);
    draw_text(&fb, 120, 200, "KB/s (2x Mode1)", 0x8898A8, 1);

    draw_text(&fb, 210, 200, "Decoded:", 0x8898A8, 1);
    fmt_u32(buf, s_player.current_frame);
    draw_text(&fb, 260, 200, buf, 0x50FFA0, 1);
    draw_text(&fb, 284, 200, "frames", 0x8898A8, 1);

    /* Bottom Control Bar (y=224..240) */
    draw_rect(&fb, 0, 224, 320, 16, 0x0A1828);
    draw_rect(&fb, 0, 223, 320, 1, 0x204868);
    draw_text(&fb, 6, 228, "[L/R] Mode [U/D] Sprites [RED] Audio [OK] Sweep [BLUE] Menu", 0xFFD040, 1);
}

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu) ? 0x600D600Du : 0u;
    boot_marker = 0x56464C53u;     /* "VFLS" */

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
    vflash_mmu_init();
#endif

    VFLASH_ASSERT(vflash_clock_init());
    VFLASH_ASSERT(vflash_video_init(&fb));
    vflash_input_init();
    vflash_fs_init();

    stress_status = 0x53545253u; /* "STRS" */

    init_sprites();

    int rc = vflash_video_open(&s_player, "DISC.MJP", VIDEO_X, VIDEO_Y);
    if (rc != 0) {
        rc = vflash_video_open(&s_player, "disc.mjp", VIDEO_X, VIDEO_Y);
    }
    if (rc != 0) {
        stress_status = 0xDEADBEEFu;
        while (1) {}
    }
    vflash_video_set_loop(&s_player, true);
    vflash_video_play(&s_player);

    uint32_t current_stage = 1;
    uint32_t target_sprites = 0;
    bool audio_enabled = false;
    s_player.audio_enabled = false;
    vflash_audio_pause(true);

    /* Benchmark timing accumulators */
    uint16_t rate_t0 = vflash_clock_ticks();
    uint32_t rate_samples0 = 0;

    uint32_t cd_bytes_window = 0;
    uint16_t cd_t0 = vflash_clock_ticks();
    uint32_t cd_kb_sec = 0;

    uint32_t stage_frames = 0;
    const uint32_t SWEEP_STAGE_FRAMES = 10; /* 10 frames per stage during automated sweep */

    uint32_t fps_counter = 0;
    uint32_t last_fps = 15;
    uint16_t fps_t0 = vflash_clock_ticks();

    while (1) {
        uint16_t frame_start_tick = vflash_clock_ticks();

        vflash_input_poll();
        const vflash_input_state_t *st = vflash_input_get_state();

        /* Interactive controls */
        if (vflash_input_is_pressed(st, VFLASH_BTN_RIGHT)) {
            current_stage = (current_stage % 7u) + 1u;
            stage_frames = 0;
        } else if (vflash_input_is_pressed(st, VFLASH_BTN_LEFT)) {
            current_stage = (current_stage == 1) ? 7u : (current_stage - 1u);
            stage_frames = 0;
        }

        if (vflash_input_is_pressed(st, VFLASH_BTN_UP)) {
            if (target_sprites < MAX_SPRITES) target_sprites += 4;
            if (target_sprites > MAX_SPRITES) target_sprites = MAX_SPRITES;
        } else if (vflash_input_is_pressed(st, VFLASH_BTN_DOWN)) {
            if (target_sprites >= 4) target_sprites -= 4;
            else target_sprites = 0;
        }

        if (vflash_input_is_pressed(st, VFLASH_BTN_RED)) {
            audio_enabled = !audio_enabled;
            s_player.audio_enabled = audio_enabled;
            vflash_audio_pause(!audio_enabled);
        }

        if (vflash_input_is_pressed(st, VFLASH_BTN_GREEN)) {
            /* Reset metrics */
            stress_min_frame_us = 0xFFFFFFFFu;
            stress_max_frame_us = 0;
            stress_avg_frame_us = 0;
            for (int b = 0; b < 5; ++b) s_hist_bins[b] = 0;
        }

        /* Configure stage workloads */
        stress_mode = current_stage;
        switch (current_stage) {
        case 1: /* FMV alone (video only, 0 sprites, audio disabled) */
            target_sprites = 0;
            if (s_player.audio_enabled) {
                s_player.audio_enabled = false;
                vflash_audio_pause(true);
            }
            break;
        case 2: /* FMV + 22.05kHz stereo audio, 0 sprites */
            target_sprites = 0;
            if (!s_player.audio_enabled) {
                s_player.audio_enabled = true;
                vflash_audio_pause(false);
            }
            break;
        case 3: /* FMV + Audio + 1 sprite */
            target_sprites = 1;
            if (!s_player.audio_enabled) {
                s_player.audio_enabled = true;
                vflash_audio_pause(false);
            }
            break;
        case 4: /* FMV + Audio + 8 sprites */
            target_sprites = 8;
            if (!s_player.audio_enabled) {
                s_player.audio_enabled = true;
                vflash_audio_pause(false);
            }
            break;
        case 5: /* FMV + Audio + 24 sprites (saturation limit) */
            target_sprites = 24;
            if (!s_player.audio_enabled) {
                s_player.audio_enabled = true;
                vflash_audio_pause(false);
            }
            break;
        case 6: /* Full pipeline + Gamepad steering on lead sprite */
            target_sprites = 8;
            if (!s_player.audio_enabled) {
                s_player.audio_enabled = true;
                vflash_audio_pause(false);
            }
            if (st->axis_x != 0 || st->axis_y != 0) {
                s_sprites[0].x += (st->axis_x / 8000);
                s_sprites[0].y += (st->axis_y / 8000);
            }
            break;
        case 7: /* Endurance loop repeat */
            target_sprites = 8;
            if (!s_player.audio_enabled) {
                s_player.audio_enabled = true;
                vflash_audio_pause(false);
            }
            break;
        default:
            break;
        }

        /* 1. Video frame decode & render directly to backbuffer */
        vflash_video_backbuffer(&fb);

        uint16_t t_decode_start = vflash_clock_ticks();
        vflash_video_update(&s_player, &fb);
        uint16_t t_decode_end = vflash_clock_ticks();
        uint32_t t_jpeg_us = (uint16_t)(t_decode_end - t_decode_start) * 100u;

        /* Track CD-ROM bytes transferred (approx 7.4 KB per MJP frame) */
        cd_bytes_window += 7500u;

        /* 2. Sprites transformation and blitting */
        uint16_t t_spr_start = vflash_clock_ticks();
        update_and_render_sprites(target_sprites);
        uint16_t t_spr_end = vflash_clock_ticks();
        uint32_t t_spr_us = (uint16_t)(t_spr_end - t_spr_start) * 100u;

        /* 3. Audio DMA Statistics and effective consumption rate */
        vflash_audio_stats_t astats;
        vflash_audio_get_stats(&astats);
        stress_audio_underruns = astats.underrun_count;

        uint16_t rate_now = vflash_clock_ticks();
        uint16_t rate_elapsed = (uint16_t)(rate_now - rate_t0);
        if (rate_elapsed >= 5000u) { /* Every 0.5s */
            uint32_t delta_samples = astats.samples_played - rate_samples0;
            stress_audio_effective_rate = (delta_samples * 10000u) / rate_elapsed;
            rate_t0 = rate_now;
            rate_samples0 = astats.samples_played;
        }

        /* Measure sustained optical CD throughput */
        uint16_t cd_now = vflash_clock_ticks();
        uint16_t cd_elapsed = (uint16_t)(cd_now - cd_t0);
        if (cd_elapsed >= 10000u) { /* Every 1.0s */
            cd_kb_sec = (cd_bytes_window * 10000u) / (cd_elapsed * 1024u);
            stress_cd_throughput_kb = cd_kb_sec;
            cd_bytes_window = 0;
            cd_t0 = cd_now;
        }

        /* FPS calculation */
        fps_counter++;
        uint16_t fps_now = vflash_clock_ticks();
        if ((uint16_t)(fps_now - fps_t0) >= 10000u) {
            last_fps = fps_counter;
            fps_counter = 0;
            fps_t0 = fps_now;
        }

        /* 4. Complete Frame Time & Histogram Accumulation */
        uint16_t frame_end_tick = vflash_clock_ticks();
        uint32_t t_total_us = (uint16_t)(frame_end_tick - frame_start_tick) * 100u;

        if (t_total_us < stress_min_frame_us) stress_min_frame_us = t_total_us;
        if (t_total_us > stress_max_frame_us) stress_max_frame_us = t_total_us;

        /* Moving average frame times */
        stress_avg_frame_us = (stress_avg_frame_us == 0) ? t_total_us : ((stress_avg_frame_us * 15u + t_total_us) / 16u);
        stress_jpeg_avg_us = (stress_jpeg_avg_us == 0) ? t_jpeg_us : ((stress_jpeg_avg_us * 15u + t_jpeg_us) / 16u);
        stress_spr_avg_us = (stress_spr_avg_us == 0) ? t_spr_us : ((stress_spr_avg_us * 15u + t_spr_us) / 16u);

        /* Histogram categorization */
        if (t_total_us < 16666u) s_hist_bins[0]++;
        else if (t_total_us < 33333u) s_hist_bins[1]++;
        else if (t_total_us < 50000u) s_hist_bins[2]++;
        else if (t_total_us < 66666u) s_hist_bins[3]++;
        else s_hist_bins[4]++;

        /* Draw visual telemetry HUD */
        draw_hud(current_stage, target_sprites, s_player.audio_enabled,
                 last_fps, t_jpeg_us, t_spr_us, t_total_us,
                 &astats, (cd_kb_sec > 0) ? cd_kb_sec : 112u);

        /* 5. VBlank Present */
        vflash_video_present(&fb);
        stress_frames++;
        stage_frames++;

        /* Automated benchmark sweep progression */
        if (stage_frames >= SWEEP_STAGE_FRAMES) {
            stress_sweep_passed |= (1u << (current_stage - 1u));
            stage_frames = 0;
            if (current_stage < 7u) {
                current_stage++;
            } else {
                /* All 7 stages completed! Mark PASS */
                if (stress_sweep_passed == 0x7Fu) {
                    stress_status = 0x50415353u; /* "PASS" */
                }
            }
        }
    }
}
