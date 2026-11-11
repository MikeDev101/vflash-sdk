#include <SDL.h>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <array>
#include <utility>

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
#include "vflash/mmu.h"
#endif
#include "vflash/clock.h"

extern "C" {
    volatile uint32_t startup_status = 0;
    volatile uint32_t boot_marker = 0;
    volatile uint32_t data_cookie = 0x13579BDFu;

    volatile uint32_t cpp_global_ctor_runs = 0;
    volatile uint32_t cpp_virtual_calls = 0;
    volatile uint32_t cpp_template_ok = 0;
    volatile uint32_t cpp_dynamic_alloc_ok = 0;
    volatile uint32_t cpp_sdl2_frames = 0;
    volatile uint32_t cpp_all_passed = 0;
}

/* 1. Global constructor demonstration */
class GlobalInitSentinel {
public:
    uint32_t signature;
    GlobalInitSentinel() : signature(0x43505031u) { /* "CPP1" */
        cpp_global_ctor_runs++;
    }
};

static GlobalInitSentinel g_sentinel;

/* 2. Template class demonstration */
template <typename T>
class Vec2 {
public:
    T x, y;
    constexpr Vec2(T px = 0, T py = 0) : x(px), y(py) {}
    Vec2 operator+(const Vec2 &o) const { return Vec2(x + o.x, y + o.y); }
    Vec2 operator*(T scalar) const { return Vec2(x * scalar, y * scalar); }
    bool operator==(const Vec2 &o) const { return x == o.x && y == o.y; }
};

template <typename T>
constexpr T clamp_val(T v, T lo, T hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

/* 3. Polymorphic class hierarchy */
class Entity {
public:
    virtual ~Entity() = default;
    virtual void update() = 0;
    virtual void render(SDL_Renderer *renderer) = 0;
};

class BoxEntity : public Entity {
protected:
    Vec2<int> m_pos;
    Vec2<int> m_vel;
    int m_w, m_h;
    uint8_t m_r, m_g, m_b;

public:
    BoxEntity(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b)
        : m_pos(x, y), m_vel(2, 1), m_w(w), m_h(h), m_r(r), m_g(g), m_b(b) {}

    void update() override {
        m_pos = m_pos + m_vel;
        if (m_pos.x <= 10 || m_pos.x + m_w >= 310) m_vel.x = -m_vel.x;
        if (m_pos.y <= 10 || m_pos.y + m_h >= 230) m_vel.y = -m_vel.y;
        cpp_virtual_calls++;
    }

    void render(SDL_Renderer *renderer) override {
        SDL_SetRenderDrawColor(renderer, m_r, m_g, m_b, 255);
        SDL_Rect rc = { m_pos.x, m_pos.y, m_w, m_h };
        SDL_RenderFillRect(renderer, &rc);
    }
};

class HighlightEntity : public BoxEntity {
public:
    HighlightEntity(int x, int y, int w, int h)
        : BoxEntity(x, y, w, h, 0, 230, 118) {
        m_vel = Vec2<int>(1, 2);
    }

    void render(SDL_Renderer *renderer) override {
        BoxEntity::render(renderer);
        /* Inner accent */
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_Rect inner = { m_pos.x + 2, m_pos.y + 2, m_w - 4, m_h - 4 };
        SDL_RenderDrawRect(renderer, &inner);
    }
};

/* Helper factory for unique_ptr */
template <typename T, typename... Args>
std::unique_ptr<T> make_unique_obj(Args&&... args) {
    return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
}

extern "C" void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu) ? 0x600D600Du : 0;
    boot_marker = 0x56464C53u;

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
    vflash_mmu_init();
    vflash_mmu_identity();
    vflash_mmu_map_section(0xA8000000u, 0xA8000000u, 1);
    vflash_mmu_enable();
#endif

    /* Check global constructor */
    bool ctor_ok = (g_sentinel.signature == 0x43505031u) && (cpp_global_ctor_runs == 1);

    /* Check templates */
    Vec2<int> v1(10, 20);
    Vec2<int> v2(5, -5);
    Vec2<int> v3 = (v1 + v2) * 2;
    int clamped = clamp_val(150, 0, 100);
    bool template_ok = (v3 == Vec2<int>(30, 30)) && (clamped == 100);
    if (template_ok) {
        cpp_template_ok = 1;
    }

    /* Check dynamic allocation: new, new[], delete, delete[] */
    bool alloc_ok = false;
    {
        int *dynamic_array = new int[16];
        for (int i = 0; i < 16; ++i) {
            dynamic_array[i] = (i + 1) * 3;
        }
        int sum = 0;
        for (int i = 0; i < 16; ++i) {
            sum += dynamic_array[i];
        }
        delete[] dynamic_array;

        auto p_box = make_unique_obj<BoxEntity>(40, 50, 24, 24, 255, 82, 82);
        p_box->update();

        alloc_ok = (sum == 408) && (cpp_virtual_calls >= 1);
    }
    if (alloc_ok) {
        cpp_dynamic_alloc_ok = 1;
    }

    /* Initialize SDL2 Video */
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        for (;;) {}
    }

    SDL_Window *window = nullptr;
    SDL_Renderer *renderer = nullptr;
    if (SDL_CreateWindowAndRenderer(320, 240, 0, &window, &renderer) != 0) {
        for (;;) {}
    }

    /* Allocate polymorphic entities dynamically */
    std::unique_ptr<Entity> entities[3];
    entities[0] = make_unique_obj<BoxEntity>(30, 40, 36, 24, 68, 138, 255);
    entities[1] = make_unique_obj<BoxEntity>(120, 80, 28, 28, 255, 214, 0);
    entities[2] = make_unique_obj<HighlightEntity>(200, 120, 32, 32);

    /* Main render loop */
    while (1) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {}

        for (int i = 0; i < 3; ++i) {
            entities[i]->update();
        }

        /* Clear background to dark navy */
        SDL_SetRenderDrawColor(renderer, 14, 22, 36, 255);
        SDL_RenderClear(renderer);

        /* Draw top banner panel */
        SDL_SetRenderDrawColor(renderer, 26, 44, 78, 255);
        SDL_Rect header = { 0, 0, 320, 24 };
        SDL_RenderFillRect(renderer, &header);

        /* Draw status indicators */
        SDL_SetRenderDrawColor(renderer, 0, 229, 255, 255);
        SDL_Rect bar = { 8, 8, 8, 8 };
        SDL_RenderFillRect(renderer, &bar);

        /* Draw active entities */
        for (int i = 0; i < 3; ++i) {
            entities[i]->render(renderer);
        }

        SDL_RenderPresent(renderer);
        cpp_sdl2_frames++;

        if (ctor_ok && cpp_template_ok && cpp_dynamic_alloc_ok && cpp_virtual_calls >= 10) {
            cpp_all_passed = 1;
        }
    }
}

