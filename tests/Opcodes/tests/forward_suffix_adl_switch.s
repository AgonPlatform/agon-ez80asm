; Explicit suffix widths survive subsequent ADL changes.
org 0
assume adl=0
ld.sis hl,value
assume adl=1
assume adl=0
ld.lis hl,value
assume adl=1
assume adl=0
ld.sil hl,value
assume adl=1
assume adl=0
ld.lil hl,value
assume adl=1
assume adl=1
ld.sis hl,value
assume adl=0
assume adl=1
ld.lis hl,value
assume adl=0
assume adl=1
ld.sil hl,value
assume adl=0
assume adl=1
ld.lil hl,value
assume adl=0
value: .equ $1234
