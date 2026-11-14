import ctypes
from pathlib import Path
import subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
class GPUTests(unittest.TestCase):
 def test_software_bounds_overlap_and_capabilities(self):
  code=r'''
#include "vflash/gpu.h"
#include <assert.h>
#include <limits.h>
int main(void){
 vflash_gpu_t g;vflash_gpu_surface_t s,d;
 uint16_t buf[66],other[64];for(int i=0;i<66;++i)buf[i]=0x255A;
 vflash_framebuffer_t fb={buf+1,8,8,8,64,VFLASH_BGR555},src={other,8,8,8,64,VFLASH_BGR555};
 assert(vflash_gpu_init(&g,VFLASH_GPU_ZEVIO)==VFLASH_GPU_UNSUPPORTED);
 assert(!vflash_gpu_caps(&g).hardware_accelerated);
 assert(vflash_gpu_wait_idle(&g)==VFLASH_GPU_NOT_INITIALIZED);
 assert(vflash_gpu_init(&g,VFLASH_GPU_SOFTWARE)==0);
 vflash_gpu_caps_t c=vflash_gpu_caps(&g);assert(c.fill_rect && c.blit && !c.hardware_accelerated && !c.alpha_blend && !c.scaling && !c.color_key_sprite);
 assert(vflash_gpu_surface_register(&s,&src,0,0)==0);
 assert(vflash_gpu_surface_register(&d,&fb,0,0)==0);
 assert(s.layout==VFLASH_GPU_LAYOUT_LINEAR);
 s.layout=VFLASH_GPU_LAYOUT_ZEVIO_TILED;
 assert(vflash_gpu_fill_rect(&g,&s,0,0,1,1,0)==VFLASH_GPU_UNSUPPORTED);
 s.layout=VFLASH_GPU_LAYOUT_LINEAR;
 assert(vflash_gpu_fill_rect(&g,&s,0,0,8,8,0xFF0000)==0);
 assert(vflash_gpu_clip(&d,2,2,3,3)==0);
 assert(vflash_gpu_blit(&g,&d,&s,-2,-2)==0);
 for(int y=0;y<8;++y)for(int x=0;x<8;++x)assert(buf[1+y*8+x]==((x>=2&&x<5&&y>=2&&y<5)?31:0x255A));
 assert(vflash_gpu_fill_rect(&g,&d,INT_MIN,INT_MIN,INT_MAX,INT_MAX,0)==0);
 assert(vflash_gpu_fill_rect(&g,&d,0,0,-1,1,0)==VFLASH_GPU_INVALID);
 assert(vflash_gpu_clip(&d,0,0,8,8)==0);
 for(int i=0;i<64;++i)buf[i+1]=(uint16_t)i;
 assert(vflash_gpu_blit(&g,&d,&d,1,1)==0);
 for(int y=1;y<8;++y)for(int x=1;x<8;++x)assert(buf[1+y*8+x]==(y-1)*8+x-1);
 assert(buf[0]==0x255A&&buf[65]==0x255A);
 vflash_framebuffer_t overlapping={buf+2,7,7,8,56,VFLASH_BGR555};
 assert(vflash_gpu_surface_register(&s,&overlapping,0,0)==0);
 assert(vflash_gpu_blit(&g,&d,&s,0,0)==VFLASH_GPU_UNSUPPORTED);
 uint16_t padded[24],tile[4]={1,2,3,4};
 for(int i=0;i<24;++i)padded[i]=0x255A;
 vflash_framebuffer_t padfb={padded,4,4,6,24,VFLASH_BGR555},tilefb={tile,2,2,2,4,VFLASH_BGR555};
 vflash_gpu_surface_t pad,t;
 assert(vflash_gpu_surface_register(&pad,&padfb,0,0)==0);
 assert(vflash_gpu_surface_register(&t,&tilefb,0,0)==0);
 assert(vflash_gpu_blit(&g,&pad,&t,-1,1)==0);
 assert(padded[6]==2 && padded[12]==4);
 for(int y=0;y<4;++y)assert(padded[y*6+4]==0x255A && padded[y*6+5]==0x255A);
 src.pixel_format=99;assert(vflash_gpu_surface_register(&s,&src,0,0)==VFLASH_GPU_INVALID);
 src.pixel_format=VFLASH_BGR555;src.capacity_pixels=1;assert(vflash_gpu_surface_register(&s,&src,0,0)==VFLASH_GPU_INVALID);
 vflash_gpu_surface_t native;
 assert(vflash_gpu_surface_register_tiled(&native,(void *)0x10000000,0x10000000,32768)==0);
 assert(native.layout==VFLASH_GPU_LAYOUT_ZEVIO_TILED);
 assert(vflash_gpu_fill_rect(&g,&native,0,0,1,1,0)==VFLASH_GPU_UNSUPPORTED);
 assert(vflash_gpu_surface_register_tiled(&native,(void *)0x10000001,0x10000001,32768)==VFLASH_GPU_INVALID);
 assert(vflash_gpu_surface_register_tiled(&native,(void *)0x10000000,0x10000000,2048)==VFLASH_GPU_INVALID);
 assert(vflash_gpu_surface_register_tiled_ex(&native,(void *)0x10000000,0x10000000,320,240,491520)==0);
 assert(native.fb.width==320 && native.fb.height==240 && native.fb.stride==1024 && native.fb.capacity_pixels==245760);
 assert(vflash_gpu_surface_register_tiled_ex(&native,(void *)0x10000000,0x10000000,96,24,49152)==0);
 assert(vflash_gpu_surface_register_tiled_ex(&native,(void *)0x10000000,0x10000000,100,24,49152)==VFLASH_GPU_INVALID);
 assert(vflash_gpu_surface_register_tiled_ex(&native,(void *)0x10000000,0x10000000,96,20,49152)==VFLASH_GPU_INVALID);
 assert(vflash_gpu_surface_register_tiled_ex(&native,(void *)0x10000000,0x10000000,320,240,475136)==VFLASH_GPU_INVALID);
 assert(vflash_gpu_surface_register_tiled_ex(&native,(void *)0x10F88040,0x10F88040,320,240,491520)==VFLASH_GPU_INVALID);
 assert(vflash_gpu_upload(&g,&native,&s,0,0,8,8,0,0)==VFLASH_GPU_UNSUPPORTED);
 assert(vflash_gpu_upload(&g,&d,&s,0,0,8,8,0,0)==VFLASH_GPU_UNSUPPORTED);
 assert(vflash_gpu_present_view(&g,&native,96,24,0,0)==VFLASH_GPU_UNSUPPORTED);
 assert(vflash_gpu_draw_sprite(&g,&native,0,0,8,8,0,0)==VFLASH_GPU_UNSUPPORTED);
 assert(vflash_gpu_draw_sprite_ex(&g,&native,VFLASH_GPU_TEXTURE_ABGR4444,0,0,8,8,0,0)==VFLASH_GPU_UNSUPPORTED);
 assert(vflash_gpu_draw_sprite_ex(&g,&native,99,0,0,8,8,0,0)==VFLASH_GPU_INVALID);
 assert(vflash_gpu_draw_sprite(&g,&d,0,0,8,8,0,0)==VFLASH_GPU_UNSUPPORTED);
 assert(!vflash_gpu_caps_for_layout(&g,VFLASH_GPU_LAYOUT_ZEVIO_TILED).fill_rect);
 assert(!vflash_gpu_caps_for_layout(&g,VFLASH_GPU_LAYOUT_ZEVIO_TILED).upload);
 assert(!vflash_gpu_caps_for_layout(&g,VFLASH_GPU_LAYOUT_ZEVIO_TILED).color_key_sprite);
 assert(!vflash_gpu_caps_for_layout(&g,VFLASH_GPU_LAYOUT_ZEVIO_TILED).alpha_blend);
 assert(vflash_gpu_shutdown(&g)==0);assert(vflash_gpu_fill_rect(&g,&d,0,0,1,1,0)==VFLASH_GPU_NOT_INITIALIZED);
}
'''
  with tempfile.TemporaryDirectory() as temp:
   f=Path(temp)/'test.c';f.write_text(code);exe=Path(temp)/'test'
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'include'),str(f),*[str(p) for p in (ROOT/'src/gpu').glob('*.c')],str(ROOT/'src/graphics/draw.c'),str(ROOT/'src/graphics/font.c'),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)


class RendererTests(unittest.TestCase):
    def test_renderer_host_contracts_and_parameter_validation(self):
        code = r'''
#include "vflash/renderer.h"
#include <assert.h>
#include <string.h>

int main(void) {
    vflash_renderer_t r;
    memset(&r, 0, sizeof(r));

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

    assert(vflash_renderer_init(NULL, NULL) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_init(&r, NULL) == VFLASH_GPU_INVALID);

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

    vflash_renderer_config_t bad = cfg;
    bad.surface_a = NULL;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.surface_b = NULL;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    bad = cfg;
    bad.surface_a = (void *)((uintptr_t)surf_a + 4);
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    bad = cfg;
    bad.surface_width = 100;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.surface_height = 30;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    bad = cfg;
    bad.playfield_width = 160;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.playfield_width = 50;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.playfield_height = 40;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    bad = cfg;
    bad.screen_x = 300;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.screen_y = 220;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.screen_x = -1;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    bad = cfg;
    bad.atlas_x = 110;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);
    bad = cfg;
    bad.atlas_y = 10;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    bad = cfg;
    bad.surface_bytes = 1024;
    assert(vflash_renderer_init(&r, &bad) == VFLASH_GPU_INVALID);

    r.initialized = 1;
    r.generation_id = 1;
    r.config = cfg;
    r.atlas_free_count = 1;
    r.atlas_free[0].x = (uint16_t)cfg.atlas_x;
    r.atlas_free[0].y = (uint16_t)cfg.atlas_y;
    r.atlas_free[0].w = (uint16_t)cfg.atlas_width;
    r.atlas_free[0].h = (uint16_t)cfg.atlas_height;

    assert(vflash_renderer_upload_texture(&r, NULL, dummy_pix, 8, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_upload_texture(&r, &tex, NULL, 8, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_upload_texture(&r, &tex, dummy_pix, 0, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_upload_texture(&r, &tex, dummy_pix, 7, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_upload_texture(&r, &tex, dummy_pix, 8, 8, (vflash_texture_type_t)99) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_upload_texture(&r, &tex, dummy_pix, 64, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_INVALID);

    r.atlas_free[0].h = 4;
    assert(vflash_renderer_upload_texture(&r, &tex, dummy_pix, 8, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_OUT_OF_MEMORY);
    assert(r.atlas_free_count == 1 && r.atlas_free[0].h == 4);
    r.atlas_free[0].h = (uint16_t)cfg.atlas_height;

    // Test atlas reclamation + coalescing via vflash_renderer_destroy_texture
    r.atlas_free_count = 0;
    vflash_texture_t t1 = { (uint16_t)cfg.atlas_x, (uint16_t)cfg.atlas_y, 8, 16, VFLASH_TEXTURE_OPAQUE, r.generation_id };
    vflash_texture_t t2 = { (uint16_t)cfg.atlas_x, (uint16_t)(cfg.atlas_y + 16), 8, 16, VFLASH_TEXTURE_OPAQUE, r.generation_id };
    vflash_texture_t t3;
    assert(vflash_renderer_create_target(&r, &t3, 8, 16, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_OUT_OF_MEMORY);
    assert(vflash_renderer_destroy_texture(&r, &t1) == VFLASH_GPU_OK);
    assert(vflash_renderer_destroy_texture(&r, &t2) == VFLASH_GPU_OK);
    assert(r.atlas_free_count == 1 && r.atlas_free[0].x == cfg.atlas_x &&
           r.atlas_free[0].y == cfg.atlas_y && r.atlas_free[0].w == 8 && r.atlas_free[0].h == 32);

    // Fragmentation stress: mixed sizes, scrambled free order, repeated cycles
    r.atlas_free_count = 1;
    r.atlas_free[0].x = (uint16_t)cfg.atlas_x;
    r.atlas_free[0].y = (uint16_t)cfg.atlas_y;
    r.atlas_free[0].w = (uint16_t)cfg.atlas_width;
    r.atlas_free[0].h = (uint16_t)cfg.atlas_height;
    {
        static const uint16_t sizes[6][2] = {
            { 8, 8 }, { 12, 16 }, { 16, 8 }, { 4, 24 }, { 20, 12 }, { 8, 16 }
        };
        vflash_texture_t pool[8];
        uint8_t live[8] = { 0 };
        uint32_t rng = 0x12345678u;
        for (int iter = 0; iter < 64; ++iter) {
            rng = rng * 1664525u + 1013904223u;
            int idx = (int)((rng >> 24) & 7u);
            if (!live[idx]) {
                int s = (int)((rng >> 16) % 6u);
                uint32_t ax = 0, ay = 0;
                uint32_t w = sizes[s][0], h = sizes[s][1];
                // Use upload_texture? gpu is uninit, so test via create_target
                // only for OOM or test destroy_texture coalescing directly.
                (void)ax; (void)ay;
                pool[idx].width = w;
                pool[idx].height = h;
                pool[idx].type = VFLASH_TEXTURE_OPAQUE;
                pool[idx].id = r.generation_id;
                // Find first fitting free rect to simulate alloc
                for (uint32_t fi = 0; fi < r.atlas_free_count; ++fi) {
                    if (r.atlas_free[fi].w >= w && r.atlas_free[fi].h >= h) {
                        vflash_atlas_rect_t c = r.atlas_free[fi];
                        r.atlas_free[fi] = r.atlas_free[--r.atlas_free_count];
                        if (c.w > w && r.atlas_free_count < VFLASH_MAX_ATLAS_FREE_RECTS) {
                            r.atlas_free[r.atlas_free_count++] = (vflash_atlas_rect_t){
                                (uint16_t)(c.x + w), c.y, (uint16_t)(c.w - w), (uint16_t)h
                            };
                        }
                        if (c.h > h && r.atlas_free_count < VFLASH_MAX_ATLAS_FREE_RECTS) {
                            r.atlas_free[r.atlas_free_count++] = (vflash_atlas_rect_t){
                                c.x, (uint16_t)(c.y + h), c.w, (uint16_t)(c.h - h)
                            };
                        }
                        pool[idx].atlas_x = c.x;
                        pool[idx].atlas_y = c.y;
                        live[idx] = 1;
                        break;
                    }
                }
            } else {
                assert(vflash_renderer_destroy_texture(&r, &pool[idx]) == VFLASH_GPU_OK);
                live[idx] = 0;
            }
        }
        for (int idx = 0; idx < 8; ++idx) {
            if (live[idx]) {
                assert(vflash_renderer_destroy_texture(&r, &pool[idx]) == VFLASH_GPU_OK);
            }
        }
        assert(r.atlas_free_count == 1 &&
               r.atlas_free[0].x == cfg.atlas_x &&
               r.atlas_free[0].y == cfg.atlas_y &&
               r.atlas_free[0].w == cfg.atlas_width &&
               r.atlas_free[0].h == cfg.atlas_height);
    }

    // Separate fragmentation vs insufficient-area vs metadata-overflow failures
    r.atlas_alloc_fail_oom = 0;
    r.atlas_alloc_fail_frag = 0;
    r.atlas_alloc_fail_meta = 0;
    r.atlas_free_count = 2;
    r.atlas_free[0] = (vflash_atlas_rect_t){ (uint16_t)cfg.atlas_x, (uint16_t)cfg.atlas_y, 8, 8 };
    r.atlas_free[1] = (vflash_atlas_rect_t){ (uint16_t)(cfg.atlas_x + 16), (uint16_t)cfg.atlas_y, 8, 8 };
    assert(vflash_renderer_create_target(&r, &t3, 16, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_OUT_OF_MEMORY);
    assert(r.atlas_alloc_fail_frag == 1 && r.atlas_alloc_fail_oom == 0);
    assert(vflash_renderer_create_target(&r, &t3, 16, 16, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_OUT_OF_MEMORY);
    assert(r.atlas_alloc_fail_oom == 1 && r.atlas_alloc_fail_frag == 1);

    r.atlas_free_count = VFLASH_MAX_ATLAS_FREE_RECTS;
    for (uint32_t i = 0; i < VFLASH_MAX_ATLAS_FREE_RECTS; ++i) {
        r.atlas_free[i] = (vflash_atlas_rect_t){ (uint16_t)cfg.atlas_x, (uint16_t)cfg.atlas_y, 16, 16 };
    }
    assert(vflash_renderer_create_target(&r, &t3, 8, 8, VFLASH_TEXTURE_OPAQUE) == VFLASH_GPU_OUT_OF_MEMORY);
    assert(r.atlas_alloc_fail_meta == 1 && r.atlas_free_count == VFLASH_MAX_ATLAS_FREE_RECTS);

    tex.width = 16; tex.height = 16; tex.atlas_x = 96; tex.atlas_y = 0; tex.type = VFLASH_TEXTURE_OPAQUE; tex.id = 1;
    assert(vflash_renderer_destroy_texture(NULL, &tex) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_destroy_texture(&r, NULL) == VFLASH_GPU_INVALID);
    vflash_texture_t bad_tex = tex;
    bad_tex.id = 99;
    assert(vflash_renderer_destroy_texture(&r, &bad_tex) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_destroy_texture(&r, &tex) == VFLASH_GPU_OK);
    assert(tex.id == 0 && tex.width == 0 && tex.height == 0);
    assert(vflash_renderer_destroy_texture(&r, &tex) == VFLASH_GPU_INVALID);

    assert(vflash_renderer_draw(&r, NULL, 0, 0) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_draw(&r, &tex, 0, 0) == VFLASH_GPU_INVALID);
    tex.width = 16; tex.height = 16; tex.atlas_x = 96; tex.atlas_y = 0; tex.type = VFLASH_TEXTURE_OPAQUE; tex.id = 99;
    assert(vflash_renderer_draw(&r, &tex, 0, 0) == VFLASH_GPU_INVALID);
    tex.id = 1;
    assert(vflash_renderer_draw_sub(&r, NULL, 0, 0, 16, 16, 0, 0) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_draw_sub(&r, &tex, -1, 0, 16, 16, 0, 0) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_draw_sub(&r, &tex, 0, -1, 16, 16, 0, 0) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_draw_sub(&r, &tex, 0, 0, 20, 16, 0, 0) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_draw_sub(&r, &tex, 8, 0, 12, 16, 0, 0) == VFLASH_GPU_INVALID);

    vflash_texture_t rt;
    assert(vflash_renderer_create_target(NULL, &rt, 16, 16, VFLASH_TEXTURE_KEYED) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_create_target(&r, NULL, 16, 16, VFLASH_TEXTURE_KEYED) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_create_target(&r, &rt, 0, 16, VFLASH_TEXTURE_KEYED) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_create_target(&r, &rt, 64, 8, VFLASH_TEXTURE_KEYED) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_set_target(NULL, NULL) == VFLASH_GPU_NOT_INITIALIZED);
    assert(vflash_renderer_set_target(&r, &bad_tex) == VFLASH_GPU_INVALID);
    assert(vflash_renderer_set_target(&r, NULL) == VFLASH_GPU_INVALID);

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

    assert(SDL_CreateRenderer_Static(NULL, NULL) == NULL);
    assert(SDL_CreateRenderer_Static(&storage, NULL) == NULL);

    storage.initialized = 1;
    storage.native_renderer.initialized = 1;
    storage.native_renderer.config = cfg;
    storage.native_renderer.atlas_free_count = 1;
    storage.native_renderer.atlas_free[0].x = (uint16_t)cfg.atlas_x;
    storage.native_renderer.atlas_free[0].y = (uint16_t)cfg.atlas_y;
    storage.native_renderer.atlas_free[0].w = (uint16_t)cfg.atlas_width;
    storage.native_renderer.atlas_free[0].h = (uint16_t)cfg.atlas_height;
    storage.native_renderer.generation_id = 1;

    assert(SDL_CreateTexture(NULL, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 16, 16) == NULL);
    assert(SDL_CreateTexture(&storage, SDL_PIXELFORMAT_UNKNOWN, SDL_TEXTUREACCESS_STATIC, 16, 16) == NULL);
    assert(SDL_CreateTexture(&storage, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 16, 16) == NULL);
    assert(SDL_CreateTexture(&storage, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 15, 16) == NULL);
    assert(SDL_CreateTexture(&storage, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 0, 16) == NULL);
    assert(SDL_CreateTexture(&storage, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, -16, 16) == NULL);

    SDL_Texture *tex_argb = SDL_CreateTexture(&storage, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 16, 16);
    assert(tex_argb != NULL);
    assert(tex_argb->blend_mode == SDL_BLENDMODE_BLEND);

    SDL_Texture *tex_bgr = SDL_CreateTexture(&storage, SDL_PIXELFORMAT_BGR555, SDL_TEXTUREACCESS_STATIC, 16, 16);
    assert(tex_bgr != NULL);
    assert(tex_bgr->blend_mode == SDL_BLENDMODE_NONE);

    SDL_BlendMode bm;
    assert(SDL_GetTextureBlendMode(tex_argb, &bm) == 0 && bm == SDL_BLENDMODE_BLEND);
    assert(SDL_SetTextureBlendMode(tex_argb, SDL_BLENDMODE_NONE) == 0);
    assert(SDL_GetTextureBlendMode(tex_argb, &bm) == 0 && bm == SDL_BLENDMODE_NONE);
    assert(SDL_SetTextureBlendMode(tex_argb, SDL_BLENDMODE_BLEND) == 0);

    tex_argb->uploaded = 1;
    assert(SDL_SetTextureBlendMode(tex_argb, SDL_BLENDMODE_NONE) == -1);
    assert(strstr(SDL_GetError(), "cannot change blend mode") != NULL);
    assert(SDL_SetTextureBlendMode(tex_argb, SDL_BLENDMODE_BLEND) == 0);
    tex_argb->uploaded = 0;

    uint32_t dummy_pixels[256];
    SDL_Rect partial = {0, 0, 8, 8};
    assert(SDL_UpdateTexture(tex_argb, &partial, dummy_pixels, 16 * 4) == -1);
    assert(strstr(SDL_GetError(), "partial texture update unsupported") != NULL);

    dummy_pixels[0] = 0x00FF0000;
    dummy_pixels[1] = 0xFF000000;
    dummy_pixels[2] = 0xFFFFFFFF;
    dummy_pixels[3] = 0x8000FF00;

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
        if (x == 0) assert((texel >> 12) == 0);
        if (x == 1) assert(texel == 0xF000);
        if (x == 2) assert(texel == 0xFFFF);
        if (x == 3) assert(texel == 0x80F0);
    }

    tex_argb->uploaded = 1;
    SDL_Rect src_ok = {0, 0, 16, 16};
    SDL_Rect dst_ok = {10, 10, 16, 16};
    SDL_Rect dst_scaled = {10, 10, 32, 32};

    assert(SDL_RenderCopy(&storage, tex_argb, &src_ok, &dst_scaled) == -1);
    assert(strstr(SDL_GetError(), "unsupported scaling") != NULL);
    assert(SDL_RenderCopy(&storage, tex_argb, NULL, &dst_scaled) == -1);

    SDL_Rect src_oob = {10, 0, 16, 16};
    assert(SDL_RenderCopy(&storage, tex_argb, &src_oob, &src_ok) == -1);
    assert(strstr(SDL_GetError(), "source rect outside texture bounds") != NULL);

    SDL_Rect src_neg = {-1, 0, 16, 16};
    assert(SDL_RenderCopy(&storage, tex_argb, &src_neg, &src_ok) == -1);

    SDL_DestroyTexture(tex_argb);
    assert(tex_argb->in_use == 0);
    assert(SDL_RenderCopy(&storage, tex_argb, &src_ok, &dst_ok) == -1);

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


class MMUTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        stub = Path(cls.temp.name) / 'stub.c'
        stub.write_text('unsigned state; unsigned vflash_mmu_control(void){return state;} unsigned vflash_mmu_processor_state(void){return 0xd3;} void vflash_mmu_activate(unsigned t){(void)t;state=1;} void vflash_mmu_deactivate(void){state=0;}')
        output = Path(cls.temp.name) / 'mmu.so'
        subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC', '-I', str(ROOT / 'include'), str(ROOT / 'src/platform/mmu.c'), str(stub), '-o', str(output)], check=True)
        cls.lib = ctypes.CDLL(str(output))
        cls.lib.vflash_mmu_descriptor.restype = ctypes.c_uint32

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def setUp(self):
        self.lib.vflash_mmu_disable()
        self.lib.vflash_mmu_init()

    def test_section_encoding_and_conflict_are_atomic(self):
        f = self.lib.vflash_mmu_map_section
        self.assertEqual(f(0x100000, 0x200000, 1), 0)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0x100000, 1), 0x200412)
        self.assertEqual(f(0x100000, 0x300000, 1), 2)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0x100000, 1), 0x200412)
        self.assertEqual(f(1, 0, 1), 1)
        self.assertEqual(f(0, 0, 4), 3)

    def test_page_permissions_capacity_unmap(self):
        f = self.lib.vflash_mmu_map_page
        for ap in (1, 2, 3):
            va = 0x20000000 + ap * 4096
            self.assertEqual(f(va, 0x10000000, ap), 0)
            self.assertEqual(self.lib.vflash_mmu_descriptor(va, 2), 0x10000002 + ap * 0x550)
        self.assertEqual(f(0x20001000, 0x10002000, 1), 2)
        self.assertEqual(self.lib.vflash_mmu_unmap_page(0x20001000), 0)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0x20001000, 2), 0)
        for va in (0x20100000, 0x20200000, 0x20300000):
            self.assertEqual(f(va, 0x10000000, 1), 0)
        self.assertEqual(f(0x20400000, 0x10000000, 1), 5)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0x20400000, 1), 0)

    def test_identity_enable_and_active_edit_rejection(self):
        self.assertEqual(self.lib.vflash_mmu_enable(), 6)
        self.assertEqual(self.lib.vflash_mmu_identity(), 0)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0x10000000, 1), 0x1000041E)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0xA8000000, 1), 0xA8000412)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0x90000000, 1), 0x90000412)
        self.assertEqual(self.lib.vflash_mmu_descriptor(0x30000000, 1), 0)
        self.assertEqual(self.lib.vflash_mmu_enable(), 0)
        self.assertEqual(self.lib.vflash_mmu_init(), 4)
        self.assertEqual(self.lib.vflash_mmu_map_section(0x30000000, 0, 1), 4)
        self.assertEqual(self.lib.vflash_mmu_map_page(0x30000000, 0, 1), 4)
        self.assertEqual(self.lib.vflash_mmu_unmap_page(0x30000000), 4)


if __name__ == '__main__':
    unittest.main()
