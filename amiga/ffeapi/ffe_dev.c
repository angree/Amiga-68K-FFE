/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 * Developer mode switch, see ffe_dev.h. */
#include <stdio.h>
#include "ffe_dev.h"

int ffe_dev_mode (void)
{
	static int state = -1;

	if (state < 0) {
		FILE *f = fopen ("PROGDIR:ffe_dev", "r");
		state = f != NULL;
		if (f != NULL) fclose (f);
	}
	return state;
}

FILE *ffe_logopen (const char *path, const char *mode)
{
	return ffe_dev_mode () ? fopen (path, mode) : NULL;
}
