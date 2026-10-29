# GM sample bank: provenance and licences

<!-- WRITTEN BY tools/music/build_gmbank.py -- edit the script, not this file. -->

`tools/music/ffe_gm.bnk` (3066041 bytes) is built by `tools/music/build_gmbank.py` from two
repositories read at pinned commits, plus sounds the script generates itself.

**Every recorded sample in the bank is CC0 1.0 Universal (public domain dedication).**
No GeneralUser GS, TimGM6mb, FluidR3, MuseScore General or any other non-CC0 bank was used.

| repository | commit | licence |
|---|---|---|
| https://github.com/sgossner/VCSL | `c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e` | CC0 1.0 Universal (`LICENSE` at that commit, reproduced below) |
| https://github.com/sgossner/VSCO-2-CE | `440300901dfe9275fd84e0b7763af1f8443ae62e` | CC0 1.0 Universal (`LICENSE` at that commit, reproduced below) |

Notes on the licence check:

- VCSL: `LICENSE` is the CC0 1.0 Universal legal code; `README.md` says "This collection is under
  a Creative Commons 0 license. Essentially it's Public Domain- you can do whatever you want with
  these sounds (even make commercial software), no royalties, no credit, no special terms."
- VSCO-2-CE: `LICENSE` is the CC0 1.0 Universal legal code. Its older `Readme.txt` (2016) adds
  two non-binding *requests* -- not to sell the samples directly, and to credit Versilian
  Studios / Sam Gossner and Ivy Audio / Simon Dalzell. CC0 makes neither a condition; we honour
  both anyway: the bank is not sold, and the credit is here and in the release notes.
- VSCO-2-CE samples are used only for what VCSL lacks: bowed and plucked strings, brass,
  flute, oboe, bassoon, clarinet, and one muted concert bass drum.

Credit: samples by Versilian Studios LLC (Sam Gossner) and contributors -- VCSL,
https://github.com/sgossner/VCSL -- and VS Chamber Orchestra: Community Edition, recorded by Sam
Gossner and Simon Dalzell (Ivy Audio), sample cutting by Elan Hickler (Soundemote).

## Generated sounds (ours, MIT, like the script)

These are computed by `build_gmbank.py` (integer-frequency additive synthesis, a seeded LCG
noise source, simple one-pole filters); no recording is involved:

- `organ`: organ at 110 Hz; organ at 220 Hz; organ at 440 Hz; organ at 55 Hz; organ at 880 Hz
- `eguitar`: eguitar at 110 Hz; eguitar at 220 Hz; eguitar at 440 Hz
- `odguitar`: odguitar at 110 Hz; odguitar at 220 Hz; odguitar at 440 Hz
- `distguitar`: distguitar at 110 Hz; distguitar at 220 Hz; distguitar at 440 Hz
- `ebass`: ebass at 110 Hz; ebass at 220 Hz; ebass at 55 Hz
- `synbass`: synbass at 110 Hz; synbass at 220 Hz; synbass at 55 Hz
- `sqlead`: sqlead at 110 Hz; sqlead at 220 Hz; sqlead at 440 Hz; sqlead at 880 Hz
- `sawlead`: sawlead at 110 Hz; sawlead at 220 Hz; sawlead at 440 Hz; sawlead at 880 Hz
- `synbrass`: synbrass at 110 Hz; synbrass at 220 Hz; synbrass at 440 Hz
- `pad`: pad at 110 Hz; pad at 220 Hz; pad at 440 Hz; pad at 880 Hz
- `choir`: choir at 110 Hz; choir at 220 Hz; choir at 440 Hz; choir at 880 Hz
- `steeldrum`: steeldrum at 220 Hz; steeldrum at 440 Hz; steeldrum at 880 Hz
- `fxbell`: fxbell at 220 Hz; fxbell at 440 Hz; fxbell at 880 Hz
- `breath`: breath (band-passed LCG noise, looped)
- `fret`: fret (noise burst and falling squeak)
- `bird`: bird (four sine chirps)
- `phone`: phone (440+480 Hz ring at 20 Hz, looped)
- `heli`: heli (noise chopped at 12 Hz, looped)
- `applause`: applause (260 LCG-placed noise claps, looped)
- `gunshot`: gunshot (decaying noise and 70 Hz thump)
- `kick_g`: kick_g (sine sweep 155->45 Hz with click)
- `whistle_s`: whistle_s (short whistle)
- `whistle_l`: whistle_l (long whistle)
- `cuica_m`: cuica_m (muted cuica sweep)
- `cuica_o`: cuica_o (open cuica sweep)

One derived sound: `revcym` is the VCSL clash cymbal `cymbal_crash1_ff2.wav` reversed (still CC0).

## Exact input files

Every file below was downloaded from `https://raw.githubusercontent.com/<owner>/<repo>/<commit>/<path>`.

- **piano** (161207 bytes in the bank)
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C0_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_F#0_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C1_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_F#1_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C2_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_F#2_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C3_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_F#3_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C4_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_F#4_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C5_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_F#5_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C6_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_F#6_v3_rr1_Player.wav`
  - `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_C7_v3_rr1_Player.wav`
- **epiano** (74288 bytes in the bank)
  - `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_C1_vl2.wav`
  - `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_C2_vl2.wav`
  - `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_C3_vl2.wav`
  - `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_C4_vl2.wav`
  - `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_C5_vl2.wav`
  - `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_C6_vl2.wav`
  - `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_C7_vl2.wav`
- **clav** (45731 bytes in the bank)
  - `vcsl:Electrophones/TX81Z/Clavisynth/Clavisynth_C0_vl2.wav`
  - `vcsl:Electrophones/TX81Z/Clavisynth/Clavisynth_C1_vl2.wav`
  - `vcsl:Electrophones/TX81Z/Clavisynth/Clavisynth_C2_vl2.wav`
  - `vcsl:Electrophones/TX81Z/Clavisynth/Clavisynth_C3_vl2.wav`
  - `vcsl:Electrophones/TX81Z/Clavisynth/Clavisynth_C4_vl2.wav`
  - `vcsl:Electrophones/TX81Z/Clavisynth/Clavisynth_C5_vl2.wav`
- **harpsi** (65787 bytes in the bank)
  - `vcsl:Chordophones/Zithers/Harpsichord, English/Sustains/Normal/ZuckermannKitHarpsi_Normal_Sus_A#0_rr1.wav`
  - `vcsl:Chordophones/Zithers/Harpsichord, English/Sustains/Normal/ZuckermannKitHarpsi_Normal_Sus_F#1_rr1.wav`
  - `vcsl:Chordophones/Zithers/Harpsichord, English/Sustains/Normal/ZuckermannKitHarpsi_Normal_Sus_D2_rr1.wav`
  - `vcsl:Chordophones/Zithers/Harpsichord, English/Sustains/Normal/ZuckermannKitHarpsi_Normal_Sus_A#2_rr1.wav`
  - `vcsl:Chordophones/Zithers/Harpsichord, English/Sustains/Normal/ZuckermannKitHarpsi_Normal_Sus_F#3_rr1.wav`
  - `vcsl:Chordophones/Zithers/Harpsichord, English/Sustains/Normal/ZuckermannKitHarpsi_Normal_Sus_D4_rr1.wav`
  - `vcsl:Chordophones/Zithers/Harpsichord, English/Sustains/Normal/ZuckermannKitHarpsi_Normal_Sus_A#4_rr1.wav`
  - `vcsl:Chordophones/Zithers/Harpsichord, English/Sustains/Normal/ZuckermannKitHarpsi_Normal_Sus_E5_rr1.wav`
- **glock** (50290 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Glockenspiel/glock_medium_G4_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Glockenspiel/glock_medium_C5_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Glockenspiel/glock_medium_G5_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Glockenspiel/glock_medium_C6_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Glockenspiel/glock_medium_G6_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Glockenspiel/glock_medium_C7_01.wav`
- **vibes** (57085 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/Vibes_hard_A2_v2_rr1_Main.wav`
  - `vcsl:Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/Vibes_hard_E3_v2_rr1_Main.wav`
  - `vcsl:Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/Vibes_hard_B3_v2_rr1_Main.wav`
  - `vcsl:Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/Vibes_hard_F4_v2_rr1_Main.wav`
  - `vcsl:Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/Vibes_hard_C5_v2_rr1_Main.wav`
  - `vcsl:Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/Vibes_hard_E5_v2_rr1_Main.wav`
- **marimba** (53292 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Marimba/Marimba_hit_Outrigger_F1_med_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Marimba/Marimba_hit_Outrigger_C2_med_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Marimba/Marimba_hit_Outrigger_G2_med_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Marimba/Marimba_hit_Outrigger_B2_med_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Marimba/Marimba_hit_Outrigger_F3_med_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Marimba/Marimba_hit_Outrigger_C4_med_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Marimba/Marimba_hit_Outrigger_G4_med_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Marimba/Marimba_hit_Outrigger_B4_med_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Marimba/Marimba_hit_Outrigger_F5_med_01.wav`
  - `vcsl:Idiophones/Struck Idiophones/Marimba/Marimba_hit_Outrigger_C6_med_01.wav`
- **xylo** (40859 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Xylophone/Medium Mallets/Xylo_Medium_G3_pp_01_far.wav`
  - `vcsl:Idiophones/Struck Idiophones/Xylophone/Medium Mallets/Xylo_Medium_C4_pp_01_far.wav`
  - `vcsl:Idiophones/Struck Idiophones/Xylophone/Medium Mallets/Xylo_Medium_G4_pp_01_far.wav`
  - `vcsl:Idiophones/Struck Idiophones/Xylophone/Medium Mallets/Xylo_Medium_C5_pp_01_far.wav`
  - `vcsl:Idiophones/Struck Idiophones/Xylophone/Medium Mallets/Xylo_Medium_G5_pp_01_far.wav`
  - `vcsl:Idiophones/Struck Idiophones/Xylophone/Medium Mallets/Xylo_Medium_C6_pp_01_far.wav`
  - `vcsl:Idiophones/Struck Idiophones/Xylophone/Medium Mallets/Xylo_Medium_G6_pp_01_far.wav`
  - `vcsl:Idiophones/Struck Idiophones/Xylophone/Medium Mallets/Xylo_Medium_C7_pp_01_far.wav`
- **chimes** (54779 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Tubular Bells 1/chimes_C3_f_rr1.wav`
  - `vcsl:Idiophones/Struck Idiophones/Tubular Bells 1/chimes_F#3_fff_rr1.wav`
  - `vcsl:Idiophones/Struck Idiophones/Tubular Bells 1/chimes_C4_ff_rr2.wav`
  - `vcsl:Idiophones/Struck Idiophones/Tubular Bells 1/chimes_E4_ff_rr2.wav`
- **pipeorgan** (59262 bytes in the bank)
  - `vcsl:Aerophones/Edge-blown Aerophones/Pipe Organ/Loud/Rode_Man3Open_C1.wav`
  - `vcsl:Aerophones/Edge-blown Aerophones/Pipe Organ/Loud/Rode_Man3Open_C2.wav`
  - `vcsl:Aerophones/Edge-blown Aerophones/Pipe Organ/Loud/Rode_Man3Open_C3.wav`
  - `vcsl:Aerophones/Edge-blown Aerophones/Pipe Organ/Loud/Rode_Man3Open_C4.wav`
  - `vcsl:Aerophones/Edge-blown Aerophones/Pipe Organ/Loud/Rode_Man3Open_C5.wav`
  - `vcsl:Aerophones/Edge-blown Aerophones/Pipe Organ/Loud/Rode_Man3Open_C6.wav`
- **harmonica** (68123 bytes in the bank)
  - `vcsl:Aerophones/Free Aerophones/Harmonica-Hohner-Super64/Sustains/Normal/Hohner-Super64_Normal _C2.wav`
  - `vcsl:Aerophones/Free Aerophones/Harmonica-Hohner-Super64/Sustains/Normal/Hohner-Super64_Normal _G2.wav`
  - `vcsl:Aerophones/Free Aerophones/Harmonica-Hohner-Super64/Sustains/Normal/Hohner-Super64_Normal _E3.wav`
  - `vcsl:Aerophones/Free Aerophones/Harmonica-Hohner-Super64/Sustains/Normal/Hohner-Super64_Normal _C4.wav`
  - `vcsl:Aerophones/Free Aerophones/Harmonica-Hohner-Super64/Sustains/Normal/Hohner-Super64_Normal _G4.wav`
  - `vcsl:Aerophones/Free Aerophones/Harmonica-Hohner-Super64/Sustains/Normal/Hohner-Super64_Normal _E5.wav`
  - `vcsl:Aerophones/Free Aerophones/Harmonica-Hohner-Super64/Sustains/Normal/Hohner-Super64_Normal _C6.wav`
- **guitar** (57128 bytes in the bank)
  - `vcsl:Chordophones/Composite Chordophones/Strumstick/Finger/Strumstick_Finger_Str1_Main_D2_vl2_rr1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Strumstick/Finger/Strumstick_Finger_Str1_Main_G2_vl2_rr1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Strumstick/Finger/Strumstick_Finger_Str2_Main_C#3_vl2_rr1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Strumstick/Finger/Strumstick_Finger_Str3_Main_G3_vl2_rr1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Strumstick/Finger/Strumstick_Finger_Str3_Main_C#4_vl2_rr1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Strumstick/Finger/Strumstick_Finger_Str3_Main_G4_vl2_rr2.wav`
- **harp** (95232 bytes in the bank)
  - `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_B1_mf1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_F2_mf1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_C3_mf3.wav`
  - `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_G3_mf1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_D4_mf1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_A4_mf1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_E5_mf1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_B5_mf1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_F6_mf1.wav`
  - `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_B6_mf1.wav`
- **sax** (76158 bytes in the bank)
  - `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_G#1_vl2_rr1.wav`
  - `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_D2_vl2_rr1.wav`
  - `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_G#2_vl2_rr1.wav`
  - `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_D3_vl2_rr1.wav`
  - `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_G#3_vl2_rr1.wav`
  - `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_D4_vl2_rr1.wav`
  - `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_G#4_vl2_rr1.wav`
  - `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_C5_vl2_rr1.wav`
- **sopsax** (49042 bytes in the bank)
  - `vcsl:Aerophones/Reed Aerophones/Saxello/Non-Vibrato/BrettSaxello_SusNV_MainSpirit_A#2_vl2_rr2.wav`
  - `vcsl:Aerophones/Reed Aerophones/Saxello/Non-Vibrato/BrettSaxello_SusNV_MainSpirit_F#3_vl2_rr2.wav`
  - `vcsl:Aerophones/Reed Aerophones/Saxello/Non-Vibrato/BrettSaxello_SusNV_MainSpirit_D4_vl2_rr2.wav`
  - `vcsl:Aerophones/Reed Aerophones/Saxello/Non-Vibrato/BrettSaxello_SusNV_MainSpirit_A#4_vl2_rr2.wav`
  - `vcsl:Aerophones/Reed Aerophones/Saxello/Non-Vibrato/BrettSaxello_SusNV_MainSpirit_E5_vl2_rr2.wav`
- **recorder** (52087 bytes in the bank)
  - `vcsl:Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/AltRecorder_Sus_F3_rr1_Main.wav`
  - `vcsl:Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/AltRecorder_Sus_C4_rr1_Main.wav`
  - `vcsl:Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/AltRecorder_Sus_F#4_rr1_Main.wav`
  - `vcsl:Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/AltRecorder_Sus_C5_rr1_Main.wav`
  - `vcsl:Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/AltRecorder_Sus_E5_rr1_Main.wav`
- **ocarina** (41063 bytes in the bank)
  - `vcsl:Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus/StdOcarina_Sus_A3.wav`
  - `vcsl:Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus/StdOcarina_Sus_D4.wav`
  - `vcsl:Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus/StdOcarina_Sus_G#4.wav`
  - `vcsl:Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus/StdOcarina_Sus_C#5.wav`
- **koto** (48550 bytes in the bank)
  - `vcsl:Chordophones/Zithers/Dan Tranh/Normal/B1_f_1.wav`
  - `vcsl:Chordophones/Zithers/Dan Tranh/Normal/F#2_f_1.wav`
  - `vcsl:Chordophones/Zithers/Dan Tranh/Normal/B2_f_1.wav`
  - `vcsl:Chordophones/Zithers/Dan Tranh/Normal/F#3_f_1.wav`
  - `vcsl:Chordophones/Zithers/Dan Tranh/Normal/B3_f_1.wav`
  - `vcsl:Chordophones/Zithers/Dan Tranh/Normal/F#4_f_1.wav`
- **kalimba** (30423 bytes in the bank)
  - `vcsl:Idiophones/Plucked Idiophones/Kalimba, Kenya/Mbira6_Normal_MainSpirit_B2_k8_vl3_rr2.wav`
  - `vcsl:Idiophones/Plucked Idiophones/Kalimba, Kenya/Mbira6_Normal_MainSpirit_F#3_k10_vl3_rr2.wav`
  - `vcsl:Idiophones/Plucked Idiophones/Kalimba, Kenya/Mbira6_Normal_MainSpirit_B3_k12_vl3_rr2.wav`
  - `vcsl:Idiophones/Plucked Idiophones/Kalimba, Kenya/Mbira6_Normal_MainSpirit_F#4_k14_vl3_rr2.wav`
  - `vcsl:Idiophones/Plucked Idiophones/Kalimba, Kenya/Mbira6_Normal_MainSpirit_A4_k1_vl3_rr2.wav`
- **timpani** (35282 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Timpani 2/Hit/Timpani1A_hit_v3_rr1_main.wav`
  - `vcsl:Membranophones/Struck Membranophones/Timpani 2/Hit/Timpani4A_hit_v3_rr1_main.wav`
- **strings** (126195 bytes in the bank)
  - `vsco:Strings/Cello Section/susvib/susvib_C1_v1_1.wav`
  - `vsco:Strings/Cello Section/susvib/susvib_G1_v1_1.wav`
  - `vsco:Strings/Cello Section/susvib/susvib_D2_v1_1.wav`
  - `vsco:Strings/Violin Section/susVib/VlnEns_susVib_G2_v1.wav`
  - `vsco:Strings/Violin Section/susVib/VlnEns_susVib_D3_v1.wav`
  - `vsco:Strings/Violin Section/susVib/VlnEns_susVib_A3_v1.wav`
  - `vsco:Strings/Violin Section/susVib/VlnEns_susVib_E4_v1.wav`
  - `vsco:Strings/Violin Section/susVib/VlnEns_susVib_B4_v1.wav`
  - `vsco:Strings/Violin Section/susVib/VlnEns_susVib_D5_v1.wav`
- **pizz** (48474 bytes in the bank)
  - `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_E0_v1_rr1.wav`
  - `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_A#0_v1_rr1.wav`
  - `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_D1_v1_rr1.wav`
  - `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_G#1_v1_rr1.wav`
  - `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_C#2_v1_rr1.wav`
  - `vsco:Strings/Violin Section/Pizz/VlnEns_Pizz_G2_v1_rr1.wav`
  - `vsco:Strings/Violin Section/Pizz/VlnEns_Pizz_D3_v1_rr1.wav`
  - `vsco:Strings/Violin Section/Pizz/VlnEns_Pizz_A3_v1_rr1.wav`
  - `vsco:Strings/Violin Section/Pizz/VlnEns_Pizz_E4_v1_rr1.wav`
  - `vsco:Strings/Violin Section/Pizz/VlnEns_Pizz_B4_v1_rr1.wav`
  - `vsco:Strings/Violin Section/Pizz/VlnEns_Pizz_D5_v1_rr1.wav`
- **cbpizz** (31242 bytes in the bank)
  - `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_E0_v1_rr1.wav`
  - `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_A#0_v1_rr1.wav`
  - `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_D1_v1_rr1.wav`
  - `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_G#1_v1_rr1.wav`
  - `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_C#2_v1_rr1.wav`
  - `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_G#2_v1_rr1.wav`
- **trumpet** (57126 bytes in the bank)
  - `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_F2_v3_rr1.wav`
  - `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_C3_v3_rr1.wav`
  - `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_G3_v3_rr1.wav`
  - `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_D4_v3_rr1.wav`
  - `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_A4_v3_rr1.wav`
  - `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_C5_v3_rr1.wav`
- **trombone** (39887 bytes in the bank)
  - `vsco:Brass/Tenor Trombone/sus/tenortbn_sus_D#1_v3_1.wav`
  - `vsco:Brass/Tenor Trombone/sus/tenortbn_sus_A#1_v3_1.wav`
  - `vsco:Brass/Tenor Trombone/sus/tenortbn_sus_F2_v3_1.wav`
  - `vsco:Brass/Tenor Trombone/sus/tenortbn_sus_C3_v3_1.wav`
  - `vsco:Brass/Tenor Trombone/sus/tenortbn_sus_F3_v3_1.wav`
- **tuba** (30408 bytes in the bank)
  - `vsco:Brass/Tuba/sus/Tuba3_sus_A#0_v2_rr1_Mid.wav`
  - `vsco:Brass/Tuba/sus/Tuba3_sus_F1_v2_rr1_Mid.wav`
  - `vsco:Brass/Tuba/sus/Tuba3_sus_A#1_v2_rr1_Mid.wav`
  - `vsco:Brass/Tuba/sus/Tuba3_sus_F2_v2_rr1_Mid.wav`
  - `vsco:Brass/Tuba/sus/Tuba3_sus_A#2_v2_rr1_Mid.wav`
- **horn** (43877 bytes in the bank)
  - `vsco:Brass/F Horn/sus/MOHorn_sus_C1_v2_1.wav`
  - `vsco:Brass/F Horn/sus/MOHorn_sus_G1_v2_1.wav`
  - `vsco:Brass/F Horn/sus/MOHorn_sus_D2_v2_1.wav`
  - `vsco:Brass/F Horn/sus/MOHorn_sus_A2_v2_1.wav`
  - `vsco:Brass/F Horn/sus/MOHorn_sus_C3_v2_1.wav`
- **brass** (0 bytes in the bank)
  - `vsco:Brass/Tenor Trombone/sus/tenortbn_sus_D#1_v3_1.wav`
  - `vsco:Brass/Tenor Trombone/sus/tenortbn_sus_A#1_v3_1.wav`
  - `vsco:Brass/Tenor Trombone/sus/tenortbn_sus_F2_v3_1.wav`
  - `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_C3_v3_rr1.wav`
  - `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_G3_v3_rr1.wav`
  - `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_D4_v3_rr1.wav`
  - `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_A4_v3_rr1.wav`
  - `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_C5_v3_rr1.wav`
- **flute** (60110 bytes in the bank)
  - `vsco:Woodwinds/Flute/susNV/LDFlute_susNV_C3_v1_1.wav`
  - `vsco:Woodwinds/Flute/susNV/LDFlute_susNV_A3_v1_1.wav`
  - `vsco:Woodwinds/Flute/susNV/LDFlute_susNV_E4_v1_1.wav`
  - `vsco:Woodwinds/Flute/susNV/LDFlute_susNV_C5_v1_1.wav`
  - `vsco:Woodwinds/Flute/susNV/LDFlute_susNV_A5_v1_1.wav`
  - `vsco:Woodwinds/Flute/susNV/LDFlute_susNV_C6_v1_1.wav`
- **oboe** (57050 bytes in the bank)
  - `vsco:Woodwinds/Oboe/Sus/Oboe_Sus_A#2_v3_Main.wav`
  - `vsco:Woodwinds/Oboe/Sus/Oboe_Sus_F3_v3_Main.wav`
  - `vsco:Woodwinds/Oboe/Sus/Oboe_Sus_A#3_v3_Main.wav`
  - `vsco:Woodwinds/Oboe/Sus/Oboe_Sus_F4_v3_Main.wav`
  - `vsco:Woodwinds/Oboe/Sus/Oboe_Sus_A#4_v3_Main.wav`
  - `vsco:Woodwinds/Oboe/Sus/Oboe_Sus_F5_v3_Main.wav`
- **bassoon** (51192 bytes in the bank)
  - `vsco:Woodwinds/Bassoon/sus/PSBassoon_A#0_v2_1.wav`
  - `vsco:Woodwinds/Bassoon/sus/PSBassoon_F1_v2_1.wav`
  - `vsco:Woodwinds/Bassoon/sus/PSBassoon_C2_v2_1.wav`
  - `vsco:Woodwinds/Bassoon/sus/PSBassoon_G2_v2_1.wav`
  - `vsco:Woodwinds/Bassoon/sus/PSBassoon_D#3_v2_1.wav`
  - `vsco:Woodwinds/Bassoon/sus/PSBassoon_A3_v2_1.wav`
  - `vsco:Woodwinds/Bassoon/sus/PSBassoon_D#4_v2_1.wav`
- **clarinet** (65098 bytes in the bank)
  - `vsco:Woodwinds/Clarinet/susLong/DCClar_susLong_D2_v2_rr1_sum.wav`
  - `vsco:Woodwinds/Clarinet/susLong/DCClar_susLong_A#2_v2_rr1_sum.wav`
  - `vsco:Woodwinds/Clarinet/susLong/DCClar_susLong_F3_v2_rr1_sum.wav`
  - `vsco:Woodwinds/Clarinet/susLong/DCClar_susLong_A#3_v2_rr1_sum.wav`
  - `vsco:Woodwinds/Clarinet/susLong/DCClar_susLong_F4_v2_rr1_sum.wav`
  - `vsco:Woodwinds/Clarinet/susLong/DCClar_susLong_A#4_v2_rr1_sum.wav`
  - `vsco:Woodwinds/Clarinet/susLong/DCClar_susLong_D5_v2_rr1_sum.wav`
- **ocean** (33601 bytes in the bank)
  - `vcsl:Membranophones/Other Membranophones/Ocean Drum/OceanDrum_Sus_1_Mid.wav`
- **kick_c** (12801 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Bass Drum 1/BDrumNew_hit_v5_rr1_Sum.wav`
- **kick_m** (9601 bytes in the bank)
  - `vsco:VSCO 1 Percussion/drums/bass/bdrum_muted_ff_1.wav`
- **snare** (13231 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Snare Drum, Modern 1/Snare2_HitSN_v6_rr1_Mid.wav`
- **snare2** (11026 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Snare Drum, Modern 3/Snare4_HitSN_v4_rr1_Mid.wav`
- **sidestick** (6616 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Snare Drum, Rope Tension/RopeSnare_sidestick_Main_vl2_rr1.wav`
- **clap** (8821 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Claps/Clap_rr1.wav`
- **tom_lo** (12801 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Tom 2/Stick/TomL_HitS_v3_rr1_Mid.wav`
- **tom_hi** (11201 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Tom 1/Stick/TomH_HitS_v3_rr1_Mid.wav`
- **hh_closed** (6616 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Hi-Hat Cymbal/HiHat_HitC_v3_rr1_Mid.wav`
- **hh_pedal** (6616 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Hi-Hat Cymbal/HiHat_Close_rr1_Mid.wav`
- **hh_open** (19846 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Hi-Hat Cymbal/HiHat_HitO_rr1_Mid.wav`
- **crash** (33076 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Suspended Cymbal 1/susCymb1_hit_stick_f1.wav`
- **crash2** (33076 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Clash Cymbals 1/cymbal_crash1_mf1.wav`
- **ride** (28666 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Suspended Cymbal 2/susCymb2_hit_stick_mp1.wav`
- **ridebell** (26461 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Suspended Cymbal 1/susCymb1_hit_bell_mf1.wav`
- **tamb** (13231 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Tambourine 1/Tamb1_Hit_v2_rr1_Mid.wav`
- **cowbell** (11026 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Cowbells/Cowbell1_Normal_v3_rr1_Mid.wav`
- **vibraslap** (16001 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Vibraslap/Legacy/vibraslap_rr1.wav`
- **bongo_hi** (8821 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Bongos/BongoH_Hit1_v2_rr1_Mid.wav`
- **bongo_lo** (9923 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Bongos/BongoL_Hit1_v2_rr1_Mid.wav`
- **conga_mute** (6616 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Conga/Quinto_HitFM1_v2_rr1_Sum.wav`
- **conga_hi** (13231 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Conga/Conga_HitN_v2_rr1_Sum.wav`
- **conga_lo** (11201 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Conga/Tumba_HitN_v2_rr1_Sum.wav`
- **timbale** (11026 bytes in the bank)
  - `vcsl:Membranophones/Struck Membranophones/Snare Drum, Rope Tension/Low/RopeSnare_low_ns_Main_vl2_rr1.wav`
- **agogo_hi** (13231 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Agogo Bells/Agogo_High_v2_rr1_Mid.wav`
- **agogo_lo** (13231 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Agogo Bells/Agogo_Low_v2_rr1_Mid.wav`
- **cabasa** (6616 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Cabasa/Cabasa1_Hit_rr1_Mid.wav`
- **maracas** (6616 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Shaker, Small/Mid_Shaker_Slap_rr1.wav`
- **guiro_s** (6616 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Guiro/Guiro_Hit_rr1_Mid.wav`
- **guiro_l** (17641 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Guiro/Guiro_Med_rr1_Mid.wav`
- **claves** (6616 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Claves/Claves1_Hit_v2_rr1_Mid.wav`
- **woodblock** (6616 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Woodblock/wood_click_f_rr1.wav`
- **tri_mute** (8821 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Triangles/Triangle1_HitM_v1_rr2_Mid.wav`
- **tri_open** (28666 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Triangles/Triangle1_Hit_v2_rr1_Mid.wav`
- **shaker** (5850 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Shaker, Small/Mid_ShakerDouble_Down_rr1.wav`
- **sleigh** (17641 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Sleigh Bells/Sleighbells_Hit_rr1_Mid.wav`
- **marktree** (33076 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Mark Trees/Legacy/windchimes_asc1.wav`
- **revcym** (28666 bytes in the bank)
  - `vcsl:Idiophones/Struck Idiophones/Clash Cymbals 1/cymbal_crash1_ff2.wav (reversed)`

Read but not stored (octave-convention and tuning probes, and files rejected as more than
0.6 semitone off their name):

- `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_A#-1_v3_rr1_Player.wav`
- `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_A5_v3_rr1_Player.wav`
- `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_D#1_v3_rr1_Player.wav`
- `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_E2_v3_rr1_Player.wav`
- `vcsl:Chordophones/Zithers/Grand Piano, Kawai/Sustains/GPiano_sus_G#4_v3_rr1_Player.wav`
- `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_C0_vl2.wav`
- `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_E1_vl2.wav`
- `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_E4_vl2.wav`
- `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_G#0_vl2.wav`
- `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_G#3_vl2.wav`
- `vcsl:Electrophones/TX81Z/FM Piano/FMPiano_G#6_vl2.wav`
- `vcsl:Electrophones/TX81Z/Clavisynth/Clavisynth_C6_vl2.wav`
- `vcsl:Electrophones/TX81Z/Clavisynth/Clavisynth_E2_vl2.wav`
- `vcsl:Electrophones/TX81Z/Clavisynth/Clavisynth_E5_vl2.wav`
- `vcsl:Electrophones/TX81Z/Clavisynth/Clavisynth_G#2_vl2.wav`
- `vcsl:Electrophones/TX81Z/Clavisynth/Clavisynth_G#5_vl2.wav`
- `vcsl:Chordophones/Zithers/Harpsichord, English/Sustains/Normal/ZuckermannKitHarpsi_Normal_Sus_C4_rr1.wav`
- `vcsl:Chordophones/Zithers/Harpsichord, English/Sustains/Normal/ZuckermannKitHarpsi_Normal_Sus_D3_rr1.wav`
- `vcsl:Chordophones/Zithers/Harpsichord, English/Sustains/Normal/ZuckermannKitHarpsi_Normal_Sus_E2_rr1.wav`
- `vcsl:Chordophones/Zithers/Harpsichord, English/Sustains/Normal/ZuckermannKitHarpsi_Normal_Sus_G#1_rr1.wav`
- `vcsl:Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/Vibes_hard_A4_v2_rr1_Main.wav`
- `vcsl:Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/Vibes_hard_C3_v2_rr1_Main.wav`
- `vcsl:Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/Vibes_hard_D4_v2_rr1_Main.wav`
- `vcsl:Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/Vibes_hard_F2_v2_rr1_Main.wav`
- `vcsl:Idiophones/Struck Idiophones/Vibraphone/Hard Mallets/Vibes_hard_G3_v2_rr1_Main.wav`
- `vcsl:Idiophones/Struck Idiophones/Tubular Bells 1/chimes_A#3_ff_rr1.wav`
- `vcsl:Idiophones/Struck Idiophones/Tubular Bells 1/chimes_D3_ff_rr1.wav`
- `vcsl:Idiophones/Struck Idiophones/Tubular Bells 1/chimes_D4_ff_rr2.wav`
- `vcsl:Idiophones/Struck Idiophones/Tubular Bells 1/chimes_E3_ff_rr2.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Pipe Organ/Loud/Rode_Man3Open_A1.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Pipe Organ/Loud/Rode_Man3Open_A4.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Pipe Organ/Loud/Rode_Man3Open_D#2.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Pipe Organ/Loud/Rode_Man3Open_D#5.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Pipe Organ/Loud/Rode_Man3Open_F#3.wav`
- `vcsl:Aerophones/Free Aerophones/Harmonica-Hohner-Super64/Sustains/Normal/Hohner-Super64_Normal _G3.wav`
- `vcsl:Aerophones/Free Aerophones/Harmonica-Hohner-Super64/Sustains/Normal/Hohner-Super64_Normal _G5.wav`
- `vcsl:Chordophones/Composite Chordophones/Strumstick/Finger/Strumstick_Finger_Str3_Main_A4_vl2_rr1.wav`
- `vcsl:Chordophones/Composite Chordophones/Strumstick/Finger/Strumstick_Finger_Str3_Main_D4_vl2_rr1.wav`
- `vcsl:Chordophones/Composite Chordophones/Strumstick/Finger/Strumstick_Finger_Str3_Main_F#3_vl2_rr1.wav`
- `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_A2_mf1.wav`
- `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_D2_mf1.wav`
- `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_E3_mf1.wav`
- `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_F4_mf1.wav`
- `vcsl:Chordophones/Composite Chordophones/Concert Harp/KSHarp_G5_mf1.wav`
- `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_A#1_vl2_rr1.wav`
- `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_A#4_vl2_rr1.wav`
- `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_C4_vl2_rr1.wav`
- `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_E2_vl2_rr1.wav`
- `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_E5_vl2_rr1.wav`
- `vcsl:Aerophones/Reed Aerophones/Tenor Saxophone/Non-Vibrato/BrettTenor_NV_Main_F#4_vl2_rr1.wav`
- `vcsl:Aerophones/Reed Aerophones/Saxello/Non-Vibrato/BrettSaxello_SusNV_MainSpirit_A#3_vl2_rr2.wav`
- `vcsl:Aerophones/Reed Aerophones/Saxello/Non-Vibrato/BrettSaxello_SusNV_MainSpirit_D3_vl2_rr2.wav`
- `vcsl:Aerophones/Reed Aerophones/Saxello/Non-Vibrato/BrettSaxello_SusNV_MainSpirit_F#4_vl2_rr2.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/AltRecorder_Sus_A#3_rr1_Main.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/AltRecorder_Sus_D4_rr1_Main.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/AltRecorder_Sus_E4_rr1_Main.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Baroque Alto Recorder/Sustain/AltRecorder_Sus_G#3_rr1_Main.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus/StdOcarina_Sus_A#4.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus/StdOcarina_Sus_A4.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus/StdOcarina_Sus_B3.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus/StdOcarina_Sus_C#4.wav`
- `vcsl:Aerophones/Edge-blown Aerophones/Ocarina, Typical/Sustains/Sus/StdOcarina_Sus_C5.wav`
- `vcsl:Chordophones/Zithers/Dan Tranh/Normal/C#2_f_1.wav`
- `vcsl:Chordophones/Zithers/Dan Tranh/Normal/C#4_f_1.wav`
- `vcsl:Chordophones/Zithers/Dan Tranh/Normal/D#3_f_1.wav`
- `vcsl:Chordophones/Zithers/Dan Tranh/Normal/G#3_f_1.wav`
- `vcsl:Idiophones/Plucked Idiophones/Kalimba, Kenya/Mbira6_Normal_MainSpirit_B4_k15_vl3_rr2.wav`
- `vcsl:Idiophones/Plucked Idiophones/Kalimba, Kenya/Mbira6_Normal_MainSpirit_C#4_k2_vl3_rr2.wav`
- `vcsl:Idiophones/Plucked Idiophones/Kalimba, Kenya/Mbira6_Normal_MainSpirit_D#3_k9_vl3_rr2.wav`
- `vcsl:Idiophones/Plucked Idiophones/Kalimba, Kenya/Mbira6_Normal_MainSpirit_G#3_k4_vl3_rr2.wav`
- `vsco:Strings/Cello Section/susvib/susvib_A2_v1_1.wav`
- `vsco:Strings/Cello Section/susvib/susvib_B3_v1_1.wav`
- `vsco:Strings/Cello Section/susvib/susvib_C3_v1_1.wav`
- `vsco:Strings/Cello Section/susvib/susvib_D4_v1_1.wav`
- `vsco:Strings/Cello Section/susvib/susvib_E3_v1_1.wav`
- `vsco:Strings/Cello Section/susvib/susvib_F4_v1_1.wav`
- `vsco:Strings/Cello Section/susvib/susvib_G3_v1_1.wav`
- `vsco:Strings/Violin Section/susVib/VlnEns_susVib_A2_v1.wav`
- `vsco:Strings/Violin Section/susVib/VlnEns_susVib_B2_v1.wav`
- `vsco:Strings/Violin Section/susVib/VlnEns_susVib_C4_v1.wav`
- `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_B2_v1_rr1.wav`
- `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_C1_v1_rr1.wav`
- `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_E2_v1_rr1.wav`
- `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_F#1_v1_rr1.wav`
- `vsco:Strings/Violin Section/Pizz/VlnEns_Pizz_A2_v1_rr1.wav`
- `vsco:Strings/Violin Section/Pizz/VlnEns_Pizz_B2_v1_rr1.wav`
- `vsco:Strings/Violin Section/Pizz/VlnEns_Pizz_C4_v1_rr1.wav`
- `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_E1_v1_rr1.wav`
- `vsco:Strings/Solo Contrabass/Pizz/BKCtbss_Pizz_A1_v1_rr1.wav`
- `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_A#3_v3_rr1.wav`
- `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_A2_v3_rr1.wav`
- `vsco:Brass/Trumpet/sus/Sum_SHTrumpet_sus_D#3_v3_rr1.wav`
- `vsco:Brass/Tenor Trombone/sus/tenortbn_sus_C#3_v3_1.wav`
- `vsco:Brass/Tenor Trombone/sus/tenortbn_sus_D#3_v3_1.wav`
- `vsco:Brass/Tenor Trombone/sus/tenortbn_sus_D2_v3_1.wav`
- `vsco:Brass/Tenor Trombone/sus/tenortbn_sus_F1_v3_1.wav`
- `vsco:Brass/Tuba/sus/Tuba3_sus_D#1_v2_rr1_Mid.wav`
- `vsco:Brass/Tuba/sus/Tuba3_sus_D2_v2_rr1_Mid.wav`
- `vsco:Brass/F Horn/sus/MOHorn_sus_A#1_v2_1.wav`
- `vsco:Brass/F Horn/sus/MOHorn_sus_D#1_v2_1.wav`
- `vsco:Brass/F Horn/sus/MOHorn_sus_F2_v2_1.wav`
- `vsco:Woodwinds/Flute/susNV/LDFlute_susNV_A4_v1_1.wav`
- `vsco:Woodwinds/Flute/susNV/LDFlute_susNV_C4_v1_1.wav`
- `vsco:Woodwinds/Flute/susNV/LDFlute_susNV_E3_v1_1.wav`
- `vsco:Woodwinds/Oboe/Sus/Oboe_Sus_D3_v3_Main.wav`
- `vsco:Woodwinds/Oboe/Sus/Oboe_Sus_D4_v3_Main.wav`
- `vsco:Woodwinds/Oboe/Sus/Oboe_Sus_D5_v3_Main.wav`
- `vsco:Woodwinds/Bassoon/sus/PSBassoon_A1_v2_1.wav`
- `vsco:Woodwinds/Bassoon/sus/PSBassoon_C#1_v2_1.wav`
- `vsco:Woodwinds/Bassoon/sus/PSBassoon_C3_v2_1.wav`
- `vsco:Woodwinds/Clarinet/susLong/DCClar_susLong_D3_v2_rr1_sum.wav`
- `vsco:Woodwinds/Clarinet/susLong/DCClar_susLong_D4_v2_rr1_sum.wav`
- `vsco:Woodwinds/Clarinet/susLong/DCClar_susLong_F#5_v2_rr1_sum.wav`

## GM program map

| prog | GM name | instrument | prog | GM name | instrument |
|---|---|---|---|---|---|
| 0 | Acoustic Grand | `piano` | 64 | Soprano Sax | `sopsax` |
| 1 | Bright Acoustic | `piano` | 65 | Alto Sax | `sax` |
| 2 | Electric Grand | `piano` | 66 | Tenor Sax | `sax` |
| 3 | Honky-tonk | `piano` | 67 | Baritone Sax | `sax` |
| 4 | Electric Piano 1 | `epiano` | 68 | Oboe | `oboe` |
| 5 | Electric Piano 2 | `epiano` | 69 | English Horn | `oboe` |
| 6 | Harpsichord | `harpsi` | 70 | Bassoon | `bassoon` |
| 7 | Clavinet | `clav` | 71 | Clarinet | `clarinet` |
| 8 | Celesta | `glock` | 72 | Piccolo | `flute` |
| 9 | Glockenspiel | `glock` | 73 | Flute | `flute` |
| 10 | Music Box | `glock` | 74 | Recorder | `recorder` |
| 11 | Vibraphone | `vibes` | 75 | Pan Flute | `recorder` |
| 12 | Marimba | `marimba` | 76 | Blown Bottle | `ocarina` |
| 13 | Xylophone | `xylo` | 77 | Shakuhachi | `recorder` |
| 14 | Tubular Bells | `chimes` | 78 | Whistle | `ocarina` |
| 15 | Dulcimer | `koto` | 79 | Ocarina | `ocarina` |
| 16 | Drawbar Organ | `organ` | 80 | Square Lead | `sqlead` |
| 17 | Percussive Organ | `organ` | 81 | Saw Lead | `sawlead` |
| 18 | Rock Organ | `organ` | 82 | Calliope Lead | `sqlead` |
| 19 | Church Organ | `pipeorgan` | 83 | Chiff Lead | `flute` |
| 20 | Reed Organ | `harmonica` | 84 | Charang Lead | `odguitar` |
| 21 | Accordion | `harmonica` | 85 | Voice Lead | `choir` |
| 22 | Harmonica | `harmonica` | 86 | Fifths Lead | `sawlead` |
| 23 | Tango Accordion | `harmonica` | 87 | Bass + Lead | `sawlead` |
| 24 | Nylon Guitar | `guitar` | 88 | New Age Pad | `pad` |
| 25 | Steel Guitar | `guitar` | 89 | Warm Pad | `pad` |
| 26 | Jazz Guitar | `eguitar` | 90 | Polysynth Pad | `sawlead` |
| 27 | Clean Guitar | `eguitar` | 91 | Choir Pad | `choir` |
| 28 | Muted Guitar | `eguitar` | 92 | Bowed Pad | `pad` |
| 29 | Overdriven Guitar | `odguitar` | 93 | Metallic Pad | `pad` |
| 30 | Distortion Guitar | `distguitar` | 94 | Halo Pad | `choir` |
| 31 | Guitar Harmonics | `fxbell` | 95 | Sweep Pad | `pad` |
| 32 | Acoustic Bass | `cbpizz` | 96 | Rain | `fxbell` |
| 33 | Fingered Bass | `ebass` | 97 | Soundtrack | `pad` |
| 34 | Picked Bass | `ebass` | 98 | Crystal | `fxbell` |
| 35 | Fretless Bass | `ebass` | 99 | Atmosphere | `pad` |
| 36 | Slap Bass 1 | `ebass` | 100 | Brightness | `pad` |
| 37 | Slap Bass 2 | `ebass` | 101 | Goblins | `choir` |
| 38 | Synth Bass 1 | `synbass` | 102 | Echoes | `fxbell` |
| 39 | Synth Bass 2 | `synbass` | 103 | Sci-fi | `fxbell` |
| 40 | Violin | `strings` | 104 | Sitar | `koto` |
| 41 | Viola | `strings` | 105 | Banjo | `guitar` |
| 42 | Cello | `strings` | 106 | Shamisen | `koto` |
| 43 | Contrabass | `strings` | 107 | Koto | `koto` |
| 44 | Tremolo Strings | `strings` | 108 | Kalimba | `kalimba` |
| 45 | Pizzicato Strings | `pizz` | 109 | Bagpipe | `oboe` |
| 46 | Orchestral Harp | `harp` | 110 | Fiddle | `strings` |
| 47 | Timpani | `timpani` | 111 | Shanai | `oboe` |
| 48 | String Ensemble 1 | `strings` | 112 | Tinkle Bell | `glock` |
| 49 | String Ensemble 2 | `strings` | 113 | Agogo | `agogo_hi` |
| 50 | Synth Strings 1 | `pad` | 114 | Steel Drums | `steeldrum` |
| 51 | Synth Strings 2 | `pad` | 115 | Woodblock | `woodblock` |
| 52 | Choir Aahs | `choir` | 116 | Taiko Drum | `kick_c` |
| 53 | Voice Oohs | `choir` | 117 | Melodic Tom | `tom_lo` |
| 54 | Synth Voice | `choir` | 118 | Synth Drum | `kick_g` |
| 55 | Orchestra Hit | `brass` | 119 | Reverse Cymbal | `revcym` |
| 56 | Trumpet | `trumpet` | 120 | Guitar Fret Noise | `fret` |
| 57 | Trombone | `trombone` | 121 | Breath Noise | `breath` |
| 58 | Tuba | `tuba` | 122 | Seashore | `ocean` |
| 59 | Muted Trumpet | `trumpet` | 123 | Bird Tweet | `bird` |
| 60 | French Horn | `horn` | 124 | Telephone Ring | `phone` |
| 61 | Brass Section | `brass` | 125 | Helicopter | `heli` |
| 62 | Synth Brass 1 | `synbrass` | 126 | Applause | `applause` |
| 63 | Synth Brass 2 | `synbrass` | 127 | Gunshot | `gunshot` |

## Channel 10 drum map

| key | instrument | played at | group |
|---|---|---|---|
| 27 | `claves` | 72 |  |
| 28 | `clap` | 64 |  |
| 31 | `claves` | 60 |  |
| 33 | `woodblock` | 66 |  |
| 34 | `cowbell` | 72 |  |
| 35 | `kick_m` | 60 |  |
| 36 | `kick_g` | 60 |  |
| 37 | `sidestick` | 60 |  |
| 38 | `snare` | 60 |  |
| 39 | `clap` | 60 |  |
| 40 | `snare2` | 60 |  |
| 41 | `tom_lo` | 53 |  |
| 42 | `hh_closed` | 60 | 1 |
| 43 | `tom_lo` | 57 |  |
| 44 | `hh_pedal` | 60 | 1 |
| 45 | `tom_lo` | 61 |  |
| 46 | `hh_open` | 60 | 1 |
| 47 | `tom_hi` | 57 |  |
| 48 | `tom_hi` | 60 |  |
| 49 | `crash` | 60 |  |
| 50 | `tom_hi` | 63 |  |
| 51 | `ride` | 60 |  |
| 52 | `crash2` | 55 |  |
| 53 | `ridebell` | 60 |  |
| 54 | `tamb` | 60 |  |
| 55 | `crash` | 67 |  |
| 56 | `cowbell` | 60 |  |
| 57 | `crash2` | 60 |  |
| 58 | `vibraslap` | 60 |  |
| 59 | `ride` | 63 |  |
| 60 | `bongo_hi` | 60 |  |
| 61 | `bongo_lo` | 60 |  |
| 62 | `conga_mute` | 60 |  |
| 63 | `conga_hi` | 60 |  |
| 64 | `conga_lo` | 60 |  |
| 65 | `timbale` | 64 |  |
| 66 | `timbale` | 58 |  |
| 67 | `agogo_hi` | 60 |  |
| 68 | `agogo_lo` | 60 |  |
| 69 | `cabasa` | 60 |  |
| 70 | `maracas` | 60 |  |
| 71 | `whistle_s` | 60 | 2 |
| 72 | `whistle_l` | 60 | 2 |
| 73 | `guiro_s` | 60 | 3 |
| 74 | `guiro_l` | 60 | 3 |
| 75 | `claves` | 60 |  |
| 76 | `woodblock` | 64 |  |
| 77 | `woodblock` | 58 |  |
| 78 | `cuica_m` | 60 | 4 |
| 79 | `cuica_o` | 60 | 4 |
| 80 | `tri_mute` | 60 | 5 |
| 81 | `tri_open` | 60 | 5 |
| 82 | `shaker` | 60 |  |
| 83 | `sleigh` | 60 |  |
| 84 | `marktree` | 60 |  |
| 85 | `claves` | 70 |  |
| 86 | `kick_c` | 55 |  |
| 87 | `kick_c` | 50 |  |

## Licence text: sgossner/VCSL at `c1ea7bcc3c73`

```
Creative Commons Legal Code

CC0 1.0 Universal

    CREATIVE COMMONS CORPORATION IS NOT A LAW FIRM AND DOES NOT PROVIDE
    LEGAL SERVICES. DISTRIBUTION OF THIS DOCUMENT DOES NOT CREATE AN
    ATTORNEY-CLIENT RELATIONSHIP. CREATIVE COMMONS PROVIDES THIS
    INFORMATION ON AN "AS-IS" BASIS. CREATIVE COMMONS MAKES NO WARRANTIES
    REGARDING THE USE OF THIS DOCUMENT OR THE INFORMATION OR WORKS
    PROVIDED HEREUNDER, AND DISCLAIMS LIABILITY FOR DAMAGES RESULTING FROM
    THE USE OF THIS DOCUMENT OR THE INFORMATION OR WORKS PROVIDED
    HEREUNDER.

Statement of Purpose

The laws of most jurisdictions throughout the world automatically confer
exclusive Copyright and Related Rights (defined below) upon the creator
and subsequent owner(s) (each and all, an "owner") of an original work of
authorship and/or a database (each, a "Work").

Certain owners wish to permanently relinquish those rights to a Work for
the purpose of contributing to a commons of creative, cultural and
scientific works ("Commons") that the public can reliably and without fear
of later claims of infringement build upon, modify, incorporate in other
works, reuse and redistribute as freely as possible in any form whatsoever
and for any purposes, including without limitation commercial purposes.
These owners may contribute to the Commons to promote the ideal of a free
culture and the further production of creative, cultural and scientific
works, or to gain reputation or greater distribution for their Work in
part through the use and efforts of others.

For these and/or other purposes and motivations, and without any
expectation of additional consideration or compensation, the person
associating CC0 with a Work (the "Affirmer"), to the extent that he or she
is an owner of Copyright and Related Rights in the Work, voluntarily
elects to apply CC0 to the Work and publicly distribute the Work under its
terms, with knowledge of his or her Copyright and Related Rights in the
Work and the meaning and intended legal effect of CC0 on those rights.

1. Copyright and Related Rights. A Work made available under CC0 may be
protected by copyright and related or neighboring rights ("Copyright and
Related Rights"). Copyright and Related Rights include, but are not
limited to, the following:

  i. the right to reproduce, adapt, distribute, perform, display,
     communicate, and translate a Work;
 ii. moral rights retained by the original author(s) and/or performer(s);
iii. publicity and privacy rights pertaining to a person's image or
     likeness depicted in a Work;
 iv. rights protecting against unfair competition in regards to a Work,
     subject to the limitations in paragraph 4(a), below;
  v. rights protecting the extraction, dissemination, use and reuse of data
     in a Work;
 vi. database rights (such as those arising under Directive 96/9/EC of the
     European Parliament and of the Council of 11 March 1996 on the legal
     protection of databases, and under any national implementation
     thereof, including any amended or successor version of such
     directive); and
vii. other similar, equivalent or corresponding rights throughout the
     world based on applicable law or treaty, and any national
     implementations thereof.

2. Waiver. To the greatest extent permitted by, but not in contravention
of, applicable law, Affirmer hereby overtly, fully, permanently,
irrevocably and unconditionally waives, abandons, and surrenders all of
Affirmer's Copyright and Related Rights and associated claims and causes
of action, whether now known or unknown (including existing as well as
future claims and causes of action), in the Work (i) in all territories
worldwide, (ii) for the maximum duration provided by applicable law or
treaty (including future time extensions), (iii) in any current or future
medium and for any number of copies, and (iv) for any purpose whatsoever,
including without limitation commercial, advertising or promotional
purposes (the "Waiver"). Affirmer makes the Waiver for the benefit of each
member of the public at large and to the detriment of Affirmer's heirs and
successors, fully intending that such Waiver shall not be subject to
revocation, rescission, cancellation, termination, or any other legal or
equitable action to disrupt the quiet enjoyment of the Work by the public
as contemplated by Affirmer's express Statement of Purpose.

3. Public License Fallback. Should any part of the Waiver for any reason
be judged legally invalid or ineffective under applicable law, then the
Waiver shall be preserved to the maximum extent permitted taking into
account Affirmer's express Statement of Purpose. In addition, to the
extent the Waiver is so judged Affirmer hereby grants to each affected
person a royalty-free, non transferable, non sublicensable, non exclusive,
irrevocable and unconditional license to exercise Affirmer's Copyright and
Related Rights in the Work (i) in all territories worldwide, (ii) for the
maximum duration provided by applicable law or treaty (including future
time extensions), (iii) in any current or future medium and for any number
of copies, and (iv) for any purpose whatsoever, including without
limitation commercial, advertising or promotional purposes (the
"License"). The License shall be deemed effective as of the date CC0 was
applied by Affirmer to the Work. Should any part of the License for any
reason be judged legally invalid or ineffective under applicable law, such
partial invalidity or ineffectiveness shall not invalidate the remainder
of the License, and in such case Affirmer hereby affirms that he or she
will not (i) exercise any of his or her remaining Copyright and Related
Rights in the Work or (ii) assert any associated claims and causes of
action with respect to the Work, in either case contrary to Affirmer's
express Statement of Purpose.

4. Limitations and Disclaimers.

 a. No trademark or patent rights held by Affirmer are waived, abandoned,
    surrendered, licensed or otherwise affected by this document.
 b. Affirmer offers the Work as-is and makes no representations or
    warranties of any kind concerning the Work, express, implied,
    statutory or otherwise, including without limitation warranties of
    title, merchantability, fitness for a particular purpose, non
    infringement, or the absence of latent or other defects, accuracy, or
    the present or absence of errors, whether or not discoverable, all to
    the greatest extent permissible under applicable law.
 c. Affirmer disclaims responsibility for clearing rights of other persons
    that may apply to the Work or any use thereof, including without
    limitation any person's Copyright and Related Rights in the Work.
    Further, Affirmer disclaims responsibility for obtaining any necessary
    consents, permissions or other rights required for any use of the
    Work.
 d. Affirmer understands and acknowledges that Creative Commons is not a
    party to this document and has no duty or obligation with respect to
    this CC0 or use of the Work.
```

## Licence text: sgossner/VSCO-2-CE at `440300901dfe`

```
CC0 1.0 Universal

Statement of Purpose

The laws of most jurisdictions throughout the world automatically confer
exclusive Copyright and Related Rights (defined below) upon the creator and
subsequent owner(s) (each and all, an "owner") of an original work of
authorship and/or a database (each, a "Work").

Certain owners wish to permanently relinquish those rights to a Work for the
purpose of contributing to a commons of creative, cultural and scientific
works ("Commons") that the public can reliably and without fear of later
claims of infringement build upon, modify, incorporate in other works, reuse
and redistribute as freely as possible in any form whatsoever and for any
purposes, including without limitation commercial purposes. These owners may
contribute to the Commons to promote the ideal of a free culture and the
further production of creative, cultural and scientific works, or to gain
reputation or greater distribution for their Work in part through the use and
efforts of others.

For these and/or other purposes and motivations, and without any expectation
of additional consideration or compensation, the person associating CC0 with a
Work (the "Affirmer"), to the extent that he or she is an owner of Copyright
and Related Rights in the Work, voluntarily elects to apply CC0 to the Work
and publicly distribute the Work under its terms, with knowledge of his or her
Copyright and Related Rights in the Work and the meaning and intended legal
effect of CC0 on those rights.

1. Copyright and Related Rights. A Work made available under CC0 may be
protected by copyright and related or neighboring rights ("Copyright and
Related Rights"). Copyright and Related Rights include, but are not limited
to, the following:

  i. the right to reproduce, adapt, distribute, perform, display, communicate,
  and translate a Work;

  ii. moral rights retained by the original author(s) and/or performer(s);

  iii. publicity and privacy rights pertaining to a person's image or likeness
  depicted in a Work;

  iv. rights protecting against unfair competition in regards to a Work,
  subject to the limitations in paragraph 4(a), below;

  v. rights protecting the extraction, dissemination, use and reuse of data in
  a Work;

  vi. database rights (such as those arising under Directive 96/9/EC of the
  European Parliament and of the Council of 11 March 1996 on the legal
  protection of databases, and under any national implementation thereof,
  including any amended or successor version of such directive); and

  vii. other similar, equivalent or corresponding rights throughout the world
  based on applicable law or treaty, and any national implementations thereof.

2. Waiver. To the greatest extent permitted by, but not in contravention of,
applicable law, Affirmer hereby overtly, fully, permanently, irrevocably and
unconditionally waives, abandons, and surrenders all of Affirmer's Copyright
and Related Rights and associated claims and causes of action, whether now
known or unknown (including existing as well as future claims and causes of
action), in the Work (i) in all territories worldwide, (ii) for the maximum
duration provided by applicable law or treaty (including future time
extensions), (iii) in any current or future medium and for any number of
copies, and (iv) for any purpose whatsoever, including without limitation
commercial, advertising or promotional purposes (the "Waiver"). Affirmer makes
the Waiver for the benefit of each member of the public at large and to the
detriment of Affirmer's heirs and successors, fully intending that such Waiver
shall not be subject to revocation, rescission, cancellation, termination, or
any other legal or equitable action to disrupt the quiet enjoyment of the Work
by the public as contemplated by Affirmer's express Statement of Purpose.

3. Public License Fallback. Should any part of the Waiver for any reason be
judged legally invalid or ineffective under applicable law, then the Waiver
shall be preserved to the maximum extent permitted taking into account
Affirmer's express Statement of Purpose. In addition, to the extent the Waiver
is so judged Affirmer hereby grants to each affected person a royalty-free,
non transferable, non sublicensable, non exclusive, irrevocable and
unconditional license to exercise Affirmer's Copyright and Related Rights in
the Work (i) in all territories worldwide, (ii) for the maximum duration
provided by applicable law or treaty (including future time extensions), (iii)
in any current or future medium and for any number of copies, and (iv) for any
purpose whatsoever, including without limitation commercial, advertising or
promotional purposes (the "License"). The License shall be deemed effective as
of the date CC0 was applied by Affirmer to the Work. Should any part of the
License for any reason be judged legally invalid or ineffective under
applicable law, such partial invalidity or ineffectiveness shall not
invalidate the remainder of the License, and in such case Affirmer hereby
affirms that he or she will not (i) exercise any of his or her remaining
Copyright and Related Rights in the Work or (ii) assert any associated claims
and causes of action with respect to the Work, in either case contrary to
Affirmer's express Statement of Purpose.

4. Limitations and Disclaimers.

  a. No trademark or patent rights held by Affirmer are waived, abandoned,
  surrendered, licensed or otherwise affected by this document.

  b. Affirmer offers the Work as-is and makes no representations or warranties
  of any kind concerning the Work, express, implied, statutory or otherwise,
  including without limitation warranties of title, merchantability, fitness
  for a particular purpose, non infringement, or the absence of latent or
  other defects, accuracy, or the present or absence of errors, whether or not
  discoverable, all to the greatest extent permissible under applicable law.

  c. Affirmer disclaims responsibility for clearing rights of other persons
  that may apply to the Work or any use thereof, including without limitation
  any person's Copyright and Related Rights in the Work. Further, Affirmer
  disclaims responsibility for obtaining any necessary consents, permissions
  or other rights required for any use of the Work.

  d. Affirmer understands and acknowledges that Creative Commons is not a
  party to this document and has no duty or obligation with respect to this
  CC0 or use of the Work.

For more information, please see
<http://creativecommons.org/publicdomain/zero/1.0/>
```
