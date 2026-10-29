/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * The program's own folder as a real path (2026-10-24, developer: "remove
 * every use of PROGDIR"). PROGDIR: is a per-process name: a Shell or a
 * program started from ours does not have it, and asks for a volume called
 * PROGDIR. ffe_pd("name") returns "<the folder of this program>/name", e.g.
 * "Work:AmiFFE/amiffe.cfg", resolved once with NameFromLock(GetProgramDir()).
 * If that fails the name alone is returned (the current directory).
 * Eight rotating buffers, so a few results can be used at the same time
 * (e.g. both arguments of a rename); not for the music refill process.
 */
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "ffe_dev.h"

#define PD_LEN 256

const char *ffe_pd (const char *name)
{
	static char base[PD_LEN];
	static int known;
	static char ring[8][PD_LEN];
	static int next;
	char *out;

	if (!known) {
		BPTR l = GetProgramDir ();
		size_t n;

		base[0] = '\0';
		if (l && NameFromLock (l, (STRPTR)base, PD_LEN - 2)) {
			n = strlen (base);
			if (n > 0 && base[n - 1] != ':' && base[n - 1] != '/') { base[n] = '/'; base[n + 1] = '\0'; }
		} else
			base[0] = '\0';
		known = 1;
	}
	out = ring[next];
	next = (next + 1) & 7;
	snprintf (out, PD_LEN, "%s%s", base, name);
	return out;
}
