# Repeat the intro run until the hang watchdog (amiga\ffeapi\ffe_watchdog.c)
# writes Work:ffe_watchdog.log, then keep a copy. Uses the all-local config.
#   powershell -File winuae\harness\hunt_hang.ps1 -Tries 5 -WatchSec 80
# NOTE: PowerShell variable names are case-insensitive -- $work and $WORK are
# the same variable, which is how an earlier inline version of this loop
# overwrote its own work path.
param([int]$Tries = 5, [int]$WatchSec = 80)
$root = 'I:\GITHUB\Amiga_Frontier_2_First_Encouters'
$workDir = 'C:\temp\ffe\work'
for ($i = 1; $i -le $Tries; $i++) {
  Remove-Item "$workDir\ffe_watchdog.log" -ErrorAction SilentlyContinue
  & powershell -NoProfile -File "$root\winuae\harness\autokey_run.ps1" -Config ffe-020-aga-alllocal.uae `
      -WorkDir $workDir -Keys "21" -WatchSec $WatchSec -WindowSec 400 2>&1 | Out-Null
  $last = Select-String -Path "$workDir\ffe_dump.log" -Pattern "^presents=" -ErrorAction SilentlyContinue | Select-Object -Last 1
  $pres = if ($last) { $last.Line.Split(' ')[0] } else { "presents=?" }
  $caught = Test-Path "$workDir\ffe_watchdog.log"
  Write-Host "try $i $pres watchdog=$caught"
  if ($caught) {
    Copy-Item "$workDir\ffe_watchdog.log" "C:\temp\ffe\watchdog_try$i.log" -Force
    Copy-Item "$workDir\ffe_trace.log" "C:\temp\ffe\watchdog_trace_try$i.log" -Force -ErrorAction SilentlyContinue
    break
  }
}
