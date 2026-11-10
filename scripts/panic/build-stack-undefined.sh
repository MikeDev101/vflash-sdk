#!/bin/sh
# Build the separate stack-undefined panic diagnostic disc.
set -eu
sdk_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
exec "$sdk_dir/scripts/build-panic-test.sh" stack-undefined "$@"
