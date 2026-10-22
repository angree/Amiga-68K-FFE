# Launch the Amiga build AND LEAVE IT RUNNING, so the developer can watch it.
#
# autokey_run.ps1 exists to MEASURE: it drives the game, watches for a fixed
# time and then stops the emulator. That is exactly wrong when the request is
# "run it so I can see it" -- by the time you look, the window is gone. This
# does the same launch and the same key delivery and then simply returns,
# leaving the emulator up.
#
# The safety rules still hold and are not relaxed for convenience:
#   * every window this process owns is parked on a SECONDARY monitor, never
#     the primary one, which the developer records;
#   * SWP_NOACTIVATE, so the move never steals focus from what they are typing
#     in;
#   * keys go through PROGDIR:ffe_autokey files that the platform layer polls,
#     so nothing is injected into the global input queue and no window has to
#     be foreground;
#   * the zombie sweep stops only instances whose ExecutablePath is OURS.
#
# Stop it by closing the window, or with:
#   Get-CimInstance Win32_Process -Filter "Name='winuae.exe'" |
#     Where-Object { $_.ExecutablePath -eq
#       'I:\GITHUB\Amiga_Frontier_2_First_Encouters\tools\winuae281\winuae.exe' } |
#     ForEach-Object { Stop-Process -Id $_.ProcessId -Force }
param(
  [string]$Config = "ffe-020-aga-localhdf.uae",
  [string]$Keys   = "21,21",
  [int]   $GapSec = 3,
  # Work: directory the config mounts; must match the config (the all-local
  # one uses C:\temp\ffe\work). Named WorkDir, not Work: PowerShell variable
  # names are case-insensitive and $WORK is taken below.
  [string]$WorkDir = ""
)
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System; using System.Text; using System.Collections.Generic;
using System.Runtime.InteropServices;
public class WV {
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  // Rooted so the GC cannot collect it mid-enumeration: an unrooted delegate
  // returns a SHORT list, and on 2026-08-06 that made me state as a fact that a
  // running emulator had no windows.
  static EnumProc keep;
  public static List<IntPtr> All(uint want) {
    List<IntPtr> o = new List<IntPtr>();
    keep = delegate(IntPtr h, IntPtr p) {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == want && IsWindowVisible(h)) o.Add(h);
      return true;
    };
    EnumWindows(keep, IntPtr.Zero);
    return o;
  }
}
"@
$ROOT = 'I:\GITHUB\Amiga_Frontier_2_First_Encouters'
$exe  = "$ROOT\tools\winuae281\winuae.exe"
$wd   = "$ROOT\tools\winuae281"
$cfg  = "$ROOT\winuae\$Config"
$WORK = if ($WorkDir) { $WorkDir } else { "$ROOT\winuae\work" }

Remove-Item "$WORK\ffe_input.log","$WORK\ffe_autokey","$WORK\ffe_game.log",
            "$WORK\ffe_enter.log","$WORK\ffe_trace.log","$WORK\ffe_frame.raw",
            "$WORK\ffe_crash.log","$WORK\ffe_dump.log" -ErrorAction SilentlyContinue

Get-CimInstance Win32_Process -Filter "Name='winuae.exe'" -ErrorAction SilentlyContinue |
  ForEach-Object {
    if ($_.ExecutablePath -eq $exe) {
      Write-Host "stopping our orphaned instance $($_.ProcessId)"
      Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
    }
  }

$p = Start-Process -FilePath $exe -ArgumentList '-f', $cfg, '-nodirectinput', '-norawinput' `
                   -WorkingDirectory $wd -PassThru
Write-Host "WinUAE pid=$($p.Id)  config=$Config"

# LAST non-primary screen: the first one hosts a window that occludes anything
# parked at its top-left.
$scr = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -Last 1
$deadline = (Get-Date).AddSeconds(75)
$parked = 0
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 500
  if ($p.HasExited) { Write-Host "WinUAE exited (code $($p.ExitCode))"; exit 1 }
  $w = [WV]::All([uint32]$p.Id)
  if ($w.Count) {
    foreach ($h in $w) {
      if ($scr) {
        # 0x0001 SWP_NOSIZE | 0x0010 SWP_NOACTIVATE
        [WV]::SetWindowPos($h, [IntPtr]::Zero, $scr.Bounds.X + 40, $scr.Bounds.Y + 40, 0, 0, 0x0011) | Out-Null
      }
    }
    $parked = $w.Count
    break
  }
}
if (-not $parked) { Write-Host "no window yet; leaving it running anyway"; }
else { Write-Host "parked $parked window(s) on $(if($scr){$scr.DeviceName}else{'the only screen'}) -- never the primary" }

# Wait for the game's own log: presence means it is up. Log-driven, not
# screen-driven, for the reason autokey_run.ps1 gives.
$up = $false
$deadline = (Get-Date).AddSeconds(90)
while ((Get-Date) -lt $deadline) {
  if (Test-Path "$WORK\ffe_game.log") { $up = $true; break }
  Start-Sleep -Milliseconds 500
}
Write-Host $(if ($up) { "GAME UP (log present)" } else { "game log not seen yet" })

foreach ($k in $Keys.Split(',')) {
  Start-Sleep -Seconds $GapSec
  Set-Content -Path "$WORK\ffe_autokey" -Value "$k" -Encoding ascii
  Write-Host "  key $k dropped"
}

Write-Host ""
Write-Host "LEFT RUNNING as pid $($p.Id). Close its window when you are done,"
Write-Host "or stop it with the one-liner in the header of this script."
