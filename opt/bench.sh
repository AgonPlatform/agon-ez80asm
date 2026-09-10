#!/bin/bash
# Times an Agon build of ez80asm on a fixed set of sources, on the emulator.
#
#   opt/bench.sh [path-to-agon-binary] [runs] [fast]
#
# THE TIMING IS THE ASSEMBLER'S OWN `Done in` line, and the emulator runs
# WITHOUT -u: unthrottled, the guest clock stops measuring guest work.
#
# One boot for the whole set: a boot costs about ten seconds and there is no
# reason to pay it six times. Each source gets its own directory on the card,
# is assembled `runs` times, and the reported times are summed and divided --
# the clock counts hundredths, so a source that takes 30 ms otherwise reads
# 0.03 give or take a whole tick. A throwaway assembly goes last, because the
# emulator stops the moment the guest writes the exit port and the final line
# would still be flushing.
#
# `fast` as the third argument runs only the three quick sources, which is what
# to use while iterating; the whole set is for confirming a change.
set -uo pipefail
cd "$(dirname "$0")/.."

BIN="${1:-opt/build/ez80asm-agon.bin}"
RUNS="${2:-4}"
MODE="${3:-full}"
EMU="${AGON_EMU:-$HOME/fab-agon-emulator-1.2.4}"
ZAP="${ZAP_TREE:-$HOME/code/zap}"
[ -f "$BIN" ] || { echo "no binary at $BIN" >&2; exit 2; }

# name|directory|entry|flags|runs   -- runs 0 means use $RUNS
SET=$(cat <<SETEOF
opcodes_l|$ZAP/test/corpus/Opcodes/tests|opcodes_l.s||0
z80_undoc|$ZAP/test/corpus/Opcodes/tests|z80_undocumented.s||0
binarytest|$ZAP/test/corpus/Numbers/tests|compound_binarytest.s||0
adl0label|$ZAP/test/corpus/Defines/tests|allowed16bitlabel_adl0.s||2
rokky|$ZAP/test/corpus/Z_PRG_Agon-Rokky/tests|rokky.s||2
bbcbasic|$ZAP/test/corpus/Z_PRG_Agon-bbc-basic-v/tests|bbcbasicvez.s| -m|1
SETEOF
)
[ "$MODE" = fast ] && SET=$(printf '%s\n' "$SET" | head -3)

W=$(mktemp -d); trap 'rm -rf "$W"' EXIT
sd="$W/sd"; mkdir -p "$sd/bin"
cp -r "$EMU/sdcard/mos" "$sd/" 2>/dev/null
cp "$EMU/sdcard/MOS.bin" "$EMU/sdcard/firmware.bin" "$sd/" 2>/dev/null
cp "$BIN" "$sd/bin/ez80asm.bin"
printf '  nop\n  ret\n' > "$sd/zzflush.s"

: > "$W/plan"
{
    i=0
    while IFS='|' read -r name dir file flags runs; do
        [ -n "$name" ] || continue
        i=$((i + 1))
        n=$runs; [ "$n" = 0 ] && n=$RUNS
        mkdir -p "$sd/d$i"
        cp -r "$dir"/* "$sd/d$i/" 2>/dev/null
        printf '%s|%d|%d\n' "$name" "$i" "$n" >> "$W/plan"
        printf 'cd /d%d\r\n' "$i"
        for ((r = 0; r < n; r++)); do
            printf 'ez80asm %s out.bin -c%s\r\n' "$file" "$flags"
        done
    done <<< "$SET"
    printf 'cd /\r\n'
    printf 'ez80asm zzflush.s zzf.bin -c\r\n'
    printf 'emulator_exit_success\r\n'
} > "$sd/autoexec.txt"

fifo="$W/f"; mkfifo "$fifo"
tail -f /dev/null > "$fifo" & hold=$!
(cd "$EMU" && timeout 1800 ./agon-cli-emulator --sdcard "$sd" -z < "$fifo" > "$W/cap" 2>&1)
kill "$hold" 2>/dev/null; wait "$hold" 2>/dev/null

boots=$(grep -c "MOS Version" "$W/cap" || true)
if [ "${boots:-1}" -gt 1 ]; then
    echo "WARNING: the machine reset $((boots - 1)) time(s); timings below are not trustworthy" >&2
fi

tr -d '\r' < "$W/cap" | grep '^Done in ' | awk '{print $3}' > "$W/times"
printf '%-12s %8s\n' source seconds
awk -F'|' -v tf="$W/times" '
    BEGIN { while ((getline t < tf) > 0) all[++m] = t; k = 0 }
    {
        s = 0; ok = 1
        for (r = 1; r <= $3; r++) { if (++k > m) { ok = 0; break } s += all[k] }
        if (ok) printf "%-12s %8.4f\n", $1, s / $3
        else    printf "%-12s %8s\n", $1, "short"
    }' "$W/plan"
