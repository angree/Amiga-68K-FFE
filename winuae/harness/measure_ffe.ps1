# Run the Amiga probe under WinUAE, wait for its log to be complete, print it.
# No screenshot -- this is for the numbers, not the picture.
#
# Stops ONLY the emulator this script started, verified BOTH by PID and by
# executable path. The user runs emulators for several projects simultaneously;
# a stray kill destroys someone else's session. See launch_ffe.ps1 for the same
# guard and the reason it exists.
param(
  [string]$Config = "ffe-020-aga.uae",
  [string]$Log    = "ffe_agatest.log",
  [int]   $MaxSec = 300
)
$ROOT = 'I:\GITHUB\Amiga_Frontier_2_First_Encouters'
$exe  = "$ROOT\tools\winuae281\winuae.exe"
$wd   = "$ROOT\tools\winuae281"
$cfg  = "$ROOT\winuae\$Config"
$log  = "$ROOT\winuae\work\$Log"

function Stop-OurWinUAE {
  param([int]$ProcId)
  $q = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcId" -ErrorAction SilentlyContinue
  if (-not $q) { return }
  if (-not $q.ExecutablePath)    { Write-Host "REFUSING to stop pid ${ProcId}: cannot read its path"; return }
  if ($q.ExecutablePath -ne $exe){ Write-Host "REFUSING to stop pid ${ProcId}: $($q.ExecutablePath) is not ours"; return }
  Stop-Process -Id $ProcId -Force -ErrorAction SilentlyContinue
}

if (-not (Test-Path $cfg)) { Write-Host "no such config: $cfg"; exit 1 }
Remove-Item $log -ErrorAction SilentlyContinue

$others = @(Get-Process winuae -ErrorAction SilentlyContinue)
if ($others.Count) { Write-Host "note: $($others.Count) other WinUAE instance(s) running - leaving them alone" }

$p = Start-Process -FilePath $exe -ArgumentList '-f', $cfg, '-nodirectinput', '-norawinput' `
                   -WorkingDirectory $wd -PassThru
Write-Host "[$Config] our pid=$($p.Id)"

$i = 0
for (; $i -lt $MaxSec; $i++) {
  Start-Sleep -Seconds 1
  if (Test-Path $log) {
    $t = Get-Content $log -Raw -ErrorAction SilentlyContinue
    if ($t -and $t -match 'holding \d+ s') { break }   # probe reached its hold: log is complete
  }
  if ($p.HasExited) { Write-Host "  emulator exited early"; break }
}
Stop-OurWinUAE -ProcId $p.Id
Write-Host "  done after ${i}s; our pid stopped, others untouched`n"
if (Test-Path $log) { Get-Content $log } else { Write-Host "  NO LOG PRODUCED" }
