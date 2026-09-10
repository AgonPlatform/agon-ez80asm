# ez80asm on the Agon: measured optimisations

Every row is one change, measured on fab-agon-emulator 1.2.4 (Agon Light,
18.432 MHz) by reading the assembler's own `Done in` line, and verified against
the stock v2.2 binary over 561 sources with `opt/verify.sh` -- identical output
bytes and identical diagnostic counts, or the change does not count.

Seconds, lower is better. `x` is stock / this build.

## Baseline: stock v2.2 (241abaa)

| source | seconds |
|---|---|
| opcodes_l | 0.5300 |
| z80_undoc | 1.0850 |
| binarytest | 1.6300 |
| adl0label | 6.9700 |
| rokky | 2.5000 |
| bbcbasic (-m) | 22.4800 |

## 1. Table-driven isspace() and tolower()

`isspace()` and `tolower()` are library calls on the eZ80, and the tokeniser
calls one of them per character of every line, on both passes. Two 256-byte
tables and a macro apiece turn each call into an index and a load.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.5050 | 1.050 |
| z80_undoc | 0.9950 | 1.090 |
| binarytest | 1.4500 | 1.124 |
| adl0label | 7.0000 | 0.996 |
| rokky | 2.2400 | 1.116 |
| bbcbasic (-m) | 19.6600 | 1.143 |

Binary 55601 -> 55377 bytes.

Geomean 1.085x, whole set 1.105x.
