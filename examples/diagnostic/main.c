#include "SDL.h"
#include "vflash/audio.h"
#include "vflash/clock.h"
#include "vflash/input.h"
#include "vflash/mmu.h"
#include "vflash/renderer.h"
#include <stdint.h>

extern int vflash_sdl2_get_stats(SDL_Renderer *renderer, uint32_t *lists,
                                 uint32_t *sub_ticks, uint32_t *comp_ticks);

volatile uint32_t diag_magic = 0;
volatile uint32_t diag_frames_completed = 0;
volatile uint32_t diag_mmu_control = 0;
volatile uint32_t diag_elapsed_ticks = 0;
volatile uint32_t diag_gpu_lists = 0;
volatile uint32_t diag_gpu_sub_ticks = 0;
volatile uint32_t diag_gpu_comp_ticks = 0;
volatile uint32_t diag_audio_samples = 0;
volatile uint32_t diag_audio_underruns = 0;
volatile uint32_t diag_input_polls = 0;

static void fill_sprite_pixels(uint16_t *buf)
{
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (x == 0 || y == 0 || x == 15 || y == 15) {
                buf[y * 16 + x] = 0x0000u;
            } else if (x > 4 && x < 11 && y > 4 && y < 11) {
                buf[y * 16 + x] = 0x03FFu;
            } else {
                buf[y * 16 + x] = 0x7C1Fu;
            }
        }
    }
}

static void fill_stamp_pixels(uint16_t *buf)
{
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            int dx = x - 8;
            int dy = y - 8;
            if (dx * dx + dy * dy <= 36) {
                buf[y * 16 + x] = 0x80F4u;
            } else {
                buf[y * 16 + x] = 0x0000u;
            }
        }
    }
}

static void push_audio_frame(void)
{
    int16_t pcm[368];
    static uint32_t phase = 0;
    for (uint32_t i = 0; i < 368u; ++i) {
        pcm[i] = ((phase & 32u) != 0) ? 1200 : -1200;
        phase++;
    }
    if (vflash_audio_get_available_bytes() >= sizeof(pcm)) {
        vflash_audio_write(pcm, sizeof(pcm));
    }
}

int vflash_main(void)
{

#if defined(DIAG_MMU)
    if (vflash_mmu_identity() != VFLASH_MMU_OK ||
        vflash_mmu_enable() != VFLASH_MMU_OK) {
        diag_magic = 0xDEAD0001u;
        return 1;
    }
    diag_mmu_control = vflash_mmu_control();
#else
    uint32_t cp15_c1 = 0;
    __asm__ volatile("mrc p15, 0, %0, c1, c0, 0" : "=r"(cp15_c1));
    diag_mmu_control = cp15_c1;
#endif

    vflash_clock_init();

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) < 0) {
        diag_magic = 0xDEAD0002u;
        return 1;
    }

    SDL_Window *win = SDL_CreateWindow("diag", 0, 0, 320, 240, 0);
    SDL_Renderer *ren = SDL_CreateRenderer(
        win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!win || !ren) {
        diag_magic = 0xDEAD0003u;
        return 1;
    }

    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(ren, &info) != 0 ||
        (info.flags & SDL_RENDERER_TARGETTEXTURE) == 0) {
        diag_magic = 0xDEAD0004u;
        return 1;
    }

    uint16_t sprite_px[16 * 16];
    uint16_t stamp_px[16 * 16];
    fill_sprite_pixels(sprite_px);
    fill_stamp_pixels(stamp_px);

    SDL_Texture *sprite_tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_BGR555, SDL_TEXTUREACCESS_STATIC, 16, 16);
    SDL_Texture *stamp_tex = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_ABGR4444, SDL_TEXTUREACCESS_STATIC, 16, 16);
    SDL_Texture *pen_target = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_BGR555, SDL_TEXTUREACCESS_TARGET, 160, 120);
    SDL_Texture *scratch_target = SDL_CreateTexture(
        ren, SDL_PIXELFORMAT_BGR555, SDL_TEXTUREACCESS_TARGET, 64, 64);

    if (!sprite_tex || !stamp_tex || !pen_target || !scratch_target) {
        diag_magic = 0xDEAD0005u;
        return 1;
    }

    SDL_SetTextureBlendMode(sprite_tex, SDL_BLENDMODE_ADD);
    SDL_SetTextureBlendMode(stamp_tex, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(pen_target, SDL_BLENDMODE_BLEND);
    SDL_SetTextureBlendMode(scratch_target, SDL_BLENDMODE_NONE);

    if (SDL_UpdateTexture(sprite_tex, NULL, sprite_px, 16 * 2) != 0 ||
        SDL_UpdateTexture(stamp_tex, NULL, stamp_px, 16 * 2) != 0) {
        diag_magic = 0xDEAD0006u;
        return 1;
    }

    vflash_audio_config_t acfg = {
        .sample_rate = 22050,
        .channels = 1,
        .format = VFLASH_AUDIO_FMT_S16LE,
        .buffer_samples = 2048
    };
    vflash_audio_init(&acfg);
    for (int i = 0; i < 6; ++i) {
        push_audio_frame();
    }

    if (SDL_SetRenderTarget(ren, pen_target) != 0) {
        diag_magic = 0xDEAD0007u;
        return 1;
    }
    SDL_SetRenderDrawColor(ren, 0, 0, 0, 0);
    SDL_RenderClear(ren);

    if (SDL_SetRenderTarget(ren, scratch_target) != 0) {
        diag_magic = 0xDEAD0008u;
        return 1;
    }
    SDL_SetRenderDrawColor(ren, 16, 48, 128, 255);
    SDL_RenderClear(ren);
    SDL_Rect box = {12, 12, 40, 40};
    SDL_SetRenderDrawColor(ren, 255, 220, 40, 255);
    SDL_RenderFillRect(ren, &box);
    SDL_SetRenderTarget(ren, NULL);

    uint16_t start_ticks = vflash_clock_ticks();
    uint32_t total_ticks = 0;
    uint16_t prev_ticks = start_ticks;

    for (uint32_t frame = 0; frame < 2100u; ++frame) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
        }
        diag_input_polls++;
        push_audio_frame();

        if (SDL_SetRenderTarget(ren, pen_target) != 0) {
            diag_magic = 0xDEAD0009u;
            return 1;
        }
        int px = (int)((frame * 3u) % 152u);
        int py = (int)((frame * 2u) % 112u);
        int nx = (int)(((frame + 1u) * 3u) % 152u);
        int ny = (int)(((frame + 1u) * 2u) % 112u);
        SDL_SetRenderDrawColor(ren, (Uint8)(80u + (frame & 127u)),
                               (Uint8)(200u - (frame & 63u)), 255, 255);
        if (nx > px && ny > py) {
            SDL_RenderDrawLine(ren, px, py, nx, ny);
        } else {
            SDL_Rect dot = {px, py, 3, 3};
            SDL_RenderFillRect(ren, &dot);
        }
        if ((frame % 120u) == 0u) {
            SDL_Rect sdst = {px, py, 16, 16};
            SDL_RenderCopy(ren, stamp_tex, NULL, &sdst);
        }

        SDL_SetRenderTarget(ren, scratch_target);
        SDL_Rect tick_dot = {(int)(frame & 31u) + 16, 28, 4, 4};
        SDL_SetRenderDrawColor(ren, 255, 80, 80, 255);
        SDL_RenderFillRect(ren, &tick_dot);

        SDL_SetRenderTarget(ren, NULL);
        SDL_SetRenderDrawColor(ren, 24, 32, 48, 255);
        SDL_RenderClear(ren);

        SDL_Rect pen_dst = {20, 40, 160, 120};
        SDL_RenderCopy(ren, pen_target, NULL, &pen_dst);

        SDL_Rect scratch_dst = {230, 24, 64, 64};
        SDL_RenderCopy(ren, scratch_target, NULL, &scratch_dst);

        SDL_Rect spr_dst = {(int)(20 + ((frame * 2u) % 260u)),
                            (int)(40 + ((frame * 1u) % 140u)), 16, 16};
        SDL_RenderCopy(ren, sprite_tex, NULL, &spr_dst);

        SDL_RenderPresent(ren);

        uint16_t now = vflash_clock_ticks();
        total_ticks += (uint16_t)(now - prev_ticks);
        prev_ticks = now;
        diag_frames_completed = frame + 1u;
    }

    diag_elapsed_ticks = total_ticks;
    uint32_t lists = 0, sub = 0, comp = 0;
    vflash_sdl2_get_stats(ren, &lists, &sub, &comp);
    diag_gpu_lists = lists;
    diag_gpu_sub_ticks = sub;
    diag_gpu_comp_ticks = comp;
    vflash_audio_stats_t astats;
    vflash_audio_get_stats(&astats);
    diag_audio_samples = astats.samples_played;
    diag_audio_underruns = astats.underrun_count;
    diag_magic = 0x44494147u;

    for (;;) {
        SDL_RenderPresent(ren);
    }
    return 0;
}
