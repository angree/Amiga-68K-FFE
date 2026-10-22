# Probe which host->Amiga input mechanism actually reaches IDCMP.
# WinUAE may read input via DirectInput (ignores injected events) or via plain
# Windows messages, and the answer decides how the whole test loop is driven.
# Four phases, spaced so the Amiga side's periodic counter dump identifies each.

Add-Type @"
using System; using System.Runtime.InteropServices;
public class Prb {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public const uint MOVE=0x0001, LDOWN=0x0002, LUP=0x0004;
  public const uint KEYUP=0x0002;
  public const uint WM_MOUSEMOVE=0x0200, WM_KEYDOWN=0x0100, WM_KEYUP=0x0101;
}
"@

$p = Get-Process winuae -ErrorAction SilentlyContinue |
     Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
if (-not $p) { Write-Output "ERROR: WinUAE not running"; exit 1 }
$h = $p.MainWindowHandle
$r = New-Object Prb+RECT
[void][Prb]::GetWindowRect($h, [ref]$r)
$cx = [int](($r.Left + $r.Right) / 2)
$cy = [int](($r.Top + $r.Bottom) / 2)
[void][Prb]::SetForegroundWindow($h)
Start-Sleep -Milliseconds 700

Write-Output "PHASE A (SetCursorPos, uncaptured) starting"
for ($i = 0; $i -lt 40; $i++) {
  $a = $i * 0.3
  [void][Prb]::SetCursorPos($cx + [int](120 * [Math]::Cos($a)), $cy + [int](90 * [Math]::Sin($a)))
  Start-Sleep -Milliseconds 100
}

Write-Output "PHASE B (keybd_event arrows, uncaptured) starting"
Start-Sleep -Seconds 2
for ($i = 0; $i -lt 10; $i++) {
  $vk = if ($i % 2 -eq 0) { 0x28 } else { 0x26 }
  [Prb]::keybd_event([byte]$vk, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 100
  [Prb]::keybd_event([byte]$vk, 0, [Prb]::KEYUP, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 300
}

Write-Output "PHASE C (capture + mouse_event relative) starting"
Start-Sleep -Seconds 2
[void][Prb]::SetCursorPos($cx, $cy)
[Prb]::mouse_event([Prb]::LDOWN, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 120
[Prb]::mouse_event([Prb]::LUP, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 800
for ($i = 0; $i -lt 40; $i++) {
  $a = $i * 0.3
  [Prb]::mouse_event([Prb]::MOVE, [int](16 * [Math]::Cos($a)), [int](16 * [Math]::Sin($a)), 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 100
}

Write-Output "PHASE D (keybd_event arrows, captured) starting"
Start-Sleep -Seconds 2
for ($i = 0; $i -lt 10; $i++) {
  $vk = if ($i % 2 -eq 0) { 0x28 } else { 0x26 }
  [Prb]::keybd_event([byte]$vk, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 100
  [Prb]::keybd_event([byte]$vk, 0, [Prb]::KEYUP, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 300
}
Write-Output "all phases done"
