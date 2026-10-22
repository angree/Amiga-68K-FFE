/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * Exercises the three parts of the platform layer that never touch the screen:
 * the .cfg reader (ffe_cfg.c), the directory layer (ffe_dir.c) and
 * BlitClipWrapper (ffe_misc.c). No SystemInit, no screen, no game code -- this
 * one runs to completion on its own and can be read entirely from its log.
 *
 * It builds its own fixtures rather than assuming any: a config file and a
 * small directory tree under PROGDIR:, both rewritten on every run. The config
 * is pointed at that tree through ffe_dir_init_from(), so the walk below is
 * over a directory whose exact contents are known and can be asserted.
 *
 * WHAT IT IS ACTUALLY TRYING TO CATCH, in order of how much a wrong answer
 * would cost later:
 *   - "Work:/save" instead of "Work:save". A path ending in ':' takes no
 *     separator, and every DirMake* result and every DirNavigateTree step below
 *     is checked for the substring ":/" for exactly that reason;
 *   - walking off the front of a volume: (Parent Dir) at "Work:" must be a
 *     no-op, not "Wo" or an empty path;
 *   - BlitClipWrapper called with a negative extent for a rectangle that clips
 *     away entirely -- the reference does that, we must not;
 *   - the config reader inventing a section or a key out of a comment line.
 *
 * Log goes to PROGDIR:ffe_dirtest.log with one fflush per line, same as
 * amiga/probes/ffe_apitest.c: the run script detaches us with `Run >NIL: <NIL:`
 * and nothing else survives that.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "ffeapi/ffeapi.h"

#define TMPDIR  "PROGDIR:ffe_dirtest.tmp"
#define SUBDIR  "PROGDIR:ffe_dirtest.tmp/sub"
#define CFGFILE "PROGDIR:ffe_dirtest.cfg"

#define ALPHA_SIZE 10
#define BETA_SIZE  20

static FILE *g_log;
static int   g_pass, g_fail;

static void logf_ (const char *fmt, ...)
{
	va_list ap;
	if (g_log == NULL) return;
	va_start (ap, fmt);
	vfprintf (g_log, fmt, ap);
	va_end (ap);
	fputc ('\n', g_log);
	fflush (g_log);			/* a Guru must not cost us the last line */
}

static void check (int ok, const char *fmt, ...)
{
	va_list ap;

	if (ok) g_pass++; else g_fail++;
	if (g_log == NULL) return;
	fputs (ok ? "ok   " : "FAIL ", g_log);
	va_start (ap, fmt);
	vfprintf (g_log, fmt, ap);
	va_end (ap);
	fputc ('\n', g_log);
	fflush (g_log);
}

/* The one rule this whole probe exists to police. */
static int no_colon_slash (const char *s)
{
	return strstr (s, ":/") == NULL;
}

static int ends_with (const char *s, char c)
{
	size_t n = strlen (s);
	return n > 0 && s[n - 1] == c;
}

/* ---- fixtures ----------------------------------------------------------- */

static int make_dir (const char *path)
{
	BPTR lock = CreateDir ((CONST_STRPTR)path);

	if (lock != 0) { UnLock (lock); return 1; }

	lock = Lock ((CONST_STRPTR)path, ACCESS_READ);	/* left over from a previous run? */
	if (lock != 0) { UnLock (lock); return 1; }
	return 0;
}

static int make_file (const char *path, int size)
{
	FILE *f = fopen (path, "wb");
	int i;

	if (f == NULL) return 0;
	for (i = 0; i < size; i++) fputc ('x', f);
	fclose (f);
	return 1;
}

/* The config the Cfg tests read AND the one ffe_dir_init_from() is pointed at.
 *
 * The two comment lines are deliberate traps: both carry an '=' and one carries
 * a bracketed word, which is precisely what ref/jjffe/ffecfg.c turns into a key
 * named "#" and could turn into a phantom section. sectioncount is asserted
 * below to prove neither happened. */
static int write_cfg (void)
{
	FILE *f = fopen (CFGFILE, "wb");

	if (f == NULL) return 0;
	fputs ("# written by ffe_dirtest -- rewritten on every run\n", f);
	fputs ("# a comment with an = sign and a [SECTIONLIKE] word in it\n", f);
	fputs ("\n", f);
	fputs ("[PROBE]\n", f);
	fputs ("num=42\n", f);
	fputs ("neg=-7\n", f);
	fputs ("hex=0x1f\n", f);
	fputs ("   spaced   =   hello world   \n", f);
	fputs ("empty=\n", f);
	fputs ("str=Frontier\n", f);
	fputs ("\n", f);
	fputs ("[DIR]\n", f);
	fputs ("AVIPath=" TMPDIR "\n", f);
	fputs ("CmmdrPath=" TMPDIR "\n", f);
	/* A volume root on purpose: this is the case that produces "SYS:beep.wav"
	 * and would produce "SYS:/beep.wav" if the separator rule were wrong. */
	fputs ("SamplePath=SYS:\n", f);
	fputs ("SongPath=MUSIC\n", f);
	fclose (f);
	return 1;
}

/* ---- the config reader -------------------------------------------------- */

static void test_cfg (void)
{
	CfgStruct cfg;
	char buf[64];
	int v, rv;

	logf_ ("---- config reader ----");

	/* Safe on a struct CfgOpen has failed on: that is the documented contract
	 * of CfgGetKeyValDef and the reason the game can call the four Cfg
	 * functions blind in FUNC_001416_TimerInit. */
	rv = CfgOpen (&cfg, (char *)"PROGDIR:no_such_file.cfg");
	check (rv == 0, "CfgOpen on a missing file returns 0 (got %d)", rv);
	check (CfgFindSection (&cfg, (char *)"DIR") == 0, "CfgFindSection after a failed open returns 0");
	v = 123;
	rv = CfgGetKeyValDef (&cfg, (char *)"maxfps", &v, 50);
	check (rv == 0 && v == 50, "CfgGetKeyValDef after a failed open: rv=%d value=%d (want 0/50)", rv, v);
	check (CfgClose (&cfg) == 0, "CfgClose on a never-opened struct returns 0");

	rv = CfgOpen (&cfg, (char *)CFGFILE);
	check (rv == 1, "CfgOpen (\"%s\") = %d", CFGFILE, rv);
	if (rv == 0) return;

	check (cfg.sectioncount == 2,
	       "sectioncount = %d (want 2: a comment must not invent a section)",
	       cfg.sectioncount);
	check (cfg.currentsection == NULL, "currentsection is NULL right after open");

	/* Before any successful FindSection there is no current section, so a key
	 * read has to fail rather than dig through section 0. */
	v = -1;
	check (CfgGetKeyVal (&cfg, (char *)"num", &v) == 0, "CfgGetKeyVal with no section selected returns 0");
	check (CfgFindSection (&cfg, (char *)"NOSUCH") == 0, "CfgFindSection on a missing section returns 0");

	check (CfgFindSection (&cfg, (char *)"probe") == 1, "CfgFindSection is case insensitive (\"probe\" finds [PROBE])");

	v = -1; rv = CfgGetKeyVal (&cfg, (char *)"num", &v);
	check (rv == 1 && v == 42, "CfgGetKeyVal num: rv=%d value=%d (want 1/42)", rv, v);
	v = -1; rv = CfgGetKeyVal (&cfg, (char *)"NUM", &v);
	check (rv == 1 && v == 42, "CfgGetKeyVal is case insensitive on the key: rv=%d value=%d", rv, v);
	v = 0;  rv = CfgGetKeyVal (&cfg, (char *)"neg", &v);
	check (rv == 1 && v == -7, "CfgGetKeyVal neg: rv=%d value=%d (want 1/-7)", rv, v);
	v = 0;  rv = CfgGetKeyVal (&cfg, (char *)"hex", &v);
	check (rv == 1 && v == 31, "CfgGetKeyVal hex 0x1f: rv=%d value=%d (want 1/31, strtol base 0)", rv, v);
	v = 777; rv = CfgGetKeyVal (&cfg, (char *)"missing", &v);
	check (rv == 0 && v == 777, "CfgGetKeyVal on a missing key returns 0 and leaves the value alone (rv=%d v=%d)", rv, v);
	v = 0; rv = CfgGetKeyVal (&cfg, (char *)"#", &v);
	check (rv == 0, "no key called \"#\" was invented from the comment lines (rv=%d)", rv);

	buf[0] = 0; rv = CfgGetKeyStr (&cfg, (char *)"str", buf, (int)sizeof (buf));
	check (rv == 1 && strcmp (buf, "Frontier") == 0, "CfgGetKeyStr str: rv=%d value=\"%s\"", rv, buf);

	/* Leading and trailing blanks come off both the key and the value; a blank
	 * INSIDE the value stays, because an Amiga path may contain one. */
	buf[0] = 0; rv = CfgGetKeyStr (&cfg, (char *)"spaced", buf, (int)sizeof (buf));
	check (rv == 1 && strcmp (buf, "hello world") == 0,
	       "CfgGetKeyStr spaced: rv=%d value=\"%s\" (want \"hello world\", inner space kept)", rv, buf);

	buf[0] = 'z'; rv = CfgGetKeyStr (&cfg, (char *)"empty", buf, (int)sizeof (buf));
	check (rv == 1 && buf[0] == 0, "CfgGetKeyStr on \"empty=\": rv=%d value=\"%s\" (want 1 and \"\")", rv, buf);

	memset (buf, 'z', sizeof (buf));
	rv = CfgGetKeyStr (&cfg, (char *)"str", buf, 4);
	check (rv == 1 && strcmp (buf, "Fro") == 0 && buf[3] == 0,
	       "CfgGetKeyStr truncates and terminates at buflen 4: \"%s\"", buf);

	buf[0] = 0; rv = CfgGetKeyStr (&cfg, (char *)"missing", buf, (int)sizeof (buf));
	check (rv == 0, "CfgGetKeyStr on a missing key returns 0 (rv=%d)", rv);

	v = 0; rv = CfgGetKeyValDef (&cfg, (char *)"num", &v, 99);
	check (rv == 1 && v == 42, "CfgGetKeyValDef found: rv=%d value=%d (want 1/42)", rv, v);
	v = 0; rv = CfgGetKeyValDef (&cfg, (char *)"missing", &v, 99);
	check (rv == 0 && v == 99, "CfgGetKeyValDef defaulted: rv=%d value=%d (want 0/99)", rv, v);

	check (CfgFindSection (&cfg, (char *)"DIR") == 1, "CfgFindSection [DIR]");
	buf[0] = 0; rv = CfgGetKeyStr (&cfg, (char *)"SamplePath", buf, (int)sizeof (buf));
	check (rv == 1 && strcmp (buf, "SYS:") == 0, "[DIR] SamplePath = \"%s\" (want SYS:)", buf);

	check (CfgClose (&cfg) == 1, "CfgClose returns 1");
	check (cfg.filename == NULL && cfg.sections == NULL && cfg.sectioncount == 0,
	       "CfgClose zeroes the struct");
	check (CfgClose (&cfg) == 0, "a second CfgClose returns 0 rather than freeing twice");

	v = 5; rv = CfgGetKeyValDef (&cfg, (char *)"num", &v, 88);
	check (rv == 0 && v == 88, "CfgGetKeyValDef after close: rv=%d value=%d (want 0/88)", rv, v);
}

/* ---- the directory layer ------------------------------------------------ */

static const char *type_name (int t)
{
	switch (t) {
	case 0: return "file";
	case 2: return "dir ";
	case 4: return "up  ";
	case 6: return "vol ";
	default: return "????";
	}
}

/* 6 -> 4 -> 2 -> 0 is the order the game expects; this makes it comparable. */
static int type_rank (int t)
{
	switch (t) {
	case 6: return 0;
	case 4: return 1;
	case 2: return 2;
	case 0: return 3;
	default: return 9;
	}
}

static void test_dir (void)
{
	FileInfo fi, sub, up, vol;
	char buf[256];
	char base[256], after[256], before[256];
	int nvol = 0, nup = 0, ndir = 0, nfile = 0, n = 0;
	int rank = -1, order_ok = 1, colon_ok = 1;
	int got_sub = 0, got_alpha = 0, got_beta = 0, got_vol = 0;
	const char *p;
	int steps;

	logf_ ("---- directory layer ----");

	ffe_dir_init_from (CFGFILE);

	strcpy (base, DirGetCmmdrPath ());
	logf_ ("CmmdrPath after init = \"%s\"", base);
	check (no_colon_slash (base), "CmmdrPath has no \":/\" in it: \"%s\"", base);
	check (ends_with (base, '/') || ends_with (base, ':'),
	       "CmmdrPath ends in a separator: \"%s\"", base);

	/* ---- name builders. The interesting pair is Sample (SamplePath is a
	 * volume root, so the filename must butt straight up against the ':') and
	 * AVI (a real directory, so there must be a '/'). */
	DirMakeSampleName (buf, (char *)"beep.wav");
	logf_ ("DirMakeSampleName (\"beep.wav\") = \"%s\"", buf);
	p = strstr (buf, "beep.wav");
	check (p != NULL && p != buf && p[-1] == ':' && no_colon_slash (buf),
	       "volume path + filename takes NO separator: \"%s\"", buf);

	DirMakeAVIName (buf, (char *)"intro");
	logf_ ("DirMakeAVIName (\"intro\") = \"%s\"", buf);
	p = strstr (buf, "intro.avi");
	check (p != NULL && p != buf && p[-1] == '/' && no_colon_slash (buf),
	       "directory path + filename takes a '/': \"%s\"", buf);

	DirMakeSongName (buf, (char *)"title.mod");
	logf_ ("DirMakeSongName (\"title.mod\") = \"%s\"", buf);
	check (no_colon_slash (buf), "song name has no \":/\": \"%s\"", buf);

	DirMakeCmmdrName (buf, (char *)"jameson.cmd");
	logf_ ("DirMakeCmmdrName (\"jameson.cmd\") = \"%s\"", buf);
	check (no_colon_slash (buf) && strstr (buf, "jameson.cmd") != NULL,
	       "commander name built: \"%s\"", buf);

	/* ---- the walk. */
	logf_ ("DirFindFirst / DirFindNext over \"%s\":", base);
	memset (&sub, 0, sizeof (sub));
	memset (&vol, 0, sizeof (vol));

	if (DirFindFirst (&fi) == 0) {
		check (0, "DirFindFirst returned 0 -- nothing to enumerate at all");
		return;
	}

	do {
		int r = type_rank (fi.type);

		logf_ ("  [%2d] type %d %s size %7d  \"%s\"", n, fi.type,
		       type_name (fi.type), fi.size, fi.pName);

		if (r < rank) order_ok = 0;
		if (r > rank) rank = r;

		if (fi.type == 6) {
			nvol++;
			if (!got_vol) { vol = fi; got_vol = 1; }
			if (!ends_with (fi.pName, ':')) colon_ok = 0;
		}
		else if (fi.type == 4) {
			nup++;
			if (strcmp (fi.pName, "(Parent Dir)/") != 0) colon_ok = 0;
		}
		else if (fi.type == 2) {
			ndir++;
			if (!ends_with (fi.pName, '/')) colon_ok = 0;
			if (strcmp (fi.pName, "sub/") == 0) { sub = fi; got_sub = 1; }
		}
		else {
			nfile++;
			if (strcmp (fi.pName, "alpha.dat") == 0)
				got_alpha = (fi.size == ALPHA_SIZE) ? 1 : -1;
			if (strcmp (fi.pName, "beta.dat") == 0)
				got_beta = (fi.size == BETA_SIZE) ? 1 : -1;
		}

		n++;
	} while (n < 200 && DirFindNext (&fi) == 1);

	logf_ ("  %d entries: %d volume(s), %d parent, %d dir(s), %d file(s)",
	       n, nvol, nup, ndir, nfile);

	check (nvol >= 1, "at least one volume enumerated (%d)", nvol);
	check (order_ok, "entries come back in the order volumes, parent, dirs, files");
	check (colon_ok, "every name is well formed: volumes end ':', dirs end '/', parent is \"(Parent Dir)/\"");
	check (nup == 1, "exactly one parent entry (%d)", nup);
	check (got_sub, "the \"sub/\" subdirectory was listed with type 2");
	check (got_alpha == 1, "alpha.dat listed with size %d", ALPHA_SIZE);
	check (got_beta == 1, "beta.dat listed with size %d", BETA_SIZE);
	check (nfile == 2, "exactly the two files we created were listed (%d)", nfile);

	/* ---- navigate down and back up. */
	logf_ ("DirNavigateTree:");
	logf_ ("  start          \"%s\"", DirGetCmmdrPath ());

	if (got_sub) {
		DirNavigateTree (&sub);
		strcpy (after, DirGetCmmdrPath ());
		logf_ ("  into \"sub/\"   \"%s\"", after);
		check (strncmp (after, base, strlen (base)) == 0 &&
		       strcmp (after + strlen (base), "sub/") == 0,
		       "descending appends \"sub/\" and nothing else: \"%s\"", after);
		check (no_colon_slash (after), "no \":/\" after descending: \"%s\"", after);
	}

	strcpy (up.pName, "(Parent Dir)/");
	up.type = 4;
	up.size = 0;

	DirNavigateTree (&up);
	strcpy (after, DirGetCmmdrPath ());
	logf_ ("  up one level   \"%s\"", after);
	check (strcmp (after, base) == 0, "going back up lands exactly where we started: \"%s\"", after);

	/* All the way to the volume root, which is where the classic bug lives:
	 * one step too many turns "Work:" into "Wor" or into nothing. */
	steps = 0;
	while (steps < 16 && !ends_with (DirGetCmmdrPath (), ':')) {
		DirNavigateTree (&up);
		logf_ ("  up             \"%s\"", DirGetCmmdrPath ());
		if (!no_colon_slash (DirGetCmmdrPath ())) colon_ok = 0;
		steps++;
	}
	check (ends_with (DirGetCmmdrPath (), ':'),
	       "walking up %d time(s) reaches a volume root: \"%s\"", steps, DirGetCmmdrPath ());

	strcpy (before, DirGetCmmdrPath ());
	DirNavigateTree (&up);
	check (strcmp (before, DirGetCmmdrPath ()) == 0,
	       "(Parent Dir) at a volume root is a no-op: \"%s\" -> \"%s\"", before, DirGetCmmdrPath ());

	/* Switching volume: the stored path ends in ':' and a name appended to it
	 * must not grow a separator. */
	if (got_vol) {
		DirNavigateTree (&vol);
		strcpy (after, DirGetCmmdrPath ());
		logf_ ("  volume \"%s\"  -> \"%s\"", vol.pName, after);
		check (strcmp (after, vol.pName) == 0, "type 6 replaces the path with the volume name");
		DirMakeCmmdrName (buf, (char *)"save.cmd");
		logf_ ("  DirMakeCmmdrName at a volume root = \"%s\"", buf);
		check (no_colon_slash (buf), "no separator after a volume root: \"%s\"", buf);
	}

	DirResetCmmdrPath ();
	check (strcmp (DirGetCmmdrPath (), base) == 0,
	       "DirResetCmmdrPath restores the startup path: \"%s\"", DirGetCmmdrPath ());

	ffe_dir_cleanup ();
}

/* ---- BlitClipWrapper ---------------------------------------------------- */

static unsigned char g_src[4096];

static int   g_calls;
static long  g_off;
static int   g_bx, g_by, g_bw, g_bh, g_bj;

static void blit_stub (void *pData, int x, int y, int w, int h, int jump)
{
	g_calls++;
	g_off = (long)((unsigned char *)pData - g_src);
	g_bx = x; g_by = y; g_bw = w; g_bh = h; g_bj = jump;
}

typedef struct {
	const char *name;
	int x, y, w, h, jump;		/* what the game asks for */
	int call;					/* 1 if BlitFunc must be called */
	int ex, ey, ew, eh, ej;		/* what BlitFunc must receive */
	long eoff;					/* and from how far into the source */
} BlitCase;

static void test_blit (void)
{
	static const BlitCase cases[] = {
		{ "whole screen, exact",   0,   0, 320, 200, 0, 1,   0,   0, 320, 200,  0,   0 },
		{ "corner, exact fit",   300, 190,  20,  10, 0, 1, 300, 190,  20,  10,  0,   0 },
		{ "flush against right", 300,   0,  20,   1, 0, 1, 300,   0,  20,   1,  0,   0 },
		{ "clipped left",         -5,  10,  20,  10, 4, 1,   0,  10,  15,  10,  9,   5 },
		{ "clipped top",          10,  -3,  20,  10, 4, 1,  10,   0,  20,   7,  4,  72 },
		{ "clipped right",       310,  10,  20,  10, 4, 1, 310,  10,  10,  10, 14,   0 },
		{ "clipped bottom",       10, 195,  20,  10, 4, 1,  10, 195,  20,   5,  4,   0 },
		{ "clipped top+left",     -5,  -5,  20,  20, 0, 1,   0,   0,  15,  15,  5, 105 },
		{ "off screen left",    -100,  10,  20,  10, 0, 0,   0,   0,   0,   0,  0,   0 },
		{ "off screen right",    400,  10,  20,  10, 0, 0,   0,   0,   0,   0,  0,   0 },
		{ "off screen top",       10, -50,  20,  10, 0, 0,   0,   0,   0,   0,  0,   0 },
		{ "off screen bottom",    10, 300,  20,  10, 0, 0,   0,   0,   0,   0,  0,   0 },
		{ "zero width",           10,  10,   0,  10, 0, 0,   0,   0,   0,   0,  0,   0 },
		{ "zero height",          10,  10,  20,   0, 0, 0,   0,   0,   0,   0,  0,   0 }
	};
	int i;

	logf_ ("---- BlitClipWrapper ----");

	for (i = 0; i < (int)(sizeof (cases) / sizeof (cases[0])); i++)
	{
		const BlitCase *c = &cases[i];
		int ok;

		g_calls = 0;
		g_off = -1; g_bx = g_by = g_bw = g_bh = g_bj = -1;

		BlitClipWrapper (g_src, c->x, c->y, c->w, c->h, c->jump, blit_stub);

		if (c->call == 0) {
			ok = (g_calls == 0);
			check (ok, "%-20s in (%4d,%4d %3dx%3d jump %d) -> no call (calls=%d)",
			       c->name, c->x, c->y, c->w, c->h, c->jump, g_calls);
		}
		else {
			ok = (g_calls == 1 && g_bx == c->ex && g_by == c->ey &&
			      g_bw == c->ew && g_bh == c->eh && g_bj == c->ej &&
			      g_off == c->eoff);
			check (ok, "%-20s in (%4d,%4d %3dx%3d jump %d) -> out (%d,%d %dx%d jump %d off %ld)%s",
			       c->name, c->x, c->y, c->w, c->h, c->jump,
			       g_bx, g_by, g_bw, g_bh, g_bj, g_off,
			       ok ? "" : " -- MISMATCH");
			if (!ok)
				logf_ ("     expected (%d,%d %dx%d jump %d off %ld) calls=%d",
				       c->ex, c->ey, c->ew, c->eh, c->ej, c->eoff, g_calls);
		}
	}
}

/* ---- main --------------------------------------------------------------- */

int main (void)
{
	g_log = fopen ("PROGDIR:ffe_dirtest.log", "w");
	logf_ ("FFE platform layer probe -- config file, directories, blit clipping");
	logf_ ("no screen is opened and no game code is present");

	logf_ ("---- fixtures ----");
	check (make_dir (TMPDIR),  "test directory %s", TMPDIR);
	check (make_dir (SUBDIR),  "test subdirectory %s", SUBDIR);
	check (make_file (TMPDIR "/alpha.dat", ALPHA_SIZE), "alpha.dat (%d bytes)", ALPHA_SIZE);
	check (make_file (TMPDIR "/beta.dat",  BETA_SIZE),  "beta.dat (%d bytes)", BETA_SIZE);
	check (write_cfg (), "config file %s", CFGFILE);

	test_cfg ();
	test_dir ();
	test_blit ();

	logf_ ("---- summary ----");
	logf_ ("%d passed, %d failed", g_pass, g_fail);

	if (g_log != NULL) fclose (g_log);
	return g_fail == 0 ? 0 : 20;
}
