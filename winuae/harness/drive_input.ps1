# Drive the emulated Amiga programmatically: capture the mouse into WinUAE,
# move the pointer, click, press arrow keys, then ESC to quit the test program.
# WinUAE only receives input when it is the foreground window, so this briefly
# takes focus - it is the autonomous test loop, not a background task.
param(
  [int]$Moves = 60,
  [int]$Arrows = 8
)

Add-Type @"
using System; using System.Runtime.InteropServices;
public class Drv {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public const uint MOVE=0x0001, LDOWN=0x0002, LUP=0x0004, ABSOLUTE=0x8000;
  public const uint KEYUP=0x0002;
}
"@

$p = Get-Process winuae -ErrorAction SilentlyContinue |
     Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
if (-not $p) { Write-Output "ERROR: WinUAE not running"; exit 1 }
$h = $p.MainWindowHandle

$r = New-Object Drv+RECT
[void][Drv]::GetWindowRect($h, [ref]$r)
$cx = [int](($r.Left + $r.Right) / 2)
$cy = [int](($r.Top + $r.Bottom) / 2)

[void][Drv]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 800

# Click once to let WinUAE grab the mouse, then feed relative motion.
[void][Drv]::SetCursorPos($cx, $cy)
Start-Sleep -Milliseconds 300
[Drv]::mouse_event([Drv]::LDOWN, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 120
[Drv]::mouse_event([Drv]::LUP, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 600
Write-Output "mouse captured"

# A circular sweep exercises MOUSEMOVE in both axes.
for ($i = 0; $i -lt $Moves; $i++) {
  $a = $i * 0.35
  $dx = [int](14 * [Math]::Cos($a))
  $dy = [int](14 * [Math]::Sin($a))
  [Drv]::mouse_event([Drv]::MOVE, $dx, $dy, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 45
}
Write-Output "$Moves moves sent"

# Two clicks leave permanent marks on screen (visible proof in the screenshot).
for ($i = 0; $i -lt 2; $i++) {
  [Drv]::mouse_event([Drv]::LDOWN, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 120
  [Drv]::mouse_event([Drv]::LUP, 0, 0, 0, [IntPtr]::Zero)
  [Drv]::mouse_event([Drv]::MOVE, 40, 25, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 400
}
Write-Output "clicks sent"

# Arrow keys drive the ScrollRaster path.
for ($i = 0; $i -lt $Arrows; $i++) {
  $vk = if ($i % 2 -eq 0) { 0x28 } else { 0x26 }   # Down / Up
  [Drv]::keybd_event([byte]$vk, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 80
  [Drv]::keybd_event([byte]$vk, 0, [Drv]::KEYUP, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 500
}
Write-Output "$Arrows arrow keys sent"
