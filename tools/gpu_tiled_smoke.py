"""Check native tiled RAM, D2/D1, swaps, direct scanout and linear recovery."""
import argparse,concurrent.futures,json,re,struct,subprocess,sys
from pathlib import Path
from host_renderer import read_ppm,to_rgb
ROOT=Path(__file__).resolve().parents[1]
def run(mmu,view,emulator,bios,base):
 out=base/f'{mmu}-{view}';out.mkdir(parents=True,exist_ok=True)
 result=out/'tiled-result.json';result.unlink(missing_ok=True)
 elf=ROOT/f'build/vflash_gpu_tiled_{mmu}_{view}.elf'
 subprocess.run([sys.executable,str(ROOT/'tools/emulator_smoke.py'),'--emulator',str(emulator),'--bios',str(bios),'--elf',str(elf),'--disc',str(ROOT/f'build/gpu/tiled/{mmu}/{view}/homebrew.cue'),'--out',str(out),'--trace-video'],check=True,stdout=subprocess.DEVNULL,timeout=150)
 sy={p[2]:int(p[0],16) for line in subprocess.check_output(['arm-none-eabi-nm','--defined-only',str(elf)],text=True).splitlines() if len(p:=line.split())==3}
 ram=(out/'ram.bin').read_bytes();word=lambda name:struct.unpack_from('<I',ram,sy[name]-0x10000000)[0]
 assert word('tiled_status')==0x54494C31 and word('tiled_completions')==5
 assert word('tiled_swaps')==(2 if view=='linear' else 3)
 assert bool(word('tiled_control')&1)==(mmu=='on')
 offset=lambda x,y:(y//8)*8192+(x//32)*256+(y%8)*32+x%32
 a=[0x255A]*16384;b=a.copy()
 def fill(s,x,y,w,h,c):
  for yy in range(y,y+h):
   for xx in range(x,x+w):s[offset(xx,yy)]=c
 for args in [(0,0,32,8,31),(32,0,32,8,0x3e0),(0,8,32,8,0x7c00),(32,8,32,8,0x7fff),(28,6,12,4,0x3ff),(7,3,5,6,0x7c1f)]:fill(a,*args)
 fill(b,0,0,64,16,0x7c00);fill(b,31,7,2,2,31)
 visible_a=[a[offset(x,y)] for y in range(16) for x in range(64)]
 for y in range(16):
  for x in range(64):a[offset(x+80,y)]=visible_a[y*64+x]
 snapshot=a.copy()
 for y in range(14):
  for x in range(60):a[offset(x+84,y+2)]=snapshot[offset(x+80,y)]
 for name,expected in [('tiled_a',a),('tiled_b',b)]:
  assert struct.unpack_from('<16384H',ram,sy[name]-0x10000000)==tuple(expected),f'{name} pattern or untouched padding differs'
 expected_readback=tuple(a[offset(x+80,y)] for y in range(16) for x in range(64))
 assert struct.unpack_from('<1024H',ram,sy['tiled_readback']-0x10000000)==expected_readback,'D1 readback differs'
 assert ram[sy['video_pixels']-0x10000000:][:153600]==bytes(153600),'CPU linear buffer must remain black'
 expected=[0]*76800
 if view!='linear':
  source=a if view=='a' else b
  for y in range(16):
   for x in range(64):expected[(19+y)*320+13+x]=source[offset(x,y)]
 assert read_ppm(out/'frame.ppm')==(320,240,to_rgb(struct.pack('<76800H',*expected))),'direct scanout/placement/recovery mismatch'
 text=(out/'run.log').read_text(errors='replace')
 addresses=[int(v,16) for v in re.findall(r'layer 0 address ([0-9A-Fa-f]{8})',text)]
 sequence=[sy['tiled_a'],sy['tiled_b'],sy['video_pixels']]
 for address in addresses:
  if sequence and address==sequence[0]:sequence.pop(0)
 assert not sequence,'scanout did not visit A, B, then linear recovery'
 result.write_text(json.dumps(dict(passed=True,mmu=mmu,view=view,verified='all tiled words including padding; D2 copy and overlap; D1; direct scanout; A/B/linear transitions',cpu_conversion=False,completion_lists=5),indent=2)+'\n')
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
