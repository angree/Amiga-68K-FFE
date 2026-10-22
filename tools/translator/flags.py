#!/usr/bin/env python3
"""The flag model and the liveness pass behind it (PLAN 4).

We model four real flags -- C, Z, N, V -- because those are the four that map
1:1 onto the 68k CCR (x86 CF/ZF/SF/OF). PF and AF are deliberately absent: the
only parity consumer in the whole of ffe.asm is a `jpe` at line 133039, inside a
hand-ported x87 routine, and AF has none at all.

Flags are EAGER: every translated arithmetic instruction is expected to leave
the CCR holding what the x86 left in EFLAGS. That is nearly free because the
mapping is 1:1, but it is not universally free -- a 68k `move` writes N/Z where
an x86 `mov` writes nothing, and a 68k `lsr` clears V where an x86 `shr` sets it
from the old MSB. This module exists so that lowering can PROVE those cases
harmless instead of assuming it: it computes, for every instruction, which flags
are still live afterwards. Lowering then asserts that the flags it gets wrong or
writes spuriously are all dead. If they are not, translation stops.

THE FIFTH, X. The 68k keeps a second carry, the Extend bit, and the
multiprecision instructions read it instead of C: ADDX/SUBX/ROXL/ROXR take their
carry-in from X, never from C. So an x86 CF has two possible destinations
depending on who consumes it, and the two are NOT interchangeable -- 68k CMP and
TST write C but leave X alone, while MOVE writes C but leaves X alone as well.
`X` is therefore modelled as a fifth liveness fact meaning "the 68k X bit must
hold the x86 CF at this point". It is defined by every instruction that writes
CF (the old CF, wherever it lived, is gone) and used only by `adc`, `sbb` and
`rcl`/`rcr`. Two consequences fall straight out of the dataflow:

  - `add` / `mov` / `mov` / `adc` (the 64-bit add idiom, 5 sites in the game)
    translates verbatim, because MOVE does not disturb X. See lo_mov.
  - a `cmp` feeding an `sbb` does NOT, because 68k CMP leaves X stale. That is
    PLAN 4.3's X-flag gap; the 13 sites where it happens are all one idiom, and
    the translator refuses rather than guessing at any of them.

Two liveness boundary conditions, both taken from PLAN 3.3 and both measured
there rather than assumed:
  - flags are DEAD at `ret` (no caller in ffe.asm consumes flags across a call);
  - `call` therefore CLOBBERS all five.
"""
from parse import Insn, Label, Mem, Sym, Table, TranslationError, fail

C, Z, N, V, X = "C", "Z", "N", "V", "X"
ALL = frozenset("CZNV")     # the four x86 EFLAGS bits we model
ALLX = frozenset("CZNVX")   # ... plus the 68k Extend bit that shadows CF
NONE = frozenset()

# x86 condition -> (68k branch, flags the condition reads). PLAN 4.4. The
# operand order of the preceding `cmp` is swapped by lowering, NOT the
# condition -- mirroring the condition as well is the classic double-negative
# bug in hand ports.
CC = {
    "jz":   ("beq", frozenset(Z)),
    "je":   ("beq", frozenset(Z)),
    "jnz":  ("bne", frozenset(Z)),
    "jne":  ("bne", frozenset(Z)),
    "jl":   ("blt", frozenset(N + V)),
    "jnge": ("blt", frozenset(N + V)),
    "jnl":  ("bge", frozenset(N + V)),
    "jge":  ("bge", frozenset(N + V)),
    "jg":   ("bgt", frozenset(N + V + Z)),
    "jnle": ("bgt", frozenset(N + V + Z)),
    "jng":  ("ble", frozenset(N + V + Z)),
    "jle":  ("ble", frozenset(N + V + Z)),
    "jc":   ("bcs", frozenset(C)),
    "jb":   ("bcs", frozenset(C)),
    "jnae": ("bcs", frozenset(C)),
    "jnc":  ("bcc", frozenset(C)),
    "jnb":  ("bcc", frozenset(C)),
    "jae":  ("bcc", frozenset(C)),
    "ja":   ("bhi", frozenset(C + Z)),
    "jnbe": ("bhi", frozenset(C + Z)),
    "jna":  ("bls", frozenset(C + Z)),
    "jbe":  ("bls", frozenset(C + Z)),
    "js":   ("bmi", frozenset(N)),
    "jns":  ("bpl", frozenset(N)),
    "jo":   ("bvs", frozenset(V)),
    "jno":  ("bvc", frozenset(V)),
}

# What each x86 instruction does to the modelled flags: (defs, uses).
# "defs" includes flags x86 documents as UNDEFINED (imul's SF/ZF, everything
# after div): they are clobbered either way, and a downstream reader of one
# shows up as a live flag, which is exactly when lowering must complain.
#
# Every instruction that writes CF defines BOTH C and X, because it destroys the
# CF value that the 68k might have been holding in either bit. The only x86
# instructions that write some flags but deliberately leave CF alone are inc and
# dec -- which is exactly why those two are dangerous on the 68k, where ADDQ and
# SUBQ write C and X like any other add.
EFFECT = {
    "push": (NONE, NONE),
    "pop":  (NONE, NONE),
    "mov":  (NONE, NONE),
    "movsx": (NONE, NONE),     # x86 MOVSX/MOVZX write no flags at all
    "movzx": (NONE, NONE),
    "cdq":  (NONE, NONE),      # nor does CDQ/CWD
    "lea":  (NONE, NONE),
    "nop":  (NONE, NONE),
    # x86 REP MOVS and a bare MOVS write no flags. REPNE SCASB does -- it
    # leaves the LAST comparison's flags behind -- but the loop lowering
    # exits through a TST on the exhausted path, so lower.py declares all
    # five WRONG and liveness has to prove them dead. Measured: every one
    # of the game's 38 sites is `repne scasb` + `not ecx`, no flag read.
    "rep":  (NONE, NONE),
    "repne": (ALLX, NONE),
    "movsb": (NONE, NONE),
    "movsw": (NONE, NONE),
    "movsd": (NONE, NONE),
    "jmp":  (NONE, NONE),
    "ret":  (NONE, NONE),
    "call": (ALLX, NONE),      # t-ABI: flags are dead across a call
    "imul": (ALLX, NONE),      # CF/OF meaningful, SF/ZF undefined
    "mul":  (ALLX, NONE),
    "div":  (ALLX, NONE),      # all four undefined on x86
    "idiv": (ALLX, NONE),
    "shl":  (ALLX, NONE),
    "sal":  (ALLX, NONE),
    "shr":  (ALLX, NONE),
    "sar":  (ALLX, NONE),
    # x86 RCL rotates through CF and leaves SF/ZF alone; on the 68k the carry it
    # rotates through is X, which is what ROXL reads and writes.
    "rcl":  (frozenset(C + V + X), frozenset(X)),
    "rcr":  (frozenset(C + V + X), frozenset(X)),
    "and":  (ALLX, NONE),
    "or":   (ALLX, NONE),
    "xor":  (ALLX, NONE),
    "test": (ALLX, NONE),
    "cmp":  (ALLX, NONE),
    "add":  (ALLX, NONE),
    "sub":  (ALLX, NONE),
    "neg":  (ALLX, NONE),
    "not":  (NONE, NONE),
    "inc":  (frozenset(Z + N + V), NONE),       # x86 inc/dec leave CF alone
    "dec":  (frozenset(Z + N + V), NONE),
    "adc":  (ALLX, frozenset(X)),               # carry-in from X, not C
    "sbb":  (ALLX, frozenset(X)),
}
for _cc, (_b, _u) in CC.items():
    EFFECT[_cc] = (NONE, _u)
    # SETcc reads the same flags its branch reads and writes none.
    EFFECT['set' + _cc[1:]] = (NONE, _u)

# XCHG swaps two operands and leaves every flag alone.
EFFECT['xchg'] = (NONE, NONE)


def effect(ins):
    # `sbb r,r` is -(CF) whatever r held. It therefore READS C -- which the
    # 68k CMP before it produces exactly -- not X, and the lowering emits
    # Scs + EXTB rather than SUBX. Without this special case the general sbb
    # model demands an X that CMP never writes, and the `cmp eax,1 /
    # sbb eax,eax` idiom (PLAN 4.3's sites) refuses at the cmp.
    if (ins.mnem == "sbb" and len(ins.ops) == 2
            and ins.ops[0] == ins.ops[1]):
        return (ALLX, frozenset(C))
    e = EFFECT.get(ins.mnem)
    if e is None:
        fail(ins.line, ins.text, f"no flag model for {ins.mnem!r}")
    return e


def successors(fn, index_of_label):
    """Item-index successor lists. The CFG is complete for ordinary functions:
    PLAN 1 measured zero inter-function jumps in the whole game."""
    succ = {}
    items = fn.items
    for k, it in enumerate(items):
        if isinstance(it, Table):
            # Data, not code: nothing flows OUT of it, and the check at the
            # bottom refuses anything that would flow INTO it.
            succ[k] = []
            continue

        if isinstance(it, Label):
            succ[k] = [k + 1] if k + 1 < len(items) else []
            continue

        if it.mnem == "ret":
            succ[k] = []
            continue

        if it.mnem in ("jmp",) or it.mnem in CC:
            t0 = it.ops[0] if it.ops else None
            if (it.mnem == "jmp" and isinstance(t0, Mem)
                    and t0.sym is not None):
                # `jmp near [edx*4+DATA_x]` -- the successors are the
                # TABLE'S ENTRIES, all labels of this same function, so the
                # liveness at each arm of the switch is computed exactly
                # rather than guessed at.
                tbl = next((x for x in items
                            if isinstance(x, Table) and x.name == t0.sym),
                           None)
                if tbl is None:
                    fail(it.line, it.text,
                         f"indirect jmp through {t0.sym!r}, which is not an "
                         "embedded jump table in this function")
                succ[k] = []
                for e in tbl.entries:
                    ti = index_of_label.get(e)
                    if ti is None:
                        fail(it.line, it.text,
                             f"jump table entry {e!r} is not a label in this "
                             "function")
                    if ti not in succ[k]:
                        succ[k].append(ti)
            elif not isinstance(t0, Sym):
                fail(it.line, it.text,
                     "indirect or non-symbolic branch target in a form the "
                     "CFG cannot enumerate")
            else:
                t = index_of_label.get(t0.name)
                if t is None:
                    fail(it.line, it.text,
                         f"branch to {t0.name!r}, which is not a label in "
                         "this function")
                succ[k] = [t] if it.mnem == "jmp" else [t, k + 1]
        else:
            succ[k] = [k + 1]

        for s in succ[k]:
            if s >= len(items):
                fail(it.line, it.text, "control falls off the end of the function")
            if isinstance(items[s], Table):
                fail(it.line, it.text,
                     "control would flow INTO an embedded jump table -- the "
                     "layout assumption (table sits after an unconditional "
                     "transfer) does not hold here")
    return succ


def liveness(fn):
    """Backward dataflow. Returns {item index: frozenset of flags live AFTER
    that item}. Live-out of the whole function is empty (see module docstring)."""
    items = fn.items
    index_of_label = {it.name: k for k, it in enumerate(items)
                      if isinstance(it, Label)}
    succ = successors(fn, index_of_label)

    live_in = {k: NONE for k in range(len(items))}
    changed = True
    while changed:
        changed = False
        for k in range(len(items) - 1, -1, -1):
            out = NONE
            for s in succ[k]:
                out |= live_in[s]
            it = items[k]
            if isinstance(it, (Label, Table)):
                new = out
            else:
                d, u = effect(it)
                new = u | (out - d)
            if new != live_in[k]:
                live_in[k] = new
                changed = True

    live_after = {}
    for k in range(len(items)):
        out = NONE
        for s in succ[k]:
            out |= live_in[s]
        live_after[k] = out
    return live_after


def show(flags):
    return "".join(f if f in flags else "-" for f in "CZNVX")
