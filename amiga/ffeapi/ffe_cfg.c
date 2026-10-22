/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * The .cfg reader. NOT a helper of ours -- the GAME calls it: ffe.asm:1781-1784
 * declares CfgOpen, CfgClose, CfgFindSection and CfgGetKeyVal as EXTERN, and
 * FUNC_001416_TimerInit (ffe.asm:85596) walks all four of them to read
 * [TIMER] maxfps out of jjffe.cfg. Signatures and struct layout are therefore
 * ABI, not ours to redesign; they are copied field for field from
 * ref/jjffe/ffecfg.h.
 *
 * THE STRUCT HAS TO FIT. The game does not build the CfgStruct on the stack, it
 * hands us a static one: `pTmCfgStruct times 32 db 0x0` at ffe.asm:85556, a
 * 32-byte zeroed block in .data, and the only instance in the whole
 * disassembly. Ours is two pointers, two ints and a pointer = 20 bytes on 68k,
 * the same 20 it is on x86 (both are ILP32), so it fits with 12 bytes spare.
 * The game never reads a field -- it only pushes the pointer -- so nothing but
 * the size is load-bearing, and 20 <= 32 holds.
 *
 * STORAGE differs from ref/jjffe/ffecfg.c, which strdup()s every name and every
 * value separately and frees them one by one. We read the file once and point
 * into it, so an open is exactly two allocations and a close exactly two frees:
 *
 *   cfg->filename -> [ filename \0 ][ the whole file text \0 ]
 *   cfg->sections -> [ CfgSection x sectioncount ][ CfgKey x total keys ]
 *
 * cfg->filename doubles as the owner of the text because the struct has no
 * field to keep a third pointer in and we may not add one. Every name and data
 * pointer aims inside that one block, NUL-terminated in place.
 *
 * No Amiga headers: this is portable C on top of stdio, and "PROGDIR:x.cfg"
 * reaches dos.library through fopen() on its own.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ffeapi.h"

/* Case-insensitive compare, ASCII only. Hand-rolled because the reference
 * reaches for strcasecmp on Linux and stricmp on Win32 and we would rather not
 * find out at link time which of the two bebbo's libc happens to spell; every
 * key and section name in every .cfg is a plain ASCII identifier. */
static int cfg_stricmp (const char *a, const char *b)
{
	unsigned char ca, cb;

	do {
		ca = (unsigned char)*a++;
		cb = (unsigned char)*b++;
		if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca + 32);
		if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb + 32);
	} while (ca != 0 && ca == cb);

	return (int)ca - (int)cb;
}

/* Cut the next line out of the text at *pp. Returns its first character, stores
 * the one-past-the-end in *pend and leaves *pp on the start of the following
 * line. NON-MUTATING on purpose: the counting pass runs over the same text
 * before the storing pass does, and a NUL written early would make the second
 * walk stop at the first line. */
static char *cfg_line (char **pp, char **pend)
{
	char *s = *pp;
	char *e = s;

	while (*e != 0 && *e != '\n' && *e != '\r') e++;
	*pend = e;
	while (*e == '\n' || *e == '\r') e++;	/* eats blank lines too; we do not count lines */
	*pp = e;

	return s;
}

/* Classify one line [s,e), still without touching it. Returns
 *   '['  section header, name in [*pa,*pae)
 *   '='  key line, name in [*pa,*pae), value in [*pb,*pbe)
 *   0    blank, comment, or junk -- ignore.
 *
 * '#' starts a comment. The reference does NOT do that: it looks for the first
 * '=' or '[' anywhere in the line, so `# glpackedtex=1 is faster` in the stock
 * ffewinsdl.cfg becomes a key literally named "#". Harmless there (nobody looks
 * that name up) but sloppy, and a '[' inside a comment would invent a whole
 * section. Skipping comment lines outright cannot change the value of any key a
 * real config actually defines. */
static int cfg_classify (char *s, char *e, char **pa, char **pae,
                         char **pb, char **pbe)
{
	char *t;

	while (s < e && (*s == ' ' || *s == '\t')) s++;
	while (e > s && (e[-1] == ' ' || e[-1] == '\t')) e--;
	if (s == e || *s == '#') return 0;

	if (*s == '[') {
		for (t = ++s; t < e && *t != ']'; t++) ;
		if (t == e) return 0;			/* no closing bracket: not a header */
		*pa = s; *pae = t;
		return '[';
	}

	for (t = s; t < e && *t != '='; t++) ;
	if (t == e) return 0;

	*pa  = s;
	*pae = t;
	while (*pae > s && ((*pae)[-1] == ' ' || (*pae)[-1] == '\t')) (*pae)--;
	if (*pa == *pae) return 0;			/* "= value" with no key name */

	for (t++; t < e && (*t == ' ' || *t == '\t'); t++) ;
	*pb  = t;
	*pbe = e;
	return '=';
}

int CfgOpen (CfgStruct *cfg, char *filename)
{
	FILE *fd;
	long len;
	size_t namelen, got;
	char *block, *text, *p, *s, *e, *a, *ae, *b, *be;
	int nsect = 0, nkeys = 0, cur;
	CfgSection *sect;
	CfgKey *pool;

	if (cfg == NULL) return 0;
	memset (cfg, 0, sizeof (CfgStruct));
	if (filename == NULL) return 0;

	/* "rb": the line splitter drops \r itself, so a config written on the PC
	 * and a config written on the Amiga parse the same either way. */
	fd = fopen (filename, "rb");
	if (fd == NULL) return 0;

	if (fseek (fd, 0, SEEK_END) != 0) { fclose (fd); return 0; }
	len = ftell (fd);
	if (len < 0) { fclose (fd); return 0; }
	rewind (fd);

	namelen = strlen (filename);
	block = (char *)malloc (namelen + 1 + (size_t)len + 1);
	if (block == NULL) { fclose (fd); return 0; }

	memcpy (block, filename, namelen + 1);
	text = block + namelen + 1;
	got  = fread (text, 1, (size_t)len, fd);
	text[got] = 0;
	fclose (fd);

	/* Pass one: how many sections, how many keys. Keys before the first
	 * [SECTION] are dropped, as in the reference. */
	for (p = text; *p != 0; ) {
		int kind;
		s = cfg_line (&p, &e);
		kind = cfg_classify (s, e, &a, &ae, &b, &be);
		if (kind == '[') nsect++;
		else if (kind == '=' && nsect > 0) nkeys++;
	}

	if (nsect > 0) {
		size_t sz = (size_t)nsect * sizeof (CfgSection)
		          + (size_t)nkeys * sizeof (CfgKey);
		sect = (CfgSection *)malloc (sz);
		if (sect == NULL) { free (block); return 0; }
		pool = (CfgKey *)(void *)((char *)sect
		                          + (size_t)nsect * sizeof (CfgSection));
	}
	else { sect = NULL; pool = NULL; }

	/* Pass two: fill in, terminating names and values in place. pool walks
	 * forward and every section takes the cursor as the head of its own run --
	 * a section's keys are contiguous because they are contiguous in the file. */
	cur = -1;
	for (p = text; *p != 0; ) {
		int kind;
		s = cfg_line (&p, &e);
		kind = cfg_classify (s, e, &a, &ae, &b, &be);

		if (kind == '[') {
			*ae = 0;
			cur++;
			sect[cur].name     = a;
			sect[cur].keycount = 0;
			sect[cur].keys     = pool;
		}
		else if (kind == '=' && cur >= 0) {
			*ae = 0;
			*be = 0;
			pool->name = a;
			pool->data = b;
			pool++;
			sect[cur].keycount++;
		}
	}

	cfg->filename       = block;
	cfg->wasmodified    = 0;
	cfg->sectioncount   = nsect;
	cfg->sections       = sect;
	cfg->currentsection = NULL;

	return 1;
}

int CfgClose (CfgStruct *cfg)
{
	if (cfg == NULL || cfg->filename == NULL) return 0;

	/* wasmodified is never set: nothing on the Amiga side writes a key back and
	 * the game only ever reads, so "save if modified" has nothing to save. The
	 * field stays because it is part of the layout the game reserves for. */
	free (cfg->sections);
	free (cfg->filename);		/* one block: the name AND the whole file text */
	memset (cfg, 0, sizeof (CfgStruct));

	return 1;
}

int CfgFindSection (CfgStruct *cfg, char *sectname)
{
	int i;

	if (cfg == NULL || cfg->filename == NULL || sectname == NULL) return 0;

	for (i = 0; i < cfg->sectioncount; i++) {
		if (cfg_stricmp (sectname, cfg->sections[i].name) == 0) {
			cfg->currentsection = &cfg->sections[i];
			return 1;
		}
	}

	return 0;
}

/* The value of keyname in the current section, or NULL. */
static const char *cfg_lookup (CfgStruct *cfg, char *keyname)
{
	int i;

	if (cfg == NULL || cfg->filename == NULL || keyname == NULL) return NULL;
	if (cfg->currentsection == NULL) return NULL;

	for (i = 0; i < cfg->currentsection->keycount; i++)
		if (cfg_stricmp (keyname, cfg->currentsection->keys[i].name) == 0)
			return cfg->currentsection->keys[i].data;

	return NULL;
}

int CfgGetKeyVal (CfgStruct *cfg, char *keyname, int *value)
{
	const char *data = cfg_lookup (cfg, keyname);

	if (data == NULL || value == NULL) return 0;

	/* Base 0, exactly as the reference: "0x1f" is hex and a leading 0 is octal.
	 * Every stock config writes plain decimal, so the octal trap never fires,
	 * but changing the base would change the meaning of a user's file. */
	*value = (int)strtol (data, NULL, 0);
	return 1;
}

int CfgGetKeyStr (CfgStruct *cfg, char *keyname, char *value, int buflen)
{
	const char *data = cfg_lookup (cfg, keyname);
	size_t n;

	if (data == NULL || value == NULL || buflen <= 0) return 0;

	/* The reference uses strncpy, which leaves the buffer unterminated when the
	 * value is exactly buflen long. We always terminate; the callers are all
	 * ours and all of them strcat onto the result. */
	n = strlen (data);
	if (n > (size_t)buflen - 1) n = (size_t)buflen - 1;
	memcpy (value, data, n);
	value[n] = 0;

	return 1;
}

int CfgGetKeyValDef (CfgStruct *cfg, char *keyname, int *value, int def)
{
	if (value == NULL) return 0;

	/* Safe after a failed CfgOpen or CfgFindSection by construction: CfgOpen
	 * zeroes the struct before it can fail, so filename and currentsection are
	 * NULL and cfg_lookup gives up on them. */
	if (CfgGetKeyVal (cfg, keyname, value)) return 1;

	*value = def;
	return 0;
}
