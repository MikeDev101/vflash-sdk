#include "game.hpp"
#include <cstdint>

extern "C" {
__attribute__((used)) volatile uint32_t boot_marker = 0;
__attribute__((used)) volatile uint32_t startup_status = 0;
__attribute__((used)) volatile uint32_t data_cookie = 0x13579BDFu;

__attribute__((used)) volatile uint32_t game_status = 0;   /* 0x47414D45 ("GAME") -> 0x50415353 ("PASS") */
__attribute__((used)) volatile uint32_t game_frames = 0;
__attribute__((used)) volatile uint32_t game_score = 0;

void vflash_main(void) {
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu) ? 0x600D600Du : 0u;
    boot_marker = 0x56464C53u;

    DuckGame game;
    if (!game.init()) {
        game_status = 0xDEADBEEFu;
        while (1) {}
    }

    game_status = 0x47414D45u; /* "GAME" */

    while (game.is_running()) {
        game.handle_input();
        game.update();
        game.render();

        game_frames = game.frame_count();
        game_score = static_cast<uint32_t>(game.score());

        if (game_frames >= 30) {
            game_status = 0x50415353u; /* "PASS" */
        }
    }
}
}
