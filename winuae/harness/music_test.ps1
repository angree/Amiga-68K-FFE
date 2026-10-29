# Music test (2026-10-24): E:\ffe\mustest = the release layout + Music/AdLib
# rendered on the PC + ffe-music. JIT off.
#  1. KS 3.1 AGA: the game with music=1 (AdLib pre-rendered): do songs start,
#     how many underruns (ffe_music.c logs them).
#  2. KS 3.1: ffe-music on the Amiga itself, one 59 s song at 11 kHz Q=0,
#     timed with Date before and after.
#  3. AROS nightly AGA: the game with music=1 (audio.device on AROS?).
$h = "E:\GITHUB\amiffe\winuae\harness\test_matrix.ps1"
$W = "E:\ffe\mustest"
$O = "E:\ffe\wtest-shots\music"
New-Item -ItemType Directory -Force "$W\T", $O | Out-Null
@('Date >Work:T/fm.txt', 'Work:AmiFFE/ffe-music ADLIB SONG=babayaga RATE=11025 Q=0 OUT=RAM: >>Work:T/fm.txt',
  'Date >>Work:T/fm.txt', 'List RAM:AdLib >>Work:T/fm.txt') -join "`n" | Set-Content -NoNewline -Encoding ascii "$W\T\fm"
$runs = @(
  @{ c = 'nj-mus-prof.uae'; g = 'Work:AmiFFE/ffe_game'; t = 'ks31-game'; s = '150' },
  @{ c = 'nj-mus-prof.uae'; g = 'Execute Work:T/fm';    t = 'ks31-render'; s = '420' },
  @{ c = 'nj-mus-aros.uae'; g = 'Work:AmiFFE/ffe_game'; t = 'aros-game'; s = '150' }
)
foreach ($x in $runs) {
  Write-Host "===== $($x.t) ====="
  Remove-Item "$W\T\fm.txt" -ErrorAction SilentlyContinue
  & powershell -NoProfile -File $h -Config $x.c -WorkDir $W -Game $x.g -Shots $x.s -Tag ("mus-" + $x.t) -OutDir $O | Select-String -NotMatch '^shot|^started|^stopped'
  if (Test-Path "$W\T\fm.txt") { Write-Host "--- T/fm.txt"; Get-Content "$W\T\fm.txt" }
  if (Test-Path "$W\stdout.txt") { Copy-Item "$W\stdout.txt" "$O\$($x.t)-stdout.txt" -Force }
}
