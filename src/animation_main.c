#include "animation.h"
#include "clock.h"
#include "video.h"

volatile uint32_t boot_marker, startup_status, video_status, render_status;
volatile uint32_t data_cookie = 0x13579BDFu;
/* One coherent record of the last completed/presented frame, read at vblank
 * by the emulator test. The back-buffer render never changes this record. */
volatile struct {
    uint32_t frame;
    int32_t x, y;
    uint32_t fps, shown_us, bank, draw_ticks, frame_ticks;
} animation_snapshot;

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
    animation_state_t state;
    animation_init(&state);
    uint16_t last = vflash_clock_ticks();
    uint32_t window_ticks = 0, window_frames = 0, fps = 0, draw_us = 0, frame = 0;
    for (;;) {
        if (!vflash_video_backbuffer(&fb)) break;
        uint32_t shown_fps = fps, shown_us = draw_us;
        uint16_t begin = vflash_clock_ticks();
        animation_render(&fb, &state, shown_fps, shown_us);
        uint16_t draw_ticks = (uint16_t)(vflash_clock_ticks() - begin);
        if (!vflash_video_present(&fb)) break;
        uint16_t now = vflash_clock_ticks();
        uint16_t elapsed = (uint16_t)(now - last);
        last = now;
        animation_snapshot.frame = ++frame;
        animation_snapshot.x = state.x;
        animation_snapshot.y = state.y;
        animation_snapshot.fps = shown_fps;
        animation_snapshot.shown_us = shown_us;
        animation_snapshot.bank = vflash_video_front_bank();
        animation_snapshot.draw_ticks = draw_ticks;
        animation_snapshot.frame_ticks = elapsed;
        render_status = 0x44524157u;
        draw_us = (uint32_t)draw_ticks * 100;
        window_ticks += elapsed;
        ++window_frames;
        if (window_ticks >= 10000) {
            fps = (window_frames * 10000 + window_ticks / 2) / window_ticks;
            window_frames = window_ticks = 0;
        }
        animation_step(&state);
    }
    render_status = 0xBAD00003u;
    for (;;) __asm__ volatile("nop");
}
