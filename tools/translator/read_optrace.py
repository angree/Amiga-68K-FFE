#!/usr/bin/env python3
"""Read the model-interpreter opcode trace and name the last handler that ran.

    python tools\\translator\\read_optrace.py [-n 25]

The machine hard-resets a few tens of frames after the game enters the world,
with no crash log at all, so the killer is a wild write or a wild `jsr` inside
one opcode handler. `FfeOpTrace` logs one `OPT p=<stream> w=<word>` line per
dispatch, flushed per line, so **the last line in the file is the last opcode
that started**. This script names its handler.

The dispatcher is `and edx,byte +0x1f / mov eax,[edx*4+DATA_007816] / call eax`
(ref\\jjffe\\ffe.asm:111199-111201), so handler = HANDLERS[w & 0x1f].

Read the result the way LEFTOFF.md sets out BEFORE the run, not after:

  * last OPT inside a known model stream  -> that handler is the killer;
  * last OPT with a pointer outside the model data -> the PREVIOUS line names
    the handler that returned a bad stream pointer;
  * a clean end on opcode 0 (the terminator) -> the interpreter is exonerated
    and the next run instruments the drawing side instead.

Fitting the reading to the data afterwards is how this project produced five
"root causes" in one day and retracted three of them.
"""

import io
import os
import re
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOG = os.path.join(ROOT, "winuae", "work", "ffe_enter.log")

# DATA_007816, ffedat.asm:25528. Roles marked where they are established.
HANDLERS = [
    ("FUNC_001754", "terminator"), ("FUNC_001698", ""), ("FUNC_001702", ""),
    ("FUNC_001707", ""), ("FUNC_001711", ""), ("FUNC_001721", "text string"),
    ("FUNC_001696", ""), ("FUNC_001706", ""), ("FUNC_001710", ""),
    ("FUNC_001738", ""), ("FUNC_001747", "complex polygon"),
    ("FUNC_001752", "skip if cond"), ("FUNC_001755", "skip if !cond"),
    ("FUNC_001758", ""), ("FUNC_001749", "subobject"), ("FUNC_001759", ""),
    ("FUNC_001815", ""), ("FUNC_001760", ""), ("FUNC_001763", ""),
    ("FUNC_001756", "skip if bit clear"), ("FUNC_001757", "skip if bit set"),
    ("FUNC_001697", ""), ("FUNC_001737", ""), ("FUNC_001765", ""),
    ("FUNC_001766", ""), ("FUNC_001771", ""), ("FUNC_001762", ""),
    ("FUNC_001748", ""), ("FUNC_001741", ""), ("FUNC_001758", ""),
    ("FUNC_001767", ""), ("FUNC_001874", ""),
]

OPT = re.compile(r"OPT p=([0-9a-fA-F]+) w=([0-9a-fA-F]+)")


def name(w):
    fn, role = HANDLERS[w & 0x1f]
    return "%s%s" % (fn, "  (%s)" % role if role else "")


def main():
    n = 25
    if "-n" in sys.argv:
        n = int(sys.argv[sys.argv.index("-n") + 1])
    try:
        txt = io.open(LOG, encoding="utf-8", errors="replace").read()
    except IOError:
        print("BRAK LOGU: %s -- czy bieg zostal wykonany?" % LOG)
        return 1

    rows = [(int(m.group(1), 16), int(m.group(2), 16)) for m in OPT.finditer(txt)]
    if not rows:
        print("Zero linii OPT w %s." % LOG)
        print("Sonda nie wystrzelila albo bieg nie doszedl do interpretera --")
        print("sprawdz RENDERPATH: 1772=0 znaczy 'nie doszedl', nie 'sonda zepsuta'.")
        return 1

    print("linii OPT: %d" % len(rows))
    print()

    # Zakresy wskaznika strumienia: skoki wstecz zdradzaja nowy model.
    ps = [p for p, _ in rows]
    print("zakres wskaznika strumienia: 0x%08X .. 0x%08X" % (min(ps), max(ps)))
    print()

    print("=== ROZKLAD OPKODOW ===")
    for w, c in Counter(w & 0x1f for _, w in rows).most_common():
        print("  op %2d  x%-6d %s" % (w, c, name(w)))
    print()

    print("=== OSTATNIE %d DYSPOZYCJI (ostatnia = ta, ktora nie wrocila) ===" % n)
    for i, (p, w) in enumerate(rows[-n:]):
        mark = "   <== OSTATNIA" if i == len(rows[-n:]) - 1 else ""
        print("  p=0x%08X w=0x%04X  op %2d  %s%s" % (p, w, w & 0x1f, name(w), mark))
    print()

    last_p, last_w = rows[-1]
    print("=== ODCZYT ===")
    print("  Ostatni opkod: %d -> %s" % (last_w & 0x1f, name(last_w)))
    if (last_w & 0x1f) == 0:
        print("  Terminator. Interpreter doszedl do konca modelu czysto --")
        print("  to go UNIEWINNIA, a nastepny bieg instrumentuje strone rysowania.")
    else:
        step = last_p - rows[-2][0] if len(rows) > 1 else 0
        print("  Skok wskaznika od poprzedniej dyspozycji: %+d bajtow" % step)
        if last_p < 0x100000:
            print("  WSKAZNIK W NISKIEJ PAMIECI -- to obszar wektorow wyjatkow 68k.")
            print("  Interpreter chodzi i pisze po wektorach, wiec nastepny wyjatek")
            print("  jest podwojnym bledem i maszyna resetuje sie BEZ zadnego handlera.")
            print("  To tlumaczy brak ffe_crash.log mimo zalinkowanego _FfeTrapCode.")
        if abs(step) > 4096:
            print()
            print("  UWAGA na wniosek: DUZY SKOK NIE dowodzi, ze winny jest handler z")
            print("  poprzedniej linii. Opkod 14 (FUNC_001749) REKURUJE przez")
            print("  FUNC_001750 -> FUNC_001773 -> FUNC_001772, wiec sasiednie linie")
            print("  OPT moga nalezec do ROZNYCH wywolan dyspozytora, a nie do jednej")
            print("  petli z przesuwanym pc. Zeby to rozstrzygnac trzeba logowac takze")
            print("  pc WEJSCIA do FUNC_001772 -- granice petli. Bez tego mozna tylko")
            print("  powiedziec, ze wskaznik jest dziki, nie kto go zrobil.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
