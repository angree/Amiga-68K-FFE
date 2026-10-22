/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * HANG WATCHDOG (2026-09-25). The intro freezes intermittently (presents
 * 353/432/449/993/1121 in different runs, no TRAP, no Guru, nothing in
 * ffe_crash.log), and the VBlank PC sampler -- which DOES dump in a normal
 * run (positive control) -- never fires during the freeze. So the game task
 * is not executing user code: it is either Wait()ing forever inside some
 * system call, or the whole system is dead.
 *
 * This separate DOS process (priority +1, so it also preempts a user-mode
 * spin) polls the present counter once a second. After 4 s without a present
 * it writes Work:ffe_watchdog.log ONCE: the game task's state, the signals it
 * waits for, and 96 longs from its saved stack pointer -- the return
 * addresses there name the call it is stuck in (resolve against
 * build\amiga-out\ffe_game.symmap with the MAP anchors the log carries).
 * No file at all after a freeze = the scheduler itself is dead (a wild write
 * into exec), which is a different hunt.
 *
 * Started from ffe_game.c main via ffe_watchdog_start(); harmless otherwise.
 *
 * SOFT RESET (2026-10-02, developer's request: "reset the machine, do not
 * restart WinUAE -- it keeps taking my mouse"). Once a second this process
 * also looks for Work:ffe_reboot; when the host creates that file it is
 * deleted and the machine does ColdReboot(), which boots straight back into
 * Work:run and therefore into whatever ffe_game the host deployed meanwhile.
 * Because it runs here, at priority +1 in its own process, it works while
 * the game renders, while it is frozen, AND after it exited: an atexit
 * handler parks the game task in a Delay loop instead of letting exit()
 * unload the segment this process's code lives in.
 */
#include <exec/types.h>
#include "ffe_dev.h"
#include <exec/tasks.h>
#include <dos/dos.h>
#include <dos/dostags.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>

extern unsigned long ffe_get_presents (void);
extern char ffe_wd_anchor_a[] __asm__("FUNC_000286");
extern char ffe_wd_anchor_b[] __asm__("FUNC_001682");
extern char ffe_wd_anchor_c[] __asm__("FUNC_000938");

static struct Task *g_game;
static volatile int g_exiting;

/* SOFT RESTART (2026-10-09). A hardware reset of the emulated A1200 costs
 * ~31 s at the developer's -80% CPU throttle (ROM init + drive detection), paid
 * twice per run. So PROGDIR:ffe_reboot now asks the GAME TO EXIT: VideoBlit
 * sees ffe_quit_req and calls exit(0), the atexit handlers close the screen
 * and audio, and S/Startup-Sequence loops straight back into Work:ffe_game --
 * the binary the host deployed meanwhile. Only when the game does not exit
 * within 3 s (frozen, not presenting) does the watchdog fall back to
 * ColdReboot(). An exit that was NOT requested (crash -> exit(13), or the
 * game quitting) leaves PROGDIR:ffe_hold behind, so the restarted instance
 * parks and the logs of the dead run stay untouched. */
volatile int ffe_quit_req;
static volatile int g_wd_done;
static volatile int g_parked;

/* PROGDIR:ffe_reboot present -> delete it, return 1. */
static int wd_flag (void)
{
	BPTR lk = Lock ((STRPTR)"PROGDIR:ffe_reboot", SHARED_LOCK);

	if (!lk) return 0;
	UnLock (lk);
	DeleteFile ((STRPTR)"PROGDIR:ffe_reboot");
	return 1;
}

static void wd_touch (const char *name)
{
	BPTR fh = Open ((STRPTR)name, MODE_NEWFILE);
	if (fh) { Write (fh, (APTR)"1", 1); Close (fh); }
}

static void wd_write (BPTR fh, const char *s)
{
	long n = 0;

	while (s[n]) n++;
	Write (fh, (APTR)s, n);
}

static void wd_entry (void)
{
	unsigned long last = 0;
	int still = 0;
	char line[160];

	{
		BPTR ah = Open ((STRPTR)"PROGDIR:ffe_watchdog_alive.log", MODE_NEWFILE);
		if (ah) { wd_write (ah, "watchdog alive\n"); Close (ah); }
	}
	for (;;) {
		unsigned long p;

		Delay (25);
		if (g_exiting) { g_wd_done = 1; return; }	/* the game is leaving: so do we */
		if (wd_flag ()) {
			int i;
			/* PARK, DO NOT EXIT (2026-10-09). The first soft-restart design
			 * called exit(0) from VideoBlit; it never reached the atexit
			 * handlers and twice left the machine at a wild PC. So a request
			 * while the game RUNS only parks it (ffe_park below: no more
			 * writes, ffe_holding tells the host the logs are safe), and a
			 * request while it is already PARKED -- or frozen -- is the
			 * hardware reset that loads the next binary. One reset per run
			 * instead of two. */
			if (g_parked) { Delay (10); ColdReboot (); }
			ffe_quit_req = 1;
			for (i = 0; i < 30 && !g_parked; i++) Delay (5);	/* 3 s to park */
			if (!g_parked) { Delay (10); ColdReboot (); }
			continue;
		}
		if (still < 0) continue;	/* hang already logged */
		p = ffe_get_presents ();
		if (p == last && p > 30) still++;
		else { still = 0; last = p; }
		if (still >= 8) {		/* 8 x 0.5 s without a present */
			BPTR fh = Open ((STRPTR)"PROGDIR:ffe_watchdog.log", MODE_NEWFILE);

			if (fh) {
				const unsigned long *sp;
				int i;

				Forbid ();
				snprintf (line, sizeof line,
				          "WATCHDOG presents=%lu state=%d sigwait=%08lx sigrecvd=%08lx"
				          " spreg=%08lx splower=%08lx spupper=%08lx\n",
				          p, (int)g_game->tc_State, (unsigned long)g_game->tc_SigWait,
				          (unsigned long)g_game->tc_SigRecvd,
				          (unsigned long)g_game->tc_SPReg,
				          (unsigned long)g_game->tc_SPLower,
				          (unsigned long)g_game->tc_SPUpper);
				Permit ();
				wd_write (fh, line);
				snprintf (line, sizeof line,
				          "MAP FUNC_000286=%lx FUNC_001682=%lx FUNC_000938=%lx\n",
				          (unsigned long)ffe_wd_anchor_a,
				          (unsigned long)ffe_wd_anchor_b,
				          (unsigned long)ffe_wd_anchor_c);
				wd_write (fh, line);
				sp = (const unsigned long *)g_game->tc_SPReg;
				for (i = 0; i < 96; i++) {
					if ((unsigned long)(sp + i) >= (unsigned long)g_game->tc_SPUpper)
						break;
					snprintf (line, sizeof line, "S %03d %08lx\n", i * 4, sp[i]);
					wd_write (fh, line);
				}
				Close (fh);
			}
			still = -1;	/* logged once; keep polling for the reboot flag */
		}
	}
}

/* HOLD (2026-10-02): called first thing in main. While Work:ffe_hold exists
 * the freshly booted game writes nothing and only services the reboot flag,
 * so the host can read and delete the previous run's logs safely (a host-side
 * read during a run poisons the Amiga's writes) and then start the next run by
 * deleting ffe_hold. winuae\harness\soft_run.ps1 drives it. */
void ffe_hold_wait (void)
{
	BPTR lk;

	if (!ffe_dev_mode ()) return;	/* release: no harness hold */
	lk = Lock ((STRPTR)"PROGDIR:ffe_hold", SHARED_LOCK);
	if (!lk) return;
	UnLock (lk);
	wd_touch ("PROGDIR:ffe_holding");	/* the host waits for this: "holding, logs are safe" */
	/* RELEASE BY CREATION, NOT BY DELETION (2026-10-02). The first version
	 * waited for the host to DELETE ffe_hold, and once the machine sat in the
	 * hold forever: WinUAE's directory filesystem kept answering the Lock of a
	 * file the host had already removed. A file the host CREATES is always
	 * seen (ffe_reboot proves it every run), so the host now creates ffe_go and
	 * the Amiga deletes both files itself. */
	for (;;) {
		lk = Lock ((STRPTR)"PROGDIR:ffe_go", SHARED_LOCK);
		if (lk) {
			UnLock (lk);
			DeleteFile ((STRPTR)"PROGDIR:ffe_go");
			DeleteFile ((STRPTR)"PROGDIR:ffe_hold");
			return;
		}
		/* a restart request while holding: this instance may be an OLD binary
		 * (the host deploys before it asks), so exit and let S/Startup-Sequence
		 * run the new one. Nothing is open yet and no atexit is registered. */
		if (wd_flag ()) { Delay (10); ColdReboot (); }	/* reload: the host may have deployed a newer binary */
		Delay (25);
	}
}

/* atexit (registered first, so it runs LAST): tell the watchdog process to
 * end and wait until it has, because its code lives in the segment exit() is
 * about to unload. An exit nobody requested parks the next instance. */
static void wd_exit (void)
{
	extern void ffe_boot_mark (const char *);
	int i;

	ffe_boot_mark ("wd_exit (last atexit)");
	if (!ffe_quit_req) wd_touch ("PROGDIR:ffe_hold");
	g_exiting = 1;
	for (i = 0; i < 100 && !g_wd_done; i++) Delay (5);
	Delay (10);
}

void ffe_watchdog_start (void)
{
	struct Process *pr;
	FILE *lf;

	/* release: no watchdog process at all (hang logs, ffe_reboot, ffe_hold
	 * are test-machine features -- an unrequested exit used to leave ffe_hold
	 * behind and the NEXT start of the game then waited forever) */
	if (!ffe_dev_mode ()) return;
	g_game = FindTask (NULL);
	pr = CreateNewProcTags (NP_Entry, (ULONG)wd_entry,
	                   NP_Name, (ULONG)"ffe_watchdog",
	                   NP_Priority, 1,
	                   NP_StackSize, 8192,
	                   /* PROGDIR: of the child = the game's directory, and no
	                    * "insert volume" requester ever (release builds run
	                    * without the harness's Work: volume) */
	                   NP_HomeDir, (ULONG)DupLock (GetProgramDir ()),
	                   NP_WindowPtr, (ULONG)-1,
	                   TAG_END);
	if (pr) atexit (wd_exit);
	lf = ffe_logopen ("PROGDIR:ffe_game.log", "a");
	if (lf) { fprintf (lf, "WATCHDOG CreateNewProc -> %lx\n", (unsigned long)pr); fclose (lf); }
}

/* the positive control's stall: a Wait on a signal nobody sends */
unsigned long ffe_wait_forever (void)
{
	return Wait (SIGBREAKF_CTRL_F);
}

/* BOOT MARKS (2026-10-09, developer: "measure the start-up before waiting on
 * it"). ffe_boot_mark(tag) appends "<ms since the first mark> <tag>" to
 * PROGDIR:ffe_boot.log. DateStamp-based (20 ms steps), usable before the
 * screen exists. A dozen marks per run, so the file I/O is not a cost. */
void ffe_boot_mark (const char *tag)
{
	static unsigned long t0;
	static int first = 1;
	struct DateStamp ds;
	unsigned long ms;
	BPTR fh;
	char line[96];

	if (!ffe_dev_mode ()) return;
	DateStamp (&ds);
	ms = (unsigned long)ds.ds_Minute * 60000UL + (unsigned long)ds.ds_Tick * 20UL;
	if (first) t0 = ms;
	fh = Open ((STRPTR)"PROGDIR:ffe_boot.log", first ? MODE_NEWFILE : MODE_READWRITE);
	first = 0;
	if (!fh) return;
	Seek (fh, 0, OFFSET_END);
	snprintf (line, sizeof line, "%lu %s%c", ms - t0, tag, 10);
	wd_write (fh, line);
	Close (fh);
}

/* Called from VideoBlit when ffe_quit_req is set: stop here for good. The
 * game task never returns from this, so nothing writes a log any more. */
void ffe_park (void)
{
	extern void amigagfx_set_bar_text (const char *t);

	/* say so on the screen bar: a parked game is a frozen picture and looked
	 * exactly like a hang to the developer (2026-10-09) */
	{
		extern long ffe_last_fps10;
		char t[48];

		if (ffe_last_fps10 >= 0)
			snprintf (t, sizeof t, "AmiFFE 0.1.0  %ld.%ld FPS  PARKED",
			          ffe_last_fps10 / 10, ffe_last_fps10 % 10);
		else
			snprintf (t, sizeof t, "AmiFFE 0.1.0  PARKED");
		amigagfx_set_bar_text (t);
	}
	g_parked = 1;
	wd_touch ("PROGDIR:ffe_holding");
	for (;;) Delay (50);
}
