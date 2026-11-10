import tempfile, subprocess, unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class RendererTests(unittest.TestCase):
    def test_renderer_host_contracts_and_parameter_validation(self):
        code = r'''
#include "vflash/renderer.h"
#include <assert.h>
#include <string.h>

int main(void) {
    vflash_renderer_t r;
    memset(&r, 0, sizeof(r));

    /* 1. Uninitialized calls reject cleanly with NOT_INITIALIZED */
    vflash_texture_t tex;
    uint16_t dummy_pix[64];
    assert(vflash_renderer_shutdown(&r) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_reset_atlas(&r) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_destroy_texture(&r, &tex) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_clear(&r, 0) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_fill_rect(&r, 0, 0, 10, 10, 0) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_set_clip(&r, 0, 0, 10, 10) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_reset_clip(&r) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_upload_texture(&r, &tex, dummy_pix, 8, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_draw(&r, &tex, 0, 0) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_draw_sub(&r, &tex, 0, 0, 8, 8, 0, 0) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_present(&r) == VFLASH_GPU_NOT_INITIALIZED);

    /* 2. Null config or renderer rejects with INVALID */
    assert(vflash_renderer_init(NULL, NULL) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_init(&r, NULL) == VFLASH_GPU_INVALID);

    /* 3. Config validation checks */
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

    /* Null surface pointers */
    vflash_renderer_config_t bad = cfg;
    bad.surface_a = NULL;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.surface_b = NULL;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    /* Misaligned surface pointer */
    bad = cfg;
    bad.surface_a = (void *)((uintptr_t)surf_a + 4);
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    /* Invalid surface geometry */
    bad = cfg;
    bad.surface_width = 100; /* not multiple of 32 */
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.surface_height = 30; /* not multiple of 8 */
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    /* Invalid playfield geometry */
    bad = cfg;
    bad.playfield_width = 160; /* > surface_width */
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.playfield_width = 50; /* not multiple of 32 */
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.playfield_height = 40; /* > surface_height */
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    /* Screen placement overflow */
    bad = cfg;
    bad.screen_x = 300; /* 300 + 96 > 320 */
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.screen_y = 220; /* 220 + 32 > 240 */
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.screen_x = -1;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    /* Atlas placement overflow */
    bad = cfg;
    bad.atlas_x = 110; /* 110 + 32 > 128 */
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.atlas_y = 10; /* 10 + 32 > 32 */
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    /* Insufficient surface bytes */
    bad = cfg;
    bad.surface_bytes = 1024;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    /* 4. Mock initialized renderer for draw, lifecycle, and upload validation */
    r.initialized = 1;
    r.generation_id = 1;
    r.config = cfg;
    r.alloc_x = cfg.atlas_x;
    r.alloc_y = cfg.atlas_y;
    r.alloc_row_h = 0;

    /* Texture upload validation */
    assert(vflash_renderer_upload_texture(&r, NULL, dummy_pix, 8, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_upload_texture(&r, &tex, NULL, 8, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_upload_texture(&r, &tex, dummy_pix, 0, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_upload_texture(&r, &tex, dummy_pix, 7, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_INVALID); /* odd width */
    assert(vflash_renderer_upload_texture(&r, &tex, dummy_pix, 8, 8, (vflash_texture_type_t)99) == VFLASH_GPU_INVALID); /* invalid type */
    assert(vflash_renderer_upload_texture(&r, &tex, dummy_pix, 64, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_INVALID); /* wider than atlas */

    /* Allocation exhaustion returns OUT_OF_MEMORY without altering alloc pointers */
    r.alloc_y = cfg.atlas_y + cfg.atlas_height - 4; /* only 4 lines remain */
    uint32_t saved_x = r.alloc_x, saved_y = r.alloc_y, saved_row_h = r.alloc_row_h;
    assert(vflash_renderer_upload_texture(&r, &tex, dummy_pix, 8, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_OUT_OF_MEMORY);
    assert(r.alloc_x == saved_x && r.alloc_y == saved_y && r.alloc_row_h == saved_row_h);
    r.alloc_y = cfg.atlas_y;

    /* Texture destruction and lifecycle validation */
    tex.width = 16; tex.height = 16; tex.atlas_x = 96; tex.atlas_y = 0; tex.type = VFLASH_TEXTURE_OPAQUE; tex.id = 1;
    assert(vflash_renderer_destroy_texture(NULL, &tex) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_destroy_texture(&r, NULL) == VFLASH_GPU_INVALID);
    vflash_texture_t bad_tex = tex;
    bad_tex.id = 99; /* mismatched generation */
    assert(vflash_renderer_destroy_texture(&r, &bad_tex) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_destroy_texture(&r, &tex) == VFLASH_GPU_OK);
    assert(tex.id == 0 && tex.width == 0 && tex.height == 0);
    assert(vflash_renderer_destroy_texture(&r, &tex) == VFLASH_GPU_INVALID); /* already destroyed */

    /* Draw validation */
    assert(vflash_renderer_draw(&r, NULL, 0, 0) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_draw(&r, &tex, 0, 0) == VFLASH_GPU_INVALID); /* destroyed texture */
    tex.width = 16; tex.height = 16; tex.atlas_x = 96; tex.atlas_y = 0; tex.type = VFLASH_TEXTURE_OPAQUE; tex.id = 99;
    assert(vflash_renderer_draw(&r, &tex, 0, 0) == VFLASH_GPU_INVALID); /* stale generation */
    tex.id = 1; /* valid generation */
    assert(vflash_renderer_draw_sub(&r, NULL, 0, 0, 16, 16, 0, 0) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_draw_sub(&r, &tex, -1, 0, 16, 16, 0, 0) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_draw_sub(&r, &tex, 0, -1, 16, 16, 0, 0) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_draw_sub(&r, &tex, 0, 0, 20, 16, 0, 0) == VFLASH_GPU_INVALID); /* sub-rect exceeds texture */
    assert(vflash_renderer_draw_sub(&r, &tex, 8, 0, 12, 16, 0, 0) == VFLASH_GPU_INVALID);

    return 0;
}
'''
        with tempfile.TemporaryDirectory() as temp:
            src = Path(temp) / 'test.c'
            src.write_text(code)
            exe = Path(temp) / 'test'
            cmd = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                   '-I', str(ROOT / 'include'), str(src),
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

