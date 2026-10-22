# Frontier: First Encounters na Amigę 68k — analiza wykonalności

Data: 2026-07-28. Źródło danych: własna analiza sklonowanego repozytorium
`I:\GITHUB\Amiga_Frontier_2_First_Encouters\ref\jjffe` (JJFFE 2.8, John Jordan) plus research
sieciowy. Wszystkie liczby poniżej są zmierzone, nie szacowane, chyba że wyraźnie napisano inaczej.

---

## 1. Werdykt

**Port jest wykonalny i — co zaskakujące — technicznie *łatwiejszy* od OpenTTD pod wieloma
względami.** Nie jest to jednak "port" w sensie rekompilacji źródeł C: JJFFE to 113 133 instrukcje
asemblera x86. Głównym zadaniem jest **napisanie translatora x86 → 68k**, a nie przepisywanie gry.

Trzy fakty, które przesądzają o wykonalności:

1. **Gra to 320×200, 8bpp chunky, paleta 256 kolorów.** Dokładnie ten format, który obsługuje
   istniejąca biblioteka `I:\GITHUB\Amiga_OpenTTD\native\openttd\amiga_gfx.c` (AGA c2p / EHB / RTG).
2. **Kod jest praktycznie wyłącznie całkowitoliczbowy** — 149 instrukcji x87 na 113 133 (0,13 %),
   z czego realnie liczy się jedno wywołanie `sqrt()` w całej grze. Arytmetyka to fixed-point
   32-bit i własne procedury 64-bitowe, które **mapują się 1:1 na `muls.l`/`divs.l` z 68020+**.
3. **Cały interfejs do systemu to 40 funkcji C** (`ffeapi.h`) — wideo, mysz, klawiatura, joystick,
   timer, dźwięk, katalogi. Nic więcej. To jest cała powierzchnia portu.

Wymagania oryginału: **386DX/33 + 4 MB RAM**. To mniej niż wymagało OpenTTD (24 MB).

---

## 2. Czym naprawdę jest JJFFE

JJFFE to **anotowana deasemblacja oryginalnego pliku wykonywalnego FFE** (DOS, 1995), połączona
z ręcznie napisaną warstwą platformową w C/C++. Deasembler (`srctools/ffedis.cpp`) jest w repo,
razem z tabelą relokacji z oryginalnego binarium — to ważne, patrz §4.

Zmierzone rozmiary (`I:\GITHUB\Amiga_Frontier_2_First_Encouters\ref\jjffe`):

| plik | rozmiar | zawartość |
|---|---|---|
| `ffe.asm` | 2 537 976 B | **113 133 instrukcji x86**, 1 621 funkcji, 8 836 etykiet |
| `ffebmp.asm` | 7 615 155 B | 1 301 KB danych: bitmapy, modele 3D, tablice sinusów |
| `ffedat.asm` | 1 082 788 B | 157 KB danych + 750 KB BSS: struktury gry, misje |
| `win32/`, `sdl/`, `linux/` | ~90 KB | warstwa platformowa (wideo/dźwięk/wejście/katalogi) |
| `srctools/*.txt` | ~250 KB | **notatki RE Johna Jordana**: pseudo-C fizyki, generatora systemów, BBS, gracza, portów kosmicznych, struktur |

Obraz pamięci gry: **1,46 MB danych statycznych + 750 KB BSS + ~570 KB kodu ≈ 2,8 MB**, plus sterta.
Oryginalny nagłówek deasemblera potwierdza: 0x6A000 (434 KB) kodu, 0x25B000 (2,4 MB) danych.

### Kod jest wygenerowany przez kompilator C, nie pisany ręcznie

To jest kluczowa obserwacja i widać ją gołym okiem:

```asm
FUNC_001601_DrawHLine:
        push ebp
        mov ebp,esp          ; standardowa ramka cdecl
        push ebx
        push esi
        mov eax,[ebp+0x10]   ; argumenty na [ebp+8], [ebp+0xc], [ebp+0x10]...
```

Każda z 1 621 funkcji ma ten sam, regularny kształt: cdecl, argumenty na stosie, zmienne lokalne
na `[ebp-n]`, brak sztuczek. **To radykalnie zwiększa niezawodność tłumaczenia maszynowego** —
nie ma tu ręcznie optymalizowanego asemblera z trikami na flagach czy self-modifying code.

### Rozkład instrukcji (procent z 113 133)

```
mov 28,98%   push 18,13%   add 7,93%   call 6,33%   pop 5,69%   cmp 4,15%
lea 3,31%    jmp 2,41%     movsx 1,87% jz 1,76%     xor 1,74%   ret 1,70%
test 1,61%   sub 1,41%     jnz 1,40%   sar 1,29%    shl 1,17%   inc 0,90%
...  imul 0,70%   idiv 0,19%   div 0,04%   mul 0,01%
```

W praktyce **~45 różnych mnemoników**. Czego **nie ma** (sprawdzone):
`int`, `in`/`out`, `loop`, `xlat`, `bsf`/`bsr`, `shld`/`shrd`, `enter`/`leave`, instrukcje BCD,
`cmpxchg`. Jest 1× `pushf`, 5× `sahf`, 1× `cli`, 1× `rcl`, 9 prefiksów segmentowych (`es:` przy
`rep movs`), a jedyne odwołania do rejestrów segmentowych siedzą w ostatnich 40 liniach pliku
(stub obsługi wyjątków, nieużywany w grze).

### Sterowanie: prawie bez pułapek

1 420 miejsc z pośrednim `call`/`jmp`, ale prawie wszystkie to `call near [DATA_xxxx]` — czyli
**zmienne ze wskaźnikiem na funkcję**, których wartości pochodzą z samego programu. Statyczna
rekompilacja radzi sobie z tym trywialnie: wskaźnik dostaje adres *przetłumaczonej* funkcji.
Prawdziwych tablic skoków (`jmp near [eax*4+DATA_00000N]`) jest kilkanaście, a ich zawartość jest
symboliczna w `ffedat.asm`, więc też się tłumaczy. Zostaje garstka `call near [ebp+0xc]` —
callbacki przekazywane w argumencie, również bezpieczne.

**To jest najlepszy możliwy scenariusz dla statycznej rekompilacji.** Typowy problem binary
translation — nierozwiązywalny przepływ sterowania — tu praktycznie nie występuje.

### Arytmetyka fixed-point mapuje się idealnie na 68020+

Najczęściej wołana funkcja w grze (441 miejsc wywołania) to:

```asm
FUNC_001521:                 ; = (int32)(((int64)a * (int64)b) >> 31)
        mov eax,[ebp+0x8]
        imul dword [ebp+0xc] ; edx:eax = a*b, 64-bit
        shl eax,1
        rcl edx,1
        mov eax,edx
```

Na 68020+ to `muls.l d1,d2:d0` + przesunięcie — **4 instrukcje inline zamiast wywołania funkcji
z ramką stosu**. Podobnie `FUNC_001523` (dzielenie fixed-point) → `divs.l <ea>,Dr:Dq`, które ma
dokładnie tę samą semantykę 64/32 co x86 `div` z `edx:eax`.

Czasy: `muls.l` to ~44 cykle na 68030, 16 na 68040, **2 cykle na 68060**. Dla porównania 386
`imul r/m32` to 25–40 cykli. Na 060 arytmetyka fixed-point FFE będzie **dramatycznie szybsza niż
na jakimkolwiek 486**. To argument, że 060 poradzi sobie z tą grą bardzo dobrze.

**Konsekwencja: 68020 to minimum** (68000 nie ma `muls.l`/`divs.l` 32-bit), identycznie jak
w porcie OpenTTD.

---

## 3. Warstwa platformowa — mapowanie na to, co już mamy

`ffeapi.h` to całe API. Poniżej mapowanie na istniejący kod w `I:\GITHUB\Amiga_OpenTTD\native\openttd\`:

| FFE API | Amiga | źródło |
|---|---|---|
| `VideoBlit(pData,x,y,w,h,jump)` | kopia do chunky + `amigagfx_blit()` | `amiga_gfx.c` |
| `VideoSetPalValue`/`GetPalValue` (6-bit VGA) | `amigagfx_set_palette()` (AGA 8-bit, `<<2`) | `amiga_gfx.c` |
| `VideoPointerExclusive/Enable/Disable` | wskaźnik sprzętowy Intuition | `amiga_gfx.c` |
| `InputMouseReadButtons/Mickeys/Pos` | `amigagfx_poll()` + `amigagfx_warp_pointer()` | `amiga_gfx.c` |
| `InputKeybReadStates` (tablica scancodów) | rawkey → tablica mapująca | nowa, ~200 linii |
| `InputJoyReadPos/Buttons` | `lowlevel.library` lub port gry | nowa, mała |
| `TimerGetTimeStamp/Sleep/FrameUpdate` | `amigagfx_millis()` | `amiga_gfx.c` |
| `SoundPlaySample(index,vol,pitch)` | Paula DMA, 4 kanały z priorytetem | `amiga_audio.c` |
| `SoundPlaySong` (HMI MIDI) | pre-render → strumień Paula | `amiga_m.cpp`, `amiga_mscan.c` |
| `SoundStream*` (dźwięk z AVI) | stub w pierwszej fazie | — |
| `DirFindFirst/Next`, `DirMake*Name` | `dos.library` | nowa, ~150 linii |

**Zero-copy framebuffer.** Gra rysuje do własnego bufora opisanego strukturą `DATA_007773`:

```asm
DATA_007773:    ; Buffer structure for whole backbuffer
        dd DATA_009226          ; wskaźnik na piksele (w BSS)
        db 0,0,0,0, 0,0,0,0     ; x, y
        db 0x40,0x1,0,0         ; 320   ← little-endian!
        db 0xc8,0,0,0           ; 200
        db 0,0,0,0              ; jump
```

Wystarczy przy starcie ustawić `DATA_009226` na `amigagfx_chunky()` i **`VideoBlit` staje się
samym `amigagfx_blit(0,0,320,200)` — bez kopiowania 64 KB na klatkę.**
Jest też druga struktura `DATA_007774` opisująca "upper backbuffer" 320×158 — to okno 3D nad
konsolą. Czyli gra sama daje nam informację, że przy locie w kosmosie **wystarczy c2p 158 linii,
a nie 200** — 21 % oszczędności na najgorętszej ścieżce.

**Wskaźnik myszy.** `FUNC_001637_FlipScreen` pokazuje, że gra rysuje kursor programowo
(save-blit → masked-blit). `VideoPointerExclusive()` zwracające 0 wyłącza to i pozwala użyć
sprzętowego wskaźnika Intuition — mniej pracy CPU i mniej brudnych prostokątów.

**Dźwięk.** Sample to pliki `fx/*.raw`, 16-bit signed mono 22050 Hz (widać po `size/2`
w `sdlsound.cpp`). Konwersja do 8-bit dla Pauli to wzięcie starszego bajtu; okres PAL dla
22050 Hz to ~161, znacznie powyżej limitu 124. Wersja SDL miksuje 16 kanałów, ale tablica
deskryptorów pokazuje ~8 stałych głosów (wiatr, stacja, ECM, syrena, silnik) — do 4 kanałów Pauli
mapuje się priorytetowo, dokładnie jak w sterowniku SFX OpenTTD.
Muzyka to 17 utworów HMI MIDI (`qqfront`, `hallking`, `valkries`, `bluedan`, `ggofkiev`…) —
w wersji SDL **w ogóle niezaimplementowana**. Najprościej: pre-render do próbek i strumień przez
Paulę, czyli dokładnie mechanizm, który już działa w OpenTTD.

**Pliki zewnętrzne, których gra potrzebuje:** `mission.dat`, `cabin*.bmp`, `fx/*.raw`,
`music/*.hmp`. Filmy `data/*.avi` (300 MB) są opcjonalne. Reszta gry — grafika, modele 3D,
galaktyka — jest wkompilowana w binarium. **Brak skomplikowanego ładowania danych to duży plus.**

---

## 4. Trzy realne przeszkody

### 4.1 Endianness — największe ryzyko poprawnościowe

x86 jest little-endian, 68k big-endian. Kod przetłumaczony instrukcja-po-instrukcji jest
wewnętrznie spójny dla danych tworzonych w czasie działania, ale **1,46 MB danych statycznych
to problem**. Przykład z powyższej struktury bufora: `db 0x40,0x1,0,0` to LE-dword 320. Przepisane
jako surowe bajty na 68k dadzą 0x40010000.

Skala problemu, zmierzona:

- **`dd` (dword) — bezpieczne.** 4 072 + 4 438 + 454 dwordów, z czego 8 454 to **symboliczne
  wskaźniki** (deasembler odzyskał je z tabeli relokacji oryginalnego EXE — `RELOCOFF`/`RELOCSIZE`
  w `ffedis.cpp`). Tłumaczą się na `dc.l LABEL` i są poprawne z definicji. Reszta to stałe —
  `dd 0x1234` → `dc.l $1234` zachowuje wartość.
- **`db` — ryzykowne.** 144 KB w `ffedat.asm` + 1 301 KB w `ffebmp.asm`.
  - `ffebmp.asm` to w większości **bitmapy** (tablice bajtów, neutralne endianowo) oraz **kod
    modeli 3D**, który jest **16-bitowy** (format udokumentowany przez Jongware dla FFE i przez
    Marka Watsona dla FE2). To dobrze zdefiniowana, ograniczona transformacja: byte-swap słów
    w wydzielonym regionie. Region jest oznaczony — `ffebmp.asm:10542` ma komentarz
    `; MODEL DATA START`, a `ffebmp.asm:41814` to `; Ship object table` z nazwanymi statkami.
  - `ffedat.asm` to struktury gry. Skanowanie znajduje 388 linii wzorca `db n,0x0,0x0,0x0`
    (niemal na pewno LE-dwordy), ale to tylko dolna granica.

**Jak to rozwiązać — trzy uzupełniające się metody:**
1. Tabela relokacji już rozwiązuje wszystkie wskaźniki.
2. **Statyczna inferencja szerokości dostępu**: przeskanować `ffe.asm` i dla każdej etykiety
   `DATA_xxxx` zebrać, z jaką szerokością (`mov al` / `mov ax` / `mov eax` / `movsx`) jest czytana.
3. **Logowanie dostępów w czasie działania**: zbudować JJFFE na PC (warstwa SDL jest w repo),
   oinstrumentować dostępy do regionu danych statycznych i przegrać sporo gry. Daje empiryczną
   mapę typów, której nie da żadna analiza statyczna.

Te trzy razem dają wysoką pewność. To praca do wykonania, ale **ograniczona i mierzalna** — nie
jest to nieokreślone ryzyko.

### 4.2 Translator x86 → 68k

Nie istnieje gotowe narzędzie (sprawdzone: PortAsm robi 68k→x86, czyli odwrotnie; GLFrontier
zrobił 68k→C/x86 dla Frontier: Elite II). **Trzeba go napisać.** Dobra wiadomość: przy 45
mnemonikach, płaskim modelu pamięci i braku FPU jest to narzędzie rzędu 2–4 tys. linii Pythona.

Kluczowy szczęśliwy zbieg okoliczności: **flagi x86 mapują się niemal 1:1 na CCR 68k.**
ZF→Z, SF→N, CF→C (pożyczka po odejmowaniu ma tę samą polaryzację), OF→V. Skoki warunkowe:
`jz`→`beq`, `jnz`→`bne`, `jl`→`blt`, `jg`→`bgt`, `jc`→`bcs`, `ja`→`bhi`, `js`→`bmi`.
Pułapki do obsłużenia: `inc`/`dec` nie zmieniają CF (a `addq`/`subq` zmieniają C i X) oraz
odwrotna kolejność operandów w `cmp` (x86 `cmp a,b` = a−b; 68k `cmp src,dst` = dst−src).

Mapowanie rejestrów: `eax`→`d0`, `ecx`→`d1`, `edx`→`d2`, `ebx`→`d3`, `esi`→`a2`, `edi`→`a3`,
`ebp`→`a5`, `esp`→`a7`; `a0`/`a1` jako scratch dla operandów pamięciowych z bazą w rejestrze
danych. Tryb adresowania 68020 `(d16,An,Dn*4)` pokrywa x86 `[base+index*scale+disp]`.
Argumenty na stosie (cdecl) przechodzą bez zmian: `push dword X` → `move.l X,-(sp)`,
`add esp,8` → `addq.l #8,sp`.

**Optymalizacje, które translator może zrobić automatycznie:**
- inline `FUNC_001521`/`1522`/`1523` w 500+ miejscach → `muls.l`/`divs.l` zamiast wywołania,
- eliminacja martwego liczenia flag (proste, bo widać następną instrukcję),
- `mov eax,[x]; push eax` → `move.l x,-(sp)`.

### 4.3 Wydajność

Punkty odniesienia (z FFE FAQ i porównań CPU — traktować jako rząd wielkości, nie precyzję):

| sprzęt | klasa | oczekiwane FFE |
|---|---|---|
| 386DX/33 | minimum wg pudełka | pokaz slajdów |
| 486DLC/40 | ~15–20 MIPS | „ledwo grywalne" na low detail |
| **68030/50** | ~15–18 MIPS, słabsza cache | **marginalne — low detail, obniżony zasięg** |
| **68040/40** | ~35 MIPS | **grywalne, klasa 486DX/33–40** |
| **68060/50** | ~67 MIPS, `muls.l` w 2 cyklach | **komfortowo** |

Do tego dochodzi narzut tłumaczenia (szacunek: 1,3–1,8×) i c2p, ale odejmują się nasze przewagi:
inline mnożenia fixed-point, ręcznie pisane `DrawHLine`/`DrawSpans`/blity w 68k, sprzętowy
wskaźnik, zero-copy framebuffer, c2p tylko 158 linii w locie.

Koszt c2p przy 320×200×8bpp (64 000 pikseli), z bibliotek już w `I:\GITHUB\Amiga_OpenTTD\native\`:
z grubsza ~20–25 ms na 030/50, ~7–8 ms na 040/25, ~2–3 ms na 060/50. **Na RTG (Picasso96) to
zwykłe memcpy** — a wtedy nawet 030 nie płaci nic za wyświetlanie.

FFE ma **wbudowane poziomy detali** — to naturalna dźwignia dla słabszego sprzętu, dokładnie
ta sama strategia co „low detail" na 486.

Wniosek: **060 = bardzo dobrze, 040 = grywalnie, 030/50 = na granicy przy low detail.**
To ten sam kształt wyniku, co osiągnęliście w OpenTTD.

---

## 5. Porównanie strategii

| | (A) x86 → asm 68k | (B) x86 → C (transliteracja, styl N64Recomp) | (C) dekompilacja do czytelnego C |
|---|---|---|---|
| nakład na start | średni (translator ~2–4k linii) | mniejszy (brak backendu 68k) | ogromny (1 621 funkcji) |
| wydajność | przewidywalna, najlepsza | zależna od bebbo GCC 6.5 | potencjalnie najlepsza |
| ryzyko toolchainu | niskie (vasm/GNU as) | **wysokie** — 113k instrukcji patologicznego C przez GCC, który już wam robił ICE i miscompile przy `-O1` | średnie |
| debugowalność | trudna | średnia | dobra |
| czas do pierwszego uruchomienia | tygodnie | tygodnie | miesiące |

**Rekomendacja: (A) jako ścieżka główna, (C) selektywnie i przyrostowo.**

Uzasadnienie: bebbo GCC 6.5 jest w waszym projekcie OpenTTD udokumentowanym źródłem problemów
(5 plików musi iść na `-O0`, zakaz `-O2`, ICE w `MakeDirty()`). Rzucenie w niego 113 tys.
instrukcji transliterowanego C to proszenie się o kłopoty, których nie da się zdiagnozować.
Translator do asemblera generuje jeden `.s` na funkcję, składa się `vasm`/`m68k-amigaos-as`
i nie ma tam żadnego optymalizatora, który mógłby popsuć kod.

Ścieżka (C) wchodzi później i punktowo: **278 z 1 621 funkcji ma już nadane nazwy przez
Johna Jordana** i obejmują one cały rasteryzator — `DrawHLine`, `DrawLine`, `DrawTriangle`,
`DrawQuad`, `DrawTexTriangle`, `DrawTexQuad`, `DrawSpans`, `DrawParticle`, `DrawCircle`,
`DrawSprite`, `DrawChar`, `Blit*`, `DPal*`. To jest dokładnie ta lista, którą warto przepisać
ręcznie w 68k. Dodatkowo `srctools/*.txt` zawiera pseudo-C fizyki, generatora systemów, BBS-u
i struktur gry — gotowa dokumentacja do dekompilacji.

**Odrzucone:** emulacja x86 na 68k (386 protected mode interpretowany na 060 to bez szans);
przepisanie gry od zera na silniku FE2 (to nie port, tylko nowa gra — logika misji, BBS,
generator galaktyki FFE to lata pracy).

---

## 6. Co jeszcze jest do wykorzystania

- **`I:\GITHUB\Amiga_OpenTTD\native\openttd\amiga_gfx.c` + `.h`** — ekran 8bpp chunky z trzema
  backendami (AGA c2p / EHB 6-bitplane / RTG Picasso96), wejście, timer, splash, log, warp
  wskaźnika. Napisana z zasadą „zero nagłówków Amigi w kodzie gry", co tu jest równie przydatne.
- **`I:\GITHUB\Amiga_OpenTTD\native\openttd\amiga_audio.c` + `amiga_s.cpp`** — Paula, 4 kanały,
  DMA, bez wątków, plus strumieniowanie muzyki z podwójnym buforowaniem.
- **`I:\GITHUB\Amiga_OpenTTD\native\c2p1x1_6_c5_bm_040.s`, `c2p_rect.s`, `c2p_glue.s`** — c2p.
- **Cała infrastruktura testowa**: WinUAE + konfiguracje 030/040/RTG/AGA, pętla
  build → deploy → screenshot → decyzja, `build-with-ice-retry.sh`.
- **`https://github.com/watsonmw/fe2-intro`** — reimplementacja w C renderera 3D i audio
  Frontier: Elite II, **działająca natywnie na Amidze z RTG**, aktualizowana (czerwiec 2026).
  Autor deklaruje pokrycie 95 % kodu graficznego FE2, ma kompilator i dekompilator formatu
  modeli 3D. FFE używa ewolucji tego samego formatu bytecode'u modeli — **to jest referencja
  numer jeden przy rozgryzaniu `ffebmp.asm` i przy pisaniu szybkiego rasteryzatora 68k.**
  Uwaga: fe2-intro wymaga RTG (nie ma c2p) — my mamy c2p, więc pod tym względem stoimy lepiej.
- **`https://github.com/pcercuei/glfrontier`** — Tom Morton zrobił dokładnie odwrotną operację
  (68k → C / i386) dla FE2. Wart przeczytania jako wzorzec architektury translatora.
- **Dokumentacja Jongware** formatu model code FFE (`jongware.com/galaxy7.html`).

---

## 7. Proponowany plan etapowy

**M0 — referencja na PC.** Zbudować JJFFE z warstwą SDL na Windows/Linux. Bez działającej
referencji nie ma differential testingu, a bez niego translator jest nieweryfikowalny.
NASM 0.98 jest wymagany przez oryginalny build; sprawdzić czy nowszy NASM daje identyczny obraz.

**M1 — translator, prototyp.** Pokrycie ~45 mnemoników. Weryfikacja: harness, który uruchamia
tę samą funkcję liściową na PC (build NASM) i na 68k (WinUAE) na tych samych danych wejściowych
i porównuje wyjście bajt po bajcie. Zacząć od `FUNC_001521` (mnożenie fixed-point), `Sin32`,
`Int64Add64`, `DrawHLine`.

**M2 — typowanie danych i endianness.** Trzy metody z §4.1. Produkt: generator, który z
`ffedat.asm`/`ffebmp.asm` robi big-endianowy obraz danych + raport „bajty, których typu nie
ustaliliśmy". Ten raport jest miarą ryzyka.

**M3 — warstwa platformowa na Amidze.** 40 funkcji `ffeapi.h` na `amiga_gfx.c`/`amiga_audio.c`.
Do zrobienia od zera: mapowanie rawkey→scancode, joystick, `DirFindFirst/Next`.

**M4 — pierwszy start.** Cel: intro i menu na ekranie. To jest bramka ryzyka — jeśli tu się uda,
reszta to optymalizacja i polerowanie.

**M5 — optymalizacja.** Inline fixed-point, ręczny 68k dla rasteryzatora (lista 278 nazwanych
funkcji), sprzętowy wskaźnik, c2p tylko okna 3D, poziomy detali, profilowanie.

**M6 — dźwięk i muzyka.** Paula SFX z priorytetem kanałów, pre-render 17 utworów.

Bramki decyzyjne: po M1 wiadomo, czy translator jest wykonalny (jeśli nie — plan B to ścieżka C).
Po M2 wiadomo, jak duże jest ryzyko endianness. Po M4 wiadomo, jaka jest realna wydajność.

---

## 8. Licencje — stan faktyczny

**JJFFE nie ma żadnej licencji.** Zweryfikowane: pole `license` w API GitHuba to `null`, w repo
nie ma pliku `LICENSE` ani `COPYING`, i żaden plik źródłowy nie zawiera nagłówka licencyjnego.
Jedyne noty copyright w całym drzewie dotyczą **oryginalnej gry**:

- `ref\jjffe\ffedat.asm:18331` — `(C) Copyright 1995 D.J.Braben. All rights reserved`
- `ref\jjffe\ffe.asm:68223` — `Original game copyright Frontier Developments` (napis na splashu)

Brak licencji to nie domena publiczna — to „wszelkie prawa zastrzeżone" z mocy prawa. Do tego są
tu **dwie warstwy praw**: praca RE Johna Jordana oraz leżąca pod spodem gra Brabena/Frontier
Developments. Ta druga dominuje, bo deasemblacja jest utworem zależnym od gry. FD zapowiedziało
otwarcie FFE w 2000 r. i nigdy tego nie zrobiło. Sam readme JJFFE mówi wprost: „this is a hacked
and reverse engineered version of a commercial game… It will be removed rapidly if anybody
complains". Repozytorium jest zarchiwizowane pod kontem `videogamepreservation`.

Referencje, z których moglibyśmy czerpać, są w tej samej sytuacji — **też bez licencji**:

| projekt | licencja |
|---|---|
| `videogamepreservation/jjffe` | brak (`null`), archiwum z 2015 |
| `watsonmw/fe2-intro` | brak (`null`), brak pliku LICENSE w root |
| `pcercuei/glfrontier` | brak (`null`) |
| c2p Kalmsa (`c2p1x1_6_c5_bm_040.s`) | brak formalnej licencji, tylko atrybucja autora; de facto swobodnie używane w scenie amigowej |

**Uwaga o GPL — realne ograniczenie, nie formalność.** `amiga_gfx.c`/`amiga_gfx.h` z portu OpenTTD
noszą nagłówek GPL v2 („This file is part of OpenTTD"). Napisałeś ten plik sam i nie zależy on od
niczego z OpenTTD (jego własny komentarz to podkreśla), więc jako posiadacz praw możesz go
przelicencjonować **na potrzeby tego projektu**. Ale jeśli zostawisz nagłówek GPL, to połączonej
całości nie da się legalnie dystrybuować: GPL wymaga, by cały utwór dawał się rozpowszechniać na
warunkach GPL, a część wywodząca się z FFE tego nie spełnia. Decyzję trzeba podjąć świadomie,
zanim kod trafi do wspólnego drzewa — nie po fakcie.

Praktyczny wniosek: taka sama postawa jak JJFFE — dystrybuować **wyłącznie przetłumaczony
plik wykonywalny**, wymagający oryginalnych plików danych gracza (`mission.dat`, `fx/`, `music/`,
`cabin*.bmp`). Budowanie i granie u siebie, na własnej kopii gry, to zupełnie inna sytuacja niż
publikacja — wszystkie powyższe ograniczenia gryzą dopiero przy dystrybucji. To wyraźnie inna
sytuacja prawna niż OpenTTD (czyste GPL) i decyzja o publikacji należy do Ciebie.

---

## 9. Ciekawostka historyczna

Amigowa wersja First Encounters **była planowana** — na A1200, A4000 i CD32, z premierą latem
1995 r. Nigdy nie powstała. Twórcy uważali więc, że 68020 uciągnie tę grę; nasze pomiary
arytmetyki to potwierdzają. Ten port dokończyłby coś, co anulowano 31 lat temu.

---

## Źródła

- [videogamepreservation/jjffe — źródła JJFFE](https://github.com/videogamepreservation/jjffe)
- [JJFFE — Elite Wiki](https://wiki.alioth.net/index.php/JJFFE)
- [watsonmw/fe2-intro — renderer FE2 w C, działa na Amidze z RTG](https://github.com/watsonmw/fe2-intro)
- [Frontier: Elite 2 — Graphics Study, Mark Watson](http://watsonmw.com/fintro/)
- [pcercuei/glfrontier — FE2 zreverse'owane przez Toma Mortona](https://github.com/pcercuei/glfrontier)
- [FFE FAQ — wymagania sprzętowe](https://www.jades.org/ffefaq2.htm)
- [Frontier: First Encounters — Wikipedia (anulowana wersja amigowa)](https://en.wikipedia.org/wiki/Frontier:_First_Encounters)
- [FFED3D — FrontierAstro](https://www.frontierastro.co.uk/Hires/hiresffe.html)
- [PortAsm/68K — komercyjny translator, kierunek odwrotny](https://microapl.com/Porting/PortAsm68Kto86.html)
- [68060 vs 486/Pentium — dyskusja o wydajności](https://www.amibay.com/threads/68060-equivalent-to-486-dx-or-pentium-in-performance.77763/)
