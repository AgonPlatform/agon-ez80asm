#!/bin/bash
# Builds the Agon binary with agondev. Cleans first: the two builds share obj/.
set -e
cd "$(dirname "$0")/.."
export PATH="$HOME/agondev/bin:$PATH"
rm -rf obj bin
make -f Makefile-agon > /dev/null 2>&1 || { make -f Makefile-agon 2>&1 | tail -5; exit 1; }
mkdir -p opt/build
cp bin/ez80asm.bin "${1:-opt/build/ez80asm-agon.bin}"
ls -l "${1:-opt/build/ez80asm-agon.bin}" | awk '{print "agon: " $9 " " $5 " bytes"}'
