/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * Replacement for SDL 1.2's libSDLmain.a.
 *
 * WHY: the stock libSDLmain.a (built 2012 against MSVCRT) calls redirect_output(),
 * which touches the _iob[] FILE array through the import symbol _imp___iob. UCRT --
 * what every current MinGW-w64 targets -- has no _iob at all, so linking the stock
 * library fails with "undefined reference to `_imp___iob'". Nothing else in that
 * library is wanted here: its only other jobs are the WinMain->main glue (we build
 * -mconsole, so the CRT calls main directly) and setting the module handle.
 *
 * Losing redirect_output() is a feature, not a loss: it would have silently sent
 * stdout and stderr to stdout.txt / stderr.txt next to the executable. We want the
 * console, because M0's whole purpose is to observe this program.
 *
 * This file must NOT include SDL.h: SDL_main.h does `#define main SDL_main`, which
 * would rename the very function we are here to define. The two SDL symbols are
 * therefore declared by hand. SDL_main already has C linkage -- SDL_main.h declares
 * it extern "C" before sdlvideo.cpp defines it.
 */

#include <windows.h>

extern int  SDL_main(int argc, char *argv[]);
extern void SDL_SetModuleHandle(void *hInst);   /* SDLCALL is __cdecl on win32 */

int main(int argc, char *argv[])
{
	SDL_SetModuleHandle(GetModuleHandle(NULL));
	return SDL_main(argc, argv);
}
