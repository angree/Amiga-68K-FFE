/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * FFE's game clock, and the one-millisecond sleep.
 *
 * The clock is VIRTUAL and quantised to 5 ms: TimerGetTimeStamp walks a
 * private stamp forward in whole 5 ms steps for as long as real time is ahead
 * of it. TimerFrameUpdate does that walk, pumps input, and then RESETS the
 * reference point to "now" -- deliberately discarding every millisecond that
 * passed between the walk and that resample: the pump itself, the sub-5 ms
 * remainder, and in the SDL layer an arbitrarily long pause while the window
 * sits inactive. Time the FRAME spent rendering elapsed before the call and is
 * walked in normally; time the PLATFORM LAYER spent is not, so a stall in it
 * can never be replayed into the game's physics as a jump.
 *
 * Copied structure for structure from ref/jjffe/sdl/sdltimer.cpp. It is not a
 * bug and must not be "fixed".
 *
 * WHERE REAL TIME COMES FROM: amigagfx_millis(), which is DateStamp-based and
 * therefore 20 ms granular. The stamp consequently advances four 5 ms steps at
 * a time rather than smoothly -- harmless here, since the game only ever
 * compares stamps against each other, but it is why TimerSleep does NOT poll
 * that clock (a 1 ms wait would round to 0 or 20 ms).
 *
 * This file DOES include Amiga headers -- it is one of the dedicated plain-C
 * files that is allowed to -- because a real 1 ms sleep needs timer.device.
 */

#include <exec/types.h>
#include <exec/io.h>
#include <exec/ports.h>
#include <devices/timer.h>
#include <proto/exec.h>
#include <proto/dos.h>

#include "ffeapi.h"
#include "amiga_gfx.h"

static ULONG g_stamp;		/* the game clock */
static ULONG g_last;		/* real-time reference the stamp was last synced to */

/* timer.device UNIT_MICROHZ, opened once for TimerSleep. Delay(1) is the
 * obvious alternative and is wrong by a factor of twenty: AmigaDOS ticks are
 * 1/50 s, so the smallest Delay is 20 ms. The game calls TimerSleep in spin
 * loops (waiting on sound and on the intro streams), where 20 ms per turn
 * would cost real frames. */
static struct MsgPort     *g_tport;
static struct timerequest *g_treq;
static int                 g_topen;

void ffe_timer_init (void)
{
	g_stamp = 0;
	g_last  = (ULONG)amigagfx_millis ();

	g_tport = CreateMsgPort ();
	if (g_tport != NULL)
		g_treq = (struct timerequest *)CreateIORequest (g_tport, sizeof (struct timerequest));
	if (g_treq != NULL &&
	    OpenDevice ((CONST_STRPTR)"timer.device", UNIT_MICROHZ,
	                (struct IORequest *)g_treq, 0) == 0)
		g_topen = 1;
	/* Failure is survivable: TimerSleep falls back to Delay(1). */
}

void ffe_timer_cleanup (void)
{
	if (g_topen) { CloseDevice ((struct IORequest *)g_treq); g_topen = 0; }
	if (g_treq  != NULL) { DeleteIORequest ((struct IORequest *)g_treq); g_treq = NULL; }
	if (g_tport != NULL) { DeleteMsgPort (g_tport); g_tport = NULL; }
}

/* FIXED-STEP MODE (2026-10-16, opt-in): if PROGDIR:ffe_fixedstep exists, the
 * clock advances by exactly 20 ms per frame (TimerFrameUpdate) instead of
 * following real time -- what the PC reference gets at a steady 50 fps. The
 * game then integrates with the same per-frame step as the PC and runs in
 * slow motion where the Amiga cannot keep 50 fps; for side-by-side tests only.
 * A busy-wait that calls TimerGetTimeStamp without frames (error dialogs,
 * spin loops) still advances 5 ms per 2000 calls so it cannot hang. */
static int g_fixed = -1;
static ULONG g_fixed_calls;
static int fixed_mode (void)
{
	if (g_fixed < 0) {
		BPTR l = Lock ((CONST_STRPTR)"PROGDIR:ffe_fixedstep", ACCESS_READ);
		g_fixed = l != 0;
		if (l) UnLock (l);
	}
	return g_fixed;
}

ULONG TimerGetTimeStamp (void)
{
	ULONG curTime;

	if (fixed_mode ()) {
		if (++g_fixed_calls >= 2000) { g_fixed_calls = 0; g_stamp += 5; }
		return g_stamp;
	}
	curTime = (ULONG)amigagfx_millis ();

	while (g_last + 5 <= curTime)
	{
		g_stamp += 5;
		g_last  += 5;
	}

	return g_stamp;
}

extern void ffe_dbg_trace (const char *tag);	/* FIRST-LIGHT DEBUG */

void TimerFrameUpdate (void)
{
	ffe_dbg_trace ("TimerFrameUpdate");
	if (fixed_mode ()) {
		g_stamp += 20; g_fixed_calls = 0;
		ffe_pump_input ();
		ffe_sound_service ();
		return;
	}
	TimerGetTimeStamp ();
	ffe_pump_input ();

	/* Re-arm any looping voice whose buffer has run out. Paula plays a buffer
	 * once (ioa_Cycles = 1), so a sample with a descriptor loopcount only keeps
	 * going if something services it regularly, and once per frame is where
	 * that belongs. Cheap and non-blocking when nothing needs re-arming. */
	ffe_sound_service ();

	/* The load-bearing line. See the header comment: everything that happened
	 * since the walk above -- the pump, and the remainder below 5 ms -- is
	 * dropped on purpose. The SDL layer additionally blocks between the two
	 * while its window is inactive, and that pause is what the line mainly
	 * exists to swallow; an Intuition backdrop screen has no equivalent state,
	 * so there is nothing here to block on. */
	g_last = (ULONG)amigagfx_millis ();
}

void TimerSleep (void)
{
	if (g_topen) {
		g_treq->tr_node.io_Command = TR_ADDREQUEST;
		g_treq->tr_time.tv_secs  = 0;
		g_treq->tr_time.tv_micro = 1000;
		DoIO ((struct IORequest *)g_treq);
		return;
	}
	Delay (1);				/* 20 ms, the best AmigaDOS can do -- see above */
}
