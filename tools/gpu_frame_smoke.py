"""Check integrated native HAL frame rendering, swaps and shutdown recovery."""
import argparse,concurrent.futures,json,re,struct,subprocess,sys
from pathlib import Path
from host_renderer import read_ppm,to_rgb
ROOT=Path(__file__).resolve().parents[1]
def run(mmu,view,emulator,bios,base):
 out=base/f'{mmu}-{view}';out.mkdir(parents=True,exist_ok=True)
 result=out/'frame-result.json';result.unlink(missing_ok=True)
 elf=ROOT/f'build/vflash_gpu_frame_{mmu}_{view}.elf'
 subprocess.run([sys.executable,str(ROOT/'tools/emulator_smoke.py'),'--emulator',str(emulator),'--bios',str(bios),'--elf',str(elf),'--disc',str(ROOT/f'build/gpu/frame/{mmu}/{view}/homebrew.cue'),'--out',str(out),'--trace-video'],check=True,stdout=subprocess.DEVNULL,timeout=150)
 sy={p[2]:int(p[0],16) for line in subprocess.check_output(['arm-none-eabi-nm','--defined-only',str(elf)],text=True).splitlines() if len(p:=line.split())==3}
 ram=(out/'ram.bin').read_bytes();word=lambda name:struct.unpack_from('<I',ram,sy[name]-0x10000000)[0]
 assert word('gpu_frame_status')==0x47465231 and word('gpu_frame_count')==32
 assert word('gpu_lists')==213
 assert bool(word('gpu_frame_control')&1)==(mmu=='on')
 offset=lambda x,y:(y//8)*8192+(x//32)*256+(y%8)*32+x%32
 a=[0x255A]*16384;b=a.copy();wide=[0x255A]*24576
 def fill(s,x,y,w,h,c):
  for yy in range(y,y+h):
   for xx in range(x,x+w):s[offset(xx,yy)]=c
 for args in [(0,0,32,8,31),(32,0,32,8,0x3e0),(0,8,32,8,0x7c00),(32,8,32,8,0x7fff),(28,6,12,4,0x3ff),(7,3,5,6,0x7c1f)]:fill(a,*args)
 fill(b,0,0,64,16,0x3e0);fill(b,31,7,2,2,31);fill(b,0,0,16,4,0x7fff)
 for surface in (a,b):
  snapshot=surface.copy()
  for y in range(4):
   for x in range(16):surface[offset(x+44,y+12)]=snapshot[offset(x,y)]
 fill(wide,0,0,96,24,0);fill(wide,64,16,32,8,0x7fe0)
 for x in range(14):wide[offset(x,23)]=0x4000+2+x
 for y in range(8):
  for x in range(16):
   val=0x4000+y*16+x
   wide[offset(40+x,8+y)]=val;wide[offset(72+x,16+y)]=val
 for name,count,expected in [('frame_a',16384,a),('frame_b',16384,b),('frame_wide',24576,wide)]:
  assert struct.unpack_from(f'<{count}H',ram,sy[name]-0x10000000)==tuple(expected),f'{name} pattern or untouched padding differs'
 assert ram[sy['video_pixels']-0x10000000:][:153600]==bytes(153600),'CPU linear buffer must remain black'
 expected=[0]*76800
 if view!='linear':
  source=a if view=='a' else b
  for y in range(16):
   for x in range(64):expected[((0 if view=='a' else 224)+y)*320+(0 if view=='a' else 256)+x]=source[offset(x,y)]
 assert read_ppm(out/'frame.ppm')==(320,240,to_rgb(struct.pack('<76800H',*expected))),'direct scanout/placement/recovery mismatch'
 text=(out/'run.log').read_text(errors='replace')
 addresses=[int(v,16) for v in re.findall(r'layer 0 address ([0-9A-Fa-f]{8})',text)]
 sequence=[sy['frame_wide'],sy['frame_a'],sy['frame_b'],sy['video_pixels']]
 for address in addresses:
  if sequence and address==sequence[0]:sequence.pop(0)
 assert not sequence,'scanout did not visit A, B, then linear recovery'
 banks=[v for v in addresses if v in (sy['frame_a'],sy['frame_b'])]
 assert len(banks)>=32 and banks[:32]==[sy['frame_a'] if i%2==0 else sy['frame_b'] for i in range(32)]
 stats={name:word(name) for name in ('gpu_operation_ticks','gpu_present_ticks','gpu_total_ticks','gpu_submission_ticks','gpu_completion_ticks','gpu_lists')}
 assert stats['gpu_total_ticks']>=stats['gpu_present_ticks']>0
 result.write_text(json.dumps(dict(passed=True,mmu=mmu,view=view,frames=32,statistics=stats,timer_resolution_us=100,cpu_conversion=False),indent=2)+'\n')
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
