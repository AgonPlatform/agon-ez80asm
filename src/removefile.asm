.assume adl=1
.section .text
.global _removefile

; Removes filename from SD card
; Input: filename string
; Output: A: File error, or 0 if OK
_removefile:
	push	ix
	ld 		ix,0
	add 	ix, sp
	ld 		hl, (ix+6)	; Address of path (zero terminated)
	ld a,	05h			; MOS_DEL API
	rst.lil	08h			; Delete a file or folder from the SD card
	ld		sp,ix
	pop		ix
	ret
