# Frontier: First Encounters — Amiga 68k (based on JJFFE)

A native AmigaOS port of **Frontier: First Encounters** (1995, originally developed by Frontier
Developments) for classic Amigas with an accelerator, based on John Jordan's **JJFFE**. The Amiga
version was announced in 1995 and never released; this is an attempt to finish it.

It is a replacement executable: you need the original **DOS** game for its data. No game content
is included.

Unofficial fan project, not affiliated with or endorsed by Frontier Developments.

## Status

Playable. Compared frame by frame against the PC reference (JJFFE) and matching it: intro, cockpit
views, all console screens and starport services, galactic/system maps, trading, take-off,
flight, hyperspace, docking at stations, landing on planets, saving and loading.

**It is slow.** The translated code runs instruction for instruction what the PC executed.
Measured in WinUAE without JIT at a speed comparable to a 68040/40: **2–4 frames per second**
in flight in 0.1.2. 0.1.3 is about 1.4× faster in the intro (measured on a fixed-speed emulated
68020: 1.86 → 2.56 fps on average): the texture-mapping loops and a few other hot spots are
hand-written 68k now, and on AGA only the screen rows that changed are converted to bitplanes.
Every such change is checked to give byte-identical frames. A 68060 or a fast FPGA accelerator
is still what the game needs to be playable.

Not yet: music (the floppy release has none).

## Requirements

- 68020 or better (no FPU needed); it runs on a 68020, but see Status: a 68060 or a fast
  FPGA accelerator is needed for a usable frame rate. 32 MB fast RAM recommended
- AGA chipset, or a graphics card with an 8-bit Picasso96/CyberGraphX mode
- The original DOS game files (floppy or CD version)

One executable for every 68k system. Tested in WinUAE with the same binary:

| System | AGA | RTG |
|---|---|---|
| AmigaOS 3.1 (Kickstart 3.1 + Workbench 3.1) | yes | yes (Picasso96) |
| AROS 68k, current nightly ROM and system | yes | yes |
| Older AROS ROM (e.g. WinUAE's built-in one) with a Workbench boot disk | yes | no RTG on that ROM |

AROS for x86, x86-64 or ARM cannot run 68k programs and is not supported.

## Installation

1. Unpack the archive where you keep games, e.g. `Games:`. It holds the `AmiFFE` drawer (with
   its icon): `ffe_game`, `ffe-prefs`, this README and Workbench icons for all of them.
2. Copy your **whole original DOS game folder, as it is**, into a new folder called `FFE`
   inside the `AmiFFE` drawer. Do not pick or rename files — copy everything:

   ```
   Games:AmiFFE/ffe_game
   Games:AmiFFE/FFE/MISSION.DAT
   Games:AmiFFE/FFE/CABIN0.BMP ... CABIN5.BMP
   Games:AmiFFE/FFE/FX/...
   Games:AmiFFE/FFE/COMMANDR/
   Games:AmiFFE/FFE/...           (everything else from the DOS folder)
   ```

   The original installer's folder name `Elite3` also works instead of `FFE`, and so do files
   placed directly next to the executable. A drive or assign named `FFE:` that holds the game
   files is used too, so in WinUAE you can add the DOS folder as a directory drive with the
   volume name `FFE` instead of copying it.
3. Double-click the `ffe_game` icon, or run `ffe_game` from the Shell. The game sets up its own
   1 MB stack, so no `Stack` command or icon setting is needed.

### On a hard disk image (HDF)

No disk image is distributed: the Amiga operating system files are not ours to give away. Use
your own installation:

- **AmigaOS 3.x:** install Workbench on an HDF as usual (or use the one you already have), then
  copy the `AmiFFE` drawer from steps 1–2 onto it, e.g. to `Work:Games/AmiFFE/`.
- **AROS 68k:** the AROS m68k nightly build (from https://www.aros.org, downloads → nightly
  builds, the `amiga-m68k` boot ISO): copy the ISO's contents to your boot drive or HDF, then
  copy the game folder onto it the same way. AROS boots from its own ROM files (`aros-rom.bin`
  + `aros-ext.bin` from the ISO's `boot/amiga/`, in WinUAE as main and extended ROM), or on
  Kickstart through the AROS boot floppy (`Emergency-Boot.adf`).
- In **WinUAE** a host folder can also be used instead of an HDF: *Hard drives → Add Directory*,
  pointing at the game folder.

Saved games go to `AmiFFE/FFE/COMMANDR/`. Amiga and PC save files are not interchangeable.

## Display settings

Run `ffe-prefs` (double-click its icon) before the game to choose the display. It opens a small window; from the Shell,
`ffe-prefs GFX=RTG VIDEO=PAL` saves without one and `ffe-prefs SHOW` prints the settings.

- **Display:** Auto (= AGA), AGA, or RTG (an 8-bit Picasso96/CyberGraphX screen, no
  chunky-to-planar, faster). If no RTG mode is available, the game falls back to AGA.
- **Video** (AGA only): as the machine boots, PAL (50 Hz) or NTSC (60 Hz). Forcing the other
  standard needs its monitor driver in `Devs:Monitors` (e.g. `NTSC` on a PAL machine); without it
  the game uses the machine's default.

The settings are stored in `amiffe.cfg` next to the executable, one `key=value` per line
(`gfx=0|1|2`, `video=0|1|2`), and can also be edited by hand.

## Playing

The controls are the original game's. A few that are easy to miss:

- **Esc twice** opens the game menu (save, load, options).
- **Shift+F1…F5** — time acceleration.
- **F3** galactic map → pick a star → **F1** → **H** to hyperspace (outside the atmosphere).
- **n** — navigation target list; selecting a target engages the autopilot.

## How it is made

The game itself is **not rewritten**. JJFFE is John Jordan's annotated disassembly of the original
DOS executable; an x86 → 68k translator written for this project turns it into 68k code mechanically,
function by function. A few routines are hand-ported, and the original code and tables are
converted to big-endian. What is new is the translator, the Amiga platform layer (display, sound,
input, files) and the build tools.

So `ffe_game` is Frontier's program running on a different CPU, in the same way JJFFE is.

## Building from source

The repository holds only the translator, the Amiga platform layer and the build scripts. The
build also needs the JJFFE disassembly, which is not included. Toolchain: bebbo's amiga-gcc
(`-mcpu=68020 -msoft-float`) and vasm, under WSL/Linux. The CyberGraphX developer headers
(`proto/cybergraphics.h` and friends) are not included; put them in `amiga/cgx-include/`. See
`docs/` and `tools/`.

## Legal

*Frontier: First Encounters* is © Frontier Developments / David Braben. All rights to the game
remain theirs. This includes its code, which the executable contains in translated form, and its
graphics, models, data and sound, which are not distributed here.

The original authors have tolerated community versions of the game for a long time. Frontier
distributed it as shareware through The Elite Club, and JJFFE has been available since the early
2000s. This port follows the same approach: an executable that needs the original game files, and
no game data. If the rights holders object, it will be taken down.

## Credits

- **David Braben / Frontier Developments** — the original game, © 1995.
- **John Jordan** — [JJFFE](https://github.com/videogamepreservation/jjffe), the reverse
  engineering and bug fixes that kept the game alive. This port is built directly on it.
- **Mark Watson** — [fe2-intro](https://github.com/watsonmw/fe2-intro) and his study of the engine.
- **Tom Morton** — [GLFrontier](https://github.com/pcercuei/glfrontier).
- **Jongware** — documentation of the FFE model code format.
- **Mikael Kalms** — the chunky-to-planar routines used for the AGA display.

## Licence

[MIT](LICENSE) covers only the code written for this project: the translator, the Amiga platform
layer and the tools. It does not cover any part of the original game or of JJFFE, including the
game code in translated form inside `ffe_game`.
