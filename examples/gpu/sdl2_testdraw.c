#include "SDL.h"

#define WINDOW_WIDTH  320
#define WINDOW_HEIGHT 240
#define NUM_OBJECTS   16

#ifndef GPU_RENDERER_FINAL
#define GPU_RENDERER_FINAL 0
#endif

#if defined(GPU_SDL2_MMU) || defined(GPU_SPRITE_MMU)
#include "vflash/mmu.h"
#endif

volatile uint32_t boot_marker, startup_status, video_status, render_status;
volatile uint32_t data_cookie = 0x13579BDF;
volatile uint32_t gpu_sdl2_draw_frames = 0;
volatile uint32_t gpu_sdl2_draw_status = 0;
volatile uint32_t gpu_sdl2_control = 0;

static SDL_Renderer *renderer;
static SDL_Window *window;

static void DrawRects(SDL_Renderer *r)
{
    /* Clear to dark blue */
    SDL_SetRenderDrawColor(r, 0, 16, 48, 255);
    SDL_RenderClear(r);

    /* Draw distinct colored test rectangles */
    SDL_Rect rect1 = { 10, 10, 60, 40 };
    SDL_SetRenderDrawColor(r, 255, 0, 0, 255); /* Red */
    SDL_RenderFillRect(r, &rect1);

    SDL_Rect rect2 = { 80, 20, 80, 50 };
    SDL_SetRenderDrawColor(r, 0, 255, 0, 255); /* Green */
    SDL_RenderFillRect(r, &rect2);

    SDL_Rect rect3 = { 180, 40, 100, 60 };
    SDL_SetRenderDrawColor(r, 0, 0, 255, 255); /* Blue */
    SDL_RenderFillRect(r, &rect3);

    SDL_Rect rect4 = { 50, 120, 120, 80 };
    SDL_SetRenderDrawColor(r, 255, 255, 0, 255); /* Yellow */
    SDL_RenderFillRect(r, &rect4);

    SDL_Rect rect5 = { 200, 140, 90, 70 };
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255); /* White */
    SDL_RenderFillRect(r, &rect5);

    SDL_RenderPresent(r);
    gpu_sdl2_draw_frames++;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    if (SDL_CreateWindowAndRenderer(WINDOW_WIDTH, WINDOW_HEIGHT, 0, &window, &renderer) < 0) {
        gpu_sdl2_draw_status = 0xDEAD0001;
        return 1;
    }

    video_status = 0x56494431;
    render_status = 0x44524157;

    int target_frames = 32;
#if GPU_RENDERER_FINAL == 0
    target_frames = 33; /* Lands on presented surface A */
#elif GPU_RENDERER_FINAL == 1
    target_frames = 32; /* Lands on presented surface B */
#endif

    for (int frame = 0; frame < target_frames; ++frame) {
        DrawRects(renderer);
    }

#if GPU_RENDERER_FINAL == 2
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
#endif

    gpu_sdl2_draw_status = 0x600D4452; /* GOOD DRAW */

    /* Maintain final frame for scanout capture */
    while (1) {
        /* idle */
    }

    return 0;
}

int vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDF) ? 0x600D600D : 0;
    boot_marker = 0x56464C53;
#if defined(GPU_SDL2_MMU) || defined(GPU_SPRITE_MMU)
    vflash_mmu_init();
    vflash_mmu_identity();
    vflash_mmu_map_section(0xA8000000, 0xA8000000, 1);
    vflash_mmu_enable();
    gpu_sdl2_control = vflash_mmu_control();
#else
    gpu_sdl2_control = 0;
#endif
    char *argv[] = { "sdl2_testdraw", 0 };
    return main(1, argv);
}
