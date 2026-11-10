#!/bin/sh
set -eu
sdk_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec python3 "$sdk_dir/tools/play_homebrew.py" --bios-menu "$@"
