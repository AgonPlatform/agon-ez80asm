; Change ADL after each forward operand, before its definition.
org 0
assume adl=0
ld hl,value
call target
assume adl=1
ld hl,value
call target
assume adl=0
value: .equ $1234
target: nop
