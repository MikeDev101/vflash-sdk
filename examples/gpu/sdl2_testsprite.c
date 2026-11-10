#include "SDL.h"
#include "SDL_vflash.h"
#include "testutils.h"

#define WINDOW_WIDTH  320
#define WINDOW_HEIGHT 240
#define NUM_SPRITES   8

#ifndef GPU_RENDERER_FINAL
#define GPU_RENDERER_FINAL 0
#endif

#if defined(GPU_SDL2_MMU) || defined(GPU_SPRITE_MMU)
#include "vflash/mmu.h"
#endif

volatile uint32_t boot_marker, startup_status, video_status, render_status;
volatile uint32_t data_cookie = 0x13579BDF;
volatile uint32_t gpu_sdl2_frames = 0;
volatile uint32_t gpu_sdl2_status = 0;
volatile uint32_t gpu_sdl2_control = 0;
volatile uint32_t gpu_lists = 0;
volatile uint32_t gpu_submission_ticks = 0;
volatile uint32_t gpu_completion_ticks = 0;
volatile uint32_t gpu_heap_used = 0;
volatile uint32_t gpu_alloc_count = 0;
volatile char gpu_sdl2_error[128];

static SDL_Texture *sprite;
static SDL_Rect positions[NUM_SPRITES];
static SDL_Rect velocities[NUM_SPRITES];
static int sprite_w, sprite_h;

static SDL_Renderer *renderer;
static SDL_Window *window;
static int done = 0;

static void MoveSprites(void)
{
    int i;
    int window_w = WINDOW_WIDTH;
    int window_h = WINDOW_HEIGHT;
    SDL_Rect *position, *velocity;

    /* Draw a gray background (identical to upstream testspriteminimal) */
    SDL_SetRenderDrawColor(renderer, 0xA0, 0xA0, 0xA0, 0xFF);
    SDL_RenderClear(renderer);

    /* Move the sprite, bounce at the wall, and draw */
    for (i = 0; i < NUM_SPRITES; ++i) {
        position = &positions[i];
        velocity = &velocities[i];
        position->x += velocity->x;
        if ((position->x < 0) || (position->x >= (window_w - sprite_w))) {
            velocity->x = -velocity->x;
            position->x += velocity->x;
        }
        position->y += velocity->y;
        if ((position->y < 0) || (position->y >= (window_h - sprite_h))) {
            velocity->y = -velocity->y;
            position->y += velocity->y;
        }

        /* Blit the sprite onto the screen */
        SDL_RenderCopy(renderer, sprite, NULL, position);
    }

    /* Update the screen! */
    SDL_RenderPresent(renderer);
    gpu_sdl2_frames++;
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    int i;

    if (SDL_CreateWindowAndRenderer(WINDOW_WIDTH, WINDOW_HEIGHT, 0, &window, &renderer) < 0) {
        gpu_sdl2_status = 0xDEAD0001;
        return 1;
    }

    sprite = LoadTexture(renderer, "icon.bmp", SDL_TRUE, &sprite_w, &sprite_h);
    if (!sprite) {
        gpu_sdl2_status = 0xDEAD0002;
        const char *err = SDL_GetError();
        if (err) {
            for (int j = 0; j < 127 && err[j]; ++j) gpu_sdl2_error[j] = err[j];
        }
        return 2;
    }

    /* Initialize deterministic sprite positions */
    for (i = 0; i < NUM_SPRITES; ++i) {
        positions[i].x = 16 + i * 32;
        positions[i].y = 20 + (i % 4) * 40;
        positions[i].w = sprite_w;
        positions[i].h = sprite_h;
        velocities[i].x = (i % 2 == 0) ? 1 : -1;
        velocities[i].y = (i % 3 == 0) ? 1 : -1;
    }

    video_status = 0x56494431;
    render_status = 0x44524157;

#if GPU_RENDERER_FINAL == 2
    int target_frames = 32;

    /* Run 32 frames of animation then perform clean shutdown for scanout recovery check */
    for (int frame = 0; frame < target_frames && !done; ++frame) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                done = 1;
            }
        }
        MoveSprites();
    }

    vflash_sdl2_get_stats(renderer, (uint32_t *)&gpu_lists, (uint32_t *)&gpu_submission_ticks, (uint32_t *)&gpu_completion_ticks);
    gpu_heap_used = (uint32_t)vflash_sdl2_get_heap_used();
    gpu_alloc_count = (uint32_t)vflash_sdl2_get_alloc_count();

    /* Clean shutdown: restore linear scanout */
    SDL_DestroyTexture(sprite);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    gpu_sdl2_status = 0x600D5D12; /* GOOD SDL2 */

    /* Maintain final frame for scanout capture */
    while (1) {
        /* idle */
    }
#else
    gpu_sdl2_status = 0x600D5D12; /* GOOD SDL2 */

    /* Run real SDL2 rendering and animation indefinitely */
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                done = 1;
            }
        }
        MoveSprites();
        vflash_sdl2_get_stats(renderer, (uint32_t *)&gpu_lists, (uint32_t *)&gpu_submission_ticks, (uint32_t *)&gpu_completion_ticks);
        gpu_heap_used = (uint32_t)vflash_sdl2_get_heap_used();
        gpu_alloc_count = (uint32_t)vflash_sdl2_get_alloc_count();
    }
#endif

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
    char *argv[] = { "sdl2_testsprite", 0 };
    return main(1, argv);
}
