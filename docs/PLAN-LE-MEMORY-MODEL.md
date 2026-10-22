# PLAN: model pamięci little-endian (LE) dla całego przetłumaczonego kodu

*Data: 2026-09-25. Autor: investigator (fable), na zlecenie orkiestratora. Dokument planistyczny —
żadna linia kodu nie została zmieniona. Wszystkie liczby poniżej są ZMIERZONE skryptami
uruchomionymi dziś na `ref\jjffe\ffe.asm`, `build\xlat-full\*.s` i manifestach storage; tam gdzie coś
jest szacunkiem, jest to napisane wprost.*

---

## 0. Rekomendacja w pięciu zdaniach

1. **Wprowadzić jednolity model LE**: każda komórka pamięci widoczna dla kodu gry (dane statyczne
   `ffedat`/`ffebmp`/support, BSS, stos, bufory przekazywane z C) trzyma bajty **dokładnie jak na
   x86**; każdy 16/32-bitowy load/store w przetłumaczonym kodzie robi `rol.w #8` / `swap`;
   dostępy bajtowe nic nie kosztują. To jest wariant **(C) hybrydowy z LE jako domyślnym**:
   wyjątkami są wyłącznie komórki, których **nigdy nie czyta kod x86 jako danych** (adresy
   powrotu `jsr`, łącznik ramki `push ebp`/`pop ebp`, obszary `movem` wrapperów, tabele skoków
   wbudowane w funkcje, prywatne — dowiedzione — sloty stosu).
2. **To nie jest nowy mechanizm, tylko uogólnienie istniejącego.** Manifest storage mówi już dziś,
   że **97 % bajtów obrazu danych jest w kolejności x86** (`le` + `plain` = 2,14 MB z 2,22 MB), a
   ścieżka „nazwany operand `le` -> swap przy dostępie" istnieje w `lo_mov`, `lo_cmp`, `_addsub`,
   `_logic`, `lo_movsx/movzx`, `lo_push`. Model LE to: „`_swap_kind()` zwraca `le` dla KAŻDEGO
   operandu 2/4-bajtowego, chyba że jest na liście wyjątków", plus brakujące kształty (RMW przez
   wskaźnik/stos, `push`/`pop` rejestru, `call`/`jmp` przez pamięć, thunki na granicy z C).
3. **Znikają**: `le_read_sites.tsv` (293 wiersze), `lo_byte_sites.tsv` (56), `PROVEN_SUBWORD` (59
   funkcji / 203 sloty), `PROVEN_BY_CALLERS` (89 / 104), `BE32_MEMBER`, `_static_disp()`,
   `storage.resolve()`, klasyfikacja `staticscan/wordscan/streamscan/overrides*.tsv` (cały
   `tools\datamap\` poza konwerterem), flip przemieszczeń w `frame.py`. Zostaje jedna reguła i
   jeden audyt („każdy dostęp 2/4-bajtowy ma swap albo tag `native`").
4. **Koszt**: statycznie +25…40 % instrukcji w przetłumaczonym kodzie (po mitigacji (i)),
   dynamicznie szacunkowo **1,2x–1,6x wolniej** przed mitigacjami; gorące pętle rasteryzatora są
   **bajtowe** (DrawHLine: 2 dostępy `.b` w pętli, Tex*: 3 `.b` w pętli, cała reszta to `.l` na
   stosie w setupie), więc mitigacja (i) „prywatne sloty stosu zostają natywne" odzyskuje
   większość. Docelowo (iii): ręczne, natywne przepisanie top-N gorących liści **przy zachowaniu
   konwencji LE w pamięci** — dokładnie to, o co prosi deweloper („najpierw działa, potem
   wymieniamy na szybsze").
5. **Największe ryzyko** to nie translator, tylko **granica z C** (loader hunków relokuje wskaźniki
   natywnie -> potrzebny fix-up przy starcie; 70 zewnętrznych funkcji C wołanych ze stosu LE ->
   generowane thunki; ~80 miejsc w sondach C czytających pamięć gry natywnie). Każde z nich ma
   poniżej falsyfikator.

---

## 1. Stan dzisiejszy — liczby, na których stoi plan

### 1.1 Obraz danych statycznych (manifesty `build\data-be\*.storage.tsv`, `support_data.storage.tsv`)

| plik | kind | etykiet | bajtów |
|---|---|---|---|
| ffedat | `le` | 3105 | 518 377 |
| ffedat | `plain` | 257 | 396 847 |
| ffedat | `be32` | 805 | 79 344 |
| ffedat | `be16` | 91 | 19 188 |
| ffedat | `split` | 22 | 4 220 |
| ffebmp | `le` | 3366 | 1 220 536 |
| ffebmp | `be32` | 492 | 79 408 |
| ffebmp | `split` | 465 | 32 576 |
| support | `le`/`be32`/`split` | 55/27/2 | 3405/352/40 |

**Wniosek:** tylko ~178 KB (`be32`+`be16`) jest dziś fizycznie przestawione; `be32` to głównie
`dc.l` z relokowanymi wskaźnikami (742 x `dd FUNC_`, 7 258 x `dd DATA_` w obu plikach) i
tablice liczb. Reszta (97 %) już JEST w układzie x86 i już dziś wymaga swapów przy szerszych
odczytach — czyli model LE nie zmienia charakteru pracy, tylko usuwa wyjątki.

### 1.2 Tabele per-miejsce, które plan likwiduje

`le_read_sites.tsv` 293 wiersze; `lo_byte_sites.tsv` 56; `PROVEN_SUBWORD` 59 funkcji / 203
slotów; `PROVEN_BY_CALLERS` 89 funkcji / 104 sloty; `BE32_MEMBER` 2; `overrides*.tsv` ~160 KB.
Każdy wpis to jeden zdiagnozowany błąd tej samej klasy (zapis w jednej szerokości, odczyt w innej).

### 1.3 Census x86 (`ffe.asm`, 121 831 instrukcji, skrypt z 2026-09-25)

| klasa operandu pamięciowego | dword | word | byte |
|---|---|---|---|
| stos (`[ebp+-d]`, `[esp+d]`) | 13 991 | 678 | 1 438 |
| nazwany (`[DATA_x...]`) | 4 628 | 239 | 1 200 |
| przez wskaźnik (`[reg+d]`, indeksowane) | 9 840 | 2 886 | 3 629 |
| **razem** | **28 459** | **3 803** | **6 267** |

Do tego: `push` 14 777 rejestrów + 3 400 `byte imm` + 2 325 `dword` (imm/mem/label); `pop` ~6 400;
`lea` 3 744 (nie jest dostępem); `rep movsd` 151, `rep movsb` 40, `repne scasb` 38, gołe `movs*` 19,
**zero** `stos/lods/cmps`; `xchg` z pamięcią **0**; x87 z pamięcią 143 (wszystkie w 6 funkcjach
portowanych ręcznie w C). RMW na pamięci: `add` 728, `inc` 322, `sub` 185, `sar` 175, `dec` 138,
`or` 112, `and` 103, `shl` 97, `neg` 65, `not` 26, `xor` 25, `shr` 2, `adc` 1 (razem ~2 000,
w tym część bajtowych — te nic nie kosztują).

Instrukcje z pamięcią wg mnemonika: `mov` 26 636, `cmp` 3 166, `movsx` 1 867, `add` 1 417, `call`
1 385 (przez pamięć), `push` 1 032, `sub` 472, `imul` 436, `movzx` 358, `test` 348.

### 1.4 Wyemitowany 68k (`build\xlat-full`, 1 612 plików, 144 225 instrukcji)

Dostępy do pamięci: `.l` 49 217 (w tym stos 34 591, absolutne 4 238, wskaźnik 10 388), `.w` 3 912,
`.b` 4 478, `pea` 8 951, `movem` 3 050. Gorące funkcje:

| funkcja | instr. | `.l` | `.w` | `.b` | uwaga |
|---|---|---|---|---|---|
| FUNC_001601 DrawHLine | 61 | 14 (stos/setup) | 0 | 2 | pętla pikselowa bajtowa; `mov [edx],ecx` z kolorem x4 — swap neutralny wartościowo |
| FUNC_001613 DrawSpans | 125 | 48 | 0 | 0 | głównie sloty stosu + rekordy spanów |
| FUNC_001650..1653 Tex* | 238–240 | 134 | 0 | 3 | 134 `.l` to setup na stosie; pętla bajtowa |
| FUNC_001521 mul | 14 | 5 | 0 | 0 | 2 argumenty + wynik; kandydat na inline |
| FUNC_001772 interpreter | 32 | 13 | 1 | 0 | fetch opcodu już dziś ma swap (site 111195) |
| FUNC_001478 wierzchołki | 1217 | 406 | 20 | 99 | |
| FUNC_001604 DrawTriangle | 1010 | 174 | 110 | 0 | 110 `.w` = pary {x,y} |

### 1.5 Fakty przesądzające o granicach modelu (zmierzone)

- **Kod x86 nie czyta adresu powrotu ani zapisanego `ebp` jako danych.** Jedyne dwa `[ebp+0x4]` /
  `[ebp]` w całym `ffe.asm`: 133075 (w `_SetJmp`, zastąpionym przez `amiga\probes\ffe_setjmp.s`)
  i 83193 (`lea ecx,[ebp+0x4]` w `FUNC_001330`, gdzie `ebp` jest wskaźnikiem danych, nie ramką).
- **Dostępy `[esp+d]`: 113 w 9 funkcjach**, 100 z nich w `FUNC_001330_DPalBuild` (bezramkowa, sloty
  lokalne) — zwykłe sloty, `frame.py` już je mapuje.
- **Ładowania `mov` pod żywymi flagami: 4; zapisy: 6; `push`/`pop` pod żywymi flagami: 0**
  (aproksymacja statyczna: konsument flag przed następnym setterem). Problem CCR przy swapie
  jest marginalny — ~10 miejsc w grze.
- **Adresy funkcji C w danych statycznych: 0** (`dd _X` nie występuje). Adresy funkcji pchane na
  stos: 38 `push dword FUNC_`, z czego 10 to callbacki dla `_BlitClipWrapper` (97056...97804), w tym
  **3 przetłumaczone** (`_BufferBlit`, `_BufferReverseBlit`, `_BufferMaskedBlit`) i 7 z C
  (`_VideoBlit`, `_VideoMaskedBlit`, `_VideoReverseBlit`).
- **Zapisy adresu funkcji do pamięci w czasie wykonania: 89** (`mov [..],FUNC_x`) — na 68k to
  relokowany immediate, więc swap musi być w czasie wykonania (3 instrukcje), nie w translatorze.
- **Zewnętrzne funkcje C wołane z przetłumaczonego kodu: 70** (z 85 `EXTERN`): 42 z `ffeapi.h`
  + libc (`_abs` 84 miejsc, `_strlen` 32, `_memcpy` 31, `_fread`/`_fwrite` 21+21, `_memset` 20,
  `_sprintf` 16, `_fopen` 11, `_strcat` 11, `_free` 13, `_malloc` 8 ...).
- **Niewyrównane przemieszczenia stałe** (proxy dla alternatywy XOR-3): wskaźnik dword 507/9 045
  (5,6 %), wskaźnik+indeks dword 28/310, stos dword 46/13 531, stos word 13/678, nazwane 5+5.
  Do tego nieznana liczba przesunięć obliczanych w locie (indeksy bajtowe).

---

## 2. Definicja modelu LE — inwariant i lista wyjątków

**Inwariant P0.** *Dla każdego adresu A, który przetłumaczony kod gry może odczytać lub zapisać
jako dane, bajt pod A ma tę samą wartość, jaką miałby pod A na x86 (po relokacji adresów).*

Z P0 wynika mechanicznie: load N-bajtowy = `move.N (ea),Dn` + odwrócenie bajtów; store =
odwrócenie + `move.N Dn,(ea)`; dostęp bajtowy = bez zmian; `rep movs*` = bez zmian (kopiuje
bajty); `lea` = bez zmian; wskaźnik w pamięci = 4 bajty w kolejności x86, po swapie jest
poprawnym adresem 68k.

**Wyjątki od P0 (komórki, których kod x86 NIGDY nie czyta jako danych — każdy z dowodem w 1.5):**

| wyjątek | dlaczego bezpieczny | kto go czyta |
|---|---|---|
| adres powrotu `jsr` | zero odczytów `[ebp+4]` poza `_SetJmp` (zastąpiony) | tylko `rts` |
| łącznik ramki `push ebp` -> `pea (a5)`, `pop ebp` -> `movea.l (sp)+,a5` | zero odczytów `[ebp+0]` | tylko para push/pop |
| pary `push reg`/`pop reg` callee-saved w prologu/epilogu, gdy żaden `[ebp-k]`/`[esp+k]` nie trafia w ten slot | `frame.py` zna wszystkie sloty funkcji | tylko para |
| **prywatne sloty lokalne**: każdy dostęp w funkcji w jednej szerokości, adres nie ucieka (`lea`, `push` adresu, `rep movs` przez ten slot) | dokładnie to liczy dziś `frame._slots()` + `scan_escaping_aggregates.py` | tylko ta funkcja |
| tabele skoków wbudowane w funkcje (`dc.l JUMP_x`, 32 funkcje) | czyta je tylko `lo_jmp` przez `movea.l (idx*4+tab),a0` | translator |
| obszary `movem.l` wrapperów C, sond, `FfeEnterLog` | symetryczne save/restore | 68k |
| zawartość bufora `SetJmp` (`DATA_009125/9126/9271`) | „Layout, ours alone" (`ffe_setjmp.s`) | nasz asm |
| bufory prywatne C (malloc w `ffe_cfg.c`, stan Paula, c2p) | gra dostaje tylko wskaźnik | C |

Wszystko poza tą tabelą — w tym **argumenty pchane przed `call`**, lokalne agregaty, arena
węzłów, rekordy obiektów, tablice `ffedat`, bytecode `ffebmp`, bitmapy, bufor ekranu, pliki
wczytane `fread` — jest LE.

**Bonus, którego dziś nie ma:** pliki gry (commander/save, `.raw`, config) są na dysku LE;
w modelu LE `fread` do pamięci gry jest poprawny bez żadnej konwersji (dziś każdy wczytany
rekord czytany jako dword byłby odwrócony, chyba że gdzieś jest ręczna konwersja).

---

## 3. Granice poprawności — odpowiedzi na pytania 1(a)–(g)

### (a) Wskaźniki w pamięci

- **Relokowane wskaźniki w obrazie danych** (742 `dd FUNC_` + 7 258 `dd DATA_` + support):
  **loader hunków AmigaDOS wpisuje adres jako natywny BE longword i nie potrafi inaczej.**
  Asembler też nie rozbije relokacji na bajty. Więc: `convert_static.py` emituje `dc.l SYM` jak
  dziś **plus tabelę offsetów tych slotów** (`ffe_le_fixups: dc.l off1, off2, ..., -1` per sekcja),
  a `ffe_game.c` przed `_asmmain` i przed każdym `ffe_*_init` czytającym symbole gry wykonuje
  `ffe_le_fixup()`: dla każdego offsetu odwraca 4 bajty w miejscu (68020 znosi nieparzyste
  adresy — `odd_longs` już dziś są w obrazie). Falsyfikator: po fix-upie zrzucić 20 slotów
  (`DATA_007816` — 32 handlery interpretera; `DATA_004681` — 465 deskryptorów) i porównać ze
  `.symmap`; dowolna niezgodność = tabela offsetów niekompletna.
- **Wskaźnik ładowany jako baza `An`**: `move.l (ea),d7` + swap + `movea.l d7,a0` — ta ścieżka
  już istnieje w `lo_mov` (gałąź `is_areg(d)` przy `sk`). +4 instrukcje na load wskaźnika.
- **Tabele skoków** (`dc.l JUMP_x` w ciele funkcji): zostają BE; `lo_jmp` przez `Mem` jest
  jedynym czytelnikiem (CFG dowodzi, że tabela jest własnością funkcji) i emituje natywny load.
  Wymaga to jednego znacznika: operand `Mem` pochodzący z `LOCAL_TABLES` -> `native`.
- **Adresy funkcji w strukturach gry** (deferred-draw, `[DATA_007629]`, `DATA_007816`): 89 sklepów
  `mov [..],FUNC_x` -> `move.l #FUNC_x,d7 / rol.w #8 / swap / rol.w #8 / move.l d7,(ea)`; 38
  `push FUNC_x` -> analogicznie na `-(sp)`; `call [mem]`/`call reg` -> load+swap+`jsr (a0)`
  (`lo_call` ścieżka `Mem`, +3). Inicjalizatory statyczne pokrywa fix-up z punktu 1. Spójne.
- **Adresy funkcji przetłumaczonych przekazywane do C** (3 callbacki `_Buffer*Blit` dla
  `BlitClipWrapper`, `_atexit` 1): C woła je natywnie, więc pchany musi być adres **wrappera
  C-ABI** (`emit.c_entry`, który w modelu LE dodatkowo swapuje kopiowane argumenty). Reguła
  w `lo_push`: `push dword <FUNC translated>` w miejscu, którego konsumentem jest EXTERN C
  -> adres wrappera. Uwaga na nazwę: `_BufferBlit` już ma podkreślnik, wrapper będzie
  `__BufferBlit` — do sprawdzenia w `emit.c_entry` (`"_" + fn.base`).

### (b) Stos

- **Argumenty**: caller pcha LE, callee czyta `[ebp+8+4k]` ze swapem. Jednolicie, bo miejsce
  `push` nie zna callee (1 385 wywołań pośrednich). `push reg` -> `move.l dN,d7 / swap-seq /
  move.l d7,-(sp)` (CCR: x86 `push` nie pisze flag; zmierzono **0** pushów pod żywymi flagami,
  audyt liveness i tak to złapie). `push imm` -> `pea (bswap(imm)).l` — **bez CCR, 1 instrukcja**,
  koszt zero. `push label` -> 4 instrukcje (relokacja w locie). `push dword [mem]` i
  `pop dword [mem]` **nie potrzebują żadnego swapu**, bo kopiują 4 bajty LE->LE.
- **`pop reg`** -> `move.l (sp)+,d7 / swap-seq / move.l d7,dN` (lub `movea.l`); **para
  prolog/epilog** (`push ebx...pop ebx`) — natywna, gdy `frame.py` dowiedzie, że slot nie jest
  adresowany (patrz wyjątki).
- **Sub-word na stosie** (1 438 byte + 678 word): przemieszczenie x86 **bez zmian**, word ze
  swapem, byte za darmo. Cała klasa „{short x,short y} na stosie", „RGB courier `[ebp-3]`",
  „word w dwordzie `[ebp-0x17]`", „flip na zapisany a5" (commit `cab165a`) **znika z definicji**.
- **Konwencja wywołań**: t-ABI bez zmian (rejestry jak dziś), zmienia się tylko układ bajtów
  slotów. `add esp,N` bez zmian.

### (c) Warstwa platformy C — kompletna lista interfejsów

| interfejs | dziś | w modelu LE |
|---|---|---|
| 42 funkcje `ffeapi.h` wołane ze stosu LE | `jsr _VideoBlit` | `jsr LE__VideoBlit` -> generowany thunk: kopiuje N argumentów ze swapem, `jsr _VideoBlit`, `lea (4N,sp),sp`, `rts`; d0 natywne |
| out-parametry C->gra: `InputMouseReadMickeys/Pos`, `InputJoyReadPos`, `SoundCheckInit` (3 x long*), `CfgGetKeyVal/Def` (int*), `DirFindFirst/Next/NavigateTree` (`FileInfo.type/size`) | natywny store | `LE32_PUT(p,v)` w 8 funkcjach (ffe_input.c x3, ffe_sound.c x1, ffe_cfg.c x2, ffe_dir.c x3); `pName[16]`, `InputKeybReadStates`, `VideoGet/SetPalValue` = bajty, bez zmian; `TimerGetTimeStamp` zwraca w d0 — bez zmian |
| `BlitClipWrapper` (C, 13 miejsc, callback) | natywne | thunk wejściowy jak wyżej; callback woła natywnie; przetłumaczone `_Buffer*Blit` przez wrapper C-ABI (patrz (a)) |
| `CfgStruct` (32 B w grze, wypełniany przez C) | gra tylko przekazuje wskaźnik | bez zmian, o ile gra nie czyta pól — sprawdzić grepem `pTmCfgStruct` (dziś widać tylko `push`) |
| libc (28 funkcji: `memcpy/memset/strlen/abs/fopen/fread/fwrite/sprintf/malloc/free/...`) | bezpośrednio | thunki generowane z tabeli liczby argumentów (`ffeapi.h` + prototypy libc); vararg (`sprintf/fprintf`) — thunk kopiuje stały max K=10 dwordów (nadmiar to nieszkodliwa kopia ze stosu wołającego) |
| zmienne współdzielone produkcyjne: `DATA_008804` (clock), `DATA_009156/9157`, `DATA_009158/9181` (bajty — ok), `DATA_007690`, `DATA_007691` (bajty — ok), `DATA_009208/9209` (be16 -> LE16), `DATA_007683` (hot-rects, longi), `ptrSavedX/Y`, `tmLastTime/tmMinFrame/tmNext50Hz`, `splashTimeout`, `DATA_007773` (deskryptor bufora), `DATA_009132`, `DATA_009200/9251` | natywne `extern long` | makra `LE32_GET/PUT`, `LE16_*` w `amiga\ffeapi\ffe_le.h`; ~15 symboli, ~40 miejsc w `ffe_video.c`/`ffe_input.c`/`ffe_timer.c` |
| sondy (`ffe_game.c` 44 symbole, `ffe_video.c` ~30) | natywne | te same makra; nie blokują kroku — mogą być poprawiane leniwie, ale **każda sonda nie przeniesiona czyta śmieci**, więc: grep `__asm__("DATA_` + `extern long` = lista do przejścia |
| `ffe_planet.c` (FUNC_001877): `Scr`, `long *p3`, `DATA_009262/63/65/67`, `DATA_008814..17`, wektory C `n3[3]` przekazywane do `_FUNC_001878/1879` | natywne | ~40 miejsc na akcesory; **wektory lokalne C oddawane grze muszą być zakodowane LE przed wywołaniem i zdekodowane po** — reguła: „bufor C oddany grze jest LE" |
| shimy x87 związane z gołymi nazwami (`FUNC_000833`, `FUNC_000112/1486/118/120`, `FUNC_CheckInputWrap`, `NullKill`) | C pod nazwą t-ABI, argumenty czytane natywnie | generowany „wejściowy thunk" per shim: swap argumentów -> `jmp` do C (lista w `massgen.py` już istnieje) |
| `_SetJmp/_LongJmp` (asm) | czyta `(4,sp)`/`(8,sp)` natywnie | 2 argumenty ze swapem (+6 instrukcji) |
| `FfeCrashDie`, `ffe_trap.s`, sampler VBlank | czytają PC/stos 68k | bez zmian (to są dane 68k) |
| oracle (`ffe_xlattest.c`, 27 `memcpy` pól, `DATA_007773`/`DATA_007683` definiowane po stronie C) | natywne | definicje LE (`FfeBufDesc` zakodowany), odczyty pól przez `le32()`; wejście przez `_FUNC_` (wrapper swapuje) — **harness porównuje LICZBY, więc test swapów jest bezpośredni** |

### (d) Konwencja C <-> kod przetłumaczony

- **C -> gra**: `_FUNC_x` (`emit.c_entry`) kopiuje argumenty `move.l (disp,sp),-(sp)`; w LE:
  `move.l (disp,sp),d7 / rol.w #8,d7 / swap d7 / rol.w #8,d7 / move.l d7,-(sp)` (+3 linie na
  argument, 1 zmiana w `emit.py`). Wartość zwrotna w d0 — natywna, bez zmian.
- **gra -> C**: `LE__X` thunk (generowany `tools\translator\gen_thunks.py` -> `build\xlat-full\
  c_thunks.s`) o tej samej strukturze; `lo_call` w trybie LE mapuje `call _X` (cel na liście
  EXTERN) na `jsr LE__X`. Wskaźniki w argumentach to adresy 68k (po swapie) — poprawne.
- **Bufory**: bajtowe (ekran, próbki, stringi, klawisze) — bez zmian; słowa/longi w buforach
  współdzielonych — LE po obu stronach (akcesory w C).

### (e) Flagi

- `rol.w`/`swap` piszą N/Z/V/C, **nie X** (M68000PRM; już zacytowane w `LE_SWAP_NOTE`), więc
  łańcuchy `add/mov/adc` przeżywają.
- Zmierzone: 4 loady, 6 store'y, 0 push/pop pod żywymi flagami (statycznie). Dla nich:
  prymityw `move ccr,d6` ... `move d6,ccr` (68020 ma MOVE from CCR; koszt ~8 cykli), już
  wymieniony jako trzecia droga w komunikacie odmowy `lower()` („move ccr,d7 / move d7,ccr").
- RMW na pamięci pod żywymi flagami (`dec dword [x] / jnz`, `sar dword [x],1 / jl` — częste):
  load+swap -> op w d7 (flagi dokładne) -> `move ccr,d6` -> swap-back+store -> `move d6,ccr`.
  Tańsza droga tam, gdzie żywe jest tylko Z/N: `tst.l d7` po store (N/Z odtworzone, V/C=0) —
  liveness decyduje.
- `cmp [mem],imm` z żywym wyłącznie Z: `cmpi.N #bswap(imm),(ea)` — 1 instrukcja, bez swapu
  (równość jest niezmiennicza względem permutacji bajtów). Dla N/C — load+swap jak dziś.
- Test bitowy `test dword [mem],imm` z maską w jednym bajcie -> `btst`/`and.b` na właściwym
  bajcie LE (mechanizm „narrow to one byte" z `_logic` już istnieje dla `or/and/xor`).

### (f) Kształty instrukcji — jak każdy się obniża

| kształt | x86 miejsc | obniżenie w LE | koszt (instr.) |
|---|---|---|---|
| `mov r,[m]` / `mov [m],r` 32-bit | ~19 000 / ~9 500 | istniejąca ścieżka `le` | +3 |
| `mov r,[m]` 16-bit | część z 3 803 | `move.w`+`rol.w #8` (istnieje) | +1 |
| `mov [m],imm` | — | stała odwrócona w translatorze (istnieje) | 0 |
| bajtowe | 6 267 | bez zmian | 0 |
| `movsx/movzx r,word [m]` | ~1 500 | istnieje (site path) | +1 |
| `movsx/movzx r,byte [m]` | ~360+ | bez zmian | 0 |
| `cmp r,[m]`, `cmp [m],r`, `cmp [m],imm` | 3 166 | istnieją (site paths) + `cmpi` Z-only | +3 / +0 |
| `add/sub/and/or/xor/imul r,[m]` | ~2 000 | istnieją dla nazwanych `le` — uogólnić na wskaźnik/stos | +3 |
| `add/sub/inc/dec/neg/not/or/and/xor/sar/shl/shr [m]` (RMW) | ~2 000 (część bajtowa) | wzorzec z `_addsub` (load+swap+op+swap+store) uogólnić; single-byte narrow | +6 (+CCR gdy żywe) |
| `push reg` / `pop reg` | 14 777 / 6 400 | swap w d7; pary prolog/epilog natywne | +3 / 0 |
| `push imm` | 3 400+ | `pea` odwróconej stałej | 0 |
| `push [m]`, `pop [m]` | ~1 000 | kopiowanie 4 bajtów bez swapu | 0 |
| `push label`, `mov [m],label` | 38 / 89 | swap w locie | +3 |
| `call [m]`, `call r`, `jmp [tab]` | 1 385 / 35 | load+swap+`jsr (a0)`; tabela własna -> natywnie | +3 / 0 |
| `rep movsb/d`, `movs*`, `repne scasb` | 229 | bez zmian (bajty) | 0 |
| `xchg` z pamięcią | 0 | — | — |
| `lea` | 3 744 | bez zmian | 0 |
| 64-bit (`Int64*`): `[edx]`,`[edx+4]` | kilkadziesiąt | dwa swapowane dwordy; X przeżywa | +3 każdy |
| `imul/idiv/div r,[m]` | 471 | load+swap do d7, potem `muls.l d7` | +3 |
| `test [m],imm` | 348 | byte -> 0; dword -> narrow lub load+swap | 0/+3 |

### (g) Porty ręczne x87 i oracle

- 6 funkcji x87 w C czyta pamięć gry przez wskaźniki -> akcesory LE (3c). `FUNC_001877`
  (`ffe_planet.c`, 29 KB) to największa praca: ~40 miejsc. Ich argumenty przychodzą LE ->
  generowany thunk wejściowy.
- Oracle x86 (`tools\oracle\oracle.c`) — **bez zmian** (to strona x86). `ffe_xlattest.c`: 27
  `memcpy` przez `le32()`, 2 definicje tabel LE, wejścia przez `_FUNC_` — wrapper swapuje.
  **Zmiana modelu nie zmienia ani jednego wektora**: te same liczby po obu stronach, więc
  `compare.py` jest kompletnym testem poprawności swapów dla 98 nośników.

---

## 4. Koszt

### 4.1 Model cykli (68020, przypadek z cache, wg tabel MC68020UM rozdz. 9; **do zmierzenia, nie zaufać**)

`move.l (d16,An),Dn` ok. 7; `rol.w #8,Dn` ok. 4; `swap Dn` ok. 4 -> swapowany load dworda ok. 19
(**ok. 2,7x**), word ok. 11 (1,6x), bajt 1x. Store analogicznie (swap w kopii d7: +4 instr.). Push
rejestru: 1 -> 5 instrukcji. Źródło tabel: MC68020 User's Manual (NXP, `MC68020UM.pdf`,
https://www.nxp.com/docs/en/data-sheet/MC68020UM.pdf, rozdział 9 „Instruction Timing"); wartości
są przybliżone i **muszą** zostać zmierzone samplerem VBlank (`PROGDIR:ffe_sample`) — tak jak
każde inne twierdzenie w tym projekcie.

### 4.2 Statycznie

Z census 1.3: dostępów 2/4-bajtowych do pamięci ~32 000 + push/pop ~24 500 = ~56 500 miejsc
na 121 831 instrukcji x86 (46 %). Po wyjątkach (pary prolog/epilog ok. 2x3 na funkcję x 1 620 ok.
9 700; `push imm` 3 400; `push/pop [m]`; prywatne sloty lokalne — szacunkowo 40–60 % z 13 991
stosowych dwordów) zostaje ~30 000–35 000 swapowanych dostępów x ~3 instrukcje = **+90 000...
105 000 instrukcji statycznie na 144 225 dziś (+60...70 %)**; z mitigacją (i) w pełni i `cmpi`
Z-only bliżej **+25...40 %**.

### 4.3 Dynamicznie — realistyczny przedział

Gorące pętle są bajtowe (1.4), a setupy spanów to sloty stosu, w większości prywatne
(**mitigacja (i) trafia dokładnie tam**). Interpreter modeli i transformacje wierzchołków
(`FUNC_001478`: 406 `.l`) czytają rekordy przez wskaźniki — tu koszt jest pełny. Szacunek:
**1,2x–1,6x wolniej** w kodzie przetłumaczonym bez (iii); po natywnym przepisaniu ~20 najgorętszych
liści (oracle-zielone, więc bezpieczne) **ok. 1,0–1,15x**. Miara: `presents=` z `ffe_dump.log` na
ustalonym keyingu (`-Keys "21,21"`, ten sam czas ściany) plus lista gorących translacji z samplera
VBlank — przed (krok 0) i po (krok 6).

### 4.4 Mitigacje, w kolejności opłacalności

1. **(i) Prywatne sloty stosu natywne** — `frame.py` już liczy szerokości per slot; dodać „adres
   nie ucieka" (`scan_escaping_aggregates.py` ma logikę) i „nie jest argumentem/zapisanym
   rejestrem". Zbiera większość z 34 591 stosowych `.l` w 68k. Dowód poprawności: slot
   prywatny widzi tylko własna funkcja, w jednej szerokości -> układ bajtów jest obojętny.
2. **`pea` odwróconej stałej** dla `push imm` (3 400+ miejsc, zero kosztu) i `cmpi #bswap`
   dla Z-only (`cmp [m],imm` + `je/jne` — częsty idiom).
3. **(ii) Etykiety statyczne jednoszerokościowe natywne** — możliwe, ale **niezalecane**: to
   dokładnie przywrócenie manifestu i klasy „`plain` vs `le`" (bug `DATA_008818`: 3 bajty +
   flaga). Zysk mały (nazwane dwordy to 4 628 miejsc), ryzyko to cały dzisiejszy problem.
   Zostawić na później, tylko dla etykiet czytanych **wyłącznie** przez nazwę (bez `lea`,
   bez `push label`, bez arytmetyki między etykietami) i tylko po pomiarze.
4. **Peephole**: load+swap+store bez użycia rejestru (`mov eax,[a] / mov [b],eax`) -> kopiowanie
   bajtów bez swapów; swap+swap tego samego rejestru bez użycia -> usuń oba.
5. **(iii) Natywne przepisanie top-N liści** (DrawHLine, DrawSpans, Tex*, `FUNC_001521` inline
   przy 441 wywołaniach, `Sin16/32`) — każda trzymana do oracle jak dziś, **z zachowaniem
   konwencji LE w pamięci** (w pętli pikselowej i tak są bajty; swapy tylko w setupie).

---

## 5. Alternatywy — uczciwe porównanie

### (A) Status quo + automatyczna detekcja (instrumentowany bieg PC loguje każdy dostęp)

Co daje: klasę szerokości per (etykieta / offset w rekordzie / slot ramki) z prawdziwego
biegu — „runtime half" pipeline'u, którego nigdy nie napisano. Dlaczego **nie** rozwiązuje
problemu: (1) dzisiejsze błędy to **miejsca o mieszanej szerokości** (courier RGB, `desc+0x14`
dword/word, `DATA_008818` 3 B + flaga, packed float word-w-dwordzie) — log potwierdzi, że są
mieszane, i nadal trzeba będzie swapować przy jednym z dostępów, czyli **generować tabele
per-miejsce, tylko większe**; (2) pokrycie = tylko ścieżki przebiegnięte (scenariusz 0 nie
wchodzi w handel, walkę, mapę); (3) narzędzie samo w sobie: instrumentacja ~40 000 operandów w
kopii `ffe.asm` (mechanizm `build-pc-probe.py` jest, ale per-instrukcja), log rzędu 10^8 zdarzeń,
klasyfikator, generator tabel — porównywalna praca z modelem LE, a wynik to ta sama krucha
architektura. **Warto zbudować później jako narzędzie diagnostyczne** (np. porównanie
zawartości pamięci PC vs Amiga w danym punkcie), nie jako fundament.

### (B) „Adres XOR 3" (dwordy natywne, bajt pod `a^3`, słowo pod `a^2`)

Zalety: loady/store'y dwordów darmowe. Wady tu dyskwalifikujące: (1) **każdy dostęp bajtowy i
słowny kosztuje instrukcję na adresie** (`eor` na kopii wskaźnika; 6 267 bajtowych + 3 803
słownych statycznie, plus wszystkie pętle `rep movsb`, `repne scasb`, kopiowanie stringów);
(2) **niewyrównane dwordy łamią model**: 507 stałych niewyrównanych przemieszczeń przez
wskaźnik (5,6 %), 46 na stosie, plus wszystkie obliczane w locie (`[ebp-3]`, `[ebp-0xf]`,
`[ebx+0x27f]`, packed float `+2`) — dword pod `a` z `a%4<>0` nie jest w tym modelu jednym
polem, tylko kawałkami dwóch, i trzeba go składać z 4 bajtów; (3) **każdy bufor bajtowy
przekazany do/z C jest poprzestawiany**: bufor ekranu 320x200 (c2p, RTG copy), stringi do
`fopen/strcat/DirMake*`, tablica klawiszy, próbki, bitmapy z `ffebmp`, pliki z `fread` — C
musiałoby w całości mówić „XOR-3"; (4) `rep movsd` między buforami o różnym `a%4` przestaje
być kopią bajtów. **Odrzucone.**

### (C) Hybryda: LE dla heap/stos/danych surowych, BE dla tabel „tylko-kod"

To jest dokładnie rekomendowany model (2): LE domyślnie, wyjątki tylko tam, gdzie czytelnik
jest **wyłącznie 68k** (translator, wrapper, rts) lub slot jest **dowiedzenie prywatny**. Każdy
dodatkowy wyjątek „bo ta tabela statyczna jest czytana tylko jako dword" to powrót do manifestu
i musi mieć własny falsyfikator (4.4 pkt 3).

**Rekomendacja: (C) = LE z zamkniętą listą wyjątków.**

---

## 6. Plan migracji — małe, weryfikowalne kroki; gra uruchamialna i oracle zielony po każdym

Zasada: **przełącznik trybu, nie widelec**. `FFE_MEM_LE=1` w środowisku (jak `FFE_NO_SHIFT_MASK`)
wybiera reguły LE w `lower.py`/`frame.py`/`emit.py`, tryb konwertera i zestaw thunków. Stary
tryb zostaje do bisekcji, aż krok 5 go usunie. **Backup zip / tag przed każdym krokiem.**

### Krok 0 — linia bazowa (0,5 dnia)
- `python tools\translator\compare.py` -> zapisać `IDENTICAL: 5451`; zachować `build\xlat-full\*.o`
  (do diffu po rozmiarach, metoda z 2026-08-06) i `.symmap`.
- Trzy biegi `autokey_run.ps1 -Keys "21,21"` na ustalony czas ściany: `presents=`, `TRAP`,
  `ffe_frame.raw`; PC: `run-pc-intro.ps1` (klatka odniesienia). Sampler VBlank: top-30 translacji.
- Tag `pre-le-model`.

### Krok 1 — translator w trybie LE, oracle-zielony na 98 nośnikach (3–5 dni)
Pliki: `tools\translator\lower.py` (+~600/-~250 linii), `frame.py` (klasyfikator prywatności
slotów ~150 linii; flipy pod `if not LE`), `emit.py` (`c_entry` +3 linie/arg; `LOCAL_TABLES`
tag native), `storage.py` (`kind()` -> `le` w LE), `parse.py` (bez zmian).
1. `_swap_kind()`: w LE zwraca `le` dla każdego `Mem` 2/4-bajtowego, chyba że `o.native`
   (ustawiane przez `frame.apply()` dla prywatnych slotów/łącznika ramki i przez emit dla
   tabel własnych). `_site_swap`, `lo_byte_site`, `_static_disp` -> identyczność.
2. Nowe/uogólnione kształty (lista 3f): RMW przez wskaźnik/stos, `push/pop reg`, `push imm` ->
   `pea`, `push/pop [m]` bez swapu, `push label`/`mov [m],label` swap w locie, `call/jmp` przez
   pamięć, `imul/idiv/div` z pamięci, `test [m]`.
3. Prymityw CCR (`move ccr,d6 / move d6,ccr`) jako czwarta droga w `lower()` zamiast odmowy,
   **tylko** gdy liveness tego wymaga (obiekty dead-flag zostają bajtowo identyczne z krokiem 0
   w trybie BE — to test regresji trybu BE).
4. **Audyt LE** w `lower()`: każdy operand `Mem` 2/4-bajtowy w wyemitowanych liniach ma
   `rol.w/swap` albo tag `native`; w przeciwnym razie odmowa (lustro dzisiejszego audytu).
5. `ffe_xlattest.c`: definicje `DATA_007773`/`DATA_007683` LE, `le32()` na 27 `memcpy`.
6. Pętla: `translate.py` nośników pod `FFE_MEM_LE=1` -> `build-amiga.sh ffe_xlattest` ->
   `run_probe.ps1` -> `compare.py`. **Kryterium: IDENTICAL 5451.**
Falsyfikatory: rekord różny w funkcji, której x86 nie zmienił -> reguła kształtu w `lower.py`;
odmowa audytu -> brakujący kształt; **jeśli po tygodniu więcej niż ~10 % nośników nie da się
uzielenić, model ma lukę konstrukcyjną, nie implementacyjną — zatrzymać i opisać.**

### Krok 2 — obraz danych LE + fix-up wskaźników (1 dzień)
Pliki: `tools\datamap\convert_static.py` (tryb LE: `db/dw` surowo, `dd num` -> 4 bajty LE,
`dd SYM` -> `dc.l SYM` + offset do `ffe_le_fixups_<sekcja>`), `extract_support.py` (to samo),
`amiga\probes\ffe_le_fixup.c` (~40 linii), wywołanie w `ffe_game.c` przed `_asmmain` i
`ffe_*_init`. Manifest emituje `le` dla wszystkiego (translator w LE go ignoruje).
Falsyfikator: zrzut 20 slotów po fix-upie vs `.symmap` (`DATA_007816[0..31]`, `DATA_004681`),
plus licznik „slotów naprawionych" = 742 + 7 258 + support; różnica = niekompletna tabela.

### Krok 3 — granica z C: thunki, akcesory, shimy (2–3 dni)
Pliki: nowy `tools\translator\gen_thunks.py` -> `build\xlat-full\c_thunks.s` (70 wejść z
tabelą liczby argumentów: `ffeapi.h` + prototypy libc; vararg K=10) i wejścia dla 8 shimów
C pod gołymi nazwami; `lo_call` w LE: `call _X` z listy EXTERN -> `jsr LE__X`; `lo_push` adresu
funkcji przetłumaczonej dla konsumenta C -> wrapper. `amiga\ffeapi\ffe_le.h` (makra);
`ffe_input.c`, `ffe_sound.c`, `ffe_cfg.c`, `ffe_dir.c`, `ffe_timer.c`, `ffe_video.c`
(zmienne współdzielone, ~40 miejsc produkcyjnych); `ffe_setjmp.s` (+6 linii);
`ffe_planet.c` (~40 miejsc + kodowanie wektorów lokalnych); `ffe_game.c` shimy x87.
Sondy: lista z `grep __asm__("DATA_` — przenieść te, które są w aktywnym użyciu (`--bodylist`,
`--optrace`, `MODSEQ`, `PALMAP`, `RGBMAP`, `BLITMAP`); reszta może odmawiać pod `#if !LE`.
Falsyfikator: `ffe_apitest`/`ffe_drawtest` (istniejące próbki platformy) zielone; potem krok 4.

### Krok 4 — pełna gra w LE (1–2 dni biegów)
`massgen.py` pod `FFE_MEM_LE=1` -> `build-amiga.sh ffe_game` -> `watch_ffe.ps1`/`autokey_run.ps1`.
Kryteria, po kolei (pierwsza rozbieżność, nie najciekawsza): splash -> 'y' -> intro (klatki
`ffe_seq` vs `run-pc-intro.ps1`, obie **obejrzane**), `--bodylist 880` = 27 ciał identycznych
ze stringiem referencji, HUD `Hope`, `presents` >= linia bazowa (na czas ściany!), `OPSEQ`/`MODSEQ`
vs `PCOPSEQ`. Regresja względem kroku 0 w którymkolwiek punkcie lokalizuje się metodą „diff map
linkera po rozmiarach + która funkcja" jak 2026-08-06.
**Oczekiwanie, którego nie wolno zakładać, tylko sprawdzić**: crash TRAP 5 w `FUNC_001876`
(`P0=P1=P2`) może zniknąć, bo jest w tej samej rodzinie (rekordy wierzchołków przez wskaźnik).
Jeśli nie zniknie — to jest NOWA informacja: divergencja opcodów nie jest kwestią bajtów.

### Krok 5 — usunięcie tabel i trybu BE (0,5 dnia, po N>=3 zielonych biegach)
Usunąć: `le_read_sites.tsv`, `lo_byte_sites.tsv`, `PROVEN_SUBWORD`, `PROVEN_BY_CALLERS`,
`BE32_MEMBER`, `_static_disp`, `storage.resolve`, `overrides*.tsv`, `staticscan/wordscan/
streamscan/addrscan*.py`, `scan_arg_slots.py`, `scan_packed_locals.py`; `frame.py` zostaje jako
„width map + private slots". Zaktualizować skill `ffe-translator` i `CLAUDE.md` (sekcja
„sub-word stack rule" staje się historią). Tag `le-model-landed`.

### Krok 6 — pomiar i mitigacje (iteracyjnie, każda z osobnym pomiarem)
Kolejność 4.4: (i) prywatne sloty (może już w kroku 1 — wtedy tu tylko zaostrzenie),
`pea`/`cmpi`, peephole, (iii) natywne liście przez oracle. Miara jak w kroku 0. **Próg
decyzji**: jeśli po (i)+(ii) gra jest > 1,5x wolniejsza niż linia bazowa **i** sampler wskazuje
swapy na stosie jako dominantę — rozważyć wariant „stos natywny + `frame.py` tylko dla
argumentów" (kompromis: wraca połowa klasy błędów, ale tylko na stosie i tylko sub-word).

### Rozmiar zmian (szacunek)
`lower.py` +-850 linii; `frame.py` +-300; `emit.py` +-40; `storage.py` -150; `convert_static.py`
+-120; `extract_support.py` +-40; `gen_thunks.py` +150 (nowy); `ffe_le_fixup.c` +40 (nowy);
`ffe_le.h` +30 (nowy); platforma C ~120 linii w 6 plikach; `ffe_planet.c` ~60; `ffe_xlattest.c`
~60; `ffe_setjmp.s` +6; `massgen.py` +10; `build-amiga.sh` +15 (c_thunks, fixup). Usunięć: ~2 500
linii i ~250 KB tsv.

---

## 7. Ryzyka i falsyfikatory

| # | ryzyko | jak się objawi | falsyfikator / obrona |
|---|---|---|---|
| 1 | **Fix-up wskaźników niekompletny** (slot `dd SYM` w `split`/support poza tabelą; slot naprawiony dwa razy) | dziki wskaźnik przy pierwszym użyciu tabeli — jak `DATA_007811` (bez logu) | licznik naprawionych = liczba `dd SYM` z konwertera; zrzut 20 slotów vs `.symmap`; fix-up idempotentny przez flagę „już wykonany" |
| 2 | **C czyta/pisze pamięć gry natywnie** (sondy, shimy, `ffe_planet.c`, out-parametry) | wartości x2^24, „nic nie biegnie", fałszywe znaleziska — dokładnie rodzaj błędu, który sondy już tu produkowały | grep `__asm__("DATA_` + `extern long/short` = lista; każde miejsce albo makro `LE32_*`, albo `#error` pod LE; paired probes PC vs Amiga jako kontrola |
| 3 | **Wydajność** poniżej progu grywalności na 68020 | `presents` na czas ściany < 0,6x bazy po (i)+(ii) | pomiar w kroku 0 i 6; sampler VBlank; próg decyzji z kroku 6 |
| 4 | **CCR zniszczone przez swap pod żywą flagą** | oracle wyłapie w nośnikach; poza nimi — błędna gałąź | audyt liveness już odmawia; prymityw CCR; 0/4/6 zmierzonych miejsc |
| 5 | **Adres funkcji przetłumaczonej trafia do C bez wrappera** (`BlitClipWrapper`, `atexit`) | blit z argumentami x2^24 -> zapis poza buforem | `lo_push` label -> wrapper gdy konsument EXTERN; test `ffe_drawtest` |
| 6 | **Vararg thunk** kopiuje za mało argumentów do `sprintf` | urwane stringi debug | K=10 > maksimum w grze (zmierzone `add esp,20` = 5 argumentów) |
| 7 | Regresja trybu BE podczas kroku 1 (przy współdzielonych regułach) | `.o` nośników zmienione | diff bajtowy `.o` z kroku 0 przy `FFE_MEM_LE` niewłączonym |
| 8 | `SetJmp` przez stos LE | `LongJmp` wraca w złe miejsce | 2 argumenty ze swapem; test: licznik `_ffe_longjmp_n` jak dotąd |

---

## 8. Co ten plan NIE ustala (i co by ustaliło)

- Rzeczywisty koszt dynamiczny — tylko pomiar (krok 0/6).
- Czy TRAP 5 / divergencja opcodów w modelu 360 jest z tej rodziny — krok 4 to pokaże; nie
  zakładać.
- Liczbę slotów stosu, które (i) zostawi natywnymi — policzy nowy klasyfikator w `frame.py`
  (można to zmierzyć **przed** implementacją reguł: dodać tryb `--report` liczący sloty
  jednoszerokościowe nieuciekające; koszt 1 h, a decyduje o połowie szacunku kosztu).
- Czy jakikolwiek kod gry czyta `CfgStruct`/`FileInfo` polami — grep `pTmCfgStruct` i
  odbiorców `DirFind*` (1 h).

*Ścieżki: `I:\GITHUB\Amiga_Frontier_2_First_Encouters\tools\translator\lower.py`,
`...\tools\translator\frame.py`, `...\emit.py`, `...\storage.py`, `...\massgen.py`,
`I:\GITHUB\Amiga_Frontier_2_First_Encouters\tools\datamap\convert_static.py`,
`...\tools\datamap\extract_support.py`, `...\le_read_sites.tsv`, `...\lo_byte_sites.tsv`,
`I:\GITHUB\Amiga_Frontier_2_First_Encouters\amiga\ffeapi\*.c`,
`I:\GITHUB\Amiga_Frontier_2_First_Encouters\amiga\probes\ffe_game.c`, `...\ffe_planet.c`,
`...\ffe_setjmp.s`, `...\ffe_xlattest.c`,
`I:\GITHUB\Amiga_Frontier_2_First_Encouters\build\xlat-full\`, `...\build\data-be\`,
`I:\GITHUB\Amiga_Frontier_2_First_Encouters\build\build-amiga.sh`.*