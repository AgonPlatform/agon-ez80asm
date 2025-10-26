.assume adl=1
.section .text
.global _getfilesize


; Gets filesize from an open file
; requires MOS mos_getfil call
; Input: MOS filehandle
; Output: HL - 24bit filesize

_getfilesize:
	PUSH IX
	LD	 IX, 0h
	ADD	 IX, SP

    ; get pointer to FIL structure in MOS memory
	LD	A, (IX+6)
    LD  C, A
    LD  A,	19h		; MOS_GETFIL API call
    RST.LIL 08h

    LD  DE, 11    ; offset to lower 3bytes in FSIZE_t, part of the FFOBJD struct that HL points to
    ADD HL, DE
    LD  HL, (HL)     ; load actual FSIZE_t value (lower 3 bytes)

	LD	SP, IX
	POP	IX
	RET
