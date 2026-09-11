#!/bin/bash
# Builds the Linux binary. The host and Agon builds share obj/, so clean first.
set -e
cd "$(dirname "$0")/.."
rm -rf obj bin
make > /dev/null 2>&1 || { make 2>&1 | tail -5; exit 1; }
mkdir -p opt/build
cp bin/ez80asm "${1:-opt/build/ez80asm-host}"
echo "host: ${1:-opt/build/ez80asm-host}"
