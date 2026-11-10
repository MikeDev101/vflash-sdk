#include <stdint.h>
#include "demo.h"
#include "video.h"

/* Symbols give the debugger a location; no unverified fixed RAM mailbox. */
volatile uint32_t boot_marker;
volatile uint32_t startup_status;
volatile uint32_t data_cookie = 0x13579BDFu;
volatile uint32_t video_status;
volatile uint32_t render_status;

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu)
        ? 0x600D600Du : 0xBAD00001u;
    boot_marker = 0x56464C53u;
    vflash_framebuffer_t fb;
    if (vflash_video_init(&fb)) {
        video_status = 0x56494431u; /* VID1: configured, not yet visual proof */
        demo_render(&fb);
        render_status = vflash_video_present(&fb) ? 0x44524157u : 0xBAD00003u;
    } else {
        video_status = 0xBAD00002u;
    }
    for (;;) {
        __asm__ volatile("nop");
    }
}
