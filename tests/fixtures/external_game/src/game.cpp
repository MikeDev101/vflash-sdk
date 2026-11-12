#include "game.hpp"
#include <algorithm>

DuckGame::DuckGame() = default;

DuckGame::~DuckGame() {
    if (m_controller) {
        SDL_GameControllerClose(m_controller);
        m_controller = nullptr;
    }
    if (m_renderer) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
    if (m_window) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
    SDL_Quit();
}

bool DuckGame::init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        return false;
    }

    m_window = SDL_CreateWindow("Duck Game",
                                SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                320, 240, SDL_WINDOW_SHOWN);
    if (!m_window) return false;

    m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
    if (!m_renderer) return false;

    if (SDL_NumJoysticks() > 0 && SDL_IsGameController(0)) {
        m_controller = SDL_GameControllerOpen(0);
    }

    for (size_t i = 0; i < m_sparks.size(); ++i) {
        m_sparks[i] = Spark{0, 0, 0, 0, 0, 0, 0, 0};
    }

    m_running = true;
    return true;
}

void DuckGame::handle_input() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) {
            m_running = false;
        } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            if (e.cbutton.button == SDL_CONTROLLER_BUTTON_A) {
                /* Jump or flap */
                m_duck_vy = -3;
                m_score += 10;
            } else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT) {
                m_duck_vx = -2;
            } else if (e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
                m_duck_vx = 2;
            }
        }
    }
}

void DuckGame::update() {
    m_duck_x += m_duck_vx;
    m_duck_y += m_duck_vy;

    /* Screen bounce */
    if (m_duck_x < 8) {
        m_duck_x = 8;
        m_duck_vx = -m_duck_vx;
    } else if (m_duck_x > 320 - 40) {
        m_duck_x = 320 - 40;
        m_duck_vx = -m_duck_vx;
    }

    if (m_duck_y < 16) {
        m_duck_y = 16;
        m_duck_vy = 1;
    } else if (m_duck_y > 240 - 40) {
        m_duck_y = 240 - 40;
        m_duck_vy = -1;
    }

    /* Update particle sparks */
    for (auto& s : m_sparks) {
        if (s.life > 0) {
            s.x += s.vx;
            s.y += s.vy;
            s.life--;
        }
    }

    m_frame_count++;
}

void DuckGame::render() {
    /* Sky background */
    SDL_SetRenderDrawColor(m_renderer, 0x20, 0x48, 0x88, 0xFF);
    SDL_RenderClear(m_renderer);

    /* Draw ground */
    SDL_SetRenderDrawColor(m_renderer, 0x30, 0x70, 0x30, 0xFF);
    SDL_Rect ground{0, 200, 320, 40};
    SDL_RenderFillRect(m_renderer, &ground);

    /* Draw duck body */
    SDL_SetRenderDrawColor(m_renderer, 0xF0, 0xC0, 0x20, 0xFF);
    SDL_Rect duck_rect{m_duck_x, m_duck_y, 32, 24};
    SDL_RenderFillRect(m_renderer, &duck_rect);

    /* Duck beak */
    SDL_SetRenderDrawColor(m_renderer, 0xE0, 0x60, 0x10, 0xFF);
    SDL_Rect beak_rect{m_duck_x + (m_duck_vx >= 0 ? 28 : -8), m_duck_y + 8, 8, 8};
    SDL_RenderFillRect(m_renderer, &beak_rect);

    /* Duck eye */
    SDL_SetRenderDrawColor(m_renderer, 0x00, 0x00, 0x00, 0xFF);
    SDL_Rect eye_rect{m_duck_x + (m_duck_vx >= 0 ? 20 : 8), m_duck_y + 4, 4, 4};
    SDL_RenderFillRect(m_renderer, &eye_rect);

    /* Draw spark particles */
    for (const auto& s : m_sparks) {
        if (s.life > 0) {
            SDL_SetRenderDrawColor(m_renderer, s.r, s.g, s.b, 0xFF);
            SDL_Rect pr{s.x, s.y, 2, 2};
            SDL_RenderFillRect(m_renderer, &pr);
        }
    }

    SDL_RenderPresent(m_renderer);
}
