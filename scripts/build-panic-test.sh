#!/bin/sh
# Shared build implementation for the per-diagnostic scripts.
set -eu
sdk_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
mode=${1-}
case "$mode" in
    manual|undefined|stack|nested|swi|thumb|restore|system|stack-undefined|thumb-swi) shift ;;
    *) printf '%s\n' 'Usage: build-panic-test.sh TEST [cmake --build options]' >&2; exit 2 ;;
esac
if [ ! -f "$sdk_dir/build/CMakeCache.txt" ]; then
    cmake -S "$sdk_dir" -B "$sdk_dir/build" \
        -DCMAKE_TOOLCHAIN_FILE="$sdk_dir/cmake/arm-none-eabi.cmake"
fi
cmake --build "$sdk_dir/build" --target "panic_${mode}_disc_image" "$@"
printf '\nBuilt %s panic diagnostic.\nDisc: %s\nELF:  %s\n' "$mode" \
    "$sdk_dir/build/panic/$mode/homebrew.cue" "$sdk_dir/build/vflash_panic_$mode.elf"
