; Target is exactly +127 from the next instruction.
org 0
    djnz target
    blkb 127,0
target: nop
