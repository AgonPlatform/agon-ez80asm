# ez80asm on the Agon: measured optimisations

Every row is one change, measured on fab-agon-emulator 1.2.4 (Agon Light,
18.432 MHz) by reading the assembler's own `Done in` line, and verified against
the stock v2.2 binary over 561 sources with `opt/verify.sh` -- identical output
bytes and identical diagnostic counts, or the change does not count.

Seconds, lower is better. `x` is stock / this build.

## Where it ends up

Fifteen changes, each measured on its own and each leaving the output
identical. None of them changes how the assembler works: still two passes,
still the same files, the same structures and the same order of decisions.

| source | stock v2.2 | now | x |
|---|---|---|---|
| opcodes_l | 0.5300 | 0.2350 | 2.26 |
| z80_undoc | 1.0850 | 0.7600 | 1.43 |
| binarytest | 1.6300 | 1.1500 | 1.42 |
| adl0label | 6.9700 | 0.1900 | 36.7 |
| rokky | 2.5000 | 1.5500 | 1.61 |
| bbcbasic (-m) | 22.4800 | 11.5200 | 1.95 |
| **total** | **35.1950** | **15.4050** | **2.28** |

Geomean 2.85x, or 1.69x leaving adl0label out -- that source spends nearly all
of its time filling an ORG gap, and 41x is what writing that gap in blocks
rather than a byte at a time is worth. Binary 55601 -> 56597 bytes.

What did the work, roughly in order of what it was worth:

1. Writing runs of bytes in blocks (ORG fills, DS fills, INCBIN).
2. Reading a line with memchr() and memcpy() instead of character by character,
   and refilling so that a line never spans a refill.
3. Matching register sets on their bytes rather than through the 24-bit AND
   and OR library calls.
4. Rejecting a candidate encoding on the cheapest of its three tests.
5. Table-driven, then arithmetic, character classification in place of
   <ctype.h> calls and strchr() on string literals.
6. Not calling functions that were going to decide they had nothing to do.

Two things that did not work are written up where they happened: a register-set
comparison written as a function instead of a macro (section 5), and an
arithmetic ISMNEMONICEND() (last section).

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

## 10. Three calls per emitted byte, down to none

`emit_8bit()` is the most-run path in the assembler, and every byte through it
made three calls: `ioFlushDSSpaces()`, which does nothing unless a DS is
pending; `io_outputc()`, which is four lines; and `_io_flush()` once a bufferful.
Testing `remaining_dsspaces` before the first and writing the four lines in
place leaves only the flush, once every 32 KB.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.2450 | 2.163 |
| z80_undoc | 0.7700 | 1.409 |
| binarytest | 1.1950 | 1.364 |
| adl0label | 0.1700 | 41.0 |
| rokky | 1.6000 | 1.562 |
| bbcbasic (-m) | 12.3800 | 1.816 |

Geomean 2.802x (1.638x without adl0label), whole set 2.151x. Binary 56234
bytes.

## 11. Ask about the literal flag only when it can matter

`getOperandToken()` tested the in-a-literal flag for every character of every
operand, to decide whether a comma ends the token -- but only three characters
can end anything, so ordinary ones can get past on three comparisons with no
state to load. The flag is consulted when one of the three turns up.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.2450 | 2.163 |
| z80_undoc | 0.7650 | 1.418 |
| binarytest | 1.1850 | 1.376 |
| adl0label | 0.1700 | 41.0 |
| rokky | 1.6000 | 1.562 |
| bbcbasic (-m) | 12.3000 | 1.828 |

Geomean 2.812x (1.645x without adl0label), whole set 2.164x. Binary 56231
bytes.

## Verification on the Agon itself

`opt/verify.sh` runs on the PC, where `int` is 32 bits and `uint24_t` is a
32-bit type wearing a hat. The macros added here read the bytes of a word, so
the machine they run on matters. `opt/verify-agon.sh` boots the emulator once,
assembles 23 sources with the stock v2.2 binary and with this build into
separate directories, and compares the binary, the listing and the console
output of each pair. 23 of 23 agree.

The set is chosen for what changed: line lengths at 256 and 257 characters and
with CRLF endings for the rewritten line readers, truncated and wide immediates
for the range macros, DS and ORG fills for the block writer, three listings for
the paths that keep their character-at-a-time loop, and bbcbasic under `-m`.

## 12. Ask before calling a function that will do nothing

`emit_instruction()` made three calls per instruction that mostly return
without doing anything: `prefix_ddfd_suffix()`, which begins by checking
`F_DDFDOK` -- two thirds of encodings do not have it -- and
`transform_instruction()` twice, three quarters of whose transform slots are
`TRANSFORM_NONE`. The tests move to the call site.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.2300 | 2.304 |
| z80_undoc | 0.7700 | 1.409 |
| binarytest | 1.1600 | 1.405 |
| adl0label | 0.1800 | 38.7 |
| rokky | 1.5800 | 1.582 |
| bbcbasic (-m) | 12.1200 | 1.855 |

Geomean 2.834x (1.680x without adl0label), whole set 2.194x. Binary 56255
bytes.

## 13. Ask before calling definelabel() as well

`definelabel()` has nothing to do when the line carries no label: pass 1
returns immediately, and pass 2 only ever sets the label scope. Most lines
carry no label, and `emit_instruction()` calls it for every instruction it
emits. `DEFINELABEL()` puts the test at the call site.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.2400 | 2.208 |
| z80_undoc | 0.7600 | 1.428 |
| binarytest | 1.1500 | 1.417 |
| adl0label | 0.1700 | 41.0 |
| rokky | 1.5600 | 1.603 |
| bbcbasic (-m) | 11.9400 | 1.883 |

Geomean 2.865x (1.683x without adl0label), whole set 2.225x. Binary 56408
bytes.

opcodes_l goes the other way by a hundredth of a second, on a source that is
almost entirely labelled lines, where the added test never saves the call.

## Tried and dropped: an arithmetic ISMNEMONICEND()

`ISMNEMONICEND()` asks four questions -- zero, space, ':' and ';' -- and
`ISSPACE()` had just shown that comparisons beat a table index. Written as four
comparisons it was slower on every source that moved: opcodes_l 0.235s against
0.230s, binarytest 1.170s against 1.160s, bbcbasic 12.16s against 12.12s. Two
comparisons beat a table lookup here and four do not, which puts the boundary
somewhere in between, and the table stays.

## 14. Refill so the buffer ends on a line boundary

Under `-m` the reader refilled when it ran out of bytes, which happens in the
middle of a line, so it had to copy the front of the line, refill, and carry on
-- an outer loop, a running length, and a moving destination, all for the sake
of one line in every bufferful.

`_fillLineBuffer()` moves the decision to the refill. Whatever the last read
left after its final newline is carried to the front of the buffer, and the
usable end is pulled back to the last newline in what is now there, so every
line in the buffer is whole. The reader becomes a memchr(), a memcpy() and no
loop at all. At end of file, a short read means what is left is the last line,
newline or not, and there is nothing to pull back to.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.2350 | 2.255 |
| z80_undoc | 0.7600 | 1.428 |
| binarytest | 1.1500 | 1.417 |
| adl0label | 0.1900 | 36.7 |
| rokky | 1.5600 | 1.603 |
| bbcbasic (-m) | 11.6400 | 1.931 |

Geomean 2.836x (1.694x without adl0label), whole set 2.244x. Binary 56597
bytes. Nothing but bbcbasic uses `-m`; adl0label reads 0.19s against 0.17s,
which is two hundredths on a source that finishes in a fifth of a second.

The buffer has to be larger than `LINEMAX` for this: a full buffer with no
newline in it then means one line is longer than a line is allowed to be,
which is exactly the case the character-at-a-time path is kept for. At 1 KiB
and 4 KiB it is, by a wide margin.

`opt/verify.sh` now assembles every source four ways -- plainly, `-l`, `-m` and
both -- because `-m` is a different reader and nothing else was exercising it.
The refill logic was also run with the buffer cut to 512 bytes, so that the
carry path fires every fifteen lines or so: 561 of 561 still agree.

## 15. A 4 KiB input buffer for -m

With the refill above in place, buying more buffer is worth less than it was,
and it was not worth much:

| INPUT_BUFFERSIZE | before section 14 | after |
|---|---|---|
| 1024 | 11.9400 | 11.6400 |
| 4096 | 11.8400 | 11.5200 |

The buffer is a local of `processContent()`, which recurses once per include,
so the memory to compare is `MAXPROCESSDEPTH` copies of it: 8 KiB against
32 KiB. Four times the memory buys 1%, in the mode whose whole purpose is not
using memory -- and a 1 KiB buffer with the trimmed refill already beats a
4 KiB buffer without it. It is separated out so it can be taken or left on its
own.

Geomean 2.845x, whole set 2.257x.

## 16. One way to read a file

`-m` chose between two readers: without it, every source file was read whole
into memory and lines were handed out of that; with it, files were read a
buffer at a time. The first is why a large project could not be assembled on
an Agon at all -- the BBC BASIC tree is 386 KB of source on a 512 KB machine,
and without `-m` it resets the machine, twenty-six times in four minutes of
trying.

With the refill of section 14 in place, the buffered reader is not slower than
the resident one anywhere. Every source, both ways, on the same binary:

| source | whole file in memory | a buffer at a time |
|---|---|---|
| opcodes_l | 0.2350 | 0.2300 |
| z80_undoc | 0.7600 | 0.7500 |
| binarytest | 1.1500 | 1.1250 |
| adl0label | 0.1900 | 0.1800 |
| rokky | 1.5500 | 1.5500 |
| bbcbasic | resets the machine | 11.5200 |

So the resident reader is a mode that is never faster, cannot do the big job,
and costs a second reader, a second INCBIN path, a branch in four files and a
per-file allocation of the whole source. It goes. `-m` is still accepted and
ignored, so command lines that pass it keep working.

| source | seconds | x |
|---|---|---|
| opcodes_l | 0.2350 | 2.255 |
| z80_undoc | 0.7450 | 1.456 |
| binarytest | 1.1150 | 1.462 |
| adl0label | 0.1700 | 41.0 |
| rokky | 1.5200 | 1.645 |
| bbcbasic | 11.4800 | 1.958 |

Geomean 2.931x (1.729x without adl0label), whole set 2.306x. Binary 56597 ->
55853 bytes.

One thing this cannot measure. The emulator's SD card is a directory on the
host, so reading a file costs almost nothing; on real hardware it does not.
The resident reader read each file once, in pass 1, and pass 2 came out of
memory; the buffered reader reads every file in both passes. For the BBC BASIC
tree that is 772 KB off the card instead of 386 KB. On a real Agon that
difference is worth measuring before taking this change -- though the mode it
replaces cannot assemble that tree at all.
