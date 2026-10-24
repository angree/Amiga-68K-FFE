# A series of deterministic profiles (nj-det-aga.uae: 68020 at a fixed 50 MHz,
# JIT off) of the builds in E:\ffe\bins, one after the other.
#   det_series.ps1 -Runs "B:detB2","A:detA1","C:detC1"
param([string]$Runs)   # "B:detB2,A:detA1:nodirty,C:detC1" (one string: -File passes no arrays)
foreach ($r in $Runs.Split(',')) {
  $b, $t, $opt = $r.Split(':')
  # option "nodirty": PROGDIR:ffe_nodirty for this run (the 0.1.2 full-frame c2p)
  Remove-Item E:\ffe\proftest\AmiFFE\ffe_nodirty -ErrorAction SilentlyContinue
  if ($opt -eq 'nodirty') { Set-Content -NoNewline -Encoding ascii E:\ffe\proftest\AmiFFE\ffe_nodirty "1`n" }
  Copy-Item "E:\ffe\bins\ffe_game.$b" E:\ffe\proftest\AmiFFE\ffe_game -Force
  $sym = "E:\ffe\bins\ffe_game.$b.symmap"
  if (-not (Test-Path $sym)) { $sym = "E:\ffe\bins\ffe_game.B.symmap" }
  & powershell -NoProfile -File E:\GITHUB\amiffe\winuae\harness\prof_run.ps1 -Tag $t -Config nj-det-aga.uae -Symmap $sym -Shots "245" | Select-Object -Last 1
  $l = Get-Content "E:\ffe\prof\$t.log" -TotalCount 3
  Write-Host "$b $t : $($l[1]) | $($l[2])"
}
