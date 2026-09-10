#!/bin/bash
# Runs opt/test_macros.c, which compares every table and macro in src/ against
# the library call or comparison it replaced, over every input.
set -e
cd "$(dirname "$0")/.."
W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
gcc -O2 -Isrc -o "$W/test_macros" opt/test_macros.c src/ctype_tab.c
"$W/test_macros"
