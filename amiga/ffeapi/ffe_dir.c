/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * The Dir* half of ffeapi.h: four configurable directories, the name builders
 * the game uses to reach mission data / samples / music / saved commanders, and
 * the three-call iterator that drives the save-file browser.
 *
 * Modelled on ref/jjffe/win32/w32dir.cpp -- same names, same return
 * conventions, same type numbers (6 volume, 4 parent, 2 directory, 0 file) --
 * but the path handling is rewritten, because the reference is DOS to the bone:
 * it hard-codes '\\' and it enumerates "drives" by chdir'ing A: through Z:.
 *
 * THE AMIGA PATH RULE, and it is the one thing in this file worth getting
 * right: a path ending in ':' is a volume or a device and takes NO separator
 * before the next component. "Work:save", never "Work:/save". Everything else
 * gets a '/'. That rule lives in exactly one function, dir_add_sep(), and every
 * path this file produces goes through it, so the four stored paths always end
 * in ':' or '/' and the name builders stay a bare strcat -- the same shape as
 * the reference's.
 *
 * The other half of the rule is the reverse: a path we hand to Lock() must NOT
 * keep that trailing '/'. On AmigaDOS an empty path component means "the parent
 * of", so Lock("Work:save/") is at best filesystem-dependent. dir_lock_dir()
 * strips it.
 *
 * Volumes replace drive letters, and they come from the DosList rather than
 * from probing: LockDosList / NextDosEntry / UnLockDosList with
 * LDF_VOLUMES|LDF_READ. Re-read on every DirFindFirst, because on this machine
 * a volume can appear or vanish between two visits to the save screen.
 *
 * This file DOES include Amiga headers -- it is one of the dedicated plain-C
 * files that is allowed to.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <string.h>

#include "ffeapi.h"

#define DIR_PATHMAX 256			/* the reference's buffer size, kept */
#define DIR_MAXVOLS 32			/* the reference had 26, one per drive letter */

static char pDirAVIPath[DIR_PATHMAX];
static char pDirCmmdrPathSys[DIR_PATHMAX];	/* the startup value, for DirResetCmmdrPath */
static char pDirCmmdrPath[DIR_PATHMAX];		/* where the browser currently is */
static char pDirSamplePath[DIR_PATHMAX];
static char pDirSongPath[DIR_PATHMAX];

static FileInfo g_vols[DIR_MAXVOLS];
static int      g_numvols;

/* Iteration state. The two directory phases each need their own pass over the
 * directory -- the game wants every subdirectory before every file -- so the
 * scan is opened, run out and closed twice, exactly as the reference re-runs
 * _findfirst with dirDirsFlag cleared. */
enum { PH_VOLS = 0, PH_PARENT, PH_DIRS, PH_FILES, PH_DONE };

static int                    g_phase = PH_DONE;
static int                    g_volidx;
static int                    g_scanopen;
static BPTR                   g_lock;
static struct FileInfoBlock  *g_fib;

/* ---- paths -------------------------------------------------------------- */

/* Terminate a path so a bare filename can be strcat'ed onto it. ':' already
 * separates, '/' already separates, everything else needs a '/'. An empty path
 * stays empty: "" + "file" is a valid name relative to the current directory. */
static void dir_add_sep (char *path)
{
	size_t n = strlen (path);

	if (n == 0) return;
	if (path[n - 1] == ':' || path[n - 1] == '/') return;
	if (n + 2 > DIR_PATHMAX) return;
	path[n]     = '/';
	path[n + 1] = 0;
}

/* Lock a directory held in our separator-terminated form. The trailing '/' has
 * to come off first (see the header comment); a path ending in ':' is a volume
 * root and goes through unchanged. Returns 0 without leaving anything locked if
 * the directory is not there. */
static BPTR dir_lock_dir (const char *path)
{
	char buf[DIR_PATHMAX];
	size_t n = strlen (path);

	if (n == 0 || n >= DIR_PATHMAX) return 0;
	memcpy (buf, path, n + 1);
	if (buf[n - 1] == '/') buf[n - 1] = 0;
	if (buf[0] == 0) return 0;

	return Lock ((CONST_STRPTR)buf, ACCESS_READ);
}

/* The directory the executable was loaded from, absolute, no trailing
 * separator. GetProgramDir's lock belongs to the process -- it must NOT be
 * unlocked. Returns 0 if dos.library will not tell us. */
static int dir_prog_dir (char *out)
{
	BPTR lock = GetProgramDir ();

	out[0] = 0;
	if (lock != 0 && NameFromLock (lock, (STRPTR)out, DIR_PATHMAX - 2) != DOSFALSE)
		return 1;
	out[0] = 0;
	return 0;
}

/* Turn whatever the config said into a path a filename can be appended to.
 *
 * Lock + NameFromLock canonicalises it to absolute "Volume:dir/dir" form, and
 * that is what makes the walk in DirNavigateTree well defined -- a relative
 * path has no last component to strip once it is down to one. The Win32 layer
 * does the same job with _getcwd plus a drive letter test.
 *
 * A relative name that does not resolve against the current directory is
 * retried against PROGDIR:, because an Amiga game's data sits next to its
 * executable and a detached process's current directory is rarely the one the
 * user has in mind. If it still does not resolve -- no music installed, say --
 * the configured name is kept as written and merely gets its separator, so the
 * game can still build names under it. */
static void dir_fix_path (const char *in, char *out)
{
	char buf[DIR_PATHMAX];
	BPTR lock;

	out[0] = 0;

	lock = Lock ((CONST_STRPTR)in, ACCESS_READ);
	if (lock == 0 && strchr (in, ':') == NULL) {
		if (dir_prog_dir (buf)) {
			dir_add_sep (buf);
			if (strlen (buf) + strlen (in) < DIR_PATHMAX) {
				strcat (buf, in);
				lock = Lock ((CONST_STRPTR)buf, ACCESS_READ);
			}
		}
	}

	if (lock != 0) {
		if (NameFromLock (lock, (STRPTR)out, DIR_PATHMAX - 2) == DOSFALSE) out[0] = 0;
		UnLock (lock);
	}

	if (out[0] == 0) {
		strncpy (out, in, DIR_PATHMAX - 2);
		out[DIR_PATHMAX - 2] = 0;
	}

	dir_add_sep (out);
}

/* One [DIR] key, with the reference's fallback to a hard-coded default.
 *
 * The reference strtok()s the value at the first space, tab or newline. We do
 * not: CfgGetKeyStr already hands back a trimmed value, and an Amiga path may
 * legally contain a space ("Work:My Saves"), so cutting at one would break a
 * config that is perfectly well formed here. */
static void dir_cfg_path (CfgStruct *cfg, char *key, const char *def, char *out)
{
	char buf[DIR_PATHMAX];

	if (CfgGetKeyStr (cfg, key, buf, DIR_PATHMAX - 1) == 0 || buf[0] == 0) {
		strncpy (buf, def, DIR_PATHMAX - 1);
		buf[DIR_PATHMAX - 1] = 0;
	}
	dir_fix_path (buf, out);
}

/* A volume root has nowhere above it, and neither has an empty (current
 * directory) path -- so neither gets a "(Parent Dir)/" entry offered. */
static int dir_has_parent (const char *path)
{
	size_t n = strlen (path);

	if (n == 0) return 0;
	return path[n - 1] != ':';
}

/* ---- volumes ------------------------------------------------------------ */

static void dir_add_vol (const char *name)
{
	FileInfo *fi;
	size_t n;

	if (g_numvols >= DIR_MAXVOLS) return;

	fi = &g_vols[g_numvols];
	n  = strlen (name);
	/* pName is 16 bytes and that is the game's own declaration, so a volume
	 * whose name runs past 15 characters including the colon gets truncated and
	 * will not lock. Nothing can be done about it from this side. */
	if (n > sizeof (fi->pName) - 1) n = sizeof (fi->pName) - 1;
	memcpy (fi->pName, name, n);
	fi->pName[n] = 0;
	fi->type = 6;
	fi->size = 0;
	g_numvols++;
}

/* Walk the DosList for mounted volumes. dol_Name is a BSTR -- length byte
 * first -- and we present each one in "Volume:" form, which is what
 * DirNavigateTree stores straight into the commander path. */
static void dir_scan_volumes (void)
{
	struct DosList *dl;
	char name[36];

	g_numvols = 0;

	dl = LockDosList (LDF_VOLUMES | LDF_READ);
	if (dl != NULL) {
		while ((dl = NextDosEntry (dl, LDF_VOLUMES | LDF_READ)) != NULL) {
			const unsigned char *bs = (const unsigned char *)BADDR (dl->dol_Name);
			int len;

			if (bs == NULL) continue;
			len = (int)bs[0];
			if (len <= 0) continue;
			if (len > (int)sizeof (name) - 2) len = (int)sizeof (name) - 2;
			memcpy (name, bs + 1, (size_t)len);
			name[len]     = ':';
			name[len + 1] = 0;
			dir_add_vol (name);
		}
		UnLockDosList (LDF_VOLUMES | LDF_READ);	/* the one and only exit path */
	}

	/* Nothing enumerated (no dos.library volumes at all, which really should not
	 * happen, or the list was empty): offer the volume the commander path lives
	 * on, so the browser shows something the player can navigate from. */
	if (g_numvols == 0) {
		char vbuf[DIR_PATHMAX];
		char *colon;

		strncpy (vbuf, pDirCmmdrPathSys, DIR_PATHMAX - 1);
		vbuf[DIR_PATHMAX - 1] = 0;
		colon = strchr (vbuf, ':');
		if (colon != NULL) colon[1] = 0;
		else strcpy (vbuf, "PROGDIR:");
		dir_add_vol (vbuf);
	}
}

/* ---- directory scan ----------------------------------------------------- */

/* Idempotent, and called from every path that abandons a scan -- including
 * DirFindFirst, because the game drops the iteration the moment the player
 * picks a file and would otherwise leak one lock per visit to the browser. */
static void dir_scan_close (void)
{
	if (g_fib != NULL) { FreeDosObject (DOS_FIB, g_fib); g_fib = NULL; }
	if (g_lock != 0)   { UnLock (g_lock); g_lock = 0; }
	g_scanopen = 0;
}

/* Open the commander directory and put the FileInfoBlock on its header, ready
 * for ExNext. Returns 0 with nothing locked if it cannot be read. */
static int dir_scan_open (void)
{
	dir_scan_close ();

	g_lock = dir_lock_dir (pDirCmmdrPath);
	if (g_lock == 0) return 0;

	/* AllocDosObject, not a stack struct: a FileInfoBlock must be longword
	 * aligned and the compiler owes us no such promise for an automatic. */
	g_fib = (struct FileInfoBlock *)AllocDosObject (DOS_FIB, NULL);
	if (g_fib == NULL) { UnLock (g_lock); g_lock = 0; return 0; }

	if (Examine (g_lock, g_fib) == DOSFALSE) { dir_scan_close (); return 0; }

	g_scanopen = 1;
	return 1;
}

static void dir_fill (FileInfo *pFile, const char *name, int isdir, long size)
{
	size_t max = sizeof (pFile->pName) - 1;		/* 15 */
	size_t n;

	if (isdir) max--;							/* room for the trailing '/' */
	n = strlen (name);
	if (n > max) n = max;
	memcpy (pFile->pName, name, n);
	if (isdir) pFile->pName[n++] = '/';
	pFile->pName[n] = 0;

	pFile->type = isdir ? 2 : 0;
	pFile->size = isdir ? 0 : (int)size;
}

/* ---- init / cleanup ----------------------------------------------------- */

void ffe_dir_init_from (const char *cfgfile)
{
	CfgStruct cfg;
	BPTR lock;

	dir_scan_close ();
	g_phase = PH_DONE;

	CfgOpen (&cfg, (char *)cfgfile);
	CfgFindSection (&cfg, "DIR");
	dir_cfg_path (&cfg, "AVIPath",    "DATA",     pDirAVIPath);
	dir_cfg_path (&cfg, "CmmdrPath",  "COMMANDR", pDirCmmdrPath);
	dir_cfg_path (&cfg, "SamplePath", "FX",       pDirSamplePath);
	dir_cfg_path (&cfg, "SongPath",   "MUSIC",    pDirSongPath);
	CfgClose (&cfg);
	/* All four calls are safe even though CfgOpen may have failed: CfgOpen
	 * zeroes the struct before it can fail and every accessor tests for it. */

	/* The commander directory is the one that has to exist -- it is where the
	 * browser starts and where saves land. Win32 falls back to the current
	 * directory; we fall back to the program's own, which is the same intent on
	 * a system where a detached process's current directory is not what the
	 * player thinks it is. */
	lock = dir_lock_dir (pDirCmmdrPath);
	if (lock == 0) {
		if (!dir_prog_dir (pDirCmmdrPath)) strcpy (pDirCmmdrPath, "PROGDIR:");
		dir_add_sep (pDirCmmdrPath);
	}
	else UnLock (lock);

	strcpy (pDirCmmdrPathSys, pDirCmmdrPath);

	dir_scan_volumes ();
}

void ffe_dir_init (void)
{
	ffe_dir_init_from (FFE_CONFIGFILE);
}

void ffe_dir_cleanup (void)
{
	dir_scan_close ();
	g_phase = PH_DONE;
}

/* ---- the ffeapi surface ------------------------------------------------- */

char *DirMakeAVIName (char *pBuf, char *pStub)
{
	strcpy (pBuf, pDirAVIPath);
	strcat (pBuf, pStub);
	strcat (pBuf, ".avi");
	return pBuf;
}

char *DirMakeSampleName (char *pBuf, char *pFilename)
{
	strcpy (pBuf, pDirSamplePath);
	strcat (pBuf, pFilename);
	return pBuf;
}

char *DirMakeSongName (char *pBuf, char *pFilename)
{
	strcpy (pBuf, pDirSongPath);
	strcat (pBuf, pFilename);
	return pBuf;
}

char *DirMakeCmmdrName (char *pBuf, char *pFilename)
{
	strcpy (pBuf, pDirCmmdrPath);
	strcat (pBuf, pFilename);
	return pBuf;
}

char *DirGetCmmdrPath (void)
{
	return pDirCmmdrPath;
}

void DirResetCmmdrPath (void)
{
	strcpy (pDirCmmdrPath, pDirCmmdrPathSys);
}

int DirFindFirst (FileInfo *pFile)
{
	dir_scan_close ();
	g_volidx = 0;
	g_phase  = PH_VOLS;

	dir_scan_volumes ();	/* media comes and goes; the list is only true now */

	return DirFindNext (pFile);
}

int DirFindNext (FileInfo *pFile)
{
	for (;;)
	{
		switch (g_phase)
		{
		case PH_VOLS:
			if (g_volidx < g_numvols) { *pFile = g_vols[g_volidx++]; return 1; }
			g_phase = PH_PARENT;
			break;

		case PH_PARENT:
			/* Synthesised, because an AmigaDOS directory listing contains no
			 * ".." to pick up the way the DOS one does. Offered first of the
			 * directory entries, which is where FAT put it and where both
			 * reference layers therefore show it. */
			g_phase = PH_DIRS;
			if (dir_has_parent (pDirCmmdrPath)) {
				strcpy (pFile->pName, "(Parent Dir)/");
				pFile->type = 4;
				pFile->size = 0;
				return 1;
			}
			break;

		case PH_DIRS:
		case PH_FILES:
			if (!g_scanopen && !dir_scan_open ()) {
				g_phase = (g_phase == PH_DIRS) ? PH_FILES : PH_DONE;
				break;
			}
			if (ExNext (g_lock, g_fib) == DOSFALSE) {
				dir_scan_close ();
				g_phase = (g_phase == PH_DIRS) ? PH_FILES : PH_DONE;
				break;
			}
			{
				/* fib_DirEntryType > 0 is the standard "this is a directory"
				 * test: ST_ROOT, ST_USERDIR and ST_LINKDIR are positive, files
				 * and file links negative. */
				int isdir = (g_fib->fib_DirEntryType > 0);
				const char *nm = (const char *)g_fib->fib_FileName;

				if (g_phase == PH_DIRS  && !isdir) break;
				if (g_phase == PH_FILES &&  isdir) break;
				if (nm[0] == 0) break;
				if (strcmp (nm, ".") == 0)  break;	/* the reference skips it */
				if (strcmp (nm, "..") == 0) break;	/* we synthesise our own */

				dir_fill (pFile, nm, isdir, g_fib->fib_Size);
				return 1;
			}

		default:
			return 0;
		}
	}
}

void DirNavigateTree (FileInfo *pFile)
{
	if (pFile->type == 2) {
		/* pName already carries its trailing '/', and pDirCmmdrPath already
		 * ends in ':' or '/', so this is the whole of the separator rule for
		 * the descending case: "Work:" + "save/" is "Work:save/". */
		if (strlen (pDirCmmdrPath) + strlen (pFile->pName) < DIR_PATHMAX)
			strcat (pDirCmmdrPath, pFile->pName);
	}
	else if (pFile->type == 4) {
		size_t n = strlen (pDirCmmdrPath);
		char *p;

		if (n == 0) return;
		if (pDirCmmdrPath[n - 1] == ':') return;	/* volume root: already at the top */
		if (pDirCmmdrPath[n - 1] == '/') pDirCmmdrPath[n - 1] = 0;

		p = strrchr (pDirCmmdrPath, '/');
		if (p != NULL) p[1] = 0;					/* "Work:a/b" -> "Work:a/" */
		else {
			p = strchr (pDirCmmdrPath, ':');
			if (p != NULL) p[1] = 0;				/* "Work:save" -> "Work:" */
			else pDirCmmdrPath[0] = 0;				/* relative single name -> the cwd */
		}
	}
	else if (pFile->type == 6) {
		strcpy (pDirCmmdrPath, pFile->pName);		/* "Work:", separator included */
	}
}

/* WHERE THE ORIGINAL GAME FILES ARE (2026-10-16, for the release).
 *
 * The player copies the original DOS game directory AS IT IS into a folder
 * next to the executable, so nothing has to be picked or renamed. Every file
 * the game opens (MISSION.DAT, CABIN*.BMP, FX/, COMMANDR/, later MUSIC/) is a
 * relative name, so making that folder the current directory is all it takes.
 * Order: next to the executable, then FFE/, Elite3/ (the floppy version's own
 * install folder name) and FIRSTENC/. AmigaDOS names are case-insensitive, so
 * the DOS upper-case names need no change. The lock is kept for the life of
 * the process (it IS the current directory). */
void ffe_find_data_dir (void)
{
	static const char *const dirs[] = { "PROGDIR:FFE", "PROGDIR:Elite3", "PROGDIR:FIRSTENC", NULL };
	char probe[64];
	BPTR l;
	int i;

	l = Lock ((CONST_STRPTR)"PROGDIR:MISSION.DAT", ACCESS_READ);
	if (l != 0) {
		UnLock (l);
		l = Lock ((CONST_STRPTR)"PROGDIR:", ACCESS_READ);
		if (l != 0) CurrentDir (l);
		return;
	}
	for (i = 0; dirs[i] != NULL; i++) {
		strcpy (probe, dirs[i]);
		strcat (probe, "/MISSION.DAT");
		l = Lock ((CONST_STRPTR)probe, ACCESS_READ);
		if (l == 0) continue;
		UnLock (l);
		l = Lock ((CONST_STRPTR)dirs[i], ACCESS_READ);
		if (l != 0) { CurrentDir (l); return; }
	}
	/* Last: a volume or assign called FFE: (2026-10-22, the AROS/universal
	 * HDF: the DOS folder mounted as its own drive in WinUAE). No "insert
	 * volume FFE" requester when it is not there. */
	{
		struct Process *me = (struct Process *)FindTask (NULL);
		APTR win = me->pr_WindowPtr;

		me->pr_WindowPtr = (APTR)-1;
		l = Lock ((CONST_STRPTR)"FFE:MISSION.DAT", ACCESS_READ);
		if (l != 0) {
			UnLock (l);
			l = Lock ((CONST_STRPTR)"FFE:", ACCESS_READ);
			if (l != 0) CurrentDir (l);
		}
		me->pr_WindowPtr = win;
	}
}
