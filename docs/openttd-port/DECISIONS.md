# Port decisions (2026-07-19, from Phase-1 research)

## Base version: OpenTTD 1.0.5  (fallback 0.7.5, reference 0.6.3 & 1.9.3)

Chosen because it is the sweet spot for a 68040 native port:
- **C++03** (minimum GCC 3.3; builds fine with bebbo amiga-gcc GCC 6.5 `-std=gnu++98`). No C++11/17/20.
- **8bpp default blitter** (`8bpp-optimized`); the 32bpp blitters exist but are never instantiated. The framebuffer is a plain chunky 8-bit buffer — direct match for Picasso96/CGX `WritePixelArray` or an AGA chunky-to-planar pass.
- **All external libraries optional** (zlib only for savegames/content). SDL never mandatory; Win32-GDI / Allegro / DOS backends in-tree are the native-backend reference.
- **First legally redistributable** OpenTTD: OpenGFX + OpenSFX + NoSound/NoMusic fallbacks (1.0.0+). Decisive — TTD never shipped on Amiga, so users can't legally supply original grfs.
- **Still contains the in-tree Amiga/MorphOS scaffolding** (removal was 2019, commit `7784d7771362f98145f17c469430e0f3e3b207b5`, first missing in 1.10).

Source: https://cdn.openttd.org/openttd-releases/1.0.5/openttd-1.0.5-source.tar.bz2
Fallback 0.7.5 (last with cheap NTP train pathfinder; sound needs original sample.cat): https://cdn.openttd.org/openttd-releases/0.7.5/openttd-0.7.5-source.tar.bz2
Reference 0.6.3 (official MorphOS build `openttd-0.6.3-morphos.lha` = big-endian proof).
Last full tree WITH Amiga support (for reference if 1.0.5 bits are thin): https://github.com/OpenTTD/OpenTTD/tree/1.9.3

### Pathfinder
1.0.5 removed NTP (r18362) — cheapest train PF is gone; rely on YAPF (built to cut CPU vs NPF, has segment caching). If YAPF is the 68040 bottleneck after profiling, drop to 0.7.5 (has NTP+OPF).

### Rendering path
Prefer **RTG 8bpp chunky** (Picasso96/CGX `WritePixelArray`) — far faster than AGA planar. AGA path = chunky-to-planar (c2p) as stretch goal. Game opens screen via system ScreenMode requester (ASL) so both RTG modes and PAL:HiRes Laced are selectable.

## Reusable code from OpenTTD's own history (1.0.5 tree, cross-check 1.9.3)
- `src/thread/thread_morphos.cpp` — exec/Message-based threads. **We will NOT use it**; use `thread_none.cpp` and run single-task (draw+game loop in one task). The MorphOS thread code's own comment warns it "goes foobar" without exact libnix semaphore support.
- `src/network/core/core.cpp` + `os_abstraction.h` — `__AMIGA__`/`__MORPHOS__` `bsdsocket.library` networking (`OpenLibrary("bsdsocket.library",4)`), timer.device-based usleep. Networking is low priority; may stub initially.
- `src/os/unix/unix.cpp` (Amiga bits), `src/fileio.cpp`, `src/fios.cpp`, `src/stdafx.h` — path/volume handling (avoid `//` in filenames — a documented Amiga crash), byte order shims, `$VER:` version cookie, `os/morphos/` packaging.
- **Endianness: no minefield.** OpenTTD has a first-class `TTD_BIG_ENDIAN` path; savegame container is already big-endian/network order (zero-swap on 68k); GRF reading is byte-by-byte. Proven by the shipping MorphOS (big-endian) builds.

## What we must WRITE (never existed in mainline)
- **Native video driver** (`video/amiga_v.cpp`-style): expose the 8bpp `_screen` buffer, push dirty rects to CGX/P96 (or c2p to AGA), pump Intuition IDCMP events in `MainLoop`, ScreenMode requester via ASL. Reference: the Win32-GDI driver (no SDL).
- **Native sound driver**: feed `MxMixSamples()` output to **AHI** (`ahi.device`). Music via AHI or extmidi/CAMD later.
- Alternative first-bringup video/input path: **SDL 1.2 for 68k** (`github.com/AmigaPorts/libSDL12` / `HenrykRichter/libSDL12_Amiga68k`, RTG-aware, 68k-asm blitters). Use SDL to get pixels on screen fast, then replace with native CGX/P96 for speed.

## Toolchain
- **bebbo amiga-gcc, GCC 6.5.0b (C++14)** — prebuilt Windows installer `http://franke.ms/download/setup-amiga-gcc.exe`. Sufficient for 1.0.5 (C++03). Repo: `https://codeberg.org/bebbo/amiga-gcc`.
- Flags: `-m68040 -mhard-float -noixemul` (libnix, clean native binary, no ixemul dependency).
- Reference headers/autodocs: NDK 3.2 `https://aminet.net/dev/misc/NDK3.2.lha`.

## Test environment
- WinUAE 0.9.90 (`winuae1.exe`, in repo) — A4000/68040/AGA, boot `workbench_31.HDF`, ROM `ROM3.1_A4000.40.70.ROM`. Config: `winuae/otd-boot.uae`. Upgrade to WinUAE 2.8.1 if RTG/automation flaky.
- Picasso96 (Aminet v2.0) for RTG uaegfx; AHI (`ahiusr_4.18.lha`) for audio.
- Automation: host-side screenshot (`winuae/harness/capture.ps1`, CopyFromScreen), AutoHotkey/SendInput for clicks, serial→TCP/file for logs, save-states as checkpoints. Screenshot→read→click→screenshot loop.

## Hard rules learned from the failed 0.3.3 port
1. Modern toolchain (GCC 6.5) — kills the gcc-2.95 miscompilation class.
2. **No threads** — single-task (`thread_none`).
3. Native video backend for AGA (SDL only acceptable for RTG-only first bringup).
4. Track a maintainable tag; don't freeze forever like 0.3.3 did.
5. Small-map/8bpp path; careful pool sizing; document a realistic floor (060/040 + 32–64 MB Fast RAM + RTG for comfort, AGA as stretch).

## Display architecture (2026-07-19): pluggable backends — AGA **and** RTG, ECS/OCS not precluded

**Requirement (user, explicit):** the port must keep **AGA compatibility**, not just Picasso96.
Classic AGA machines are the heart of this project; the old 2004 port ran on AGA too, only slow in
hires. So the native video driver is built as **one VideoDriver + a thin display-backend layer**,
never CGX-only.

```
src/video/amiga_v.cpp   -> OpenTTD VideoDriver (Start/Stop/MakeDirty/MainLoop/
                           ChangeResolution/ToggleFullscreen) + IDCMP mouse & keyboard
  backend interface     -> Open(w,h) / Close() / BlitChunkyRect(...) / SetPalette(...) / GetInfo()
  ├── backend_cgx       -> RTG (Picasso96/CyberGraphX): chunky 8bpp straight out via
  │                        WriteLUTPixelArray (or LockBitMap+memcpy). No conversion. Fast.
  └── backend_aga       -> AGA: chunky->planar (c2p) into an 8-bitplane (256 colour) screen.
```

**Why this is clean:** OpenTTD always renders into a **chunky 8bpp** buffer (`_screen.dst_ptr`,
blitter `8bpp-optimized`). Only the final "get it on screen" step differs per chipset, so one
driver + two blit strategies covers both worlds.

**Selection:** runtime detection (prefer RTG if `cybergraphics.library` + an 8bit mode exist, else
fall back to AGA), with an explicit override for testing (`-v amiga:cgx` / `-v amiga:aga`) and a
clear log line stating the chosen backend.

**AGA resolution — do NOT go below 640x480 (user, explicit).** OpenTTD does not scale its UI: it
renders at native resolution, so below 640x480 the windows/toolbars/menus stop fitting — the GUI
breaks, it isn't merely "smaller". (The PSP build showed sub-480p is technically possible but not
comfortable to play; lores is worse still.) Going lower would require downscaling the finished frame
via a LUT — extra work AND worse image. So: **AGA target = 640x480 @ 8 bitplanes / 256 colours**,
same as the RTG path. Accept the c2p cost, measure real FPS on the 68040, and optimise from there
(fast c2p + strict dirty-rect blitting — on AGA that is a necessity, not an optimisation).
No resolution tricks, no lores mode.

**ECS/OCS:** deliberately NOT implemented now — those chipsets cap at 32 colours (64 EHB) while
OpenTTD needs 256, so they'd need palette reduction/dithering and a real quality sacrifice. The
backend interface is kept clean enough that a `backend_ecs` could be added later.

**Test matrix:** RTG path on `winuae/otd-aew.uae` (uaegfx present); AGA path on a config with the
RTG board removed (no `gfxcard_size`) so the fallback is genuinely exercised on a plain AGA A4000.

## SETTLED: use Kalms' assembly c2p (2026-07-19, measured on a CALIBRATED 68040/25)

**This supersedes the "do NOT write our own c2p" conclusion below, which rested on two bad
premises**: the emulator was never calibrated (it ran at A500 speed, ~4.5x too slow), and the only
alternatives tested were written in C. With a real 68040/25 (`winuae/otd-aga-040-25.uae`,
`cpu_throttle=-800.0`, SysInfo 0.94) and Mikael Kalms' 68040 assembly:

| rect | WritePixelArray8 | **Kalms c2p_rect** | speedup |
|---|---|---|---|
| 640x512 full | 388 ms (2.5 fps) | **117.5 ms (8.5 fps)** | 3.3x |
| 320x256 | 96 ms | **29.6 ms** | 3.2x |
| 160x64 | 11.4 ms | **3.8 ms** | 3.0x |
| 64x32 | 2.4 ms | **0.7 ms** | 3.4x |

Validated bit-exact against a naive reference before timing. Extrapolating: ~11 fps full-frame on a
68040/33, ~14 fps on an 040/40 — consistent with the user's recollection of Mac emulators managing
~10 fps while scrolling, and with the 55-90 ms bandwidth ceiling from `research/c2p-best-practices.md`.

**Decisions that follow:**
- The AGA backend uses `upstream/kalms-c2p/special/c2p_rect.s` via `native/c2p_glue.s`
  (bebbo amiga-gcc ships `vasmm68k_mot`, so Motorola syntax assembles with no translation).
- The screen must own a **single contiguous Chip RAM block** for all 8 planes (`AllocMem` +
  `InitBitMap` + `SA_BitMap`); `AllocBitMap` does not guarantee equally-spaced planes and c2p_rect
  requires them. See `native/agac2p.c`.
- Constraints to respect everywhere: x and width multiples of 32, chunky buffer in Fast RAM.
- Dirty rects still matter enormously (0.7 ms vs 117 ms), and `ScrollRaster` for panning still
  stands - but the port is comfortably viable on AGA, which was the open question.

**Never quote an emulator timing without its SysInfo ratio.** `cpu_speed=real` pins the machine to
A500 timing and overrides `cpu_frequency`/`finegrain_cpu_speed`; the working control is
`cpu_speed=max` + `cpu_throttle`, written by WinUAE in **tenths of a percent** (`-800.0` = -80%).
Configs: `otd-aga-040-25.uae` (0.94) and `otd-aga-040-40.uae` (1.77).

## SUPERSEDED: the AGA blit cost (2026-07-19, Step 2) — timings here were measured UNCALIBRATED

Benchmark `native/agabench.c`, run on `winuae/otd-aga-25mhz.uae` — pure AGA, **CPU throttled to
25 MHz** (`cpu_speed=real` + `cpu_frequency=25000000`), i.e. an A4000/040. Timings under the default
`cpu_speed=max` are ~20x faster and **meaningless as a target** — always benchmark throttled.

| rect | WritePixelArray8 (system) | our c2p, byte stores | our c2p, 32-bit stores |
|---|---|---|---|
| 640x512 full | **782 ms** (1.2/s) | 970 ms | 1091 ms |
| 320x256 | **198 ms** (5.0/s) | 243 ms | 274 ms |
| 160x64 | **25 ms** (40/s) | 31 ms | 35 ms |
| 64x32 | **5 ms** (200/s) | 6.2 ms | 7.2 ms |

Our c2p was validated bit-exact against a naive reference before being timed, so this is a fair
comparison, not a broken implementation.

**Conclusion 1 — do NOT write our own c2p.** Two hypotheses were tested and both were wrong: a
hand-rolled 8x8 bit-transpose in C loses to graphics.library by ~20%, and batching Chip RAM writes
into 32-bit stores made it *worse* still (the extra shift/accumulate work in C costs more than the
saved write cycles). `WritePixelArray8()` is already well tuned. Only hand-written 68k assembly
(e.g. the Kalms c2p) could beat it, and even a 3x win would only reach ~4 fps full-frame — it does
not change the architecture. Use the system call; spend the effort elsewhere.

**Conclusion 2 — dirty rectangles are mandatory, not an optimisation.** Cost is linear in area
(~420 px/ms). A 20 fps budget is 50 ms = **~21 000 pixels per frame**, about one 160x131 rect.
Typical OpenTTD UI updates fit comfortably; a full-frame repaint (1.2 fps) never will.

**Conclusion 3 — viewport scrolling must not repaint the frame.** Panning the map is the one common
action that dirties everything. Use `ScrollRaster()` so the blitter moves the existing bitmap and
only the newly exposed strip goes through c2p. This is the single most important optimisation on
AGA and must be designed into the video driver from the start, not bolted on.

**Headroom:** this is the *floor* — a 25 MHz 68040. A 68060/50-66 is ~3-4x faster, and the RTG path
skips c2p entirely (chunky straight out), so the same driver should be comfortable there.
