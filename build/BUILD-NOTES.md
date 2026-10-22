# OpenTTD 1.0.5 → m68k-amigaos (68040) — Build Notes (Phase 5a)

Goal: prove OpenTTD 1.0.5 compiles + links into a valid AmigaOS 68k Hunk
executable (magic `0x000003F3`) using null/dedicated drivers. Built inside
**WSL1 Ubuntu 22.04** with the bebbo amiga-gcc GCC 6.5.0b cross-compiler.

## 0. Environment gotchas (WSL1 — important)

- This is **WSL1** (`uname` shows `Microsoft` kernel 4.4). Two hard constraints:
  1. **8 MB stack, unraisable.** `ulimit -s` hard cap = 8192 KB. Even as root
     with `ulimit -s unlimited`, WSL1 caps the actual stack VMA at ~8 MB (a
     32 MB recursion test still segfaults). This directly affects the gcc build
     (see §2).
  2. The **`/mnt/i` drvfs mount goes stale/empty.** Re-run
     `sudo mount -t drvfs I: /mnt/i` whenever it errors. Build in native fs
     (`/home/angree`), never under `/mnt`.
- When driving WSL from the Windows Bash tool, the shell layering **strips
  `$VARIABLES`** (even inside single-quoted / quoted-heredoc strings) and can
  split very long `$`-containing lines. Write commands with **literal absolute
  paths, no shell variables**, or put them in a script file that contains no
  `$`.

## 1. Toolchain: bebbo amiga-gcc (GCC 6.5.0b), installed at `/opt/amiga`

Repo `https://codeberg.org/bebbo/amiga-gcc` (default branch `master`). Its gcc
sub-repo is pinned to branch **`amiga6` = GCC 6.5.0b** (correct for OpenTTD
1.0.5's C++03). Build driver: `make all PREFIX=/opt/amiga NDK=3.2 -j16`.

Fragilities hit and fixed:
- **fd2sfd "already-applied patch" abort** — the cloned `adtools/fd2sfd`
  already contains the patch; `patch -N` returns non-zero and kills the recipe.
  Re-running make skips it (the `configure` file already exists). See
  `patches/toolchain-01-fd2sfd-patch-tolerant.md`.
- **Parallel-git checkout crash** — under `-j16`, git crashed mid-checkout of
  the large binutils/gcc repos (`BUG: tempfile.c:230 ... Aborted (core dumped)`),
  and make deleted the target. Fix: **pre-clone the big repos serially** before
  running make (binutils `amiga-2.46`, gcc `amiga6`, newlib-cygwin `amiga`,
  libnix, amiga-netinclude), then `make all` reuses them.

## 2. THE root blocker: gcc stack overflow on WSL1's 8 MB stack

Building the target libraries, the freshly-built cross-`cc1`/`cc1plus`
**segfaults (ICE)** on certain source files at `-O1`/`-O2`/`-Os`, but compiles
them fine at `-O0`. Files seen: `libnix .../__initsock.c` (`_sock_select`),
`libstdc++ .../cp-demangle.c` (`__cxa_demangle`), `.../tree.cc`
(`_Rb_tree_rotate_left` — a *trivial* function), `locale_facets_nonio.tcc`.

A trivial function crashing ruled out "large function". **gdb backtrace of the
ICE:**
```
SIGSEGV in df_uses_record(...)          # gcc's recursive RTL dataflow analysis
Backtrace stopped: Cannot access memory at address 0x7ffffffecf38   # top of stack
```
→ It is a **stack overflow inside gcc's own recursion**, triggered because
WSL1 caps the stack at 8 MB. `-O0` skips the `df` (dataflow) passes, so it does
not overflow. This is a *clean crash*, **not** miscompilation — files that DO
compile at `-O2` are correct.

Two things were tried and are worth recording:
- Rebuilding gcc with host `CFLAGS=-O1` instead of the default `-Os` (edit is a
  make-var override, not a source patch) — did **not** help (the overflow is in
  gcc's runtime recursion, independent of how gcc itself was optimized). It is
  still a reasonable hardening and is harmless.
- Raising the stack as root — **does not work on WSL1** (VMA cap, see §0).

**Fix that works:** build the GCC **target libraries at `-O0`**:
```
make libgcc PREFIX=/opt/amiga NDK=3.2 \
     CFLAGS_FOR_TARGET="-O0 -fomit-frame-pointer" \
     CXXFLAGS_FOR_TARGET="-O0 -fomit-frame-pointer" -j8
```
IMPORTANT: do **not** add `-fno-exceptions -fno-rtti` to `CXXFLAGS_FOR_TARGET`
here. That override *replaces* libstdc++'s per-file flags, and with `-fno-rtti`
the `#else` branch of `ios_failure.cc` uses a C++11 `using X = Y;` alias that
fails under `-std=gnu++98`. Leaving rtti/exceptions at their defaults lets
libstdc++ compile cleanly. `-O0` libstdc++ is fine — OpenTTD uses almost none of
it (essentially just `operator new/delete` from libsupc++); Phase 6 can revisit.

`libnix` also needed one file forced to `-O0`: `sources/socket/socket/__initsock.c`
got a `#pragma GCC optimize ("O0")` prepended (see `patches/`). (Not needed by
OpenTTD's networking, which calls bsdsocket.library directly, but it unblocks
the full libnix install.)

After libgcc: run **`make netinclude PREFIX=/opt/amiga`** — it is NOT part of
`make all`, and it installs `proto/socket.h` / `sys/socket.h` etc. into
`/opt/amiga/m68k-amigaos/ndk-include`, required because `--enable-dedicated`
forces networking on (bsdsocket.library).

## 3. Sanity checks

- C hello, `-noixemul`: links to a Hunk exe, first 4 bytes `00 00 03 f3`. OK.
- C++ hello needs the C++ headers + libsupc++/libstdc++ from §2.

## 4. OpenTTD cross-configure

See `build-openttd.sh` for the exact invocation. Key choices explained there:
`--enable-dedicated` (mandatory — configure refuses null-video otherwise, and
it selects only dedicated_v/null_v/null_s/null_m), `--os=UNIX`, `--endian=BE`
(cross build can't run a target endian probe), `--without-threads` (→
`thread_none.cpp`), all optional libs off. Cross tools via
`--cc-host/--cxx-host/--strip`; native host tools (strgen, endian_check) via
`--cc-build/--cxx-build`. Target flags `-m68040 -noixemul -std=gnu++98`.

## 5. OpenTTD source patches

See `patches/` (each with rationale). Applied to
`/home/angree/build/openttd-1.0.5` (a copy of `upstream/openttd-1.0.5`).

<!-- Section 5/§4 result status is finalized at the end of the build run. -->
