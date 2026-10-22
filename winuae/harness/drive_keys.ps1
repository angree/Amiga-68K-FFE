# Keyboard probe: three different injection methods in one run, so a single
# Amiga-side log identifies which one WinUAE actually accepts.
#   M1 - keybd_event with a virtual-key code (what failed before)
#   M2 - SendInput with a hardware SCANCODE
#   M3 - SendInput with SCANCODE + EXTENDEDKEY (correct for the arrow cluster)
# The Amiga program prints the last raw key code, so the methods are told apart
# by which Amiga rawkey shows up: A=$20, Down=$4D, Up=$4C.

Add-Type @"
using System; using System.Runtime.InteropServices;
public class Keys {
  [StructLayout(LayoutKind.Sequential)]
  public struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)]
  public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; }
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] p, int cb);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint f, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public const uint EXTENDED=0x0001, KEYUP=0x0002, SCANCODE=0x0008;
  public const uint LDOWN=0x0002, LUP=0x0004;

  public static void Send(ushort scan, bool ext, bool up) {
    uint f = SCANCODE;
    if (ext) f |= EXTENDED;
    if (up)  f |= KEYUP;
    INPUT[] a = new INPUT[1];
    a[0].type = 1;
    a[0].ki.wVk = 0; a[0].ki.wScan = scan; a[0].ki.dwFlags = f;
    SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@

$p = Get-Process winuae -ErrorAction SilentlyContinue |
     Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
if (-not $p) { Write-Output "ERROR: WinUAE not running"; exit 1 }
# SetForegroundWindow called from a background process is refused by Windows,
# so injected keys would go to whatever window really has focus. A synthetic
# click on the WinUAE window activates it for real - keys only work after this.
$h = $p.MainWindowHandle
$r = New-Object Keys+RECT
[void][Keys]::GetWindowRect($h, [ref]$r)
[void][Keys]::SetCursorPos([int](($r.Left+$r.Right)/2), [int](($r.Top+$r.Bottom)/2))
Start-Sleep -Milliseconds 250
[Keys]::mouse_event([Keys]::LDOWN, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 120
[Keys]::mouse_event([Keys]::LUP, 0, 0, 0, [IntPtr]::Zero)
Start-Sleep -Milliseconds 900
if ([Keys]::GetForegroundWindow() -eq $h) { Write-Output "WinUAE has focus - keys can be delivered" }
else { Write-Output "WARNING: WinUAE is NOT foreground; keys will be lost" }

Write-Output "M1: keybd_event VK_DOWN x5 (expect Amiga rawkey \$4D if it works)"
for ($i=0; $i -lt 5; $i++) {
  [Keys]::keybd_event(0x28, 0, 0, [IntPtr]::Zero); Start-Sleep -Milliseconds 90
  [Keys]::keybd_event(0x28, 0, [Keys]::KEYUP, [IntPtr]::Zero); Start-Sleep -Milliseconds 350
}
Start-Sleep -Seconds 3

Write-Output "M2: SendInput scancode 'A' (0x1E) x5 (expect Amiga rawkey \$20)"
for ($i=0; $i -lt 5; $i++) {
  [Keys]::Send(0x1E, $false, $false); Start-Sleep -Milliseconds 90
  [Keys]::Send(0x1E, $false, $true);  Start-Sleep -Milliseconds 350
}
Start-Sleep -Seconds 3

Write-Output "M3: SendInput scancode+extended Down (0x50) x5 (expect Amiga rawkey \$4D)"
for ($i=0; $i -lt 5; $i++) {
  [Keys]::Send(0x50, $true, $false); Start-Sleep -Milliseconds 90
  [Keys]::Send(0x50, $true, $true);  Start-Sleep -Milliseconds 350
}
Start-Sleep -Seconds 3

Write-Output "M3b: SendInput scancode+extended Up (0x48) x5 (expect Amiga rawkey \$4C)"
for ($i=0; $i -lt 5; $i++) {
  [Keys]::Send(0x48, $true, $false); Start-Sleep -Milliseconds 90
  [Keys]::Send(0x48, $true, $true);  Start-Sleep -Milliseconds 350
}
Write-Output "keyboard probe done"
