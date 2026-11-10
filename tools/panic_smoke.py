"""Boot a separate panic diagnostic through firmware and verify actual SDRAM/scanout.

No exceptions are synthesized by this runner. Thumb UDF is a negative capability
probe for the pinned interpreter; abort/IRQ/FIQ validation is deliberately absent.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from host_renderer import read_ppm, to_rgb

ROOT = Path(__file__).resolve().parents[1]
MODES = ('manual', 'undefined', 'stack', 'nested', 'swi', 'thumb', 'restore',
         'system', 'stack-undefined', 'thumb-swi')
WORDS = ('magic', 'version', 'reason', 'flags', 'fault_pc', 'cpsr', 'spsr',
         'exception_lr', 'entry_sp', 'origin_sp', 'origin_lr',
         *('r' + str(i) for i in range(13)), 'fault_address', 'fault_status',
         'secondary_reason', 'secondary_pc', 'vector_mask')
RECORD = struct.Struct('<29I64s')
RAM_BASE = 0x10000000


def load_renderer(output):
    compiler = os.environ.get('VFLASH_HOST_CC') or shutil.which('cc')
    if not compiler:
        raise ValueError('panic verification requires a native C compiler')
    subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-shared', '-fPIC',
                    '-I', str(ROOT / 'include'), str(ROOT / 'src/runtime/panic_render.c'),
                    str(ROOT / 'src/graphics/font.c'), '-o', str(output)],
                   capture_output=True, check=True, timeout=30)
    library = ctypes.CDLL(str(output))
    library.vflash_panic_draw.argtypes = [ctypes.POINTER(ctypes.c_uint16), ctypes.c_void_p]
    library.vflash_panic_draw.restype = None
    return library


def reference_pixels(record, library):
    # Canaries also catch a renderer writing outside its dedicated framebuffer.
    pixels = (ctypes.c_uint16 * (320 * 240 + 2))()
    pixels[0] = pixels[-1] = 0xA55A
    saved = ctypes.create_string_buffer(record, len(record))
    library.vflash_panic_draw(ctypes.cast(ctypes.byref(pixels, 2), ctypes.POINTER(ctypes.c_uint16)), saved)
    if pixels[0] != 0xA55A or pixels[-1] != 0xA55A:
        raise ValueError('panic renderer overran its framebuffer')
    return struct.pack('<76800H', *pixels[1:-1])


def require(condition, message):
    if not condition:
        raise ValueError(message)


def inspect_run(mode, elf, output):
    nm = subprocess.check_output(['arm-none-eabi-nm', '--defined-only', str(elf)], text=True, timeout=10)
    symbols = {parts[2]: int(parts[0], 16) for line in nm.splitlines() if len(parts := line.split()) == 3}
    ram = (output / 'ram.bin').read_bytes()
    require(len(ram) == 16 * 1024 * 1024, 'incomplete SDRAM dump')
    word = lambda address: struct.unpack_from('<I', ram, address - RAM_BASE)[0]
    data = ram[symbols['vflash_crash_record'] - RAM_BASE:][:RECORD.size]
    values = RECORD.unpack(data)
    record = dict(zip(WORDS, values[:-1]))
    record['message'] = values[-1].split(b'\0', 1)[0].decode('ascii')
    require(word(symbols['video_status']) == 0x56494431 and word(symbols['render_status']) == 0x44524157,
            'normal timer/video initialization did not complete before the fault')
    hardware = mode in ('undefined', 'swi', 'restore', 'system', 'stack-undefined', 'thumb-swi')
    undefined = mode in ('undefined', 'restore', 'system', 'stack-undefined')
    reason = 3 if undefined else (4 if mode in ('swi', 'thumb-swi') else 1)
    flags = (23 if undefined else (3 if mode == 'thumb-swi' else (19 if mode == 'swi' else 6)))
    require((record['magic'], record['version'], record['reason'], record['flags']) ==
            (0x56435253, 1, reason, flags), f'incorrect crash identity/context flags: {record}')
    require(record['fault_address'] == record['fault_status'] == 0, 'unvalidated fault-register data')
    if hardware:
        require(record['cpsr'] & 0x1f == (0x1b if undefined else 0x13), 'wrong exception mode')
        require(record['cpsr'] & 0x20 == 0, 'exception handler did not enter ARM state')
        require(record['spsr'] & 0x3f == (0x1f if mode == 'system' else (0x33 if mode == 'thumb-swi' else 0x13)),
                'wrong interrupted mode/instruction state')
        require(record['cpsr'] & 0xf0000000 == record['spsr'] & 0xf0000000, 'condition flags lost at exception entry')
        if mode == 'thumb-swi':
            require(record['fault_pc'] == 0, 'Thumb PC must remain explicitly unknown on this interpreter')
        else:
            require(record['fault_pc'] == symbols['panic_fault_instruction'], 'faulting PC differs from UDF/SWI symbol')
            require(record['exception_lr'] == record['fault_pc'] + 4, 'wrong ARM exception return address')
        if undefined:
            require([record['r' + str(i)] for i in range(13)] == [int(c * 8, 16) for c in '123456789ABDC'],
                    'interrupted general-purpose registers were not preserved')
            require(record['spsr'] & 0xf0000000 == 0x60000000, 'known interrupted condition flags lost')
        else:
            require(record['origin_lr'] == 0, 'same-bank SWI cannot recover overwritten SVC LR')
    else:
        require(record['spsr'] == 0 and record['origin_lr'] == record['exception_lr'], 'manual context incorrect')
        require(bool(record['message']), 'manual panic message missing')
    if mode in ('stack', 'stack-undefined'):
        require(record['origin_sp'] == 0, 'bad application SP was not captured')
    elif mode == 'system':
        require(record['origin_sp'] == 0x10fff800 and record['origin_lr'] == 0x12345678,
                'SYS/USR banked SP/LR lost')
    else:
        require(symbols['__stack_bottom'] <= record['origin_sp'] <= symbols['__stack_top'], 'application stack not preserved')
    require(word(symbols['vflash_panic_busy']) == 1, 'panic entry guard not set')
    if mode == 'nested':
        require((record['secondary_reason'], record['secondary_pc']) == (3, symbols['panic_nested_fault']),
                'nested exception was not recorded correctly')
    else:
        require(record['secondary_reason'] == record['secondary_pc'] == 0, 'unexpected nested exception')
    if mode == 'restore':
        require(word(symbols['panic_restore_status']) == 1, 'restore/readback/reinstall did not complete')
    if mode == 'thumb':
        require(word(symbols['thumb_probe_returned']) == 1 and 'DID NOT TRAP' in record['message'],
                'Thumb negative capability probe did not complete')
    backups = [struct.unpack_from('<5I', ram, symbols['vflash_vector_backups'] - RAM_BASE + 20 * i) for i in range(2)]
    mask = 3 if mode in ('swi', 'thumb-swi') else 1
    require(record['vector_mask'] == mask, 'incorrect vector ownership mask')
    for i, (vector, slot, original, patched, active) in enumerate(backups):
        expected_active = int(bool(mask & (1 << i)))
        require(vector == 4 + i * 4 and slot == 0x1000ff84 + i * 4, 'unexpected firmware trampoline location')
        require(original == 0xe59ff040 and active == expected_active, 'original firmware word was not retained')
        require(word(slot) == (patched if active else original), 'trampoline overwritten or firmware SWI modified')
    # Watch both slots from reset. After the SDK's first patch, every write must
    # be one of its installation/restoration writes, including optional SWI.
    log = (output / 'run.log').read_text(errors='replace')
    watch = re.findall(r'\[WATCH\] write32 ([0-9A-F]+) = ([0-9A-F]+) PC=([0-9A-F]+)', log)
    writes = [tuple(int(value, 16) for value in item) for item in watch]
    first = next((i for i, (_, _, pc) in enumerate(writes) if 0x10c00000 <= pc < symbols['__image_end']), None)
    require(first is not None, 'no observable SDK vector write')
    writes = writes[first:]
    watch_lines = [line for line in log.splitlines() if line.startswith('[WATCH]')]
    first_line = next(i for i, line in enumerate(watch_lines)
                      if f'PC={writes[0][2]:08X}' in line)
    require(len(watch_lines[first_line:]) == len(writes), 'unexpected DMA or partial-word trampoline write')
    expected_writes = [(backups[0][1], backups[0][3])]
    if mode == 'restore':
        expected_writes += [(backups[0][1], backups[0][2]), (backups[0][1], backups[0][3])]
    if mask == 3:
        expected_writes += [(backups[1][1], backups[1][3])]
    require([(address, value) for address, value, _ in writes] == expected_writes,
            f'unexpected trampoline writes after SDK takeover: {writes}')
    require(all(0x10c00000 <= pc < symbols['__image_end'] for _, _, pc in writes), 'firmware reused a patched slot')
    start, end = symbols['__panic_start'], symbols['__panic_end']
    ranges = [(symbols['vflash_crash_record'], RECORD.size), (symbols['vflash_vector_backups'], 40),
              (symbols['vflash_panic_busy'], 4), (symbols['vflash_panic_stack'], 4096),
              (symbols['vflash_panic_pixels'], 153600)]
    require(all(start <= address < address + size <= end for address, size in ranges), 'panic storage outside reserved region')
    ranges.sort()
    require(all(a + size <= b for (a, size), (b, _) in zip(ranges, ranges[1:])), 'panic buffers overlap')
    require(symbols['__bss_end'] <= start and end <= symbols['__heap_start'] <= symbols['__heap_end'] == symbols['__stack_bottom'],
            'panic storage overlaps application BSS/heap/stack')
    with tempfile.TemporaryDirectory(prefix='panic-renderer-', dir=output) as temp:
        native = reference_pixels(data, load_renderer(Path(temp) / 'panic.so'))
    actual = ram[symbols['vflash_panic_pixels'] - RAM_BASE:][:len(native)]
    require(actual == native, 'guest panic pixels differ from host C renderer')
    width, height, rgb = read_ppm(output / 'frame.ppm')
    require((width, height) == (320, 240) and rgb == to_rgb(native), 'actual panic scanout differs from crash record')
    result = dict(passed=True, mode=mode, hardware_exception=hardware, record=record,
                  reserved_region=dict(start=start, end=end, bytes=end-start),
                  trampolines=[dict(zip(('vector', 'slot', 'original', 'patched', 'active'), values)) for values in backups],
                  takeover_writes=writes, native_sha256=hashlib.sha256(native).hexdigest(),
                  verification='firmware boot; exact SDRAM record, vector writes, native framebuffer and actual RGB scanout')
    (output / 'crash-record.bin').write_bytes(data)
    (output / 'panic-result.json').write_text(json.dumps(result, indent=2) + '\n')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--emulator', type=Path, required=True)
    parser.add_argument('--bios', type=Path, required=True)
    parser.add_argument('--build', type=Path, default=ROOT / 'build')
    parser.add_argument('--test', choices=MODES + ('all',), default='undefined')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    try:
        for mode in MODES if args.test == 'all' else (args.test,):
            output = args.out / mode if args.test == 'all' else args.out
            output.mkdir(parents=True, exist_ok=True)
            for name in ('panic-result.json', 'crash-record.bin'):
                (output / name).unlink(missing_ok=True)
            elf = args.build / f'vflash_panic_{mode}.elf'
            command = [sys.executable, str(ROOT / 'tools/emulator_smoke.py'), '--emulator', str(args.emulator),
                       '--bios', str(args.bios), '--disc', str(args.build / f'panic/{mode}/homebrew.cue'),
                       '--elf', str(elf), '--out', str(output), '--watch-vectors']
            subprocess.run(command, check=True, stdout=subprocess.DEVNULL, timeout=150)
            result = inspect_run(mode, elf, output)
            print(f"PASS {mode}: reason={result['record']['reason']} PC={result['record']['fault_pc']:08X}; "
                  f"{'CPU exception' if result['hardware_exception'] else 'manual panic'}, record and scanout verified", flush=True)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f'Panic verification failed: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
