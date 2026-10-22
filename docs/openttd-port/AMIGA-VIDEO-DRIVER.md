# A native AmigaOS 68k video driver: how ours works, and how to reuse it

This describes the display and input layer written for the OpenTTD 68k port: a chunky
8bpp framebuffer in Fast RAM, dirty rectangles, Mikael Kalms' assembly chunky-to-planar,
and an Intuition custom screen on plain AGA. No RTG board, no SDL, no CyberGraphX.

It is written for a C programmer who has never touched Amiga graphics. Amiga concepts are
explained where they first matter. Everything stated as measured was measured on a
calibrated emulated 68040 (see §6 for what "calibrated" means and why it matters).

The code is five files:

| file | role |
|---|---|
| `native/openttd/amiga_gfx.h` | the entire interface between the two layers; ~80 lines, no Amiga headers |
| `native/openttd/amiga_gfx.c` | every Amiga API call in the project, plain C |
| `native/openttd/amiga_v.cpp` | the OpenTTD `VideoDriver` subclass; C++, no Amiga headers |
| `native/openttd/amiga_v.h` | driver class + factory registration |
| `native/c2p_glue.s` + `native/c2p_rect.s` | Kalms' c2p and a C-callable wrapper for it |

Plus the standalone programs that produced the design, each of which still builds and runs
on its own: `native/gfxtest.c` (screen + palette + image), `native/agabench.c` (measure the
system blit and two C c2p attempts), `native/agac2p.c` (measure Kalms' c2p), and
`native/againput.c` (IDCMP input + dirty-rect blitting under load).

---

## 1. Architecture: two layers, and why the split is load-bearing

```
  OpenTTD C++  ──►  amiga_gfx.h  ──►  amiga_gfx.c  ──►  Intuition / graphics.library
  (amiga_v.cpp)     9 functions       plain C            c2p_rect_asm (68k asm)
```

**All Amiga API calls live in one plain-C file. The C++ side includes no Amiga header at
all.** This is not tidiness, it is the difference between a port that builds and one that
does not.

Including `<proto/exec.h>` from C++ pulls in bebbo's `inline/stubs.h`, which transitively
drags in dos, intuition and graphics. Those headers define, at global scope:

- macros named `Insert`, `Remove`, `Allocate` (exec.library list and memory calls);
- types named `struct ViewPort` (graphics/view.h) and `Point`.

OpenTTD has its own `Point`, its own `ViewPort`, and `SmallMap::Insert`. The collisions are
not warnings — they are hard errors deep inside template instantiations, and the macros in
particular rewrite identifiers in files that never asked for Amiga anything. This is
precisely what killed the earlier SDL-based attempt at this port: the collisions arrived via
`network/core/os_abstraction.h`'s `__AMIGA__` branch, and chasing them with `#undef` was a
losing game (see `PROGRESS.md`, "the header collisions were NOT dedicated-vs-GUI").

The rule that follows, stated so it cannot be lost:

> If a name collision ever appears, move the code **into** the C file. Never include an
> Amiga header from C++.

`amiga_gfx.h` (`native/openttd/amiga_gfx.h:10-17`) carries this warning in its own header
comment for the same reason.

### The interface

Nine functions and one POD event struct. Nothing else crosses the boundary — no pointers to
Amiga structures, no Amiga types, only `int`, `unsigned char *` and `unsigned long`:

```c
int   amigagfx_open(int w, int h);        /* 0 = ok, 1..4 = which step failed */
void  amigagfx_close(void);
unsigned char *amigagfx_chunky(void);     /* the framebuffer, in Fast RAM */
int   amigagfx_pitch(void);               /* bytes per row (== width) */
void  amigagfx_set_palette(const unsigned char *rgb, int first, int count);
void  amigagfx_blit(int x, int y, int w, int h);   /* push one dirty rect */
unsigned long amigagfx_millis(void);      /* ms since open */
int   amigagfx_poll(AmigaGfxEvent *ev);   /* 0 when the queue is empty */
void  amigagfx_log(const char *msg);
```

`AmigaGfxEvent` is `{ int type; int x, y; int code; }` with `AMIGAGFX_EV_*` constants. Raw
Amiga key codes are passed through as integers rather than translated in C — translation is
application policy and belongs on the application side (§5).

That is a complete display backend interface. If you are porting something else, this is the
shape to aim for: the application layer never learns that Intuition exists.

---

## 2. The display pipeline, end to end

### The Amiga background you need

The Amiga's native chipset (OCS/ECS/AGA) is **planar**. A 256-colour image is not one byte
per pixel; it is 8 separate 1-bit images ("bitplanes"), where pixel *n*'s colour index is
assembled from bit *n* of each plane. Almost all software — including OpenTTD's 8bpp blitter
— produces **chunky** pixels: one byte per pixel, colour index directly. Converting one to
the other is *chunky-to-planar*, or **c2p**, and on a 68k it is the single most expensive
thing a native AGA display does.

There are two kinds of RAM. **Chip RAM** is shared with the custom chips and is the only
memory the display hardware can fetch bitplanes from. **Fast RAM** is CPU-only, much faster,
and on a 68040 it is cacheable in copyback mode. Chip RAM on an 040 is mapped
**cache-inhibited/serialized** — every write goes straight to a ~7 MHz bus that the display
DMA is simultaneously stealing slots from.

An **Intuition screen** is a display context (a resolution, a depth, a palette, a bitmap).
A **window** lives on a screen and is how you receive input events. **IDCMP** is Intuition's
message port mechanism for delivering those events.

### The pipeline

```
  OpenTTD 8bpp blitter
        │  writes chunky bytes, no conversion
        ▼
  chunky buffer, w*h bytes, Fast RAM         amigagfx_chunky()
        │  MakeDirty() collects rectangles
        ▼
  dirty rectangle list (up to 48)            amiga_v.cpp:190-194
        │  one call per rect
        ▼
  amigagfx_blit()  →  c2p_rect_asm  →  Kalms c2p_rect
        │  32 pixels at a time, longword stores
        ▼
  8 contiguous bitplanes, ONE Chip RAM block
        │  handed to Intuition once, at open
        ▼
  custom screen (SA_BitMap) — the display hardware reads it directly
```

The key property: OpenTTD's `_screen.dst_ptr` points **at our chunky buffer**
(`amiga_v.cpp:312`). The game's blitter draws into it with zero conversion, zero copies, and
no knowledge of the Amiga. Every 8bpp-rendering application has this shape; that is why the
approach transfers.

### Step 1 — the chunky buffer must be in Fast RAM

```c
g_chunky = AllocVec((ULONG)w * h, MEMF_ANY | MEMF_CLEAR);   /* amiga_gfx.c:94 */
```

`MEMF_ANY` lets exec pick, which on any machine with Fast RAM means Fast RAM. This matters:
the application writes to this buffer constantly, and the c2p reads it constantly. In Chip
RAM both would run cache-inhibited at bus speed. Judgement call, but a well-founded one —
the c2p research (`docs/research/c2p-best-practices.md` §5) is unambiguous that the chunky
buffer, and any lookup tables, belong in Fast RAM, and that you should never *read* Chip RAM
in a c2p inner loop.

### Step 2 — the bitplanes must be ONE contiguous allocation

```c
g_bpr       = ((w + 15) >> 4) << 1;        /* bytes per row, word-aligned */
g_planesize = (ULONG)g_bpr * h;
g_chip      = AllocMem(g_planesize * DEPTH, MEMF_CHIP | MEMF_CLEAR);
InitBitMap(&g_bitmap, DEPTH, w, h);
for (i = 0; i < DEPTH; i++)
    g_bitmap.Planes[i] = (PLANEPTR)(g_chip + (ULONG)i * g_planesize);
```
(`amiga_gfx.c:90-105`)

The obvious call here is `AllocBitMap()`, and it is wrong for this purpose. `AllocBitMap`
does **not** guarantee that the planes end up equally spaced in memory — it may allocate
them separately, and on a system with a graphics board it may not give you plain Chip RAM at
all. Kalms' `c2p_rect` takes a *single* `bplsize` in `d6` and walks from plane to plane by
adding it (`native/c2p_rect.s:22-25`, `lea (a1,a3.l*8),a1`). Equally spaced planes are a
hard precondition; violate it and you get scribbled memory, not an error.

So: allocate one block, build the `struct BitMap` by hand, and hand it to Intuition with
`SA_BitMap` (`amiga_gfx.c:118`). Intuition then displays *your* memory. This is also what
makes the whole thing fast — there is no intermediate buffer anywhere in the path.

`g_bpr` rounds the width up to a whole number of 16-bit words because that is what the
chipset addresses. For our widths (320, 352, 640) it is exactly `w/8`.

### Step 3 — the c2p and its constraints

`amigagfx_blit()` fills a nine-field argument block and calls the assembly:

```c
struct C2PArgs {
    UWORD x, y, w, h;
    UWORD cmod, bmod;      /* chunky stride, bitplane BytesPerRow */
    ULONG bplsize;         /* distance between planes */
    APTR  chunky;          /* BASE of chunky buffer; the routine adds x/y itself */
    APTR  bpl;             /* BASE of plane 0 */
};
```
(`amiga_gfx.c:47-53`; layout must match `native/c2p_glue.s:17-24` exactly.)

**x and width must be multiples of 32.** Kalms' routine processes 32-pixel columns and
performs no error checking — his own words are that bad parameters "will most probably
crash". The driver does not reject such rectangles, it *grows them outwards* onto the grid
and then clips to the screen:

```c
x  &= ~31;
x2  = (x2 + 31) & ~31;
if (x < 0) x = 0;                    /* after the mask, not before */
...
if (x2 > g_width)  x2 = g_width;
if (x2 <= x || y2 <= y) return;      /* degenerate or fully off-screen */
```
(`amiga_gfx.c:220-227`)

Note the ordering: the negative-x clamp comes *after* the `&= ~31`, because masking a
negative number moves it further negative. Growing rather than rejecting is deliberate —
callers should not have to know the alignment rule, and converting at most 31 extra pixel
columns is far cheaper than a missed update.

Screen width itself is rounded up to a multiple of 32 in `CreateMainSurface()`
(`amiga_v.cpp:300`). That is why the offered resolution list is 320x256, 352x272, 640x480,
640x512 (`amiga_v.cpp:48-53`) and why 368 — the other common PAL overscan width — is absent:
368 is not a multiple of 32.

### Step 4 — the assembly wrapper, and a real ABI trap

`c2p_rect` takes its nine arguments in `d0-d6/a0/a1`, i.e. in registers the C ABI treats as
callee-saved (`d2`–`d7`). The routine *does* save them itself — but only after we have
already loaded our arguments into them, so its epilogue would restore *our* values and
destroy the caller's. `native/c2p_glue.s` exists solely to save `d2-d7/a2-a6` first, load
the arguments from a struct, `jsr`, and restore:

```
_c2p_rect_asm:
        movem.l d2-d7/a2-a6,-(sp)
        move.l  48(sp),a1          ; 11 saved longs = 44, +4 return address
        ...
        jsr     c2p_rect
        movem.l (sp)+,d2-d7/a2-a6
        rts
```

Assembled with the `vasmm68k_mot` that ships with bebbo amiga-gcc, so Kalms' Motorola syntax
needs no translation:

```
vasmm68k_mot -Fhunk -m68040 -no-opt -o c2p_glue.o c2p_glue.s
```

(`c2p_glue.s` `include`s `c2p_rect.s`, so this one command produces both.)

### Step 5 — the palette

AmigaOS wants 32-bit-per-component colour values. An 8-bit component is replicated into all
four bytes, which is the correct expansion (0xFF → 0xFFFFFFFF, 0x80 → 0x80808080):

```c
table[0] = ((ULONG)count << 16) | (ULONG)first;   /* count, first pen */
for (i = 0; i < count; i++) {
    table[1 + i*3 + 0] = ((ULONG)rgb[i*3 + 0]) * 0x01010101UL;
    ...
}
table[1 + count * 3] = 0UL;                       /* terminator */
LoadRGB32(&g_screen->ViewPort, table);
```
(`amiga_gfx.c:189-206`)

On the C++ side this is driven by OpenTTD's palette-animation protocol
(`CheckPaletteAnim()`, `amiga_v.cpp:224-245`), which asks the current blitter whether the
video backend or the blitter itself should animate. For a plain 8bpp AGA screen the answer
is the video backend, and `LoadRGB32` is essentially free — no pixels are touched, which is
why palette-cycled water and vehicle lights cost nothing here.

---

## 3. Dirty rectangles

### Why not one bounding box

The first version tracked a single bounding box. That was a mistake, and an instructive one.

OpenTTD's `DrawDirtyBlocks()` already coalesces damage internally and then calls the driver's
`MakeDirty()` **once per resulting region**. Merging those regions into one box therefore
re-does work the application had already done for us, and converts everything in between. A
frame that genuinely changed three small windows was costing a 640x209 blit — **over ten
times the necessary pixels** — and at a calibrated 040 speed it was visible as sluggishness,
not as a microbenchmark difference.

The general lesson transfers to any port: *check whether your application already coalesces
damage before you coalesce it again.*

### The list

```c
#define MAX_DIRTY_RECTS 48
struct DirtyRect { int left, top, right, bottom; };
static DirtyRect _dirty[MAX_DIRTY_RECTS];
```
(`amiga_v.cpp:170-194`)

`MakeDirty()` (`amiga_v.cpp:455-503`) does three things:

1. **Always** extends the bounding box, regardless of mode. It is the overflow fallback and
   the statistics baseline, so it must be maintained unconditionally.
2. **Merge on touch.** If the new rectangle intersects or abuts an existing one, they merge.
   The test is `!(a->left > right || a->right < left || ...)` — note it treats *touching*
   rectangles as intersecting, which is what you want: two adjacent rects cost the same as
   their union but with more call overhead. After a merge, the enlarged rectangle is retested
   against all later entries, because one merge can bring a third rectangle into contact with
   the result. Merged-away entries are removed by swapping in the last element.
3. **Overflow.** At 48 rectangles the frame is too fragmented to track individually; set
   `_dirty_overflow` and let the whole frame fall back to the bounding box. Updates are never
   dropped — that is the important part. Dropping an update leaves visible garbage on screen
   that nothing will ever repaint.

`DrawSurfaceToScreen()` (`amiga_v.cpp:266-294`) then issues one `amigagfx_blit()` per
rectangle, or one for the bounding box in overflow/forced mode, and resets the list.

### Measuring it instead of asserting it

Both paths are compiled into the same binary, and the old behaviour is selectable at runtime
with `-v amiga:bbox` (`amiga_v.cpp:176`, `415`). This is deliberate methodology: an
optimisation evaluated by comparing two different builds is confounded with everything else
that changed in those builds. One binary, one flag, two runs.

The driver also counts pixels actually converted against pixels a bounding box would have
cost, and logs the ratio every 200 frames (`ReportBlitStats()`, `amiga_v.cpp:251-264`):

```
1234 frames: 8123 kpx converted vs 24010 kpx for one bbox (66% saved)
```

**Measured: 60–70% fewer pixels converted on a typical frame.** Since c2p cost is very close
to linear in area (§6), that is a direct 60–70% cut in the frame's blit time.

---

## 4. Screen mode selection

The display mode is not implied by the width and height — you must ask for a **display ID**,
a 32-bit value combining a monitor and a mode key. The driver builds one from the requested
size (`amiga_gfx.c:111-127`):

```c
ULONG modeid = PAL_MONITOR_ID;
modeid |= (w > 400) ? HIRES_KEY : LORES_KEY;
if (h > 300) modeid |= 0x0004;          /* LACE bit */
```

The reasoning: a 320-wide screen is a lores mode; anything at or below roughly 256 lines fits
a PAL frame without interlace, and non-interlaced is the entire point of wanting a lores mode
(no flicker). 640x480 and 640x512 are hires interlaced.

### The fallback is dangerous, and is logged as such

If `OpenScreenTags` fails with `SA_DisplayID`, the driver retries with the tag omitted
(`amiga_gfx.c:128-141`). That succeeds far more often — but it hands mode selection to the
system, which gives you whatever Workbench happens to be running. A lores-sized screen then
silently comes up in a hires mode. It looks *almost* right, and a log line that prints only
`320x256` cannot tell you it happened.

Hence:

```c
ULONG got = GetVPModeID(&g_screen->ViewPort);
fprintf(stdout, "amiga: modeid wanted $%08lx got $%08lx  %s%s  [%s]\n",
        g_want_modeid, got,
        (got & HIRES_KEY) ? "HIRES " : "LORES ",
        (got & 0x0004)    ? "INTERLACED" : "non-interlaced",
        g_used_fallback ? "SYSTEM FALLBACK - our mode was refused"
                        : "our mode accepted");
```
(`amiga_gfx.c:143-157`)

**Log the mode you got, never the mode you asked for.** Intuition substitutes silently, and
this applies to any Amiga display code, not just ours.

### Other screen tags

`SA_Quiet` and `SA_ShowTitle FALSE` suppress the title bar and screen furniture — the
application owns every pixel. `SA_Type CUSTOMSCREEN` means our own screen, not a shared
Workbench one.

### Changing resolution at runtime

`ChangeResolution()` (`amiga_v.cpp:595-612`) deliberately does **not** reopen the screen. It
records the choice, returns `true` so the application persists it, and applies it on the next
run. This is a judgement call specific to a GUI application: every already-open window keeps
its old position and size, so going 640 → 320 leaves a single window covering the display,
and the font tables were built once at startup and cannot follow. Doing it properly means
re-laying-out the whole interface. If your application has no persistent window layout, you
can simply call `amigagfx_close()` + `amigagfx_open()` and repoint the framebuffer — that
path already works, it is what `CreateMainSurface()` does.

`ToggleFullscreen()` returns `fullscreen` unchanged: an Intuition custom screen is always
fullscreen, and there is no windowed mode to switch to.

---

## 5. Input via IDCMP

### The window exists only for events

A screen cannot receive input; a window can. So the driver opens a window that covers the
whole screen and is invisible (`amiga_gfx.c:159-168`):

```c
WFLG_BACKDROP | WFLG_BORDERLESS | WFLG_ACTIVATE |
WFLG_REPORTMOUSE | WFLG_RMBTRAP | WFLG_NOCAREREFRESH
```

- `WFLG_BACKDROP` + `WFLG_BORDERLESS` — no frame, no gadgets, sits behind everything; the
  application's own pixels are what you see.
- `WFLG_REPORTMOUSE` — deliver `IDCMP_MOUSEMOVE` continuously, not only on clicks.
- **`WFLG_RMBTRAP`** — without this, Intuition eats the right mouse button for its menu
  system and your application never sees it. Any Amiga program that wants the right button
  for itself must set this.
- `WFLG_NOCAREREFRESH` — we repaint from the chunky buffer, so we do not want refresh
  messages.

`WFLG_ACTIVATE` alone did **not** reliably give a backdrop window keyboard focus — during
bring-up `native/againput.c` recorded 1357 mouse moves and zero key events, even when the
user typed by hand. An explicit `ActivateWindow(g_window)` after opening fixed it
(`amiga_gfx.c:173`). Do both.

### IDCMP_INTUITICKS is not optional

```c
WA_IDCMP, IDCMP_MOUSEMOVE | IDCMP_MOUSEBUTTONS | IDCMP_RAWKEY | IDCMP_INTUITICKS
```

`IDCMP_INTUITICKS` delivers a message roughly 10 times a second regardless of input. It is a
heartbeat. Without it, an event loop built on `WaitPort()` blocks **forever** when no input
arrives — which means an unattended or automated run can never time out, never log, and never
exit. It looks exactly like a hang. Our OpenTTD main loop polls rather than waits, so it does
not strictly need the heartbeat, but `native/againput.c` did and the flag stays for anything
that reuses this code with a blocking loop.

### Polling

`amigagfx_poll()` (`amiga_gfx.c:248-295`) pops at most one `IntuiMessage`, copies out the
four fields it needs, and calls `ReplyMsg()` **immediately** — Amiga message ports lend you
the message; you must not hold it while doing work. Returns 0 when the queue is empty, so the
caller drains with `while (amigagfx_poll(&ev)) { ... }`.

### Raw key codes, and why releases matter

`IDCMP_RAWKEY` gives positional, layout-independent scan codes, with **bit 7 set on release**.
The C layer forwards **every** code, presses and releases alike, and translates nothing. The
application side needs the releases to track modifiers:

```c
switch (ev.code & 0x7f) {
    case 0x60: case 0x61: _amiga_shift = ((ev.code & 0x80) == 0); break;  /* shift */
    case 0x63:            _amiga_ctrl  = ((ev.code & 0x80) == 0); break;  /* ctrl  */
    default:
        if ((ev.code & 0x80) == 0) HandleAmigaKey(ev.code & 0x7f);
        break;
}
```
(`amiga_v.cpp:391-401`)

Translation lives in a table on the C++ side (`amiga_v.cpp:69-105`) mapping raw code →
application key code + unshifted/shifted ASCII, written against the physical Amiga keyboard.
Keep it there: it is application policy, and it keeps the C layer free of any dependency on
the application's key constants.

One more thing worth stating: ESC is *not* hardwired to quit. During bring-up it was, which
made the keyboard look like it worked when nothing else did. Quitting belongs in the
application's menu; ESC belongs to the application.

### Intuition cannot warp the pointer

This is the constraint that most affects a game. Drag-scrolling (hold a button, move the
mouse, the view pans) is normally implemented by pinning the pointer and reading deltas —
SDL does it with a warp. **Intuition has no pointer-warp call.** So the driver tracks the
physical position itself and reports deltas against it, leaving the application's logical
cursor anchored (`amiga_v.cpp:339-357`):

```c
if (_cursor.fix_at) {                       /* app wants the cursor pinned */
    _cursor.delta.x = ev.x - _phys_x;       /* delta vs PHYSICAL position */
    _cursor.delta.y = ev.y - _phys_y;
} else {
    _cursor.delta.x = ev.x - _cursor.pos.x; /* delta vs logical position */
    _cursor.pos.x = ev.x; _cursor.pos.y = ev.y;
}
_phys_x = ev.x; _phys_y = ev.y;             /* always */
```

Without this branch the map does not scroll on right-drag at all. Anything reusing this code
for a game with drag-to-pan needs the same two-position scheme.

A related trap found the hard way: OpenTTD needed **both** `_right_button_down` *and*
`_right_button_clicked` set on press (`amiga_v.cpp:363-379`). The "down" flag alone only says
the button is held; the "clicked" flag is what the GUI layer turns into an event, and that
event is what starts scrolling. Symptom: the button was demonstrably arriving and nothing
happened. Both the SDL and Win32 drivers set the pair together — when a native driver
misbehaves, diff it against an existing one before assuming the platform is at fault.

There is also a runtime diagnostic worth copying: `-v amiga:rmbaslmb` makes the right button
behave exactly like the left one (`amiga_v.cpp:178-182`). If right-clicking then acts like a
left click, the IDCMP event is fine and the bug is in the scroll handling; if nothing happens
at all, the button never reached the driver. That is a one-flag bisection of the whole input
stack.

---

## 6. Performance, with the conditions attached

### The measurements

Measured with `native/agac2p.c` on a calibrated emulated 68040/25, image data 640x512x8,
bitplanes in Chip RAM, chunky source in Fast RAM. Kalms' routine was validated **bit-exact**
against a naive reference implementation before any timing was taken (`agac2p.c:132-154`).

| rectangle | `WritePixelArray8` (graphics.library) | Kalms `c2p_rect` | speedup |
|---|---|---|---|
| 640x512 (full frame) | 388 ms (2.5 fps) | **117.5 ms (8.5 fps)** | 3.3x |
| 320x256 | 96 ms | **29.6 ms** | 3.2x |
| 160x64 | 11.4 ms | **3.8 ms** | 3.0x |
| 64x32 | 2.4 ms | **0.7 ms** | 3.4x |

Extrapolating by clock: roughly 11 fps full-frame on a 68040/33, roughly 14 fps on an 040/40.
That answered the open question of the project — a full native AGA port at 640x480x8 is
viable.

Two conclusions:

**Cost is essentially linear in area.** 640x512 is 16x the area of 160x64 and takes ~31x the
time; 320x256 is 4x the area of 160x64 and takes ~7.8x. Close enough to linear that you can
budget by pixel count: at ~2800 px/ms, a 20 fps budget is roughly 140 000 px/frame. Hence §3.

**c2p on a 68040 is Chip-RAM-write-bandwidth-bound, not CPU-bound.** The community benchmark
on real hardware (A4000/68040-40, 320x256x8) has every good c2p routine converging on
**10.9 ms — identical to a plain Fast→Chip `CopyMem` of the same 80 KB**. The bit shuffling is
free; you are paying for 320 KB of writes onto a ~7 MHz bus that display DMA is also using.
Full sources in `docs/research/c2p-best-practices.md`.

Corollaries that follow directly from being bandwidth-bound:

- **Do not write your own c2p in C.** We tried, twice. A validated 8x8 bit-transpose in C came
  in at 970 ms full-frame — *slower* than the system call. "Optimising" it to accumulate
  32-bit stores made it worse still, 1091 ms, because in C the extra shift/accumulate work
  costs more than the saved write cycles buy (`native/agabench.c` has both, still runnable).
  Use hand-written assembly or use the system call; there is no useful middle.
- **Do not use the blitter to assist c2p on an 040.** It competes for the same Chip RAM bus.
  ADoom, a shipped and tuned product, uses blitter-assisted c2p on 020/030 and CPU-only on
  040+. On 020/030 the picture inverts and the blitter helps a lot.
- **Never byte-write to Chip RAM, never read Chip RAM in the inner loop.** Kalms' routine
  already obeys both.
- **Fewer bitplanes is a linear saving.** 6 planes instead of 8 is 25% less bus traffic. Only
  relevant if you can live with 64 colours.
- Reducing resolution is a linear saving for the same reason. 512x384 is 60% of 640x512.

### Emulator timings are meaningless unless calibrated

State this loudly to anyone reusing the numbers. On the default WinUAE `cpu_speed=max` this
machine runs roughly 8x a real 68040/25; on `cpu_speed=real` it runs about 4.5x too *slow*.
An uncalibrated benchmark run produced 782 ms for the same full frame that measures 388 ms
calibrated — and that bad number led to a documented-then-reversed decision not to write a
c2p at all (`docs/DECISIONS.md`, "SUPERSEDED").

How we calibrated:

1. Run **SysInfo 3.24** inside the emulator and read its ratio against "A4000 68040 25MHz".
   Target 1.0 for an 040/25, 1.33 for the user's real Apollo 040/33.
2. The only control that actually works is `cpu_speed=max` plus `cpu_throttle`. **WinUAE
   writes that slider in tenths of a percent**: `cpu_throttle=-800.0` means -80%, and yields
   SysInfo 0.94 — a real A4000/040/25. `-700.0` gives 1.77, roughly an 040/40. Hand-written
   values like `cpu_throttle=-69` mean -6.9% and do nothing, which is exactly the trap we fell
   into.
3. `cpu_speed=real` **pins the machine to A500 timing and silently overrides** both
   `cpu_frequency` and `finegrain_cpu_speed`. Do not use it.
4. Regardless of configuration: run SysInfo and the benchmark in the *same* config and scale
   the result by the ratio. **Never quote an emulator timing without stating the ratio it was
   measured at.**

Configs: `winuae/otd-aga-040-25.uae` (0.94) and `winuae/otd-aga-040-40.uae` (1.77) for
timing; `winuae/otd-aga.uae` (`cpu_speed=max`) for functional testing, where wall-clock
matters more than realism.

### Frame skipping, and why the simulation rate is untouchable

`amiga_v.cpp:129-149` implements adaptive frame skipping, currently **off by default**. The
reasoning is worth recording: the simulation rate is fixed and must stay fixed — OpenTTD
depends on it for determinism and savegame compatibility, so slowing the game clock to cope
with a slow machine changes how the game actually plays. The honest lever is to draw less
often, which is precisely what fast-forward already demonstrates. It is off by default because
in practice dropped frames looked worse than letting the player press fast-forward; `-v
amiga:frameskip=N` pins it. The skip decision is made *before* the game loop
(`amiga_v.cpp:541-542`) so window drawing — where the blitter cost is — can be skipped too;
nothing is lost, because the application keeps its dirty blocks until something draws them.

### Known unexploited win: ScrollRaster

Panning the view is the one common action that dirties the whole screen. `ScrollRaster()`
makes the blitter move the existing bitmap in Chip RAM and leaves only a thin newly-exposed
strip needing conversion. `native/againput.c:228-239` proves the technique works and measures
it. It is not yet wired into the OpenTTD driver; for a scrolling application it is likely the
single largest remaining optimisation.

---

## 7. If you are porting something else

The shortest path.

**Copy unchanged:**

- `native/c2p_rect.s` — Mikael Kalms, public domain, commercial use explicitly allowed.
- `native/c2p_glue.s` — the register-save wrapper. Only change it if you change `C2PArgs`.
- `native/openttd/amiga_gfx.c` and `.h` — the whole Amiga layer, if you want 8bpp AGA.

**Change:**

- `_amiga_resolutions` equivalent — your own list, **widths multiple of 32**.
- The display-ID heuristic in `amigagfx_open()` if your size/mode policy differs.
- The key table — it is written against your application's key constants.
- `DEPTH` if you want fewer bitplanes (linear bandwidth saving; needs a palette that fits).
- `amigagfx_log()` — currently `fprintf(stdout)` + `fflush`, see §8.

**Write:**

- The equivalent of `amiga_v.cpp`: your application's display/input interface implemented in
  terms of the nine `amigagfx_*` functions. That file contains no Amiga code and should not
  acquire any.

**The contract between the layers, in full:**

1. Call `amigagfx_open(w, h)` with `w` a multiple of 32. Non-zero return says which step
   failed: 1 = chunky alloc, 2 = Chip RAM alloc, 3 = screen, 4 = window.
2. Point your renderer's framebuffer at `amigagfx_chunky()`, stride `amigagfx_pitch()`,
   format 8-bit palette indices, origin top-left.
3. Push the palette with `amigagfx_set_palette(rgb, first, count)`, `rgb` being `count*3`
   bytes.
4. Render into the chunky buffer however you like. Record what you changed.
5. Call `amigagfx_blit(x, y, w, h)` once per changed region. Alignment and clipping are
   handled for you. **Do not merge regions that do not touch** (§3).
6. Drain input with `while (amigagfx_poll(&ev)) { ... }` every frame. Handle key releases.
7. `amigagfx_millis()` for a millisecond clock. It is derived from `DateStamp()`, whose
   resolution is 1/50 s (20 ms) — fine for pacing a game loop, useless for timing a frame.
   If you need finer timing, use a different source.
8. `amigagfx_close()` at exit; it is idempotent and safe to call after a partial open.

**What this layer does not do**, and you may need: no double buffering (single buffered, so a
large blit can tear); no RTG/CyberGraphX path (on a graphics board you would skip c2p entirely
and use `WriteLUTPixelArray` — the natural second backend behind the same nine functions); no
`ScrollRaster` fast-path; no ASL screenmode requester; no sound.

---

## 8. Traps. Read this section before you spend a day on any of them

### Toolchain miscompilations (bebbo amiga-gcc, GCC 6.5.0b)

All four produce **working binaries that behave wrongly**, not compile errors. Every one of
them was first mistaken for a bug in the application or the driver. Each was isolated with a
small standalone Amiga program rather than by debugging inside a 5 MB binary — the probes are
kept in `native/` for exactly that reason.

1. **`-O2` breaks C++ exception unwinding.** `native/exctest.cpp` throws and catches four
   frames deep: `-O0` fine, `-O1` fine, `-O2` catches the throw and then dies with Software
   Failure #80000003. `-O2 -fno-omit-frame-pointer` still dies, so the frame pointer is not
   the cause. A `catch (...)` placed directly around the call does **not** help, because
   unwinding phase 1 never finds the handler and calls `terminate` without unwinding.
   **Build the whole tree at `-O1`.** Where a file trips the cc1plus ICE, drop that file to
   `-O0` — never `-O2`.

2. **`-m68040` does not imply hardware FPU.** You must pass **`-mhard-float`** (or
   `-msoft-float`) explicitly; the default float ABI is broken. Without it GCC emits
   soft-float whose double routines are wrong — identically at `-O0`, `-O1` and `-O2`, which is
   why lowering the optimisation level never helps. `native/fptest.c`:
   ```
   pow(0.7,1) = 0.490000   (want 0.7)
   pow(0.7,3) = 0.000000   (want 0.343)
   ```
   `sin()` and the basic operators were fine; `pow()` and double-to-string were garbage. This
   broke two apparently unrelated things: map generation hung (its inner loop calls `pow()`),
   and savegames written by that binary were permanently corrupt — they load, then fail
   validation. If you hit this, **delete any data files the bad binary wrote**; they will keep
   producing misleading results.

3. **zlib must be built at `-O0`.** Same compiler, silent again: `-O1` → `deflateInit`
   returns `-2`; `-O2` → compresses but `uncompress` returns `-3`, i.e. **silent data
   corruption**; `-O1 -fno-strict-aliasing` still broken, so aliasing is not the cause.
   Cross-testing settles where the fault is: caller `-O0` + zlib `-O1` fails, caller `-O1` +
   zlib `-O0` works. Probe: `native/zlibtest.c`.

4. **`window.cpp` miscompiles at `-O1`.** Closing any window crashed instantly with an
   uncaught C++ exception; rebuilding just that one file at `-O0` fixes it. This is a
   workaround, not a diagnosis.

The meta-lesson: in this toolchain, **codegen is a first-class suspect, not a last resort**.
Four independent miscompilations. When behaviour is inexplicable, rebuild the suspect file at
a lower optimisation level before you rewrite it.

### Build-system traps

- **`./configure` flags do not necessarily reach the generated Makefile.** Ours passed
  `-mhard-float` and the generated `objs/release/Makefile` had only `-m68040`. **Verify flags
  by grepping the generated Makefile**, never by reading the configure script.
- **Do not link `-lc` or `-lpthread`** alongside `-noixemul`. Either pulls newlib in next to
  libnix and you get duplicate definitions of `close`/`fclose`/`__errno`.
- **Set a big stack.** `unsigned long __stack = 1024 * 1024;` at file scope. The AmigaDOS CLI
  default is around 4 KB and a C++ application will Guru.

### Platform traps

- **bebbo defines `__AMIGA__`, not `__AMIGAOS__`.** Every in-tree code path guarded on
  `__AMIGAOS__` alone is silently dead code — in our case including directory creation and
  `Volume:` path handling, which is why saving hung. If you inherit a codebase with existing
  Amiga support, audit every one of those guards.
- **The AmigaDOS 3.1 shell cannot redirect stderr.** `program >file` captures stdout only, so
  anything logged to stderr vanishes and a crash or hang leaves no trace at all. `freopen()`
  on stderr silently fails too — we tried. **Log to stdout with an explicit `fflush()` after
  every line** (`amigagfx_log()`, `amiga_gfx.c:40-44`). Without the flush the last line before
  a hang — the interesting one — never reaches disk.
- **`*` is the AmigaDOS escape character.** `Echo "***"` does not do what you expect in a
  startup script.
- **Do not assume the application's own Amiga support works.** OpenTTD 1.0.5's in-tree
  `CSleep()` drives `timer.device` through a `TimerPort`/`TimerRequest` pair that the Unix
  source declares as NULL and nothing ever opens; the first idle iteration of the main loop
  crashed with #80000008. Replaced with dos.library `Delay()`, which needs no setup.

### Reading a crash

- Software Failure **#80000003** / **#80000004** are address and illegal-instruction errors —
  the signature of a wild write or of executing instructions the CPU does not have (we hit the
  latter from a library built with Apollo/AMMX 68080 blitters on a 68040).
- **`std::terminate() called` is an uncaught C++ exception**, and the `#80000006` that follows
  is just the `abort()`. That signature *rules out* a wild c2p write, which would be a bus or
  address error. Distinguishing these two saved a lot of misdirected searching.
- Put phase markers in the first few main-loop iterations only (`amiga_v.cpp:517-591`,
  `if (trace < 3) amigagfx_log(...)`). A crash is otherwise indistinguishable between game
  logic, window drawing and your own blit, and after iteration 3 the markers cost nothing.

### Emulator traps (WinUAE, for the test loop)

- Use WinUAE **< 3.0** (0.9.90 or 2.8.1) and keep **JIT off** (`cachesize=0`,
  `cpu_compatible=true`); JIT produced spurious #80000004 crashes.
- Set **`gfx_api=0`** (DirectDraw). Direct3D rendering captures as a black image from the host.
- Host screenshots come out **pure black if the emulator window is on a secondary monitor**.
  Both `PrintWindow` and `CopyFromScreen` fail identically, and it looks exactly like "the
  Amiga did not boot". Force the window onto the primary display.
- Use a **directory hard drive** for the deploy target. Binaries and logs then move through an
  ordinary host folder in both directions, with no disk-image juggling and no file locks.
  Keep the startup script on that drive (`S:User-Startup` containing only `Execute Work:run`),
  so changing what runs never requires rewriting the boot image.

---

## Credits and licence

The chunky-to-planar routine in `native/c2p_rect.s` is **Mikael Kalms' `special/c2p_rect.s`**
from <https://github.com/Kalmalyzer/kalms-c2p>, public domain, commercial use explicitly
allowed by the author, strongly influenced (per his own header) by Aki Laukkanen's and
Azure's 040 routines. His c2p tutorial is at
<https://www.lysator.liu.se/~mikaelk/doc/c2ptut/>.

Everything else described here is part of this project and carries its licence (GPL v2, as
OpenTTD). The Amiga layer (`amiga_gfx.c`/`.h`, `c2p_glue.s`) has no dependency on OpenTTD and
can be lifted whole.

Further reading in this repo: `docs/research/c2p-best-practices.md` (the background research,
with sources), `docs/DECISIONS.md` (why AGA, why 1.0.5, why these constraints), `BUILDING.md`
(the toolchain in detail), `PROGRESS.md` (the chronological record, including the wrong turns).
