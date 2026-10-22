# Six start locations on both machines (2026-10-16).
# The intro's GUI actions 0x31..0x35 ('1'..'5') pick scenarios 1,2,3,6,5 through
# DATA_007191 (ffe.asm:68448-68456); no key picks scenario 0 (Hope).
# Amiga: the key is pressed in the intro. PC: FFE_SCENARIO forces the same record
# in the FUNC_000829 shim (build\pcprobe_proj.c), equivalent to the key.
#   powershell -File build\run-scenarios.ps1 [-Which "1,2,3,4,5"] [-Side both|pc|amiga]
param([string]$Which = "1,2,3,4,5", [string]$Side = "both", [int]$WatchSec = 60)
$scen = @{ 1 = 1; 2 = 2; 3 = 3; 4 = 6; 5 = 5 }
$out = 'C:\temp\amiffe\build\SCEN'
New-Item -ItemType Directory -Force $out | Out-Null
foreach ($k in ($Which -split ',' | ForEach-Object { [int]$_ })) {
  $s = $scen[$k]
  if ($Side -ne 'amiga') {
    # the SAME key in the intro as on the Amiga (FFE_SCENARIO forcing gave a
    # broken spawn -- empty space at Alt 0 m -- 2026-10-16)
    Set-Location C:\temp\amiffe
    $gk = "0x{0:X2}:0x{1:X2}" -f (0x30 + $k), (1 + $k)
    powershell -File build\run-pc-seq.ps1 -ExeName ffesdl-probe.exe -Every 64 -WatchSec $WatchSec -Game -GameKey $gk | Select-Object -Last 1
    $d = "$out\pc-key$k-$(Get-Date -Format HHmmss)"; New-Item -ItemType Directory -Force $d | Out-Null
    Get-ChildItem C:\temp\amiffe\build\m0-pc\run\seq -File | Copy-Item -Destination $d
  }
  if ($Side -ne 'pc') {
    Get-ChildItem C:\temp\ffe\work\seq -File | Remove-Item -ErrorAction SilentlyContinue
    & C:\temp\amiffe\winuae\harness\soft_run.ps1 -Keys "21,$k" -GapSec 10 -WatchSec $WatchSec | Select-Object -Last 1
    $d = "$out\am-key$k-$(Get-Date -Format HHmmss)"; New-Item -ItemType Directory -Force $d | Out-Null
    Get-ChildItem C:\temp\ffe\work\seq -File | Copy-Item -Destination $d
  }
  "key $k (scenario $s) done"
}
