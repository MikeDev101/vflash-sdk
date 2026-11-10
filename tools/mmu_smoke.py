"""Validate optional translation bring-up; does NOT validate memory protection."""
import argparse,ctypes,json,struct,subprocess,sys,tempfile
from pathlib import Path
from host_renderer import read_ppm,to_rgb
from panic_smoke import load_renderer,reference_pixels
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--emulator',required=True,type=Path);p.add_argument('--bios',required=True,type=Path)
 p.add_argument('--test',choices=('identity','undefined'),default='identity')
 p.add_argument('--out',required=True,type=Path)
 a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
 result=a.out/'mmu-result.json';result.unlink(missing_ok=True)
 elf=ROOT/f'build/vflash_mmu_{a.test}.elf'
 cmd=[sys.executable,str(ROOT/'tools/emulator_smoke.py'),'--emulator',str(a.emulator),'--bios',str(a.bios),
      '--elf',str(elf),'--disc',str(ROOT/f'build/mmu/{a.test}/homebrew.cue'),'--out',str(a.out)]
 if a.test=='identity':cmd+=['--verify-video']
 subprocess.run(cmd,check=True,stdout=subprocess.DEVNULL)
 sy={v[2]:int(v[0],16) for l in subprocess.check_output(['arm-none-eabi-nm','--defined-only',str(elf)],text=True).splitlines() if len(v:=l.split())==3}
 ram=(a.out/'ram.bin').read_bytes()
 word=lambda name:struct.unpack_from('<I',ram,sy[name]-0x10000000)[0]
 observed={k:word(k) for k in ('mmu_status','mmu_enabled_control','mmu_disabled_control','mmu_init_ticks','mmu_tables_bytes','mmu_probe')}
 if not (observed['mmu_status']==0x4D4D5531 and observed['mmu_probe']==0x1926ABCD and observed['mmu_enabled_control']&1 and not observed['mmu_disabled_control']&1 and not observed['mmu_enabled_control']&0x1004):
  raise ValueError(f'MMU bring-up failed: {observed}')
 if sy['vflash_mmu_l1']&16383 or sy['vflash_mmu_l2']&1023:raise ValueError('table alignment')
 if a.test=='undefined':
  data=ram[sy['vflash_crash_record']-0x10000000:][:180]
  record=struct.unpack('<29I64s',data)
  if (record[0],record[2],record[3],record[4],record[7])!=(0x56435253,3,23,sy['mmu_fault_instruction'],sy['mmu_fault_instruction']+4):raise ValueError('wrong CPU exception/PC')
  with tempfile.TemporaryDirectory() as tmp:native=reference_pixels(data,load_renderer(Path(tmp)/'panic.so'))
  if ram[sy['vflash_panic_pixels']-0x10000000:][:len(native)]!=native:raise ValueError('panic RAM pixels')
  if read_ppm(a.out/'frame.ppm')!=(320,240,to_rgb(native)):raise ValueError('panic scanout')
 result.write_text(json.dumps(dict(passed=True,scope='translation only; no abort or permission validation',observed=observed,initialization_us=observed['mmu_init_ticks']*100),indent=2)+'\n')
 print(result.read_text())
if __name__=='__main__':main()
