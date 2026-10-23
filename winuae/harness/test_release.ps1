# Release test (2026-10-23): the unpacked release archive in E:\ffe\reltest,
# JIT OFF (nj-*.uae: cachesize=0, cpu_compatible=true, 68020, no FPU).
#  - Workbench 3.1: the user's path, driven from INSIDE the guest by
#    Work:T/wbclick (input.device): open Work, open AmiFFE, double-click
#    ffe-prefs, close it, double-click ffe_game.
#  - AROS nightly AGA/RTG and the old AROS ROM: ffe-prefs from the Shell,
#    icontest (icon.library reads all four icons), the game with a 4 KB stack.
# One machine at a time; test_matrix.ps1 takes C:\temp\winuae.lock.
$h = "E:\GITHUB\amiffe\winuae\harness\test_matrix.ps1"
$W = "E:\ffe\reltest"
$O = "E:\ffe\wtest-shots\r012-rel"
New-Item -ItemType Directory -Force $O | Out-Null
$runs = @(
  @{ c = 'nj-ks31-rtg.uae';    g = 'Run >NIL: Execute Work:T/wbsteps'; t = 'ks31-wb';     s = '75,105,170' },
  @{ c = 'nj-aros-rtg.uae';    g = 'Execute Work:T/aros-RTG';          t = 'aros-rtg';    s = '90,170' },
  @{ c = 'nj-aros-aga.uae';    g = 'Execute Work:T/aros-AGA';          t = 'aros-aga';    s = '90,170' },
  @{ c = 'nj-arosold-aga.uae'; g = 'Execute Work:T/aros-AGA';          t = 'arosold-aga'; s = '90,170' }
)
foreach ($x in $runs) {
  Write-Host "===== $($x.t) ====="
  foreach ($f in 'wb.txt','prefs.txt','icons.txt') { Remove-Item "$W\T\$f" -ErrorAction SilentlyContinue }
  Remove-Item "$W\AmiFFE\amiffe.cfg" -ErrorAction SilentlyContinue
  & powershell -NoProfile -File $h -Config $x.c -WorkDir $W -Game $x.g -Shots $x.s -Tag ("r012-rel-" + $x.t) -OutDir $O
  foreach ($f in 'wb.txt','prefs.txt','icons.txt') {
    if (Test-Path "$W\T\$f") { Write-Host "--- T/$f"; Get-Content "$W\T\$f"; Copy-Item "$W\T\$f" "$O\$($x.t)-$f" }
  }
  if (Test-Path "$W\AmiFFE\amiffe.cfg") { Write-Host "--- amiffe.cfg"; Get-Content "$W\AmiFFE\amiffe.cfg" }
  if (Test-Path "$W\AmiFFE\ffe_crash.log") { Write-Host "--- CRASH LOG"; Get-Content "$W\AmiFFE\ffe_crash.log" }
}
