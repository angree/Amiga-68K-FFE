# Profiling run (2026-10-24): the intro on KS 3.1 AGA, 68020 no FPU, JIT OFF
# (nj-prof-aga.uae), Work: = E:\ffe\proftest. PROGDIR:ffe_prof holds the
# window in emulated seconds; the game writes PROGDIR:ffe_prof.log at its end.
# The log and the build's symbol map are kept as E:\ffe\prof\<Tag>.*; read
# them with tools\perf\prof_report.py.
#   prof_run.ps1 -Tag base1 [-Game ffe_game] [-Config nj-prof-aga.uae]
param([string]$Tag = "p", [string]$Config = "nj-prof-aga.uae", [string]$Shots = "120,245",
      [string]$Symmap = "E:\ffe\mirror\build\amiga-out\ffe_game.symmap")
$W = "E:\ffe\proftest"
$O = "E:\ffe\prof"
New-Item -ItemType Directory -Force $O | Out-Null
Remove-Item "$W\AmiFFE\ffe_prof.log" -ErrorAction SilentlyContinue
Copy-Item $Symmap "$O\$Tag.symmap" -Force
(Get-FileHash "$W\AmiFFE\ffe_game" -Algorithm MD5).Hash | Set-Content "$O\$Tag.md5"
& powershell -NoProfile -File E:\GITHUB\amiffe\winuae\harness\test_matrix.ps1 -Config $Config -WorkDir $W -Game 'Work:AmiFFE/ffe_game' -Shots $Shots -Tag "prof-$Tag" -OutDir $O
if (Test-Path "$W\AmiFFE\ffe_prof.log") { Copy-Item "$W\AmiFFE\ffe_prof.log" "$O\$Tag.log" -Force; Write-Host "profile -> $O\$Tag.log" }
else { Write-Host "NO ffe_prof.log - the run did not reach the end of the window" }
