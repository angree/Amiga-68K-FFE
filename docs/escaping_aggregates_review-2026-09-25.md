# Review of `escaping_aggregates.tsv` (2026-09-25)

Input: `build\datamap\escaping_aggregates.tsv` (336 rows). Skipped: every function already in
`PROVEN_SUBWORD`, plus FUNC_001486 / FUNC_001877 (hand-ported C) and FUNC_002095 / FUNC_002110
(x87 helpers). **59 functions reviewed.** Every verdict below was read off `ffe.asm` (function
body + the callee the address escapes to); "emitted" displacements were checked against the
rewrite notes in `build\xlat-full\FUNC_*.s` where it mattered.

Semantics reminder (frame.py `apply()`): a PROVEN_SUBWORD entry lists the exact `(base, disp)` of
each SUB-WORD access that must pass through. `lea` counts as a 4-byte access in `_slots()`, which
is why most rows here are flipped at all.

Verdict counts: **AGGREGATE 24**, **VALUE 35** (FUNC_000076 has one of each; counted AGGREGATE),
**UNSURE 0**. Two further miscompiles NOT of this class were found on the way (section at the end).

---

## AGGREGATE

### FUNC_000076 -- slot [ebp-0x6f..-0x6d]: AGGREGATE; slot [ebp-0x10c]: VALUE
- 7988-7996: two bytes copied from a byte stream (`mov al,[eax]` / `mov [ebp-0x6f],al`,
  `mov [ebp-0x6e],al`) plus a NUL at -0x6d: a 2-char string.
- 7999/8038/8096/8128/... (25 sites): `lea eax,[ebp-0x6f]` then `mov cl,[eax] / cmp cl,[edx] /
  mov cl,[eax+0x1] / cmp cl,[edx+0x1]` (8002-8008) -- strcmp against `[ebx+0xc0]`, `+0xc3`, ...,
  ascending address order. No dword access to the slot exists except the `lea`.
- Emitted: -0x6f -> -0x6c, -0x6d -> -0x6e: the string is stored reversed, compares always fail.
- [ebp-0x10c] (9750 `mov ax,[ebp+0xfffffef4]`): dword written from `eax` (FUNC_000858's return,
  9734/9738), low word read -> VALUE, flip right.
```python
"FUNC_000076": {("ebp", -0x6f), ("ebp", -0x6e), ("ebp", -0x6d)},
```

### FUNC_000110 -- [ebp-0x2]: AGGREGATE  (flip reads (0,a5))
- 12250-12255: `push byte +0x2 / lea eax,[ebp-0x2] / push eax / ... / call FUNC_000123`;
  FUNC_000123 is a buffered read that `_memcpy`s 2 bytes to the pointer (14103-14107).
- 12257 `movzx eax,word [ebp-0x2]` reads the same two bytes.
- Emitted `[ebp-2] -> [ebp+0]`: reads the high word of the caller's SAVED a5, which nothing wrote.
- Note: pass-through yields the raw file bytes; whether the word then needs a byte swap is the
  same open question as FUNC_000109's dword read at 12238 (not in `le_read_sites.tsv` either).
  That is a data-order question, separate from this slot fix.
```python
"FUNC_000110": {("ebp", -0x2)},
```

### FUNC_000120 -- [ebp-0x4]/[ebp-0x2] point; [ebp-0x208..-0x207] and [ebp-0x30c..-0x30b] strings: AGGREGATE
- 13787-13788 `mov word [ebp-0x4],0x1 / mov word [ebp-0x2],0x11`, 13874 `lea eax,[ebp-0x4]` ->
  FUNC_extravideo, which reads `movsx edx,word [eax]` (x) / `movsx ecx,word [eax+0x2]` (y)
  (13681-13685): a {short x,y} point.
- 13820-13828: `mov byte [ebp-0x208],0x14 / mov byte [ebp-0x207],0x64` then `rep movsd` of the
  string into `lea edi,[ebp-0x206]`, and 13831 `lea eax,[ebp-0x208]` -> TextWriteStringOnly: a
  byte string whose first two bytes are control codes. 13843-13852/13867: the same at -0x30c.
- **Inert today**: `build\xlat-full\FUNC_000120.s` does not exist (x87 `fild/fcomp/fnstsw/sahf`,
  13815-13818). Listing it costs nothing and is right if the function is ever translated.
```python
"FUNC_000120": {("ebp", -0x4), ("ebp", -0x2),
                ("ebp", -0x208), ("ebp", -0x207),
                ("ebp", -0x30c), ("ebp", -0x30b)},
```

### FUNC_000212_SysMenuFileNameEntry / FUNC_000213_SysMenuCmmdrNameEntry -- four P2D points: AGGREGATE
- 17385-17398 (213: 17439-17452): eight word stores build four points at -0x4/-0x2, -0xc/-0xa,
  -0x8/-0x6, -0x10/-0xe (corners from DATA_004911..4914 / DATA_004898..4901).
- 17400-17408: `lea` of each point -> FUNC_001607_DrawQuad -> FUNC_001608, which reads
  `movsx eax,word [esi+0x2]` (y at +2, 95513-95514); prototype comment `F1608 (P2D p1, ...)`.
- 17414/17416: `movsx word [ebp-0x2]` / `[ebp-0x4]` pushed as (y, x) to FUNC_000129.
- Emitted: every x/y pair transposed.
```python
"FUNC_000212": {("ebp", -0x4), ("ebp", -0x2), ("ebp", -0xc), ("ebp", -0xa),
                ("ebp", -0x8), ("ebp", -0x6), ("ebp", -0x10), ("ebp", -0xe)},
"FUNC_000213": {("ebp", -0x4), ("ebp", -0x2), ("ebp", -0xc), ("ebp", -0xa),
                ("ebp", -0x8), ("ebp", -0x6), ("ebp", -0x10), ("ebp", -0xe)},
```

### FUNC_000275 -- [ebp-0x8]/[ebp-0x6]: AGGREGATE
- 21502-21505: `add cx,0x93 / mov [ebp-0x8],cx / add dx,0xbb / mov [ebp-0x6],dx` (x, y).
- 21515-21517: `lea eax,[ebp-0x8]` -> FUNC_001569_DrawScannerStalk `(P2D pos, ...)`, which reads
  `movsx edx,word [ebx+0x2]` / `movsx eax,word [ebx]` (93342-93344).
```python
"FUNC_000275": {("ebp", -0x8), ("ebp", -0x6)},
```

### FUNC_000281 / FUNC_000286 -- RGB triple [ebp-0x4f..-0x4d]: AGGREGATE
- 281: 21824-21835 three bytes built channel by channel; 286: eight blocks of three
  `mov byte [ebp-0x4f/-0x4e/-0x4d],imm` (22312-22409).
- `lea eax,[ebp-0x4f]` -> FUNC_001575, which copies `mov dx,[eax] / mov dl,[eax+0x2]` (93543-93547)
  -- word+byte, address order; same shape as FUNC_001766.
```python
"FUNC_000281": {("ebp", -0x4f), ("ebp", -0x4e), ("ebp", -0x4d)},
"FUNC_000286": {("ebp", -0x4f), ("ebp", -0x4e), ("ebp", -0x4d)},
```

### FUNC_000460 / FUNC_000506 -- word [ebp-0x2]: AGGREGATE  (flip WRITES (0,a5))
- 31938-31943 (506: 33748-33753): `mov ax,[ebx+0x24] / mov [ebp-0x2],ax / lea eax,[ebp-0x2] /
  push eax / push byte +0x10 / call FUNC_000658`.
- FUNC_000658 -> FUNC_000635 treats the pointer as a WORD in/out: `movzx eax,word [esi]`
  (43613) / `mov [esi],dx` (43610).
- Emitted `mov [ebp-0x2],ax  [ebp-2] -> [ebp+0]`: writes the caller's saved a5 while the callee
  reads (-2,a5). A word variable whose address escapes -- pass-through is the only consistent choice.
```python
"FUNC_000460": {("ebp", -0x2)},
"FUNC_000506": {("ebp", -0x2)},
```

### FUNC_000714 / FUNC_000718 / FUNC_000727 / FUNC_001024 / FUNC_001033 -- byte out-params of FUNC_000570: AGGREGATE
All reach FUNC_000570 (via FUNC_000569, which forwards its 7 args verbatim, 35962-35976).
FUNC_000570 writes arg3 and arg4 and arg7 as BYTES and arg5/arg6 as DWORDS:
`mov eax,[ebp+0x10] / mov [eax],dl` (36171-36173), `mov eax,[ebp+0x14] / mov [eax],dl`
(36168-36170), `mov edx,[ebp+0x20] / mov [edx],al` (36166-36167), dwords at +0x18/+0x1c
(36153-36163). A single byte at [ebp-1]/[ebp-2] could not be a dword on x86 anyway -- it would
overlap the saved ebp.
- **FUNC_000714**: arg3 = `lea [ebp-0x2]`, arg4 = `lea [ebp-0x1]` (47272-47284); reads
  `test byte [ebp-0x2],0x10` / `movsx eax,byte [ebp-0x1]` (47286-47288). Emitted
  `[ebp-2] -> [ebp+1]`, `[ebp-1] -> [ebp+2]`: reads bytes of the saved a5.
- **FUNC_000718**: same call (47507-47519), `test byte [ebp-0x2],0x80` (47521) -> (1,a5).
- **FUNC_000727**: arg3 = `lea [ebp-0x6]` (48101); `cmp/test byte [ebp-0x6]` (48108-48114).
  Emitted -6 -> -3, i.e. a byte of the dword out-param at [ebp-0x4].
- **FUNC_001024**: via FUNC_001033, whose arg5 (`[ebp+0x18]`) is forwarded as FUNC_000570's arg7
  (76155-76167); FUNC_001024 passes `lea [ebp-0x2]` there (75698). Then `dec byte [ebp-0x2]`,
  `mov dl,[ebp-0x2]`, `cmp byte`, `movsx byte` (75713-75719). Emitted `-> [ebp+1]`: the `dec`
  WRITES the saved a5.
- **FUNC_001033**: its own arg3 to FUNC_000569 is `lea [ebp-0x5]` (76162); `mov al,[ebp-0x5]`
  (76169). Emitted -5 -> -2, a byte of the dword out-param at [ebp-0x4].
```python
"FUNC_000714": {("ebp", -0x2), ("ebp", -0x1)},
"FUNC_000718": {("ebp", -0x2)},
"FUNC_000727": {("ebp", -0x6)},
"FUNC_001024": {("ebp", -0x2)},
"FUNC_001033": {("ebp", -0x5)},
```

### FUNC_000910 -- byte [ebp-0x1]: AGGREGATE  (flip writes AND reads (2,a5))
- 67766-67771: `mov al,[eax+DATA_007190] / mov [ebp-0x1],al / lea eax,[ebp-0x1] / push eax /
  push byte +0x14 / call FUNC_000148` (message broadcast; handlers get the pointer).
- 67774 `mov al,[ebp-0x1]` then `jmp [eax*4+DATA_000037]` -- a jump-table index.
- The x86 original cannot have treated [ebp-1] as a dword (it spans the saved ebp), so any handler
  access through the pointer is a byte. Emitted `[ebp-1] -> [ebp+2]`: store and read both land in
  the saved a5 -- round-trips, but corrupts the caller's frame pointer on every call.
- The packed-float words at [ebp-0x8]/[ebp-0x6] (67820-67833) are not in the TSV and are VALUE.
```python
"FUNC_000910": {("ebp", -0x1)},
```

### FUNC_000961 / FUNC_000974 / FUNC_000998 / FUNC_001066 / FUNC_001094 -- RGB triples to FUNC_001575: AGGREGATE
Same consumer as FUNC_000281 (word+byte copy, 93543-93547).
- **FUNC_000961**: 71660-71662 three byte stores at -0x3/-0x2/-0x1, 71666 `lea eax,[ebp-0x3]`.
  Emitted `[ebp-3] -> [ebp+0]`: **writes the high byte of the saved a5**, and (-3,a5), the byte
  FUNC_001575 reads first, is never written.
- **FUNC_000974**: 72394-72397, bytes -0x37/-0x36/-0x35, `lea eax,[ebp-0x37]`.
- **FUNC_000998**: 74184-74197, word+byte `mov ax,[DATA_007266] / mov [ebp-0x23],ax /
  mov al,[DATA_007267] / mov [ebp-0x21],al`, re-read as word+byte (74188-74194) and
  `lea eax,[ebp-0x23]`. The FUNC_001766 shape.
- **FUNC_001066**: 79196-79199, bytes -0x23/-0x22/-0x21, `lea eax,[ebp-0x23]`.
- **FUNC_001094**: 80180-80184, word+byte at -0x27/-0x25 from DATA_007275/7276, `lea eax,[ebp-0x27]`.
- Data note (not a slot question): DATA_007266 (`db 7,7`) and DATA_007275 (`db 0,0`) are
  `word-only` in `staticscan.tsv`; both happen to be byte-symmetric, so their storage kind cannot
  show here.
```python
"FUNC_000961": {("ebp", -0x3), ("ebp", -0x2), ("ebp", -0x1)},
"FUNC_000974": {("ebp", -0x37), ("ebp", -0x36), ("ebp", -0x35)},
"FUNC_000998": {("ebp", -0x23), ("ebp", -0x21)},
"FUNC_001066": {("ebp", -0x23), ("ebp", -0x22), ("ebp", -0x21)},
"FUNC_001094": {("ebp", -0x27), ("ebp", -0x25)},
```

### FUNC_001099 -- screen point [ebp-0x34]/[ebp-0x32]: AGGREGATE
- 80618-80622: `lea eax,[ebp-0x64] / push / lea eax,[ebp-0x34] / push / call FUNC_001479`.
  FUNC_001479 writes the destination as `mov [edx],cx` (sx) / `mov [edx+0x2],ax` (sy)
  (88806-88809) -- the packed (sx,sy) word pair, address order.
- 80624-80627: `movsx word [ebp-0x32]` / `[ebp-0x34]` pushed (y, x) to FUNC_001112.
- Emitted: -0x34 <-> -0x32 swapped, the point is read transposed.
```python
"FUNC_001099": {("ebp", -0x34), ("ebp", -0x32)},
```

### FUNC_001111 -- P2D [ebp-0x4]/[ebp-0x2]: AGGREGATE
- 81981-81987: `mov word [ebp-0x4],0x9f / mov word [ebp-0x2],0x4f / ... / lea eax,[ebp-0x4] /
  call FUNC_001612_DrawCircle`: (159,79), the viewport centre.
- DrawCircle reads `movsx ecx,word [eax]` / `movsx eax,word [eax+0x2]` (96475-96477).
```python
"FUNC_001111": {("ebp", -0x4), ("ebp", -0x2)},
```

### FUNC_001114 -- four P2D corners: AGGREGATE
- 82089-82127: eight word stores; x0 at -0x4/-0xc, y0 at -0x2/-0x6, x1 at -0x10/-0x8, y1 at
  -0xe/-0xa, giving points (-0x4:x0,y0) (-0xc:x0,y1) (-0x8:x1,y0) (-0x10:x1,y1).
- The code proves the layout with constants (82129-82135): `cmp word [ebp-0xc],0x140` (x vs
  width 320), `cmp word [ebp-0x2],0x9e` (y vs height 158).
- 82145-82192: four `lea` pairs -> FUNC_001616_DrawLine (oracle-carried with C {short x,y}).
```python
"FUNC_001114": {("ebp", -0x4), ("ebp", -0x2), ("ebp", -0xc), ("ebp", -0xa),
                ("ebp", -0x8), ("ebp", -0x6), ("ebp", -0x10), ("ebp", -0xe)},
```

---

## VALUE (flip is right; no entry)

| function | slot(s) | evidence |
|---|---|---|
| FUNC_000031 | -0x4 | 2951-2952 `mov [ebp-0x4],edi / mov ax,[ebp-0x4]` -- low word of a register value; the `lea [ebp-0x10]` block is a different slot |
| FUNC_000246 | -0xc | 19500/19527/19532/19617 dword stores (`0xffff`, table dword), 19620 low byte read; the leas are the separate bytes -0xd/-0xe/-0xf |
| FUNC_000582 | -0x4 | 38538-38540 `xor eax,eax / mov al,[esi] / mov [ebp-0x4],eax`, byte reads of the low byte; the `lea [ebp-0x8]` is another slot |
| FUNC_000596 | -0x14, -0x8 | FUNC_000592 writes arg3 as three DWORDS (`mov [ebx],eax / [ebx+0x4] / [ebx+0x8]`, 39612-39617) and arg4 as a dword (39610-39611); caller reads low words. **See the sibling miscompile below.** |
| FUNC_000597 | -0x12, -0x8 | 40000-40003 `mov [ebp-0x14],eax` (FUNC_001489 packed float) then `add [ebp-0x12],cx` = exponent adjust in the HIGH word, then `push dword [ebp-0x14]` to FUNC_001482 (numeric by PROVEN_BY_CALLERS); 40149-40152 `mov [ebp-0x8],eax / test byte [ebp-0x8],1` |
| FUNC_000610 | -0x38,-0x34,-0x30,-0x4, -0x18/-0x16 | FUNC_000592 dword out-params + `sub [ebp-0x38],eax` arithmetic; 41980-41984 lo=mantissa/hi=exponent assembled then read whole `mov eax,[ebp-0x18]` into FUNC_001495 |
| FUNC_000622 | -0x40,-0x44,-0x4c,-0x50,-0x54 | FUNC_000869 / FUNC_000870 write every out-param as a zero-extended DWORD (`xor ecx,ecx / mov cl,[...] / mov [esi],ecx`, 60918-60937; 60960-60970); caller reads low words |
| FUNC_000669 FUNC_000672 FUNC_000684 FUNC_000685 FUNC_000689 FUNC_000690 FUNC_000694 FUNC_000695 FUNC_000696 FUNC_000699 | the `0x5000` / `0x31`-style word pair | `mov word [ebp-0x4],0x5000 / mov word [ebp-0x2],0x31 / push dword [ebp-0x4]` (e.g. 45054-45056): a packed float built as a NUMERIC dword 0x00315000 and passed by value to FUNC_000702 -> FUNC_001507. The flip builds the right number. **But the consumer is wrong, see below.** |
| FUNC_000678 | -0x4 | FUNC_000590 arg4: `mov edx,[ebp+0x14] / mov [edx],eax` (39538-39539) |
| FUNC_000682 | -0x14 | FUNC_000901 -> FUNC_000903 arg7: `mov edx,[ebp+0x20] / mov [edx],eax` (67222-67223), read back as dword (67225, 67369) |
| FUNC_000735 FUNC_000741 | -0xc,-0x8,-0x4 | int vector: dword stores (48512-48514 / 48870-48887) + FUNC_001669_VecMatMul in place, low words read |
| FUNC_000756 | -0x10,-0xc,-0x8,-0x4 | FUNC_000590 out-params (3 dwords + shift dword), all four also dword-accessed in the function (49926-49953) |
| FUNC_000773 | -0x8 | dword arithmetic throughout (`sub [ebp-0x8],eax`, `dec dword`, `imul ebx,[ebp-0x8]`), 50747 low word read |
| FUNC_000776 | -0x8 | FUNC_000901 -> FUNC_000903 arg7 dword out-param (as FUNC_000682) |
| FUNC_000839 | -0x11 | 57016-57018 `mov eax,[ebx+0x10a] / mov [ebp-0x14],eax / and byte [ebp-0x11],0x3` -- the TOP byte of a numeric dword (a system code), then `push dword [ebp-0x14]` to FUNC_000852, which reads its arg as a dword (59685) and its top byte as `[ebp+0xb]` (numeric on both sides) |
| FUNC_000843 | -0x3e, -0x42/-0x44 | packed floats: 58697-58699 table dword + `add word [ebp-0x3e]` (exponent); 58788-58832 mantissa/exponent word pairs pushed whole to FUNC_001482 (numeric) |
| FUNC_000860 | -0x8 | FUNC_000852 arg4: `mov edx,[ebp+0x14] / mov [edx],eax` (59728-59729); `test byte [ebp-0x8],0x80` reads the low byte |
| FUNC_000880 | -0x4, -0x2 | 64645-64648 packed float pushed to FUNC_001482; 64656-64690 `mov [ebp-0x4],eax` (FUNC_001496 result) then `add word [ebp-0x2]` exponent tweaks (the JJFFE "FIX" lines) and `mov ax,[ebp-0x2]` |
| FUNC_000982 | -0xe, -0x10 | 72830-72843 packed float `0x0000:7998` pushed to FUNC_001482, then `mov [ebp-0x10],eax` and the exponent read `movsx word [ebp-0xe]` |
| FUNC_001100 | -0x4 | FUNC_000590 arg4 dword (81050-81068) |
| FUNC_001109 | -0x8, -0x4, -0x2 | FUNC_000590 arg4 dword; `mov [ebp-0x4],eax` packed float with exponent `add ax,[ebp-0x2]` (81732-81745) |
| FUNC_001429_GuiReadAllInput | -0x4, -0x8 | `_InputMouseReadPos(long*, long*)` (ffeapi.h:41), low words compared against 0x13f / 0xc7 |
| FUNC_001432 | -0x8, -0xc | `_InputJoyReadPos` / `_InputMouseReadPos` (long*) and FUNC_001429's `add dword [esi]` on the same pointers |
| FUNC_001464 | -0x4, -0x2 | packed float lo/hi built (86673-86675) and pushed to FUNC_001496, which reads its arg as a dword (`mov eax,[ebp+0x8]`, 89530) and so flips its own word accesses |
| FUNC_001485 | +0xc | `neg word [ebp+0xc]` (mantissa of an incoming packed float) with `push dword [ebp+0xc]` in the same function, into FUNC_001486 (numeric per PROVEN_BY_CALLERS) |

---

## Found on the way -- NOT this class, not acted on

1. **FUNC_000596 reads the HIGH words of two vector components.** FUNC_000592 fills
   `[ebp-0x14]`, `[ebp-0x10]`, `[ebp-0xc]` as three numeric DWORDS (39612-39617). Only -0x14 has
   a `lea` (and -0x8, the shift count), so only those two are dword-proven and flipped; -0x10 and
   -0xc are single-width and pass through. `build\xlat-full\FUNC_000596.s` emits
   `move.w (-16,a5),d0` and `move.w (-12,a5),d0` for 39901/39903 -- the high halves -- which are
   then stored into the object's `+0x10a` / `+0x10c`. Hypothesis: the fix is a
   `PROVEN_BY_CALLERS` mark `{("ebp", -0x10), ("ebp", -0xc)}` for FUNC_000596. Falsifier: any
   component whose magnitude is < 0x8000 should read identically on both machines at
   `[obj+0x10a]` after the fix, and 0/-1 before it.
2. **FUNC_001507 reads its packed-float argument pass-through, while every producer is numeric.**
   It reads `movsx eax,word [ebp+0xc]` (mantissa, 89963/89967/89971) and `movsx eax,word
   [ebp+0xe]` (exponent, 89976); FUNC_001507 is not in `PROVEN_BY_CALLERS` and has no dword
   access to the slot, so the emitted code is `move.w (12,a5)` / `move.w (14,a5)` -- i.e. the
   exponent is read as the mantissa and vice versa for a numeric dword. Every caller I found
   passes a numeric dword: `[obj+0xb0]` (4149, 60643, 64831 -- written by register stores at 4144
   and 64693), FUNC_000031's return (5500-5505), a pass-through arg (67915), and the ten
   FUNC_0006xx `0x5000` builders above via FUNC_000702 (46879 `push dword [ebp+0xc]`). Hypothesis:
   `"FUNC_001507": {("ebp", 0xc)}` in PROVEN_BY_CALLERS makes all 16 call sites consistent; the
   alternative (marking the ten builders AGGREGATE) would fix those ten and leave the six numeric
   callers wrong. Falsifier: log `word [ebp+0xc]` at FUNC_001507 entry on both machines -- the
   reference reads 0x5000 for the FUNC_000669 path; ours should read 0x0031 today.
3. FUNC_001100 81077-81081: `mov [DATA_008882],eax` then `add ax,[DATA_008883]` /
   `mov [DATA_008883],ax` -- the exponent word of a dword reached through the NEXT label. The
   `storage.BE32_MEMBER` family; not checked whether it is already covered.
4. FUNC_000110 / FUNC_000109 read file bytes via `_memcpy` and then use them as a word/dword
   (12257, 12238) with no site swap registered. With the slot fixed, the bytes are the file's
   own order; whether the file is LE is a data question.

---

## Summary

| function | verdict | reason |
|---|---|---|
| FUNC_000031 | VALUE | low word of `edi` stored as a dword |
| FUNC_000076 | AGGREGATE (-0x6f..-0x6d) + VALUE (-0x10c) | 2-char string strcmp'd through `lea`; -0x10c is a call result |
| FUNC_000110 | AGGREGATE | `_memcpy` target of 2 file bytes; flip reads (0,a5) |
| FUNC_000120 | AGGREGATE (inert, not translated) | P2D to FUNC_extravideo + two control-code strings |
| FUNC_000212 | AGGREGATE | four P2D corners to DrawQuad |
| FUNC_000213 | AGGREGATE | four P2D corners to DrawQuad |
| FUNC_000246 | VALUE | dword table value, low byte read |
| FUNC_000275 | AGGREGATE | P2D to DrawScannerStalk |
| FUNC_000281 | AGGREGATE | RGB triple to FUNC_001575 |
| FUNC_000286 | AGGREGATE | RGB triple to FUNC_001575 (8 sites) |
| FUNC_000460 | AGGREGATE | word in/out via pointer to FUNC_000658; flip writes (0,a5) |
| FUNC_000506 | AGGREGATE | same as FUNC_000460 |
| FUNC_000582 | VALUE | zero-extended byte stored as dword |
| FUNC_000596 | VALUE | FUNC_000592 dword out-params (sibling slots -0x10/-0xc miscompiled the other way) |
| FUNC_000597 | VALUE | packed-float exponent adjust; dword low byte |
| FUNC_000610 | VALUE | dword out-params / arithmetic; packed float assembled |
| FUNC_000622 | VALUE | FUNC_000869/870 write zero-extended dwords |
| FUNC_000669 | VALUE | numeric packed float by value (consumer FUNC_001507 wrong) |
| FUNC_000672 | VALUE | same |
| FUNC_000678 | VALUE | FUNC_000590 dword out-param |
| FUNC_000682 | VALUE | FUNC_000903 dword out-param |
| FUNC_000684 | VALUE | numeric packed float by value |
| FUNC_000685 | VALUE | same |
| FUNC_000689 | VALUE | same |
| FUNC_000690 | VALUE | same |
| FUNC_000694 | VALUE | same |
| FUNC_000695 | VALUE | same |
| FUNC_000696 | VALUE | same |
| FUNC_000699 | VALUE | same |
| FUNC_000714 | AGGREGATE | two byte out-params of FUNC_000570; flip reads (1,a5)/(2,a5) |
| FUNC_000718 | AGGREGATE | byte out-param of FUNC_000570; flip reads (1,a5) |
| FUNC_000727 | AGGREGATE | byte out-param of FUNC_000570 |
| FUNC_000735 | VALUE | int vector through VecMatMul |
| FUNC_000741 | VALUE | int vector through VecMatMul |
| FUNC_000756 | VALUE | FUNC_000590 dword out-params |
| FUNC_000773 | VALUE | dword arithmetic, low word read |
| FUNC_000776 | VALUE | FUNC_000903 dword out-param |
| FUNC_000839 | VALUE | top byte of a numeric system-code dword |
| FUNC_000843 | VALUE | packed floats pushed to FUNC_001482 |
| FUNC_000860 | VALUE | FUNC_000852 dword out-param |
| FUNC_000880 | VALUE | packed floats, exponent tweaks |
| FUNC_000910 | AGGREGATE | byte passed by pointer to the message broadcast; flip writes (2,a5) |
| FUNC_000961 | AGGREGATE | RGB triple to FUNC_001575; flip writes (0,a5) |
| FUNC_000974 | AGGREGATE | RGB triple to FUNC_001575 |
| FUNC_000982 | VALUE | packed float |
| FUNC_000998 | AGGREGATE | RGB word+byte copy to FUNC_001575 |
| FUNC_001024 | AGGREGATE | byte out-param of FUNC_000570; flip `dec`s (1,a5) |
| FUNC_001033 | AGGREGATE | byte out-param of FUNC_000570 |
| FUNC_001066 | AGGREGATE | RGB triple to FUNC_001575 |
| FUNC_001094 | AGGREGATE | RGB word+byte to FUNC_001575 |
| FUNC_001099 | AGGREGATE | (sx,sy) written word-wise by FUNC_001479 |
| FUNC_001100 | VALUE | FUNC_000590 dword out-param |
| FUNC_001109 | VALUE | dword out-param + packed float |
| FUNC_001111 | AGGREGATE | P2D to DrawCircle |
| FUNC_001114 | AGGREGATE | four P2D corners to DrawLine, proven by 0x140/0x9e compares |
| FUNC_001429 | VALUE | `long*` out-params of InputMouseReadPos |
| FUNC_001432 | VALUE | `long*` out-params of InputJoy/MouseReadPos |
| FUNC_001464 | VALUE | packed float to FUNC_001496 (dword-reading callee) |
| FUNC_001485 | VALUE | incoming packed float, dword-accessed in the same function |
