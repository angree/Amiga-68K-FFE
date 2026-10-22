# Keep the display (and system) awake while OUR WinUAE runs.
# Developer, 2026-10-15: a blanked monitor stalled the emulators for hours and the
# session stopped. SetThreadExecutionState(ES_CONTINUOUS|ES_SYSTEM_REQUIRED|
# ES_DISPLAY_REQUIRED) prevents the blanking for as long as this thread lives;
# the flag dies with the process, so nothing is changed permanently.
#   powershell -WindowStyle Hidden -File keep_display.ps1 -WatchPid <winuae pid>
param([int]$WatchPid = 0)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class KD { [DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint f); }
"@
while ($true) {
  [void][KD]::SetThreadExecutionState(0x80000003)
  if ($WatchPid -and -not (Get-Process -Id $WatchPid -ErrorAction SilentlyContinue)) { break }
  Start-Sleep -Seconds 30
}
[void][KD]::SetThreadExecutionState(0x80000000)
