#include "vflash/loading.h"
#include "vflash/video.h"
#include "vflash/draw.h"
#include "vflash/clock.h"
#include <stddef.h>

// Visual coordinates and colors reverse-engineered from retail loading sequence
#define COLOR_BG            0x00184A
#define COLOR_BAND          0x001842
#define COLOR_DIVIDER       0x001031
#define COLOR_TEXT_MAIN     0xDCE4F0
#define COLOR_TEXT_STATUS   0x8FA4C4
#define COLOR_BAR_BORDER    0x5A6B8C
#define COLOR_BAR_TROUGH    0x1D1D22
#define COLOR_BAR_HI        0xADB5C6
#define COLOR_BAR_MID       0x3F5276
#define COLOR_BAR_SHADOW    0x26354F

#define BAND_Y              94
#define BAND_H              45
#define DIVIDER_Y           139

#define ICON_X              68
#define ICON_Y              98
#define ICON_W              30
#define ICON_H              39

#define BAR_X               106
#define BAR_Y               127
#define BAR_W               145
#define BAR_H               8

static vflash_framebuffer_t s_fb;
static unsigned s_progress = 0;
static char s_status[32] = "Initializing...";
static char s_title[32] = "V.FLASH SDK";
static char s_subtitle[32] = "Homebrew Application";
static char s_caption[32] = "Open-Source Platform";
static char s_copyright[32] = "(C) 2026 SDK Contributors";
static unsigned s_anim_tick = 0;
static bool s_initialized = false;

// 30x39 clean-room procedural glyph mapping for retail V.DISC cartridge caddy
static const char * const s_icon_rows[ICON_H] = {
    "000000000022222222200000000000",
    "000000003320000000033200000000",
    "000003330000000000000133000000",
    "000000000000333333000000300000",
    "000030000333333333331000020000",
    "000300003333333333333200013000",
    "002200023333333333333321012200",
    "121002233333333333333332000120",
    "130003333333333333333333200030",
    "200033333333333333333333320002",
    "200033333333333333333333320002",
    "200033333333300003333333320002",
    "200333333333033330333333333002",
    "200333333320320033033333333002",
    "200333333320320023033333333002",
    "200333333320300003033333333002",
    "200333333320320023033333333002",
    "200333333333033330333333333002",
    "200033333333300003333333320002",
    "200033333333333333333333320002",
    "200033333333333333333333320002",
    "200003333333333333333333300002",
    "200003333333333333333333200002",
    "200001133333333333333332000002",
    "200000013333333333333311000002",
    "200000000333333333331000000002",
    "200000000000333333000000000002",
    "200000000000000000000000000002",
    "203003300033320030333200333302",
    "200300000020013030300002000002",
    "200300000020013030322202000002",
    "200120000020013030113202000002",
    "200030020032221030223201222202",
    "200000000000000000000000000002",
    "200000000320000000031000000002",
    "130000003023000000302200000030",
    "003000030000300003000131000300",
    "000333300000033330000002333000",
    "000222200000022220000002222000"
};

static uint32_t icon_palette(char code)
{
    switch (code) {
        case '1': return 0x3A4B62; // shadow border
        case '2': return 0x7B8EA8; // cartridge caddy outline
        case '3': return 0xDCE4F0; // metallic disc highlight & text
        default:  return 0;        // 0 = transparent
    }
}

static void draw_vdisc_icon(vflash_framebuffer_t *target, int ox, int oy)
{
    for (int y = 0; y < ICON_H; ++y) {
        const char *row = s_icon_rows[y];
        for (int x = 0; x < ICON_W; ++x) {
            char c = row[x];
            if (c != '0') {
                draw_pixel(target, ox + x, oy + y, icon_palette(c));
            }
        }
    }
}

static void draw_progress_capsule(vflash_framebuffer_t *target, int x, int y, int w, int h, unsigned percent)
{
    if (percent > 100) percent = 100;

    // Outer pill border
    draw_rect(target, x + 2, y, w - 4, 1, COLOR_BAR_BORDER);
    draw_rect(target, x + 2, y + h - 1, w - 4, 1, COLOR_BAR_BORDER);
    draw_rect(target, x, y + 2, 1, h - 4, COLOR_BAR_BORDER);
    draw_rect(target, x + w - 1, y + 2, 1, h - 4, COLOR_BAR_BORDER);
    draw_pixel(target, x + 1, y + 1, COLOR_BAR_BORDER);
    draw_pixel(target, x + 1, y + h - 2, COLOR_BAR_BORDER);
    draw_pixel(target, x + w - 2, y + 1, COLOR_BAR_BORDER);
    draw_pixel(target, x + w - 2, y + h - 2, COLOR_BAR_BORDER);

    // Dark recessed trough
    draw_rect(target, x + 2, y + 1, w - 4, h - 2, COLOR_BAR_TROUGH);

    // Filled progress bar interior
    int inner_w = w - 4;
    int fill_w = (int)((percent * (unsigned)inner_w) / 100u);
    if (fill_w > 0) {
        if (fill_w > inner_w) fill_w = inner_w;
        // Highlight row
        draw_rect(target, x + 2, y + 1, fill_w, 1, COLOR_BAR_HI);
        // Body rows
        if (h > 3) {
            draw_rect(target, x + 2, y + 2, fill_w, h - 3, COLOR_BAR_MID);
        }
        // Shadow row
        draw_rect(target, x + 2, y + h - 2, fill_w, 1, COLOR_BAR_SHADOW);
    }
}

static uint32_t str_length(const char *s)
{
    uint32_t len = 0;
    while (s && s[len]) len++;
    return len;
}

static void copy_string(char *dst, const char *src, size_t max_len)
{
    if (!src || max_len == 0) {
        if (max_len > 0) dst[0] = '\0';
        return;
    }
    size_t i = 0;
    while (src[i] && i + 1 < max_len) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static int center_x(const char *s, int scale)
{
    int w = (int)str_length(s) * 6 * scale;
    int x = (VFLASH_WIDTH - w) / 2;
    return (x < 0) ? 0 : x;
}

static void safe_copy_status(const char *src)
{
    copy_string(s_status, src, sizeof(s_status));
}

void vflash_loading_set_title(const char *title, const char *subtitle)
{
    if (title) copy_string(s_title, title, sizeof(s_title));
    if (subtitle) copy_string(s_subtitle, subtitle, sizeof(s_subtitle));
}

void vflash_loading_init_ex(const vflash_loading_config_t *config)
{
    if (config) {
        if (config->title) copy_string(s_title, config->title, sizeof(s_title));
        if (config->subtitle) copy_string(s_subtitle, config->subtitle, sizeof(s_subtitle));
        if (config->caption) copy_string(s_caption, config->caption, sizeof(s_caption));
        if (config->copyright) copy_string(s_copyright, config->copyright, sizeof(s_copyright));
    }
    vflash_loading_init();
}

void vflash_loading_init(void)
{
    vflash_video_init(&s_fb);
    s_progress = 0;
    safe_copy_status("Initializing hardware");
    s_anim_tick = 0;
    s_initialized = true;

    // Render initial SDK intro banner beforehand as requested
    vflash_framebuffer_t target;
    if (!vflash_video_backbuffer(&target)) {
        target = s_fb;
    }

    draw_clear(&target, COLOR_BG);
    if (s_title[0]) {
        draw_text(&target, center_x(s_title, 2), 55, s_title, 0xFFD700, 2);
    }
    if (s_subtitle[0]) {
        draw_text(&target, center_x(s_subtitle, 1), 95, s_subtitle, 0x50C0FF, 1);
    }
    if (s_caption[0]) {
        draw_text(&target, center_x(s_caption, 1), 115, s_caption, 0x90B0D0, 1);
    }
    if (s_copyright[0]) {
        draw_text(&target, center_x(s_copyright, 1), 145, s_copyright, 0x607890, 1);
    }

    vflash_video_present(&target);

    // Brief hardware/emulator pause so splash is visible
    uint16_t t_start = vflash_clock_ticks();
    while ((uint16_t)(vflash_clock_ticks() - t_start) < 250) {
        // ~25ms delay
    }

    // Transition into retail loading screen presentation
    vflash_loading_render();
}

void vflash_loading_set_progress(unsigned percent)
{
    s_progress = (percent > 100) ? 100 : percent;
}

void vflash_loading_set_status(const char *status)
{
    safe_copy_status(status);
}

void vflash_loading_render(void)
{
    vflash_framebuffer_t target;
    if (!vflash_video_backbuffer(&target)) {
        target = s_fb;
    }

    // 1. Clear background
    draw_clear(&target, COLOR_BG);

    // 2. Draw central horizontal band and divider
    draw_rect(&target, 0, BAND_Y, VFLASH_WIDTH, BAND_H, COLOR_BAND);
    draw_rect(&target, 0, DIVIDER_Y, VFLASH_WIDTH, 1, COLOR_DIVIDER);

    // 3. Draw V.DISC icon
    draw_vdisc_icon(&target, ICON_X, ICON_Y);

    // 4. Draw 'Loading . .' label with animated dots
    char load_str[16] = "Loading";
    int dots = (int)((s_anim_tick / 4u) % 4u);
    int p = 7;
    for (int i = 0; i < dots; ++i) {
        load_str[p++] = ' ';
        load_str[p++] = '.';
    }
    load_str[p] = '\0';

    if (s_status[0] != '\0') {
        draw_text(&target, 108, 103, load_str, COLOR_TEXT_MAIN, 1);
        draw_text(&target, 108, 114, s_status, COLOR_TEXT_STATUS, 1);
    } else {
        draw_text(&target, 108, 110, load_str, COLOR_TEXT_MAIN, 1);
    }

    // 5. Draw beveled progress bar
    draw_progress_capsule(&target, BAR_X, BAR_Y, BAR_W, BAR_H, s_progress);

    // 6. Present frame
    vflash_video_present(&target);
    s_anim_tick++;
}

void vflash_loading_finish(void)
{
    s_progress = 100;
    safe_copy_status("Ready");
    vflash_loading_render();

    // Ensure backbuffer is clear and ready for seamless video playback
    vflash_framebuffer_t target;
    if (vflash_video_backbuffer(&target)) {
        draw_clear(&target, 0x000000);
    }
}

unsigned vflash_loading_get_progress(void)
{
    return s_progress;
}

const char *vflash_loading_get_status(void)
{
    return s_status;
}
