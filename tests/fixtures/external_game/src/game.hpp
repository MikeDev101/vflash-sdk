#pragma once

#include <SDL.h>
#include <array>
#include <cstdint>

class DuckGame {
public:
    DuckGame();
    ~DuckGame();

    bool init();
    void handle_input();
    void update();
    void render();

    bool is_running() const { return m_running; }
    uint32_t frame_count() const { return m_frame_count; }
    int score() const { return m_score; }

private:
    SDL_Window* m_window{nullptr};
    SDL_Renderer* m_renderer{nullptr};
    SDL_GameController* m_controller{nullptr};
    bool m_running{false};
    uint32_t m_frame_count{0};

    int m_duck_x{144};
    int m_duck_y{104};
    int m_duck_vx{2};
    int m_duck_vy{1};
    int m_score{0};

    struct Spark {
        int x, y;
        int vx, vy;
        uint8_t r, g, b;
        int life;
    };
    std::array<Spark, 16> m_sparks{};
};
