# Running without an FPU: the real cause

**Status: root cause found and proven on both ends (2026-07-21).**
It was never map generation, and never the maths libraries. It was `printf`.

## The symptom

On `winuae/otd-030-nofpu.uae` (68030, `fpu_model=0`) the game died instantly:

```
Software Failure - Work:openttd-w0 - Program failed (error #8000000B)
```

`#8000000B` is a **Line-F trap** — an FPU instruction executed on a CPU that has
no FPU. `Work:ottd.log` was **0 bytes**: not one line written. So the offending
instruction ran at startup, long before terrain generation, which is where
everyone had been looking. Switching to the integer terrain generator
(`land_generator = 0`) changed nothing, because that code is never reached.

## The cause

libnix funnels the whole printf family — `printf`, `sprintf`, `snprintf`,
`vsnprintf`, `fprintf` — through one function, `__vfprintf_total_size`. In the
**68881 build** of libnix, GCC hoists two double constants and the FPU register
save into that function's **prologue**:

```
lea      sp@(-92),sp
fmovemx  %fp2/%fp4/%fp6,sp@-      <- unconditional
fmoved   #1e-10,%fp4              <- unconditional
fmoved   #-0.1,%fp2               <- unconditional
```

They execute on **every** call, whatever the format string. `snprintf("%d")` is
enough. OpenTTD formats strings while building its search paths, before any
logging is initialised — hence a crash with an empty log.

The float code in that function is guarded by `#ifdef FULL_SPECIFIERS` in
`libnix/sources/nix/stdio/__vfprintf_total_size.c`, but the guard only decides
whether `%f`/`%e`/`%g` are *supported*; it does not stop the prologue of the
compiled-with-FPU build from touching `fp0-fp7`.

## Why `-msoft-float` did not help

Because it never took effect on the libraries:

| flags | multilib linked | FPU instructions |
|---|---|---|
| `-m68040` | `libm020/libm881` | 60 |
| `-m68040 -msoft-float` | `libm020/libm881` | 60 |
| `-mcpu=68040 -msoft-float` | `libm020/libm881` | 60 |
| **`-mcpu=68020 -msoft-float`** | **`libm020`** | **0** |

bebbo's driver maps `-m68040` onto the 68020+68881 multilib, because a 68040 has
an FPU built in, and `-msoft-float` does not undo it. The result was a hybrid:
our own code compiled to the soft-float ABI, the C library taken from the
hard-FPU multilib. That mismatch is also the origin of the older
`__floatunsidf`-returns-in-`fp0` bug that `src/fp_conv.c` works around.

Note `-print-multi-directory` reports `.` for all of these and is therefore
**misleading**. The authoritative check is the link trace:

```
m68k-amigaos-g++ <flags> ... -Wl,-t | grep libm881
```

If `libm881` appears, the binary still needs an FPU.

## How it was proven

A 30-line probe (`build/probe/t_printf.c`) that logs through `dos.library`
`Write()` only — never through printf, so the log survives even if printf traps
— then calls `snprintf` and logs again. Built twice from identical source and
run on the 68030/no-FPU config:

| build | log ends at | result |
|---|---|---|
| `-m68040` | `step2: about to call snprintf` | Software Failure #8000000B |
| `-mcpu=68020 -msoft-float` | `step4: done - no FPU trap anywhere` | `4242 and text` |

Same source, same emulator, one flag difference. That is the whole diagnosis.

## The fix

`build/build-nofpu.sh` rebuilds the tree with `-mcpu=68020 -msoft-float`,
including zlib and the four hand-built objects linked through `LIBS` (which
`make` never rebuilds, and which must share the float ABI). It refuses to
proceed if `-m68040` survives anywhere in the flags, and reports any FPU
instruction left in the linked binary along with the symbol it sits in.

## Consequences worth knowing

- The build now targets **68020**, so it should run on a bare 020, an 030
  without a 68882, and a 68LC040 — not only on FPU-equipped machines.
- Floating point becomes genuine soft-float. Only terrain generation uses it,
  so the cost lands there and nowhere else.
- `BUILDING.md` section 2 ("`-m68040` does not mean hardware FPU", advising
  `-mhard-float`) documented this same mismatch from the other side and drew
  the opposite conclusion. It was right that the hybrid was broken; the fix is
  to make the build consistently soft, not consistently hard.
