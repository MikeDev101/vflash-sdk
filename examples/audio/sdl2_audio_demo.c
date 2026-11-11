#include <SDL.h>
#include <stdint.h>
#include <stdbool.h>

#include "vflash/audio.h"

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
#include "vflash/mmu.h"
#endif

/* Standard diagnostic probe symbols in SDRAM */
volatile uint32_t startup_status;
volatile uint32_t boot_marker;
volatile uint32_t data_cookie = 0x13579BDFu;
volatile uint32_t video_status;
volatile uint32_t render_status;

/* Exported probe symbols for automated test validation */
__attribute__((used, section(".data"))) volatile uint32_t sdl2_audio_frames = 0;
__attribute__((used, section(".data"))) volatile uint32_t sdl2_audio_device_id = 0;
__attribute__((used, section(".data"))) volatile uint32_t sdl2_audio_queued_bytes = 0;
__attribute__((used, section(".data"))) volatile uint32_t sdl2_audio_active = 0;
__attribute__((used, section(".data"))) volatile uint32_t sdl2_audio_samples_played = 0;
__attribute__((used, section(".data"))) volatile uint32_t sdl2_audio_callback_calls = 0;

static const int16_t s_sine_lut[256] = {
         0,    736,   1472,   2207,   2941,   3672,   4402,   5129,
      5853,   6573,   7289,   8001,   8709,   9410,  10107,  10797,
     11481,  12157,  12827,  13488,  14142,  14787,  15423,  16050,
     16667,  17274,  17871,  18457,  19032,  19595,  20147,  20686,
     21213,  21727,  22229,  22716,  23190,  23650,  24096,  24528,
     24944,  25346,  25732,  26103,  26458,  26797,  27120,  27426,
     27716,  27990,  28246,  28486,  28708,  28913,  29101,  29271,
     29424,  29558,  29675,  29774,  29856,  29919,  29964,  29991,
     30000,  29991,  29964,  29919,  29856,  29774,  29675,  29558,
     29424,  29271,  29101,  28913,  28708,  28486,  28246,  27990,
     27716,  27426,  27120,  26797,  26458,  26103,  25732,  25346,
     24944,  24528,  24096,  23650,  23190,  22716,  22229,  21727,
     21213,  20686,  20147,  19595,  19032,  18457,  17871,  17274,
     16667,  16050,  15423,  14787,  14142,  13488,  12827,  12157,
     11481,  10797,  10107,   9410,   8709,   8001,   7289,   6573,
      5853,   5129,   4402,   3672,   2941,   2207,   1472,    736,
         0,   -736,  -1472,  -2207,  -2941,  -3672,  -4402,  -5129,
     -5853,  -6573,  -7289,  -8001,  -8709,  -9410, -10107, -10797,
    -11481, -12157, -12827, -13488, -14142, -14787, -15423, -16050,
    -16667, -17274, -17871, -18457, -19032, -19595, -20147, -20686,
    -21213, -21727, -22229, -22716, -23190, -23650, -24096, -24528,
    -24944, -25346, -25732, -26103, -26458, -26797, -27120, -27426,
    -27716, -27990, -28246, -28486, -28708, -28913, -29101, -29271,
    -29424, -29558, -29675, -29774, -29856, -29919, -29964, -29991,
    -30000, -29991, -29964, -29919, -29856, -29774, -29675, -29558,
    -29424, -29271, -29101, -28913, -28708, -28486, -28246, -27990,
    -27716, -27426, -27120, -26797, -26458, -26103, -25732, -25346,
    -24944, -24528, -24096, -23650, -23190, -22716, -22229, -21727,
    -21213, -20686, -20147, -19595, -19032, -18457, -17871, -17274,
    -16667, -16050, -15423, -14787, -14142, -13488, -12827, -12157,
    -11481, -10797, -10107,  -9410,  -8709,  -8001,  -7289,  -6573,
     -5853,  -5129,  -4402,  -3672,  -2941,  -2207,  -1472,   -736,
};

static uint32_t s_phase_l = 0;
static uint32_t s_phase_r = 0;

static void audio_callback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    sdl2_audio_callback_calls++;

    int16_t *dst = (int16_t *)stream;
    int samples = len / (2 * (int)sizeof(int16_t));

    const uint32_t step_l = 167389; /* 440 Hz */
    const uint32_t step_r = 251084; /* 660 Hz */

    for (int i = 0; i < samples; ++i) {
        dst[i * 2 + 0] = s_sine_lut[(s_phase_l >> 16) & 0xFF];
        dst[i * 2 + 1] = s_sine_lut[(s_phase_r >> 16) & 0xFF];
        s_phase_l += step_l;
        s_phase_r += step_r;
    }
}

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu) ? 0x600D600Du : 0xBAD00001u;
    boot_marker = 0x56464C53u;
    video_status = 0x56494431u;
    render_status = 0x44524157u;

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
    vflash_mmu_identity();
    vflash_mmu_enable();
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        return;
    }

    SDL_Window *window = SDL_CreateWindow("V.Flash SDL2 Audio",
                                          SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          320, 240, SDL_WINDOW_SHOWN);
    if (!window) {
        SDL_Quit();
        return;
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }

    SDL_AudioSpec desired, obtained;
    SDL_zero(desired);
    desired.freq = 44100;
    desired.format = AUDIO_S16LSB;
    desired.channels = 2;
    desired.samples = 1024;
    desired.callback = audio_callback;

    SDL_AudioDeviceID dev = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained, 0);
    if (dev == 0) {
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return;
    }

    sdl2_audio_device_id = (uint32_t)dev;
    sdl2_audio_active = 1;

    /* Start audio playback */
    SDL_PauseAudioDevice(dev, 0);

    while (1) {
        sdl2_audio_frames++;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                break;
            }
        }

        vflash_audio_stats_t stats;
        vflash_audio_get_stats(&stats);
        sdl2_audio_samples_played = stats.samples_played;
        sdl2_audio_queued_bytes = stats.queued_bytes;

        /* Visual frame presentation with SDL2 renderer */
        SDL_SetRenderDrawColor(renderer, 20, 30, 48, 255);
        SDL_RenderClear(renderer);

        /* Draw animated audio level bars */
        SDL_Rect bar_l = { 32, 80, (int)((s_phase_l >> 20) & 0x7F) * 2, 24 };
        SDL_SetRenderDrawColor(renderer, 0, 220, 100, 255);
        SDL_RenderFillRect(renderer, &bar_l);

        SDL_Rect bar_r = { 32, 120, (int)((s_phase_r >> 20) & 0x7F) * 2, 24 };
        SDL_SetRenderDrawColor(renderer, 80, 160, 240, 255);
        SDL_RenderFillRect(renderer, &bar_r);

        SDL_RenderPresent(renderer);
    }

    SDL_CloseAudioDevice(dev);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
