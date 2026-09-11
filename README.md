# agon-ez80asm
## Installation and requirements
### Latest version
Download the latest [release](https://github.com/envenomator/agon-ez80asm/releases) and place the file 'ez80asm.bin' in the /bin/ folder of your microSD card. 

The assembler requires at least Console8 MOS 2.2.0, which supports a /bin/ folder for executable binaries.

### Older release versions (up to and including 1.4)
Download an older release and place both 'ez80asm.bin' and 'ez80asm.ldr' in the /mos/ folder of your microSD card.
These versions require at least MOS 1.03.

## Usage

    ez80asm <filename> [output filename] [OPTION]
    -v List version information only
    -h List help information
    -o Org start address in hexadecimal format, default is 40000
    -b Fillbyte in hexadecimal format, default is FF
    -a ADL mode 1/0, default is 1
    -i Ignore value truncation warnings
    -l Listing to file with .lst extension
    -s Export symbols
    -d Direct listing to console
    -c No color codes in output (version 1.3+)
    -x Display assembly statistics (version 1.1+)
    -m Accepted and ignored (version 2.3+); source files are always read a buffer at a time

The given filename will be assembled into these files:
- filename.bin -- output executable file
- filename.lst -- output assembler listing (optionally selected by -l flag)
- filename.symbols -- symbol export when the -s option is used

## Defaults

The assembler defaults to ADL=1 mode, CPU type EZ80. This can be overridden using the .ASSUME / .CPU directives.

The default address is 0x40000.

## Source file format
Lines in the source file should have the following format:

    [Label:] [Instruction] [Operand] [;Comment]

All fields are optional.

- Labels are defined as first token on a line, terminated without whitespace by a colon (':')
- Instructions are either z80/z180/ez80 opcodes, or assembler directives
- Starting v2.0, labels and instructions can be preceded by whitespace
- Assembler directives can optionally start with a dot (.)
- Comments start with a semi-colon (;)


Examples

```
    LABEL:                  ; Label without operator
    LABEL:LD A,B            ; Instruction after label without whitespace
    LABEL: LD A,B           ; Instruction after label with whitespace
     LD A,B                     ; Instruction without label, starting with whitespace
    ; comment on empty line
```

## Global labels

Global labels are reachable in the entire source file and cannot have the same name as ez80 mnemonics, condition names, number values, nor assembler directives.

- Global labels are case-sensitive.
- Global labels cannot start with a '$' character.
- Global labels have a maximum length of 32 characters
- Numbers cannot be used as a label name
- Register names can be used as a label name, however doing so is not recommended.
- Global labels cannot be **defined** inside a macro, however global labels that are defined outside the macro, can be **referenced** inside a macro.

### EQU labels
By using the EQU directive, global labels can be defined as a number constant instead of an address. These constants can be used in places where a number is expected in an operand. They can also be used with other directives; however the directive determines if such a constant can be defined before and/or after the directive itself. See the table below for additional details:

|        **Directive**        | **EQU allowed before directive** | **EQU allowed after directive** |
|:---------------------------:|:--------------------------------:|:-------------------------------:|
|            ALIGN            |                Yes               |                No               |
|          ASSUME ADL         |                Yes               |                No               |
|   DB/DEFB/ASCII/ASCIZ/BYTE  |                Yes               |               Yes               |
|     DW/DEFW/DL/DW24/DW32    |                Yes               |               Yes               |
| DS/DEFS/BLKB/BLKW/BLKP/BLKL |                Yes               |     No - number, Yes - value     |
|             EQU             |                Yes               |                No               |
|           FILLBYTE          |                Yes               |                No               |
|        INCLUDE/INCBIN       |                No                |                No               |
|              IF             |                Yes               |                No               |
|             ORG             |                Yes               |                No               |

**Why is this order important?**

To avoid additional assembly passes and needing to check how many additional passes would be needed, all to keep the performance impact to a minimum.

EQU  order examples:
```
; These EQU definition orders are fine

; EQU after EQU
value1: .equ 10
value2: .equ value1

; EQU after DB
 .db value3

value3: .equ 100
```

```
; Any of these orders will error out

value1: .equ value2      ; Error: forward EQU, would need extra pass

        .fillbyte value2 ; Error: forward EQU

value2: .equ 10
```

## Local labels
Local labels start with the '@' symbol, terminated by a colon (':') and define a temporary label between two global labels. As soon as a new global label starts, all local labels become out of scope and a new scoped local label space starts.

- Local labels are case-sensitive.
- Global labels have a maximum length of 32 characters, including the '@' symbol
- Up to version 1.3, 64 local labels are currently supported in each scope. Version 1.4+ allows an infinite number, limited by the amount of available memory
- A local label is the only type of label that can be defined inside of a macro, which will then have a private internal per-expansion scope that is invisible from outside the macro
- A local label defined **outside** a macro cannot be referenced from **inside** a macro.

Example usage of local labels:

```
main:
    ld hl, string
    call print_zeroterm_string

    ld hl, string
    ld a, stringend - string
    call print_string
    ret

print_zeroterm_string: ; a global label
    call setup
@localloop:            ; A local label
    ld a, (hl)         ; Load content
    ret z
    call outchar
    jr @localloop
    ret

print_string:          ; a global label
    call setup
    ld b,a             ; counter
@localloop:            ; A local label in a different scope
    ld a, (hl)
    call outchar
    djnz @localloop
    ret

setup:
    ; some setup code here
    ret

outchar:
    ; some code here that outputs a single character
    ret

string:
    .db "String to output"
stringend:
    .db 0
```

Up to version 1.3, the 'scope' of local labels was tracked using a file. This proved to be relatively costly method performance-wise. Starting version 1.4, all local labels are prefixed with their scope and placed in the memory label table. Local labels defined before a global label definition, are prefixed with their source filename. Local labels defined within the scope of a global label are prefixed with their global label.
While this makes it possible to immediately reference them from outside of their immediate scope by using their prefix, it is not recommended to do so; this method might change in a future release.

## Anonymous labels
Anonymous labels are labeled '@@', terminated by a colon (':') and define a temporary label. As soon as a new anonymous label is defined, the previous is no longer reachable.
Code can use @f/@n or @b/@p, to get the label in the FORWARD/NEXT, or BACKWARD/PREVIOUS direction.

Anonymous labels cannot be **defined** inside a macro. To optimize for performance, **referencing** an **outside** anonymous label from inside a macro isn't prohibited. Be mindful using such references, because they might result in unexpected program behavior.

Example usage of anonymous labels:

```
main:
    ld a, 3
    call routine
    ret

routine:
    ld b, 8
@@:
    cp 10        ; A can never reach 10 in this example
                 ; to show jumping to next local label

    jr c, @n     ; jump to local label in forward/next direction
    call output
    inc a
    djnz @p      ; jump to local label in back/previous direction
@@:
    ret

output:
    ; do some output here
    ret
```

## Numeric constants

The following formats are supported:

- Hexadecimal - constants starting with 0x, 0X, $, or #
- Hexadecimal - constants ending with h or H
- Binary - constants starting with 0b, 0B or %
- Binary - constants ending with b, B
- Decimal

Examples:

    $0A     ; Hex
    0x0A    ; Hex
    #0A     ; Hex
    0Ah     ; Hex
    0Bh     ; Hex (not binary - 'h' suffix takes priority over '0B' prefix)
    0b1010  ; Bin
    1010b   ; Bin
    %1010   ; bin
    10      ; Dec

$ represents the current program counter

## Character constants (literals)
Character constants are surrounded by single quotes. Supported escape sequences are:

\\a - (0x07) - Alert, beep (v1.3+)

\\b - (0x08) - Backspace

\\e - (0x1B) - Escape (v1.3+)

\\f - (0x0C) - Formfeed (v1.3+)

\\n - (0x0A) - Newline

\\r - (0x0D) - Carriage return

\\t - (0x09) - Horizontal tab

\\v - (0x0B) - Vertical tab (v1.3+)

\\\ - (0x5C) - backslash

\\' - (0x27) - Single quotation mark

\\" - (0x22) - Double quotation mark

\\? - (0x3F) - Question mark (v1.3+)

Examples:

    DB 'a'  ; Defines byte with hex value 61
    DB 'B'  ;                   hex value 41
    DB '\n' ;                   hex value 0A

## Value expressions
The assembler accepts an expression at any location where a value is expected. An expression can consist of a single numberic constant, a label, or any combination of these using operators. An expression needs at least a single contant or label.

Starting v1.11, operations can be prioritized using [ brackets ], which can be nested.
Examples of expressions for loading values into the A register:
In versions up to and including v1.10, operators in expressions had no precendence other than left-to-right and parenthesis aren't supported yet. So be careful using more than one operator in an expression using these older versions.

```
    LD A, 5
    LD A, xval
    LD A, -xval
    LD A, xval + 5
    LD A, xval + temp2          ; v1.10 and earlier versions expression using temp values
    LD A, xval + [2*[yval + 1]] ; v1.11+ version supporting brackets

xval:     .EQU 10
yval:     .EQU 4

temp1:    .EQU yval + 1
temp2:    .EQU 2*temp1
```

## Supported operators
Regular operators (between constants, labels or bracketed expressions):
- \+ (plus)
- \- (minus)
- \* (multiply)
- / (divide)
- << (shift bits left)
- \>\> (shift bits right)
- & (bitwise and)
- | (bitwise or)
- ^ (bitwise xor)

Unary operators (before a single constant, label or bracketed expression):
- ~ (bitwise not)
- \- (negative)

## String expressions
Strings are defined as literal characters surrounded by double quotes. The supported character escape sequence constants are supported in strings as well.

Examples:

```
    DB "Test"    ; Outputs hex values 54, 65, 73, 74
    DB "Test",0  ; Outputs hex values 54, 65, 73, 74, 0
    DB "Test\n"  ; Outputs hex values 54, 65, 73, 74, 0A
    ASCIZ "Test" ; Outputs hex values 54, 65, 73, 74, 0
```

## Supported Assembler directives
All assembler directives can be used with, or without leading dot (.). If a directive starts with a dot (.), it can start at the first column on a line. If a directive doesn't start with a dot (.), it it cannot start at the first column on a line and needs any amount of preceding space. 

### Positioning of a directive
```
;12345678901234567890 location position
.db 5      ; valid use of 'DB' directive
 db 5      ; valid use of 'DB' directive
db 5       ; invalid use of 'DB' directive, shows as 'invalid label'
```

### Directive overview
| Directive              | Description                                                                        | Usage                                                                                                                                                                                                                                                                                                                                                                     |
|------------------------|------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| ALIGN                  |  Select alignment for the program counter, pad bytes up to new boundary alignment  | ALIGN \<boundary\>  The boundary, expressed in bytes, must be a power of two (1,2,4,8 etc)  Example: ALIGN 256 ; align next instruction at boundary of 256                                                                                                                                                                                                                |
| ASSUME ADL             | Sets default ADL status (0 or 1)                                                   | Example: ASSUME ADL=1 ; set ADL mode to 1                                                                                                                                                                                                                                                                                                                                 | 
| CPU (v2.0+)                   | Limit instruction/opcode output to the specified CPU only                | Instructs the assembler that instructions placed after this directive need to be supported for the specified CPU only, or the assembler will need to throw an error. The assembler startup default is type EZ80. Supported CPU types are Z80 / Z180 / EZ80.                                                                                                                                                                                                 |
BLKB/BLKW/ BLKP/BLKL   | Allocate a block with a number of Bytes/ 16-bit words/24-bit Pointers/32-bit words | BLKx \<number\> [, value]  Example: BLKB 16 ; allocate 16 uninitialized bytes Example: BLKB 16, 0xFF ; allocate 16 bytes and initialize them to 0xFF                                                                                                                                                                                                                      |
| DB / DEFB ASCII / BYTE | Define byte(s)                                                                     | DB \| DEFB \| ASCII \| BYTE \<value \| string\> [, ...]  Reserve and initialize a byte, or a list of bytes. Within a string, these escape characters are supported and converted to their respective ascii code: \ \r \t \b \\\ \\' \\"  Strings are not automatically terminated by a null byte. You can use either an additional ',0' value, or use the ASCIZ directive |
| ASCIZ                  | Same as above, but terminated with a 0 Used for zero-terminated strings mostly     | ASCIZ \<value \| string\> [, ...]                                                                                                                                                                                                                                                                                                                                         |
| DW / DEFW              | Define 16-bit word(s)                                                              | DW \| DEFW \<value\> [, ...]  Reserve and initialize a word/long value, or a list of values                                                                                                                                                                                                                                                                               |
| DL / DW24              | Define 24-bit word(s)                                                              | DL \| DW24 \<value\> [, ...]                                                                                                                                                                                                                                                                                                                                              |
| DW32                   | Define 32-bit word(s)                                                              | DW32 \<value\> [, ...]                                                                                                                                                                                                                                                                                                                                              |
| DS / DEFS              | Defines storage location in bytes                                                  | DS \| DEFS \<number\> Example: DS 10 ; reserve 10 byte. If a .DS space is defined between opcodes, the fillbyte value is used in output to the binary file.                                                                                                                                                                                                                             |
| EQU                    | Assign symbolic name to label                                                      | Example: LABEL: EQU 0xFF                                                                                                                                                                                                                                                                                                                                                  |
| FILLBYTE               | Change the byte value that is used for filling unused space                        | FILLBYTE \<value\>                                                                                                                                                                                                                                                                                                                                                        |
| INCBIN                 | Include binary file                                                                | Allows the insertion of binary data from another file  Example: INCBIN "sprite.bin"                                                                                                                                                                                                                                                                                       |
| INCLUDE                | Include file in source code                                                        | Allows the insertion of source code from another file into the current source file during assembly. The included file is assembled into the current source file immediately after the directive. When the EOF (End of File) of the included file is reached, the assembly resumes on the line after the INCLUDE directive  Example: INCLUDE "example.inc"                 |
| MACRO / ENDMACRO       | Define a macro, see below for detailed explanation                                 | MACRO [arg1, arg2 ...]  [macro body] ENDMACRO                                                                                                                                                                                                                                                                                                                             |
| ORG                    | Define location counter origin.                                        | Sets the assembler location counter to a specified value. The directive must be followed by an integer constant, which is the value of the new origin. Example: ORG $40000. Starting release 1.9, when the location counter is advanced, the intervening bytes are filled with the defined fillbyte.ORG may only increase the location counter, or leave it unchanged; you cannot use ORG to move the location counter backwards.                                                                                                                                                                                                |
| RELOCATE / ENDRELOCATE (v2.0+)                   | Defines a block of code as relocatable                                         | RELOCATE \<address> [instruction lines] \<ENDRELOCATE>   The specified block of code is output at the current <em>address</em>, whilst all labels in the block are calculated with an applied offset. This allows the block of code to be copied elsewhere to the specified address, due to it's (internal) use of offset labels. The $ symbol is also translated in the specified block.                                                                                                                                                                                                |
## Macros
The 'macro' directive defines a macro, optionally followed by a maximum of 8 arguments. The following lines will be stored as the macro-body, until the 'endmacro' directive is encountered. A macro has to be defined before use.

Macro names have a maximum length of 32 characters. Macro argument names have a maximum length of 32 characters each. When calling a macro (expansion), each expanded arguments can accomodate a different maximum length of 66 (v1.11, for earlier versions this was 32), which is the maximum file length (64) plus space for double quoting the string (2).

Example macro definition without arguments:

        macro addhla ; start of macro definition
        add a,l
        jr nc, @1
        inc h
    @1:
        ld l,a
        endmacro    ; end of macro definition

Important notes:
- Starting v2.0 only **local** labels are allowed in macro definitions; global labels were never allowed, while defining anonymous labels in a macro could result in issues using earlier versions. Starting v2.0, local labels defined inside a macro will get an internal macro expansion 'scope', so the macro can be expanded multiple times without label collisions. This 'scope' will not be visible in the listing. Because of their locality, labels defined inside a macro cannot be referenced to outside of the macro
- Starting v2.0, macros can be called from within a macro, with a maximum 'nesting' level of 8 to block expansion recursion.
- Macro definitions are not allowed inside a macro definition
- Don't use instruction names (e.g. 'ld' or 'and'), nor other macro names as argument names. Starting v2.0 the assembler will throw an error.
- While accepted by the assembler; avoid defining macros with the same name as a label. This can lead to confusion between calling a label and invoking a macro in your code.

Example macro with arguments:

        macro pointless arg1, arg2
        add a, arg1
        add l, arg2
        endmacro

Example macro usage / expansion later in the code:

        pointless 10, 15
        ; will expand to add a, 10
        ;                add l, 15

## Basic conditional assembly support
The assembler supports basic conditional assembly using if/else/endif assembler directives. There is no support for nested conditions.

The 'if' directive assesses the value of the given value, usually a label. When this is non-zero, the lines following the directive are assembled up to the 'else' or 'endif' statement. If the value is zero, any lines after an appearing 'else' statement are assembled up to the 'endif' statement.

Example usage with direct listing, to show which blocks are assembled. Please observe that the same label can be 'defined' different, according to the conditional flow:
```
PC     Output      Line
040000             0001  value1:  .equ 1
040000             0002  value2:  .equ 0
040000             0003
040000             0004  proc1:
040000             0005  .if value1           ; this block will be selected
040000 21 06 00 04 0006      ld hl, localvar  ; loading the address of the label in this block
040004 7E          0007      ld a, (hl)       ; A will be loaded with 1
040005 C9          0008      ret
040006             0009  variable:
040006 01          0010  localvar: .db 1
040007             0011  .else
040007             0012      ld hl, localvar
040007             0013      ld a, (hl)
040007             0014      ret
040007             0015      .asciz "This should not be here"
040007             0016  variable:
040007             0017  localvar: .db 0      ; ; the same labels can be 'defined'
040007             0018  .endif
040007             0019
040007             0020  proc2:
040007             0021  .if value2           ; this block will NOT be selected
040007             0022  ;.if value1-1        ; would have resulted in the same flow
040007             0023      ld hl, localvar2 ; loading the address of the label in this block
040007             0024      ld a, (hl)       ; A will be loaded with 1
040007             0025      ret
040007             0026  variable2:
040007             0027  localvar2: .db 1
040007             0028  .else
040007 21 21 00 04 0029      ld hl, localvar2
04000B 7E          0030      ld a, (hl)
04000C C9          0031      ret
04000D 54 68 69 73 0032      .asciz "This should be here"
       20 73 68 6F
       75 6C 64 20
       62 65 20 68
       65 72 65 00
040021             0033  variable2:
040021 00          0034  localvar2: .db 0     ; the same label can be used, because this entire block will be skipped
040022             0035  .endif
040022             0036
```
