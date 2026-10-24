; SPDX-License-Identifier: MIT
; FIRST-LIGHT DEBUG: the crash catcher.
;
; Installed as the task's tc_TrapCode (ffe_game.c main). AmigaOS calls it in
; SUPERVISOR mode with the trap NUMBER as a longword on top of the supervisor
; stack, above the CPU's own exception frame. It records the number, SR, PC
; and format word into globals, discards the CPU frame WHATEVER its 68020
; format (sizes from the 68020 UM: fmt 0/1 = 8 bytes, 2/3 = 12, 9 = 20,
; A = 32, B = 92), builds a clean four-word format-0 frame pointing at the C
; reporter _FfeCrashDie, and RTEs into it in user mode. dos.library cannot be
; called from supervisor mode, so the file write happens in the reporter.
;
; Only registers and plain memory are touched here -- supervisor-safe.

	section	code,code

	xdef	_FfeTrapCode
	xref	_FfeCrashDie
	xdef	_ffe_crash_num
	xdef	_ffe_crash_sr
	xdef	_ffe_crash_pc
	xdef	_ffe_crash_fmt
	xdef	_ffe_crash_frame
	xdef	_ffe_crash_ustk

_FfeTrapCode:
	move.l	(sp)+,_ffe_crash_num	; trap number (pushed by exec)
	move.w	(sp),d0
	move.w	d0,_ffe_crash_sr	; SR at the fault
	move.l	(2,sp),_ffe_crash_pc	; PC at the fault
	move.w	(6,sp),d0		; format/vector word
	move.w	d0,_ffe_crash_fmt

	; raw copy of the first 48 bytes of the CPU frame: for a format $B
	; frame that includes the SSW (+$A), the instruction pipe stages and
	; the data-cycle fault address (+$10) -- decoded offline
	movem.l	d1/a0/a1,-(sp)
	lea	(12,sp),a0
	lea	_ffe_crash_frame,a1
	moveq	#11,d1
.cp:	move.l	(a0)+,(a1)+
	dbf	d1,.cp
	movem.l	(sp)+,d1/a0/a1

	; USER-STACK SNAPSHOT (2026-08-02). The wild jump that ends the render
	; lands in Kickstart ROM, and the frame copied above holds only CPU
	; state -- the RETURN-ADDRESS CHAIN that names the caller is on the
	; USER stack, because the fault happened in user mode. Copy 16 longs
	; from the USP so the reporter can print the ones inside the program's
	; own address range.
	movem.l	d1/a0/a1,-(sp)
	move	usp,a0
	lea	_ffe_crash_ustk,a1
	moveq	#15,d1
.cu:	move.l	(a0)+,(a1)+
	dbf	d1,.cu
	movem.l	(sp)+,d1/a0/a1

	; drop the whole CPU frame by format nibble
	lsr.w	#8,d0
	lsr.w	#4,d0
	and.w	#$f,d0
	cmp.w	#2,d0
	blo.s	.f8			; fmt 0/1 -> 8 bytes
	cmp.w	#3,d0
	bls.s	.f12			; fmt 2/3 -> 12
	cmp.w	#9,d0
	beq.s	.f20			; fmt 9 -> 20
	cmp.w	#10,d0
	beq.s	.f32			; fmt A -> 32
	lea	(92,sp),sp		; fmt B -> 92
	bra.s	.build
.f8:	addq.l	#8,sp
	bra.s	.build
.f12:	lea	(12,sp),sp
	bra.s	.build
.f20:	lea	(20,sp),sp
	bra.s	.build
.f32:	lea	(32,sp),sp

.build:
	; clean format-0 frame: SR (user mode, keep the saved IPL), PC, fmt/vec 0
	subq.l	#8,sp
	move.w	_ffe_crash_sr,d0
	and.w	#$0700,d0		; keep interrupt mask only; S/T cleared
	move.w	d0,(sp)
	move.l	#_FfeCrashDie,(2,sp)
	clr.w	(6,sp)
	rte

; ---------------------------------------------------------------------------
; FIRST-LIGHT DEBUG: the VBlank PC sampler, for profiling a silent spin.
;
; _FfeSampInstall (C-callable) reads VBR via exec Supervisor(), saves the
; level-3 autovector (offset $6C) and points it here. Each VBlank, if the
; interrupted context was USER mode and _ffe_samp_armed is set (FfeEnterLog
; arms it at the storm's known entry), the interrupted PC goes into a ring.
; After 3000 samples the frame's return PC is REWRITTEN to _FfeSampDump: the
; game's loop is abandoned and the C reporter writes the buffer to a file.
; The original handler is always chained to, frame intact.

	xdef	_FfeSampInstall
	xdef	_FfeSampRemove
	xdef	_ffe_samp_armed
	xdef	_ffe_samp_n
	xdef	_ffe_samp_buf
	xdef	_ffe_samp_limit
	xdef	_ffe_samp_prof
	xdef	_ffe_vbl_count
	xref	_FfeSampDump

_FfeSampVec:
	movem.l	d0/a0,-(sp)		; frame now at +8: SR.w@8, PC.l@10
	; 2026-10-24: only VERTB counts. Level 3 is also copper and blitter;
	; the profiler's clock (_ffe_vbl_count) must be 50 Hz of emulated time.
	move.w	$dff01e,d0		; INTREQR
	btst	#5,d0			; VERTB
	beq.s	.out
	addq.l	#1,_ffe_vbl_count
	move.w	(8,sp),d0
	btst	#13,d0			; interrupted in supervisor mode?
	bne.s	.out			; yes -> never sample or hijack
	tst.w	_ffe_samp_armed
	beq.s	.out
	move.l	_ffe_samp_n,d0
	cmp.l	_ffe_samp_limit,d0	; 2026-09-25: variable (was #3000)
	bge.s	.full
	lea	_ffe_samp_buf,a0
	lsl.l	#2,d0
	move.l	(10,sp),(0,a0,d0.l)	; record interrupted PC
	addq.l	#1,_ffe_samp_n
	bra.s	.out
.full:
	tst.w	_ffe_samp_prof		; profiling (2026-10-24): stop recording,
	bne.s	.out			; never divert the game
	tst.w	_ffe_samp_fired
	bne.s	.out
	move.w	#1,_ffe_samp_fired
	move.l	#_FfeSampDump,(10,sp)	; divert the interrupted task
.out:
	movem.l	(sp)+,d0/a0
	move.l	_ffe_samp_old,-(sp)	; chain to the saved handler
	rts

_FfeSampInstall:
	movem.l	a5/a6,-(sp)
	lea	.getvbr(pc),a5
	move.l	4.w,a6
	jsr	(-30,a6)		; Supervisor() -> d0 = VBR
	move.l	d0,a0
	lea	($6c,a0),a0		; level-3 autovector slot
	move.l	(a0),_ffe_samp_old
	move.l	#_FfeSampVec,(a0)
	movem.l	(sp)+,a5/a6
	rts
.getvbr:
	movec	vbr,d0
	rte

; _FfeSampRemove (C-callable, 2026-10-09): put the saved level-3 vector back.
; Without it the vector keeps pointing into this program after exit() has
; unloaded it -- the soft restart then jumped to a wild PC in supervisor mode.
; Safe to call when nothing was installed (_ffe_samp_old == 0).
_FfeSampRemove:
	movem.l	a5/a6,-(sp)
	tst.l	_ffe_samp_old
	beq.s	.none
	lea	.getvbr2(pc),a5
	move.l	4.w,a6
	jsr	(-30,a6)		; Supervisor() -> d0 = VBR
	move.l	d0,a0
	lea	($6c,a0),a0
	move.l	_ffe_samp_old,(a0)
	clr.l	_ffe_samp_old
.none:
	movem.l	(sp)+,a5/a6
	rts
.getvbr2:
	movec	vbr,d0
	rte

	section	bss,bss

_ffe_crash_num:	ds.l	1
_ffe_crash_pc:	ds.l	1
_ffe_crash_sr:	ds.w	1
_ffe_crash_fmt:	ds.w	1
_ffe_crash_frame:	ds.l	12
_ffe_crash_ustk:	ds.l	16
_ffe_samp_old:	ds.l	1
_ffe_samp_armed:	ds.w	1
_ffe_samp_fired:	ds.w	1
_ffe_samp_n:	ds.l	1
_ffe_samp_limit:	ds.l	1
_ffe_samp_prof:	ds.w	1
_ffe_vbl_count:	ds.l	1
_ffe_samp_buf:	ds.l	20000
