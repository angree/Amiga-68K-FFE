/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 angree. See LICENSE at the repository root.
 *
 * FFE video API on the Amiga chunky screen.
 *
 * The whole file is eight functions over amiga_gfx.c's 320x200 8bpp chunky
 * buffer. It contains NO Amiga headers on purpose: everything Amiga-specific
 * is already behind amiga_gfx.h.
 *
 * The semantics are those of ref/jjffe/sdl/sdlvideo.cpp, which is the
 * definition of what the game expects, down to the details that look like
 * oversights and are not:
 *   - VideoBlit advances the source by jump + w per row, VideoMaskedBlit by
 *     jump alone (its inner loop walks the source pixel by pixel);
 *   - a 320-wide VideoBlit is the frame flip, every other width copies only;
 *   - nothing clips. The game always blits inside the buffer, and adding
 *     clipping here would hide a translation bug rather than fix one.
 *
 * WHAT WE DO NOT COPY from the SDL layer: its pPalTable[] gamma ramps, built
 * from RedBase/GreenBase/BlueBase in the .cfg. With the default base of 0 the
 * ramp is the identity, and we have no config file. Palette values therefore
 * go straight through, 6-bit VGA scaled to 8-bit with << 2.
 */

#include <stdio.h>
#include "ffe_dev.h"
#include <stdlib.h>
#include <string.h>

#include "ffeapi.h"
#include "amiga_gfx.h"

/* Cached once by ffe_video_init(): amigagfx_chunky() is a plain accessor, but
 * VideoBlit is called several hundred times a frame in the console screens and
 * the call is pure overhead there. NULL until the screen is open, which is the
 * one case every entry point below has to survive -- SystemInit cannot refuse
 * to return, so a failed open has to leave the video calls as no-ops. */
static UCHAR *g_chunky;
static int    g_pitch;

/* The palette as amigagfx_set_palette() wants it: 8-bit r,g,b per entry. The
 * game's 6-bit values are recovered by VideoGetPalValue with >> 2, which is
 * exact for every value it can legally set (0..63 << 2 >> 2 == 0..63) and is
 * what the SDL layer does too. */
static UCHAR g_pal[256 * 3];

void ffe_video_init (void)
{
	g_chunky = amigagfx_chunky ();
	g_pitch  = amigagfx_pitch ();
	memset (g_pal, 0, sizeof (g_pal));
	amigagfx_set_palette (g_pal, 0, 256);
}

void ffe_video_cleanup (void)
{
	/* The buffer belongs to amiga_gfx.c and dies with amigagfx_close(); drop
	 * our copy of the pointer so a stray late blit is a no-op, not a write
	 * into freed memory. */
	g_chunky = NULL;
	g_pitch  = 0;
}

/* FIRST-LIGHT DEBUG, remove once the game is visually verified: every 512th
 * present overwrites PROGDIR:ffe_frame.raw with the palette + index buffer in
 * the exact format ffe_drawtest.c writes, so build\frame2png.py reads it
 * unchanged. Separates "the pixels are all zero" from "the palette is black"
 * when the host window shows nothing. */
static unsigned long g_presents;

/* FIRST-LIGHT DEBUG: the game's own clock and the timer support variables,
 * read straight out of the converted data image (every label is xdef). One
 * log line per dump answers "is the game clock advancing, and how fast". */
extern long game_clock[2]     __asm__("DATA_008804");
extern long accum_9156        __asm__("DATA_009156");
extern long accum_9157        __asm__("DATA_009157");
extern long tm_last           __asm__("tmLastTime");
extern long tm_minframe       __asm__("tmMinFrame");
extern long tm_next50         __asm__("tmNext50Hz");
extern UCHAR gui_action       __asm__("DATA_009158");
extern UCHAR gui_lastscan     __asm__("DATA_009181");
extern long  gui_scanaccum    __asm__("DATA_007690");
extern UCHAR gui_keystates[]  __asm__("DATA_007691");
extern long  splash_timeout   __asm__("splashTimeout");

static void dump_clock (void)
{
	/* SEPARATE FILE ON PURPOSE (2026-08-02). The measurement dump used to
	 * share ffe_game.log with the harness's boot/marker/ACTION lines. One
	 * host-side read during a run left that entry corrupt on the I: share --
	 * Windows reports "Access is denied" and WSL shows `-????????? ?` for
	 * it, permanently, while WinUAE goes on writing it happily. Keeping the
	 * dump in its own file means a poisoned ffe_game.log costs the harness
	 * nothing and costs the measurement nothing. Still: read neither while
	 * the emulator runs. */
	FILE *f = ffe_logopen ("PROGDIR:ffe_dump.log", "a");

	if (f == NULL) return;
	{
		extern int ffe_dbg_held (int raw);

		extern long dbg_ptr_x __asm__("ptrSavedX");
		extern long dbg_ptr_y __asm__("ptrSavedY");
		/* the game's integrated pointer: UpdatePtr stores x | y<<16 packed;
		 * word 009208 = X, 009209 = Y (ffe.asm:97575-97578 reads them so) */
		extern unsigned short dbg_gui_x __asm__("DATA_009208");
		extern unsigned short dbg_gui_y __asm__("DATA_009209");
		/* hot-area hit history: the walker stores every matched action code
		 * here (ffe.asm:86097-86099), so a hit is visible even when the
		 * main loop consumes DATA_009158 between two presents */
		extern UCHAR dbg_hit_now  __asm__("DATA_009159");
		extern UCHAR dbg_hit_prev __asm__("DATA_009160");

		fprintf (f, "presents=%lu clock=%ld/%ld acc56=%ld "
		         "act=%02x lastscan=%02x scanacc=%ld ky=%02x held=%d "
		         "splashTO=%ld ptr=%ld,%ld gui=%u,%u hit=%02x,%02x ts=%lu\n",
		         g_presents, game_clock[0], game_clock[1], accum_9156,
		         gui_action, gui_lastscan, gui_scanaccum,
		         gui_keystates[0x15], ffe_dbg_held (0x15),
		         splash_timeout, dbg_ptr_x, dbg_ptr_y,
		         (unsigned)dbg_gui_x, (unsigned)dbg_gui_y,
		         dbg_hit_now, dbg_hit_prev,
		         (unsigned long)TimerGetTimeStamp ());
	}
	/* HOW FAR INTO THE RENDER (2026-08-05): the monotonic progress counters
	 * filled by FfeRenderPath (ffe_game.c). The machine hard-resets a few tens
	 * of frames after the splash and writes no ffe_crash.log at all, so the
	 * LAST of these lines in ffe_dump.log is the deepest point the render is
	 * known to have reached before the vectors went.
	 * NOT RESET, unlike the CNT block below: the wanted quantity is cumulative
	 * depth ("did FUNC_001750 ever run"), not a per-window delta. The id order
	 * is fixed in ffe_rp_id[], so two runs' lines compare column by column. */
	{
		extern long ffe_rp_id[];
		extern long ffe_rp_n[];
		int i;

		fprintf (f, "RENDERPATH");
		for (i = 0; i < 9; i++)
			fprintf (f, " %ld=%ld", ffe_rp_id[i], ffe_rp_n[i]);
		fprintf (f, "\n");
	}
	/* THE FUNC_001712 BISECTION RING (2026-08-05). FfeMark1712 pushes a site
	 * number and does NO I/O -- three earlier cuts of that probe wrote a line
	 * per marker and every one of them measured itself instead of the game,
	 * because sites 37/38 sit in a loop running ~1040 times per entry and an
	 * fopen each time slowed the emulated 68020 until the run never reached the
	 * crash. Printed here, oldest first, so the LAST of these lines names the
	 * deepest point reached before the machine reset -- at most 32 presents
	 * stale, which is what the cadence buys.
	 *
	 * arg7/base are captured at site 10 (FUNC_001712's bare label) for the one
	 * data-driven access in its entry window, ffe.asm:104909
	 * `mov al,[eax+edx*8+0x3]'. entries is the positive control: the ring means
	 * nothing while entries is zero. */
	{
		extern long ffe_mark_ring[];
		extern long ffe_mark_pos, ffe_mark_arg7, ffe_mark_base,
		            ffe_mark_entries;
		long total = ffe_mark_pos;
		long n = total < 64 ? total : 64;
		long i;

		fprintf (f, "MARKRING n=%ld entries=%ld arg7=%ld base=%08lx",
		         total, ffe_mark_entries, ffe_mark_arg7,
		         (unsigned long)ffe_mark_base);
		for (i = total - n; i < total; i++)
			fprintf (f, " %ld", ffe_mark_ring[i & 63]);
		fprintf (f, "\n");
	}
	/* THE DISPATCH WALK (2026-08-05). Consecutive entries are the (in, out)
	 * pointer pair of whatever handler ran between them, because the model
	 * interpreter advances the stream by the handler's RETURN VALUE
	 * (ffe.asm:111195-111206). Opcodes 21 and 1 are dispatched by the PC
	 * reference and never by us; if a skip opcode returns the wrong pointer,
	 * this ring shows the jump that misses them. */
	{
		extern void FfeOptDump (FILE *) __asm__("_FfeOptDump");
		extern long ffe_opt_pos;
		/* FfeOptDump also carries MODSEQ, NODE1470 and the other rings, which
		 * have nothing to do with the optrace one. Gating the whole printer on
		 * ffe_opt_pos meant a run with --node1470 injected but not --optrace
		 * printed NOTHING and read exactly like "the probe never fired" --
		 * the volume-control rule hiding the event being hunted, which is a
		 * standing method rule here. Fire when EITHER ring has content.
		 *
		 * Gate on TOTAL CALLS, not on recorded entries: FfeNode1470 rejects a
		 * sample whose context pointer looks wild, so `n' can be 0 while the
		 * function ran thousands of times -- and gating on `n' hides exactly
		 * the case worth seeing, "it ran and every sample was rejected". */
		extern long ffe_s70_total;

		if (ffe_opt_pos || ffe_s70_total)
			FfeOptDump (f);
	}
	/* THE SKIP ARITHMETIC'S OWN INPUTS -- the mirror of the reference's
	 * PCSKIP1753. `exit' counts the calls that clear DATA_007824 and END the
	 * dispatcher loop, which is what makes a large OPT delta legitimate. */
	/* FUNC_001534's own view of the clock -- the mirror of PCFUNC1534. */
	{
		extern long ffe_f34_calls, ffe_f34_clock[], ffe_f34_ret[], ffe_f34_n;
		int i;

		if (ffe_f34_calls) {
			fprintf (f, "F1534 calls=%ld", ffe_f34_calls);
			for (i = 0; i < ffe_f34_n; i++)
				fprintf (f, "  [clock=%ld ret=%ld]", ffe_f34_clock[i],
				         ffe_f34_ret[i]);
			fprintf (f, "\n");
		}
	}
	/* THE SEED OF THE x10 (2026-08-06). Chain, each link measured:
	 *   [DATA_008804] -> DATA_009132 (ffe.asm:71656-71657)
	 *   DATA_009132 >> 10 -> DATA_009129[0] (71670-71672, and 71678-71681
	 *      after subtracting 0x8276e)
	 *   DATA_009129 is record offset 0x9c of the view object, which is what
	 *      FUNC_001473 mode 0x80 reads through [a1+0x14c]
	 *   -> FUNC_001758's operand o1 -> the condition fields at +0x54
	 *   -> the four skip opcodes -> which blocks of the model are walked.
	 * o1 measured 1 on the reference and 10 here, so this is where to look.
	 * DATA_009132 is be32/dword-only with all four accesses dword, so it is
	 * NOT a storage-kind asymmetry -- print the VALUE and compare. */
	{
		/* ffe.asm:71655-71656 is `mov ebx,DATA_008804 / mov eax,[ebx]', i.e.
		 * the label's ADDRESS is taken and its first dword read -- so the C
		 * declaration must be an ARRAY, not a pointer. Declaring it `long *'
		 * and taking &var would print the address of the C variable rather
		 * than the game's value. */
		extern long DATA_009132 __asm__("DATA_009132");
		extern char clock_arr[] __asm__("DATA_008804");

		fprintf (f, "TIMESEED 009132=%ld (>>10 = %ld)  008804=%ld\n",
		         DATA_009132, (long)((unsigned long)DATA_009132 >> 10),
		         *(const long *)clock_arr);
	}
	/* FUNC_001758's first store per destination index -- the mirror of
	 * PCWRITE1758. The selector names which of the sixteen operations ran. */
	{
		extern long ffe_w58_sel[], ffe_w58_o1[], ffe_w58_o2[], ffe_w58_b[];
		extern long ffe_w58_seen[], ffe_w58_n[], ffe_w58_calls;
		int i;

		if (ffe_w58_calls) {
			extern long ffe_w58_a1, ffe_w58_p14c, ffe_w58_w9c;
			extern long ffe_w58_ptrseen, ffe_w58_bytes[];

			if (ffe_w58_ptrseen) {
				extern long ffe_w58_w9c_first, ffe_w58_w9c_last;
				extern long ffe_w58_w9c_min, ffe_w58_w9c_max;
				extern long ffe_w58_clk_first, ffe_w58_clk_last;
				int q;

				/* THE SWEEP. If w9c ranges over the reference's value while
				 * opcode 21 (FUNC_001697) still never fires, the divergence is
				 * NOT timing -- we will have passed through the reference's
				 * clock and still behaved differently. */
				fprintf (f, "W9CSWEEP first=%ld last=%ld min=%ld max=%ld "
				            "clkfirst=%ld clklast=%ld\n",
				         ffe_w58_w9c_first, ffe_w58_w9c_last,
				         ffe_w58_w9c_min, ffe_w58_w9c_max,
				         ffe_w58_clk_first, ffe_w58_clk_last);
				fprintf (f, "PTR14C a1=%08lx p=%08lx w9c=%ld bytes=",
				         (unsigned long)ffe_w58_a1,
				         (unsigned long)ffe_w58_p14c, ffe_w58_w9c);
				for (q = 0; q < 12; q++)
					fprintf (f, "%02lx,", ffe_w58_bytes[q]);
				fprintf (f, "\n");
			}
			fprintf (f, "WRITE1758 calls=%ld", ffe_w58_calls);
			for (i = 0; i < 8; i++)
				if (ffe_w58_seen[i])
					fprintf (f, "  d%d[sel=%ld b=%04lx o1=%ld o2=%ld n=%ld]",
					         i, ffe_w58_sel[i], ffe_w58_b[i], ffe_w58_o1[i],
					         ffe_w58_o2[i], ffe_w58_n[i]);
			fprintf (f, "\n");
		}
	}
	/* THE CONDITION DECODER'S MODE SPLIT -- the mirror of PCCOND1473. */
	{
		extern long ffe_cond_mode[], ffe_cond_calls;
		extern long ffe_cond_arg[], ffe_cond_val[], ffe_cond_seen[];
		int m;

		if (ffe_cond_calls) {
			fprintf (f, "COND1473 calls=%ld m00=%ld m40=%ld m80=%ld mc0=%ld",
			         ffe_cond_calls, ffe_cond_mode[0], ffe_cond_mode[1],
			         ffe_cond_mode[2], ffe_cond_mode[3]);
			for (m = 0; m < 4; m++)
				if (ffe_cond_seen[m])
					fprintf (f, "  [%d arg=%02lx val=%ld]", m * 0x40,
					         ffe_cond_arg[m], ffe_cond_val[m]);
			fprintf (f, "\n");
			{
				extern long ffe_cx_val[], ffe_cx_seen[], ffe_cx_n[];
				extern long ffe_cw_val[], ffe_cw_seen[], ffe_cw_n[];
				int i;

				fprintf (f, "CONDC0");
				for (i = 0; i < 64; i++)
					if (ffe_cx_seen[i])
						fprintf (f, " i%d=%ld(n%ld)", i, ffe_cx_val[i],
						         ffe_cx_n[i]);
				fprintf (f, "\nCOND80");
				for (i = 0; i < 64; i++)
					if (ffe_cw_seen[i])
						fprintf (f, " i%d=%ld(n%ld)", i, ffe_cw_val[i],
						         ffe_cw_n[i]);
				fprintf (f, "\n");
			}
		}
	}
	{
		extern long ffe_sk_calls, ffe_sk_ovf, ffe_sk_zeroflag, ffe_sk_exit;
		extern long ffe_sk_w[], ffe_sk_n[], ffe_sk_slots;
		long i;

		if (ffe_sk_calls) {
			fprintf (f, "SKIP1753 calls=%ld noflag=%ld exit=%ld ovf=%ld",
			         ffe_sk_calls, ffe_sk_zeroflag, ffe_sk_exit, ffe_sk_ovf);
			for (i = 0; i < ffe_sk_slots; i++)
				fprintf (f, " w%04lx=%ld(d%ld)", ffe_sk_w[i], ffe_sk_n[i],
				         2 * (((long)(short)ffe_sk_w[i]) >> 5));
			fprintf (f, "\n");
			{
				/* The same total, split by WHICH handler called. `noflag' is
				 * 68% here against the reference's 13% while the decoder
				 * feeding it measures identical, so the aggregate cannot say
				 * which of the four is wrong and this can. `other' must stay
				 * zero: a count there means the caller bucketing is broken,
				 * not that a fifth caller exists. */
				extern long ffe_sk_by[], ffe_sk_by_zero[];
				static const int who[4] = { 1752, 1755, 1756, 1757 };

				fprintf (f, "SKIPWHO");
				for (i = 0; i < 4; i++)
					fprintf (f, " %d=%ld/%ld", who[i], ffe_sk_by_zero[i],
					         ffe_sk_by[i]);
				fprintf (f, " other=%ld/%ld\n", ffe_sk_by_zero[4],
				         ffe_sk_by[4]);
			}
		}
	}
	/* THE ARENA ALLOCATOR'S BOUND (2026-08-05). FUNC_001745 compares
	 * [DATA_007823] + 36*(a1+a2) + 8*a3 + 12 against 0x8000 with a SIGNED
	 * `jng', so a NEGATIVE total passes the check and the writes that follow
	 * land outside the 32768-byte arena DATA_009251 -- the shape of the low
	 * memory write that eats the exception vectors. `calls' is the positive
	 * control: neg=0 only means anything while calls is non-zero. */
	{
		extern long ffe_arena_max, ffe_arena_neg, ffe_arena_calls;

		fprintf (f, "ARENA max=%ld neg=%ld calls=%ld\n",
		         ffe_arena_max, ffe_arena_neg, ffe_arena_calls);
	}
	/* VIEWPORT DEBUG (2026-08-01): the render-chain call counter filled by
	 * FfeCount (ffe_game.c) through inject_count.py payloads. Printed and
	 * RESET here, so each line counts the calls made since the previous
	 * dump -- "did any primitive reach the rasteriser in these 128 frames".
	 * Comes out with the rest of the debug rig. */
	{
		extern long ffe_cnt_id[];
		extern long ffe_cnt_n[];
		int i;
		int any = 0;

		for (i = 0; i < 24; i++) {
			if (ffe_cnt_id[i] == 0) break;
			if (!any) { fprintf (f, "CNT"); any = 1; }
			fprintf (f, " %ld=%ld", ffe_cnt_id[i], ffe_cnt_n[i]);
			ffe_cnt_n[i] = 0;
		}
		if (any) fprintf (f, "\n");
	}
	/* VIEWPORT DEBUG: the game's own error escape, now that it is real
	 * (ffe_setjmp.s). LJ counts LongJmp calls; POOL16 is the renderer's
	 * small-node counter (DATA_009276, ceiling 3000 in FUNC_001820) and
	 * NODE72 the big-node one (DATA_009273, ceiling 1500 in FUNC_001818).
	 * A climbing LJ with POOL16 pinned at 3000 means every frame is being
	 * abandoned for pool exhaustion -- geometry is reaching the renderer
	 * but far too much of it. */
	{
		extern long ffe_longjmp_n __asm__("_ffe_longjmp_n");
		extern long pool16 __asm__("DATA_009276");
		extern long node72 __asm__("DATA_009273");

		fprintf (f, "LJ n=%ld POOL16=%ld NODE72=%ld\n",
		         ffe_longjmp_n, pool16, node72);
	}
	/* CONSOLE BUTTON ARRAY (2026-08-02). FUNC_000261_ConsoleDrawButtons walks
	 * 82 records of stride 0x3e from DATA_005101 and uses each record's
	 * curindex as a SCALED index: `lea eax,[ebx+eax*4-0x30]`. curindex lives
	 * in DATA_005102/005103, which convert_static left in x86 byte order
	 * because their sizes are not multiples of 4 (it says so in a WARN on
	 * every run). If that is real, curindex reads as e.g. 0x01000000 and the
	 * lea addresses 64 MB past the array. Read the fields here exactly as the
	 * game does -- no injection needed, the labels are xdef'd. */
	{
		extern unsigned char btn_flag[] __asm__("DATA_005101");
		int b, vis = 0, nz = 0;

		/* ALL 82, not the first 8: the first sample looked at buttons 0..7
		 * only and found them hidden, which proves nothing if the cockpit's
		 * function-key buttons live at higher indices. Count how many carry
		 * the visible bit, then name the first few that do. */
		for (b = 0; b < 0x52; b++) {
			const unsigned char *rec = btn_flag + b * 0x3e;

			if (rec[0] & 1) vis++;
			if (*(const long *)(rec + 4)) nz++;
		}
		fprintf (f, "BTN visible=%d/82 nonzero_curindex=%d list:", vis, nz);
		for (b = 0; b < 0x52; b++) {
			const unsigned char *rec = btn_flag + b * 0x3e;

			if (rec[0] & 1)
				fprintf (f, " %d(ci=%lx)", b, *(const long *)(rec + 4));
		}
		fprintf (f, "\n");
	}
	/* COLOUR PROBE: the 16-bit colour FUNC_001791 hands to DPal16toPal.
	 * A byte-swapped word shows up here as a value with its halves the
	 * wrong way round -- compare against the same field read big-endian. */
	{
		extern long ffe_col_v[];
		extern long ffe_col_ptr[];
		extern long ffe_col_n;
		extern long ffe_col_calls;
		int i;

		if (ffe_col_calls) {
			fprintf (f, "COL calls=%ld n=%ld:", ffe_col_calls, ffe_col_n);
			for (i = 0; i < (int)ffe_col_n && i < 16; i++)
				fprintf (f, " %lx@%lx(swap=%lx)", ffe_col_v[i],
				         ffe_col_ptr[i],
				         ((ffe_col_v[i] >> 8) & 0xff) | ((ffe_col_v[i] & 0xff) << 8));
			fprintf (f, "\n");
			ffe_col_n = 0;
			ffe_col_calls = 0;
		}
	}
	/* VIEWPORT DEBUG: eight arguments of the first call to each traced
	 * function (FfeArg8Log) -- arg7 is the subdivision level the recursive
	 * terrain handlers stop on. */
	{
		extern long ffe_a8_num[];
		extern long ffe_a8_arg[][8];
		extern long ffe_a8_node[][10];
		extern long ffe_a8_n;
		extern long ffe_a8_calls;
		int i, k;

		if (ffe_a8_n) {
			for (i = 0; i < ffe_a8_n; i++) {
				fprintf (f, "A8 %ld:", ffe_a8_num[i]);
				for (k = 0; k < 8; k++)
					fprintf (f, " %lx", ffe_a8_arg[i][k]);
				fprintf (f, " | node");
				for (k = 0; k < 10; k++)
					fprintf (f, " %lx", ffe_a8_node[i][k]);
				fprintf (f, "\n");
			}
			fprintf (f, "A8 calls=%ld\n", ffe_a8_calls);
			ffe_a8_n = 0;
			ffe_a8_calls = 0;
		}
	}
	/* VIEWPORT DEBUG: the caller census (FfeRetCensus), which names the
	 * call sites eating the pool. Addresses resolve through the MAP line
	 * plus build\amiga-out\ffe_game.symmap. */
	{
		extern unsigned long ffe_ret_pc[];
		extern long ffe_ret_n[];
		extern long ffe_ret_total;
		int i;

		if (ffe_ret_total) {
			fprintf (f, "RET total=%ld", ffe_ret_total);
			for (i = 0; i < 24 && ffe_ret_pc[i]; i++)
				fprintf (f, " %lx=%ld", ffe_ret_pc[i], ffe_ret_n[i]);
			fprintf (f, "\n");
			for (i = 0; i < 24; i++) { ffe_ret_pc[i] = 0; ffe_ret_n[i] = 0; }
			ffe_ret_total = 0;
		}
	}
	/* VIEWPORT DEBUG: one anchor line, so a runtime pointer sampled out of
	 * the render chain can be resolved to a LABEL. AmigaDOS relocates at
	 * load, so only the offset from a known symbol is meaningful; these
	 * three pin the two converted data sections and the code. */
	{
		static int anchored;

		if (!anchored) {
			extern char bmp_first[] __asm__("DATA_000090");
			extern char dat_first[] __asm__("DATA_001698");
			extern char code_anchor[] __asm__("FUNC_001772");

			anchored = 1;
			fprintf (f, "ANCHOR DATA_000090=%lx DATA_001698=%lx "
			         "FUNC_001772=%lx\n", (unsigned long)bmp_first,
			         (unsigned long)dat_first, (unsigned long)code_anchor);
		}
	}
	/* VIEWPORT DEBUG: the VIEW DESCRIPTOR. FUNC_000957 (ffe.asm:71640-71642)
	 * stores the view id in DATA_009128 and hands DATA_009127 to the scene
	 * renderer through [DATA_007807] = FUNC_001682, so these 130 bytes are
	 * the camera the whole 3D pipeline works from. Printed as dwords: a
	 * zero rotation matrix or a zero eye position explains every projected
	 * vertex collapsing onto the view centre. */
	{
		extern unsigned long view_desc[] __asm__("DATA_009127");
		int i;

		fprintf (f, "VIEW");
		for (i = 0; i < 32; i++)
			fprintf (f, " %lx", view_desc[i]);
		fprintf (f, "\n");
	}
	/* VIEWPORT DEBUG: call the two primitives MatBuildYZT is made of, from C,
	 * with the exact arguments the game passed it (measured: angles -1 and
	 * 0x20000, so the matrix should come out near-identity). The `_`-prefixed
	 * C-ABI wrappers are the legal entry -- calling the bare bodies would
	 * clobber d2-d7/a2-a6 that this file's compiler expects preserved.
	 * Expected: SIN 7ffffff5 for both cosines, and a large product. A zero
	 * anywhere names the broken primitive, which is what leaves the player
	 * object's 3x3 all zero. */
	{
		extern long ffe_sin32 (long) __asm__("_FUNC_001914");
		extern long ffe_fmul (long, long) __asm__("_FUNC_001521");
		long cy = ffe_sin32 (-1 + 0x4000);
		long cz = ffe_sin32 (0x20000 + 0x4000);
		long sy = ffe_sin32 (-1);
		long sz = ffe_sin32 (0x20000);

		fprintf (f, "SIN cy=%lx cz=%lx sy=%lx sz=%lx t0=%lx MUL=%lx\n",
		         (unsigned long)cy, (unsigned long)cz, (unsigned long)sy,
		         (unsigned long)sz, (unsigned long)ffe_sin32 (0x3fff),
		         (unsigned long)ffe_fmul (cy, cz));
	}
	/* VIEWPORT DEBUG: THE ALIGNMENT TEST. MatBuildYZT is proven correct into
	 * a 4-aligned buffer, and proven to run exactly once on the player
	 * object -- whose address is only 2-ALIGNED (0x101f3b4e) because the
	 * game's object structs are packed x86 layout. This calls the same
	 * function twice with the game's own angles, once into a 4-aligned
	 * buffer and once into a deliberately 2-aligned one. If only the second
	 * comes back zero, misaligned longword stores are being lost -- which
	 * would explain the all-zero matrix and much else besides. */
	{
		extern void ffe_matyzt (long *, long, long) __asm__("_FUNC_001675");
		static char raw[48];
		long a4[9];
		long *a2 = (long *)(raw + 2);
		int k;

		for (k = 0; k < 9; k++) a4[k] = 0x11111111;
		for (k = 0; k < 40; k++) raw[k] = 0x22;
		ffe_matyzt (a4, -1, 0x20000);
		ffe_matyzt (a2, -1, 0x20000);
		fprintf (f, "ALIGN4 %lx %lx %lx  ALIGN2 %lx %lx %lx\n",
		         (unsigned long)a4[0], (unsigned long)a4[4],
		         (unsigned long)a4[8],
		         (unsigned long)*(long *)(raw + 2),
		         (unsigned long)*(long *)(raw + 2 + 16),
		         (unsigned long)*(long *)(raw + 2 + 32));
	}
	/* VIEWPORT DEBUG: the builder itself is PROVEN CORRECT and the test that
	 * proved it is removed on purpose -- called from C on a private buffer
	 * with the angles the game passes (-1, 0x20000) it returned
	 *     MATYZT 7fffffea 0 0 0 7ffffff5 0 0 0 7ffffff5
	 * i.e. the expected near-identity. Keeping the call here would also
	 * pollute the YZT probe below, because it goes through the same injected
	 * payload as the game's own call. */
	/* VIEWPORT DEBUG: THE PLAYER OBJECT, straight from a named symbol.
	 * FUNC_001054 (ffe.asm:77553-77558) opens with `mov esi,DATA_008804` /
	 * `mov eax,[esi+0xc8]` and then `rep movsd` 0x54 dwords + a word out of
	 * it -- so DATA_008804+0xc8 holds the pointer to the 338-byte player
	 * object. Printing its 3x3 at +0 together with class +0x87, type +0xff
	 * and model +0x82 answers the open question directly: was the object
	 * created and only its matrix left unwritten? */
	{
		extern char game_state[] __asm__("DATA_008804");
		char *ob = *(char **)(game_state + 0xc8);
		int k;

		/* The pointer is only 2-ALIGNED (measured: 0x101f327e) -- the game's
		 * object structs are packed x86 layout, so a 4-alignment guard here
		 * rejected the real object and printed nothing. The 68020 does
		 * misaligned longword accesses in hardware, so even is enough. */
		/* DATA_008861 is where FUNC_000996 stores the ship it just built
		 * (ffe.asm:74010), DATA_008862 the station it docks at, and
		 * DATA_008804+0xc8 the object FUNC_001054 updates every frame and
		 * the renderer draws. They are not the same object, which is why
		 * watching only +0xc8 kept reporting a zero matrix. */
		{
			extern char *ship_obj __asm__("DATA_008861");
			extern char *stat_obj __asm__("DATA_008862");
			char *s = ship_obj;
			int k;

			fprintf (f, "SHIP p=%lx", (unsigned long)s);
			if ((unsigned long)s > 0x1000 && !((unsigned long)s & 1)) {
				fprintf (f, " cls=%x M", (unsigned char)s[0x87]);
				for (k = 0; k < 9; k++)
					fprintf (f, " %lx", (unsigned long)*(long *)(s + k * 4));
			}
			/* and the TEMPLATE the ship's matrix is copied from, read at
			 * runtime: proves whether the storage-class fix reached the
			 * running image or the fault is in the copy that follows */
			{
				extern long ident[] __asm__("DATA_007265");

				fprintf (f, " TMPL %lx %lx %lx", (unsigned long)ident[0],
				         (unsigned long)ident[4], (unsigned long)ident[8]);
			}
			/* THE STATION the ship is landed on: FUNC_000994 (ffe.asm:3fc74)
			 * copies ITS 3x3 into the ship with `rep movsd` ecx=9, so the
			 * ship's orientation is only as good as the station's. */
			{
				extern char game_state2[] __asm__("DATA_008804");
				char *st = *(char **)(game_state2 + 0xcc);

				fprintf (f, " STN=%lx", (unsigned long)st);
				if ((unsigned long)st > 0x1000 && !((unsigned long)st & 1)) {
					fprintf (f, " cls=%x typ=%x model=%lx",
					         (unsigned char)st[0x87], (unsigned char)st[0xff],
					         (unsigned long)*(long *)(st + 0x82));
					for (k = 0; k < 9; k += 4)
						fprintf (f, " %lx",
						         (unsigned long)*(long *)(st + k * 4));
				}
			}
			fprintf (f, " stat=%lx\n", (unsigned long)stat_obj);
			/* THE OBJECT ARRAY, walked exactly as FUNC_001532 does
			 * (base = DATA_009133, slot = base + 0x74 + 338*i, ffe.asm:53a54,
			 * whose translation is verified correct by inspection). The game
			 * asks for the starport at the FIXED index 0x5f (FUNC_000998
			 * pushes it literally) and gets a blank slot, so the question is
			 * how many slots the system generation actually filled. */
			/* The RNG FUNC_000894 was held to a C reference here and the block is
			 * REMOVED: it cannot link, because the function has no standard
			 * prologue and the translator therefore emits no _-prefixed C-ABI
			 * wrapper for it. Verified instead by inspection of
			 * build\xlat-full\FUNC_000894.s, instruction for instruction
			 * against ffe.asm:3b628 -- both rotates (shl+shr+or) and both
			 * seed stores are exact, so the generator is NOT the fault. */
			/* THE SYSTEM-BODY TABLE. FUNC_000878 fills DATA_008798 (4080
			 * bytes = 60 records of 0x44) from the star-system description,
			 * and FUNC_000880 then walks it creating one object per record,
			 * stopping at the first record whose leading dword is 0
			 * (ffe.asm:3a362). Counters say 880 ran ONCE and 927 ONCE, i.e.
			 * the walk stopped after a single record -- so count how many
			 * records the builder actually produced. */
			{
				extern long sys_tbl[] __asm__("DATA_008798");
				int i, n = 0;

				fprintf (f, "TBLv5");
				for (i = 0; i < 60; i++)
					if (sys_tbl[i * 17]) {
						n++;
						if (n <= 8) fprintf (f, " %d:%lx", i,
						                     (unsigned long)sys_tbl[i * 17]);
					}
				fprintf (f, " nonzero=%d\n", n);
			}
			/* BLANK VIEWPORT (2026-08-02 evening): WATCH THE LINK GATE.
			 * Chain measured this session: the viewport shows only edges,
			 * FUNC_001613_DrawSpans never runs, its only callers are node
			 * callbacks, and the node list FUNC_001788 walks is EMPTY
			 * (head valid, *head == 0, twelve times). FUNC_001778 links a
			 * node only when arg3 != 0 || DATA_007833 == 0, and R36 measured
			 * arg3=0 with gate7833=1 on every allocation -- so the gate is
			 * what keeps the list empty.
			 * The contradiction this watches: DATA_007833 is zero in both
			 * ffedat.asm and the converted image (dc.b 0,0,0,0), its ONLY
			 * writer in the whole game is FUNC_001780 (ffe.asm:111611), and
			 * R37 proved FUNC_001780/001781/001782 are NEVER CALLED in the
			 * cockpit (their trace injections produced no line at all, while
			 * the same log filled with NODELIST entries). A flag that starts
			 * at 0, has no reachable writer, and reads 1 was written by
			 * something else -- a stray store.
			 * Latching the FIRST present where it turns non-zero brackets
			 * that store in time, which no amount of call tracing can do. */
			{
				extern long link_gate __asm__("DATA_007833");
				static long first_nonzero = -1;
				static long last_seen;

				if (first_nonzero < 0 && link_gate != 0)
					first_nonzero = (long)g_presents;
				fprintf (f, "GATE7833 addr=%lx val=%ld firstnz=%ld prev=%ld\n",
				         (unsigned long)&link_gate, link_gate,
				         first_nonzero, last_seen);
				last_seen = link_gate;
			}
			/* THE PLANET GATE (2026-08-05). FUNC_001877 is the ONLY writer of
			 * DATA_008814 = -1 (ffe.asm:131853), and FUNC_001052 reads it to
			 * decide whether to call FUNC_001775, which draws a HARDCODED
			 * object at (0,0,512) -- right at the camera, i.e. the black slabs
			 * across our viewport. The reference reaches that gate with
			 * 008814 = ffffffff and skips it; we measured 0 there while the
			 * system had no type-0x23 body to submit. The system now generates
			 * the reference's 27 bodies, eight of them 0x23, so this says
			 * whether the real planet is finally reaching FUNC_001877.
			 * 008815 (word) and 008817 (byte) are the ambient/sky colour the
			 * same function computes -- the obvious suspects for a GREEN sky
			 * where the reference paints violet. */
			{
				extern unsigned char DATA_008814[] __asm__("DATA_008814");
				extern unsigned char DATA_008815[] __asm__("DATA_008815");
				extern unsigned char DATA_008817[] __asm__("DATA_008817");

				extern long ffe_p1877_calls, ffe_p1877_noatmo, ffe_p1877_near;
				extern long ffe_p1877_shell, ffe_p1877_shade0, ffe_p1877_set;
				extern long ffe_p1877_singular, ffe_p1877_far, ffe_p1877_inside;

				{
					/* COLOUR BYTE-ORDER CENSUS -- see ffe_game.c. hi0 counts
					 * arguments whose TOP byte is zero (x86 order, red reads
					 * 0x00); lo0 counts the 68k-correct 0xRRGGBB00 shape. */
					extern long ffe_cc_calls, ffe_cc_hi0, ffe_cc_lo0;
					extern long ffe_cc_both0, ffe_cc_neither, ffe_cc_other;
					extern long ffe_cc_ret[], ffe_cc_cnt[], ffe_cc_hi0_by[];
					extern long ffe_cc_sample[];
					int q;

					fprintf (f, "COLCENSUS calls=%ld hi0(x86)=%ld lo0(68k)=%ld"
					            " both0=%ld neither=%ld other=%ld\n",
					         ffe_cc_calls, ffe_cc_hi0, ffe_cc_lo0,
					         ffe_cc_both0, ffe_cc_neither, ffe_cc_other);
					for (q = 0; q < 8; q++)
						if (ffe_cc_cnt[q])
							fprintf (f, "COLBY ret=%08lx n=%ld hi0=%ld sample=%08lx\n",
							         ffe_cc_ret[q], ffe_cc_cnt[q],
							         ffe_cc_hi0_by[q],
							         (unsigned long)ffe_cc_sample[q]);
				}
				{
					/* PALMAP (2026-09-25): the 12-bit colour codes handed to
					 * FUNC_001327_DPal16toPal, per call site -- see ffe_game.c.
					 * Printed only once the probe has fired. */
					extern long ffe_pm_calls, ffe_pm_other;
					extern long ffe_pm_ret[], ffe_pm_code[], ffe_pm_n[];
					int q;

					if (ffe_pm_calls) {
						fprintf (f, "PALMAP calls=%ld other=%ld\n",
						         ffe_pm_calls, ffe_pm_other);
						for (q = 0; q < 48; q++)
							if (ffe_pm_n[q])
								fprintf (f, "PALMAP1 ret=%08lx code=%03lx n=%ld\n",
								         ffe_pm_ret[q], ffe_pm_code[q], ffe_pm_n[q]);
					}
				}
				{
					/* TRI1808 (2026-09-25) -- see ffe_game.c; once, after the
					 * clock leaves the Mars close-up window. */
					extern long ffe_t8_n, ffe_t8_x[], ffe_t8_y[], ffe_t8_s[][6];
					static int t8_done;
					int q;

					if (ffe_t8_n && !t8_done && game_clock[0] > 1950000) {
						t8_done = 1;
						fprintf (f, "TRI1808 n=%ld x=%ld,%ld,%ld,%ld,%ld y=%ld,%ld,%ld,%ld,%ld\n",
						         ffe_t8_n, ffe_t8_x[0], ffe_t8_x[1], ffe_t8_x[2], ffe_t8_x[3],
						         ffe_t8_x[4], ffe_t8_y[0], ffe_t8_y[1], ffe_t8_y[2],
						         ffe_t8_y[3], ffe_t8_y[4]);
						for (q = 0; q < 8 && q < ffe_t8_n; q++)
							fprintf (f, "TRI1808S (%ld,%ld) (%ld,%ld) (%ld,%ld)\n",
							         ffe_t8_s[q][0], ffe_t8_s[q][1], ffe_t8_s[q][2],
							         ffe_t8_s[q][3], ffe_t8_s[q][4], ffe_t8_s[q][5]);
					}
				}
				{
					/* WINCNT (2026-09-25) -- see ffe_game.c; once. */
					extern long ffe_wc_cnt[];
					static int wc_done;
					int q;

					if (!wc_done && game_clock[0] > 800000) {
						extern long DATA_008812_v __asm__("DATA_008812");
						wc_done = 1;
						fprintf (f, "WINCNT d8812=%ld", DATA_008812_v);
						for (q = 0; q < 2200; q++)
							if (ffe_wc_cnt[q])
								fprintf (f, " %d=%ld", q, ffe_wc_cnt[q]);
						fprintf (f, "%c", 10);
					}
				}
				{
					/* BLITMAP (2026-09-25) -- see ffe_game.c. */
					extern long ffe_bm_calls, ffe_bm_other;
					extern long ffe_bm_idx[], ffe_bm_x[], ffe_bm_y[], ffe_bm_n[];
					extern unsigned long ffe_bm_hdr[][2], ffe_bm_buf[][6];
					int q;

					static int bm_tick; if (ffe_bm_calls && ((++bm_tick) & 7) == 1) {
						fprintf (f, "BLITMAP calls=%ld other=%ld\n", ffe_bm_calls, ffe_bm_other);
						for (q = 0; q < 32; q++)
							if (ffe_bm_n[q])
								fprintf (f, "BLITMAP1 idx=%ld x=%ld y=%ld n=%ld hdr=%08lx %08lx buf=%08lx %08lx %08lx %08lx %08lx %08lx\n",
								         ffe_bm_idx[q], ffe_bm_x[q], ffe_bm_y[q], ffe_bm_n[q],
								         ffe_bm_hdr[q][0], ffe_bm_hdr[q][1],
								         ffe_bm_buf[q][0], ffe_bm_buf[q][1], ffe_bm_buf[q][2],
								         ffe_bm_buf[q][3], ffe_bm_buf[q][4], ffe_bm_buf[q][5]);
					}
				}
				{
					/* RGBMAP (2026-09-25): 512-slot table, so only every
					 * 512 presents -- ffe_game.c has the probe. */
					extern long ffe_rm_calls, ffe_rm_other;
					extern long ffe_rm_ret[], ffe_rm_val[], ffe_rm_n[];
					int q;

					static int rm_tick; if (ffe_rm_calls && ((++rm_tick) & 15) == 1) {
						fprintf (f, "RGBMAP calls=%ld other=%ld\n",
						         ffe_rm_calls, ffe_rm_other);
						for (q = 0; q < 512; q++)
							if (ffe_rm_n[q])
								fprintf (f, "RGBMAP1 ret=%08lx val=%06lx n=%ld\n",
								         ffe_rm_ret[q], ffe_rm_val[q], ffe_rm_n[q]);
					}
				}
				fprintf (f, "PLANETGATE 008814=%08lx 008815=%04x 008817=%02x"
				            "  1877 calls=%ld noatmo=%ld near=%ld far=%ld"
				            " sing=%ld inside=%ld shell=%ld shade0=%ld set=%ld\n",
				         *(const unsigned long *)DATA_008814,
				         (unsigned int)*(const unsigned short *)DATA_008815,
				         (unsigned int)DATA_008817[0],
				         ffe_p1877_calls, ffe_p1877_noatmo, ffe_p1877_near,
				         ffe_p1877_far, ffe_p1877_singular, ffe_p1877_inside,
				         ffe_p1877_shell, ffe_p1877_shade0, ffe_p1877_set);
				{
					extern long ffe_p1877_farmag, ffe_p1877_faratmo;
					extern long ffe_p1877_farbands, ffe_p1877_farcol;
					extern long ffe_p1877_farE[3], ffe_p1877_farc[3];
					extern long ffe_p1877_farM[3];

					fprintf (f, "P1877FAR mag=%lx E=(%ld,%ld,%ld)"
					            " centre=(%ld,%ld,%ld) atmo=%ld bands=%ld"
					            " col=%lx  Mdiag=(%ld,%ld,%ld)\n",
					         ffe_p1877_farmag,
					         ffe_p1877_farE[0], ffe_p1877_farE[1],
					         ffe_p1877_farE[2],
					         ffe_p1877_farc[0], ffe_p1877_farc[1],
					         ffe_p1877_farc[2],
					         ffe_p1877_faratmo, ffe_p1877_farbands,
					         (unsigned long)ffe_p1877_farcol,
					         ffe_p1877_farM[0], ffe_p1877_farM[1],
					         ffe_p1877_farM[2]);
				}
				/* THE VIEW DESCRIPTOR -- the mirror of PCVIEW. FUNC_001877's
				 * `centre' is the object position in CAMERA space (the vertex
				 * transform's own output measures (0,0,0) on both machines),
				 * and ours is swung far off axis while z keeps the right
				 * magnitude: a rotation difference, not a scale one.
				 * DATA_009127 is what FUNC_000959 builds at ffe.asm:71524 --
				 * nine dwords of 3x3, then the 64-bit eye position whose low
				 * halves sit at +0x26 / +0x2e / +0x36. Its storage class is
				 * `plain', so a native dword read here sees exactly what the
				 * game's own dword reads see; the probe corrects nothing. */
				{
					extern unsigned char DATA_009127[] __asm__("DATA_009127");
					const long *m = (const long *)DATA_009127;
					int q;

					fprintf (f, "VIEW9127 m=");
					for (q = 0; q < 9; q++) fprintf (f, "%ld,", m[q]);
					fprintf (f, "  eye=(%ld,%ld,%ld)\n",
					         *(const long *)(DATA_009127 + 0x26),
					         *(const long *)(DATA_009127 + 0x2e),
					         *(const long *)(DATA_009127 + 0x36));
				}
				{
					extern long ffe_p1877_seq[6][12], ffe_p1877_n;
					long k;

					for (k = 0; k < ffe_p1877_n && k < 6; k++)
						fprintf (f, "P1877SEQ n=%ld centre=(%ld,%ld,%ld)"
						            " atmo=%ld bands=%ld col=%lx exit=%ld\n",
						         k + 1,
						         ffe_p1877_seq[k][0], ffe_p1877_seq[k][1],
						         ffe_p1877_seq[k][2], ffe_p1877_seq[k][3],
						         ffe_p1877_seq[k][4],
						         (unsigned long)ffe_p1877_seq[k][5],
						         ffe_p1877_seq[k][7]),
						fprintf (f, "P1877REC n=%ld rec0=%ld"
						            " cam=(%ld,%ld,%ld)\n",
						         k + 1, ffe_p1877_seq[k][8],
						         ffe_p1877_seq[k][9], ffe_p1877_seq[k][10],
						         ffe_p1877_seq[k][11]);
				}
			}
			/* F3 CRASH (2026-08-02): the RENDER-NODE arena pointer.
			 * The R20 trace ends inside FUNC_001690, whose write loop is
			 * bounded (34 records of 36 bytes from base+0x14) -- so if it
			 * writes wild, the BASE is wrong, not the count. FUNC_001689
			 * additionally zeroes SIX records BELOW that base (-0xc4 ..
			 * -0x10, the same 36-byte stride), so DATA_009200 is meant to
			 * point six records INTO the DATA_009251 arena, never at its
			 * start. Printing both settles it without another injection:
			 * off = base - arena must be >= 6*36 = 216, and base must be a
			 * plausible address at all. ffe.asm:108449-108451 computes it as
			 * DATA_009251 + a POINTER-read cursor dword. */
			{
				extern char *node_base __asm__("DATA_009200");
				extern char node_arena[] __asm__("DATA_009251");
				long off = (long)(node_base - node_arena);

				fprintf (f, "NODEARENA base=%lx arena=%lx off=%ld rec=%ld\n",
				         (unsigned long)node_base,
				         (unsigned long)node_arena, off, off / 36);
			}
			{
				extern char *obj_base __asm__("DATA_009133");
				char *b = obj_base;
				int i, used = 0;

				fprintf (f, "POOLvS base=%lx", (unsigned long)b);
				if ((unsigned long)b > 0x1000) {
					/* the ALLOCATION FLAGS are the truth, not the class byte:
					 * FUNC_000928 keeps one byte per slot at base+0..0x72 and
					 * hands out the first non-zero-free slot scanning DOWN
					 * from 0x72 (ffe.asm:3ccea-3cd4a), so the flag array says
					 * exactly which indices are live and in what order they
					 * were taken. The game then asks for the landing pad at
					 * the FIXED index 0x5f (FUNC_000998 pushes it literally),
					 * which only exists if ~20 objects were created. */
					for (i = 0; i <= 0x72; i++)
						if (b[i]) {
							char *o = b + 0x74 + 338 * i;

							used++;
							if (used <= 14)
								fprintf (f, " %d:%x/%x/%lx", i,
								         (unsigned char)b[i],
								         (unsigned char)o[0x87],
								         (unsigned long)*(long *)(o + 0x82));
						}
					fprintf (f, " used=%d", used);
					/* THE SECOND ARENA. FUNC_000880 is called from two places
					 * with two different bases: ffe.asm:67433 passes
					 * [DATA_009133] and 72339 passes DATA_008801. If the seven
					 * system objects went into the other arena, the landing-pad
					 * lookup (which uses DATA_009133) is looking in the wrong
					 * place -- so count both. */
					{
						extern char arena2[] __asm__("DATA_008801");
						int j, u2 = 0;

						for (j = 0; j <= 0x72; j++)
							if (arena2[j]) {
								u2++;
								if (u2 <= 10)
									fprintf (f, " B%d:%x", j,
									         (unsigned char)arena2[j]);
							}
						fprintf (f, " usedB=%d", u2);
					}
					{ extern long ffe_live[], ffe_live_base[];
						{ extern long ffe_alloc_base[], ffe_alloc_idx[], ffe_alloc_n; int q; fprintf (f, " ALLOC n=%ld", ffe_alloc_n); for (q = 0; q < 4 && q < (int)ffe_alloc_n; q++) fprintf (f, " %lx/%ld", (unsigned long)ffe_alloc_base[q], ffe_alloc_idx[q]); }

						fprintf (f, " LIVE 880=%ld 927=%ld 996=%ld 998=%ld "
						         "994=%ld B %lx %lx %lx", ffe_live[0] - 1, ffe_live[1] - 1,
						         ffe_live[2] - 1, ffe_live[3] - 1,
						         ffe_live[4] - 1, (unsigned long)ffe_live_base[0], (unsigned long)ffe_live_base[1], (unsigned long)ffe_live_base[2]);
					}
				}
				fprintf (f, "\n");
			}
		}
		fprintf (f, "PLAYER p=%lx", (unsigned long)ob);
		if ((unsigned long)ob > 0x1000 && !((unsigned long)ob & 1)) {
			fprintf (f, " cls=%x typ=%x model=%lx", (unsigned char)ob[0x87],
			         (unsigned char)ob[0xff],
			         (unsigned long)*(long *)(ob + 0x82));
			fprintf (f, " M");
			for (k = 0; k < 9; k++)
				fprintf (f, " %lx", (unsigned long)*(long *)(ob + k * 4));
		}
		fprintf (f, "\n");
	}
	/* VIEWPORT DEBUG: the object MatBuildYZT actually wrote to, and whether
	 * its matrix survived. Same address as PLAYER above == the pointer is
	 * stable and something zeroed the matrix; a different address == the
	 * player moved to a slot that never got one. */
	{
		extern long ffe_yzt_obj;
		char *y = (char *)ffe_yzt_obj;
		int k;

		/* the v-tag proves WHICH build is running: SMB mtimes on I: lie, and
		 * two probe rounds were wasted reading a stale binary's output */
		fprintf (f, "YZTv3 at=%lx", (unsigned long)y);
		if ((unsigned long)y > 0x1000 && !((unsigned long)y & 1))
			for (k = 0; k < 9; k++)
				fprintf (f, " %lx", (unsigned long)*(long *)(y + k * 4));
		fprintf (f, "\n");
	}
	/* VIEWPORT DEBUG: the zero-watch verdict (see zero_watch in ffe_game.c). */
	{
		extern long ffe_zero_prev, ffe_zero_at, ffe_zero_seen, ffe_zero_calls, ffe_zero_prev;

		extern long ffe_yzt_m0, ffe_yzt_checked, ffe_yzt_ret;
		extern long ffe_yzt_first, ffe_yzt_n, ffe_yzt_distinct, ffe_yzt_obj;

		{
			extern long ffe_mb_dst, ffe_mb_ay, ffe_mb_az, ffe_mb_cy,
			            ffe_mb_cz, ffe_mb_after;

			fprintf (f, "MB dst=%lx ay=%lx az=%lx cy=%lx cz=%lx after=%lx\n",
			         (unsigned long)ffe_mb_dst, (unsigned long)ffe_mb_ay,
			         (unsigned long)ffe_mb_az, (unsigned long)ffe_mb_cy,
			         (unsigned long)ffe_mb_cz, (unsigned long)ffe_mb_after);
		}
		{
			extern long ffe_len_last, ffe_len_max, ffe_len_n;

			fprintf (f, "LEN last=%ld max=%ld n=%ld\n", ffe_len_last,
			         ffe_len_max, ffe_len_n);
		}
		fprintf (f, "YZTN n=%ld distinct=%ld first=%lx last=%lx\n",
		         ffe_yzt_n, ffe_yzt_distinct, (unsigned long)ffe_yzt_first,
		         (unsigned long)ffe_yzt_obj);

		fprintf (f, "ZW prev=%ld at=%ld seen=%ld calls=%ld m0next=%lx chk=%ld ret=%lx\n",
		         ffe_zero_prev, ffe_zero_at, ffe_zero_seen, ffe_zero_calls,
		         (unsigned long)ffe_yzt_m0, ffe_yzt_checked,
		         (unsigned long)ffe_yzt_ret);
	}
	/* VIEWPORT DEBUG: the game-object dump (FfeObjLog). */
	{
		extern long ffe_ob_buf[][16];
		extern long ffe_ob_n;
		int i, k;

		for (i = 0; i < (int)ffe_ob_n; i++) {
			fprintf (f, "OB %ld at=%lx cls=%lx typ=%lx model=%lx pos=%ld,%ld M",
			         ffe_ob_buf[i][0], (unsigned long)ffe_ob_buf[i][1],
			         (unsigned long)ffe_ob_buf[i][11],
			         (unsigned long)ffe_ob_buf[i][12],
			         (unsigned long)ffe_ob_buf[i][13],
			         ffe_ob_buf[i][14], ffe_ob_buf[i][15]);
			for (k = 0; k < 9; k++)
				fprintf (f, " %lx", (unsigned long)ffe_ob_buf[i][2 + k]);
			fprintf (f, "\n");
		}
		ffe_ob_n = 0;
	}
	/* VIEWPORT DEBUG: the model vertex tables (FfeVtxTabLog). */
	{
		extern long ffe_vt_buf[][24];
		extern long ffe_vt_n;
		int i, k;

		for (i = 0; i < (int)ffe_vt_n; i++) {
			fprintf (f, "VT %ld idx=%lx t154=%lx [%lx %lx] exp=%ld",
			         ffe_vt_buf[i][0], (unsigned long)ffe_vt_buf[i][1],
			         (unsigned long)ffe_vt_buf[i][3],
			         (unsigned long)ffe_vt_buf[i][4],
			         (unsigned long)ffe_vt_buf[i][5], ffe_vt_buf[i][7]);
			fprintf (f, " M");
			for (k = 0; k < 9; k++)
				fprintf (f, " %lx", (unsigned long)ffe_vt_buf[i][8 + k]);
			fprintf (f, " P %ld %ld %ld obj=%lx SRC %lx %lx %lx\n",
			         ffe_vt_buf[i][17], ffe_vt_buf[i][18], ffe_vt_buf[i][19],
			         (unsigned long)ffe_vt_buf[i][2],
			         (unsigned long)ffe_vt_buf[i][20],
			         (unsigned long)ffe_vt_buf[i][21],
			         (unsigned long)ffe_vt_buf[i][22]);
		}
		ffe_vt_n = 0;
	}
	/* VIEWPORT DEBUG: the render-state sampler (FfeStateLog). */
	{
		extern long ffe_st_buf[][8];
		extern long ffe_st_n;
		int i;

		for (i = 0; i < (int)ffe_st_n; i++)
			fprintf (f, "ST %ld shift=%ld exp=%ld pos=%ld,%ld,%ld "
			         "d=%ld vtx=%lx\n",
			         ffe_st_buf[i][0], ffe_st_buf[i][1], ffe_st_buf[i][2],
			         ffe_st_buf[i][3], ffe_st_buf[i][4], ffe_st_buf[i][5],
			         ffe_st_buf[i][6], ffe_st_buf[i][7]);
		ffe_st_n = 0;
	}
	/* VIEWPORT DEBUG: the TRANSFORMED-VERTEX CACHE. DATA_009200 holds a
	 * pointer to it and FUNC_001699 indexes it as idx*36 (`lea eax,[eax+eax*8]`
	 * then `shl 2`, ffe.asm:103198-103206), reading x/y/z at +4/+8/+0xc and the
	 * owning object at +0x14. If these are zero the TRANSFORM is broken; if
	 * they are sane the collapse is in the projection below it. */
	{
		extern long *vtx_cache_p __asm__("DATA_009200");
		long *v = vtx_cache_p;
		int e, i;

		fprintf (f, "VTX base=%lx", (unsigned long)v);
		if ((unsigned long)v > 0x1000 && !((unsigned long)v & 3))
			for (e = 0; e < 3; e++) {
				fprintf (f, " |");
				for (i = 0; i < 9; i++)
					fprintf (f, " %lx", v[e * 9 + i]);
			}
		fprintf (f, "\n");
	}
	/* VIEWPORT DEBUG: the argument sampler (FfeArgLog). arg0 of DrawParticle
	 * is a P2D* -- printed both as the pointer and as the two shorts it
	 * points at, because "the coordinates are garbage" and "the pointer is
	 * garbage" are different bugs. */
	{
		extern long ffe_arg_buf[][12];
		extern long ffe_arg_n;
		int i;

		for (i = 0; i < (int)ffe_arg_n; i++)
			fprintf (f, "ARG %ld a=%lx b=%lx c=%ld "
			         "[a]=%ld,%ld,%ld,%ld [b]=%ld,%ld,%ld,%ld\n",
			         ffe_arg_buf[i][0], (unsigned long)ffe_arg_buf[i][1],
			         (unsigned long)ffe_arg_buf[i][2], ffe_arg_buf[i][3],
			         ffe_arg_buf[i][4], ffe_arg_buf[i][5],
			         ffe_arg_buf[i][6], ffe_arg_buf[i][7],
			         ffe_arg_buf[i][8], ffe_arg_buf[i][9],
			         ffe_arg_buf[i][10], ffe_arg_buf[i][11]);
		ffe_arg_n = 0;
	}
	fclose (f);
	(void)accum_9157; (void)tm_last; (void)tm_minframe; (void)tm_next50;
}

/* FIRST-LIGHT DEBUG: a platform-call trace. Every instrumented entry point
 * appends one line; the LAST line before a silent death says between which
 * two platform calls the translated code died. Capped hard. */
/* QUIET MODE (2026-09-25): PROGDIR:ffe_quiet present -> no platform-call
 * trace, no periodic frame/clock dumps, no halo timeline. Built to test the
 * hypothesis that the intro's "hangs" are multi-second DOS stalls caused by
 * our own growing log files on the host directory (the watchdog caught the
 * game task waiting on SIGF_DOS inside fopen/vfprintf, and the run later
 * RESUMED). Read once, lazily. */
int ffe_quiet = -1;
int ffe_is_quiet (void)
{
	if (ffe_quiet < 0) {
		/* QUIET BY DEFAULT: the measured freezes went away with the logs
		 * off (4/4 runs clean vs 4/4 stalled), so a normal run must not
		 * write them. PROGDIR:ffe_verbose turns them back on for probing. */
		FILE *q = fopen ("PROGDIR:ffe_verbose", "r");
		ffe_quiet = (q == NULL);
		if (q) fclose (q);
	}
	return ffe_quiet;
}

void ffe_dbg_trace (const char *tag)
{
	static int n;
	FILE *f;

	if (n >= 20000 || ffe_is_quiet ()) return;
	f = ffe_logopen ("PROGDIR:ffe_trace.log", "a");
	if (f == NULL) return;
	fprintf (f, "%d %s %lu\n", n, tag, (unsigned long)TimerGetTimeStamp ());
	fclose (f);
	n++;
}

static void dump_frame (void)
{
	FILE *f = ffe_logopen ("PROGDIR:ffe_frame.raw", "wb");
	UCHAR pal[3];
	int i;

	if (f == NULL) return;
	fwrite ("FFEFRM01", 1, 8, f);
	for (i = 0; i < 256; i++) {
		VideoGetPalValue (i, pal);
		fwrite (pal, 1, 3, f);
	}
	fwrite (g_chunky, 1, (size_t)FFE_SCREEN_W * FFE_SCREEN_H, f);
	fclose (f);
}

/* FPS OVERLAY (2026-09-26, developer's request): frames per second in the
 * top-left corner in a 3x5 font, averaged over >= 1 s of amigagfx_millis()
 * (DateStamp, 20 ms granular -- fine over a whole second). Drawn into the
 * chunky buffer just before the c2p, so it costs a few hundred byte writes
 * per frame. Colours are the brightest and darkest entries of the STATIC
 * palette half (0..127, identical to the PC's), searched once per update.
 * On by default; PROGDIR:ffe_nofps switches it off (frame dumps for pixel
 * comparisons against the PC reference see the overlay otherwise). */
static const UCHAR fps_font[14][5] = {
	{7,5,5,5,7}, {2,6,2,2,7}, {7,1,7,4,7}, {7,1,7,1,7}, {5,5,7,1,1},
	{7,4,7,1,7}, {7,4,7,5,7}, {7,1,1,1,1}, {7,5,7,5,7}, {7,5,7,1,7},
	{0,0,0,0,2},	/* 10 '.' */
	{7,4,6,4,4},	/* 11 'F' */
	{6,5,6,4,4},	/* 12 'P' */
	{3,4,2,1,6}	/* 13 'S' */
};
long ffe_last_fps10 = -1;	/* for the PARKED bar text (ffe_watchdog.c) */
static void fps_overlay (void)
{
	static int state = -1;		/* -1 unknown, 0 off, 1 on */
	static unsigned long t0, frames;
	static long fps10 = -1;
	static UCHAR fg = 1, bg = 0;
	UCHAR txt[12];
	int n = 0, i, r, c;
	unsigned long now;

	if (state < 0) {
		FILE *f = fopen ("PROGDIR:ffe_nofps", "r");
		state = f == NULL;
		if (f != NULL) fclose (f);
		t0 = amigagfx_millis ();
	}
	if (!state) return;
	++frames;
	now = amigagfx_millis ();
	if (now - t0 >= 1000) {
		long best = -1, worst = 1 << 30;
		fps10 = (long)((frames * 10000UL) / (now - t0));
		ffe_last_fps10 = fps10;
		frames = 0;
		t0 = now;
		for (i = 0; i < 128; i++) {
			long v = g_pal[3 * i] + g_pal[3 * i + 1] + g_pal[3 * i + 2];
			if (v > best) { best = v; fg = (UCHAR)i; }
			if (v < worst) { worst = v; bg = (UCHAR)i; }
		}
	}
	if (fps10 < 0) return;
	{
		/* with the screen bar the FPS goes there, next to the title, and the
		 * picture stays clean (developer, 2026-10-02) */
		extern int ffe_bar_on;
		static long shown = -2;
		if (ffe_bar_on) {
			if (fps10 != shown) {
				char t[48];
				snprintf (t, sizeof t, "AmiFFE 0.1.0      %ld.%ld FPS",
				          fps10 / 10, fps10 % 10);
				amigagfx_set_bar_text (t);
				shown = fps10;
			}
			return;
		}
	}
	{
		long v = fps10 > 9999 ? 9999 : fps10;
		UCHAR d[4];
		int k = 0;
		long ip = v / 10;
		do { d[k++] = (UCHAR)(ip % 10); ip /= 10; } while (ip && k < 3);
		while (k) txt[n++] = d[--k];
		txt[n++] = 10;
		txt[n++] = (UCHAR)(v % 10);
		txt[n++] = 0xff;	/* space */
		txt[n++] = 11; txt[n++] = 12; txt[n++] = 13;
	}
	/* background box 1 px around the text, then the glyphs */
	for (r = 0; r < 7; r++) {
		UCHAR *row = g_chunky + r * g_pitch;
		for (c = 0; c < n * 4 + 1; c++) row[c] = bg;
	}
	for (i = 0; i < n; i++) {
		if (txt[i] == 0xff) continue;
		for (r = 0; r < 5; r++) {
			UCHAR bits = fps_font[txt[i]][r];
			UCHAR *p = g_chunky + (r + 1) * g_pitch + 1 + i * 4;
			if (bits & 4) p[0] = fg;
			if (bits & 2) p[1] = fg;
			if (bits & 1) p[2] = fg;
		}
	}
}

void VideoBlit (UCHAR *pData, long x, long y, long w, long h, long jump)
{
	UCHAR *pBuf;

	if (g_chunky == NULL) return;

	pBuf = g_chunky + x + y * g_pitch;
	for (; h; --h, pData += jump + w, pBuf += g_pitch)
		memcpy (pBuf, pData, (size_t)w);

	if (w != FFE_SCREEN_W) return;		/* One flip per frame */

	/* Full-width blit == present. On AGA this is where the Kalms c2p runs, so
	 * it is the single most expensive call in the layer; the game issues it
	 * exactly once per frame. */
	fps_overlay ();
	amigagfx_blit (0, 0, FFE_SCREEN_W, FFE_SCREEN_H);

	++g_presents;
	{
		/* soft restart request from the watchdog (ffe_watchdog.c) */
		extern volatile int ffe_quit_req;
		if (ffe_quit_req) { extern void ffe_boot_mark (const char *); extern void ffe_park (void); ffe_boot_mark ("park request seen in VideoBlit"); ffe_park (); }
	}
	{
		/* BOOT MARKS: the first frames and a few milestones (ffe_watchdog.c) */
		extern void ffe_boot_mark (const char *);
		static int game_marked;
		if (g_presents == 1) ffe_boot_mark ("present 1 (splash)");
		else if (g_presents == 2) ffe_boot_mark ("present 2");
		else if (g_presents == 100) ffe_boot_mark ("present 100");
		else if (g_presents == 500) ffe_boot_mark ("present 500");
		if (!game_marked && (game_clock[0] & 0x80000000UL)) { game_marked = 1; ffe_boot_mark ("first GAME frame (cockpit)"); }
	}
	if (g_presents == 500) {
		/* WATCHDOG POSITIVE CONTROL (2026-09-25): with PROGDIR:ffe_stall
		 * present, block here forever the way the intermittent freeze does,
		 * so ffe_watchdog.c must produce its log. */
		FILE *st = fopen ("PROGDIR:ffe_stall", "r");
		if (st != NULL) {
			extern unsigned long ffe_wait_forever (void);
			fclose (st);
			ffe_wait_forever ();
		}
	}
	{
		extern long ffe_samp_arm_at;
		extern short ffe_samp_armed_v __asm__("_ffe_samp_armed");
		if (ffe_samp_arm_at > 0 && (long)g_presents == ffe_samp_arm_at)
			ffe_samp_armed_v = 1;
	}
	{
		/* FRAME1808 probe state machine (ffe_game.c) + its printer */
		extern void ffe_frame1808_present (long clk);
		extern long ffe_f8_n, ffe_f8_state;
		extern short ffe_f8[][9];
		static int f8_printed;

		ffe_frame1808_present (game_clock[0]);
		if (ffe_f8_state == 3 && !f8_printed) {
			FILE *pf = ffe_logopen ("PROGDIR:ffe_frame1808.log", "w");
			int q;

			f8_printed = 1;
			if (pf != NULL) {
				fprintf (pf, "FRAME1808 n=%ld clk=%ld\n", ffe_f8_n, game_clock[0]);
				for (q = 0; q < ffe_f8_n; q++)
					fprintf (pf, "T %d %d %d %d %d %d z %d %d %d\n",
					         ffe_f8[q][0], ffe_f8[q][1], ffe_f8[q][2], ffe_f8[q][3],
					         ffe_f8[q][4], ffe_f8[q][5], ffe_f8[q][6], ffe_f8[q][7],
					         ffe_f8[q][8]);
				{
					/* FRAMECNT (2026-09-26), twin of PCFRAMECNT */
					extern long ffe_fc_cnt[];
					fprintf (pf, "FRAMECNT clk=%ld", game_clock[0]);
					for (q = 0; q < 2200; q++)
						if (ffe_fc_cnt[q])
							fprintf (pf, " %d=%ld", q, ffe_fc_cnt[q]);
					fprintf (pf, "%c", 10);
				}
				{
					extern long ffe_mv_n, ffe_mv_code[], ffe_mv_r[], ffe_mv_mdl[];
					for (q = 0; q < ffe_mv_n; q++)
						fprintf (pf, "MV1473 q=%d code=%02lx r=%lx mdl=%lx%c", q,
						         (unsigned long)(ffe_mv_code[q] & 0xff), (unsigned long)ffe_mv_r[q],
						         (unsigned long)ffe_mv_mdl[q], 10);
				}
				fclose (pf);
			}
			{
				/* ITEMLOG (2026-09-26), twin of pc_items.log */
				extern short ffe_it[][11];
				extern long ffe_it_n;
				FILE *itf = ffe_logopen ("PROGDIR:ffe_items.log", "w");
				if (itf != NULL) {
					long r;
					int k;
					fprintf (itf, "ITEMS n=%ld%c", ffe_it_n, 10);
					for (r = 0; r < ffe_it_n; r++) {
						fprintf (itf, "I %d", ffe_it[r][0]);
						for (k = 1; k < 11; k++) fprintf (itf, " %d", ffe_it[r][k]);
						fprintf (itf, "%c", 10);
					}
					fclose (itf);
				}
			}
			{
				extern unsigned char ffe_v8_d[][6];
				extern long ffe_v8_r[][5], ffe_v8_n, ffe_v8_o[][12];
				extern long ffe_v8_k[];
				extern void ffe_frame1478_flush (void);
				FILE *vf;

				ffe_frame1478_flush ();
				vf = ffe_logopen ("PROGDIR:ffe_frame1478.log", "w");
				if (vf != NULL) {
					int r;
					for (r = 0; r < ffe_v8_n; r++)
						fprintf (vf, "V %02x%02x%02x%02x%02x%02x s=%ld,%ld p=%ld,%ld,%ld o=%ld,%ld,%ld m=%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld,%ld k=%ld\n",
						         ffe_v8_d[r][0], ffe_v8_d[r][1], ffe_v8_d[r][2],
						         ffe_v8_d[r][3], ffe_v8_d[r][4], ffe_v8_d[r][5],
						         ffe_v8_r[r][0], ffe_v8_r[r][1], ffe_v8_r[r][2],
						         ffe_v8_r[r][3], ffe_v8_r[r][4], ffe_v8_o[r][0],
						         ffe_v8_o[r][1], ffe_v8_o[r][2], ffe_v8_o[r][3],
						         ffe_v8_o[r][4], ffe_v8_o[r][5], ffe_v8_o[r][6],
						         ffe_v8_o[r][7], ffe_v8_o[r][8], ffe_v8_o[r][9],
						         ffe_v8_o[r][10], ffe_v8_o[r][11], ffe_v8_k[r]);
					fclose (vf);
				}
			}
			{
				extern short ffe_fh[][15];
				extern long ffe_fh_n;
				FILE *hf = ffe_logopen ("PROGDIR:ffe_framehalo.log", "w");

				if (hf != NULL) {
					int r, k;
					for (r = 0; r < ffe_fh_n; r++) {
						fprintf (hf, "H %d", ffe_fh[r][0]);
						for (k = 1; k < 15; k++) fprintf (hf, " %d", ffe_fh[r][k]);
						fprintf (hf, "\n");
					}
					fclose (hf);
				}
			}
			{
				extern long ffe_c7[][13], ffe_c7_n;
				extern unsigned long ffe_c7_dest[];
				FILE *cf = ffe_logopen ("PROGDIR:ffe_clip1837.log", "w");

				if (cf != NULL) {
					for (q = 0; q < ffe_c7_n; q++) {
						const short *d = (const short *)ffe_c7_dest[q];
						fprintf (cf, "C A=%ld,%ld,%ld z=%ld B=%ld,%ld,%ld d=%ld,%ld,%ld z=%ld out=%d,%d\n",
						         ffe_c7[q][0], ffe_c7[q][1], ffe_c7[q][2], ffe_c7[q][3],
						         ffe_c7[q][4], ffe_c7[q][5], ffe_c7[q][6], ffe_c7[q][7],
						         ffe_c7[q][8], ffe_c7[q][9], ffe_c7[q][10], d[0], d[1]);
					}
					fclose (cf);
				}
			}
		}
	}
	/* EVERY 32, not 128, and this cost a whole diagnosis on 2026-08-06.
	 *
	 * A run that ends on the renderer's TRAP 5 lands somewhere between 160 and
	 * 350 presents, and how far it gets varies with host load -- the MILESTONE
	 * binary reached 769 the day it was made and 353 today, on the same
	 * harness. At a period of 128 the last dump of a 193-present run is the one
	 * taken at present 129, which is before the game reaches the cockpit at
	 * all. So ffe_frame.raw came back BLACK and read exactly like "the build
	 * stopped rendering" -- a regression hunt that ran through a reverted
	 * translator rule, a stripped probe set and a control run of the milestone
	 * binary before the cadence turned out to be the whole of it.
	 *
	 * 64 KB through the emulated filesystem four times a run is a price worth
	 * paying to never be told that again. Compare frames, not luck. */
	if ((g_presents & 31) == 1 && !ffe_is_quiet ()) dump_frame ();
	/* SEQUENCE MODE (2026-09-25): if PROGDIR:ffe_seq exists and holds a
	 * number n, every n-th present is ALSO written to PROGDIR:seq/fNNNNN.raw
	 * (NNNNN = present number), the same scheme as the PC reference's
	 * FFE_DUMP_SEQ, so the intro can be compared scene by scene. Read once. */
	{
		static long seq = -1;

		if (seq < 0) {
			FILE *s = fopen ("PROGDIR:ffe_seq", "r");

			seq = 0;
			if (s != NULL) {
				if (fscanf (s, "%ld", &seq) != 1) seq = 0;
				fclose (s);
			}
		}
		/* CLOCK WINDOW (2026-10-16): PROGDIR:ffe_seqwin "lo hi" also writes EVERY
		 * present whose unsigned game clock is in [lo,hi] -- twin of FFE_SEQWIN on
		 * the PC, so two machines can be compared at equal clock after a jump. */
		static long win_read; static unsigned long win_lo, win_hi;
		int in_win;
		if (!win_read) {
			FILE *w = fopen ("PROGDIR:ffe_seqwin", "r");
			win_read = 1;
			if (w != NULL) { if (fscanf (w, "%lu %lu", &win_lo, &win_hi) != 2) win_lo = win_hi = 0; fclose (w); }
		}
		in_win = win_hi > 0 && (unsigned long)game_clock[0] >= win_lo && (unsigned long)game_clock[0] <= win_hi;
		if ((seq > 0 && (g_presents % (unsigned long)seq) == 0 && g_presents <= 60000) || in_win) {
			char name[40];
			FILE *f;
			UCHAR pal[3];
			int i;

			snprintf (name, sizeof name, "PROGDIR:seq/f%05lu.raw", g_presents);
			f = fopen (name, "wb");
			if (f != NULL) {
				fwrite ("FFEFRM01", 1, 8, f);
				for (i = 0; i < 256; i++) {
					VideoGetPalValue (i, pal);
					fwrite (pal, 1, 3, f);
				}
				fwrite (g_chunky, 1, (size_t)FFE_SCREEN_W * FFE_SCREEN_H, f);
				fclose (f);
			}
			/* frame -> game clock index (twin of the PC probe's) */
			f = fopen ("PROGDIR:seq/index.txt", "a");
			if (f != NULL) {
				fprintf (f, "f%05lu %ld\n", g_presents, game_clock[0]);
				fclose (f);
			}
		}
	}
	/* every 32 presents, not 128: once the 3D frames start each one costs
	 * seconds, so a 128-present window can outlast the whole watch and the
	 * LJ/RET/A8/CNT measurements never print at all. */
	if ((g_presents & 31) == 1 && (!ffe_is_quiet () || (g_presents & 127) == 1)) dump_clock ();

	/* FIRST-LIGHT DEBUG: the spontaneous-splash-exit hunt. Log every CHANGE
	 * of the GUI action byte with the present number -- the VALUE names the
	 * mechanism that produced it (0x79 = 'y', 0xf5 = hot-area default, a
	 * digit = menu choice...). One line per change, so the file stays tiny. */
	{
		static UCHAR last_act = 0;

		if (gui_action != last_act) {
			/* ffe_dump.log, with the rest of the measurement output:
			 * autokey_run.ps1 reads these ACTION lines to confirm a key
			 * registered, and ffe_game.log's directory entry on the I:
			 * share is no longer reliably readable from the host. */
			FILE *f = ffe_logopen ("PROGDIR:ffe_dump.log", "a");

			if (f != NULL) {
				fprintf (f, "ACTION %02x -> %02x at present %lu ts=%lu "
				         "lastscan=%02x\n", last_act, gui_action,
				         g_presents, (unsigned long)TimerGetTimeStamp (),
				         gui_lastscan);
				fclose (f);
			}
			last_act = gui_action;
		}
	}
}

void VideoMaskedBlit (UCHAR *pData, long x, long y, long w, long h, long jump)
{
	long cw;
	long dJump;
	UCHAR *pBuf;

	if (g_chunky == NULL) return;

	dJump = g_pitch - w;
	pBuf  = g_chunky + x + y * g_pitch;
	for (; h; --h, pData += jump, pBuf += dJump)
		for (cw = w; cw; --cw, ++pData, ++pBuf)
			if (*pData) *pBuf = *pData;
}

void VideoReverseBlit (UCHAR *pData, long x, long y, long w, long h, long jump)
{
	UCHAR *pBuf;

	if (g_chunky == NULL) return;

	pBuf = g_chunky + x + y * g_pitch;
	for (; h; --h, pData += jump + w, pBuf += g_pitch)
		memcpy (pData, pBuf, (size_t)w);
}

void VideoSetPalValue (long palindex, UCHAR *pVal)
{
	UCHAR *pEntry = g_pal + palindex * 3;

	pEntry[0] = (UCHAR)(pVal[0] << 2);
	pEntry[1] = (UCHAR)(pVal[1] << 2);
	pEntry[2] = (UCHAR)(pVal[2] << 2);

	/* One register at a time. The game sets whole palettes in loops and fades
	 * entry by entry, and amiga_gfx.c's set_palette is a LoadRGB32 on a range,
	 * so a one-entry call is the cheapest thing it can be handed. */
	amigagfx_set_palette (pEntry, (int)palindex, 1);
}

void VideoGetPalValue (long palindex, UCHAR *pVal)
{
	const UCHAR *pEntry = g_pal + palindex * 3;

	pVal[0] = (UCHAR)(pEntry[0] >> 2);
	pVal[1] = (UCHAR)(pEntry[1] >> 2);
	pVal[2] = (UCHAR)(pEntry[2] >> 2);
}

long VideoPointerExclusive (void)
{
	/* 0 = the system draws the pointer, the game must NOT draw its own. We
	 * keep the Intuition pointer: it is hardware sprite 0, it costs no chip
	 * bandwidth worth counting and it keeps moving even while a frame is being
	 * converted chunky-to-planar, which a game-drawn pointer would not. */
	return 0;
}

void VideoPointerEnable (void)
{
	/* Intentionally empty for now: with VideoPointerExclusive() == 0 the
	 * Intuition pointer is always visible and the game never asks for a
	 * different one. Hiding it (ClearPointer/SetPointer) belongs with the
	 * fullscreen-grab work, which this port does not have yet. */
}

void VideoPointerDisable (void)
{
	/* Intentionally empty, see VideoPointerEnable. */
}

/* for the hang watchdog (ffe_watchdog.c) */
unsigned long ffe_get_presents (void)
{
	return g_presents;
}
