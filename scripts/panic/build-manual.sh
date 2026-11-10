#!/bin/sh
# Build the separate manual panic diagnostic disc.
set -eu
sdk_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec "$sdk_dir/scripts/build-panic-test.sh" manual "$@"
