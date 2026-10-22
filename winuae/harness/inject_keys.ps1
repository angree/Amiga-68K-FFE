# Inject keystrokes into OUR WinUAE and capture the emulated screen.
#
# Method: SendInput with a hardware SCANCODE. This is the one the OpenTTD port
# found to work (its drive_keys.ps1 compares three methods; keybd_event with a
# virtual-key code does NOT reach WinUAE). A synthetic left click on the WinUAE
# window is required first: SetForegroundWindow called from a background process
# is refused by Windows, so without the click the keys go to whatever window
# really has focus.
#
# Stops ONLY the emulator this script started, verified by PID *and* by
# executable path -- the user runs emulators for other projects at the same time.
param(
  [string]$Config  = "ffe-020-aga.uae",
  [int]   $BootSec = 34,
  [string]$Shot    = "api-keys.png",
  [string]$Log     = "ffe_apitest.log",
  # PC set-1 scancodes to hold down together, as hex. Default: keypad 8 (thrust)
  # and Space -- two keys FFE actually flies with.
  [string[]]$Hold  = @('0x48','0x39'),
  [int]   $HoldSec = 6
)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Inj {
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT { public int dx, dy; public uint mouseData, dwFlags, time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Explicit)]   public struct INPUT { [FieldOffset(0)] public uint type; [FieldOffset(8)] public KEYBDINPUT ki; [FieldOffset(8)] public MOUSEINPUT mi; }
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] p, int cb);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  const uint KEYUP=0x0002, SCANCODE=0x0008, LDOWN=0x0002, LUP=0x0004;
  public static void Key(ushort scan, bool up) {
    INPUT[] a = new INPUT[1]; a[0].type = 1;
    a[0].ki.wScan = scan; a[0].ki.dwFlags = SCANCODE | (up ? KEYUP : 0);
    SendInput(1, a, Marshal.SizeOf(typeof(INPUT)));
  }
  public static void Click() {
    INPUT[] a = new INPUT[2];
    a[0].type = 0; a[0].mi.dwFlags = LDOWN;
    a[1].type = 0; a[1].mi.dwFlags = LUP;
    SendInput(2, a, Marshal.SizeOf(typeof(INPUT)));
  }
}
"@
$ROOT = 'I:\GITHUB\Amiga_Frontier_2_First_Encouters'
$exe  = "$ROOT\tools\winuae281\winuae.exe"
$wd   = "$ROOT\tools\winuae281"
$cfg  = "$ROOT\winuae\$Config"
$out  = "$ROOT\build\amiga-out\$Shot"
$log  = "$ROOT\winuae\work\$Log"

function Stop-OurWinUAE {
  param([int]$ProcId)
  $q = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcId" -ErrorAction SilentlyContinue
  if (-not $q) { return }
  if (-not $q.ExecutablePath)     { Write-Host "REFUSING to stop pid ${ProcId}: cannot read its path"; return }
  if ($q.ExecutablePath -ne $exe) { Write-Host "REFUSING to stop pid ${ProcId}: $($q.ExecutablePath) is not ours"; return }
  Stop-Process -Id $ProcId -Force -ErrorAction SilentlyContinue
}

Remove-Item $log -ErrorAction SilentlyContinue
$others = @(Get-Process winuae -ErrorAction SilentlyContinue)
if ($others.Count) { Write-Host "note: $($others.Count) other WinUAE running - leaving them alone" }

$p = Start-Process -FilePath $exe -ArgumentList '-f', $cfg, '-nodirectinput', '-norawinput' `
                   -WorkingDirectory $wd -PassThru
Write-Host "our pid=$($p.Id); booting for ${BootSec}s"
Start-Sleep -Seconds $BootSec
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Host "no window"; Stop-OurWinUAE -ProcId $p.Id; exit 1 }

# NEVER put the emulator on the primary monitor: the user records that screen
# for a timelapse and our window kept cutting into the recording. Park it on a
# secondary monitor instead. Visual evidence does not come from this window
# anyway -- the probe dumps its own framebuffer and build\frame2png.py turns it
# into a PNG, which is both monitor-independent and more trustworthy.
Add-Type -AssemblyName System.Windows.Forms
$target = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -First 1
if ($target) {
  $tx = $target.Bounds.X + 30; $ty = $target.Bounds.Y + 30
  Write-Host "parking WinUAE on $($target.DeviceName) at $tx,$ty (never the primary monitor)"
} else {
  $tx = 30; $ty = 30
  Write-Host "WARNING: only one monitor found - the window will land on the primary one"
}
[Inj]::SetWindowPos($h, [IntPtr](-1), $tx, $ty, 0, 0, 0x0001) | Out-Null   # topmost, keep size
Start-Sleep -Milliseconds 600
$r = New-Object Inj+RECT; [Inj]::GetWindowRect($h, [ref]$r) | Out-Null

# NEVER inject blindly. SendInput delivers to whatever window has the real
# keyboard focus, and if raising WinUAE failed the keystrokes land in the user's
# browser or editor instead. That has happened: a run once typed keypad-8, Space
# and Esc into a browser window because the emulator never came to the front.
# So: confirm the foreground window IS ours, retry a few times, and if it still
# is not, refuse to send anything and say why.
$focusOk = $false
for ($try = 1; $try -le 4 -and -not $focusOk; $try++) {
  [Inj]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0001 -bor 0x0002) | Out-Null  # topmost, keep pos+size
  Start-Sleep -Milliseconds 400
  [Inj]::GetWindowRect($h, [ref]$r) | Out-Null
  [Inj]::SetCursorPos([int](($r.L + $r.R) / 2), [int](($r.T + $r.B) / 2)) | Out-Null
  Start-Sleep -Milliseconds 250
  [Inj]::Click()
  Start-Sleep -Milliseconds 700
  [Inj]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 500
  if ([Inj]::GetForegroundWindow() -eq $h) { $focusOk = $true; break }
  Write-Host "  focus attempt $try failed"
}
if (-not $focusOk) {
  $sb = New-Object System.Text.StringBuilder 256
  [Inj]::GetWindowText([Inj]::GetForegroundWindow(), $sb, 256) | Out-Null
  Write-Host "ABORTING: could not focus our WinUAE - foreground is '$($sb.ToString())'."
  Write-Host "Refusing to inject keystrokes into somebody else's window."
  [Inj]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0001 -bor 0x0002) | Out-Null
  Stop-OurWinUAE -ProcId $p.Id
  exit 2
}
Write-Host "focus confirmed on our WinUAE window"

$codes = $Hold | ForEach-Object { [uint16]([convert]::ToInt32($_, 16)) }
Write-Host ("holding scancodes: " + (($codes | ForEach-Object { '0x{0:X2}' -f $_ }) -join ', ') + " for ${HoldSec}s")
foreach ($c in $codes) { [Inj]::Key($c, $false); Start-Sleep -Milliseconds 60 }
Start-Sleep -Seconds $HoldSec

$bmp = New-Object System.Drawing.Bitmap ($r.R-$r.L), ($r.B-$r.T)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png); $g.Dispose(); $bmp.Dispose()
Write-Host "shot -> $out"

foreach ($c in $codes) { [Inj]::Key($c, $true); Start-Sleep -Milliseconds 60 }
Start-Sleep -Seconds 2
[Inj]::Key([uint16]0x01, $false); Start-Sleep -Milliseconds 500   # Esc ends the probe
[Inj]::Key([uint16]0x01, $true)
Start-Sleep -Seconds 3

[Inj]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0001 -bor 0x0002) | Out-Null  # un-topmost
Stop-OurWinUAE -ProcId $p.Id
Write-Host "our pid stopped, others untouched`n"
if (Test-Path $log) { Get-Content $log } else { Write-Host "NO LOG" }
