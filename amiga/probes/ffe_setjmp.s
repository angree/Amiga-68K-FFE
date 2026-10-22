; SPDX-License-Identifier: MIT
;
; _SetJmp / _LongJmp -- the game's error-recovery escape, for real this time.
;
; WHY THIS FILE EXISTS (2026-08-02). The first-light build stubbed SetJmp to
; "return 0" and LongJmp to "log and exit(11)". That was wrong in a way that
; looked exactly like a hang: the renderer's small-node pool (FUNC_001820,
; 3000 x 16 bytes, counter DATA_009276) overflowed on the first landed frame,
; the game did what it was designed to do -- LongJmp(DATA_009271, 2) back to
; the SetJmp at FUNC_001876 (ffe.asm:129931), which calls FUNC_001817 and
; ABANDONS JUST THAT FRAME -- and our stub killed the process instead. The
; frame counter stopping dead at present 257 was the exit, not a spin.
;
; ffe.asm's own _SetJmp saves the x86 register image, so it cannot be
; translated; this is the 68k equivalent with identical SEMANTICS:
;   SetJmp(buf)        -> 0 on the direct call, val on arrival from LongJmp
;   LongJmp(buf, val)  -> never returns; resumes at the SetJmp call site
; A val of 0 becomes 1, as C's longjmp requires -- the game only ever passes
; 1 or 2, but the rule is free to honour.
;
; The buffer is the GAME's (DATA_009125 / DATA_009126 / DATA_009271), each
; `resb 500` in ffedat.asm, so the 52 bytes used here fit with room to spare
; and no game data needs resizing. Layout, ours alone:
;   +0  resume PC        (the SetJmp caller's return address)
;   +4  resume SP        (what RTS would have left: the pushed cdecl arg)
;   +8  d2-d7/a2-a6      (44 bytes -- the m68k C ABI's callee-saved set)
;
; Called from TRANSLATED code through the C ABI (`call _SetJmp` in ffe.asm,
; and the C name LongJmp assembles to _LongJmp), so d2-d7/a2-a6 must survive
; a normal SetJmp return: they do, nothing here writes them except the
; restore path, which is the whole point.

	section	code,code

	xdef	_SetJmp
	xdef	_LongJmp
	xdef	_ffe_longjmp_n

_SetJmp:
	move.l	(4,sp),a0		; buf
	move.l	(sp),(a0)		; resume PC = our return address
	lea	(4,sp),a1		; SP as RTS will leave it (arg still on)
	move.l	a1,(4,a0)
	movem.l	d2-d7/a2-a6,(8,a0)
	moveq	#0,d0
	rts

_LongJmp:
	move.l	(4,sp),a0		; buf
	move.l	(8,sp),d0		; val
	bne.s	.nz
	moveq	#1,d0			; longjmp(buf,0) must arrive as 1
.nz:
	addq.l	#1,_ffe_longjmp_n	; how often the game took its escape
	; DURABLE EVIDENCE, first few calls only: a per-128-present counter is
	; useless if the machine dies in between. Logged BEFORE anything is
	; restored, so a fault in the restore itself still leaves the line.
	movem.l	d0/a0,-(sp)
	move.l	d0,-(sp)
	jsr	_FfeLongJmpLog
	addq.l	#4,sp
	movem.l	(sp)+,d0/a0
	movem.l	(8,a0),d2-d7/a2-a6
	move.l	(4,a0),sp		; abandon every frame below the SetJmp
	move.l	(a0),a1
	jmp	(a1)			; return INTO SetJmp's caller, d0 = val

	section	bss,bss

_ffe_longjmp_n:	ds.l	1
