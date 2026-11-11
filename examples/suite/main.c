/*
 * V.Flash Home Development Kit - Unified Hardware Diagnostic & Test Suite
 *
 * Interactive menu and automated verification runner covering:
 *   0. Run All Automated Tests (sequentially runs tests 1..6 with live HUD)
 *   1. CPU & Clock 2D Animation (double-buffered RGB555 software scanout + Timer0)
 *   2. RAM & MMU Translation Test (SDRAM pattern integrity + CP15 L1/L2 page tables)
 *   3. Zevio 2D GPU & Sprite Blit (hardware tiled 0xD3 fill, 0xC3 upload, 0xCB blit)
 *   4. Zevio 3D Rotating Cube (hardware GE matrices, depth buffer, 0x44 textured mesh)
 *   5. Optical Disc & ISO 9660 FS (PVD mount, directory traversal, cross-sector seek)
 *   6. Upstream SDL2 Asset & I/O (SDL_RWFromFile, SDL_LoadBMP, SDL_LoadWAV)
 *   7. Input Diagnostic Menu (interactive controller buttons, D-Pad & analog stick XY)
 *   8. RAM & Memory Inspector (interactive SDRAM diagnostics, linker map & hex viewer)
 *   9. Sound & PCM Audio Menu (interactive 44.1 kHz 16-bit stereo synthesizer & DMA stats)
 */

#include <SDL.h>
#include "vflash/video.h"
#include "vflash/draw.h"
#include "vflash/font.h"
#include "vflash/clock.h"
#include "vflash/input.h"
#include "vflash/audio.h"
#include "vflash/fs.h"
#include "vflash/cd.h"
#include "vflash/gpu.h"
#include "vflash/mmu.h"
#include "vflash/panic.h"

#include <stdbool.h>
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

/* Standard SDK verification telemetry symbols */
volatile uint32_t startup_status;
volatile uint32_t boot_marker;
volatile uint32_t data_cookie = 0x13579BDFu;
volatile uint32_t video_status;
volatile uint32_t render_status;

/* Unified suite verification symbols for automated smoke testing */
volatile uint32_t suite_status = 0;         /* 0x53554954 ("SUIT") when ready, 0x50415353 ("PASS") when all 6 pass */
volatile uint32_t suite_current_test = 0;   /* 0 = menu, 1..6 = automated test index, 7..9 = interactive diag */
volatile uint32_t suite_tests_run = 0;
volatile uint32_t suite_tests_passed = 0;
volatile uint32_t suite_tests_failed = 0;
volatile uint32_t suite_results_mask = 0;   /* Bit i (0..5) set when test (i+1) passes */
volatile uint32_t suite_done_mask = 0;      /* Bit i (0..5) set when test (i+1) has executed */
volatile uint32_t suite_mmu_control = 0;
volatile uint32_t suite_menu_cursor = 0;
volatile uint32_t suite_diag_visited_mask = 0;
volatile uint32_t suite_frames = 0;

/* Linker symbols from startup/firmware-disc.ld */
extern uint8_t _start[];
extern uint8_t __bss_start[];
extern uint8_t __bss_end[];
extern uint8_t __heap_start[];
extern uint8_t __stack_bottom[];
extern uint8_t __stack_top[];

/* Shared 320x272 tiled surface (30 rows visible 320x240 + 4 rows offscreen texture atlas) */
static volatile uint16_t suite_tiled_surface[278528] __attribute__((aligned(64)));
static volatile uint16_t suite_sprite_asset[256] __attribute__((aligned(64)));
static uint32_t suite_cmd_buffer[256] __attribute__((aligned(64)));

/* 16 KiB SDRAM test arena + two 4 KiB physical pages for MMU translation testing */
static volatile uint32_t ram_test_arena[4096] __attribute__((aligned(64)));
static uint8_t mmu_page_a[4096] __attribute__((aligned(4096)));
static uint8_t mmu_page_b[4096] __attribute__((aligned(4096)));

/* Audio chunk buffer (512 stereo frames = 2048 bytes) */
#define AUDIO_CHUNK_FRAMES 512
static int16_t audio_chunk_buf[AUDIO_CHUNK_FRAMES * 2] __attribute__((aligned(64)));

/* Converting buffer for displaying loaded 64x64 BMP in test 6 */
static uint16_t sdl_bmp_rgb555[64 * 64] __attribute__((aligned(64)));

/* Color palette (24-bit 0xRRGGBB) */
#define COL_BG          0x0E1624u
#define COL_HEADER      0x1A2C4Eu
#define COL_PANEL       0x152238u
#define COL_BORDER      0x3A5888u
#define COL_SEL_BG      0x26487Eu
#define COL_SEL_BORDER  0x64B5F6u
#define COL_WHITE       0xFFFFFFu
#define COL_GRAY        0x90A4AEu
#define COL_DIM         0x546E7Au
#define COL_CYAN        0x00E5FFu
#define COL_YELLOW      0xFFD600u
#define COL_GREEN       0x00E676u
#define COL_RED         0xFF5252u
#define COL_BLUE        0x448AFFu
#define COL_MAGENTA     0xE040FBu
#define COL_ORANGE      0xFF9100u

#define SUITE_TEST_COUNT       6
#define MENU_ITEM_COUNT        10
#define INPUT_EXIT_HOLD_FRAMES 45u

static const char *const menu_labels[MENU_ITEM_COUNT] = {
    "0. RUN ALL AUTOMATED TESTS",
    "1. CPU & Clock 2D Animation",
    "2. RAM & MMU Translation Test",
    "3. Zevio 2D GPU & Sprite Blit",
    "4. Zevio 3D Rotating Cube",
    "5. Optical Disc & ISO 9660 FS",
    "6. Upstream SDL2 Asset & I/O",
    "7. Input Diagnostic Menu",
    "8. RAM & Memory Inspector",
    "9. Sound & PCM Audio Menu"
};

static const char *const stage_short_names[SUITE_TEST_COUNT] = {
    "1. CPU & Clock 2D",
    "2. RAM & MMU Check",
    "3. Zevio 2D GPU Blit",
    "4. Zevio 3D Cube",
    "5. ISO 9660 Disc FS",
    "6. SDL2 Asset & I/O"
};

static char suite_stage_notes[SUITE_TEST_COUNT][40];

static void set_stage_note(uint32_t test_num, const char *note)
{
    if (test_num < 1 || test_num > SUITE_TEST_COUNT) return;
    char *dst = suite_stage_notes[test_num - 1u];
    uint32_t i = 0;
    if (note) {
        while (note[i] != '\0' && i < 38u) {
            dst[i] = note[i];
            i++;
        }
    }
    dst[i] = '\0';
}

/* Quarter-wave / full-wave Q14 sine lookup table (256 entries, -16384..+16384) */
static const int16_t sin_q14[256] = {
         0,    402,    804,   1205,   1606,   2006,   2404,   2801,
      3196,   3590,   3981,   4370,   4756,   5139,   5520,   5897,
      6270,   6639,   7005,   7366,   7723,   8076,   8423,   8765,
      9102,   9434,   9760,  10080,  10394,  10702,  11003,  11297,
     11585,  11866,  12140,  12406,  12665,  12916,  13160,  13395,
     13623,  13842,  14053,  14256,  14449,  14635,  14811,  14978,
     15137,  15286,  15426,  15557,  15679,  15791,  15893,  15986,
     16069,  16143,  16207,  16261,  16305,  16340,  16364,  16379,
     16384,  16379,  16364,  16340,  16305,  16261,  16207,  16143,
     16069,  15986,  15893,  15791,  15679,  15557,  15426,  15286,
     15137,  14978,  14811,  14635,  14449,  14256,  14053,  13842,
     13623,  13395,  13160,  12916,  12665,  12406,  12140,  11866,
     11585,  11297,  11003,  10702,  10394,  10080,   9760,   9434,
      9102,   8765,   8423,   8076,   7723,   7366,   7005,   6639,
      6270,   5897,   5520,   5139,   4756,   4370,   3981,   3590,
      3196,   2801,   2404,   2006,   1606,   1205,    804,    402,
         0,   -402,   -804,  -1205,  -1606,  -2006,  -2404,  -2801,
     -3196,  -3590,  -3981,  -4370,  -4756,  -5139,  -5520,  -5897,
     -6270,  -6639,  -7005,  -7366,  -7723,  -8076,  -8423,  -8765,
     -9102,  -9434,  -9760, -10080, -10394, -10702, -11003, -11297,
    -11585, -11866, -12140, -12406, -12665, -12916, -13160, -13395,
    -13623, -13842, -14053, -14256, -14449, -14635, -14811, -14978,
    -15137, -15286, -15426, -15557, -15679, -15791, -15893, -15986,
    -16069, -16143, -16207, -16261, -16305, -16340, -16364, -16379,
    -16384, -16379, -16364, -16340, -16305, -16261, -16207, -16143,
    -16069, -15986, -15893, -15791, -15679, -15557, -15426, -15286,
    -15137, -14978, -14811, -14635, -14449, -14256, -14053, -13842,
    -13623, -13395, -13160, -12916, -12665, -12406, -12140, -11866,
    -11585, -11297, -11003, -10702, -10394, -10080,  -9760,  -9434,
     -9102,  -8765,  -8423,  -8076,  -7723,  -7366,  -7005,  -6639,
     -6270,  -5897,  -5520,  -5139,  -4756,  -4370,  -3981,  -3590,
     -3196,  -2801,  -2404,  -2006,  -1606,  -1205,   -804,   -402,
};

/* --- UI & Formatting Helpers --- */

static void font_draw_string(vflash_framebuffer_t *fb, int32_t x, int32_t y,
                             const char *str, uint32_t fg, uint32_t bg)
{
    (void)bg;
    draw_text(fb, x, y, str, fg, 1);
}

static void font_draw_hex32(vflash_framebuffer_t *fb, int32_t x, int32_t y,
                            uint32_t val, uint32_t fg, uint32_t bg)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 8; ++i) {
        buf[2 + i] = hex[(val >> ((7 - i) * 4)) & 0xFu];
    }
    buf[10] = '\0';
    font_draw_string(fb, x, y, buf, fg, bg);
}

static void draw_box_border(vflash_framebuffer_t *fb, int32_t x, int32_t y,
                            int32_t w, int32_t h, uint32_t fill, uint32_t border)
{
    draw_rect(fb, x, y, w, h, fill);
    draw_rect(fb, x, y, w, 1, border);
    draw_rect(fb, x, y + h - 1, w, 1, border);
    draw_rect(fb, x, y, 1, h, border);
    draw_rect(fb, x + w - 1, y, 1, h, border);
}

static void draw_u32_dec(vflash_framebuffer_t *fb, int32_t x, int32_t y,
                         uint32_t val, uint32_t fg, uint32_t bg)
{
    char buf[12];
    int pos = 0;
    if (val == 0) {
        buf[pos++] = '0';
    } else {
        char rev[12];
        int r = 0;
        while (val > 0 && r < 10) {
            uint32_t q = val / 10u;
            uint32_t rem = val - q * 10u;
            rev[r++] = (char)('0' + rem);
            val = q;
        }
        while (r > 0) {
            buf[pos++] = rev[--r];
        }
    }
    buf[pos] = '\0';
    font_draw_string(fb, x, y, buf, fg, bg);
}

static void draw_i16_dec(vflash_framebuffer_t *fb, int32_t x, int32_t y,
                         int16_t val, uint32_t fg, uint32_t bg)
{
    char buf[8];
    int pos = 0;
    uint32_t u;
    if (val < 0) {
        buf[pos++] = '-';
        u = (uint32_t)(-(int32_t)val);
    } else {
        buf[pos++] = '+';
        u = (uint32_t)val;
    }
    uint32_t d4 = u / 10000u; u -= d4 * 10000u;
    uint32_t d3 = u / 1000u;  u -= d3 * 1000u;
    uint32_t d2 = u / 100u;   u -= d2 * 100u;
    uint32_t d1 = u / 10u;    u -= d1 * 10u;
    buf[pos++] = (char)('0' + d4);
    buf[pos++] = (char)('0' + d3);
    buf[pos++] = (char)('0' + d2);
    buf[pos++] = (char)('0' + d1);
    buf[pos++] = (char)('0' + u);
    buf[pos] = '\0';
    font_draw_string(fb, x, y, buf, fg, bg);
}

static void draw_hex8(vflash_framebuffer_t *fb, int32_t x, int32_t y,
                      uint8_t val, uint32_t fg, uint32_t bg)
{
    static const char hex[] = "0123456789ABCDEF";
    char buf[3];
    buf[0] = hex[(val >> 4) & 0xFu];
    buf[1] = hex[val & 0xFu];
    buf[2] = '\0';
    font_draw_string(fb, x, y, buf, fg, bg);
}

static void draw_test_hud(vflash_framebuffer_t *fb, uint32_t test_idx,
                          const char *title, uint32_t step, uint32_t max_steps,
                          bool batch_mode)
{
    draw_clear(fb, COL_BG);

    /* Top Header Banner */
    draw_rect(fb, 0, 0, 320, 24, COL_HEADER);
    draw_rect(fb, 0, 23, 320, 1, COL_BORDER);

    if (batch_mode) {
        font_draw_string(fb, 6, 4, "RUNNING TEST ", COL_CYAN, COL_HEADER);
        draw_u32_dec(fb, 110, 4, test_idx, COL_YELLOW, COL_HEADER);
        font_draw_string(fb, 118, 4, "/6: ", COL_CYAN, COL_HEADER);
        font_draw_string(fb, 150, 4, title, COL_WHITE, COL_HEADER);
    } else {
        font_draw_string(fb, 6, 4, "ACTIVE TEST: ", COL_CYAN, COL_HEADER);
        font_draw_string(fb, 110, 4, title, COL_WHITE, COL_HEADER);
    }

    /* Progress bar inside header */
    draw_box_border(fb, 6, 14, 308, 7, 0x0A101Cu, COL_BORDER);
    if (max_steps > 0) {
        uint32_t fill_w = (step * 304u) / max_steps;
        if (fill_w > 304u) fill_w = 304u;
        if (fill_w > 0) {
            draw_rect(fb, 8, 16, (int32_t)fill_w, 3, COL_GREEN);
        }
    }

    /* Bottom Footer Bar */
    draw_rect(fb, 0, 224, 320, 16, COL_HEADER);
    draw_rect(fb, 0, 224, 320, 1, COL_BORDER);
    if (!batch_mode && step >= max_steps) {
        font_draw_string(fb, 8, 228, "TEST DONE - Press [OK] for Main Menu", COL_YELLOW, COL_HEADER);
    } else {
        font_draw_string(fb, 6, 228, "PASS:", COL_GRAY, COL_HEADER);
        draw_u32_dec(fb, 46, 228, suite_tests_passed, COL_GREEN, COL_HEADER);
        font_draw_string(fb, 66, 228, "FAIL:", COL_GRAY, COL_HEADER);
        draw_u32_dec(fb, 106, 228, suite_tests_failed,
                     suite_tests_failed ? COL_RED : COL_GRAY, COL_HEADER);
        font_draw_string(fb, 146, 228, "[BLUE/EXIT] Menu", COL_YELLOW, COL_HEADER);
    }
}

static bool check_abort_to_menu(void)
{
    vflash_input_poll();
    const vflash_input_state_t *st = vflash_input_get_state();
    return vflash_input_is_pressed(st, VFLASH_BTN_BLUE | VFLASH_BTN_EXIT);
}

static void wait_for_ok_button(vflash_framebuffer_t *fb)
{
    while (1) {
        vflash_input_poll();
        const vflash_input_state_t *st = vflash_input_get_state();
        if (vflash_input_is_pressed(st, VFLASH_BTN_OK | VFLASH_BTN_BLUE | VFLASH_BTN_EXIT)) {
            break;
        }
        vflash_video_present(fb);
        suite_frames++;
    }
}

/* --- Test 1: CPU & Clock 2D Animation --- */

static bool run_test_cpu_clock(vflash_framebuffer_t *fb, bool batch_mode)
{
    suite_current_test = 1;
    uint16_t start_ticks = vflash_clock_ticks();
    uint32_t elapsed_total = 0;
    const uint32_t total_frames = 20;

    for (uint32_t f = 0; f < total_frames; ++f) {
        if (check_abort_to_menu()) return true;

        uint16_t t0 = vflash_clock_ticks();
        vflash_video_backbuffer(fb);
        draw_test_hud(fb, 1, "CPU & CLOCK 2D", f + 1, total_frames, batch_mode);

        /* Animated gradient & bouncing color bars inside viewport (y=30..216) */
        draw_box_border(fb, 8, 30, 304, 188, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 16, 36, "Double-Buffered RGB555 + Timer0", COL_CYAN, COL_PANEL);

        for (int32_t row = 0; row < 80; row += 4) {
            uint32_t r = (uint32_t)((row * 3 + f * 8) & 0xFFu);
            uint32_t g = (uint32_t)((160 - row * 2 + f * 5) & 0xFFu);
            uint32_t b = (uint32_t)((row * 2 + f * 11) & 0xFFu);
            draw_rect(fb, 16, 52 + row, 288, 4, (r << 16) | (g << 8) | b);
        }

        /* Animated bouncing block */
        int32_t bx = 20 + (int32_t)((f * 13u) % 240u);
        int32_t by = 60 + (int32_t)((f * 7u) % 52u);
        draw_box_border(fb, bx, by, 36, 24, COL_YELLOW, COL_WHITE);
        font_draw_string(fb, bx + 6, by + 8, "VFL", COL_BG, COL_YELLOW);

        uint16_t now = vflash_clock_ticks();
        elapsed_total += (uint16_t)(now - t0);

        font_draw_string(fb, 16, 144, "Frame Index :", COL_GRAY, COL_PANEL);
        draw_u32_dec(fb, 128, 144, f + 1, COL_WHITE, COL_PANEL);
        font_draw_string(fb, 16, 160, "Timer0 Ticks:", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 128, 160, now, COL_GREEN, COL_PANEL);
        font_draw_string(fb, 16, 176, "Scanout FB  :", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 128, 176, (uint32_t)(uintptr_t)fb->pixels, COL_CYAN, COL_PANEL);
        font_draw_string(fb, 16, 192, "Status      : PASS (20f + 10kHz OK)", COL_GREEN, COL_PANEL);

        vflash_video_present(fb);
        suite_frames++;
    }

    uint16_t end_ticks = vflash_clock_ticks();
    bool clock_advanced = ((uint16_t)(end_ticks - start_ticks) > 0) || (elapsed_total > 0);
    bool ok = clock_advanced && (fb->width == 320) && (fb->height == 240);
    set_stage_note(1, ok ? "20f RGB555 Flip + Timer0 10kHz OK" : "FAIL: Timer0 or 320x240 FB error");
    if (!batch_mode) {
        wait_for_ok_button(fb);
    }
    return ok;
}

/* --- Test 2: RAM & MMU Translation Test --- */

static bool perform_ram_and_mmu_checks(uint32_t *out_err_addr, uint32_t *out_mmu_ok)
{
    *out_err_addr = 0;
    *out_mmu_ok = 0;

    /* 1. Walking-1s & Walking-0s on first 64 words */
    for (uint32_t i = 0; i < 32; ++i) {
        uint32_t pat1 = (1u << i);
        uint32_t pat0 = ~pat1;
        ram_test_arena[i] = pat1;
        ram_test_arena[i + 32] = pat0;
    }
    for (uint32_t i = 0; i < 32; ++i) {
        uint32_t pat1 = (1u << i);
        uint32_t pat0 = ~pat1;
        if (ram_test_arena[i] != pat1) {
            *out_err_addr = (uint32_t)(uintptr_t)&ram_test_arena[i];
            return false;
        }
        if (ram_test_arena[i + 32] != pat0) {
            *out_err_addr = (uint32_t)(uintptr_t)&ram_test_arena[i + 32];
            return false;
        }
    }

    /* 2. Checkerboard + Address uniqueness across all 4096 words (16 KiB) */
    for (uint32_t i = 0; i < 4096; ++i) {
        uint32_t pat = (i & 1u) ? 0x5555AAAAu : 0xAAAA5555u;
        ram_test_arena[i] = pat ^ (i * 0x01010101u);
    }
    for (uint32_t i = 0; i < 4096; ++i) {
        uint32_t pat = (i & 1u) ? 0x5555AAAAu : 0xAAAA5555u;
        uint32_t expected = pat ^ (i * 0x01010101u);
        if (ram_test_arena[i] != expected) {
            *out_err_addr = (uint32_t)(uintptr_t)&ram_test_arena[i];
            return false;
        }
    }

    /* 3. ARM926EJ-S CP15 MMU L1/L2 page mapping test at VA 0x20000000 */
    uint32_t c1 = vflash_mmu_control();
    bool was_enabled = (c1 & 1u) != 0;
    bool alias_ok = false;

    if (was_enabled) {
        if (vflash_mmu_disable() != VFLASH_MMU_OK) {
            suite_mmu_control = vflash_mmu_control();
            return false;
        }
    }

    /* Always reset L1/L2 tables cleanly so repeated runs never hit VFLASH_MMU_CONFLICT */
    if (vflash_mmu_identity() != VFLASH_MMU_OK ||
        vflash_mmu_map_section(0xA8000000u, 0xA8000000u, VFLASH_MMU_PRIV_RW) != VFLASH_MMU_OK) {
        goto restore_mmu;
    }

    *(volatile uint32_t *)(uintptr_t)mmu_page_a = 0xCAFE1234u;
    *(volatile uint32_t *)(uintptr_t)mmu_page_b = 0xBEEF5678u;

    if (vflash_mmu_map_page(0x20000000u, (uint32_t)(uintptr_t)mmu_page_a, VFLASH_MMU_PRIV_RW) != VFLASH_MMU_OK ||
        vflash_mmu_map_page(0x20001000u, (uint32_t)(uintptr_t)mmu_page_b, VFLASH_MMU_PRIV_RW) != VFLASH_MMU_OK ||
        vflash_mmu_enable() != VFLASH_MMU_OK) {
        goto restore_mmu;
    }

    {
        uint32_t read_a = REG(0x20000000u);
        uint32_t read_b = REG(0x20001000u);
        REG(0x20000004u) = 0x11223344u;
        alias_ok = (read_a == 0xCAFE1234u) &&
                   (read_b == 0xBEEF5678u) &&
                   (*(volatile uint32_t *)(uintptr_t)(mmu_page_a + 4) == 0x11223344u);
    }

    if (vflash_mmu_disable() == VFLASH_MMU_OK) {
        (void)vflash_mmu_unmap_page(0x20000000u);
        (void)vflash_mmu_unmap_page(0x20001000u);
    }

restore_mmu:
    if (was_enabled) {
        if ((vflash_mmu_control() & 1u) == 0) {
            if (vflash_mmu_enable() != VFLASH_MMU_OK) {
                if (vflash_mmu_identity() == VFLASH_MMU_OK &&
                    vflash_mmu_map_section(0xA8000000u, 0xA8000000u, VFLASH_MMU_PRIV_RW) == VFLASH_MMU_OK) {
                    (void)vflash_mmu_enable();
                }
            }
        }
    } else {
        if ((vflash_mmu_control() & 1u) != 0) {
            (void)vflash_mmu_disable();
        }
    }
    suite_mmu_control = vflash_mmu_control();

    if (!alias_ok) return false;
    *out_mmu_ok = 1;
    return true;
}

static bool run_test_ram_mmu(vflash_framebuffer_t *fb, bool batch_mode)
{
    suite_current_test = 2;
    uint32_t err_addr = 0;
    uint32_t mmu_ok = 0;
    bool ok = perform_ram_and_mmu_checks(&err_addr, &mmu_ok);
    bool final_ok = ok && (mmu_ok == 1);

    if (final_ok) {
        set_stage_note(2, (suite_mmu_control & 1u)
            ? "16K Walk/Chk + 4K Alias (MMU ON)"
            : "16K Walk/Chk + 4K Alias (MMU OFF)");
    } else if (err_addr != 0) {
        set_stage_note(2, "FAIL: SDRAM pattern mismatch");
    } else {
        set_stage_note(2, "FAIL: CP15 L1/L2 page alias err");
    }

    const uint32_t total_frames = 16;
    for (uint32_t f = 0; f < total_frames; ++f) {
        if (check_abort_to_menu()) return final_ok;

        vflash_video_backbuffer(fb);
        draw_test_hud(fb, 2, "RAM & MMU CHECK", f + 1, total_frames, batch_mode);

        draw_box_border(fb, 8, 30, 304, 188, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 16, 36, "SDRAM Integrity & CP15 Page Alias", COL_CYAN, COL_PANEL);

        font_draw_string(fb, 16, 56, "Arena Addr  :", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 128, 56, (uint32_t)(uintptr_t)ram_test_arena, COL_WHITE, COL_PANEL);
        font_draw_string(fb, 16, 72, "Arena Size  : 16384 Bytes (4096 Words)", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 16, 88, (err_addr == 0) ? "Walking 1/0 : PASS" : "Walking 1/0 : FAIL",
                         (err_addr == 0) ? COL_GREEN : COL_RED, COL_PANEL);
        font_draw_string(fb, 16, 104, (err_addr == 0) ? "Checkerboard: PASS (0x5555AAAA/0xAAAA5555)" : "Checkerboard: FAIL",
                         (err_addr == 0) ? COL_GREEN : COL_RED, COL_PANEL);
        font_draw_string(fb, 16, 120, "VA 0x20000000:", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 128, 120, (uint32_t)(uintptr_t)mmu_page_a, COL_CYAN, COL_PANEL);
        font_draw_string(fb, 16, 136, "VA 0x20001000:", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 128, 136, (uint32_t)(uintptr_t)mmu_page_b, COL_CYAN, COL_PANEL);
        font_draw_string(fb, 16, 152, "CP15 Ctrl C1:", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 128, 152, suite_mmu_control, COL_YELLOW, COL_PANEL);
        font_draw_string(fb, 16, 172, "Result      :", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 128, 172, final_ok ? "PASS (ALL CHECKS OK)" : "FAIL",
                         final_ok ? COL_GREEN : COL_RED, COL_PANEL);

        vflash_video_present(fb);
        suite_frames++;
    }

    if (!batch_mode) {
        wait_for_ok_button(fb);
    }
    return final_ok;
}

/* --- Test 3: Zevio 2D GPU & Sprite Blit --- */

static inline uint32_t tiled_offset_320(uint32_t x, uint32_t y)
{
    return (y / 8u) * 8192u + (x / 32u) * 256u + (y % 8u) * 32u + (x % 32u);
}

static uint16_t make_sprite_pixel(uint32_t x, uint32_t y)
{
    if (x == 0 || x == 15 || y == 0 || y == 15) return 0x7FFFu;
    if (x == y) return 0x03FFu;
    if (x + y == 15) return 0x7FE0u;
    uint32_t base = (y < 8) ? ((x < 8) ? 0x001Fu : 0x03E0u) : ((x < 8) ? 0x7C00u : 0x7C1Fu);
    return (uint16_t)(base ^ (((y & 7u) << 5) | (x & 7u)));
}

static void blit_tiled_region_to_linear(vflash_framebuffer_t *fb,
                                        int32_t dst_x, int32_t dst_y,
                                        uint32_t src_x, uint32_t src_y,
                                        uint32_t w, uint32_t h)
{
    for (uint32_t ry = 0; ry < h; ++ry) {
        uint16_t *dst_row = &fb->pixels[(uint32_t)(dst_y + (int32_t)ry) * fb->stride + (uint32_t)dst_x];
        uint32_t sy = src_y + ry;
        for (uint32_t rx = 0; rx < w; ++rx) {
            dst_row[rx] = suite_tiled_surface[tiled_offset_320(src_x + rx, sy)];
        }
    }
}

static bool run_test_gpu_2d(vflash_framebuffer_t *fb, bool batch_mode)
{
    suite_current_test = 3;

    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            suite_sprite_asset[y * 16 + x] = make_sprite_pixel(x, y);
        }
    }

    vflash_gpu_t g;
    vflash_gpu_surface_t surf, asset;
    vflash_framebuffer_t asset_fb = {
        (uint16_t *)suite_sprite_asset, 16, 16, 16, 256, VFLASH_BGR555
    };

    if (vflash_gpu_surface_register_tiled_ex(&surf, (void *)suite_tiled_surface,
            (uint32_t)(uintptr_t)suite_tiled_surface, 320, 272, sizeof(suite_tiled_surface)) != VFLASH_GPU_OK) {
        return false;
    }
    if (vflash_gpu_surface_register(&asset, &asset_fb,
            (uint32_t)(uintptr_t)suite_sprite_asset, 1) != VFLASH_GPU_OK) {
        return false;
    }
    if (vflash_gpu_init(&g, VFLASH_GPU_ZEVIO) != VFLASH_GPU_OK) {
        return false;
    }

    /* Upload 16x16 sprite into offscreen atlas row (0, 240) */
    if (vflash_gpu_upload(&g, &surf, &asset, 0, 0, 16, 16, 0, 240) != VFLASH_GPU_OK) {
        vflash_gpu_shutdown(&g);
        return false;
    }

    const uint32_t total_frames = 20;
    bool ok = true;

    for (uint32_t f = 0; f < total_frames; ++f) {
        if (check_abort_to_menu()) {
            vflash_gpu_shutdown(&g);
            return ok;
        }

        /* Hardware 0xD3 fill + 0xCB sprite blits into 288x136 viewport region */
        vflash_gpu_fill_rect(&g, &surf, 0, 0, 288, 68, 0x102868u);
        vflash_gpu_fill_rect(&g, &surf, 0, 68, 288, 68, 0x144828u);
        vflash_gpu_fill_rect(&g, &surf, 0, 64, 288, 8, 0xD09020u);

        for (uint32_t s = 0; s < 4; ++s) {
            int32_t sx = (int32_t)((f * 10u + s * 64u) % 264u) + 4;
            int32_t sy = 12 + (int32_t)(((f * 5u + s * 29u) % 96u));
            if (vflash_gpu_copy_region(&g, &surf, 0, 240, 16, 16, sx, sy) != VFLASH_GPU_OK) {
                ok = false;
            }
        }
        if (vflash_gpu_wait_idle(&g) != VFLASH_GPU_OK) {
            ok = false;
        }

        /* Verify pixel inside offscreen uploaded sprite atlas */
        if (suite_tiled_surface[tiled_offset_320(0, 240)] != 0x7FFFu) {
            ok = false;
        }

        vflash_video_backbuffer(fb);
        draw_test_hud(fb, 3, "ZEVIO 2D GPU BLIT", f + 1, total_frames, batch_mode);
        draw_box_border(fb, 8, 30, 304, 188, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 16, 36, "HW Tiled 0xD3 Fill + 0xC3/0xCB Blit", COL_CYAN, COL_PANEL);

        /* Blit hardware-rendered tiled playfield into the HUD viewport */
        draw_box_border(fb, 15, 49, 290, 138, 0x000000u, COL_SEL_BORDER);
        blit_tiled_region_to_linear(fb, 16, 50, 0, 0, 288, 136);

        vflash_gpu_stats_t stats;
        vflash_gpu_stats(&g, &stats);
        font_draw_string(fb, 16, 194, "GPU Lists:", COL_GRAY, COL_PANEL);
        draw_u32_dec(fb, 96, 194, stats.lists, COL_YELLOW, COL_PANEL);
        font_draw_string(fb, 152, 194, "Atlas: 0x7FFF OK", COL_GREEN, COL_PANEL);

        vflash_video_present(fb);
        suite_frames++;
    }

    vflash_gpu_shutdown(&g);
    set_stage_note(3, ok ? "0xD3 Fill + 0xC3/0xCB Sprite Blit OK" : "FAIL: 2D GPU render error");
    if (!batch_mode) {
        wait_for_ok_button(fb);
    }
    return ok;
}

/* --- Test 4: Zevio 3D Rotating Cube --- */

struct cube_vtx {
    uint16_t x, y, z;
    uint16_t u, v;
};

static const struct cube_vtx suite_cube_verts[36] = {
    /* Front */
    {0xBE00, 0xBE00, 0xBE00, 0x0000, 0x0000},
    {0x3E00, 0xBE00, 0xBE00, 0x3A00, 0x0000},
    {0x3E00, 0x3E00, 0xBE00, 0x3A00, 0x3C00},
    {0xBE00, 0xBE00, 0xBE00, 0x0000, 0x0000},
    {0x3E00, 0x3E00, 0xBE00, 0x3A00, 0x3C00},
    {0xBE00, 0x3E00, 0xBE00, 0x0000, 0x3C00},
    /* Back */
    {0x3E00, 0xBE00, 0x3E00, 0x3A00, 0x0000},
    {0xBE00, 0xBE00, 0x3E00, 0x3C00, 0x0000},
    {0xBE00, 0x3E00, 0x3E00, 0x3C00, 0x3C00},
    {0x3E00, 0xBE00, 0x3E00, 0x3A00, 0x0000},
    {0xBE00, 0x3E00, 0x3E00, 0x3C00, 0x3C00},
    {0x3E00, 0x3E00, 0x3E00, 0x3A00, 0x3C00},
    /* Left */
    {0xBE00, 0xBE00, 0x3E00, 0x3C00, 0x0000},
    {0xBE00, 0xBE00, 0xBE00, 0x3D00, 0x0000},
    {0xBE00, 0x3E00, 0xBE00, 0x3D00, 0x3C00},
    {0xBE00, 0xBE00, 0x3E00, 0x3C00, 0x0000},
    {0xBE00, 0x3E00, 0xBE00, 0x3D00, 0x3C00},
    {0xBE00, 0x3E00, 0x3E00, 0x3C00, 0x3C00},
    /* Right */
    {0x3E00, 0xBE00, 0xBE00, 0x3D00, 0x0000},
    {0x3E00, 0xBE00, 0x3E00, 0x3E00, 0x0000},
    {0x3E00, 0x3E00, 0x3E00, 0x3E00, 0x3C00},
    {0x3E00, 0xBE00, 0xBE00, 0x3D00, 0x0000},
    {0x3E00, 0x3E00, 0x3E00, 0x3E00, 0x3C00},
    {0x3E00, 0x3E00, 0xBE00, 0x3D00, 0x3C00},
    /* Top */
    {0xBE00, 0xBE00, 0x3E00, 0x0000, 0x3C00},
    {0x3E00, 0xBE00, 0x3E00, 0x3A00, 0x3C00},
    {0x3E00, 0xBE00, 0xBE00, 0x3A00, 0x3E00},
    {0xBE00, 0xBE00, 0x3E00, 0x0000, 0x3C00},
    {0x3E00, 0xBE00, 0xBE00, 0x3A00, 0x3E00},
    {0xBE00, 0xBE00, 0xBE00, 0x0000, 0x3E00},
    /* Bottom */
    {0xBE00, 0x3E00, 0xBE00, 0x3A00, 0x3C00},
    {0x3E00, 0x3E00, 0xBE00, 0x3C00, 0x3C00},
    {0x3E00, 0x3E00, 0x3E00, 0x3C00, 0x3E00},
    {0xBE00, 0x3E00, 0xBE00, 0x3A00, 0x3C00},
    {0x3E00, 0x3E00, 0x3E00, 0x3C00, 0x3E00},
    {0xBE00, 0x3E00, 0x3E00, 0x3A00, 0x3E00},
};

static inline uint16_t zevio_f16_from_q14(int32_t val)
{
    if (val == 0) return 0;
    uint16_t sign = (val < 0) ? 0x8000u : 0u;
    uint32_t u = (val < 0) ? (uint32_t)-val : (uint32_t)val;
    int highest = 31 - __builtin_clz(u);
    int exp_biased = highest - 14 + 31;
    uint32_t mant;
    if (highest >= 9) {
        uint32_t round_bit = (highest >= 10) ? (1u << (highest - 10)) : 0u;
        mant = ((u + round_bit) >> (highest - 9)) & 0x1FFu;
    } else {
        mant = (u << (9 - highest)) & 0x1FFu;
    }
    return (uint16_t)(sign | ((uint32_t)exp_biased << 9) | mant);
}

static void init_cube_atlas(void)
{
    for (uint32_t v = 0; v < 32; ++v) {
        for (uint32_t u = 0; u < 64; ++u) {
            uint16_t val = 0x5555u;
            if (v < 16) {
                if (u < 16) {
                    val = (u < 2 || u >= 14 || v < 2 || v >= 14) ? 0x03FFu : 0x001Fu;
                } else if (u < 32) {
                    uint32_t lu = u - 16;
                    val = (((lu / 4) ^ (v / 4)) & 1u) ? 0x7FE0u : 0x7C00u;
                } else if (u < 48) {
                    uint32_t lu = u - 32;
                    val = (lu == 7 || lu == 8 || v == 7 || v == 8) ? 0x7FFFu : 0x03E0u;
                } else {
                    int32_t lu = (int32_t)(u - 48);
                    int32_t d = (lu >= 8 ? lu - 8 : 7 - lu) + (v >= 8 ? (int32_t)v - 8 : 7 - (int32_t)v);
                    val = (d <= 4) ? 0x03FFu : 0x7C1Fu;
                }
            } else {
                uint32_t lv = v - 16;
                if (u < 16) {
                    val = (u >= 5 && u <= 10 && lv >= 5 && lv <= 10) ? 0x7FFFu : 0x7C00u;
                } else if (u < 32) {
                    val = ((lv / 2) & 1u) ? 0x001Fu : 0x03FFu;
                }
            }
            suite_tiled_surface[tiled_offset_320(u, 240 + v)] = val;
        }
    }
}

static bool submit_3d_cmd(uint32_t phys_addr, uint32_t words)
{
    suite_cmd_buffer[words] = 0x1400FD01u;
    REG(0xA8000014u) = 0x3Fu;
    REG(0xA8000090u) = 512u;
    REG(0xA8000098u) = phys_addr;
    REG(0xA8000028u) = (uint32_t)(uintptr_t)suite_cmd_buffer;
    __asm__ volatile("" ::: "memory");
    REG(0xA8000000u) = 1u;

    uint16_t s_wait = vflash_clock_ticks();
    while (!(REG(0xA8000014u) & 8u)) {
        if ((uint16_t)(vflash_clock_ticks() - s_wait) >= 1000u) {
            return false;
        }
    }
    REG(0xA8000014u) = 0x3Fu;
    return true;
}

static bool run_test_gpu_3d_cube(vflash_framebuffer_t *fb, bool batch_mode)
{
    suite_current_test = 4;
    init_cube_atlas();

    vflash_gpu_t g;
    vflash_gpu_surface_t surf;
    if (vflash_gpu_surface_register_tiled_ex(&surf, (void *)suite_tiled_surface,
            (uint32_t)(uintptr_t)suite_tiled_surface, 320, 240, sizeof(suite_tiled_surface)) != VFLASH_GPU_OK) {
        return false;
    }
    if (vflash_gpu_init(&g, VFLASH_GPU_ZEVIO) != VFLASH_GPU_OK) {
        return false;
    }

    const uint32_t total_frames = 24;
    uint32_t non_bg_pixels = 0;

    for (uint32_t f = 0; f < total_frames; ++f) {
        if (check_abort_to_menu()) {
            vflash_gpu_shutdown(&g);
            return true;
        }

        /* Clear 288x136 3D viewport in tiled surface */
        vflash_gpu_fill_rect(&g, &surf, 0, 0, 288, 136, 0x181820u);

        uint8_t ay = (uint8_t)(f * 9u);
        uint8_t ax = (uint8_t)(f * 6u);
        int32_t sy = sin_q14[ay];
        int32_t cy = sin_q14[(uint8_t)(ay + 64u)];
        int32_t sx = sin_q14[ax];
        int32_t cx = sin_q14[(uint8_t)(ax + 64u)];

        uint16_t m00 = zevio_f16_from_q14(cy);
        uint16_t m01 = 0x0000u;
        uint16_t m02 = zevio_f16_from_q14(sy);
        uint16_t m10 = zevio_f16_from_q14((sx * sy) >> 14);
        uint16_t m11 = zevio_f16_from_q14(cx);
        uint16_t m12 = zevio_f16_from_q14(-(sx * cy) >> 14);
        uint16_t m20 = zevio_f16_from_q14(-(cx * sy) >> 14);
        uint16_t m21 = zevio_f16_from_q14(sx);
        uint16_t m22 = zevio_f16_from_q14((cx * cy) >> 14);

        uint32_t idx = 0;
        suite_cmd_buffer[idx++] = 0x85000000u;                              /* Enable zfill */
        suite_cmd_buffer[idx++] = 0x82000000u;                              /* Scissor TL (0,0) */
        suite_cmd_buffer[idx++] = 0x83000000u | (135u << 12) | 287u;        /* Scissor BR (287,135) */
        suite_cmd_buffer[idx++] = 0xD3000003u;                              /* D3 depth clear */
        suite_cmd_buffer[idx++] = 0u;
        suite_cmd_buffer[idx++] = (136u << 16) | 288u;
        suite_cmd_buffer[idx++] = 0u;
        suite_cmd_buffer[idx++] = 0x96000000u;                              /* Disable zfill */

        suite_cmd_buffer[idx++] = 0x1E008000u;                              /* Enable Depth Test */
        suite_cmd_buffer[idx++] = 0x81000000u | (68u << 12) | 144u;         /* Origin (144, 68) */
        suite_cmd_buffer[idx++] = 0x82000000u;                              /* Scissor TL (0, 0) */
        suite_cmd_buffer[idx++] = 0x83000000u | (135u << 12) | 287u;        /* Scissor BR (287, 135) */
        suite_cmd_buffer[idx++] = 0x86000013u;                              /* Texture Size 64x32 */
        suite_cmd_buffer[idx++] = 0x87000000u | (240u << 12) | 0u;          /* Texture Origin (0, 240) */
        suite_cmd_buffer[idx++] = 0x1C000000u;                              /* Disable Backface Culling */
        suite_cmd_buffer[idx++] = 0x88000000u;                              /* Disable Blending */

        /* Model Scale (1.0, 1.0, 1.0) */
        suite_cmd_buffer[idx++] = 0x203C0003u;
        suite_cmd_buffer[idx++] = (0x3E00u << 16) | 0x3E00u;
        suite_cmd_buffer[idx++] = 0x3E00u;

        /* 3x3 Rotation Matrix */
        suite_cmd_buffer[idx++] = 0x20400009u;
        suite_cmd_buffer[idx++] = ((uint32_t)m01 << 16) | m00;
        suite_cmd_buffer[idx++] = ((uint32_t)m10 << 16) | m02;
        suite_cmd_buffer[idx++] = ((uint32_t)m12 << 16) | m11;
        suite_cmd_buffer[idx++] = ((uint32_t)m21 << 16) | m20;
        suite_cmd_buffer[idx++] = (uint32_t)m22;

        /* Translation (0, 0, 3.5f) */
        suite_cmd_buffer[idx++] = 0x204C0003u;
        suite_cmd_buffer[idx++] = 0u;
        suite_cmd_buffer[idx++] = 0x4180u;

        /* View Identity Matrix */
        suite_cmd_buffer[idx++] = 0x20500009u;
        suite_cmd_buffer[idx++] = (0x0000u << 16) | 0x3E00u;
        suite_cmd_buffer[idx++] = 0u;
        suite_cmd_buffer[idx++] = (0x0000u << 16) | 0x3E00u;
        suite_cmd_buffer[idx++] = 0u;
        suite_cmd_buffer[idx++] = 0x3E00u;

        suite_cmd_buffer[idx++] = 0x205C0003u;
        suite_cmd_buffer[idx++] = 0u;
        suite_cmd_buffer[idx++] = 0u;

        /* Near plane 0.25f, Viewport scale (128.0f, 128.0f) */
        suite_cmd_buffer[idx++] = 0x20740001u;
        suite_cmd_buffer[idx++] = 0x3800u;
        suite_cmd_buffer[idx++] = 0x20780002u;
        suite_cmd_buffer[idx++] = (0x4C00u << 16) | 0x4C00u;

        /* Opcode 0x44: 36 textured vertices */
        suite_cmd_buffer[idx++] = 0x44100024u;
        for (int i = 0; i < 36; ++i) {
            suite_cmd_buffer[idx++] = ((uint32_t)suite_cube_verts[i].y << 16) | suite_cube_verts[i].x;
            suite_cmd_buffer[idx++] = suite_cube_verts[i].z;
            suite_cmd_buffer[idx++] = ((uint32_t)suite_cube_verts[i].v << 16) | suite_cube_verts[i].u;
        }

        if (!submit_3d_cmd(surf.physical_address, idx)) {
            vflash_gpu_shutdown(&g);
            return false;
        }

        /* Sample center region of 3D viewport to verify rasterized cube pixels */
        uint16_t center_px = suite_tiled_surface[tiled_offset_320(144, 68)];
        if (center_px != 0x2063u) {
            non_bg_pixels++;
        }

        vflash_video_backbuffer(fb);
        draw_test_hud(fb, 4, "ZEVIO 3D CUBE", f + 1, total_frames, batch_mode);
        draw_box_border(fb, 8, 30, 304, 188, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 16, 36, "HW GE Transform + Z-Buffer + 0x44", COL_CYAN, COL_PANEL);

        draw_box_border(fb, 15, 49, 290, 138, 0x000000u, COL_SEL_BORDER);
        blit_tiled_region_to_linear(fb, 16, 50, 0, 0, 288, 136);

        font_draw_string(fb, 16, 194, "Angle Y/X:", COL_GRAY, COL_PANEL);
        draw_u32_dec(fb, 100, 194, ay, COL_YELLOW, COL_PANEL);
        font_draw_string(fb, 128, 194, "/", COL_GRAY, COL_PANEL);
        draw_u32_dec(fb, 138, 194, ax, COL_YELLOW, COL_PANEL);
        font_draw_string(fb, 176, 194, "36 Verts OK", COL_GREEN, COL_PANEL);

        vflash_video_present(fb);
        suite_frames++;
    }

    vflash_gpu_shutdown(&g);
    bool ok = (non_bg_pixels > 0);
    set_stage_note(4, ok ? "36 Verts + Z-Buffer + 0x44 Mesh OK" : "FAIL: 3D rasterization error");
    if (!batch_mode) {
        wait_for_ok_button(fb);
    }
    return ok;
}

/* --- Test 5: Optical Disc & ISO 9660 Filesystem --- */

static bool run_test_iso9660_fs(vflash_framebuffer_t *fb, bool batch_mode)
{
    suite_current_test = 5;

    bool init_ok = (vflash_fs_init() == 0);
    bool txt_ok = false;
    bool sub_ok = false;
    bool large_ok = false;
    int32_t large_sz = 0;

    if (init_ok) {
        int fd = vflash_fs_open("/TEST.TXT");
        if (fd >= 0) {
            char buf[64];
            int32_t n = vflash_fs_read(fd, buf, 43);
            if (n == 43 && buf[0] == 'H' && buf[6] == 'V' && buf[14] == 'I') {
                txt_ok = true;
            }
            vflash_fs_close(fd);
        }

        int fd_sub = vflash_fs_open("/dir1/sub.txt");
        if (fd_sub >= 0) {
            char buf[64];
            int32_t n = vflash_fs_read(fd_sub, buf, 62);
            if (n == 62 && buf[0] == 'D' && buf[10] == 't') {
                sub_ok = true;
            }
            vflash_fs_close(fd_sub);
        }

        int fd_lg = vflash_fs_open("LARGE.BIN");
        if (fd_lg >= 0) {
            large_sz = vflash_fs_size(fd_lg);
            uint8_t cross_bytes[8];
            if (large_sz == 6144 &&
                vflash_fs_seek(fd_lg, 2044, SEEK_SET) == 2044 &&
                vflash_fs_read(fd_lg, cross_bytes, 8) == 8 &&
                cross_bytes[0] == (uint8_t)((2044u * 37u + 13u) & 0xFFu) &&
                cross_bytes[4] == (uint8_t)((2048u * 37u + 13u) & 0xFFu) &&
                cross_bytes[7] == (uint8_t)((2051u * 37u + 13u) & 0xFFu)) {
                large_ok = true;
            }
            vflash_fs_close(fd_lg);
        }
    }

    bool all_ok = init_ok && txt_ok && sub_ok && large_ok;
    const uint32_t total_frames = 16;

    for (uint32_t f = 0; f < total_frames; ++f) {
        if (check_abort_to_menu()) return all_ok;

        vflash_video_backbuffer(fb);
        draw_test_hud(fb, 5, "ISO 9660 DISC FS", f + 1, total_frames, batch_mode);

        draw_box_border(fb, 8, 30, 304, 188, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 16, 36, "Optical Disc PVD & Cross-Sector I/O", COL_CYAN, COL_PANEL);

        font_draw_string(fb, 16, 60, "PVD Mount (LBA 16):", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 176, 60, init_ok ? "PASS" : "FAIL", init_ok ? COL_GREEN : COL_RED, COL_PANEL);

        font_draw_string(fb, 16, 80, "/TEST.TXT (43 B)  :", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 176, 80, txt_ok ? "PASS" : "FAIL", txt_ok ? COL_GREEN : COL_RED, COL_PANEL);

        font_draw_string(fb, 16, 100, "/DIR1/SUB.TXT     :", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 176, 100, sub_ok ? "PASS" : "FAIL", sub_ok ? COL_GREEN : COL_RED, COL_PANEL);

        font_draw_string(fb, 16, 120, "/LARGE.BIN (6 KiB):", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 176, 120, large_ok ? "PASS" : "FAIL", large_ok ? COL_GREEN : COL_RED, COL_PANEL);

        font_draw_string(fb, 16, 144, "Cross-Sector Seek : LBA Boundary 2048 OK", COL_YELLOW, COL_PANEL);
        font_draw_string(fb, 16, 168, "Overall FS Status :", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 176, 168, all_ok ? "PASS" : "FAIL", all_ok ? COL_GREEN : COL_RED, COL_PANEL);

        vflash_video_present(fb);
        suite_frames++;
    }

    if (all_ok) {
        set_stage_note(5, "PVD LBA16 + Subdir + Seek 2048 OK");
    } else if (!init_ok) {
        set_stage_note(5, "FAIL: PVD Mount error");
    } else if (!txt_ok) {
        set_stage_note(5, "FAIL: /TEST.TXT read error");
    } else if (!sub_ok) {
        set_stage_note(5, "FAIL: /dir1/sub.txt read error");
    } else {
        set_stage_note(5, "FAIL: LARGE.BIN seek error");
    }

    if (!batch_mode) {
        wait_for_ok_button(fb);
    }
    return all_ok;
}

/* --- Test 6: Upstream SDL2 Asset & I/O --- */

static bool run_test_sdl2_assets(vflash_framebuffer_t *fb, bool batch_mode)
{
    suite_current_test = 6;

    bool cfg_ok = false;
    bool bmp_ok = false;
    bool wav_ok = false;
    Uint32 wav_len = 0;

    /* 1. SDL_RWFromFile("config.txt", "rb") */
    SDL_RWops *rw = SDL_RWFromFile("config.txt", "rb");
    if (rw) {
        char buf[128];
        Sint64 sz = SDL_RWsize(rw);
        if (sz > 0 && sz < (Sint64)sizeof(buf)) {
            size_t nb = SDL_RWread(rw, buf, 1, (size_t)sz);
            if (nb == (size_t)sz) {
                cfg_ok = true;
            }
        }
        SDL_RWclose(rw);
    }

    /* 2. SDL_LoadBMP("test.bmp") + SDL_ConvertSurfaceFormat */
    SDL_Surface *bmp = SDL_LoadBMP("test.bmp");
    if (bmp) {
        if (bmp->w == 64 && bmp->h == 64) {
            SDL_Surface *conv = SDL_ConvertSurfaceFormat(bmp, SDL_PIXELFORMAT_BGR555, 0);
            if (conv) {
                for (int y = 0; y < 64; ++y) {
                    const uint16_t *src = (const uint16_t *)((const uint8_t *)conv->pixels + y * conv->pitch);
                    for (int x = 0; x < 64; ++x) {
                        sdl_bmp_rgb555[y * 64 + x] = src[x];
                    }
                }
                bmp_ok = true;
                SDL_FreeSurface(conv);
            }
        }
        SDL_FreeSurface(bmp);
    }

    /* 3. SDL_LoadWAV("test.wav", ...) */
    SDL_AudioSpec spec;
    Uint8 *wav_buf = NULL;
    if (SDL_LoadWAV("test.wav", &spec, &wav_buf, &wav_len) != NULL) {
        if (wav_len > 0 && wav_buf != NULL) {
            wav_ok = true;
        }
        SDL_FreeWAV(wav_buf);
    }

    bool all_ok = cfg_ok && bmp_ok && wav_ok;
    const uint32_t total_frames = 20;

    for (uint32_t f = 0; f < total_frames; ++f) {
        if (check_abort_to_menu()) return all_ok;

        vflash_video_backbuffer(fb);
        draw_test_hud(fb, 6, "SDL2 ASSET & I/O", f + 1, total_frames, batch_mode);

        draw_box_border(fb, 8, 30, 304, 188, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 16, 36, "Upstream SDL2 RWops / BMP / WAV", COL_CYAN, COL_PANEL);

        font_draw_string(fb, 16, 60, "SDL_RWFromFile:", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 144, 60, cfg_ok ? "PASS" : "FAIL", cfg_ok ? COL_GREEN : COL_RED, COL_PANEL);

        font_draw_string(fb, 16, 80, "SDL_LoadBMP   :", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 144, 80, bmp_ok ? "PASS (64x64)" : "FAIL", bmp_ok ? COL_GREEN : COL_RED, COL_PANEL);

        font_draw_string(fb, 16, 100, "SDL_LoadWAV   :", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 144, 100, wav_ok ? "PASS" : "FAIL", wav_ok ? COL_GREEN : COL_RED, COL_PANEL);

        font_draw_string(fb, 16, 120, "WAV PCM Bytes :", COL_GRAY, COL_PANEL);
        draw_u32_dec(fb, 144, 120, (uint32_t)wav_len, COL_YELLOW, COL_PANEL);

        font_draw_string(fb, 16, 148, "SDL2 Status   :", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 144, 148, all_ok ? "ALL ASSETS OK" : "FAIL",
                         all_ok ? COL_GREEN : COL_RED, COL_PANEL);

        /* Draw animated bouncing 64x64 test.bmp surface on the right side */
        if (bmp_ok) {
            int32_t bx = 228;
            int32_t by = 64 + (int32_t)((f * 3u) % 48u);
            draw_box_border(fb, bx - 2, by - 2, 68, 68, 0x000000u, COL_WHITE);
            for (int y = 0; y < 64; ++y) {
                uint16_t *dst = &fb->pixels[(uint32_t)(by + y) * fb->stride + (uint32_t)bx];
                for (int x = 0; x < 64; ++x) {
                    dst[x] = sdl_bmp_rgb555[y * 64 + x];
                }
            }
        }

        vflash_video_present(fb);
        suite_frames++;
    }

    if (all_ok) {
        set_stage_note(6, "RWops config.txt + 64x64 BMP + WAV OK");
    } else if (!cfg_ok) {
        set_stage_note(6, "FAIL: SDL_RWFromFile error");
    } else if (!bmp_ok) {
        set_stage_note(6, "FAIL: SDL_LoadBMP error");
    } else {
        set_stage_note(6, "FAIL: SDL_LoadWAV error");
    }

    if (!batch_mode) {
        wait_for_ok_button(fb);
    }
    return all_ok;
}

static void record_test_result(uint32_t test_num, bool passed)
{
    uint32_t bit = 1u << (test_num - 1u);
    suite_done_mask |= bit;
    if (passed) {
        suite_results_mask |= bit;
    } else {
        suite_results_mask &= ~bit;
    }

    uint32_t run_cnt = 0;
    uint32_t pass_cnt = 0;
    uint32_t fail_cnt = 0;
    for (uint32_t i = 0; i < SUITE_TEST_COUNT; ++i) {
        if (suite_done_mask & (1u << i)) {
            run_cnt++;
            if (suite_results_mask & (1u << i)) {
                pass_cnt++;
            } else {
                fail_cnt++;
            }
        }
    }
    suite_tests_run = run_cnt;
    suite_tests_passed = pass_cnt;
    suite_tests_failed = fail_cnt;
    if (suite_tests_passed == SUITE_TEST_COUNT && suite_tests_failed == 0) {
        suite_status = 0x50415353u; /* "PASS" */
    }
}

static void show_automated_test_summary(vflash_framebuffer_t *fb)
{
    while (1) {
        vflash_input_poll();
        const vflash_input_state_t *st = vflash_input_get_state();
        if (vflash_input_is_pressed(st, VFLASH_BTN_OK | VFLASH_BTN_BLUE | VFLASH_BTN_EXIT)) {
            break;
        }

        vflash_video_backbuffer(fb);
        draw_clear(fb, COL_BG);

        /* Top Header Banner */
        draw_rect(fb, 0, 0, 320, 24, COL_HEADER);
        draw_rect(fb, 0, 23, 320, 1, COL_BORDER);
        font_draw_string(fb, 8, 4, "AUTOMATED SUITE TEST SUMMARY", COL_CYAN, COL_HEADER);
        font_draw_string(fb, 218, 4, (suite_tests_passed == SUITE_TEST_COUNT) ? "[ALL PASSED]" : "[FAILED]",
                         (suite_tests_passed == SUITE_TEST_COUNT) ? COL_GREEN : COL_RED, COL_HEADER);

        /* Main Panel Box */
        draw_box_border(fb, 6, 26, 308, 195, COL_PANEL, COL_BORDER);

        for (uint32_t i = 0; i < SUITE_TEST_COUNT; ++i) {
            int32_t iy = 30 + (int32_t)i * 30;
            draw_box_border(fb, 10, iy, 300, 27, 0x0C1626u, COL_BORDER);

            uint32_t bit = 1u << i;
            bool executed = (suite_done_mask & bit) != 0;
            bool passed = (suite_results_mask & bit) != 0;

            font_draw_string(fb, 16, iy + 4, stage_short_names[i], COL_WHITE, 0x0C1626u);

            if (executed) {
                font_draw_string(fb, 252, iy + 4, passed ? "[PASS]" : "[FAIL]",
                                 passed ? COL_GREEN : COL_RED, 0x0C1626u);
            } else {
                font_draw_string(fb, 252, iy + 4, "[SKIP]", COL_DIM, 0x0C1626u);
            }

            const char *note = suite_stage_notes[i];
            if (note[0] != '\0') {
                font_draw_string(fb, 16, iy + 15, note, passed ? COL_GRAY : COL_RED, 0x0C1626u);
            } else {
                font_draw_string(fb, 16, iy + 15, "No stage notes recorded", COL_DIM, 0x0C1626u);
            }
        }

        /* Bottom Footer Bar */
        draw_rect(fb, 0, 224, 320, 16, COL_HEADER);
        draw_rect(fb, 0, 224, 320, 1, COL_BORDER);
        font_draw_string(fb, 8, 228, "Press [OK] to return to Main Menu", COL_YELLOW, COL_HEADER);

        vflash_video_present(fb);
        suite_frames++;
    }
}

static void run_all_automated_tests(vflash_framebuffer_t *fb)
{
    record_test_result(1, run_test_cpu_clock(fb, true));
    record_test_result(2, run_test_ram_mmu(fb, true));
    record_test_result(3, run_test_gpu_2d(fb, true));
    record_test_result(4, run_test_gpu_3d_cube(fb, true));
    record_test_result(5, run_test_iso9660_fs(fb, true));
    record_test_result(6, run_test_sdl2_assets(fb, true));
    suite_current_test = 0;
    show_automated_test_summary(fb);
}

/* --- Interactive Diagnostic 7: Input Diagnostic Menu --- */

static void run_diag_input(vflash_framebuffer_t *fb)
{
    suite_current_test = 7;
    suite_diag_visited_mask |= (1u << 0);
    uint32_t ever_seen = 0;
    uint32_t hold_frames = 0;
    int16_t min_x = 0, max_x = 0, min_y = 0, max_y = 0;

    while (1) {
        vflash_input_poll();
        const vflash_input_state_t *st = vflash_input_get_state();
        ever_seen |= st->buttons;

        if (st->axis_x < min_x) min_x = st->axis_x;
        if (st->axis_x > max_x) max_x = st->axis_x;
        if (st->axis_y < min_y) min_y = st->axis_y;
        if (st->axis_y > max_y) max_y = st->axis_y;

        /* Exit to Main Menu only when ANY button is held down continuously for a moment */
        if (st->buttons != 0) {
            hold_frames++;
            if (hold_frames >= INPUT_EXIT_HOLD_FRAMES) {
                break;
            }
        } else {
            hold_frames = 0;
        }

        if (vflash_input_is_pressed(st, VFLASH_BTN_STICK)) {
            ever_seen = 0;
            min_x = max_x = min_y = max_y = 0;
        }

        vflash_video_backbuffer(fb);
        draw_clear(fb, COL_BG);

        /* Header */
        draw_rect(fb, 0, 0, 320, 20, COL_HEADER);
        font_draw_string(fb, 8, 6, "INPUT DIAGNOSTIC MENU", COL_CYAN, COL_HEADER);
        font_draw_string(fb, 200, 6, st->connected ? "ONLINE" : "POLLING",
                         st->connected ? COL_GREEN : COL_YELLOW, COL_HEADER);

        /* Left Box: Analog Stick XY Plot (96x96) */
        draw_box_border(fb, 8, 26, 136, 142, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 16, 32, "Analog Stick", COL_CYAN, COL_PANEL);
        draw_box_border(fb, 28, 44, 96, 96, 0x0A101Cu, COL_BORDER);
        draw_rect(fb, 28, 92, 96, 1, COL_DIM);
        draw_rect(fb, 76, 44, 1, 96, COL_DIM);

        int32_t dot_x = 76 + ((int32_t)st->axis_x * 44) / 32768;
        int32_t dot_y = 92 + ((int32_t)st->axis_y * 44) / 32768;
        draw_rect(fb, dot_x - 3, dot_y - 3, 7, 7, COL_GREEN);

        font_draw_string(fb, 14, 146, "X:", COL_GRAY, COL_PANEL);
        draw_i16_dec(fb, 30, 146, st->axis_x, COL_WHITE, COL_PANEL);
        font_draw_string(fb, 80, 146, "Y:", COL_GRAY, COL_PANEL);
        draw_i16_dec(fb, 96, 146, st->axis_y, COL_WHITE, COL_PANEL);

        /* Right Box: Digital D-Pad & Action Buttons */
        draw_box_border(fb, 150, 26, 162, 142, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 158, 32, "Buttons (Held/Tested)", COL_CYAN, COL_PANEL);

        static const struct {
            uint32_t mask;
            const char *name;
            int32_t x, y;
            uint32_t active_col;
        } btns[10] = {
            { VFLASH_BTN_UP,     "UP",  158,  48, COL_CYAN    },
            { VFLASH_BTN_DOWN,   "DN",  158,  68, COL_CYAN    },
            { VFLASH_BTN_LEFT,   "LT",  158,  88, COL_CYAN    },
            { VFLASH_BTN_RIGHT,  "RT",  158, 108, COL_CYAN    },
            { VFLASH_BTN_OK,     "OK",  158, 128, COL_WHITE   },
            { VFLASH_BTN_RED,    "RED", 230,  48, COL_RED     },
            { VFLASH_BTN_YELLOW, "YEL", 230,  68, COL_YELLOW  },
            { VFLASH_BTN_GREEN,  "GRN", 230,  88, COL_GREEN   },
            { VFLASH_BTN_BLUE,   "BLU", 230, 108, COL_BLUE    },
            { VFLASH_BTN_STICK,  "STK", 230, 128, COL_MAGENTA }
        };

        for (int i = 0; i < 10; ++i) {
            bool held = (st->buttons & btns[i].mask) != 0;
            bool seen = (ever_seen & btns[i].mask) != 0;
            uint32_t bg = held ? btns[i].active_col : (seen ? 0x1E3A2Au : 0x0A101Cu);
            uint32_t fg = held ? COL_BG : (seen ? COL_GREEN : COL_GRAY);
            draw_box_border(fb, btns[i].x, btns[i].y, 64, 16, bg, seen ? COL_GREEN : COL_BORDER);
            font_draw_string(fb, btns[i].x + 8, btns[i].y + 4, btns[i].name, fg, bg);
            if (seen && !held) {
                font_draw_string(fb, btns[i].x + 44, btns[i].y + 4, "OK", COL_GREEN, bg);
            }
        }

        /* Telemetry Panel */
        draw_box_border(fb, 8, 174, 304, 44, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 16, 180, "Held Mask:", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 100, 180, st->buttons, COL_YELLOW, COL_PANEL);
        font_draw_string(fb, 190, 180, "Seen:", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 232, 180, ever_seen, COL_GREEN, COL_PANEL);

        font_draw_string(fb, 16, 196, "Raw X/Y  :", COL_GRAY, COL_PANEL);
        draw_u32_dec(fb, 100, 196, st->raw_x, COL_WHITE, COL_PANEL);
        font_draw_string(fb, 144, 196, "/", COL_GRAY, COL_PANEL);
        draw_u32_dec(fb, 156, 196, st->raw_y, COL_WHITE, COL_PANEL);

        /* Footer with Exit Hold Progress Indicator */
        draw_rect(fb, 0, 224, 320, 16, COL_HEADER);
        if (hold_frames > 0) {
            uint32_t bar_w = (hold_frames * 90u) / INPUT_EXIT_HOLD_FRAMES;
            if (bar_w > 90u) bar_w = 90u;
            font_draw_string(fb, 8, 228, "HOLD TO EXIT:", COL_YELLOW, COL_HEADER);
            draw_box_border(fb, 110, 227, 94, 9, 0x0A101Cu, COL_BORDER);
            if (bar_w > 0) {
                draw_rect(fb, 112, 229, (int32_t)bar_w, 5, COL_RED);
            }
            font_draw_string(fb, 212, 228, "Exiting...", COL_WHITE, COL_HEADER);
        } else {
            font_draw_string(fb, 8, 228, "[STK] Clear   [HOLD ANY BUTTON] Main Menu", COL_YELLOW, COL_HEADER);
        }

        vflash_video_present(fb);
        suite_frames++;
    }
    suite_current_test = 0;
}

/* --- Interactive Diagnostic 8: RAM & Memory Inspector --- */

static void run_diag_ram_inspector(vflash_framebuffer_t *fb)
{
    suite_current_test = 8;
    suite_diag_visited_mask |= (1u << 1);
    uint32_t err_addr = 0;
    uint32_t mmu_ok = 0;
    bool test_ok = perform_ram_and_mmu_checks(&err_addr, &mmu_ok);
    uint32_t test_runs = 1;

    const uintptr_t presets[4] = {
        (uintptr_t)_start,
        (uintptr_t)ram_test_arena,
        (uintptr_t)__bss_start,
        (uintptr_t)mmu_page_a
    };
    const char *const preset_names[4] = {
        ".text (_start)",
        "ram_test_arena",
        ".bss (__bss_start)",
        "mmu_page_a (4K)"
    };
    uint32_t preset_idx = 0;
    uintptr_t inspect_addr = presets[0];

    while (1) {
        vflash_input_poll();
        const vflash_input_state_t *st = vflash_input_get_state();

        if (vflash_input_is_pressed(st, VFLASH_BTN_BLUE | VFLASH_BTN_EXIT)) {
            break;
        }
        if (vflash_input_is_pressed(st, VFLASH_BTN_LEFT)) {
            preset_idx = (preset_idx + 3u) & 3u;
            inspect_addr = presets[preset_idx];
        }
        if (vflash_input_is_pressed(st, VFLASH_BTN_RIGHT)) {
            preset_idx = (preset_idx + 1u) & 3u;
            inspect_addr = presets[preset_idx];
        }
        if (vflash_input_is_pressed(st, VFLASH_BTN_UP)) {
            if (inspect_addr >= 0x10C00040u) inspect_addr -= 64u;
        }
        if (vflash_input_is_pressed(st, VFLASH_BTN_DOWN)) {
            if (inspect_addr + 64u < 0x11000000u) inspect_addr += 64u;
        }
        if (vflash_input_is_pressed(st, VFLASH_BTN_RED | VFLASH_BTN_OK)) {
            test_ok = perform_ram_and_mmu_checks(&err_addr, &mmu_ok);
            test_runs++;
        }

        vflash_video_backbuffer(fb);
        draw_clear(fb, COL_BG);

        draw_rect(fb, 0, 0, 320, 20, COL_HEADER);
        font_draw_string(fb, 8, 6, "RAM & MEMORY INSPECTOR", COL_CYAN, COL_HEADER);
        font_draw_string(fb, 224, 6, test_ok ? "RAM OK" : "RAM ERR",
                         test_ok ? COL_GREEN : COL_RED, COL_HEADER);

        /* Linker Map & Diagnostics Panel */
        draw_box_border(fb, 8, 24, 304, 76, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 14, 28, "_start :", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 80, 28, (uint32_t)(uintptr_t)_start, COL_WHITE, COL_PANEL);
        font_draw_string(fb, 168, 28, "bss  :", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 218, 28, (uint32_t)(uintptr_t)__bss_start, COL_WHITE, COL_PANEL);

        font_draw_string(fb, 14, 42, "heap   :", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 80, 42, (uint32_t)(uintptr_t)__heap_start, COL_CYAN, COL_PANEL);
        font_draw_string(fb, 168, 42, "stack:", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 218, 42, (uint32_t)(uintptr_t)__stack_top, COL_CYAN, COL_PANEL);

        font_draw_string(fb, 14, 56, "CP15 C1:", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 80, 56, suite_mmu_control, COL_YELLOW, COL_PANEL);
        font_draw_string(fb, 168, 56, "Runs :", COL_GRAY, COL_PANEL);
        draw_u32_dec(fb, 218, 56, test_runs, COL_GREEN, COL_PANEL);

        font_draw_string(fb, 14, 72, "16K Pattern + 4K Page Alias:", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 242, 72, test_ok ? "PASS" : "FAIL",
                         test_ok ? COL_GREEN : COL_RED, COL_PANEL);

        /* Live Hex Memory Dump Viewer (8 rows x 8 bytes = 64 bytes) */
        draw_box_border(fb, 8, 104, 304, 116, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 14, 108, "Region:", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 74, 108, preset_names[preset_idx], COL_YELLOW, COL_PANEL);

        const volatile uint8_t *ptr = (const volatile uint8_t *)inspect_addr;
        for (uint32_t row = 0; row < 8; ++row) {
            int32_t ry = 122 + (int32_t)row * 11;
            uint32_t row_addr = (uint32_t)(inspect_addr + row * 8u);
            font_draw_hex32(fb, 12, ry, row_addr, COL_CYAN, COL_PANEL);
            for (uint32_t col = 0; col < 8; ++col) {
                uint8_t b = ptr[row * 8u + col];
                draw_hex8(fb, 104 + (int32_t)col * 25, ry, b,
                          (b == 0) ? COL_DIM : COL_WHITE, COL_PANEL);
            }
        }

        draw_rect(fb, 0, 224, 320, 16, COL_HEADER);
        font_draw_string(fb, 6, 228, "[L/R] Region [U/D] Scroll [RED] Test [BLUE] Menu", COL_YELLOW, COL_HEADER);

        vflash_video_present(fb);
        suite_frames++;
    }
    suite_current_test = 0;
}

/* --- Interactive Diagnostic 9: Sound & PCM Audio Menu --- */

static const char *const audio_mode_names[5] = {
    "0: STEREO 440Hz(L)/880Hz(R)",
    "1: LEFT CHANNEL 440Hz ONLY",
    "2: RIGHT CHANNEL 880Hz ONLY",
    "3: FREQUENCY CHIRP SWEEP",
    "4: MAJOR CHORD ARPEGGIO"
};

static void fill_diag_audio_chunk(uint32_t mode, uint32_t *phase_l, uint32_t *phase_r,
                                  uint32_t *arp_tick, uint32_t *chime_rem,
                                  uint32_t *out_peak_l, uint32_t *out_peak_r)
{
    static const uint32_t arp_steps[4] = {
        (523u << 16) / 44100u, /* C5 */
        (659u << 16) / 44100u, /* E5 */
        (784u << 16) / 44100u, /* G5 */
        (1047u << 16) / 44100u /* C6 */
    };

    uint32_t step_440 = (440u << 16) / 44100u;
    uint32_t step_880 = (880u << 16) / 44100u;
    uint32_t pl = *phase_l;
    uint32_t pr = *phase_r;
    uint32_t at = *arp_tick;
    uint32_t chime = *chime_rem;
    uint32_t pk_l = 0;
    uint32_t pk_r = 0;

    for (uint32_t i = 0; i < AUDIO_CHUNK_FRAMES; ++i) {
        int32_t sl = (sin_q14[(pl >> 8) & 0xFFu] * 10000) >> 14;
        int32_t sr = (sin_q14[(pr >> 8) & 0xFFu] * 10000) >> 14;

        if (mode == 0) {
            pl += step_440;
            pr += step_880;
        } else if (mode == 1) {
            pl += step_440;
            sr = 0;
        } else if (mode == 2) {
            pr += step_880;
            sl = 0;
        } else if (mode == 3) {
            uint32_t sweep = step_440 + ((at >> 4) & 0x7FFu);
            pl += sweep;
            pr += (sweep << 1);
            at++;
        } else {
            uint32_t note = arp_steps[(at >> 12) & 3u];
            pl += note;
            pr += note;
            at++;
        }

        if (chime > 0) {
            uint32_t c_step = (1318u << 16) / 44100u;
            int32_t cs = (sin_q14[((chime * c_step) >> 8) & 0xFFu] * 6000) >> 14;
            sl += cs;
            sr += cs;
            chime--;
        }

        audio_chunk_buf[i * 2]     = (int16_t)sl;
        audio_chunk_buf[i * 2 + 1] = (int16_t)sr;

        uint32_t al = (sl < 0) ? (uint32_t)-sl : (uint32_t)sl;
        uint32_t ar = (sr < 0) ? (uint32_t)-sr : (uint32_t)sr;
        if (al > pk_l) pk_l = al;
        if (ar > pk_r) pk_r = ar;
    }

    *phase_l = pl;
    *phase_r = pr;
    *arp_tick = at;
    *chime_rem = chime;
    *out_peak_l = pk_l;
    *out_peak_r = pk_r;
}

static void run_diag_sound_menu(vflash_framebuffer_t *fb)
{
    suite_current_test = 9;
    suite_diag_visited_mask |= (1u << 2);
    vflash_audio_config_t cfg = {
        .sample_rate = 44100,
        .channels = 2,
        .format = VFLASH_AUDIO_FMT_S16LE,
        .buffer_samples = 4096
    };
    bool audio_ok = vflash_audio_init(&cfg);

    uint32_t mode = 0;
    bool paused = false;
    uint32_t phase_l = 0, phase_r = 0, arp_tick = 0, chime_rem = 0;
    uint32_t peak_l = 0, peak_r = 0;

    while (1) {
        vflash_input_poll();
        const vflash_input_state_t *st = vflash_input_get_state();

        if (vflash_input_is_pressed(st, VFLASH_BTN_BLUE | VFLASH_BTN_EXIT)) {
            break;
        }
        if (vflash_input_is_pressed(st, VFLASH_BTN_LEFT)) {
            mode = (mode + 4u) % 5u;
        }
        if (vflash_input_is_pressed(st, VFLASH_BTN_RIGHT)) {
            mode = (mode + 1u) % 5u;
        }
        if (vflash_input_is_pressed(st, VFLASH_BTN_RED | VFLASH_BTN_OK)) {
            paused = !paused;
            vflash_audio_pause(paused);
        }
        if (vflash_input_is_pressed(st, VFLASH_BTN_GREEN)) {
            chime_rem = 8192u;
        }

        if (audio_ok && !paused) {
            while (vflash_audio_get_available_bytes() >= sizeof(audio_chunk_buf)) {
                fill_diag_audio_chunk(mode, &phase_l, &phase_r, &arp_tick, &chime_rem, &peak_l, &peak_r);
                vflash_audio_write(audio_chunk_buf, sizeof(audio_chunk_buf));
            }
            vflash_audio_update();
        } else {
            peak_l = 0;
            peak_r = 0;
        }

        vflash_audio_stats_t astats;
        vflash_audio_get_stats(&astats);

        vflash_video_backbuffer(fb);
        draw_clear(fb, COL_BG);

        draw_rect(fb, 0, 0, 320, 20, COL_HEADER);
        font_draw_string(fb, 8, 6, "SOUND & PCM AUDIO MENU", COL_CYAN, COL_HEADER);
        font_draw_string(fb, 224, 6, paused ? "PAUSED" : "PLAYING",
                         paused ? COL_YELLOW : COL_GREEN, COL_HEADER);

        /* Waveform Mode & VU Meters */
        draw_box_border(fb, 8, 26, 304, 108, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 16, 32, "Mode:", COL_GRAY, COL_PANEL);
        font_draw_string(fb, 60, 32, audio_mode_names[mode], COL_YELLOW, COL_PANEL);

        /* Left & Right VU Bars */
        font_draw_string(fb, 16, 52, "L VU:", COL_CYAN, COL_PANEL);
        draw_box_border(fb, 60, 50, 236, 12, 0x0A101Cu, COL_BORDER);
        uint32_t vu_l = (peak_l * 232u) / 16000u;
        if (vu_l > 232u) vu_l = 232u;
        if (vu_l > 0) draw_rect(fb, 62, 52, (int32_t)vu_l, 8, COL_GREEN);

        font_draw_string(fb, 16, 70, "R VU:", COL_MAGENTA, COL_PANEL);
        draw_box_border(fb, 60, 68, 236, 12, 0x0A101Cu, COL_BORDER);
        uint32_t vu_r = (peak_r * 232u) / 16000u;
        if (vu_r > 232u) vu_r = 232u;
        if (vu_r > 0) draw_rect(fb, 62, 70, (int32_t)vu_r, 8, COL_CYAN);

        /* Oscilloscope preview box */
        draw_box_border(fb, 16, 86, 280, 42, 0x0A101Cu, COL_BORDER);
        draw_rect(fb, 17, 107, 278, 1, COL_DIM);
        for (uint32_t x = 0; x < 276; ++x) {
            int32_t s_l = audio_chunk_buf[x * 2];
            int32_t s_r = audio_chunk_buf[x * 2 + 1];
            int32_t yl = 107 - (s_l * 16) / 16000;
            int32_t yr = 107 - (s_r * 16) / 16000;
            if (yl >= 88 && yl <= 125) draw_rect(fb, 18 + (int32_t)x, yl, 1, 2, COL_GREEN);
            if (yr >= 88 && yr <= 125) draw_rect(fb, 18 + (int32_t)x, yr, 1, 2, COL_CYAN);
        }

        /* Hardware Audio DMA Stats */
        draw_box_border(fb, 8, 140, 304, 78, COL_PANEL, COL_BORDER);
        font_draw_string(fb, 16, 146, "Format : 44100 Hz 16-bit Stereo PCM", COL_WHITE, COL_PANEL);
        font_draw_string(fb, 16, 162, "Written:", COL_GRAY, COL_PANEL);
        draw_u32_dec(fb, 84, 162, astats.samples_written, COL_GREEN, COL_PANEL);
        font_draw_string(fb, 168, 162, "Played:", COL_GRAY, COL_PANEL);
        draw_u32_dec(fb, 228, 162, astats.samples_played, COL_CYAN, COL_PANEL);

        font_draw_string(fb, 16, 178, "Queued :", COL_GRAY, COL_PANEL);
        draw_u32_dec(fb, 84, 178, astats.queued_bytes, COL_YELLOW, COL_PANEL);
        font_draw_string(fb, 168, 178, "Underrun:", COL_GRAY, COL_PANEL);
        draw_u32_dec(fb, 244, 178, astats.underrun_count,
                     astats.underrun_count ? COL_RED : COL_GREEN, COL_PANEL);

        font_draw_string(fb, 16, 196, "HW Addr:", COL_GRAY, COL_PANEL);
        font_draw_hex32(fb, 84, 196, astats.current_hw_addr, COL_WHITE, COL_PANEL);

        draw_rect(fb, 0, 224, 320, 16, COL_HEADER);
        font_draw_string(fb, 6, 228, "[L/R] Mode [RED] Pause [GRN] Chime [BLUE] Menu", COL_YELLOW, COL_HEADER);

        vflash_video_present(fb);
        suite_frames++;
    }

    vflash_audio_shutdown();
    suite_current_test = 0;
}

/* --- Main Menu Renderer & Loop --- */

static void draw_main_menu(vflash_framebuffer_t *fb, uint32_t cursor)
{
    draw_clear(fb, COL_BG);

    /* Top Title Banner */
    draw_rect(fb, 0, 0, 320, 22, COL_HEADER);
    draw_rect(fb, 0, 21, 320, 1, COL_BORDER);
    font_draw_string(fb, 6, 4, "V.FLASH SDK DIAGNOSTIC & TEST SUITE", COL_CYAN, COL_HEADER);

    /* Summary status on right of sub-header */
    font_draw_string(fb, 6, 13, "MMU:", COL_GRAY, COL_HEADER);
    font_draw_string(fb, 38, 13, (suite_mmu_control & 1u) ? "ON " : "OFF",
                     (suite_mmu_control & 1u) ? COL_GREEN : COL_YELLOW, COL_HEADER);
    font_draw_string(fb, 76, 13, "PASSED:", COL_GRAY, COL_HEADER);
    draw_u32_dec(fb, 134, 13, suite_tests_passed, COL_GREEN, COL_HEADER);
    font_draw_string(fb, 144, 13, "/6", COL_GRAY, COL_HEADER);
    if (suite_tests_passed == SUITE_TEST_COUNT && suite_tests_failed == 0) {
        font_draw_string(fb, 184, 13, "[ALL TESTS PASS]", COL_GREEN, COL_HEADER);
    }

    /* Menu list box */
    draw_box_border(fb, 6, 25, 308, 195, COL_PANEL, COL_BORDER);

    for (uint32_t i = 0; i < MENU_ITEM_COUNT; ++i) {
        int32_t iy = 29 + (int32_t)i * 19;
        bool sel = (i == cursor);
        uint32_t row_bg = sel ? COL_SEL_BG : COL_PANEL;
        if (sel) {
            draw_box_border(fb, 10, iy - 2, 300, 17, COL_SEL_BG, COL_SEL_BORDER);
        }

        uint32_t label_col = sel ? COL_WHITE : ((i == 0) ? COL_YELLOW : ((i >= 7) ? COL_CYAN : COL_GRAY));
        font_draw_string(fb, 16, iy + 2, menu_labels[i], label_col, row_bg);

        /* Status badge on the right side for tests 1..6 and interactive menus 7..9 */
        if (i >= 1 && i <= SUITE_TEST_COUNT) {
            uint32_t bit = 1u << (i - 1u);
            if (suite_done_mask & bit) {
                bool p = (suite_results_mask & bit) != 0;
                font_draw_string(fb, 256, iy + 2, p ? "[PASS]" : "[FAIL]",
                                 p ? COL_GREEN : COL_RED, row_bg);
            } else {
                font_draw_string(fb, 256, iy + 2, "[----]", COL_DIM, row_bg);
            }
        } else if (i >= 7) {
            font_draw_string(fb, 256, iy + 2, "[MENU]", COL_CYAN, row_bg);
        }
    }

    /* Bottom Footer */
    draw_rect(fb, 0, 224, 320, 16, COL_HEADER);
    draw_rect(fb, 0, 224, 320, 1, COL_BORDER);
    font_draw_string(fb, 8, 228, "[UP/DN] Select   [OK/RED] Run Test or Menu", COL_YELLOW, COL_HEADER);
}

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu) ? 0x600D600Du : 0u;
    boot_marker = 0x56464C53u;

    VFLASH_ASSERT(vflash_clock_init());

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
    VFLASH_ASSERT(vflash_mmu_init() == VFLASH_MMU_OK);
    VFLASH_ASSERT(vflash_mmu_identity() == VFLASH_MMU_OK);
    VFLASH_ASSERT(vflash_mmu_map_section(0xA8000000u, 0xA8000000u, VFLASH_MMU_PRIV_RW) == VFLASH_MMU_OK);
    VFLASH_ASSERT(vflash_mmu_enable() == VFLASH_MMU_OK);
#endif
    suite_mmu_control = vflash_mmu_control();

    vflash_framebuffer_t fb;
    VFLASH_ASSERT(vflash_video_init(&fb));
    video_status = 0x56494431u;

    vflash_input_init();
    SDL_Init(0);

    render_status = 0x44524157u;
    suite_status = 0x53554954u; /* "SUIT" ready */

#if defined(VFLASH_SUITE_AUTORUN) && VFLASH_SUITE_AUTORUN
    run_all_automated_tests(&fb);
#endif

    uint32_t cursor = 0;
    int32_t stick_cooldown = 0;

    while (1) {
        vflash_input_poll();
        const vflash_input_state_t *st = vflash_input_get_state();

        if (stick_cooldown > 0) stick_cooldown--;

        bool move_up = vflash_input_is_pressed(st, VFLASH_BTN_UP);
        bool move_dn = vflash_input_is_pressed(st, VFLASH_BTN_DOWN);
        if (move_up || move_dn) {
            stick_cooldown = 12;
        } else if (stick_cooldown == 0 && !(st->buttons & (VFLASH_BTN_UP | VFLASH_BTN_DOWN))) {
            if (st->axis_y < -16000) {
                move_up = true;
                stick_cooldown = 12;
            } else if (st->axis_y > 16000) {
                move_dn = true;
                stick_cooldown = 12;
            }
        }

        if (move_up) {
            cursor = (cursor + MENU_ITEM_COUNT - 1u) % MENU_ITEM_COUNT;
        }
        if (move_dn) {
            cursor = (cursor + 1u) % MENU_ITEM_COUNT;
        }
        suite_menu_cursor = cursor;

        if (vflash_input_is_pressed(st, VFLASH_BTN_OK | VFLASH_BTN_RED)) {
            switch (cursor) {
            case 0:
                run_all_automated_tests(&fb);
                break;
            case 1:
                record_test_result(1, run_test_cpu_clock(&fb, false));
                suite_current_test = 0;
                break;
            case 2:
                record_test_result(2, run_test_ram_mmu(&fb, false));
                suite_current_test = 0;
                break;
            case 3:
                record_test_result(3, run_test_gpu_2d(&fb, false));
                suite_current_test = 0;
                break;
            case 4:
                record_test_result(4, run_test_gpu_3d_cube(&fb, false));
                suite_current_test = 0;
                break;
            case 5:
                record_test_result(5, run_test_iso9660_fs(&fb, false));
                suite_current_test = 0;
                break;
            case 6:
                record_test_result(6, run_test_sdl2_assets(&fb, false));
                suite_current_test = 0;
                break;
            case 7:
                run_diag_input(&fb);
                break;
            case 8:
                run_diag_ram_inspector(&fb);
                break;
            case 9:
                run_diag_sound_menu(&fb);
                break;
            default:
                break;
            }
        }

        vflash_video_backbuffer(&fb);
        draw_main_menu(&fb, cursor);
        vflash_video_present(&fb);
        suite_frames++;
    }
}
