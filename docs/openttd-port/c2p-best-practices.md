# C2P (chunky-to-planar) — best practices research

Research date: 2026-07-19. Target: OpenTTD on AmigaOS 3.1, plain AGA (A4000), 68040 @ 25–33 MHz,
640×512(480) × 8 bitplanes, chunky 8bpp source in Fast RAM, bitplanes in Chip RAM.

Baseline measured by us (WinUAE): `WritePixelArray8()` full 640×512 frame ≈ **780 ms**; our C c2p
(butterfly) 970–1091 ms.

---

## Executive summary

**Our 780 ms/frame figure is not a hardware limit — it is roughly 10–15× worse than what a
well-known assembly c2p achieves.** On a 68040 the bottleneck of c2p is *Chip RAM write
bandwidth*, not CPU arithmetic. Measured community benchmarks show that a good 040 c2p runs at
**memory-copy speed**: 320×256×8 converts in **10.9 ms on an 040/40** — exactly the same time as a
plain fast→chip copy of the same 80 KB (thread "Different c2p routines tested (again)",
comp.sys.amiga.programmer). Scaled to 640×512 (320 KB of planar data) on an A3640 (040/25, chip
write ≈ 6.1 MB/s), a full-frame c2p should land around **55–90 ms ≈ 11–18 fps** — which matches the
~10 fps users report for ShapeShifter in 256 colours on AGA.

ShapeShifter's "magic" is not a faster algorithm than Kalms: it is (a) a tight assembly c2p, (b)
**converting only the parts of the screen that changed** (MMU-based dirty detection in the
MuEVD/EVD drivers), and (c) a **capped refresh rate** decoupled from Mac rendering.

For OpenTTD the answer is therefore: **Kalms' public-domain 040 c2p routines + OpenTTD's existing
dirty-rectangle system + a capped screen refresh**. Expected improvement: worst case (whole screen
dirty) ~10×; typical gameplay (small dirty areas) far more.

---

## 1. Canonical fast 68k c2p routines

### 1.1 Mikael Kalms (Scout/C-Lous) — the reference implementation

Canonical modern location (all routines, public domain, maintained by the author):
- **GitHub: https://github.com/Kalmalyzer/kalms-c2p** — directories:
  - `normal/` — 1–8 bpl converters, CPU-only and CPU+blitter, per-CPU variants
  - `bitmap/` — converters writing into arbitrary AmigaOS `BitMap`s (modulo-aware)
  - `special/` — incl. `c2p_rect.s` (dirty rectangles) and interrupt-friendly `c0` variants
  - `old/`, `others/` — historical + third-party (Aki Laukkanen's 040 routine, Azure's, PPC)
- Tutorial explaining the merge/butterfly technique: https://www.lysator.liu.se/~mikaelk/doc/c2ptut/

Routines directly relevant to us (headers verified in the sources):

| Routine | What it is | Constraints | Performance claim |
|---|---|---|---|
| `normal/c2p1x1_8_c5_040.s` (2000-04-11) | 8 bpl, contiguous bitplanes, no modulo, 040/060-targeted | chunky width must be multiple of 32 px | "**Estimated to run at copyspeed on 040-40 and 060**"; "not designed to perform well on 68020-030" ([source](https://raw.githubusercontent.com/Kalmalyzer/kalms-c2p/main/normal/c2p1x1_8_c5_040.s)) |
| `bitmap/c2p1x1_8_c5_bm_040.s` (2000-04-17) | 8 bpl into **any BitMap** (modulo/interlace OK), with x/y offset | width multiple of 32, x-offset multiple of 8, ≥8 planes in dest; align chunky + dest window to 4 bytes; no self-modifying code ("relies on datacache") | "68040+ optimized — performs badly on 020/030" ([source](https://raw.githubusercontent.com/Kalmalyzer/kalms-c2p/main/bitmap/c2p1x1_8_c5_bm_040.s)) |
| `special/c2p_rect.s` | **Rectangle c2p — explicitly written "for dirty-rectangle updating in windowing systems and 2D games"** | x and width multiples of 32; no error checking | "Optimized for 040+"; influenced by Aki Laukkanen's and Azure's 040 routines ([source](https://raw.githubusercontent.com/Kalmalyzer/kalms-c2p/main/special/c2p_rect.s)) |

Calling convention (both main routines): an `_init` entry taking dimensions/offsets in d0–d5, then
the converter proper with `a0 = chunky buffer`, `a1 = bitplanes/BitMap`. Public domain, commercial
use explicitly allowed.

### 1.2 Measured benchmark numbers (real hardware, community-run)

From the Usenet thread ["Different c2p routines tested (again)"](https://groups.google.com/g/comp.sys.amiga.programmer/c/PWvc5CogyB4)
(comp.sys.amiga.programmer), 320×256×8, PAL lowres, **A4000/68040-40**:

| Routine | ms/frame (040/40) | ms/frame (020/14) |
|---|---|---|
| Ludde CPU2+BLIT2 | 10.90 | 27.17 |
| Kalms CPU3+BLIT1 | 10.90 | 27.27 |
| Kalms CPU4+BLIT0 (CPU-only) | 10.97 | 46.98 |
| McGavin CPU4+BLIT0 (020 version) | 16.33 | 56.77 |
| McGavin CPU4+BLIT0 (040 version) | 11.88 | 81.79 |
| **plain fast→chip copy (reference)** | **~10.90** | ~10.6–11.4 |

Key reading of this table:
- On the **040, every good routine converges on the memory-copy time** (10.9 ms for 80 KB ⇒
  ≈ 7.5 MB/s) — c2p is **Chip-RAM-write-bandwidth-bound**, the CPU shuffling is effectively free.
- On the 020/14, the CPU is the bottleneck and blitter assistance nearly halves the time.

Cycles/pixel for context: the 1994 "Great Chunky to Planar Competition" thread quotes good
**68000** solutions at "under 70 cycles per pixel"
([source](https://groups.google.com/g/comp.sys.amiga.misc/c/Jbo5VVnriWQ)); on 040 the effective
figure is bus-limited, not cycle-limited.

### 1.3 Other named routines / libraries

- **Peter McGavin** — author of **ADoom** ([Aminet game/shoot/ADoom-1.3](https://aminet.net/package/game/shoot/ADoom-1.3),
  [readme](https://aminet.net/game/shoot/ADoom-1.3.readme)). His c2p is measurably a bit slower
  than Kalms' (table above), but ADoom is the best worked example of the full architecture (see §2/§3).
- **Aki Laukkanen** — 060/040 routines used by ADoom and MuEVD; a copy lives in Kalms' repo
  (`others/c2p8_040_amlaukka.s`).
- **c2p.library** (alimede, MIT licence) — shared library, 1–8 bpl, delta conversion (only changed
  pixels), interleaved/custom bitmaps, Akiko support on CD32; "core loop entirely in the CPU
  instruction cache". https://alimede.itch.io/c2plibrary (also on Aminet as `dev/misc/c2plib`).
  Convenient, but for max speed on 040 Kalms' raw routines are the reference.
- **BlazeWCP** ([Aminet util/boot/BlazeWCP178](https://aminet.net/util/boot/BlazeWCP178.readme),
  Rick Pratt) — a system patch replacing `WritePixelLine8/WritePixelArray8/WriteChunkyPixels` with
  32-bit-write c2p. Its very existence confirms the stock OS routines are known to be slow.
  (Interesting for end users; we should call our own c2p, not rely on a patch.)
- "1Seb_FAST_C2P.lha" (dev/asm), "c2p4.lha" (dev/src, cpu+blitter 020+) — further Aminet material,
  nothing measured above Kalms' level.
- I found no evidence that "ScreenBlit" is a distinct notable c2p; treat as low-priority lead.

---

## 2. Blitter-assisted c2p — verdict: **not for a 68040**

- **ADoom's documented design** ([readme](https://aminet.net/game/shoot/ADoom-1.3.readme)):
  "on 68020/30 a blitter-assisted C2P routine (3 CPU merges + 1 blitter pass, double-buffered)…
  **on 68040+ a CPU-only C2P routine is used**". That is a shipped, tuned product choosing
  CPU-only on our exact target.
- The benchmark table (§1.2) shows why: on 040/40 CPU-only (10.97 ms) equals CPU+blitter
  (10.90 ms) — both are pinned at the chip-bus write limit, so offloading merges to the blitter
  buys nothing; the blitter *also* competes for the same Chip RAM bus.
- Demoscene consensus ([pouët thread](https://www.pouet.net/topic.php?which=33&page=1)): the AGA
  blitter is "damn slow, so no one uses it … for c2p, since it would be slower that way" (in the
  fast-CPU context). On 020/030 the picture inverts — e.g. reports of the blitter doing the bulk of
  the work with the 030/50 CPU idle ~3/4 of the time
  ([pouët A500 blitter-c2p thread](https://www.pouet.net/topic.php?which=3838&page=2)).
- Where the blitter *is* still useful on 040/AGA: plain planar copies/scrolls of already-converted
  data, running in parallel with CPU rendering into Fast RAM.

---

## 3. Architectural tricks used by real AGA games/ports

1. **Dirty rectangles — the single biggest win for OpenTTD.** Kalms ships `c2p_rect.s`
   specifically for this ("dirty rectangles updating for windowing systems and 2D games").
   c2p.library likewise offers "delta conversion". OpenTTD already maintains a dirty-block system
   (its blitters repaint only invalidated blocks and the video driver receives update rectangles),
   so the AGA driver should c2p **only the dirty rects** (x/width rounded up to 32-px multiples).
   A typical OpenTTD frame (moving trains, some UI) dirties a small fraction of 640×512; cost drops
   proportionally.
2. **Decoupled, capped refresh (the ShapeShifter model, see §4).** Render chunky in Fast RAM at
   game speed; flush dirty regions to Chip RAM at a fixed max rate (e.g. 10–15 Hz for the
   full-screen worst case, faster when little changed). The game never blocks on Chip RAM.
3. **Interleaved bitmaps** (`AllocBitMap(..., BMF_INTERLEAVED)`): one contiguous chip region per
   scanline group, so rectangle conversion touches one stream, and `c2p1x1_8_c5_bm*.s` handles the
   modulo directly. Also makes OS blits (window moves) single-pass.
4. **Fewer bitplanes = linear bandwidth saving.** 6 planes instead of 8 ⇒ 25 % less chip-write
   volume *and* less bitplane display DMA stealing the bus. For OpenTTD this would need palette
   reduction to 64 colours — keep as a last-resort experiment, not the plan.
5. **Hardware scrolling / ScrollRaster for viewport panning**: classic AGA technique, but OpenTTD's
   many overlapping windows + its own dirty system make it poor fit; skip initially. (ADoom instead
   simply supports lower-res modes and 2×1 low-detail as its speed lever.)
6. **Resolution/mode choice**: ADoom's default was 320×200/320×256; 640×480×8 was "supported" but
   understood to be heavy. Every pixel is bus traffic — 512×384 (ShapeShifter/Savage's favourite)
   is 60 % of 640×512. If OpenTTD's UI tolerates 640×400 or 640×480, prefer that over 640×512i.

---

## 4. The ShapeShifter reality check — how Mac emulators got ~10 fps in 256 colours

ShapeShifter (Christian Bauer; [homepage](https://shapeshifter.cebix.net/),
[readme](https://aminet.net/misc/emu/ShapeShifter311.readme)) supports "color display up to 256
colors on AGA". The mechanism, per the documentation of its video-driver ecosystem:

- **The Mac framebuffer is chunky, in Amiga RAM, written at full speed by the emulated CPU** — the
  emulator never converts per-write.
- **A separate refresh task converts to bitplanes at a configurable refresh rate** (the "refresh
  rate 1…n" setting = frame skip). Savage's benchmark below was taken at refresh rate 1.
- **MMU-based dirty detection**: the external video drivers (EVD interface) can mark the
  framebuffer pages with MMU indirect descriptors and test the *modified* bits — "if one of the
  descriptors is marked as 'modified', the corresponding memory region of the display buffer has
  been touched by MacOS and has to be re-built" —
  [MuEVD.guide, Thomas Richter](http://megaburken.net/~patrik/MMULib/MuTools/MuEVD.guide).
  So **only touched regions get c2p'd**; refresh is driven either by ShapeShifter's refresh task
  (`EVD_Refresh`) or by the driver's own interrupt (`OWNREFRESH`). Users routinely advise "MMU
  Refresh: mostly always want that on"
  ([AmiBay: "Shapeshifter on A4000 — painfully slow"](https://amibay.com/archive/index.php/t-66487.html)).
- **Fast assembly c2p underneath**: MuEVD credits Aki Laukkanen's "rather genius chunky to planar
  conversion algorithm" (same family as ADoom's and Kalms' 040 code). The **Savage** EVD driver
  (László Török) advertised itself as "the fastest driver for native Amiga-chipset (AGA/ECS)":
  **512×384×8 on a 68030/50 at "35 % of Quadra 605 Color QuickDraw speed"**, its author noting
  "higher speed is not possible without changing the c2p routine"
  ([Aminet misc/emu/Savage030Demo.readme](http://aminet.net/misc/emu/Savage030Demo.readme)).
- (Basilisk II, same author, shows the same pattern on Amiga: timer-driven refresh with frame-skip;
  in 1-bit window mode it avoids c2p entirely because Mac 1-bit == one Amiga bitplane.)

**Conclusion:** the emulators' ~10 fps at 256 colours on 040/33 is exactly what §1.2's bandwidth
math predicts for full-frame c2p — and they feel *better* than 10 fps in practice because dirty
detection means most refreshes convert only a small area. They are not doing anything we cannot do;
we are simply currently using the slowest possible path (stock `WritePixelArray8` without Akiko,
which even Amiga forums describe as "terribly slow" fallback —
[exxos forum on Akiko/TF330](https://www.exxosforum.co.uk/forum/viewtopic.php?t=1716&start=10)).

---

## 5. Pitfalls and hard limits

- **Chip RAM bandwidth is the wall.** The chip bus runs at ~7 MHz, multiplexed with display DMA
  (display fetches have priority). Measured figures:
  - A1200/030-28: chip write ≈ **3.8 MB/s** (fast RAM 18.4 MB/s in the same machine) — and a plain
    fast→chip copy of a 320×256×8 screen took 21.5 ms, leading to "impossible to do a 50 Hz
    320×256×8 c2p" ([CHIP RAM speed test results, c.s.a.programmer](https://groups.google.com/g/comp.sys.amiga.programmer/c/WZQVhNzqQhw))
  - A4000 + A3640 (040/25): chip write ≈ **6.1 MB/s**, 8.1 MB/s with a wait-state mod
    ([AmiBay Ramsey/A3640 threads](https://www.amibay.com/threads/a3640-50mhz.20668/))
  - A4000/060: effective c2p-to-AGA throughput ≈ **5.5 MB/s** (same c.s.a.p. thread)
  - Budget check: 640×512×8 = 320 KB planar ⇒ **~52–60 ms/frame minimum** at 5.5–6 MB/s, *before*
    display-DMA contention (8 bitplanes hires steal many bus slots while the display is on). Hence
    the realistic 11–18 fps full-frame ceiling — and the case for dirty rects.
- **Never write bytes to Chip RAM; write longwords** (BlazeWCP's entire speedup is "32-bit c2p
  routines writing longwords to chipram"). Never *read* from Chip RAM in the inner loop.
- **Caches:** Chip RAM is mapped cache-inhibited/serialized on 040 — you cannot "copyback" your way
  around the bus. Keep the chunky buffer and c2p tables in Fast RAM (copyback is fine there);
  Kalms' 040 bm routine deliberately avoids self-modifying code and "relies on datacache".
- **`move16`**: helps fast→fast streaming; the A4000 chip bus does not burst, so expect little or
  no gain for chip writes (unverified — measure on our setup before relying on it).
- **Akiko C2P is CD32-only** — irrelevant for A4000 (and has its own cache pitfalls, see exxos
  thread above).
- **68060-only tricks** (single-cycle ROL/ROR EOR-variants) can *slow down* an 040 — Kalms'
  tutorial warns: "this does only pay off on 68060 … On 68020-68040 your C2P will most likely get
  slower" ([c2ptut](https://www.lysator.liu.se/~mikaelk/doc/c2ptut/)). Use the `_040` variants, not
  `_060` ones.
- **Alignment constraints are real**: Kalms 040 routines require width multiples of 32 px (640 ✓),
  x-offset multiples of 8/32, 4-byte-aligned buffers — round OpenTTD dirty rects outward to those
  boundaries. Invalid parameters "will most probably crash" (his words).
- **Emulator measurement caveat**: WinUAE 0.9.90's chip-bus timing is approximate (and JIT/cycle
  settings change it drastically). Our 780 ms tells us the *relative* story (WPA8 ≪ good asm), but
  final ms-numbers must be re-measured in cycle-exact mode and, ideally, sanity-checked against the
  community numbers above.

### What contradicts our 780 ms measurement

Nothing supports 780 ms as a hardware limit; everything contradicts it as a *routine* limit:
780 ms for 320 KB is an effective 0.42 MB/s — ~13–15× below the measured chip-write bandwidth of an
A4000/040, and ~70× slower per-byte than the 10.9 ms/80 KB the community measured on an 040/40.
`WritePixelArray8` without Akiko is a known-slow generic fallback, and compiled C cannot express
the register-pressure-perfect merge pipeline (the reason BlazeWCP and c2p.library exist).

---

## RECOMMENDATION (ranked)

1. **Adopt Kalms' routines (public domain) now**: `special/c2p_rect.s` for dirty-rect updates +
   `bitmap/c2p1x1_8_c5_bm_040.s` for full-frame/whole-window flushes into an interleaved BitMap.
   Assemble with vasm/bebbo-gas into the video driver. Expected: full 640×512 frame in **~55–90 ms
   (11–18 fps)** vs 780 ms today (**~10×**), small dirty areas in ~1–10 ms.
2. **Wire OpenTTD's dirty-block system straight into c2p_rect** (round rects to 32-px x/width),
   so a normal game tick converts only what changed. This is where the game becomes *playable*
   rather than just faster — typical frames should cost a few ms of chip writes.
3. **Cap and decouple refresh** (ShapeShifter model): game simulation/rendering to Fast RAM chunky
   buffer never waits for chip; a flush step runs at most N Hz and coalesces dirty rects.
4. **Layout hygiene**: `BMF_INTERLEAVED` bitmap, 4-byte-aligned chunky buffer, width 640 (already a
   multiple of 32), tables/code in Fast RAM, no chip reads.
5. **Do NOT build blitter-assisted c2p** for the 040 path (proven no-gain, §2). Optionally use the
   blitter later for planar scrolls of unchanged screen areas.
6. **Prefer a progressive ~480-line mode over 640×512 interlace if usable**; measure display-DMA
   contention in both. Keep 6/7-bitplane palette-reduction only as a last-resort experiment.
7. **Re-benchmark in the emulator after step 1** with the same harness that produced 780 ms, and
   flag that absolute times must eventually be validated against cycle-exact emulation or hardware.

## Source index

- Kalms c2p repo: https://github.com/Kalmalyzer/kalms-c2p (routines quoted:
  [c2p1x1_8_c5_040.s](https://raw.githubusercontent.com/Kalmalyzer/kalms-c2p/main/normal/c2p1x1_8_c5_040.s),
  [c2p1x1_8_c5_bm_040.s](https://raw.githubusercontent.com/Kalmalyzer/kalms-c2p/main/bitmap/c2p1x1_8_c5_bm_040.s),
  [c2p_rect.s](https://raw.githubusercontent.com/Kalmalyzer/kalms-c2p/main/special/c2p_rect.s))
- Kalms c2p tutorial: https://www.lysator.liu.se/~mikaelk/doc/c2ptut/
- Benchmarks 040/40 + 020/14: https://groups.google.com/g/comp.sys.amiga.programmer/c/PWvc5CogyB4
- Chip RAM speed tests: https://groups.google.com/g/comp.sys.amiga.programmer/c/WZQVhNzqQhw
- ADoom readme (architecture + blitter-c2p policy): https://aminet.net/game/shoot/ADoom-1.3.readme
- MuEVD.guide (MMU dirty detection for ShapeShifter): http://megaburken.net/~patrik/MMULib/MuTools/MuEVD.guide
- Savage EVD readme (AGA driver performance): http://aminet.net/misc/emu/Savage030Demo.readme
- ShapeShifter: https://shapeshifter.cebix.net/ , https://aminet.net/misc/emu/ShapeShifter311.readme
- ShapeShifter-on-A4000 user thread (MMU refresh advice): https://amibay.com/archive/index.php/t-66487.html
- BlazeWCP (OS c2p patch): https://aminet.net/util/boot/BlazeWCP178.readme
- c2p.library: https://alimede.itch.io/c2plibrary
- pouët c2p threads: https://www.pouet.net/topic.php?which=33&page=1 ,
  https://www.pouet.net/topic.php?which=3838&page=2
- Great C2P Competition (1994, historical): https://groups.google.com/g/comp.sys.amiga.misc/c/Jbo5VVnriWQ
- exxos forum, Akiko/WPA8 fallback slowness: https://www.exxosforum.co.uk/forum/viewtopic.php?t=1716&start=10
