# Equivalence run (2026-10-24, performance work): the intro with a FIXED clock
# step (PROGDIR:ffe_fixedstep: 20 ms of game time per frame, whatever the
# speed), every 8th frame written to PROGDIR:seq/ (ffe_seq). Two builds that
# compute the same thing must then write byte-identical frames, however fast
# each one is. Compare with tools\perf\eq_compare.py.
#   eq_run.ps1 -Bin E:\ffe\bins\ffe_game.A -Tag A
param([string]$Bin, [string]$Tag, [int]$Seconds = 330, [string]$Config = "nj-eq-aga.uae")
$W = "E:\ffe\eqtest"
$O = "E:\ffe\eq\$Tag"
Copy-Item $Bin "$W\AmiFFE\ffe_game" -Force
if (Test-Path "$W\AmiFFE\seq") { Remove-Item "$W\AmiFFE\seq" -Recurse -Force }
New-Item -ItemType Directory -Force "$W\AmiFFE\seq" | Out-Null
& powershell -NoProfile -File E:\GITHUB\amiffe\winuae\harness\test_matrix.ps1 -Config $Config -WorkDir $W -Game 'Work:AmiFFE/ffe_game' -Shots "$Seconds" -Tag "eq-$Tag" -OutDir "E:\ffe\eq"
if (Test-Path $O) { Remove-Item $O -Recurse -Force }
Move-Item "$W\AmiFFE\seq" $O
Write-Host "$Tag : $((Get-ChildItem $O -Filter f*.raw).Count) frames -> $O"
