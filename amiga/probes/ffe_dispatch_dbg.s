; FIRST-LIGHT DEBUG -- hand-written FUNC_000144 (the module-event dispatcher)
; that logs every dispatch through C before making it. Enabled by putting
; `!FUNC_000144` in build\xlat-full\force_stub.txt (massgen then emits
; neither translation nor stub) and letting build-amiga.sh assemble this.
; The body is the t-ABI mirror of the translator's own output for:
;     push ebp / mov ebp,esp
;     mov eax,[ebp+0x8] / mov eax,[eax*4+DATA_004892]
;     mov edx,[ebp+0xc] / mov eax,[eax+edx*4]
;     test eax,eax / jz skip
;     mov edx,[ebp+0x10] / push edx / call eax / pop ecx
;   skip: pop ebp / ret
; plus the jsr into C (legal from t-ABI: translated->C needs no thunk).
	section	code,code
	xref	_FfeDispatchLog
	xref	DATA_004892

	xdef	FUNC_000144
	xdef	_FUNC_000144
FUNC_000144:
_FUNC_000144:
	pea	(a5)
	movea.l	sp,a5

	move.l	(16,a5),-(sp)
	move.l	(12,a5),-(sp)
	move.l	(8,a5),-(sp)
	jsr	_FfeDispatchLog
	lea	(12,sp),sp

	move.l	(8,a5),d0
	lea	(DATA_004892).l,a0
	move.l	(0,a0,d0.l*4),d0
	move.l	(12,a5),d2
	movea.l	d0,a0
	move.l	(0,a0,d2.l*4),d0
	tst.l	d0
	beq	.skip
	move.l	(16,a5),-(sp)
	movea.l	d0,a0
	jsr	(a0)
	addq.l	#4,sp
.skip:
	movea.l	(sp)+,a5
	rts
