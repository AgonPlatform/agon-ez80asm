#!/bin/bash
# Assembles a set of sources twice on the Agon -- once with the stock v2.2
# binary, once with this build -- and compares what the two produced.
#
#   opt/verify-agon.sh [path-to-agon-binary]
#
# opt/verify.sh proves the changes on the PC, where int is 32 bits and uint24_t
# is a lie. This runs the real thing: 24-bit pointers, 24-bit int, agondev's
# library, the eZ80's byte order. The macros that read the bytes of a word are
# exactly the code that would notice, so they need checking here as well.
#
# Both binaries run in one boot, into their own directories, and the comparison
# is Agon against Agon -- the binary, the listing, and the console output with
# the timing line dropped.
set -uo pipefail
cd "$(dirname "$0")/.."

NEW="${1:-opt/build/ez80asm-agon.bin}"
STOCK="opt/ref/ez80asm-stock.bin"
EMU="${AGON_EMU:-$HOME/fab-agon-emulator-1.2.4}"
ZAP="${ZAP_TREE:-$HOME/code/zap}"
[ -f "$NEW" ]   || { echo "no binary at $NEW" >&2; exit 2; }
[ -f "$STOCK" ] || { echo "no stock Agon binary at $STOCK" >&2; exit 2; }

# name|directory|entry|flags -- one source per line. The regression sources are
# here on purpose: the line length and CRLF ones exercise the rewritten line
# readers at their boundaries, the truncation ones exercise the range macros,
# and the listing ones cover the paths that keep their character-at-a-time loop.
SET=$(cat <<SETEOF
opcodes|$ZAP/test/corpus/Opcodes/tests|opcodes_l.s|
undoc|$ZAP/test/corpus/Opcodes/tests|z80_undocumented.s|
numbers|$ZAP/test/corpus/Numbers/tests|compound_binarytest.s|
adl0label|$ZAP/test/corpus/Defines/tests|allowed16bitlabel_adl0.s|
rokky|$ZAP/test/corpus/Z_PRG_Agon-Rokky/tests|rokky.s|
line256|$ZAP/test/regress/limits/tests|line_256.s|
line257|$ZAP/test/regress/limits/tests|line_257.s|
linelong|$ZAP/test/regress/limits/tests|line_code_long.s|
crlf255|$ZAP/test/regress/limits/tests|line_crlf_255.s|
crlf256|$ZAP/test/regress/limits/tests|line_crlf_256.s|
line256m|$ZAP/test/regress/limits/tests|line_256.s| -m
line257m|$ZAP/test/regress/limits/tests|line_257.s| -m
linelongm|$ZAP/test/regress/limits/tests|line_code_long.s| -m
crlf255m|$ZAP/test/regress/limits/tests|line_crlf_255.s| -m
crlf256m|$ZAP/test/regress/limits/tests|line_crlf_256.s| -m
opcodesm|$ZAP/test/corpus/Opcodes/tests|opcodes_l.s| -m
rokkym|$ZAP/test/corpus/Z_PRG_Agon-Rokky/tests|rokky.s| -m
immtrunc|$ZAP/test/regress/values/tests|imm_truncated.s|
immwide|$ZAP/test/regress/values/tests|imm_wide.s|
fills|$ZAP/test/regress/values/tests|fills.s|
dsfill|$ZAP/test/regress/values/tests|ds_fillbyte.s|
dsinit|$ZAP/test/regress/values/tests|ds_initializer.s|
adl0word|$ZAP/test/regress/values/tests|adl0_wordsize.s|
listpad|$ZAP/test/regress/listing/tests|list_org_pad.s| -l
listrows|$ZAP/test/regress/listing/tests|list_rows.s| -l
listfwd|$ZAP/test/regress/listing/tests|list_forward.s| -l
foldrst|$ZAP/test/regress/folds/tests|fold_forward_rst.s|
orgfar|$ZAP/test/regress/addresses/tests|org_adl0_far.s|
reloc|$ZAP/test/regress/addresses/tests|relocate_adl0_wide.s|
bbcbasic|$ZAP/test/corpus/Z_PRG_Agon-bbc-basic-v/tests|bbcbasicvez.s| -m
SETEOF
)

W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
sd="$W/sd"; mkdir -p "$sd/bin"
cp -r "$EMU/sdcard/mos" "$sd/" 2>/dev/null
cp "$EMU/sdcard/MOS.bin" "$EMU/sdcard/firmware.bin" "$sd/" 2>/dev/null
cp "$STOCK" "$sd/bin/ezstock.bin"
cp "$NEW"   "$sd/bin/eznew.bin"
printf '  nop\n  ret\n' > "$sd/zzflush.s"

: > "$W/plan"
{
    i=0
    while IFS='|' read -r name dir file flags; do
        [ -n "$name" ] || continue
        i=$((i + 1))
        for side in a b; do
            mkdir -p "$sd/$side$i"
            cp -r "$dir"/* "$sd/$side$i/" 2>/dev/null
            rm -f "$sd/$side$i"/*.bin "$sd/$side$i"/*.lst "$sd/$side$i"/*.lbl
        done
        printf '%s|%d|%s\n' "$name" "$i" "$file" >> "$W/plan"
        printf 'cd /a%d\r\n'   "$i"
        printf 'ezstock %s out.bin -c%s\r\n' "$file" "$flags"
        printf 'cd /b%d\r\n'   "$i"
        printf 'eznew %s out.bin -c%s\r\n'   "$file" "$flags"
    done <<< "$SET"
    printf 'cd /\r\n'
    printf 'eznew zzflush.s zzf.bin -c\r\n'
    printf 'emulator_exit_success\r\n'
} > "$sd/autoexec.txt"

fifo="$W/f"; mkfifo "$fifo"
tail -f /dev/null > "$fifo" & hold=$!
(cd "$EMU" && timeout 1800 ./agon-cli-emulator --sdcard "$sd" -z < "$fifo" > "$W/cap" 2>&1)
kill "$hold" 2>/dev/null; wait "$hold" 2>/dev/null

boots=$(grep -c "MOS Version" "$W/cap" || true)
[ "${boots:-1}" -gt 1 ] && echo "WARNING: the machine reset $((boots - 1)) time(s)" >&2

# Split the transcript into one chunk per run: stock first, then this build,
# for each source in turn. A run starts at "Assembling <file>", except with -m,
# where the memory banner is printed first and starts it instead. The timing
# line is dropped, being the one thing that legitimately differs.
tr -d '\r' < "$W/cap" | awk '
    /^Setting minimum memory configuration/ { n++; started = 1 }
    /^Assembling /                          { if (!started) n++; started = 0 }
    n && !/^Done in /                       { print > (dir "/con." n) }
' dir="$W" -

# cmp, but two files that are both absent count as the same.
same_file() {
    if [ -f "$1" ] || [ -f "$2" ]; then cmp -s "$1" "$2"; else return 0; fi
}

total=0; same=0; differ=0
while IFS='|' read -r name i file; do
    total=$((total + 1))
    lst="${file%.*}.lst"
    bad=""
    same_file "$sd/a$i/out.bin" "$sd/b$i/out.bin" || bad="$bad bytes"
    same_file "$sd/a$i/$lst"    "$sd/b$i/$lst"    || bad="$bad listing"
    same_file "$W/con.$((2 * i - 1))" "$W/con.$((2 * i))" || bad="$bad console"
    if [ -z "$bad" ]; then
        same=$((same + 1))
    else
        differ=$((differ + 1))
        echo "DIFFER $name:$bad"
        case "$bad" in *console*)
            diff "$W/con.$((2 * i - 1))" "$W/con.$((2 * i))" | head -12 | sed 's/^/    /' ;;
        esac
    fi
done < "$W/plan"

echo "-----"
echo "$total sources assembled on the Agon by both binaries"
echo "  $same agree on bytes, listing and console"
echo "  $differ differ"
exit $([ "$differ" -eq 0 ] && echo 0 || echo 1)
