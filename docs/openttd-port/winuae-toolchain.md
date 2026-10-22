# WinUAE + toolchain + test automation (research distillation)

Source: research agent, 2026-07-19. Actionable specifics for the Amiga 68k OpenTTD port.

## WinUAE version
- We already have `winuae1.exe` = **WinUAE 0.9.90 (2004)** in repo root — old & fast; uaegfx/AGA/68040 all supported (uaegfx predates WinUAE 1.0). **Try this first.**
- Upgrade path if RTG/automation/config keys are flaky: **WinUAE 2.8.1** (last 2.x), `https://sourceforge.net/projects/winuae/files/WinUAE/2.8.1/WinUAE2810.zip/download`. Fallback 2.4.1. All 1.x/2.x share config keys/CLI.
- Old-versions archive lives on SourceForge (winuae.net only lists 3.0.0+).

## .uae config (A4000 / 68040 / AGA / RTG / directory HD) — 2.x syntax, mostly valid for 0.9.90
```
cpu_type=68040
cpu_model=68040
fpu_model=68040
cpu_speed=max
cpu_24bit_addressing=false
cpu_compatible=false
cachesize=8192              ; JIT on, 8MB -> fast
chipset=aga
chipset_compatible=A4000
chipmem_size=4              ; units of 512KB -> 2MB chip
fastmem_size=8             ; Z2 fast MB (max 8)
z3mem_size=64              ; Z3 fast MB
kickstart_rom_file=I:\GITHUB\Amiga_OpenTTD\ROM3.1_A4000.40.70.ROM
gfxcard_size=16            ; presence enables uaegfx Zorro III RTG board
filesystem2=rw,DH0:Work:I:\GITHUB\Amiga_OpenTTD\<workdir>,0
gfx_width=800
gfx_height=600
gfx_width_windowed=800
gfx_height_windowed=600
gfx_fullscreen_amiga=false
gfx_fullscreen_picasso=false
serial_port=TCP://127.0.0.1:1234   ; log capture (may not exist in 0.9.90 -> fallback file/COM)
use_gui=no                 ; boot straight in
show_leds=true
```
- Portable mode: create empty `winuae.ini` next to the exe -> global state in ini, not registry (reproducible).
- Boot HDF instead of dir-HD via: `hardfile2=rw,DH0:<path.hdf>,0,0,0,512,0,,uae0` (0 geometry = RDB autodetect). We have `workbench_31.HDF` / `aew_wb31.hdf`.
- Directory HD: drop cross-compiled binary into the mapped Windows folder, appears live on Amiga (no reboot).
- Launch: `winuae1.exe -f config.uae` (`-s key=value` overrides per-run; `-G` suppress GUI).

## Automation loop
- **Screenshots (preferred): host-side window capture** via PowerShell + System.Drawing `PrintWindow`/`BitBlt` of the WinUAE window — version-independent, deterministic. (WinUAE's own End+PrintScreen -> `Screenshots\` is the 2.x fallback.)
- **Input**: AutoHotkey `WinActivate ahk_class PCsuxRox` then `Send`/`Click`/`MouseMove` (SendMode Event, ~50ms key down/up so Amiga polls). PowerShell `SendInput` P/Invoke as fallback. Pin window position/size host-side (WinUAE 2.x has no per-.uae window pos).
- **Serial log**: `serial_port=TCP://127.0.0.1:1234` + tiny TCP client -> logfile (2.x). Fallback com0com virtual null-modem -> COM.
- **Checkpoints**: save-state (Shift+End+F5), restore (End+F5), boot via `-statefile=x.uss`. Save a "Workbench+P96 ready" state to skip boot each run. Verify save-state + dir-HD interaction once.
- Guru detection: serial, or detect the red alert box pixel signature in a screenshot.

## Picasso96 (RTG) install under WB 3.1
- Package: Aminet **Picasso96.lha v2.0** `https://aminet.net/driver/video/Picasso96.lha` (matched to old-WinUAE uaegfx; the iComp P96 3.x is for modern WinUAE). Needs `gfxcard_size` set before boot.
- Also need lha: `https://aminet.net/util/arc/lha.run`.
- Install: drop in DH0, `lha x Picasso96.lha RAM:`, run `Picasso96 Install`, tick **uaegfx** board+monitor, reboot. `Prefs/ScreenMode` then lists `uaeGfx:640x480x8/16/24` etc. -> game can open a chunky 8bpp RTG screen (much faster than AGA planar).

## Toolchain (m68k-amigaos cross-GCC on Windows)
- **Prebuilt Windows installer (fastest): `http://franke.ms/download/setup-amiga-gcc.exe`** = bebbo amiga-gcc, **GCC 6.5.0b = full C++14**. Works out of the box for an OLDER OpenTTD baseline.
- Repo moved to Codeberg: `https://codeberg.org/bebbo/amiga-gcc` (branches: `amiga6`=GCC6.5 default, `amiga13.4`=GCC13). Mirror: `https://github.com/AmigaPorts/m68k-amigaos-gcc`.
- Modern OpenTTD needs C++17 (OTTD 12/13) / C++20 (OTTD 14+) -> would require self-built `amiga13.4` via MSYS2/WSL. **Strong argument to pick an older OpenTTD baseline (C++03/11/14) so the prebuilt GCC 6.5 suffices.**
- CPU flags: `-m68040 -mhard-float` (or `-m68020-60` fat binary), `-noixemul` (libnix) for clean native binary.
- Reference headers/autodocs: NDK 3.2 `https://aminet.net/dev/misc/NDK3.2.lha` (amiga-gcc bundles headers too, `NDK=3.2`).
- vbcc = C only, no C++ -> not for OpenTTD itself (fine for small tools).

## Game libraries
- **SDL 1.2 for 68k**: `https://github.com/AmigaPorts/libSDL12` (RTG-aware, P96/CGX, asm blitters, builds with gcc 6.5). Good first video/input backend; native CGX/P96 (`WritePixelArray`/`p96WritePixelArray` direct chunky) faster later.
- **Audio AHI**: runtime `https://aminet.net/driver/audio/ahiusr_4.18.lha`; dev headers `ahidev` (Aminet dev/misc). OpenTTD sound -> ahi.device.
- zlib/libpng/liblzma: cross-compile (`--host=m68k-amigaos`) or strip (build without lzma/zstd).
- **Threads**: AmigaOS 3 has no pthreads -> use OpenTTD's `--without-threads`-era path or a null-thread shim (code-porting task).

## Risk flags
1. Modern OpenTTD C++20 -> forces self-built GCC 13; prebuilt 6.5 only suffices for older baseline.
2. Window-position pinning must be host-side (AHK/WinMove), not a .uae option.
3. Verify save-state + directory-HD interaction before relying on .uss checkpoints.
4. 0.9.90 may lack `serial_port=TCP://` and differ in some keys/hotkeys vs 2.x — validate, else upgrade to 2.8.1.
