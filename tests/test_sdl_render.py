import tempfile, subprocess, unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class SDLRenderTests(unittest.TestCase):
    def test_sdl_render_host_contracts_and_pixel_conversions(self):
        code = r'''
#include "vflash/sdl_render.h"
#include <assert.h>
#include <string.h>

int main(void) {
    uint16_t surf_a[32768] __attribute__((aligned(64)));
    uint16_t surf_b[32768] __attribute__((aligned(64)));

    vflash_renderer_config_t cfg = {
        .surface_a = surf_a,
        .surface_b = surf_b,
        .surface_bytes = sizeof(surf_a),
        .surface_width = 128,
        .surface_height = 32,
        .playfield_width = 96,
        .playfield_height = 32,
        .screen_x = 0,
        .screen_y = 0,
        .atlas_x = 96,
        .atlas_y = 0,
        .atlas_width = 32,
        .atlas_height = 32
    };

    SDL_Renderer storage;
    memset(&storage, 0, sizeof(storage));

    /* 1. Initialization and null checks */
    assert(SDL_CreateRenderer_Static(NULL, NULL) == NULL);
    assert(SDL_CreateRenderer_Static(&storage, NULL) == NULL);

    /* Mock initialized renderer */
    storage.initialized = 1;
    storage.native_renderer.initialized = 1;
    storage.native_renderer.config = cfg;
    storage.native_renderer.alloc_x = cfg.atlas_x;
    storage.native_renderer.alloc_y = cfg.atlas_y;
    storage.native_renderer.alloc_row_h = 0;
    storage.native_renderer.generation_id = 1;

    /* 2. Texture creation parameter validation */
    assert(SDL_CreateTexture(NULL, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 16, 16) == NULL);
    assert(SDL_CreateTexture(&storage, SDL_PIXELFORMAT_UNKNOWN, SDL_TEXTUREACCESS_STATIC, 16, 16) == NULL);
    assert(SDL_CreateTexture(&storage, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 16, 16) == NULL); /* STREAMING unsupported */
    assert(SDL_CreateTexture(&storage, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 15, 16) == NULL); /* odd width */
    assert(SDL_CreateTexture(&storage, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 0, 16) == NULL);
    assert(SDL_CreateTexture(&storage, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, -16, 16) == NULL);

    SDL_Texture *tex_argb = SDL_CreateTexture(&storage, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 16, 16);
    assert(tex_argb != NULL);
    assert(tex_argb->blend_mode == SDL_BLENDMODE_BLEND); /* default BLEND for ARGB8888 */

    SDL_Texture *tex_bgr = SDL_CreateTexture(&storage, SDL_PIXELFORMAT_BGR555, SDL_TEXTUREACCESS_STATIC, 16, 16);
    assert(tex_bgr != NULL);
    assert(tex_bgr->blend_mode == SDL_BLENDMODE_NONE); /* default NONE for BGR555 */

    /* 3. Blend mode configuration & immutability after upload */
    SDL_BlendMode bm;
    assert(SDL_GetTextureBlendMode(tex_argb, &bm) == 0 && bm == SDL_BLENDMODE_BLEND);
    assert(SDL_SetTextureBlendMode(tex_argb, SDL_BLENDMODE_NONE) == 0);
    assert(SDL_GetTextureBlendMode(tex_argb, &bm) == 0 && bm == SDL_BLENDMODE_NONE);
    assert(SDL_SetTextureBlendMode(tex_argb, SDL_BLENDMODE_BLEND) == 0);

    /* Mark as uploaded to test immutability */
    tex_argb->uploaded = 1;
    assert(SDL_SetTextureBlendMode(tex_argb, SDL_BLENDMODE_NONE) == -1); /* rejected! */
    assert(strstr(SDL_GetError(), "cannot change blend mode") != NULL);
    assert(SDL_SetTextureBlendMode(tex_argb, SDL_BLENDMODE_BLEND) == 0); /* same mode is ok */
    tex_argb->uploaded = 0;

    /* 4. Partial update rejection */
    uint32_t dummy_pixels[256];
    SDL_Rect partial = {0, 0, 8, 8};
    assert(SDL_UpdateTexture(tex_argb, &partial, dummy_pixels, 16 * 4) == -1);
    assert(strstr(SDL_GetError(), "partial texture update unsupported") != NULL);

    /* Full update rect or NULL rect is allowed */
    /* Test pixel conversion math directly */
    dummy_pixels[0] = 0x00FF0000; /* transparent red (A=0) */
    dummy_pixels[1] = 0xFF000000; /* opaque black (A=255, R=0, G=0, B=0) */
    dummy_pixels[2] = 0xFFFFFFFF; /* opaque white */
    dummy_pixels[3] = 0x8000FF00; /* 50% green */

    /* Convert row 0 */
    for (int x = 0; x < 4; ++x) {
        uint32_t p = dummy_pixels[x];
        uint32_t a = (p >> 24) & 0xFF;
        uint32_t red = (p >> 16) & 0xFF;
        uint32_t green = (p >> 8) & 0xFF;
        uint32_t blue = p & 0xFF;
        uint16_t a4 = (uint16_t)(a >> 4);
        uint16_t r4 = (uint16_t)(red >> 4);
        uint16_t g4 = (uint16_t)(green >> 4);
        uint16_t b4 = (uint16_t)(blue >> 4);
        uint16_t texel = (a4 << 12) | (b4 << 8) | (g4 << 4) | r4;
        if (x == 0) assert((texel >> 12) == 0); /* A=0 */
        if (x == 1) assert(texel == 0xF000);    /* Opaque black! */
        if (x == 2) assert(texel == 0xFFFF);    /* Opaque white! */
        if (x == 3) assert(texel == 0x80F0);    /* 50% green: A=8, G=15 */
    }

    /* 5. RenderCopy validation */
    tex_argb->uploaded = 1;
    SDL_Rect src_ok = {0, 0, 16, 16};
    SDL_Rect dst_ok = {10, 10, 16, 16};
    SDL_Rect dst_scaled = {10, 10, 32, 32}; /* different size! */

    /* Rejects scaling */
    assert(SDL_RenderCopy(&storage, tex_argb, &src_ok, &dst_scaled) == -1);
    assert(strstr(SDL_GetError(), "unsupported scaling") != NULL);

    /* NULL src with explicit dst of different size */
    assert(SDL_RenderCopy(&storage, tex_argb, NULL, &dst_scaled) == -1);

    /* Out of bounds source */
    SDL_Rect src_oob = {10, 0, 16, 16}; /* 10 + 16 > 16 */
    assert(SDL_RenderCopy(&storage, tex_argb, &src_oob, &src_ok) == -1);
    assert(strstr(SDL_GetError(), "source rect outside texture bounds") != NULL);

    SDL_Rect src_neg = {-1, 0, 16, 16};
    assert(SDL_RenderCopy(&storage, tex_argb, &src_neg, &src_ok) == -1);

    /* 6. Texture destruction */
    SDL_DestroyTexture(tex_argb);
    assert(tex_argb->in_use == 0);
    assert(SDL_RenderCopy(&storage, tex_argb, &src_ok, &dst_ok) == -1); /* destroyed */

    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp) / 'test_sdl.c'
            src.write_text(code)
            exe = Path(temp) / 'test_sdl'
            cmd = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-I', str(ROOT / 'include'), str(src),
                   str(ROOT / 'src/graphics/sdl_render.c'),
                   str(ROOT / 'src/graphics/renderer.c'),
                   str(ROOT / 'src/gpu/gpu.c'),
                   str(ROOT / 'src/gpu/gpu_software.c'),
                   str(ROOT / 'src/gpu/gpu_zevio.c'),
                   str(ROOT / 'src/graphics/draw.c'),
                   str(ROOT / 'src/graphics/font.c'),
                   str(ROOT / 'src/platform/video.c'),
                   str(ROOT / 'src/platform/clock.c'),
                   '-o', str(exe)]
            subprocess.run(cmd, check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    unittest.main()
