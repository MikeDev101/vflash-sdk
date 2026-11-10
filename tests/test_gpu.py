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
 vflash_gpu_caps_t c=vflash_gpu_caps(&g);assert(c.fill_rect && c.blit && !c.hardware_accelerated && !c.alpha_blend && !c.scaling);
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
 assert(!vflash_gpu_caps_for_layout(&g,VFLASH_GPU_LAYOUT_ZEVIO_TILED).fill_rect);
 assert(vflash_gpu_shutdown(&g)==0);assert(vflash_gpu_fill_rect(&g,&d,0,0,1,1,0)==VFLASH_GPU_NOT_INITIALIZED);
}
'''
  with tempfile.TemporaryDirectory() as temp:
   f=Path(temp)/'test.c';f.write_text(code);exe=Path(temp)/'test'
   subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT/'include'),str(f),*[str(p) for p in (ROOT/'src/gpu').glob('*.c')],str(ROOT/'src/graphics/draw.c'),str(ROOT/'src/graphics/font.c'),'-o',str(exe)],check=True)
   subprocess.run([str(exe)],check=True)
