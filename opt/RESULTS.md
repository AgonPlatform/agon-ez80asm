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

## 2. Read a line with memchr() and memcpy()

Both line readers walked the input one character at a time, testing the line
length on every character. agondev's memchr() is `cpir` and its memcpy() is
`ldir`, so finding the newline and copying up to it costs a fraction of a C
loop. The character-at-a-time loop stays for the line-too-long case, which is
the only one that needs to report where it stopped.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.4950 | 1.071 |
| z80_undoc | 0.9400 | 1.154 |
| binarytest | 1.3900 | 1.173 |
| adl0label | 6.9900 | 0.997 |
| rokky | 2.1700 | 1.152 |
| bbcbasic (-m) | 14.7600 | 1.523 |

Geomean 1.168x, whole set 1.316x. Binary 55842 bytes.

## 3. Write runs of bytes in blocks

Three output paths moved one byte at a time through the buffer:

* `ioWrite()`, which INCBIN hands a whole file to at once,
* `ioFlushDSSpaces()`, which writes out a pending DS reservation,
* the fill loop in `handle_asm_org()`, which closes the gap to a new ORG.

The first now copies with memcpy(); the other two fill with memset() through a
new `io_outputfill()`. Each still flushes the buffer at exactly the same
points. The ORG fill keeps its character-at-a-time loop when a listing is being
produced, because the listing has to see every byte go by.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.5000 | 1.060 |
| z80_undoc | 0.9400 | 1.154 |
| binarytest | 1.3950 | 1.168 |
| adl0label | 0.1700 | 41.0 |
| rokky | 1.8800 | 1.330 |
| bbcbasic (-m) | 14.7800 | 1.521 |

Geomean 2.216x, whole set 1.790x. Binary 56222 bytes.

adl0label is a source whose ORG statements leave 96 KB of gap, so it was
spending nearly all of its time in that fill loop; 41x is what removing it
looks like and it pulls the geomean up on its own. Without that source the
geomean is 1.24x.

`opt/verify.sh` now assembles every source twice, plainly and with `-l`, and
compares the whole output directory -- binary, listing and label file -- plus
the console output with the timing line removed.

## 4. Classify characters with tables, not calls and chains

Three things in the same vein:

* `while(*src && ISSPACE(*src))` tests for end of string as well as for space,
  but zero is not a space, so the first test never decides anything. Dropped.
* The mnemonic scan asked four questions per character -- space, `;`, `:`,
  zero. One table answers all four at once.
* The expression parser called `strchr()` on a literal string for every
  operator test, including one inside the loop that reads a name. Three tables
  replace those, keeping `strchr(set, 0)`'s quiet habit of returning the set's
  own terminator, which the parser depends on at end of string.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.4950 | 1.071 |
| z80_undoc | 0.9250 | 1.173 |
| binarytest | 1.3350 | 1.221 |
| adl0label | 0.1700 | 41.0 |
| rokky | 1.8000 | 1.389 |
| bbcbasic (-m) | 14.3200 | 1.570 |

Geomean 2.271x (1.273x without adl0label), whole set 1.848x. Binary 56964
bytes.

## 5. Match register sets a byte at a time

The instruction matcher filters a mnemonic's candidate encodings with

    regamatch = (list->regsetA & operand1.reg) || !(list->regsetA | operand1.reg);

once per candidate -- and LD alone has about a hundred. The eZ80 has no 24-bit
AND or OR, so each of those lines is four library calls. A macro over the three
low bytes of the two words does the same job inline.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.3400 | 1.559 |
| z80_undoc | 0.8500 | 1.276 |
| binarytest | 1.2600 | 1.294 |
| adl0label | 0.1800 | 38.7 |
| rokky | 1.7300 | 1.445 |
| bbcbasic (-m) | 13.3400 | 1.685 |

Geomean 2.498x (1.444x without adl0label), whole set 1.988x. Binary 56999
bytes.

It has to be a macro. The same test written as a small function taking two
pointers made every source *slower* than leaving the library calls alone --
0.63s against 0.50s on opcodes_l, 15.08s against 14.32s on bbcbasic. A
function that the compiler gives a frame to, entered through a call with two
pushed arguments, costs more than the four hand-written library routines it was
meant to replace. The win is in not making a call at all.

## 6. Range-check by looking at the bytes

The truncation warnings ask whether a 32-bit value fits in one, two or three
bytes, signed or unsigned. On the eZ80 a 32-bit comparison is a library call,
and these run for every initializer in a DB/DW/DL list and for every immediate
an instruction emits. `OUTOFRANGE8/16/24` ask the same question of the bytes of
the word. The same treatment goes to `get_ddfd_prefix()` and the two index
register transforms, whose 24-bit ANDs were library calls too.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.3400 | 1.559 |
| z80_undoc | 0.8300 | 1.307 |
| binarytest | 1.2600 | 1.294 |
| adl0label | 0.1700 | 41.0 |
| rokky | 1.7200 | 1.453 |
| bbcbasic (-m) | 13.2800 | 1.693 |

Geomean 2.536x (1.453x without adl0label), whole set 2.000x. Binary 56886
bytes.

`validateRange8/16/24bit()` had to change to take the value by address. Given a
value it holds in registers, the compiler produced the bytes the macro asked
for with 32-bit shifts -- `__lshru`, `__ishru`, `__land`, more library calls
than the comparison it replaced -- and the first attempt cost bbcbasic 1.8%.
Passing an address leaves it no choice but to load the bytes.

## 7. Reject a candidate encoding on the cheapest test

The matcher computed all three of its tests for every candidate encoding before
looking at any of them. The addressing-mode test is two byte comparisons and
rules out nearly every candidate; the register-set tests are six byte loads
apiece. Testing the modes first and moving on when they disagree does the same
work in the same order, and stops early.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.2500 | 2.120 |
| z80_undoc | 0.8000 | 1.356 |
| binarytest | 1.2500 | 1.304 |
| adl0label | 0.1700 | 41.0 |
| rokky | 1.7100 | 1.462 |
| bbcbasic (-m) | 12.8600 | 1.748 |

Geomean 2.706x (1.571x without adl0label), whole set 2.065x. Binary 56874
bytes.

## 8. Test for space with arithmetic, not the table

The loop that skips a line's indentation is the busiest in the assembler: a
profile of bbcbasic puts 6% of everything inside it, and the source it is
reading is indented eight spaces a line. Asking `ctype_space[c]` costs a
24-bit immediate load, a zero-extend, a 24-bit add and a load, every character.
Asking whether the character is 0x20, or lands in 0x09-0x0D once 9 is
subtracted, is two compares on 8-bit registers.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.2500 | 2.120 |
| z80_undoc | 0.7800 | 1.391 |
| binarytest | 1.2050 | 1.353 |
| adl0label | 0.1800 | 38.7 |
| rokky | 1.6300 | 1.534 |
| bbcbasic (-m) | 12.5000 | 1.798 |

Geomean 2.743x (1.616x without adl0label), whole set 2.127x. Binary 56422
bytes.

Counting instructions would have picked the wrong one: the table loop is nine
instructions and the arithmetic loop twelve. What the table costs is bytes --
two 4-byte immediates per character -- and an access to memory the compare does
not need. The other classification tables stay; they answer questions with more
cases than two, and the space test is the only one running per character of the
indentation.

## 9. Fold case with arithmetic too

`TOLOWER()` gets the same treatment as `ISSPACE()`: a compare and an add rather
than a table index. It runs per character of every mnemonic on the way into the
instruction hash. A small change -- bbcbasic 12.50s to 12.48s, rokky 1.63s to
1.62s, nothing else moved -- and 234 bytes off the binary, so it stays.

Binary 56188 bytes. Geomean 2.747x, whole set 2.129x.
