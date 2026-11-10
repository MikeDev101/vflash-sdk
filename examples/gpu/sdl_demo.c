/* Standalone SDL2-compatible rendering demonstration over vflash_renderer_t:
 * validates SDL2 API subset (CreateRenderer, CreateTexture, UpdateTexture,
 * SetRenderDrawColor, RenderClear, RenderFillRect, RenderSetClipRect, RenderCopy,
 * RenderPresent, DestroyTexture, DestroyRenderer) in freestanding ARM. */
#include "vflash/sdl_render.h"
#include "vflash/clock.h"
#include "vflash/panic.h"
#include "vflash/draw.h"
#ifdef GPU_RENDERER_MMU
#include "vflash/mmu.h"
#endif

#define REG(a) (*(volatile uint32_t *)(uintptr_t)(a))
volatile uint32_t boot_marker, startup_status, video_status, render_status;
volatile uint32_t data_cookie = 0x13579BDF;
volatile uint16_t sdl_surface_a[32768] __attribute__((aligned(64)));
volatile uint16_t sdl_surface_b[32768] __attribute__((aligned(64)));
volatile uint16_t sdl_rgb555_asset[256] __attribute__((aligned(64)));
volatile uint32_t sdl_argb8888_asset[256] __attribute__((aligned(64)));

volatile uint32_t gpu_sdl_status, gpu_sdl_frames, gpu_sdl_control;
volatile uint32_t gpu_operation_ticks, gpu_present_ticks, gpu_total_ticks;
volatile uint32_t gpu_submission_ticks, gpu_completion_ticks, gpu_lists;
volatile uint32_t gpu_sdl_scaling_rejected, gpu_sdl_live_rejected, gpu_sdl_clip_cleared;

static uint16_t make_rgb555(uint32_t x, uint32_t y)
{
    if (x == 0 || x == 15 || y == 0 || y == 15) return 0x7FE0; /* yellow border */
    if (x >= 6 && x <= 9 && y >= 6 && y <= 9) return 0x0000;   /* black center */
    uint32_t r5 = (x & 15u) << 1;
    uint32_t g5 = (y & 15u) << 1;
    uint32_t b5 = ((x + y) & 15u) << 1;
    return (uint16_t)((r5 << 10) | (g5 << 5) | b5);
}

static uint32_t make_argb8888(uint32_t x, uint32_t y)
{
    /* Transparent corners (A=0) */
    if ((x < 2 && y < 2) || (x >= 14 && y < 2) || (x < 2 && y >= 14) || (x >= 14 && y >= 14))
        return 0x00FF0000;
    /* Center features */
    if (x >= 6 && x <= 9 && y >= 6 && y <= 9) {
        if (y < 8) return (x < 8) ? 0xFF000000 : 0xFFFFFFFF; /* opaque black & white */
        return (x < 8) ? 0x8000FFFF : 0x40FFFF00; /* semi-transparent cyan / yellow */
    }
    if (y < 8) return (x < 8) ? 0x80FF0000 : 0x8000FF00; /* 50% red & green */
    return (x < 8) ? 0x800000FF : 0xC0FF00FF; /* 50% blue & 75% magenta */
}

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDF) ? 0x600D600D : 0;
    boot_marker = 0x56464C53;
    VFLASH_ASSERT(vflash_clock_init());
#ifdef GPU_RENDERER_MMU
    VFLASH_ASSERT(vflash_mmu_identity() == 0);
    VFLASH_ASSERT(vflash_mmu_map_section(0xA8000000, 0xA8000000, 1) == 0);
    VFLASH_ASSERT(vflash_mmu_enable() == 0);
#endif
    uint32_t c;
    __asm__ volatile("mrc p15,0,%0,c1,c0,0" : "=r"(c));
    gpu_sdl_control = c;

    for (uint32_t i = 0; i < 32768; ++i) sdl_surface_a[i] = sdl_surface_b[i] = 0x255A;

    for (uint32_t y = 0; y < 16; ++y) {
        for (uint32_t x = 0; x < 16; ++x) {
            sdl_rgb555_asset[y * 16 + x] = make_rgb555(x, y);
            sdl_argb8888_asset[y * 16 + x] = make_argb8888(x, y);
        }
    }
    video_status = 0x56494431;

    static SDL_Renderer renderer_storage;
    vflash_renderer_config_t cfg = {
        .surface_a = (void *)sdl_surface_a,
        .surface_b = (void *)sdl_surface_b,
        .surface_bytes = sizeof(sdl_surface_a),
        .surface_width = 128,
        .surface_height = 32,
        .playfield_width = 96,
        .playfield_height = 32,
        .screen_x = 16,
        .screen_y = 24,
        .atlas_x = 96,
        .atlas_y = 8,
        .atlas_width = 32,
        .atlas_height = 16
    };

    SDL_Renderer *renderer = SDL_CreateRenderer_Static(&renderer_storage, &cfg);
    VFLASH_ASSERT(renderer != NULL);

    /* Allocate and upload textures during setup phase */
    SDL_Texture *tex_rgb = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB555, SDL_TEXTUREACCESS_STATIC, 16, 16);
    VFLASH_ASSERT(tex_rgb != NULL);
    VFLASH_ASSERT(SDL_UpdateTexture(tex_rgb, NULL, (const void *)sdl_rgb555_asset, 16 * 2) == 0);

    SDL_Texture *tex_argb = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 16, 16);
    VFLASH_ASSERT(tex_argb != NULL);
    VFLASH_ASSERT(SDL_UpdateTexture(tex_argb, NULL, (const void *)sdl_argb8888_asset, 16 * 4) == 0);

    /* Rejection test: scaling unsupported */
    SDL_Rect bad_dst = {0, 0, 32, 32};
    VFLASH_ASSERT(SDL_RenderCopy(renderer, tex_rgb, NULL, &bad_dst) == -1);
    gpu_sdl_scaling_rejected = 1;

    uint16_t begin = vflash_clock_ticks();
    for (uint32_t frame = 0; frame < 32; ++frame) {
        uint16_t start = vflash_clock_ticks();

        /* Set a clip rectangle before clear to verify RenderClear clears the full playfield */
        SDL_Rect restricted_clip = {10, 10, 20, 10};
        VFLASH_ASSERT(SDL_RenderSetClipRect(renderer, &restricted_clip) == 0);

        /* RenderClear must clear the whole 96x32 playfield ignoring restricted_clip */
        VFLASH_ASSERT(SDL_SetRenderDrawColor(renderer, 0x10, 0x20, 0x60, 255) == 0);
        VFLASH_ASSERT(SDL_RenderClear(renderer) == 0);

        /* Reset clip for background fills */
        VFLASH_ASSERT(SDL_RenderSetClipRect(renderer, NULL) == 0);

        VFLASH_ASSERT(SDL_SetRenderDrawColor(renderer, 0x10, 0x50, 0x20, 255) == 0);
        SDL_Rect ground = {0, 16, 96, 16};
        VFLASH_ASSERT(SDL_RenderFillRect(renderer, &ground) == 0);

        VFLASH_ASSERT(SDL_SetRenderDrawColor(renderer, 0xC0, 0x80, 0x20, 255) == 0);
        SDL_Rect path = {0, 14, 96, 4};
        VFLASH_ASSERT(SDL_RenderFillRect(renderer, &path) == 0);

        VFLASH_ASSERT(SDL_SetRenderDrawColor(renderer, 0x40, 0x40, 0x40, 255) == 0);
        SDL_Rect post1 = {30, 0, 4, 32};
        VFLASH_ASSERT(SDL_RenderFillRect(renderer, &post1) == 0);
        SDL_Rect post2 = {62, 0, 4, 32};
        VFLASH_ASSERT(SDL_RenderFillRect(renderer, &post2) == 0);

        /* Draw 1: Opaque BGR555 texture at (4, 8) */
        SDL_Rect dst1 = {4, 8, 16, 16};
        VFLASH_ASSERT(SDL_RenderCopy(renderer, tex_rgb, NULL, &dst1) == 0);

        /* Draw 2: Translucent ARGB8888 texture at (36, 8) */
        SDL_Rect dst2 = {36, 8, 16, 16};
        VFLASH_ASSERT(SDL_RenderCopy(renderer, tex_argb, NULL, &dst2) == 0);

        /* Draw 3: Repeated blend ARGB8888 texture at (42, 12) -- overlaps Draw 2 */
        SDL_Rect dst3 = {42, 12, 16, 16};
        VFLASH_ASSERT(SDL_RenderCopy(renderer, tex_argb, NULL, &dst3) == 0);

        /* Draw 4: Clipped ARGB8888 texture at (60, 4) with active scissor (64, 6, 20, 16) */
        SDL_Rect scissor = {64, 6, 20, 16};
        VFLASH_ASSERT(SDL_RenderSetClipRect(renderer, &scissor) == 0);
        SDL_Rect dst4 = {60, 4, 16, 16};
        VFLASH_ASSERT(SDL_RenderCopy(renderer, tex_argb, NULL, &dst4) == 0);
        VFLASH_ASSERT(SDL_RenderSetClipRect(renderer, NULL) == 0);

        gpu_operation_ticks += (uint16_t)(vflash_clock_ticks() - start);
        start = vflash_clock_ticks();
        SDL_RenderPresent(renderer);
        gpu_present_ticks += (uint16_t)(vflash_clock_ticks() - start);

        /* Rejection test: live update while displayed buffer is locked */
        VFLASH_ASSERT(SDL_UpdateTexture(tex_rgb, NULL, (const void *)sdl_rgb555_asset, 16 * 2) == -1);
        gpu_sdl_live_rejected = 1;

        gpu_sdl_frames = frame + 1;
    }
    gpu_total_ticks = (uint16_t)(vflash_clock_ticks() - begin);

    vflash_gpu_stats_t stats;
    VFLASH_ASSERT(vflash_gpu_stats(&renderer->native_renderer.gpu, &stats) == 0);
    gpu_submission_ticks = stats.submission_ticks;
    gpu_completion_ticks = stats.completion_ticks;
    gpu_lists = stats.lists;

    SDL_DestroyTexture(tex_rgb);
    SDL_DestroyTexture(tex_argb);
    SDL_DestroyRenderer(renderer);

    VFLASH_ASSERT(REG(0xB8000108) == 0 && REG(0xB8000160) == (uint32_t)(uintptr_t)renderer_storage.native_renderer.linear_fb.pixels);

#if GPU_RENDERER_FINAL == 0
    vflash_gpu_t final_gpu;
    VFLASH_ASSERT(vflash_gpu_init(&final_gpu, VFLASH_GPU_ZEVIO) == 0);
    VFLASH_ASSERT(vflash_gpu_present_view(&final_gpu, &renderer_storage.native_renderer.surf_a, 96, 32, 0, 0) == 0);
#elif GPU_RENDERER_FINAL == 1
    vflash_gpu_t final_gpu;
    VFLASH_ASSERT(vflash_gpu_init(&final_gpu, VFLASH_GPU_ZEVIO) == 0);
    VFLASH_ASSERT(vflash_gpu_present_view(&final_gpu, &renderer_storage.native_renderer.surf_b, 96, 32, 224, 208) == 0);
#endif
    VFLASH_ASSERT(vflash_panic_vectors_intact());
    render_status = 0x44524157;
    gpu_sdl_status = 0x53444C32; /* "SDL2" */
    for (;;) __asm__ volatile("nop");
}

