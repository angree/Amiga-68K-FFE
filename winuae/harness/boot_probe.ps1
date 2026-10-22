# Minimal environment probe: boots the FFE config, waits for Work:boot_marker
# (written by the harness-owned run script), reports OK/DEAD, and stops ONLY
# the instance it started (PID + executable path, the Stop-OurWinUAE rule).
param([int]$WaitSec = 120)
$ROOT = 'I:\GITHUB\Amiga_Frontier_2_First_Encouters'
$exe  = "$ROOT\tools\winuae281\winuae.exe"
$WORK = "$ROOT\winuae\work"

# the run script may have been left pointing elsewhere -- self-heal (LF-only!)
$runScript = "Stack 1000000`nCD Work:`nEcho >Work:boot_marker NOLINE `"booted`"`nWork:ffe_game`n"
Set-Content -Path "$WORK\run" -Value $runScript -Encoding ascii -NoNewline
Remove-Item "$WORK\boot_marker","$WORK\ffe_game.log" -ErrorAction SilentlyContinue

$p = Start-Process -FilePath $exe -ArgumentList '-f',"$ROOT\winuae\ffe-020-aga.uae",'-nodirectinput','-norawinput' `
                   -WorkingDirectory "$ROOT\tools\winuae281" -PassThru
$deadline = (Get-Date).AddSeconds($WaitSec)
$ok = $false
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Seconds 5
  if (Test-Path "$WORK\boot_marker") { $ok = $true; break }
}
$q = Get-CimInstance Win32_Process -Filter "ProcessId=$($p.Id)" -ErrorAction SilentlyContinue
if ($q -and $q.ExecutablePath -eq $exe) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
if ($ok) { Write-Output "ENV-OK" } else { Write-Output "ENV-DEAD" }
