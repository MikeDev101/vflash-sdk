#include "vflash/input.h"
#include "vflash/clock.h"
#include "vflash/video.h"
#include "vflash/draw.h"
#ifdef VFLASH_ENABLE_MMU
#include "vflash/mmu.h"
#include "vflash/panic.h"
#endif

/* Probe / verification symbols for automated emulator smoke test */
volatile uint32_t startup_status;
volatile uint32_t boot_marker;
volatile uint32_t data_cookie = 0x13579BDFu;
volatile uint32_t video_status;
volatile uint32_t render_status;

volatile uint32_t input_probe_status;
volatile uint32_t input_poll_count;
volatile uint32_t input_connected;
volatile uint32_t input_buttons_held;
volatile uint32_t input_buttons_ever_seen;
volatile uint32_t input_buttons_ever_pressed;
volatile uint32_t input_buttons_ever_released;
volatile int32_t  input_min_axis_x;
volatile int32_t  input_max_axis_x;
volatile int32_t  input_min_axis_y;
volatile int32_t  input_max_axis_y;
volatile int32_t  input_cursor_x;
volatile int32_t  input_cursor_y;

/* Helper to convert integer to decimal string without division */
static void int_to_str(int32_t val, char *buf)
{
    int out = 0;
    if (val < 0) {
        buf[out++] = '-';
        val = -val;
    }
    if (val == 0) {
        buf[out++] = '0';
        buf[out] = '\0';
        return;
    }
    static const int32_t powers[] = { 10000, 1000, 100, 10, 1 };
    bool started = false;
    for (int i = 0; i < 5; i++) {
        int32_t p = powers[i];
        int digit = 0;
        while (val >= p) {
            val -= p;
            digit++;
        }
        if (digit > 0 || started || i == 4) {
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

    vflash_framebuffer_t fb;
    if (!vflash_clock_init() || !vflash_video_init(&fb)) {
        video_status = 0xBAD00002u;
        for (;;) __asm__ volatile("nop");
    }
    video_status = 0x56494431u;

#ifdef VFLASH_ENABLE_MMU
    VFLASH_ASSERT(vflash_mmu_identity() == VFLASH_MMU_OK);
    VFLASH_ASSERT(vflash_mmu_enable() == VFLASH_MMU_OK);
#endif

    vflash_input_init();
    input_probe_status = 0x19001900u;
    render_status = 0x44524157u;

    input_poll_count = 0;
    input_connected = 0;
    input_buttons_held = 0;
    input_buttons_ever_seen = 0;
    input_buttons_ever_pressed = 0;
    input_buttons_ever_released = 0;
    input_min_axis_x = 0;
    input_max_axis_x = 0;
    input_min_axis_y = 0;
    input_max_axis_y = 0;

    int32_t cur_x = 160;
    int32_t cur_y = 180;
    input_cursor_x = cur_x;
    input_cursor_y = cur_y;

    for (;;) {
        if (!vflash_video_backbuffer(&fb)) break;

        /* Poll hardware controller */
        vflash_input_poll();
        const vflash_input_state_t *st = vflash_input_get_state();

        input_poll_count++;
        input_connected = st->connected ? 1 : 0;
        input_buttons_held = st->buttons;
        input_buttons_ever_seen |= st->buttons;
        input_buttons_ever_pressed |= st->pressed;
        input_buttons_ever_released |= st->released;

        if (st->axis_x < input_min_axis_x) input_min_axis_x = st->axis_x;
        if (st->axis_x > input_max_axis_x) input_max_axis_x = st->axis_x;
        if (st->axis_y < input_min_axis_y) input_min_axis_y = st->axis_y;
        if (st->axis_y > input_max_axis_y) input_max_axis_y = st->axis_y;

        /* Update movable cursor position */
        int32_t move_x = ((int32_t)st->axis_x * 8) >> 15;
        int32_t move_y = ((int32_t)st->axis_y * 8) >> 15;
        cur_x += move_x;
        cur_y += move_y;
        if (cur_x < 24) cur_x = 24;
        if (cur_x > 296) cur_x = 296;
        if (cur_y < 140) cur_y = 140;
        if (cur_y > 220) cur_y = 220;

        input_cursor_x = cur_x;
        input_cursor_y = cur_y;

        /* Clear background: dark slate */
        draw_clear(&fb, 0x181E28);

        /* Title banner */
        draw_rect(&fb, 0, 0, 320, 24, 0x0F131C);
        draw_text(&fb, 8, 7, "V.FLASH CONTROLLER", 0xFFFFFF, 1);
        if (st->connected) {
            draw_text(&fb, 220, 7, "CONNECTED", 0x33FF55, 1);
        } else {
            draw_text(&fb, 220, 7, "NO INPUT", 0xFF4444, 1);
        }

        /* --- Analog Stick Widget --- */
        draw_rect(&fb, 20, 36, 80, 80, 0x2A3244);
        draw_rect(&fb, 22, 38, 76, 76, 0x121720);
        /* Crosshairs */
        draw_rect(&fb, 22, 75, 76, 1, 0x303B50);
        draw_rect(&fb, 59, 38, 1, 76, 0x303B50);
        /* Stick dot */
        int32_t dot_x = 59 + (((int32_t)st->axis_x * 32) >> 15);
        int32_t dot_y = 75 + (((int32_t)st->axis_y * 32) >> 15);
        draw_rect(&fb, dot_x - 3, dot_y - 3, 7, 7, 0x00E5FF);
        draw_rect(&fb, dot_x - 1, dot_y - 1, 3, 3, 0xFFFFFF);

        /* Axis text */
        char num_buf[16];
        draw_text(&fb, 20, 120, "X:", 0xAAAAAA, 1);
        int_to_str(st->axis_x, num_buf);
        draw_text(&fb, 36, 120, num_buf, 0xFFFFFF, 1);

        draw_text(&fb, 70, 120, "Y:", 0xAAAAAA, 1);
        int_to_str(st->axis_y, num_buf);
        draw_text(&fb, 86, 120, num_buf, 0xFFFFFF, 1);

        /* --- D-Pad Buttons --- */
        draw_text(&fb, 140, 36, "D-PAD", 0x8899AA, 1);
        /* UP */
        uint32_t c_up = (st->buttons & VFLASH_BTN_UP) ? 0x00FF88 : 0x2E384C;
        draw_rect(&fb, 150, 52, 20, 18, c_up);
        draw_text(&fb, 157, 57, "U", 0xFFFFFF, 1);
        /* DOWN */
        uint32_t c_down = (st->buttons & VFLASH_BTN_DOWN) ? 0x00FF88 : 0x2E384C;
        draw_rect(&fb, 150, 92, 20, 18, c_down);
        draw_text(&fb, 157, 97, "D", 0xFFFFFF, 1);
        /* LEFT */
        uint32_t c_left = (st->buttons & VFLASH_BTN_LEFT) ? 0x00FF88 : 0x2E384C;
        draw_rect(&fb, 128, 72, 20, 18, c_left);
        draw_text(&fb, 135, 77, "L", 0xFFFFFF, 1);
        /* RIGHT */
        uint32_t c_right = (st->buttons & VFLASH_BTN_RIGHT) ? 0x00FF88 : 0x2E384C;
        draw_rect(&fb, 172, 72, 20, 18, c_right);
        draw_text(&fb, 179, 77, "R", 0xFFFFFF, 1);

        /* --- Action Buttons --- */
        draw_text(&fb, 230, 36, "BUTTONS", 0x8899AA, 1);
        /* RED (BTN 1) */
        uint32_t c_red = (st->buttons & VFLASH_BTN_RED) ? 0xFF3333 : 0x661111;
        draw_rect(&fb, 220, 54, 36, 18, c_red);
        draw_text(&fb, 226, 59, "RED", 0xFFFFFF, 1);
        /* YELLOW (BTN 2) */
        uint32_t c_yellow = (st->buttons & VFLASH_BTN_YELLOW) ? 0xFFDD00 : 0x665500;
        draw_rect(&fb, 264, 54, 36, 18, c_yellow);
        draw_text(&fb, 268, 59, "YEL", 0xFFFFFF, 1);
        /* GREEN (BTN 3) */
        uint32_t c_green = (st->buttons & VFLASH_BTN_GREEN) ? 0x22EE44 : 0x115522;
        draw_rect(&fb, 220, 78, 36, 18, c_green);
        draw_text(&fb, 226, 83, "GRN", 0xFFFFFF, 1);
        /* BLUE (BTN 4) */
        uint32_t c_blue = (st->buttons & VFLASH_BTN_BLUE) ? 0x3388FF : 0x112266;
        draw_rect(&fb, 264, 78, 36, 18, c_blue);
        draw_text(&fb, 268, 83, "BLU", 0xFFFFFF, 1);
        /* OK (PRIMARY / ENTER) */
        uint32_t c_ok = (st->buttons & VFLASH_BTN_OK) ? 0xFFFFFF : 0x3A4456;
        draw_rect(&fb, 242, 102, 36, 18, c_ok);
        draw_text(&fb, 252, 107, "OK", 0x000000, 1);

        /* --- Playfield / Movable Cursor Area --- */
        draw_rect(&fb, 16, 136, 288, 92, 0x2A3244);
        draw_rect(&fb, 17, 137, 286, 90, 0x0D1117);

        /* Draw player sprite */
        draw_rect(&fb, cur_x - 6, cur_y - 6, 13, 13, 0x000000);
        draw_rect(&fb, cur_x - 5, cur_y - 5, 11, 11, 0xFFDD00);
        draw_rect(&fb, cur_x - 2, cur_y - 2, 5, 5, 0xFF3300);

        if (!vflash_video_present(&fb)) break;
    }

    vflash_input_shutdown();
}
