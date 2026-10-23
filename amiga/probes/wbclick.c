/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * wbclick (2026-10-23): drives Workbench from INSIDE the emulated Amiga, so a
 * test can take the user's path (open drawers, double-click icons) without
 * any input on the host. Events go through input.device like real mouse
 * input; the pointer is placed in pixels of the frontmost screen.
 *   wbclick X Y          move and double-click
 *   wbclick X Y 1        move and single-click
 *   wbclick X Y 0        move only
 *   wbclick LIST         print the windows of the frontmost screen
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <exec/types.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>

extern struct IntuitionBase *IntuitionBase;

static struct IOStdReq *io;

static void send (struct InputEvent *ev)
{
	ULONG s, m;
	CurrentTime (&s, &m);
	ev->ie_TimeStamp.tv_secs = s;
	ev->ie_TimeStamp.tv_micro = m;
	io->io_Command = IND_WRITEEVENT;
	io->io_Data = ev;
	io->io_Length = sizeof *ev;
	DoIO ((struct IORequest *)io);
}

static void button (UWORD code)
{
	struct InputEvent ev;
	memset (&ev, 0, sizeof ev);
	ev.ie_Class = IECLASS_RAWMOUSE;
	ev.ie_Code = code;
	ev.ie_Qualifier = IEQUALIFIER_RELATIVEMOUSE;
	send (&ev);
}

int main (int argc, char **argv)
{
	struct MsgPort *port;
	struct InputEvent ev;
	struct IEPointerPixel pp;
	int clicks = 2, i;

	if (argc >= 2 && strcmp (argv[1], "LIST") == 0) {
		ULONG lk = LockIBase (0);
		struct Screen *s = IntuitionBase->FirstScreen;
		struct Window *w;
		printf ("screen '%s' %dx%d depth %d\n", s->Title ? (char *)s->Title : "",
		        s->Width, s->Height, s->BitMap.Depth);
		for (w = s->FirstWindow; w; w = w->NextWindow)
			printf ("window '%s' at %d,%d size %dx%d borders %d,%d\n",
			        w->Title ? (char *)w->Title : "", w->LeftEdge, w->TopEdge,
			        w->Width, w->Height, w->BorderLeft, w->BorderTop);
		UnlockIBase (lk);
		return 0;
	}
	if (argc < 3) { printf ("wbclick X Y [clicks] | LIST\n"); return 10; }
	if (argc >= 4) clicks = atoi (argv[3]);

	port = CreateMsgPort ();
	io = (struct IOStdReq *)CreateIORequest (port, sizeof *io);
	if (!io || OpenDevice ("input.device", 0, (struct IORequest *)io, 0)) {
		printf ("wbclick: no input.device\n");
		return 20;
	}
	memset (&ev, 0, sizeof ev);
	pp.iepp_Screen = IntuitionBase->FirstScreen;
	pp.iepp_Position.X = atoi (argv[1]);
	pp.iepp_Position.Y = atoi (argv[2]);
	ev.ie_Class = IECLASS_NEWPOINTERPOS;
	ev.ie_SubClass = IESUBCLASS_PIXEL;
	ev.ie_Code = IECODE_NOBUTTON;
	ev.ie_EventAddress = (APTR)&pp;
	send (&ev);
	Delay (10);
	for (i = 0; i < clicks; i++) {
		button (IECODE_LBUTTON);
		Delay (2);
		button (IECODE_LBUTTON | IECODE_UP_PREFIX);
		Delay (4);
	}
	printf ("wbclick %d,%d x%d\n", pp.iepp_Position.X, pp.iepp_Position.Y, clicks);
	CloseDevice ((struct IORequest *)io);
	DeleteIORequest ((struct IORequest *)io);
	DeleteMsgPort (port);
	return 0;
}
