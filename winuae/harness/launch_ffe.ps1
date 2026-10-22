# Launch WinUAE for the FFE port and capture the emulated screen.
#
# Rules carried over from the OpenTTD port, each one learned the hard way:
#   - kill EVERY running WinUAE first and confirm the count is zero; the capture
#     picks a window with Select-Object -First 1, so a leftover instance silently
#     receives the attention;
#   - force the window onto the PRIMARY monitor, or the screenshot comes out pure
#     black and looks exactly like a boot failure;
#   - after deploying a new binary, RESTART the emulator -- otherwise you are
#     testing the previous build from memory.
#
# Usage:
#   launch_ffe.ps1 [-Config otd-aga.uae] [-WaitSec 45] [-Shot name.png] [-KeepOpen]
param(
  [string]$Config   = "otd-aga.uae",
  [int]   $WaitSec  = 45,
  [string]$Shot     = "amiga-shot.png",
  [switch]$KeepOpen
)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;using System.Runtime.InteropServices;
public class UW {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@
$ROOT = 'I:\GITHUB\Amiga_Frontier_2_First_Encouters'
$exe  = "$ROOT\tools\winuae281\winuae.exe"

# TWO independent conditions must hold before this script stops any process:
# it must be the PID we started ourselves, AND its executable path must be OUR
# winuae.exe. The user runs emulators for several projects at once
# (I:\GITHUB\Amiga_Remote_Play\... among them); killing one of those destroys
# work in progress. A PID check alone is not enough -- PIDs get recycled.
function Stop-OurWinUAE {
  param([int]$ProcId)
  $q = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcId" -ErrorAction SilentlyContinue
  if (-not $q) { return }                      # already gone
  if (-not $q.ExecutablePath) {
    Write-Host "REFUSING to stop pid ${ProcId}: cannot read its path"; return
  }
  if ($q.ExecutablePath -ne $exe) {
    Write-Host "REFUSING to stop pid ${ProcId}: $($q.ExecutablePath) is not ours"; return
  }
  Stop-Process -Id $ProcId -Force -ErrorAction SilentlyContinue
}
$wd   = "$ROOT\tools\winuae281"
$cfg  = if (Test-Path $Config) { $Config } else { "$ROOT\winuae\$Config" }
$out  = if ([IO.Path]::IsPathRooted($Shot)) { $Shot } else { "$ROOT\build\amiga-out\$Shot" }

if (-not (Test-Path $cfg)) { Write-Host "no such config: $cfg"; exit 1 }
New-Item -ItemType Directory -Force -Path (Split-Path $out) | Out-Null

# NEVER touch a WinUAE this script did not start. The user runs several
# emulated machines at once, for several different projects; killing "every
# running WinUAE" -- the rule inherited from the OpenTTD port, which assumed a
# single project on the machine -- destroys other people's work in progress.
# We start one instance, we remember its PID, and that is the only process this
# script is ever allowed to stop.
#
# This is safe for the capture too: the screenshot addresses OUR window by
# handle, obtained from our own Process object, never "the first WinUAE found".
$others = @(Get-Process winuae -ErrorAction SilentlyContinue)
if ($others.Count) {
  Write-Host "note: $($others.Count) other WinUAE instance(s) running (pid $($others.Id -join ', ')) - leaving them alone"
}
Write-Host "config: $cfg"

$p = Start-Process -FilePath $exe -ArgumentList '-f', $cfg, '-nodirectinput', '-norawinput' `
                   -WorkingDirectory $wd -PassThru
Write-Host "launched pid=$($p.Id), waiting ${WaitSec}s for boot + run"

for ($i = 0; $i -lt $WaitSec; $i++) {
  Start-Sleep -Seconds 1
  if ($p.HasExited) { Write-Host "WinUAE exited early (code $($p.ExitCode))"; exit 1 }
}
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Host "no WinUAE window handle"; exit 1 }

# Park the emulator on a SECONDARY monitor and raise it there.
#
# NEVER the primary one: the user records that screen for a timelapse of the
# project and this window kept cutting into the recording. (The OpenTTD port's
# rule said "force it onto the primary monitor or screenshots come out black" --
# that was written for a single-monitor setup and does not apply here.)
#
# Raising is still needed for whatever capture happens: SetForegroundWindow
# alone is refused for a background process, so the window stays buried and
# CopyFromScreen photographs whatever is painted over it -- once, a browser.
# HWND_TOPMOST is not subject to that restriction.
#
# For visual evidence prefer the framebuffer dump: the probes write
# PROGDIR:ffe_frame.raw and build\frame2png.py converts it. That path does not
# depend on the window being visible, or on which monitor it is on, at all.
$HWND_TOPMOST   = [IntPtr](-1)
$HWND_NOTOPMOST = [IntPtr](-2)
$target = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -First 1
if ($target) {
  $tx = $target.Bounds.X + 30; $ty = $target.Bounds.Y + 30
  Write-Host "parking WinUAE on $($target.DeviceName) at $tx,$ty (never the primary monitor)"
} else {
  $tx = 30; $ty = 30
  Write-Host "WARNING: only one monitor found - the window will land on the primary one"
}
[UW]::SetWindowPos($h, $HWND_TOPMOST, $tx, $ty, 0, 0, 0x0001) | Out-Null   # SWP_NOSIZE
Start-Sleep -Milliseconds 700
[UW]::SetForegroundWindow($h) | Out-Null
Start-Sleep -Milliseconds 900

$r = New-Object UW+RECT
[UW]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $ht = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $ht
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bmp.Dispose()
Write-Host "shot -> $out  (${w}x${ht})"
# do not leave the emulator pinned over everything else the user is doing
[UW]::SetWindowPos($h, $HWND_NOTOPMOST, 0, 0, 0, 0, 0x0001 -bor 0x0002) | Out-Null

if (-not $KeepOpen) {
  Stop-OurWinUAE -ProcId $p.Id
  Write-Host "closed our WinUAE (pid $($p.Id)); other instances untouched"
} else {
  Write-Host "our WinUAE (pid $($p.Id)) left running"
}
