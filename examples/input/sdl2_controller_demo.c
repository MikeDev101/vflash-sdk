#include <SDL.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef VFLASH_ENABLE_MMU
#include "vflash/mmu.h"
#include "vflash/panic.h"
#endif

/* Standard diagnostic probe symbols in SDRAM */
volatile uint32_t startup_status;
volatile uint32_t boot_marker;
volatile uint32_t data_cookie = 0x13579BDFu;
volatile uint32_t video_status;
volatile uint32_t render_status;

/* SDL2 Joystick / GameController diagnostic symbols */
volatile uint32_t sdl2_input_status;
volatile uint32_t sdl2_num_joysticks;
volatile uint32_t sdl2_joy_opened;
volatile uint32_t sdl2_controller_opened;
volatile uint32_t sdl2_axis_motion_count;
volatile uint32_t sdl2_button_down_count;
volatile uint32_t sdl2_button_up_count;
volatile uint32_t sdl2_controller_axis_count;
volatile uint32_t sdl2_controller_button_down_count;
volatile uint32_t sdl2_controller_button_up_count;
volatile uint32_t sdl2_frames;
volatile int32_t  sdl2_last_axis_x;
volatile int32_t  sdl2_last_axis_y;
volatile uint32_t sdl2_buttons_seen;

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu) ? 0x600D600Du : 0xBAD00001u;
    boot_marker = 0x56464C53u;

#ifdef VFLASH_ENABLE_MMU
    VFLASH_ASSERT(vflash_mmu_identity() == VFLASH_MMU_OK);
    VFLASH_ASSERT(vflash_mmu_enable() == VFLASH_MMU_OK);
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
        video_status = 0xBAD00002u;
        for (;;) __asm__ volatile("nop");
    }
    video_status = 0x56494431u;
    render_status = 0x44524157u;

    sdl2_num_joysticks = (uint32_t)SDL_NumJoysticks();
    sdl2_joy_opened = 0;
    sdl2_controller_opened = 0;
    sdl2_axis_motion_count = 0;
    sdl2_button_down_count = 0;
    sdl2_button_up_count = 0;
    sdl2_controller_axis_count = 0;
    sdl2_controller_button_down_count = 0;
    sdl2_controller_button_up_count = 0;
    sdl2_frames = 0;
    sdl2_last_axis_x = 0;
    sdl2_last_axis_y = 0;
    sdl2_buttons_seen = 0;

    SDL_Joystick *joy = NULL;
    if (sdl2_num_joysticks > 0) {
        joy = SDL_JoystickOpen(0);
        if (joy) {
            sdl2_joy_opened = 1;
        }
    }

    SDL_GameController *controller = NULL;
    if (SDL_IsGameController(0)) {
        controller = SDL_GameControllerOpen(0);
        if (controller) {
            sdl2_controller_opened = 1;
        }
    }

    SDL_Window *window = SDL_CreateWindow("V.Flash SDL2 Input",
                                          SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                          320, 240, 0);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);

    sdl2_input_status = 0x5D121900u;

    int cur_x = 160;
    int cur_y = 120;

    for (;;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_JOYAXISMOTION:
                sdl2_axis_motion_count++;
                if (ev.jaxis.axis == 0) {
                    sdl2_last_axis_x = ev.jaxis.value;
                } else if (ev.jaxis.axis == 1) {
                    sdl2_last_axis_y = ev.jaxis.value;
                }
                break;

            case SDL_JOYBUTTONDOWN:
                sdl2_button_down_count++;
                if (ev.jbutton.button < 32) {
                    sdl2_buttons_seen |= (1u << ev.jbutton.button);
                }
                break;

            case SDL_JOYBUTTONUP:
                sdl2_button_up_count++;
                break;

            case SDL_CONTROLLERAXISMOTION:
                sdl2_controller_axis_count++;
                break;

            case SDL_CONTROLLERBUTTONDOWN:
                sdl2_controller_button_down_count++;
                break;

            case SDL_CONTROLLERBUTTONUP:
                sdl2_controller_button_up_count++;
                break;

            case SDL_QUIT:
                goto cleanup;
            }
        }

        /* Update cursor position from analog stick */
        cur_x += ((int)sdl2_last_axis_x * 8) >> 15;
        cur_y += ((int)sdl2_last_axis_y * 8) >> 15;
        if (cur_x < 16) cur_x = 16;
        if (cur_x > 304) cur_x = 304;
        if (cur_y < 16) cur_y = 16;
        if (cur_y > 224) cur_y = 224;

        /* Render scene */
        SDL_SetRenderDrawColor(renderer, 24, 30, 40, 255);
        SDL_RenderClear(renderer);

        /* Draw playfield bounding box */
        SDL_SetRenderDrawColor(renderer, 60, 75, 95, 255);
        SDL_Rect bounds = { 10, 10, 300, 220 };
        SDL_RenderDrawRect(renderer, &bounds);

        /* Draw crosshairs in center */
        SDL_SetRenderDrawColor(renderer, 40, 50, 65, 255);
        SDL_RenderDrawLine(renderer, 10, 120, 310, 120);
        SDL_RenderDrawLine(renderer, 160, 10, 160, 230);

        /* Draw cursor */
        SDL_SetRenderDrawColor(renderer, 255, 220, 0, 255);
        SDL_Rect cursor = { cur_x - 6, cur_y - 6, 13, 13 };
        SDL_RenderFillRect(renderer, &cursor);

        SDL_RenderPresent(renderer);
        sdl2_frames++;
    }

cleanup:
    if (controller) SDL_GameControllerClose(controller);
    if (joy) SDL_JoystickClose(joy);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
