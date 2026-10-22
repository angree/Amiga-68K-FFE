/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * DEVELOPER MODE (2026-10-16, release prep). The probe logs, frame dumps, the
 * hang watchdog and the harness hold/reboot files are for the test machines
 * only. They are on when PROGDIR:ffe_dev exists (the harness Work: folders
 * carry one) and off in a release, where the game writes nothing to its
 * folder except saved games and ffe_crash.log. */
#ifndef FFE_DEV_H
#define FFE_DEV_H
#include <stdio.h>
int ffe_dev_mode (void);
/* fopen() for probe output: NULL (as a failed fopen) outside developer mode */
FILE *ffe_logopen (const char *path, const char *mode);
#endif
