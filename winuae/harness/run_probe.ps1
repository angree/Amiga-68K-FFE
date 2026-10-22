# Run one Amiga probe under WinUAE and print its log.
#
# Waits for the log to STOP GROWING rather than for a fixed time or a magic
# marker: probes differ in how long they run and what they print last, and a
# fixed wait either truncates the log or wastes a minute per run.
#
# Stops ONLY the emulator this script started, verified by PID *and* executable
# path -- the user runs emulators for other projects at the same time. Never
# raises anything on the primary monitor: that screen is being recorded.
param(
  [Parameter(Mandatory=$true)][string]$Probe,     # e.g. ffe_dirtest
  [string]$Config    = "ffe-020-aga.uae",
  [int]   $MaxSec    = 240,
  [int]   $QuietSec  = 8      # log unchanged this long => the probe is done
)
$ROOT = 'I:\GITHUB\Amiga_Frontier_2_First_Encouters'
$exe  = "$ROOT\tools\winuae281\winuae.exe"
$wd   = "$ROOT\tools\winuae281"
$cfg  = "$ROOT\winuae\$Config"
$work = "$ROOT\winuae\work"
$log  = "$work\$Probe.log"

function Stop-OurWinUAE {
  param([int]$ProcId)
  $q = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcId" -ErrorAction SilentlyContinue
  if (-not $q) { return }
  if (-not $q.ExecutablePath)     { Write-Host "REFUSING to stop pid ${ProcId}: cannot read its path"; return }
  if ($q.ExecutablePath -ne $exe) { Write-Host "REFUSING to stop pid ${ProcId}: $($q.ExecutablePath) is not ours"; return }
  Stop-Process -Id $ProcId -Force -ErrorAction SilentlyContinue
}

if (-not (Test-Path "$work\$Probe")) { Write-Host "not deployed: $work\$Probe"; exit 1 }
# LF-only, no trailing CRLF: AmigaDOS treats a CR as part of the command name
Set-Content -Path "$work\run" -Value "Stack 1000000`nCD Work:`nRun >NIL: <NIL: Work:$Probe`n" -Encoding Ascii -NoNewline
Remove-Item $log -ErrorAction SilentlyContinue

$others = @(Get-Process winuae -ErrorAction SilentlyContinue)
if ($others.Count) { Write-Host "note: $($others.Count) other WinUAE running - leaving them alone" }

$p = Start-Process -FilePath $exe -ArgumentList '-f', $cfg, '-nodirectinput', '-norawinput' `
                   -WorkingDirectory $wd -PassThru
Write-Host "[$Probe on $Config] our pid=$($p.Id)"

$lastLen = -1; $quiet = 0; $i = 0
for (; $i -lt $MaxSec; $i++) {
  Start-Sleep -Seconds 1
  if ($p.HasExited) { Write-Host "  emulator exited early"; break }
  $len = if (Test-Path $log) { (Get-Item $log).Length } else { -1 }
  if ($len -ge 0 -and $len -eq $lastLen) {
    $quiet++
    if ($quiet -ge $QuietSec) { break }
  } else { $quiet = 0 }
  $lastLen = $len
}
Stop-OurWinUAE -ProcId $p.Id
Write-Host "  ${i}s elapsed; our pid stopped, others untouched`n"

# RESTORE the game boot script: leaving Work:run pointed at the probe cost a
# whole evening of "boots" that were silently running ffe_xlattest instead of
# the game. autokey_run/boot_probe self-heal too; this closes the loop at the
# source. LF-only (a CR makes the shell load "ffe_game<CR>" -- silent no-op).
$gameScript = "Stack 1000000`nCD Work:`nEcho >Work:boot_marker NOLINE `"booted`"`nWork:ffe_game`n"
Set-Content -Path "$work\run" -Value $gameScript -Encoding ascii -NoNewline

if (Test-Path $log) { Get-Content $log } else { Write-Host "NO LOG PRODUCED" }
