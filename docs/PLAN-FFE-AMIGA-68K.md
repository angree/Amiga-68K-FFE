# PLAN inżynierski: port FFE na Amigę 68k

Data: 2026-07-28. Autor: fable-specialist (na zlecenie).
Dokument wykonawczy do analizy `I:\GITHUB\Amiga_Frontier_2_First_Encouters\docs\RESEARCH-FFE-AMIGA-68K.md`.
Strategia jest przesądzona: **statyczny translator x86 asm → 68k asm**, z ręcznym 68k dla gorących
funkcji rasteryzatora w późnej fazie. Ten plan zamienia tę decyzję w kroki, narzędzia i bramki.

Wszystkie liczby w §1 zostały **zmierzone w tej sesji** skryptem
`C:\Users\Admin\AppData\Local\Temp\claude\I--GITHUB-Amiga-Frontier-2-First-Encouters\0037ba20-a9c9-4c39-bbb5-72ceed74eb56\scratchpad\plan_measure.py`
na `I:\GITHUB\Amiga_Frontier_2_First_Encouters\ref\jjffe\ffe.asm` (nie są przepisane z research doc).

---

## 0. Zakres i stan wejściowy

- Wejście: `ref\jjffe\ffe.asm` (113 092 instrukcje wg naszego parsera; doc mówi 113 133 — różnica
  41 to kwestia liczenia, bez znaczenia), `ffedat.asm`, `ffebmp.asm`, warstwy `sdl\`/`win32\`/`linux\`.
- Wyjście: `ffe.exe` dla AmigaOS 3.x, 68020+, `-msoft-float` po stronie C, dwa buildy
  (patrz §3.4: **020/030/040** i **060**), wymagający własnych plików danych gracza.
- Kod własny: MIT, pisany od zera. Niczego nie modyfikujemy pod `ref\` ani w `I:\GITHUB\Amiga_OpenTTD\`.
- Host: Windows 10 + WSL (bebbo `/opt/amiga/bin`), asembler docelowy: `m68k-amigaos-as` (ta sama
  ścieżka, którą OpenTTD składa `c2p_glue.s`), vasm jako zapas.

### Korekty względem RESEARCH-FFE-AMIGA-68K.md (znalezione przy pomiarach)

| # | teza w research doc | stan faktyczny (zmierzony) | waga |
|---|---|---|---|
| K1 | „~45 mnemoników" | **87 różnych mnemoników** (w tym 20 x87, kombinacje `rep`, 9 wariantów `setcc`); rdzeń całkowitoliczbowy ~60 | mała — nadal mało |
| K2 | „cały interfejs to 40 funkcji, nic więcej" | 40 z `ffeapi.h` **+ 35 funkcji libc** (`_fopen/_fread/_fwrite/_fprintf/_sprintf/_vfprintf/_malloc/_free/_sqrt/_signal/_system/_atexit/...`, `ffe.asm:1684-1717`) **+ 5 pomocniczych** (`_CfgOpen/_CfgClose/_CfgFindSection/_CfgGetKeyVal/_BlitClipWrapper`, `ffe.asm:1781-1787`). Asm **eksportuje** też `_asmmain`, `_BufferBlit/_BufferMaskedBlit/_BufferReverseBlit`, `_strncpyfill`, `_SetJmp`, `_LongJmp` | średnia — powierzchnia portu jest większa, ale to nadal libc |
| K3 | „`muls.l` 2 cykle na 68060" | prawda tylko dla formy 32→32. **Forma 64-bitowa `muls.l <ea>,Dh:Dl` oraz `divs.l/divu.l <ea>,Dr:Dq` (64/32) są NIEZAIMPLEMENTOWANE na 68060** — trapują do emulacji w 68060.library (Motorola 68060SP), dziesiątki–setki cykli. A dokładnie na nich stoi fixed-point FFE. Wymusza dwa buildy albo dispatch (§3.4) | **duża** |
| K4 | „9 prefiksów segmentowych przy rep movs" | 9 operandów `es:` łącznie, ale **2 w kodzie rysowania fontów** (`ffe.asm:94162`, `97400` — zwykłe store'y, flat model) i **7 w ręcznie pisanym `_LongJmp`** (`ffe.asm:133093+`). Przy `rep movs` — zero | mała |
| K5 | „tablice skoków: kilkanaście, zawartość w ffedat.asm" | **35 tablic** (`jmp near [reg*4+DATA_0000xx]`), zawartość **inline w `ffe.asm`** jako bloki `SECTION .data` z `dd JUMP_xxx` (np. `ffe.asm:1840`), w pełni symboliczna | mała |
| K6 | endianness danych statycznych jako główne ryzyko | potwierdzone, ALE research doc **nie wymienia plików zewnętrznych**: `mission.dat` (fopen w grze, `ffedat.asm:3028`; offsety journala w `DATA_002292`), `cabin%i.bmp` (`ffedat.asm:18470`), save'y commanderów — są little-endian na dysku i czytane przetłumaczonym (big-endianowym) kodem. Nowy podproblem §6.5 | średnia |
| K7 | — | teksty misji są **wkompilowane w `ffedat.asm`** (np. linie 893–965); `mission.dat` służy głównie journalowi | info |

Nic z powyższego nie podważa strategii — K3 i K6 zmieniają szczegóły planu i są w nim uwzględnione.

---

## 1. Zmierzone fakty, na których stoi plan

Pomiar z 2026-07-28, `plan_measure.py` (pełny output w scratchpadzie):

| właściwość | wartość | konsekwencja |
|---|---|---|
| instrukcje / funkcje | 113 092 / 1 621 `FUNC_` + 7 etykiet `_...` | skala translatora |
| pośrednie `call` | **1 344** `call [DATA_xxxx(+disp)]`, **12** `call [ebp±disp]`, **27** `call [reg]`, **2** `call [ebx+0x4]` | §5 |
| pośrednie `jmp` | **35**, wszystkie `jmp near [reg*4+DATA_0000xx]` z bounds-checkiem (`cmp`+`ja`) przed | §5 |
| `FUNC_` poza call/jmp/mov/push | **3**: `cmp eax,FUNC_001722` (×2), `cmp ecx,FUNC_001842` — porównania równościowe wskaźników | bezpieczne po translacji |
| skoki między funkcjami | **0** | jeden `.s` na funkcję jest legalny |
| funkcje bez standardowego prologu | 96 (małe leaf-settery + kilka używających ramki wołającego, np. `FUNC_000201`, `ffe.asm:16863`) | translator NIE zakłada ramek — tłumaczy mechanicznie |
| hazardy flagowe: instrukcja x86-neutralna między setterem a konsumentem | **7 miejsc** w całej grze (5× `mov` w łańcuchach add/adc 64-bit, 1× `cmp;mov;jc` @74788, 1× `dec;mov;jnz` @98023) | §4: eager flags wygrywa |
| `inc/dec` którego stary CF konsumuje później ktokolwiek | **0** | `addq/subq` legalne wszędzie |
| `cmp/test` zasilające `adc/sbb` (luka X-flag na 68k) | **13**, wszystkie to idiom `cmp r,1; sbb r,r` | jeden peephole §4.3 |
| `adc/sbb` po którym `jz/jnz` | **0** | semantyka Z przy `addx` bez znaczenia |
| rejestry high-8 (`ah/bh/ch/dh`) | **22 użycia**, 21× `test rh,maska-1-bitowa`, 1× `add [eax+0x6a],ah` | `btst #n+8` + 1 sekwencja specjalna |
| operandy 16-bit | 2 041 | `.w` natywnie |
| `movsx/movzx` | 2 119 / 421 | `extb.l/ext.l` / `moveq #0`+`move.b` |
| `idiv` | **216, wszystkie poprzedzone `cdq`** | wszystkie → `divsl.l` (32/32 z resztą, **zaimplementowane na 060**) |
| `div` | 45: 39 po wyzerowaniu edx → `divul.l`; **6 prawdziwych 64/32** (timing/init, zimne ścieżki) | 64/32 tylko na zimnych ścieżkach + w `FUNC_001523` |
| `imul` | 281 1-op (64-bit wynik), 461 2-op, 53 3-op | 2/3-op → `muls.l` 32-bit (060-safe); 1-op z martwym edx → też 32-bit; reszta → forma 64-bit / helper 060 |
| shifty | `by 1`: 447, `by 2-8`: 1 205, `by 9-31`: ~957, `by cl`: 535 | >8 wymaga `moveq #n,d7` + shift |
| `rep movsd/movsb` | 151 / 40; `repne scasb`: 38; brak `stos`, **brak `cld/std`** (kierunek zawsze w górę) | pętle inline §3.6 |
| x87 | **144 instrukcje w 7 funkcjach**: `FUNC_001877` (97), `FUNC_000112` (16), `FUNC_002110` (10), `FUNC_000118` (8), `FUNC_000120` (7), `FUNC_002095_fstore` (4), `FUNC_000833` (2, jedyny `call _sqrt` @56840) | 7 funkcji pisanych ręcznie, translator nie zna x87 |
| operandy pamięciowe wg bazy | `ebp` 17 907; `ebx` 5 740; abs 5 477; `eax` 4 636; `esi` 3 121; `edx` 1 704; `edi` 1 328; `ecx` 488; dwurejestrowe ~1 100; `esp` 113 | §3.1: koszt bazy w Dn ≈ 13k miejsc, cache a0/a1 |
| skalowanie indeksu | `*4` 1 109, `*8` 508, `*2` 364 | tryb 68020 `(bd,An,Xn*s)` pokrywa wszystko |
| `setcc` | 114 (`setg` 60, `setl` 12, `setng` 13, `setz` 10, ...) | scc daje 0xFF nie 1 → korekta §4.4 |
| `xchg` | 40 | `exg` (oba bez flag) |
| martwe artefakty | `pushf` 1 + `iret` 1 + `cli` 1 + 7× `es:` — wszystko w `_SetJmp`/`_LongJmp`/stubie wyjątków na końcu pliku | zastępujemy ręcznymi wersjami / pomijamy |
| dane | `ffedat.asm`: 144 401 B w `db` + 4 072 dwordy `dd` (3 562 linie symboliczne) + BSS `resb`; `ffebmp.asm`: 1 314 728 B w `db` + 4 438 `dd` (100% symboliczne); **zero `dw`**; brak kolizji etykiet między plikami; `ffe.asm` sięga do `ffebmp` tylko przez 5 symboli (`DATA_001697/004681/007975/008240/008241`) | §6 |
| timer | `TimerGetTimeStamp` zwraca licznik kwantowany co **5 ms** (`sdl\sdltimer.cpp`) — gra taktuje się wirtualnym zegarem 200 Hz | warstwa Amigi: `amigagfx_millis()/5` |

---

## 2. Architektura: cztery artefakty

```
tools\translator\      x86→68k (Python 3.13, ~3–6k linii)      → ffe68k\*.s
tools\datamap\         typowanie + konwersja BE danych          → ffe68k\ffedat_be.s, ffebmp_be.s + raport ryzyka
tools\difftest\        harness różnicowy (Musashi + DBI + replay)
amiga\ffeapi\          40+5 funkcji API + shimy libc na amiga_gfx/amiga_audio (C, MIT)
```

Katalogi docelowe (do utworzenia):
- `I:\GITHUB\Amiga_Frontier_2_First_Encouters\tools\translator\` — `parse.py`, `lower.py`, `flags.py`, `peep.py`, `emit.py`, `abi.py`, `cfg.py` + testy jednostkowe.
- `I:\GITHUB\Amiga_Frontier_2_First_Encouters\tools\datamap\` — `staticscan.py`, `dbi\` (klient DynamoRIO), `merge.py`, `gendata.py`, `check_be_image.py`, `filemaps\` (mission.dat, cabin bmp, save).
- `I:\GITHUB\Amiga_Frontier_2_First_Encouters\tools\difftest\` — runner Musashi (C, budowany hostowo), `vectors\`, `record_replay\`.
- `I:\GITHUB\Amiga_Frontier_2_First_Encouters\ffe68k\` — wyłącznie wyjście translatora (generowane, nie edytowane ręcznie).
- `I:\GITHUB\Amiga_Frontier_2_First_Encouters\amiga\ffeapi\` — nowy kod platformowy; istniejące `amiga\amiga_gfx.c/h`, `amiga_audio.c/h`, `amiga\c2p\` zostają jak są.

Zasada: **wszystko, co wygenerowane, jest odtwarzalne jedną komendą**; żadnych ręcznych poprawek
w `ffe68k\` (ręczne wersje funkcji żyją w osobnym katalogu `amiga\hand68k\` i nadpisują wynik
translatora na etapie linkowania listą w pliku konfiguracyjnym).

---

## 3. (A) Translator — projekt

### 3.1 Mapowanie rejestrów

| x86 | 68k | uzasadnienie |
|---|---|---|
| eax | d0 | akumulator, wynik funkcji — zgodny z ABI m68k GCC (int w d0) |
| ecx | d1 | caller-saved po obu stronach; licznik shiftów (`by cl` → shift by d1) |
| edx | d2 | drugi wynik (`cdq`/`imul`/`idiv`); d2 jest callee-saved w m68k ABI — patrz granica ABI niżej |
| ebx | d3 | callee-saved po obu stronach |
| esi | a2 | baza pamięciowa (3 121 użyć) — naturalny rejestr adresowy |
| edi | a3 | jw. (1 328) |
| ebp | a5 | ramka; `(d16,a5)` pokrywa 17 907 operandów `[ebp±d]` jedną instrukcją |
| esp | a7 | stos 1:1; `[esp+d]` (113×) → `(d16,a7)` |
| — | a0, a1 | **scratch translatora** (bazy pamięciowe z Dn, cele skoków pośrednich) |
| — | d7 | **scratch danych** translatora (operand pamięć-pamięć, shifty >8, materializacja flag) |
| — | d4–d6, a4, a6 | wolne — rezerwa na optymalizacje (cache bazy, stałe pętli) w M6 |

**Problem „baza w rejestrze danych"** (`[eax+d]`, `[ebx+d]`, `[edx+d]`, `[ecx+d]` — ~12,6k
operandów + ~1,1k dwurejestrowych): sekwencja kanoniczna to

```
movea.l d0,a0        ; NIE zmienia CCR — to ważne dla §4
move.l  (d16,a0),…   ; albo (d8,a0,d2.l*4) dla [eax+edx*4+d]
```

czyli +1 instrukcja na dostęp. Łagodzenie: **cache a0/a1 w obrębie bloku bazowego** — translator
pamięta, która wartość x86 siedzi w a0/a1; unieważnia przy zapisie do rejestru źródłowego, przy
callu i na etykiecie. W pętlach rasteryzatora (stała baza) redukuje to koszt niemal do zera.
Eksperyment na M1 (decyzja odwracalna, zlokalizowana w jednej tabeli `abi.py`): mapowanie
**ebx→a6** zamiast d3 — ebx jest najgorętszą bazą (5 740), ale bywa też operandem arytmetyki;
`adda/suba` nie ustawiają flag, więc dataflow flag musi wtedy wymuszać obejścia. Zmierzyć oba
warianty na benchmarku z przetłumaczonego `DrawSpans` i wybrać.

### 3.2 Tryby adresowania

| x86 (NASM) | 68020 | uwagi |
|---|---|---|
| `[DATA_x]`, `[DATA_x+0xN]` | `(DATA_x).l`, `(DATA_x+N).l` | absolute long, reloc w hunku |
| `[ebp+d]`, `[esp+d]` | `(d16,a5)`, `(d16,a7)` | d16 wystarcza (maks. ramka widziana: 0x124) |
| `[esi+d]`, `[edi+d]` | `(d16,a2/a3)` | bezpośrednio |
| `[eax+d]` itd. (baza w Dn) | `movea.l dN,a0` + `(d16,a0)` | scratch, §3.1 |
| `[base+idx*s+d]` | `(d8,An,Dm.l*s)` lub pełny format `(bd,An,Dm.l*s)` gdy d>127 | 68020 full extension word |
| `[idx*4+DATA_x]` (tablice skoków) | `(DATA_x,ZA0,Dm.l*4)` — suppressed base | natywny tryb 68020; gas: `(DATA_x,za0,d0.l:l*4)` — składnię potwierdzić w M1 na `m68k-amigaos-as`, zapas: `lea DATA_x,a0` + `(0,a0,d0.l*4)` |
| `[es:reg]` (2× w foncie) | zwykły dostęp | flat model, prefiks ignorowany |

Niewyrównane dostępy: 68020+ obsługuje sprzętowo (wolniej, bez wyjątku) — LE-owe offsety
nieparzyste nie wymagają nic specjalnego. `a7` pozostaje zawsze parzyste, bo x86 nigdy nie
pushuje bajtów.

### 3.3 Stos, cdecl i granica ABI

Kluczowy fakt: **układ argumentów cdecl x86 = układ argumentów m68k GCC** (wszystko na stosie,
pierwszy argument najbliżej wierzchołka, caller sprząta, int/pointer wraca w d0/eax).
Dlatego:

- `push X` → `move.l X,-(sp)`; `push byte +0xN` → `pea (N).w` (krócej i **nie rusza CCR**);
  `push dword DATA_x` → `pea (DATA_x).l`.
- `add esp, byte +0x8` → `addq.l #8,sp`; `add esp, 0xfffffedc` → `lea (-0x124,sp),sp` (lea zamiast
  `adda` — też bez CCR, choć tu bez znaczenia).
- `call FUNC_x` → `jsr FUNC_x`; `ret` → `rts`.

Definiujemy **t-ABI** (ABI kodu przetłumaczonego): rejestry wg tabeli §3.1, **flagi martwe przez
call** (kompilator x86 tego samego założenia używał — zweryfikowane pomiarem: zero konsumentów
flag po `call`), caller-saved = {d0,d1,d2,a0,a1,d7}, callee-saved = {d3,a2,a3,a5} (dokładnie to,
co kod x86 i tak zachowuje przez push/pop własnych ebx/esi/edi/ebp).

Granice t-ABI ↔ m68k-C-ABI:

1. **t → C** (410 call sites do 80 symboli zewnętrznych): stos już zgodny; C-funkcja klobbruje
   {d0,d1,a0,a1} ⊂ klobber t-ABI {d0,d1,d2,a0,a1,d7} — **wywołanie bezpośrednie, bez thunków**.
   Jedyny znak zapytania: konwencja zwrotu wskaźników bebbo GCC (d0, a0, czy oba) — 6 funkcji
   `DirMake*/DirGetCmmdrPath` zwraca `char*`. Eksperyment 10-minutowy w M1; w razie „tylko a0"
   te funkcje dostają 2-instrukcyjne thunki `jsr`+`move.l a0,d0`+`rts`. `_sqrt` (zwrot double
   w ST0/FP0) nie istnieje w kodzie tłumaczonym — jego jedyny caller `FUNC_000833` jest pisany
   ręcznie (int-sqrt 64-bit).
2. **C → t** (platforma woła grę): dokładnie 4 sygnatury: `asmmain(argc,argv)` z main() oraz
   `BufferBlit/BufferMaskedBlit/BufferReverseBlit(pData,x,y,w,h,jump)` wołane przez nasz
   odpowiednik `BlitClipWrapper`. Cztery ręczne wrappery: `movem.l d2-d7/a2-a6,-(sp)`,
   przepchnięcie argumentów na nowo, `jsr` do wersji przetłumaczonej, `movem.l` z powrotem.
   (t-kod klobbruje d2/d7 — czyli łamie m68k ABI — dlatego wrapper, nie wywołanie wprost.)
3. `_SetJmp`/`_LongJmp` (`ffe.asm:133057/133093`, 9 call sites) — ręcznie napisany DOS-owy
   setjmp z segmentami i `iret`; **zastępujemy własnym 68k setjmp**: zapis d2-d7/a2-a6/sp/PC do
   `jmp_buf`, zwrot 0/val. Stub obsługi wyjątków na końcu pliku — martwy, nie tłumaczymy.
4. `vfprintf`/`fprintf` z `%` formatami: va_list = wskaźnik na argumenty na stosie — układ
   identyczny (wszystko po 4 bajty), działa wprost; jedyny wyjątek — double w varargach — może
   wystąpić tylko z 7 funkcji x87 pisanych ręcznie, gdzie to kontrolujemy.

### 3.4 Postać wyjścia i build

- **Tryb harness**: jeden `.s` na funkcję (`ffe68k\f\FUNC_001521.s`) — do testów różnicowych.
- **Tryb pełny**: kod w ~16 plikach `ffe68k\ffe_code_00..15.s` (po ~7k instrukcji — szybka
  asemblacja równoległa), dane `ffe68k\ffedat_be.s` + `ffe68k\ffebmp_be.s` (sekcje `data`/`bss`
  hunka), `ffe68k\t_helpers.s` (helpery fixp, pętle rep, setjmp), wrappery §3.3 w
  `amiga\ffeapi\entry_shims.s`. Każda emitowana linia niesie w komentarzu oryginalną instrukcję
  x86 i numer linii `ffe.asm` (opcja `--annotate`, w buildzie release wyłączona).
- Nazewnictwo: etykiety oryginalne bez zmian (`FUNC_001601_DrawHLine`, `JUMP_003400`,
  `DATA_007773`); zmierzone 0 skoków międzyfunkcyjnych ⇒ `JUMP_` mogą być lokalne, ale zostają
  globalne dla debugowalności (koszt: rozmiar tablicy symboli, nie kodu).
- Asemblacja/link: `m68k-amigaos-as` + link przez `m68k-amigaos-gcc` (ciągnie startup i libc
  libnix — `fopen/fread/malloc/printf` z K2 dostajemy za darmo; pułapki no-FPU w printf są
  opisane w `I:\GITHUB\Amiga_Frontier_2_First_Encouters\docs\openttd-port\NOFPU.md`). vasm
  (`-Fhunk -m68020`) jako droga zapasowa, gdyby gas nie strawił trybu z suppressed base.
- **Dwa buildy z powodu K3**:
  - `ffe020` (020/030/040): `FUNC_001521/1522` inline jako `muls.l Dm,Dh:Dl` + shift, `FUNC_001523`
    jako `divu.l <ea>,Dr:Dq`; pozostałe 1-op `imul` z żywym edx → forma 64-bit.
  - `ffe060`: te same miejsca wołają helpery z `t_helpers.s` liczące 32×32→64 przez 4× `mulu.w`
    (16×16→32; na 060 ~2 cykle każdy) i dzielenie 64/32 programowo; `divsl.l/divul.l` 32/32
    zostają (są w sprzęcie 060). Zimne, rzadkie 64-bitowe `div` (6 miejsc) mogą zostać w formie
    64-bit — 68060.library je poprawnie emuluje, koszt bez znaczenia poza hot path.
  Wybór buildu per plik wykonywalny (jak w OpenTTD), nie runtime dispatch — prostsze i testowalne.

### 3.5 Idiomy (peephole, tabela w `peep.py`)

| wzorzec x86 | emisja 68k | miejsca |
|---|---|---|
| `cdq` + `idiv r/m` | `divsl.l <ea>,d2:d0` (quotient d0, reszta d2) | **216 (100% idiv)** |
| `xor edx,edx` + `div r/m` | `divul.l <ea>,d2:d0` | 39 |
| `call FUNC_001521` | inline: `move.l …,d0; muls.l …,d2:d0; lsl.l #1,d0; roxl.l #1,d2; move.l d2,d0` (wariant per build §3.4) | 441 call sites |
| `call FUNC_001522/1523` | analogicznie | ~100 |
| `xor r,r` | `moveq #0,dN` (flagi zgodne: Z=1, N=V=C=0) | częste |
| `cmp r,1` + `sbb r,r` | `tst.l dN; seq dN; extb.l dN` (r := r==0 ? −1 : 0) | 13 (100% luk X-flag) |
| `push byte +imm` | `pea (imm).w` | tysiące |
| `mov eax,[m]; push eax` (eax martwy) | `move.l (m),-(sp)` | częste |
| `imul r/m` z martwym edx | `muls.l <ea>,d0` (32-bit, 060-safe) | część z 281 |
| `test rh,#bit` | `btst #log2(bit)+8,dN` (tylko gdy konsument czyta wyłącznie Z) | 21 z 22 użyć high-8 |
| `movzx r32,r/m8` | `moveq #0,dN; move.b <ea>,dN` | 421 |
| `movsx r32,r/m8` | `move.b <ea>,dN; extb.l dN` | 2 119 (16-bit: `move.w`+`ext.l`) |

Reguła nadrzędna: **peephole nigdy nie zmienia obserwowalnych flag ani stanu rejestrów t-ABI**,
chyba że dataflow (§4) dowiódł ich martwoty. Każdy idiom ma test różnicowy w M1.

### 3.6 Konstrukcje specjalne

- `rep movsd` (151) / `rep movsb` (40): pętla inline zachowująca semantykę końcową (ecx=0,
  esi/edi przesunięte — kod może na tym polegać):
  `tst.l d1; beq .done; .l: move.l (a2)+,(a3)+; subq.l #1,d1; bne .l; .done:` — plus (M6)
  podmiana największych stałych kopii na `move.l`-unrolled/`movem.l`.
- `repne scasb` (38): pętla inline; konsumenci czytają Z (znaleziono/nie) i liczą długość z
  ecx/edi — pętla to odtwarza. Przypadek wejściowego ecx=0 (x86: flagi bez zmian) rozjeżdża się
  z naszą emisją — w idiomie strlen ecx=0xFFFFFFFF, więc nieosiągalne; lint translatora zgłasza,
  gdyby konsument flag istniał na ścieżce ecx=0.
- pojedyncze `movsb/movsw/movsd` (19): `move.b/w/l (a2)+,(a3)+`.
- `setcc r/m8` (114): `sCC <ea>; and.b #1,<ea>` — bo 68k `scc` daje 0xFF, x86 `setcc` daje 1.
  (`and.b` zmienia CCR — chronione dataflow §4; x86 `setcc` flag nie zmienia.)
- `xchg` (40): `exg` dla reg-reg; reg-mem przez d7.
- `shl/shr/sar r,cl`: shift by d1; **różnica maskowania** (x86: cnt mod 32, 68k: mod 64) —
  akceptowane ryzyko klasy „UB w C źródłowym", lint oznacza miejsca, gdzie cl nie pochodzi
  z oczywistego zakresu.
- x87 (7 funkcji z §1): wyłączone z translatora (twardy błąd, jeśli parser trafi na `f*` poza
  listą). Ręczne porty: `FUNC_002095_fstore` (ftol z trybem rounding) i `FUNC_000833`
  (`sqrt(x²+y²+z²)` ze skalowaniem — 64-bit isqrt całkowite); `FUNC_002110` (formatowanie
  double — najpewniej ścieżka zapisu configu/save; do ustalenia w M1); `FUNC_001877` (97 instr.
  x87, jeden caller @`ffe.asm:129474`) i `FUNC_000112/118/120` — rola do zidentyfikowania w M1,
  implementacja fixed-point lub soft-float, weryfikacja różnicowa jak dla każdej innej funkcji.
- Tablice skoków: parser wyciąga bloki `SECTION .data`/`dd JUMP_*` osadzone w `ffe.asm`
  (92 przełączenia sekcji) i emituje je do sekcji danych jako `dc.l JUMP_*`.

---

## 4. (B) Flagi — model i reguły

**Decyzja: flagi eager** — każda przetłumaczona instrukcja arytmetyczna zostawia w CCR dokładnie
to, co x86 w EFLAGS (w zakresie N/Z/V/C ⇔ SF/ZF/OF/CF). Uzasadnienie pomiarem: mapowanie jest
1:1 dla całego zmierzonego miksu, a WSZYSTKIE odstępstwa to 7+13 statycznych miejsc (§1) —
lazy flags (liczenie flag na żądanie, styl emulatorów) kosztowałoby wszędzie i nie dawało nic.
PF/AF nie odtwarzamy: jedyny konsument parzystości w całym pliku (`jpe` @133039) leży w ręcznie
portowanej funkcji x87.

### 4.1 Tabela zgodności (używane instrukcje)

| x86 | 68k | zgodność flag |
|---|---|---|
| `add/sub/neg` | `add/sub/neg` | pełna (C=borrow po sub, ta sama polaryzacja; X=C bonusem) |
| `cmp a,b` (=a−b) | `cmp.l b,a` (dst−src) | pełna po zamianie kolejności operandów; **CMP nie ustawia X** → §4.3 |
| `and/or/xor/test` | `and/or/eor` / `tst`+`and` do d7 | pełna (C=V=0, N/Z z wyniku) |
| `inc/dec` | `addq/subq #1` | x86 NIE rusza CF, 68k rusza C/X — legalne, bo zmierzone 0 konsumentów starego CF; translator i tak dowodzi per miejsce (§4.2) |
| `shl/shr/sar/rol/ror` | `lsl/lsr/asr/rol/ror` | C = ostatni wysunięty bit po obu stronach; V: asl vs shl różnie dla n>1 — konsumentów V po shiftach brak (zero `jo/jno` w pliku) |
| `rcl 1` (1×, w FUNC_001521) | `roxl.l #1` | dokładny odpowiednik przez X |
| `adc/sbb` | `addx/subx` (operandy przez d7, **move nie rusza X**) | C/X poprawne; Z akumulowane vs absolutne — zmierzone 0 konsumentów Z po adc/sbb |
| `mov/push/pop/lea/movsx/movzx/setcc/not/xchg` | — | x86: bez flag; 68k `move` USTAWIA N/Z i zeruje V/C → to jest jedyna systematyczna rozbieżność, patrz §4.2 |
| `mul/imul/idiv/div` | `muls/mulu/divs/divu…` | x86 zostawia SF/ZF **niezdefiniowane** → kompilator ich nie konsumował; 68k ustawia N/Z/V — bez znaczenia. Dzielenie przez zero: trap po obu stronach. Overflow ilorazu: x86 #DE (crash), 68k V-flag i jazda dalej — rozbieżność wyłącznie na ścieżkach, które w oryginale by się wywaliły; build debug może stawiać `trapv` |

### 4.2 Dataflow żywotności flag

`flags.py` liczy klasyczny backward-dataflow po CFG funkcji (CFG jest pełny: 0 skoków
międzyfunkcyjnych, pośrednie `jmp` tylko przez 35 tablic o znanych celach). Dla każdej
instrukcji wiadomo, czy flagi x86 są żywe na wyjściu. Wtedy:

- instrukcja x86-flag-neutralna, która na 68k klobbruje CCR (`move` do Dn/pamięci, `and.b #1` po
  scc, pętle rep…), a flagi są żywe → próbuj w kolejności:
  1. **reorder** — wysuń instrukcję przed setter flag (legalne, gdy brak zależności operandów);
  2. **droga bez CCR** — cel w rejestrze adresowym ⇒ `movea`; push stałej ⇒ `pea`; adresowa
     arytmetyka ⇒ `lea`;
  3. **zachowanie CCR**: `move ccr,d7` … `move d7,ccr` (obie formy legalne na 68020).
  Zmierzone zapotrzebowanie: **7 miejsc w całej grze** (5 z nich i tak przechodzi, bo `move`
  nie rusza X, a konsumentem jest `addx/subx`).
- flagi martwe (przypadek dominujący) → emituj najkrótszą formę bez oglądania się na CCR.

**Tryb paranoiczny** (`--paranoid`): każda potencjalna rozbieżność dostaje wariant 3 (save/restore
CCR), bez peepholi. Służy do bisekcji: gdy wynik różnicowy się rozjeżdża, uruchamiamy funkcję w
trybie paranoicznym — jeśli przechodzi, winien jest peephole/dataflow, jeśli nie — mapowanie bazowe.

### 4.3 Luka X-flag

68k `cmp/tst` nie ustawiają X, a `subx/addx/roxl` czytają X. Wszystkie zmierzone konsumenty X po
`cmp` to 13× idiom `cmp r,1; sbb r,r` — załatwione peepholem `seq` (§3.5), który X w ogóle nie
potrzebuje. Fallback generyczny (gdyby przyszły inne przypadki): wzmocnić producenta —
`move.l a,d7; sub.l b,d7` ustawia X=C przy identycznych pozostałych flagach.

### 4.4 Warunki skoków

`jz→beq, jnz→bne, jl→blt, jnl→bge, jg→bgt, jng→ble, jc/jb→bcs, jnc/jae→bcc, ja→bhi, jna→bls,
js→bmi, jns→bpl, jge→bge, jle→ble`; `jpe` — tylko w kodzie x87 (ręcznym). `setg→sgt` itd. z
korektą 0xFF→1. Kolejność operandów `cmp` odwrócona zgodnie z §4.1 — warunki się NIE lustrzą
(to najczęstszy błąd ręcznych portów; test jednostkowy w M1 pokrywa wszystkie 16 kombinacji
na wartościach brzegowych 0x80000000/0x7FFFFFFF).

---

## 5. (C) Pośredni przepływ sterowania

**Inwariant globalny: w programie przetłumaczonym nie istnieją adresy x86.** Każda komórka,
która kiedykolwiek zawiera wskaźnik funkcji, dostaje wartość albo (a) statycznie z `dd FUNC_x` /
`dd JUMP_x` w danych (asembler wstawia adres 68k), albo (b) dynamicznie z `mov [m],imm-FUNC_x` /
`push FUNC_x` (zmierzone 138 materializacji — wszystkie przez mov/push), albo (c) z warstwy C
(nigdy — API nie przekazuje wskaźników do kodu gry; jedyny kierunek to gra→`BlitClipWrapper`).
Skoro wszystkie źródła produkują adresy 68k, każdy pośredni skok jest spójny z definicji.

| klasa | ile | emisja |
|---|---|---|
| `call near [DATA_x(+d)]` — zmienne-wskaźniki | 1 344 | `movea.l (DATA_x+d).l,a0; jsr (a0)` |
| `call near [ebp±d]` — callback z argumentu/lokalnej | 12 | `movea.l (d,a5),a0; jsr (a0)` |
| `call near [reg]` / `call near [ebx+0x4]` — wskaźnik w rejestrze/polu struktury | 27+2 | `movea.l dN,a0; jsr (a0)` / przez scratch |
| `jmp near [reg*4+DATA_0000xx]` — switch | 35 | `move.l (DATA_x,za0,dN.l*4),a0; jmp (a0)`; tablica emitowana jako `dc.l JUMP_*` w sekcji danych; bounds-check (`cmp`+`ja`→`bhi`) tłumaczy się przed nią jak zwykły kod |

Weryfikacja domknięcia inwariantu (lint w translatorze, uruchamiany przy każdej translacji):
1. każde użycie `FUNC_*`/`JUMP_*` poza `call/jmp/jcc` musi być typu mov/push/dd — zmierzone
   wyjątki: **3 porównania równościowe** (`cmp eax,FUNC_001722` ×2, `cmp ecx,FUNC_001842`) —
   porównanie dwóch adresów 68k jest równoważne porównaniu dwóch adresów x86 ⇒ legalne, lint ma
   je na białej liście;
2. zero arytmetyki na etykietach kodu (add/sub/lea z FUNC_) — zmierzone: zero;
3. każda tablica `dd JUMP_*` musi być kompletna względem bounds-checku poprzedzającego skok.

Nie ma tu żadnego „runtime mapping table" ani podwójnych adresów — to główna wygrana statycznej
translacji nad emulacją i musi tak zostać.

---

## 6. (D) Endianness i typowanie danych

> **Decyzja użytkownika (2026-07-28): konwersja danych biegnie na maszynie osoby uruchamiającej
> grę, z jej własnych plików.** Nie dystrybuujemy przekonwertowanych danych — narzędzie
> konwertujące jest częścią wydania, dane nigdy. To rozstrzyga postawę prawną (§8 research doc)
> i zmienia `gendata` z kroku build-time w krok instalacyjny. Szczegóły projektowe tego kroku —
> gdzie biegnie (PC czy Amiga), w jakiej formie i ile trwa — **projektujemy dopiero w M2**,
> nie teraz.

### 6.1 Łańcuch narzędzi

```
ffedat.asm ─┐
ffebmp.asm ─┤→ staticscan.py ──→ evidence_static.tsv ─┐
ffe.asm    ─┘   (szerokości dostępów per etykieta)    │
                                                      ├→ merge.py → typemap.tsv + risk_report.md
build PC (M0) + DynamoRIO memtrace ─→ evidence_rt.tsv ┘         │
   (adres,rozmiar,r/w,pc → label+offset przez mapę symboli)     ▼
notatki JJ (gmstruct.txt, plstate.txt, cstruct.txt,      gendata.py → ffedat_be.s, ffebmp_be.s
 shipidx.txt) → overlays ręczne evidence_manual.tsv      check_be_image.py (patrz 6.4)
```

- **staticscan.py**: dla każdego operandu `[DATA_x+const]` w `ffe.asm` zapisuje (etykieta, offset,
  szerokość z mnemonika/rozmiaru: `byte/word/dword`, r/w). Dla `[DATA_x+reg…]` zapisuje „dostęp
  indeksowany o kroku = skala" — typuje elementy tablic. Ograniczenie: nie widzi dostępów przez
  wskaźniki ładowane do rejestrów — od tego jest runtime.
- **runtime (główne źródło)**: build PC z M0 (Linux 32-bit w WSL2) pod klientem DynamoRIO
  filtrowanym do zakresu [.data,.bss]; symbolizacja przez `nm` (wszystkie etykiety `DATA_` są
  w tablicy symboli ELF — nasm emituje nie-lokalne etykiety domyślnie). Sesje grania pokrywające:
  intro, menu, start, lot, hyperspace, walkę, dokowanie, BBS, mapę galaktyki, journal, save/load.
  Metryka pokrycia: % bajtów danych dotkniętych. Zapas, gdyby DynamoRIO zawiódł na WSL2:
  qemu-i386 z pluginem TCG (mem callback), ostatecznie Valgrind.
- **regiony ryczałtowe**: `ffebmp.asm:10542` `; MODEL DATA START` → bytecode modeli 3D = strumień
  **LE16** (format udokumentowany: Jongware, watsonmw/fe2-intro) → swap słów w całym regionie;
  `DATA_007975` sine table = LE32; bitmapy przed regionem modeli = bajty (neutralne); łańcuchy
  `db 'tekst',0` → neutralne (auto-wykrywane).
- **merge.py**: hierarchia dowodów `reloc(dd symboliczne) > manual(JJ) > runtime > static > region`;
  konflikty (ta sama komórka czytana różnymi szerokościami z niezgodnym podziałem) → typ `mixed`,
  ląduje w raporcie.

### 6.2 Format typemap.tsv

```
label       offset  len   type    evidence                 confidence
DATA_007773 0       4     ptr     reloc                    certain
DATA_007773 4       16    le32[4] runtime:rw4×9182,static  high
DATA_002327 0       2     le16    region:model             high
DATA_004242 12      4     unknown -                        none
```

Typy: `ptr, le32, le16, u8[], str, mixed, unknown`. `gendata.py` emituje: `ptr`→`dc.l LABEL`,
`le32`→`dc.l $wartość-złożona-z-4-bajtów-LE`, `le16`→`dc.w`, `u8[]/str`→`dc.b` bez zmian,
`unknown`→`dc.b` bez zmian **plus wpis do raportu** (dla danych czysto bajtowych to poprawne,
dla ukrytych skalari — błąd, który raport kwantyfikuje). BSS → `ds.b` (bez konwersji z natury).

### 6.3 Raport ryzyka rezydualnego (bramka M2)

`I:\GITHUB\Amiga_Frontier_2_First_Encouters\ffe68k\risk_report.md`, generowany przy każdym
przebiegu, zawiera: (a) sumy bajtów wg typu i źródła dowodu, per plik; (b) **listę wszystkich
przebiegów `unknown`/`mixed`** ≥4 B: etykieta+offset+len + funkcje, które ich dotykają (xref ze
staticscan) + zrzut hex; (c) trend względem poprzedniego przebiegu. Cel liczbowy na wyjście z M2:
`unknown+mixed` < **2%** bajtów `ffedat.asm` poza łańcuchami i < **0,5%** `ffebmp.asm` (region
modeli i bitmapy pokrywają ryczałty). Raport jest miarą ryzyka, nie dekoracją: każda pozycja z
listy (b) to potencjalny bug klasy „kosmiczna anomalia po 3 h grania".

### 6.4 Weryfikacja obrazu BE

`check_be_image.py`: składa obraz LE (nasm z `ref\`, bez modyfikacji) i BE (vasm/gas z
`ffe68k\`), po czym dla każdego wpisu typemap sprawdza inwarianty (ptr wskazuje na tę samą
etykietę; le32/le16 są bajtowym odwróceniem; u8 identyczne; offsety etykiet zgodne z deltami
LE po uwzględnieniu paddingu). Test dymny na znanych strukturach: `DATA_007773/007774`
(`ffedat.asm:24815`) muszą dekodować się do {320,200} i {320,158}.

### 6.5 Pliki zewnętrzne (nowy podproblem, K6)

Kod przetłumaczony czyta pliki przez `fread` i interpretuje pola wielobajtowe po big-endianowemu —
pliki gracza są LE. Zakres zmierzony: `mission.dat` (journal; offsety siedzą w `DATA_002292`),
`cabin%i.bmp` + `%s%sbmp_8\%s.bmp` + `pic%03d.bmp` (nagłówki BMP = LE), save'y commanderów
(pisane i czytane przez grę), `fx\*.raw` i `music\*.hmp` (konsumowane przez NASZĄ warstwę
platformową — endianness nas nie dotyczy), `*.avi` (opcjonalne; wersja floppy w
`I:\GITHUB\Amiga_Frontier_2_First_Encouters\Frontier-First-Encounters_DOS_EN_Floppy-version\`
ich nie ma — `SoundStream*` = stub, `DirMakeAVIName` zwraca ścieżkę nieistniejącą, gra pomija
intro filmowe; ta ścieżka degradacji istnieje w oryginale).
Rozwiązanie: **konwerter po stronie użytkownika** (`tools\datamap\convert_userdata.py`, a docelowo
mały program amigowy w pakiecie release): mission.dat i BMP wg map formatu w
`tools\datamap\filemaps\`; save'y — format natywnie BE (niekompatybilny z DOS-owym; świadoma
decyzja, odnotować w README). Alternatywa runtime'owa (swap przy fopen po nazwie pliku) odpada:
`fread` nie zna typów. Format mission.dat do zdjęcia w M2 z pomocą `DATA_002292` i kodu journala.

---

## 7. (E) Weryfikacja — harness różnicowy

### 7.1 Trzy poziomy

| poziom | co porównuje | gdzie biega | kiedy |
|---|---|---|---|
| **L1 funkcja** | wynik d0/(d2), log store'ów (adres,rozmiar,wartość), stan wskazanych regionów | x86: proces PC (funkcje wołane z małego drivera C linkowanego z `ffe.o` — wszystkie `FUNC_` są `GLOBAL`); 68k: **runner na bazie rdzenia Musashi** (MIT, tryb 68020) wbudowany w narzędzie hostowe — iteracja w milisekundach, bez bootowania czegokolwiek | M1+, CI przy każdej zmianie translatora |
| **L2 replay** | hash zawartości każdego `VideoBlit` (piksele = bajty = endian-neutralne) + sekwencja wywołań API | nagranie: PC z warstwą record (wrappery 40 funkcji logują wyniki Timer/Input/Dir/Sound); odtworzenie: PC-replay i 68k-replay (Musashi z podpiętym replayem zamiast API) | M3+ |
| **L3 system** | zachowanie na prawdziwym OS (ekran, wejście, dźwięk, wydajność) | WinUAE, konfiguracje z `I:\GITHUB\Amiga_Frontier_2_First_Encouters\winuae\` (030/040/RTG/AGA), pętla build→deploy→screenshot z `I:\GITHUB\Amiga_OpenTTD\tools\` | M4+ |

Klucz architektoniczny: **L1/L2 nie używają WinUAE ani AmigaOS.** Runner Musashi implementuje
`_malloc/_fopen/...` jako shimy hostowe i ładuje przetłumaczone `.s` zasemblowane do surowego
obrazu (as → objcopy). Dzięki temu differential testing setek funkcji jest nienadzorowany i
szybki; WinUAE zostaje do tego, do czego jest niezastąpione. Ryzyko wierności Musashi — §9 R7.

Porównanie stanu bez pułapki endian: po obu stronach zbieramy **log zapisów** (x86: klient
DynamoRIO — ten sam, co w §6; 68k: hook pamięci Musashi) jako krotki (adres-symboliczny, rozmiar,
wartość) — wartości są liczbami, nie bajtami, więc porównanie jest endian-neutralne z definicji.

### 7.2 Generowanie wejść

1. **wektory brzegowe** per klasa funkcji (0, ±1, 0x7FFFFFFF, 0x80000000, wzorce bitowe);
2. **fuzz** deterministyczny (seed w repo);
3. **nagrania rzeczywiste**: DynamoRIO loguje argumenty wybranych funkcji podczas grania na PC —
   wektory „takie, jakie gra naprawdę podaje" (ważne dla funkcji o wąskich dziedzinach, np.
   rasteryzatora, gdzie fuzz generowałby głównie nonsens).

### 7.3 Kolejność weryfikacji (pierwsze cele)

1. `FUNC_001521/1522/1523` (`ffe.asm:90809+`) — fundament fixed-point, 4 warianty emisji
   (generic/inline × 020/060); brzegi: 0x80000000, saturacja 001523;
2. `FUNC_001913_Sin16` / `FUNC_001914_Sin32` — pierwsza styczność z danymi BE (sine table
   `DATA_007975` w `ffebmp.asm:50509`);
3. `_strncpyfill` (`ffe.asm:132948`) — pętle, bajty, warunki;
4. `FUNC_000077_ExpandBracketCodes` — string processing z tablicami;
5. `FUNC_001601_DrawHLine`, potem `DrawLine/DrawTriangle/DrawSpans` — zapisy do bufora
   (porównanie pikseli), wydajnościowy benchmark przy okazji (§3.1 eksperyment ebx);
6. klaster interpretera modeli 3D (wejście przez `DATA_004681` ship object table,
   `ffebmp.asm:41814`) — pierwszy test ryczałtu LE16 regionu modeli: wyrenderować Eagle'a po obu
   stronach i porównać piksele;
7. `_asmmain` do pierwszego `TimerFrameUpdate` — domknięcie L2 na sekwencji splash
   (`ffe.asm:68269+`: memset, SysInit, teksty, paleta, flip).

### 7.4 Determinizm

Cała niedeterministyczność wchodzi przez 40 funkcji API (timer 5 ms, wejście, katalogi) — nagranie
ich wyników daje pełny determinizm replayu; RNG gry siedzi w danych i seeduje się z timera.
**Test zerowy w M0**: dwa przebiegi PC-replay z tego samego nagrania muszą dać identyczne sekwencje
hashy klatek — to weryfikuje samo założenie, zanim cokolwiek przetłumaczymy. Uwaga praktyczna:
nagranie i replay muszą obejmować też `Dir*` (listowanie katalogów) i `SoundCheckInit`.

---

## 8. (F) Kamienie milowe

Kolejność jest wiążąca, czasy trwania celowo nieokreślone — każdy milestone kończy się
sprawdzalnym kryterium, i to ono decyduje o przejściu dalej, nie kalendarz.

| M | cel (1 linia) | wyjściowe kryterium (sprawdzalne) | główne ryzyko |
|---|---|---|---|
| **M0** | referencja PC + determinizm | gra JJFFE zbudowana (WSL 32-bit SDL, `make -f ffelnxsdl.mak`, nasm współczesny vs 0.98 porównany na obrazie sekcji) **grywalna**; wrappery record/replay działają; dwa replaye → identyczne hashe klatek | bit-rot SDL 1.2/32-bit w WSL; OpenGL w sdlvideo (są ścieżki czysto-softwarowe) |
| **M1** | translator-prototyp + harness L1 | 30-funkcyjna suita (lista §7.3 pkt 1–5) bit-exact na wektorach brzegowych+fuzz+nagraniach; `--paranoid` ≡ tryb zwykły; rozstrzygnięte eksperymenty: zwrot wskaźników bebbo, mapowanie ebx, składnia suppressed-base w gas | koszt sekwencji scratch wyższy niż zakładany; wierność Musashi |
| **M2** | pipeline danych | `ffedat_be.s`+`ffebmp_be.s` asemblują się; `check_be_image.py` zielony; `risk_report.md` poniżej progów §6.3; format mission.dat opisany w `filemaps\`; pokrycie runtime ≥ 80% bajtów `.data` | pokrycie sesji grania; struktury dotykane tylko rzadkimi ścieżkami |
| **M3** | **cała gra na Musashi** (bez Amigi) | wszystkie 1 621 funkcji przetłumaczone i zlinkowane z shimami hostowymi; L2-replay: pierwsze ≥1 000 klatek (intro, menu, start nowego commandera) z hashami identycznymi z PC | długi ogon drobnych błędów translacji; to jest **główna bramka ryzyka projektu** |
| **M4** | first light na WinUAE | `ffe020` (hunk, link przez bebbo GCC) na konfiguracji `otd-aga-040-40.uae`: intro/menu widoczne i sterowalne klawiaturą+myszą; warstwa `amiga\ffeapi\` (wideo/wejście/timer/dir na `amiga_gfx.c`, dźwięk stub); log L2-hash na próbce klatek zgodny | pułapki libnix/no-FPU (udokumentowane w `docs\openttd-port\NOFPU.md`); rawkey→scancode |
| **M5** | pełna grywalność | pełny obieg: start→launch→lot→hyperspace→dok→save→load na 040/40; save BE zapisuje/odczytuje; konwerter danych użytkownika działa na floppy-wersji | wydajność jeszcze nieakceptowalna (to nie blocker M5) |
| **M6** | wydajność | profil (licznik ramek + próbkowanie PC w przerwaniu, jak w OpenTTD); ręczny 68k dla listy nazwanych funkcji rysujących (`DrawHLine…DPal*`, 278 nazw); inline fixp; build `ffe060`; c2p tylko 158 linii w locie; zero-copy `DATA_009226→amigagfx_chunky()`; **pomiar**: fps w zdefiniowanej scenie testowej (start z Ross 154) na 030/50, 040/25, 040/40, 060/50; cel: 040/40 ≥ poziom „486DX/33 playable", 030/50 grywalne na low detail | zostanie za wolno na 030 (fallback: podnieść minimum do 040, §9 R3) |
| **M7** | dźwięk i muzyka | SFX: `fx\*.raw` 16→8 bit, 4 kanały Pauli z priorytetami (mapa ~8 głosów logicznych→4); muzyka: pre-render HMI→8-bit raw (narzędzie PC w release; pliki generowane u użytkownika z JEGO `music\*.hmp`) + `AmigaAudio_Music*` streaming; `SoundStream*` zostaje stubem (brak AVI) | jakość pre-renderu HMI; chip RAM budget |
| **M8** | release engineering | archiwum LHA/ZIP (7-Zip, nigdy Compress-Archive), README z wymaganiami (020+8 MB Fast minimum — do potwierdzenia pomiarem w M6, rekomendowane 040), instrukcja konwertera, postura prawna jak JJFFE (sam plik wykonywalny); czysta instalacja na gołym WB 3.1 + pliki gracza = działa | — |

Bramki decyzyjne: po **M1** wiadomo, czy translator trzyma semantykę (jeśli nie — wracamy z
dowodami, ale pomiary §1 mówią, że trzyma); po **M2** ryzyko endian jest liczbą w raporcie, nie
lękiem; po **M3** gra logicznie działa na 68k *zanim* dotkniemy AmigaOS; po **M6** znamy realne
minimum sprzętowe. Najmniejsza uczciwa „pierwsza rzecz na ekranie Amigi" to M4-intro/menu — ale
faktyczne pierwsze piksele zobaczymy dużo wcześniej, w M1 pkt 5 (bufor `DrawHLine` na Musashi)
i w M3 (hash klatek splasha `_asmmain`), co zdejmuje presję z M4.

Zależności równoległe: `amiga\ffeapi\` (M4) można pisać równolegle z M2/M3 (rozłączne pliki);
konwerter plików (§6.5) równolegle z M3; ręczne porty x87 — w dowolnym momencie po M1.

---

## 9. (G) Rejestr ryzyk (malejąco)

| # | ryzyko | wczesny sygnał | fallback |
|---|---|---|---|
| R1 | **rezydualne błędy typowania danych** (ukryte LE-skalary w `db`) — pojedynczy zły dword psuje np. ekonomię jednego układu po godzinach gry | `risk_report.md` (liczba bajtów unknown/mixed); rozjazdy hashy L2 na długich replayach; MODE: replay całych sesji gry z PC | dociążać nagrania runtime (więcej sesji); ręczna adjudykacja listy z raportu; ostatecznie akceptacja ogona z listą znanych niepewności. **Nie ma pełnego fallbacku** — dlatego raport jest bramką M2, a L2-replay długimi sesjami jest w CI do końca projektu |
| R2 | **długi ogon błędów translatora** ujawniający się dopiero w M3 (interakcje, rzadkie ścieżki) | różnice L1 na fuzzingu; `--paranoid` vs zwykły; rozjazd hashy L2 z lokalizacją „pierwsza klatka różna → pierwszy store różny → funkcja" | tryb paranoiczny per funkcja (bisekcja automatyczna: połowa funkcji paranoid, połowa nie); najgorszy przypadek: cała gra w trybie paranoicznym działa poprawnie, tylko wolniej — grywalna baza do punktowego naprawiania |
| R3 | **wydajność na 030/50 poniżej grywalności** (repr. koszt scratch §3.1 + c2p) | benchmark `DrawSpans` w M1; fps sceny testowej w M6 | poziomy detali FFE; ręczny 68k rasteryzatora (i tak planowany); RTG jako ścieżka bez c2p; ostatecznie deklaracja minimum = 040 (mieści się w misji „najsłabszy sprzęt, który jeszcze daje radę") |
| R4 | **68060: trap 64-bit mul/div** w hot path (K3) | znane już teraz; mikrobenchmark buildów w M6 na konfiguracji 060 | build `ffe060` z helperami 4×`mulu.w` (§3.4) — fallback jest zaprojektowany, ryzyko tylko wykonawcze |
| R5 | **pliki zewnętrzne LE** (mission.dat/cabin/save) źle zmapowane | krzaki w journalu/kabinie w M5; save/load nie roundtripuje | mapy formatów + konwerter (§6.5); journal i kabina to funkcje peryferyjne — mogą wejść po M5 bez blokowania grywalności |
| R6 | **bebbo GCC psuje warstwę platformową** (znane ICE/miscompile) | objawy znane z OpenTTD: bug znika po dodaniu logowania | reguły odziedziczone: `-O0` per plik, nigdy `-O2`, `build-with-ice-retry.sh` z `I:\GITHUB\Amiga_Frontier_2_First_Encouters\build\`; warstwa jest cienka (~1–2 k linii) |
| R7 | **wierność Musashi** (68020 divsl/muls 64-bit, CCR w narożnikach) | rozjazd L1 Musashi↔WinUAE na tej samej suicie (uruchamiamy suitę L1 na WinUAE raz na milestone jako cross-check) | dwa niezależne emulatory zgodne = mocny dowód; przy różnicy — rozstrzyga instruction reference i test na prawdziwym 68060 (sceny/community) |
| R8 | **JJFFE ma własne bugi/różnice vs DOS FFE** — portujemy wiernie JJFFE, nie DOS | nie dotyczy poprawności portu (referencją JEST JJFFE 2.8) | świadome przyjęcie: bug-for-bug zgodność z JJFFE; poprawki JJ (lista w `jjffe28.txt`) traktujemy jako feature |
| R9 | **RAM**: obraz ~3,2 MB (kod 68k ~1,0–1,5 MB + dane 1,46 MB + BSS 0,75 MB) + sterta + c2p/chip — 4 MB Fast może nie wystarczyć | `AmigaMemProbe` (już w `amiga\amiga_gfx.h`) od pierwszego bootu M4 | minimum 8 MB Fast w wymaganiach; opcjonalnie build bez muzyki; BSS w hunku nie kosztuje pliku |
| R10 | **prawo**: dystrybucja pochodnej pracy RE bez licencji | n/d (stan znany) | postura JJFFE: tylko plik wykonywalny, dane gracza wymagane; decyzja o publikacji należy do użytkownika (research doc §8) |

---

## 10. Otwarte pytania z przypisanym eksperymentem

| pytanie | eksperyment rozstrzygający | kiedy |
|---|---|---|
| Konwencja zwrotu `char*` w bebbo GCC (d0/a0/oba) | 5-linijkowy test w WSL: funkcja C zwraca wskaźnik, disasm callera i callee (`m68k-amigaos-gcc -S`) | M1, dzień 1 |
| ebx→d3 czy a6 (§3.1) | benchmark przetłumaczonego `DrawSpans` w obu wariantach na Musashi (licznik cykli 68020) + na WinUAE 030 | M1 |
| Składnia suppressed-base w `m68k-amigaos-as` (`(DATA,za0,dN.l*4)`) | asemblacja próbki + disasm; fallback `lea`+indeks gotowy | M1 |
| Czy współczesny nasm daje obraz identyczny z 0.98 (sekcje/align) | `objcopy -O binary` obu wersji + diff; liczy się treść sekcji, nie nagłówki | M0 |
| Format mission.dat (pola wielobajtowe?) | analiza kodu journala wokół `DATA_002292` + hexdump pliku z wersji floppy | M2 |
| Rola `FUNC_001877` (97 instr. x87; caller @129474) i `FUNC_000112/118/120` | przejść callerów, skonfrontować z notatkami JJ (`srctools\c*.txt`); nagrania DynamoRIO pokażą, kiedy się wykonuje | M1/M2 |
| DynamoRIO na 32-bit ELF w WSL2 | dry-run memtrace na buildzie M0 | M0 |
| Realny mnożnik rozmiaru i cykli kodu przetłumaczonego (szac. 1,3–1,8×) | policzyć po translacji suity M1: bajty i cykle vs x86 | M1 |

---

## 11. Czego ten plan świadomie nie robi

- Nie tłumaczy x87 (7 funkcji ręcznie — §3.6) ani stubu wyjątków/`_SetJmp` (zastępowane).
- Nie odtwarza PF/AF ani zachowania flag po `mul/div` (x86: niezdefiniowane — nie istnieją
  konsumenci).
- Nie robi runtime dispatchu CPU w jednym binarium (dwa buildy — prostota i testowalność).
- Nie implementuje odtwarzania AVI (`SoundStream*` stub; wersja floppy ich nie ma).
- Nie optymalizuje przed M6 niczego, czego nie mierzy (peephole'e M1 są semantycznie
  konserwatywne; cała agresja dopiero po profilu).
- Nie zakłada zgodności save'ów z DOS-em (BE-native; konwerter to przyszła opcja, nie obietnica).
