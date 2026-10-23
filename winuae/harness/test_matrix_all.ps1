# One AmiFFE binary on every 68k OS (2026-10-22): Workbench 3.1 AGA/RTG, AROS
# nightly AGA/RTG, WinUAE's built-in old AROS ROM (AGA). One machine at a time.
$h = "E:\GITHUB\amiffe\winuae\harness\test_matrix.ps1"
$runs = @(
  @{ c = 'm-ks31-aga.uae';    r = 'Work:ffe-prefs GFX=AGA VIDEO=AUTO'; t = 'ks31-aga' },
  @{ c = 'm-ks31-rtg.uae';    r = 'Work:ffe-prefs GFX=RTG VIDEO=AUTO'; t = 'ks31-rtg' },
  @{ c = 'm-aros-aga.uae';    r = 'Work:ffe-prefs GFX=AGA VIDEO=AUTO'; t = 'aros-aga' },
  @{ c = 'm-aros-rtg.uae';    r = 'Work:ffe-prefs GFX=RTG VIDEO=AUTO'; t = 'aros-rtg' },
  @{ c = 'm-arosold-aga.uae'; r = 'Work:ffe-prefs GFX=AGA VIDEO=AUTO'; t = 'arosold-aga' }
)
foreach ($x in $runs) {
  Write-Host "===== $($x.t) ====="
  & powershell -NoProfile -File $h -Config $x.c -Run $x.r -Shots "60,140" -Tag ("r011-" + $x.t)
}
