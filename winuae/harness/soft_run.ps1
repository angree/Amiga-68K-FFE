# SOFT RUN (2026-10-02): run the game WITHOUT restarting WinUAE.
#
# The developer's rule: "only reset the machine, do not restart WinUAE -- it
# keeps taking my mouse". So WinUAE is launched only when OUR instance is not
# running at all; every later run is a ColdReboot() of the emulated Amiga,
# requested through two flag files in the Work: directory that the game's
# watchdog process polls (amiga\ffeapi\ffe_watchdog.c):
#
#   Work:ffe_reboot  -> deleted by the Amiga, then ColdReboot()
#   Work:ffe_hold    -> after the boot, main() waits while this exists and
#                       writes nothing, so the host can read and delete logs
#                       safely; deleting it starts the game
#
# One run:  hold+reboot -> (old logs readable/deletable) -> clear logs ->
#           release hold -> keys -> watch -> hold+reboot -> logs readable.
# With -Leave the game is left running at the end (for watching) and the logs
# must not be read until the next soft_run puts the machine in hold.
#
# Safety rules kept: the window is parked on a NON-primary monitor with
# SWP_NOACTIVATE, keys go through PROGDIR:ffe_autokey files (nothing touches
# the host input queue), and nothing is ever killed.
param(
  [string]$Config  = "ffe-020-aga-fastboot.uae",
  [string]$WorkDir = "C:\temp\ffe\work",
  [string]$Keys    = "21",
  [int]   $WatchSec = 120,
  [int]   $GapSec = 3,         # pause after each key (the 2nd 'y' must land in the intro, not the splash)
  # -Mouse "x y 1;x y 1": clicks in GAME screen coordinates (320x200), dropped
  # one at a time as PROGDIR:ffe_automouse after the keys (ffe_input.c chases
  # the game's own pointer and clicks; nothing touches the host mouse).
  [string]$Mouse = "",
  [int]   $MouseGap = 6,
  # -KeysAfter "86,..": raw keys dropped AFTER the mouse clicks (same gap as -GapSec)
  [string]$KeysAfter = "",
  # -Script "k83;m128 118 1;k83;m128 52 1": keys (kRAW) and clicks (m x y b) in ANY order,
  # one step every $GapSec seconds, run after -Keys/-Mouse/-KeysAfter
  [string]$Script = "",
  [switch]$Leave,
  [int]   $WindowSec = 400
)
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class SR {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  [DllImport("user32.dll")] static extern bool GetWindowRect(IntPtr h, out RECT r);
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  static EnumProc keep;   // rooted: an unrooted delegate can be collected mid-scan
  public static IntPtr FindEmuWindow(uint want) {
    IntPtr found = IntPtr.Zero;
    keep = delegate(IntPtr h, IntPtr p) {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid != want || !IsWindowVisible(h)) return true;
      RECT r; if (!GetWindowRect(h, out r)) return true;
      if ((r.R - r.L) < 160 || (r.B - r.T) < 100) return true;
      found = h; return false;
    };
    EnumWindows(keep, IntPtr.Zero);
    return found;
  }
}
"@
$ROOT = 'I:\GITHUB\Amiga_Frontier_2_First_Encouters'
$exe  = "$ROOT\tools\winuae281\winuae.exe"
$WORK = $WorkDir   # never name a variable $workdir/$work twice: PowerShell is case-insensitive

$sw = [Diagnostics.Stopwatch]::StartNew()
function Stamp([string]$what) { Write-Host ("[{0,6:N1}s] {1}" -f $sw.Elapsed.TotalSeconds, $what) }

function Wait-Gone([string]$path, [int]$sec) {
  for ($i = 0; $i -lt $sec * 2; $i++) {
    if (-not (Test-Path $path)) { return $true }
    Start-Sleep -Milliseconds 500
  }
  return $false
}

function Wait-Holding([int]$sec) {
  for ($i = 0; $i -lt $sec * 2; $i++) {
    if (Test-Path "$WORK\ffe_holding") { return $true }
    Start-Sleep -Milliseconds 500
  }
  return $false
}

function Clear-Logs {
  Remove-Item "$WORK\ffe_input.log","$WORK\ffe_autokey","$WORK\ffe_game.log",
              "$WORK\ffe_enter.log","$WORK\ffe_dispatch.log","$WORK\ffe_trace.log",
              "$WORK\ffe_frame.raw","$WORK\ffe_crash.log","$WORK\ffe_dump.log",
              "$WORK\ffe_samples.log","$WORK\ffe_watchdog.log","$WORK\ffe_frame1808.log",
              "$WORK\ffe_frame1478.log","$WORK\ffe_items.log","$WORK\ffe_framehalo.log",
              "$WORK\ffe_clip1837.log","$WORK\boot_marker","$WORK\ffe_holding" -ErrorAction SilentlyContinue
  if (Test-Path "$WORK\seq") { Remove-Item "$WORK\seq\*" -ErrorAction SilentlyContinue }
}

# the boot script, LF-only (a CR becomes part of the command name on AmigaDOS)
$runScript = "Stack 1000000`nCD Work:`nEcho >Work:boot_marker NOLINE `"booted`"`nWork:ffe_game`n"
Set-Content -Path "$WORK\run" -Value $runScript -Encoding ascii -NoNewline

$ours = @(Get-CimInstance Win32_Process -Filter "Name='winuae.exe'" -ErrorAction SilentlyContinue |
          Where-Object { $_.ExecutablePath -eq $exe })
if ($ours.Count -gt 0) {
  Write-Host "our WinUAE pid=$($ours[0].ProcessId) is running -> soft reset (no restart)"
  Set-Content -Path "$WORK\ffe_hold" -Value "1" -Encoding ascii
  # a stale marker from the previous end-of-run hold would release the hold
  # before this reboot has even happened
  Remove-Item "$WORK\ffe_holding","$WORK\boot_marker" -ErrorAction SilentlyContinue
  Set-Content -Path "$WORK\ffe_reboot" -Value "1" -Encoding ascii
  if (Wait-Gone "$WORK\ffe_reboot" 30) {
    # A request to a RUNNING game (left by -Leave) only PARKS it; the reboot is
    # the second request. S/Startup-Sequence writes boot_marker on every boot,
    # so "holding without a boot_marker" = parked, not rebooted.
    if ((Wait-Holding 20) -and -not (Test-Path "$WORK\boot_marker")) {
      Write-Host "the game was running: parked first, now the reset"
      Remove-Item "$WORK\ffe_holding" -ErrorAction SilentlyContinue
      Set-Content -Path "$WORK\ffe_reboot" -Value "1" -Encoding ascii
    }
  }
  if (-not (Wait-Gone "$WORK\ffe_reboot" 30)) {
    Write-Host "RESET FLAG NOT CONSUMED in 30 s: the running binary has no soft-reset watchdog,"
    Write-Host "or the emulated machine is dead. Not restarting WinUAE on my own -- say so to the developer."
    Remove-Item "$WORK\ffe_reboot" -ErrorAction SilentlyContinue
    exit 2
  }
  Stamp "reset requested; machine rebooting into HOLD"
  if (-not (Wait-Holding 90)) { Write-Host "the new boot never reached HOLD (no ffe_holding in 90 s)"; exit 1 }
  Clear-Logs
} else {
  Write-Host "our WinUAE is not running -> launching it ONCE"
  Clear-Logs
  Set-Content -Path "$WORK\ffe_hold" -Value "1" -Encoding ascii
  $p = Start-Process -FilePath $exe -ArgumentList '-f', "$ROOT\winuae\$Config", '-nodirectinput', '-norawinput' `
                     -WorkingDirectory "$ROOT\tools\winuae281" -PassThru
  $h = [IntPtr]::Zero
  $deadline = (Get-Date).AddSeconds($WindowSec)
  while ((Get-Date) -lt $deadline -and $h -eq [IntPtr]::Zero) {
    Start-Sleep -Milliseconds 500
    if ($p.HasExited) { Write-Host "WinUAE exited before showing a window"; exit 1 }
    $h = [SR]::FindEmuWindow([uint32]$p.Id)
  }
  if ($h -eq [IntPtr]::Zero) { Write-Host "no window after $WindowSec s (process left alone)"; exit 1 }
  $target = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -Last 1
  # 0x0011 = SWP_NOSIZE | SWP_NOACTIVATE: never the primary monitor, never the focus
  if ($target) { [SR]::SetWindowPos($h, [IntPtr](-2), $target.Bounds.X + 30, $target.Bounds.Y + 30, 0, 0, 0x0011) | Out-Null }
  Write-Host "launched pid=$($p.Id), parked on $($target.DeviceName)"
# keep the display awake while this emulator lives (a blanked monitor stalled
# WinUAE for hours on 2026-10-15); the flag dies with the helper process
Start-Process powershell -ArgumentList '-NoProfile','-WindowStyle','Hidden','-File',
  (Join-Path $PSScriptRoot 'keep_display.ps1'),'-WatchPid',"$($p.Id)" -WindowStyle Hidden | Out-Null
}

# fresh launch: wait for the boot to reach HOLD (the game writes ffe_holding),
# then release it. (After a soft reset that wait already happened above.)
if ($ours.Count -eq 0) {
  if (-not (Wait-Holding 180)) { Write-Host "the boot never reached HOLD (no ffe_holding in 180 s)"; exit 1 }
}
Remove-Item "$WORK\ffe_holding" -ErrorAction SilentlyContinue
# release by CREATING ffe_go: WinUAE's directory filesystem can miss a host-
# side DELETE (the machine once sat in the hold forever), never a new file.
# The Amiga deletes ffe_go and ffe_hold itself.
Set-Content -Path "$WORK\ffe_go" -Value "1" -Encoding ascii
if (-not (Wait-Gone "$WORK\ffe_go" 30)) { Write-Host "the game never picked up ffe_go"; exit 1 }
Stamp "booted to HOLD; hold released, game starting"

$up = $false
for ($i = 0; $i -lt 60; $i++) {
  if (Test-Path "$WORK\ffe_dump.log" -ErrorAction SilentlyContinue) { $up = $true; break }
  Start-Sleep -Seconds 2
}
if (-not $up) { Write-Host "game never started (no ffe_dump.log)"; exit 1 }
Stamp "GAME UP (first log line)"
Start-Sleep -Seconds 2

$actionOf = @{ 21 = "-> 79"; 1 = "-> 31" }
$KeyList = $Keys.Split(",") | Where-Object { $_.Trim() -ne "" } | ForEach-Object { [int]$_.Trim() }
$ki = 0
foreach ($k in $KeyList) {
  $ki++
  $marker = $actionOf[$k]
  $done = $false
  # Drop once, wait until the game CONSUMES the file (ffe_input.c removes it),
  # then give the ACTION line up to 15 s. Re-drop only if it was consumed and
  # never registered (pressed before the screen listened). A blind re-drop
  # after a fixed 6 s once delivered a SECOND 'y' that skipped the intro
  # into the game (2026-10-02).
  for ($try = 0; $try -lt 5 -and -not $done; $try++) {
    Set-Content -Path "$WORK\ffe_autokey" -Value "$k" -Encoding ascii
    if (-not (Wait-Gone "$WORK\ffe_autokey" 60)) { Write-Host "key $k not consumed in 60 s"; break }
    if ($null -eq $marker) { $done = $true; break }
    for ($w = 0; $w -lt 15 -and -not $done; $w++) {
      Start-Sleep -Seconds 1
      if (Select-String -Path "$WORK\ffe_dump.log" -Pattern ([regex]::Escape($marker)) -Quiet -ErrorAction SilentlyContinue) { $done = $true }
    }
  }
  Stamp ("key {0} {1}" -f $k, $(if ($done) { "registered" } else { "NEVER registered" }))
  if ($ki -lt @($KeyList).Count) { Start-Sleep -Seconds $GapSec }
}

if ($Mouse -ne "") {
  foreach ($m in $Mouse.Split(";")) {
    if ($m.Trim() -eq "") { continue }
    Start-Sleep -Seconds $MouseGap
    Set-Content -Path "$WORK\ffe_automouse" -Value $m.Trim() -Encoding ascii
    $ok = Wait-Gone "$WORK\ffe_automouse" 30
    Stamp ("mouse '{0}' {1}" -f $m.Trim(), $(if ($ok) { "consumed" } else { "NOT consumed" }))
  }
}
if ($KeysAfter -ne "") {
  foreach ($k2 in ($KeysAfter.Split(",") | Where-Object { $_.Trim() -ne "" })) {
    Start-Sleep -Seconds $GapSec
    Set-Content -Path (Join-Path $WORK "ffe_autokey") -Value $k2.Trim() -Encoding ascii
    $ok2 = Wait-Gone (Join-Path $WORK "ffe_autokey") 30
    Stamp ("key-after {0} {1}" -f $k2.Trim(), $(if ($ok2) { "consumed" } else { "NOT consumed" }))
  }
}
if ($Script -ne "") {
  foreach ($st in $Script.Split(";")) {
    $st = $st.Trim(); if ($st -eq "") { continue }
    Start-Sleep -Seconds $GapSec
    if ($st.StartsWith("k")) { $fn = "ffe_autokey"; $val = $st.Substring(1).Trim() } else { $fn = "ffe_automouse"; $val = $st.Substring(1).Trim() }
    Set-Content -Path (Join-Path $WORK $fn) -Value $val -Encoding ascii
    $okS = Wait-Gone (Join-Path $WORK $fn) 30
    Stamp ("step {0} {1}" -f $st, $(if ($okS) { "consumed" } else { "NOT consumed" }))
  }
}
Stamp "keys done; watching $WatchSec s"
Start-Sleep -Seconds $WatchSec

if ($Leave) { Write-Host "left RUNNING (do not read logs until the next soft_run)"; exit 0 }
Set-Content -Path "$WORK\ffe_hold" -Value "1" -Encoding ascii
Remove-Item "$WORK\ffe_holding" -ErrorAction SilentlyContinue
Set-Content -Path "$WORK\ffe_reboot" -Value "1" -Encoding ascii
if (-not (Wait-Gone "$WORK\ffe_reboot" 30)) {
  Write-Host "end-of-run reset NOT consumed: the game froze hard or the machine died -- logs may be partial"
  Remove-Item "$WORK\ffe_reboot" -ErrorAction SilentlyContinue
  exit 3
}
if (-not (Wait-Holding 90)) { Write-Host "end-of-run reboot never reached HOLD -- logs may still be in use"; exit 3 }
Stamp "machine in HOLD; logs are safe to read"
$last = Select-String -Path "$WORK\ffe_dump.log" -Pattern "^presents=" -ErrorAction SilentlyContinue | Select-Object -Last 1
if ($last) { Write-Host $last.Line }
if (Test-Path "$WORK\ffe_crash.log") { Write-Host "--- ffe_crash.log ---"; Get-Content "$WORK\ffe_crash.log" | Select-Object -First 15 }
if (Test-Path "$WORK\ffe_watchdog.log") { Write-Host "WATCHDOG fired (freeze) -- see ffe_watchdog.log" }
