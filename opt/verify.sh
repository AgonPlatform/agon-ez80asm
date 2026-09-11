#!/bin/bash
# Every source, assembled by the stock ez80asm and by this build, compared byte
# for byte.
#
#   opt/verify.sh [path-to-modified-host-binary]
#
# The stock binary in opt/ref is the oracle: an optimisation that changes an
# output byte is not an optimisation, it is a bug. The corpus is ez80asm's own
# tests plus zap's, which between them cover what each assembler was written to
# check.
#
# Each source is assembled twice by each binary, once plainly and once with -l,
# and the whole working directory is compared afterwards -- the binary, the
# listing and the anonymous-label file together. The console output is compared
# too, with the timing line dropped, so a change that alters a diagnostic is
# caught even when it does not alter a byte.
set -uo pipefail
cd "$(dirname "$0")/.."

STOCK="opt/ref/ez80asm-stock"
NEW="${1:-opt/build/ez80asm-host}"
[ -x "$STOCK" ] || { echo "no stock binary; run opt/build-host.sh opt/ref/ez80asm-stock" >&2; exit 2; }
[ -x "$NEW" ]   || { echo "no binary at $NEW" >&2; exit 2; }
STOCK=$(cd "$(dirname "$STOCK")" && pwd)/$(basename "$STOCK")
NEW=$(cd "$(dirname "$NEW")" && pwd)/$(basename "$NEW")

ZAP="${ZAP_TREE:-$HOME/code/zap}"
DIRS=$(ls -d tests/*/ 2>/dev/null; ls -d "$ZAP"/test/corpus/*/tests 2>/dev/null; ls -d "$ZAP"/test/regress/*/tests 2>/dev/null)

W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
total=0; same=0; differ=0

for d in $DIRS; do
    [ -d "$d" ] || continue
    for src in "$d"/*.s "$d"/*.asm; do
        [ -f "$src" ] || continue
        base=$(basename "$src")
        bad=""
        for flags in "-c" "-c -l"; do
            rm -rf "$W/a" "$W/b"; mkdir -p "$W/a" "$W/b"
            cp -r "$d"/* "$W/a/" 2>/dev/null; cp -r "$d"/* "$W/b/" 2>/dev/null
            (cd "$W/a" && timeout 60 "$STOCK" "$base" out.bin $flags 2>&1 | grep -v '^Done in ' > s.log)
            (cd "$W/b" && timeout 60 "$NEW"   "$base" out.bin $flags 2>&1 | grep -v '^Done in ' > n.log)
            mv "$W/a/s.log" "$W/s.log"; mv "$W/b/n.log" "$W/n.log"
            diff -r "$W/a" "$W/b" > /dev/null 2>&1 || bad="$bad files($flags)"
            cmp -s "$W/s.log" "$W/n.log" || bad="$bad console($flags)"
        done
        total=$((total + 1))
        if [ -z "$bad" ]; then same=$((same + 1))
        else differ=$((differ + 1)); echo "DIFFER $(basename "$(dirname "$d")")/$base:$bad"
        fi
    done
done

echo "-----"
echo "$total sources, plain and -l"
echo "  $same agree"
echo "  $differ differ"
exit $([ "$differ" -eq 0 ] && echo 0 || echo 1)
