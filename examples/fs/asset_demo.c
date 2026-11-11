#include <SDL.h>
#include <vflash/clock.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
#include <vflash/mmu.h>
#endif

volatile uint32_t startup_status;
volatile uint32_t boot_marker;
volatile uint32_t data_cookie = 0x13579BDFu;
volatile uint32_t video_status;
volatile uint32_t render_status;

volatile uint32_t asset_demo_active = 0;
volatile uint32_t asset_demo_bmp_loaded = 0;
volatile uint32_t asset_demo_wav_loaded = 0;
volatile uint32_t asset_demo_cfg_loaded = 0;
volatile uint32_t asset_demo_frames = 0;
volatile uint32_t asset_demo_err = 0;

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu) ? 0x600D600Du : 0;
    boot_marker = 0x56464C53u;

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
    vflash_mmu_init();
    vflash_mmu_identity();
    vflash_mmu_map_section(0xA8000000, 0xA8000000, 1);
    vflash_mmu_enable();
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        asset_demo_err = 0x101;
        goto loop;
    }

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    if (SDL_CreateWindowAndRenderer(320, 240, 0, &window, &renderer) != 0) {
        asset_demo_err = 0x102;
        goto loop;
    }
    video_status = 0x56494431u;
    render_status = 0x44524157u;

    /* 1. Load config.txt via SDL_RWFromFile */
    SDL_RWops *rw_cfg = SDL_RWFromFile("config.txt", "rb");
    if (rw_cfg) {
        char cfg_buf[128];
        Sint64 sz = SDL_RWsize(rw_cfg);
        if (sz > 0 && sz < (Sint64)sizeof(cfg_buf)) {
            size_t nb = SDL_RWread(rw_cfg, cfg_buf, 1, (size_t)sz);
            if (nb == (size_t)sz) {
                cfg_buf[nb] = '\0';
                asset_demo_cfg_loaded = 1;
            }
        }
        SDL_RWclose(rw_cfg);
    }

    /* 2. Load test.bmp via SDL_LoadBMP */
    SDL_Surface *bmp_surface = SDL_LoadBMP("test.bmp");
    SDL_Texture *bmp_texture = NULL;
    if (bmp_surface) {
        if (bmp_surface->w == 64 && bmp_surface->h == 64) {
            asset_demo_bmp_loaded = 1;
        }
        bmp_texture = SDL_CreateTextureFromSurface(renderer, bmp_surface);
        SDL_FreeSurface(bmp_surface);
    } else {
        asset_demo_err = 0x201;
    }

    /* 3. Load test.wav via SDL_LoadWAV and start audio playback */
    SDL_AudioSpec wav_spec;
    Uint8 *wav_buf = NULL;
    Uint32 wav_len = 0;
    SDL_AudioDeviceID audio_dev = 0;
    if (SDL_LoadWAV("test.wav", &wav_spec, &wav_buf, &wav_len) != NULL) {
        asset_demo_wav_loaded = 1;
        audio_dev = SDL_OpenAudioDevice(NULL, 0, &wav_spec, NULL, 0);
        if (audio_dev > 0) {
            SDL_QueueAudio(audio_dev, wav_buf, wav_len);
            SDL_PauseAudioDevice(audio_dev, 0);
        }
    }

    asset_demo_active = 1;

    int sprite_x = 128;
    int sprite_y = 88;
    int vel_x = 2;
    int vel_y = 1;

    while (1) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                break;
            }
        }

        /* Update bounce physics */
        sprite_x += vel_x;
        sprite_y += vel_y;
        if (sprite_x <= 0) { sprite_x = 0; vel_x = -vel_x; }
        else if (sprite_x >= 320 - 64) { sprite_x = 320 - 64; vel_x = -vel_x; }
        if (sprite_y <= 0) { sprite_y = 0; vel_y = -vel_y; }
        else if (sprite_y >= 240 - 64) { sprite_y = 240 - 64; vel_y = -vel_y; }

        /* Clear background */
        SDL_SetRenderDrawColor(renderer, 24, 40, 72, 255);
        SDL_RenderClear(renderer);

        /* Render loaded sprite texture */
        if (bmp_texture) {
            SDL_Rect dst_rect = { sprite_x, sprite_y, 64, 64 };
            SDL_RenderCopy(renderer, bmp_texture, NULL, &dst_rect);
        } else {
            /* Fallback placeholder */
            SDL_SetRenderDrawColor(renderer, 220, 180, 50, 255);
            SDL_Rect r = { sprite_x, sprite_y, 64, 64 };
            SDL_RenderFillRect(renderer, &r);
        }

        SDL_RenderPresent(renderer);
        asset_demo_frames++;

        /* Loop audio playback */
        if (audio_dev > 0 && wav_buf && wav_len > 0) {
            Uint32 queued = SDL_GetQueuedAudioSize(audio_dev);
            if (queued < wav_len / 2) {
                SDL_QueueAudio(audio_dev, wav_buf, wav_len);
            }
        }
    }

loop:
    while (1) {
        __asm__ volatile("nop");
    }
}

