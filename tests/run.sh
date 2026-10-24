#!/bin/sh
# Builds and runs the logic tests on Linux (needs: pip install ziglang)
#   tests/run.sh [path/to/vehicles.ide]
set -e
cd "$(dirname "$0")"
IDE="${1:-$(pwd)/sample_vehicles.ide}"
python3 -m ziglang cc -target x86_64-linux-gnu -O1 -w -Ifake -o /tmp/lcgarage_test test_logic.c -lm
/tmp/lcgarage_test "$IDE"
