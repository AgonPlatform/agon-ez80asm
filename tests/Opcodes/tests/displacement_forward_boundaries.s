; Forward .equ displacement boundaries on both index registers.
    ld a,(ix+minimum)
    ld a,(ix+negative)
    ld a,(ix+zero)
    ld a,(ix+maximum)
    ld a,(iy+minimum)
    ld a,(iy+negative)
    ld a,(iy+zero)
    ld a,(iy+maximum)
    ld a,(iy+offset-1)
    ld (ix+minimum),value
    ld (iy+maximum),value
minimum: .equ -128
negative: .equ -1
zero: .equ 0
maximum: .equ 127
offset: .equ 5
value: .equ 42
