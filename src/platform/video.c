#include "vflash/video.h"
#include "vflash/draw.h"
#include "vflash/clock.h"
#ifdef VFLASH_DOUBLE_BUFFER
#include "vflash/clock.h"
uint16_t video_back_pixels[VFLASH_WIDTH * VFLASH_HEIGHT] __attribute__((aligned(64)));
static uint16_t *front;
#endif

/* Owned .bss storage: its physical address is assigned by the linker, not
 * borrowed from a firmware buffer. Exported for debugger/SDRAM verification. */
uint16_t video_pixels[VFLASH_WIDTH * VFLASH_HEIGHT] __attribute__((aligned(64)));

/* FlashEm fb109dd src/hw.c: ve_write, ve_render, rgb555. All offsets are
 * relative to the custom ZEVIO video engine, NOT the A800 graphics engine. */
#define VE_BASE 0xB8000000u
#define VE_REG(offset) (*(volatile uint32_t *)(uintptr_t)(VE_BASE + (offset)))
#define VE_SIZE       0x00Cu
#define VE_STATUS     0x000u
#define VE_IRQ_MASK   0x004u
#define VE_ENABLE     0x100u
#define VE_LAYOUT     0x108u
#define VE_BACKGROUND 0x10Cu
#define VE_START0     0x130u
#define VE_END0       0x134u
#define VE_ADDRESS0   0x160u
#define SCREEN_END ((VFLASH_HEIGHT - 1u) << 16 | (VFLASH_WIDTH - 1u))

static uint32_t tiled_address, tiled_bytes, saved_linear_address;

int vflash_video_init(vflash_framebuffer_t *fb)
{
    if (!fb || tiled_address) return 0;
#ifdef VFLASH_DOUBLE_BUFFER
    front = video_pixels;
#endif
    *fb = (vflash_framebuffer_t){ video_pixels, VFLASH_WIDTH, VFLASH_HEIGHT,
        VFLASH_WIDTH, VFLASH_WIDTH * VFLASH_HEIGHT, VFLASH_BGR555 };
    uintptr_t address = (uintptr_t)video_pixels;
    if ((address & 63u) || address < 0x10000000u ||
        address > 0x11000000u - sizeof(video_pixels)) return 0;
    /* Disable every layer and video IRQ source before installing our buffer.
     * Keep the ROM's unrelated output timing registers; the emulator scanout
     * consumes only the explicit size/window/layout/address/enable below. */
    VE_REG(VE_ENABLE) = 0;
    VE_REG(VE_IRQ_MASK) = 0;
    VE_REG(VE_SIZE) = SCREEN_END;
    VE_REG(VE_LAYOUT) = 0; /* layer 0 is LINEAR: clear tile-layout bit 2 */
    VE_REG(VE_BACKGROUND) = 0;
    VE_REG(VE_START0) = 0;
    VE_REG(VE_END0) = SCREEN_END;
    VE_REG(VE_ADDRESS0) = (uint32_t)address;
    return VE_REG(VE_SIZE) == SCREEN_END && VE_REG(VE_END0) == SCREEN_END &&
        VE_REG(VE_ADDRESS0) == address && VE_REG(VE_LAYOUT) == 0;
}

int vflash_video_present(const vflash_framebuffer_t *fb)
{
    if (tiled_address || !draw_valid(fb) ||
#ifdef VFLASH_DOUBLE_BUFFER
        (fb->pixels != video_pixels && fb->pixels != video_back_pixels) || fb->pixels == front ||
#else
        fb->pixels != video_pixels ||
#endif
        fb->width != VFLASH_WIDTH || fb->height != VFLASH_HEIGHT ||
        fb->stride != VFLASH_WIDTH) return 0;
    /* Firmware handoff has MMU/caches off. Order CPU stores before enabling
     * scanout; no cache clean or DMA submission is needed in this model. */
    __asm__ volatile("" ::: "memory");
#ifdef VFLASH_DOUBLE_BUFFER
    /* Status bit 1 is set by ve_vblank and cleared by writing 1. Wait for
     * a fresh event, then flip; the next ve_render sees the completed bank. */
    VE_REG(VE_STATUS) = 2;
    uint16_t start = vflash_clock_ticks();
    while (!(VE_REG(VE_STATUS) & 2)) {
        if ((uint16_t)(vflash_clock_ticks() - start) >= 1000) return 0;
    }
    VE_REG(VE_ADDRESS0) = (uint32_t)(uintptr_t)fb->pixels;
    front = fb->pixels;
#endif
    VE_REG(VE_ENABLE) = 1u << 4; /* only color layer 0, no tiles/movie/GE */
    return VE_REG(VE_ENABLE) == (1u << 4);
}

int vflash_video_backbuffer(vflash_framebuffer_t *fb)
{
#ifdef VFLASH_DOUBLE_BUFFER
    if (!fb) return 0;
    *fb = (vflash_framebuffer_t){front == video_pixels ? video_back_pixels : video_pixels,
        VFLASH_WIDTH, VFLASH_HEIGHT, VFLASH_WIDTH, VFLASH_WIDTH * VFLASH_HEIGHT, VFLASH_BGR555};
    return 1;
#else
    (void)fb;
    return 0;
#endif
}

uint32_t vflash_video_front_bank(void)
{
#ifdef VFLASH_DOUBLE_BUFFER
    return front == video_pixels ? 0u : 1u;
#else
    return 0;
#endif
}

uint32_t vflash_video_tiled_address(void) { return tiled_address; }
uint32_t vflash_video_tiled_bytes(void) { return tiled_bytes; }
static int tiled_vblank(void)
{
    VE_REG(VE_STATUS)=2;
    uint16_t start=vflash_clock_ticks();
    while (!(VE_REG(VE_STATUS)&2))
        if ((uint16_t)(vflash_clock_ticks()-start)>=1000) return 0;
    return 1;
}
int vflash_video_present_tiled_ex(uint32_t address,uint32_t width,uint32_t height,int32_t x,int32_t y)
{
    if (!width || !height || (width&31) || (height&7) || width>VFLASH_WIDTH || height>VFLASH_HEIGHT) return 0;
    if (x<0 || y<0 || (int64_t)x+width>VFLASH_WIDTH || (int64_t)y+height>VFLASH_HEIGHT) return 0;
    uint32_t needed=(height>>3)*16384u;
    if ((address&63) || address<0x10000000 || address>0x11000000-needed) return 0;
    if (VE_REG(VE_SIZE)!=SCREEN_END || VE_REG(VE_IRQ_MASK)!=0 || VE_REG(VE_ENABLE)!=16) return 0;
    if (!tiled_address) {
        uint32_t current=VE_REG(VE_ADDRESS0);
        if (current!=(uint32_t)(uintptr_t)video_pixels
#ifdef VFLASH_DOUBLE_BUFFER
            && current!=(uint32_t)(uintptr_t)video_back_pixels
#endif
        ) return 0;
        saved_linear_address=current;
    }
    if (!tiled_vblank()) return 0;
    VE_REG(VE_ENABLE)=0;VE_REG(VE_LAYOUT)=4;
    VE_REG(VE_START0)=(uint32_t)y<<16|(uint32_t)x;
    VE_REG(VE_END0)=(uint32_t)(y+(int32_t)height-1)<<16|(uint32_t)(x+(int32_t)width-1);
    VE_REG(VE_ADDRESS0)=address;
    __asm__ volatile("" ::: "memory");VE_REG(VE_ENABLE)=16;
    tiled_address=address;tiled_bytes=needed;return 1;
}
int vflash_video_present_tiled(uint32_t address,int32_t x,int32_t y)
{
    return vflash_video_present_tiled_ex(address,64,16,x,y);
}
int vflash_video_restore_linear(void)
{
    if (!tiled_address) return 1;
    if (!tiled_vblank()) return 0;
    VE_REG(VE_ENABLE)=0;VE_REG(VE_LAYOUT)=0;
    VE_REG(VE_START0)=0;VE_REG(VE_END0)=SCREEN_END;
    VE_REG(VE_ADDRESS0)=saved_linear_address;
    __asm__ volatile("" ::: "memory");VE_REG(VE_ENABLE)=16;
    tiled_address=0;tiled_bytes=0;return VE_REG(VE_LAYOUT)==0 && VE_REG(VE_ADDRESS0)==saved_linear_address;
}
