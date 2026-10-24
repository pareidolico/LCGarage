#!/bin/sh
# Builds LCGarage.asi (32-bit Windows DLL) with zig, from Linux, macOS or Windows.
#   pip install ziglang
#   ./build.sh
set -e
cd "$(dirname "$0")"
ZIG="python3 -m ziglang"
mkdir -p build
$ZIG cc -target x86-windows-gnu -O2 -fno-stack-protector -fno-builtin -Wall -Wextra -Wno-unused-parameter \
    -c src/lcgarage.c -o build/lcgarage.o
# no C runtime: kernel32 and user32 only
$ZIG cc -target x86-windows-gnu -shared -nostdlib -o build/LCGarage.asi build/lcgarage.o \
    -lkernel32 -luser32 -Wl,--entry=DllMain
echo "built build/LCGarage.asi"
