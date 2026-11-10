"""Verify software HAL or an isolated command-driven GE fill in pinned FlashEm."""
import argparse,json,struct,subprocess,sys,tempfile,os,shutil
from pathlib import Path
from host_renderer import read_ppm,to_rgb
ROOT=Path(__file__).resolve().parents[1]
def software_fill_reference():
 # Native GE result is also compared with the actual HAL's software backend.
 # This host process never supplies pixels or memory to the emulator.
 code = r'''#include "vflash/gpu.h"
#include <stdio.h>
int main(void) {
 uint16_t pixels[256];
 vflash_framebuffer_t fb={pixels,32,8,32,256,VFLASH_BGR555};
 vflash_gpu_t gpu; vflash_gpu_surface_t surface;
 if(vflash_gpu_init(&gpu,VFLASH_GPU_SOFTWARE) ||
    vflash_gpu_surface_register(&surface,&fb,0,0) ||
    vflash_gpu_fill_rect(&gpu,&surface,0,0,32,8,0xFF0000)) return 1;
 return fwrite(pixels,sizeof(pixels),1,stdout)==1 ? 0:1;
}'''
 with tempfile.TemporaryDirectory(prefix='gpu-reference-') as temp:
  source=Path(temp)/'reference.c';source.write_text(code);exe=Path(temp)/'reference'
  compiler=os.environ.get('VFLASH_HOST_CC') or shutil.which('cc')
  if not compiler:raise ValueError('native comparison requires a host C compiler')
  files=[ROOT/'src/gpu'/name for name in ('gpu.c','gpu_software.c','gpu_zevio.c')]
  files += [ROOT/'src/graphics/draw.c',ROOT/'src/graphics/font.c']
  subprocess.run([compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-I',str(ROOT/'include'),str(source),*map(str,files),'-o',str(exe)],check=True,capture_output=True)
  data=subprocess.check_output([str(exe)])
 return struct.unpack('=256H',data)
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--emulator',type=Path,required=True);p.add_argument('--bios',type=Path,required=True)
 p.add_argument('--test',choices=('software','mmu','native'),required=True);p.add_argument('--out',type=Path,required=True)
 a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
 result=a.out/'gpu-result.json';result.unlink(missing_ok=True)
 elf=ROOT/f'build/vflash_gpu_{a.test}.elf'
 subprocess.run([sys.executable,str(ROOT/'tools/emulator_smoke.py'),'--emulator',str(a.emulator),'--bios',str(a.bios),'--elf',str(elf),'--disc',str(ROOT/f'build/gpu/{a.test}/homebrew.cue'),'--out',str(a.out)],check=True,stdout=subprocess.DEVNULL)
 sy={v[2]:int(v[0],16) for l in subprocess.check_output(['arm-none-eabi-nm','--defined-only',str(elf)],text=True).splitlines() if len(v:=l.split())==3}
 ram=(a.out/'ram.bin').read_bytes();word=lambda n:struct.unpack_from('<I',ram,sy[n]-0x10000000)[0]
 if word('gpu_status')!=(0x47505532 if a.test=='native' else 0x47505531):raise ValueError('GPU operation did not complete')
 if word('video_status')!=0x56494431 or word('render_status')!=0x44524157:raise ValueError('video initialization/present failed')
 pixels=[0]*(320*240)
 if a.test=='native':
  raw=struct.unpack_from('<8192H',ram,sy['gpu_tiled']-0x10000000)
  if raw!=software_fill_reference()+(0x255A,)*(8192-256):raise ValueError('software/native fill or untouched-region check failed')
  if word('gpu_completion')!=0xFD08:raise ValueError('GE completion status')
  for y in range(8):pixels[y*320:y*320+32]=[31]*32
  stats={n:word(n) for n in ('gpu_submit_ticks','gpu_wait_ticks','gpu_completion')}
 else:
  for y in range(10,70):
   for x in range(10,110):pixels[y*320+x]=31
  for y in range(40,48):
   for x in range(60,68):pixels[y*320+x]=0x3e0
  for y in range(20,60):
   for x in range(20,60):pixels[y*320+x]=0x7c00
  stats={n:word(n) for n in ('gpu_clear_ticks','gpu_fill_ticks','gpu_blit_ticks')}
 native=struct.pack('<76800H',*pixels)
 if ram[sy['video_pixels']-0x10000000:][:len(native)]!=native:raise ValueError('linear framebuffer mismatch')
 if read_ppm(a.out/'frame.ppm')!=(320,240,to_rgb(native)):raise ValueError('actual scanout mismatch')
 result.write_text(json.dumps(dict(passed=True,backend=a.test,statistics=stats,timer_resolution_us=100,scope='command-driven emulator fill; CPU linear presentation copy' if a.test=='native' else 'synchronous software; eight operations per measured batch'),indent=2)+'\n')
 print(result.read_text())
if __name__=='__main__':main()
