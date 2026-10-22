# Click at Amiga-screen coordinates inside the WinUAE window.
# Assumes WinUAE 2.8.1 windowed, launched with -nodirectinput -norawinput
# (host cursor position maps 1:1 onto the emulated screen).
# Usage: click_amiga.ps1 -X 85 -Y 8 [-OffX 105] [-OffY 78] [-NoClick]
param(
  [int]$X = 0,
  [int]$Y = 0,
  [int]$OffX = 105,   # Amiga (0,0) offset inside the WinUAE window rect
  [int]$OffY = 78,
  [switch]$NoClick
)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class ClkA {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public const uint LDOWN=0x0002, LUP=0x0004;
}
"@
$p = Get-Process winuae -ErrorAction SilentlyContinue |
     Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
if (-not $p) { Write-Output "ERROR: WinUAE not running"; exit 1 }
$h = $p.MainWindowHandle
$r = New-Object ClkA+RECT
[void][ClkA]::GetWindowRect($h, [ref]$r)
[void][ClkA]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 400
$hx = $r.Left + $OffX + $X
$hy = $r.Top + $OffY + $Y
[void][ClkA]::SetCursorPos($hx, $hy)
Start-Sleep -Milliseconds 400
if (-not $NoClick) {
  [ClkA]::mouse_event([ClkA]::LDOWN, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 130
  [ClkA]::mouse_event([ClkA]::LUP, 0, 0, 0, [IntPtr]::Zero)
}
Write-Output "moved to amiga($X,$Y) host($hx,$hy)$(if (-not $NoClick) { ' + click' })"
