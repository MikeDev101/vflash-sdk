#include "vflash/panic.h"
#include "vflash/video.h"
#include "vflash/clock.h"
#include "demo.h"
volatile uint32_t boot_marker, startup_status, video_status, render_status, panic_restore_status, thumb_probe_returned;
volatile uint32_t data_cookie = 0x13579BDF;
extern void panic_test_trigger(void);
void vflash_main(void)
{
    startup_status = boot_marker == 0 && data_cookie == 0x13579BDF ? 0x600D600D : 0xBAD00001;
    boot_marker = 0x56464C53;
    vflash_framebuffer_t fb;
    if (vflash_clock_init() && vflash_video_init(&fb)) {
        video_status = 0x56494431;
        demo_render(&fb);
        render_status = vflash_video_present(&fb) ? 0x44524157 : 0xBAD00003;
    }
    VFLASH_ASSERT(vflash_panic_vectors_intact());
#if PANIC_TEST_MODE == 4 || PANIC_TEST_MODE == 9
    VFLASH_ASSERT(vflash_panic_install_vectors(VFLASH_VECTOR_SWI) == 3);
#endif
#if PANIC_TEST_MODE == 6
    VFLASH_ASSERT(vflash_panic_restore_vectors());
    for (uint32_t i = 0; i < 2; ++i)
        VFLASH_ASSERT(*(volatile uint32_t *)(uintptr_t)vflash_vector_backups[i].slot == vflash_vector_backups[i].original);
    panic_restore_status = 1;
    VFLASH_ASSERT(vflash_panic_install_vectors(VFLASH_VECTOR_UNDEFINED) == 1);
#endif
    panic_test_trigger();
    vflash_panic(VFLASH_PANIC_FATAL, "THUMB UDF DID NOT TRAP IN FLASHEM");
}
