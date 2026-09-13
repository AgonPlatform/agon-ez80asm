; Target is exactly -128 from the next instruction.
org 0
target: nop
    blkb 125,0
    djnz target
