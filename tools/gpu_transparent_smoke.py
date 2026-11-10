"""Verify native 0xC8 transparent-sprite rendering, clipping, shared 128x32 tiled atlas, and 96x32 viewport scanout."""
import argparse,concurrent.futures,json,re,struct,subprocess,sys
from pathlib import Path
from host_renderer import read_ppm,to_rgb
ROOT=Path(__file__).resolve().parents[1]

def draw_color(rgb):
 r=(rgb>>19)&31;g=(rgb>>11)&31;b=(rgb>>3)&31
 return (b<<10)|(g<<5)|r

def transparent_pixel(x,y):
 if (x<3 and y<3) or (x>=13 and y<3):return 0x0000 if (x==0 and y==0) else 0x8000
 if (x<3 and y>=13) or (x>=13 and y>=13):return 0xFFFF if (x==15 and y==15) else 0x9ABC
 if 6<=x<=9 and 6<=y<=9:return 0x8000 if ((x+y)&1) else 0x0000
 if x==0 or x==15 or y==0 or y==15:return 0x7FFF
 if x==y:return 0x03FF
 if x+y==15:return 0x7FE0
 base=(0x001F if x<8 else 0x03E0) if y<8 else (0x7C00 if x<8 else 0x7C1F)
 return (base ^ (((y&7)<<5)|(x&7))) & 0x7FFF

def is_opaque(p):
 return p!=0 and not (p&0x8000)

def run(mmu,view,emulator,bios,base):
 out=base/f'{mmu}-{view}';out.mkdir(parents=True,exist_ok=True)
 result=out/'transparent-result.json';result.unlink(missing_ok=True)
 elf=ROOT/f'build/vflash_gpu_transparent_{mmu}_{view}.elf'
 subprocess.run([sys.executable,str(ROOT/'tools/emulator_smoke.py'),'--emulator',str(emulator),'--bios',str(bios),'--elf',str(elf),'--disc',str(ROOT/f'build/gpu/transparent/{mmu}/{view}/homebrew.cue'),'--out',str(out),'--trace-video'],check=True,stdout=subprocess.DEVNULL,timeout=150)
 sy={p[2]:int(p[0],16) for line in subprocess.check_output(['arm-none-eabi-nm','--defined-only',str(elf)],text=True).splitlines() if len(p:=line.split())==3}
 ram=(out/'ram.bin').read_bytes();word=lambda name:struct.unpack_from('<I',ram,sy[name]-0x10000000)[0]
 assert word('gpu_transparent_status')==0x47545331 and word('gpu_transparent_frames')==32
 assert word('gpu_transparent_uploads')==2 and word('gpu_lists')==226
 assert bool(word('gpu_transparent_control')&1)==(mmu=='on')
 expected_asset=tuple(transparent_pixel(x,y) for y in range(16) for x in range(16))
 assert struct.unpack_from('<256H',ram,sy['transparent_asset']-0x10000000)==expected_asset,'linear sprite asset modified'
 offset=lambda x,y:(y//8)*8192+(x//32)*256+(y%8)*32+x%32
 def expected_surface(final_frame):
  s=[0x255A]*32768
  def fill(x,y,w,h,c):
   for yy in range(y,y+h):
    for xx in range(x,x+w):s[offset(xx,yy)]=c
  def draw_sprite(sx,sy_src,w,h,dx,dy,cx,cy,cw,ch):
   l=max(dx,cx);t=max(dy,cy);r=min(dx+w,cx+cw);b=min(dy+h,cy+ch)
   for yy in range(t,b):
    for xx in range(l,r):
     p=s[offset(sx+(xx-dx),sy_src+(yy-dy))]
     if is_opaque(p):s[offset(xx,yy)]=p
  for y in range(16):
   for x in range(16):s[offset(104+x,8+y)]=transparent_pixel(x,y)
  fill(0,0,96,16,draw_color(0x102060))
  fill(0,16,96,16,draw_color(0x105020))
  fill(0,14,96,4,draw_color(0xC08020))
  fill(30,0,4,32,draw_color(0x404040))
  fill(62,0,4,32,draw_color(0x404040))
  sx=final_frame*2+8;sy_pos=1+((final_frame*5)&15)
  draw_sprite(104,8,16,16,sx,sy_pos,0,0,128,32)
  draw_sprite(104,8,16,16,-4,8,2,4,20,24)
  return s
 a=expected_surface(30);b=expected_surface(31)
 for name,expected in [('transparent_surface_a',a),('transparent_surface_b',b)]:
  actual=struct.unpack_from('<32768H',ram,sy[name]-0x10000000)
  assert actual==tuple(expected),f'{name} playfield, atlas or untouched padding differs'
  for y in range(32):
   for x in range(96,128):
    exp_atlas=transparent_pixel(x-104,y-8) if (104<=x<120 and 8<=y<24) else 0x255A
    assert actual[offset(x,y)]==exp_atlas,f'{name} offscreen atlas corrupted at ({x},{y})'
 assert ram[sy['video_pixels']-0x10000000:][:153600]==bytes(153600),'CPU linear buffer must remain black'
 expected=[0]*76800
 if view!='linear':
  source=a if view=='a' else b
  ox,oy=(0,0) if view=='a' else (224,208)
  for y in range(32):
   for x in range(96):expected[(oy+y)*320+ox+x]=source[offset(x,y)]
 assert read_ppm(out/'frame.ppm')==(320,240,to_rgb(struct.pack('<76800H',*expected))),'direct viewport scanout/placement/recovery mismatch'
 text=(out/'run.log').read_text(errors='replace')
 addresses=[int(v,16) for v in re.findall(r'layer 0 address ([0-9A-Fa-f]{8})',text)]
 sequence=[sy['transparent_surface_a'],sy['transparent_surface_b'],sy['video_pixels']]
 for address in addresses:
  if sequence and address==sequence[0]:sequence.pop(0)
 assert not sequence,'scanout did not visit A, B, then linear recovery'
 banks=[v for v in addresses if v in (sy['transparent_surface_a'],sy['transparent_surface_b'])]
 assert len(banks)>=32 and banks[:32]==[sy['transparent_surface_a'] if i%2==0 else sy['transparent_surface_b'] for i in range(32)]
 stats={name:word(name) for name in ('gpu_operation_ticks','gpu_present_ticks','gpu_total_ticks','gpu_submission_ticks','gpu_completion_ticks','gpu_lists','gpu_transparent_uploads')}
 assert stats['gpu_total_ticks']>=stats['gpu_present_ticks']>0
 result.write_text(json.dumps(dict(passed=True,mmu=mmu,view=view,frames=32,surface='128x32 tiled (96x32 playfield + 32x32 offscreen atlas)',sprite='16x16 color-key transparent (0x8000/0xFFFF/0x9ABC and 0x0000 cutouts, unclipped + clipped)',statistics=stats,timer_resolution_us=100,cpu_conversion=False),indent=2)+'\n')
 print('PASS',mmu,view,flush=True)

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--emulator',type=Path,required=True);p.add_argument('--bios',type=Path,required=True)
 p.add_argument('--out',type=Path,required=True)
 a=p.parse_args()
 with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
  futures=[pool.submit(run,m,v,a.emulator,a.bios,a.out) for m in ('off','on') for v in ('a','b','linear')]
  for f in futures:f.result()

if __name__=='__main__':main()

