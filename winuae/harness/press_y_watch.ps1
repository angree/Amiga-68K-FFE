# Wait for the FFE splash (pixel-detected), press 'y', watch what happens.
# Derived from inject_keys.ps1: same focus-confirmation and same stop rules --
# only the PID we started, only if its path is OUR winuae.exe.
param(
  [string]$Config  = "ffe-020-aga.uae",
  [int]   $MaxBootSec = 150,
  [uint16]$Scan    = 0x15          # 'y'
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
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint MapVirtualKeyW(uint code, uint mapType);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  // SetForegroundWindow is refused for background processes; attaching our
  // input queue to the CURRENT foreground window's thread lifts that
  // restriction (the documented AttachThreadInput technique).
  public static bool ForceFocus(IntPtr h) {
    IntPtr fg = GetForegroundWindow();
    if (fg == h) return true;
    uint cur = GetCurrentThreadId();
    uint other = GetWindowThreadProcessId(fg, IntPtr.Zero);
    AttachThreadInput(cur, other, true);
    SetForegroundWindow(h);
    AttachThreadInput(cur, other, false);
    System.Threading.Thread.Sleep(300);
    return GetForegroundWindow() == h;
  }
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  const uint KEYUP=0x0002, SCANCODE=0x0008, LDOWN=0x0002, LUP=0x0004;
  static IntPtr target;
  public static void SetTarget(IntPtr h) { target = h; }
  // PostMessage straight to OUR WinUAE window: the keystroke physically
  // cannot land in any other application, and the scancode travels in
  // lParam bits 16-23 exactly as a real WM_KEYDOWN would carry it.
  public static void Key(ushort scan, bool up) {
    uint vk = MapVirtualKeyW(scan, 1);
    uint l = 1u | ((uint)scan << 16);
    if (up) l |= 0xC0000000;
    PostMessageW(target, up ? 0x101u : 0x100u, (IntPtr)vk, (IntPtr)(long)l);
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
$outdir = "$ROOT\build\amiga-out"

function Stop-OurWinUAE {
  param([int]$ProcId)
  $q = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcId" -ErrorAction SilentlyContinue
  if (-not $q) { return }
  if (-not $q.ExecutablePath)     { Write-Host "REFUSING to stop pid ${ProcId}: cannot read its path"; return }
  if ($q.ExecutablePath -ne $exe) { Write-Host "REFUSING to stop pid ${ProcId}: $($q.ExecutablePath) is not ours"; return }
  Stop-Process -Id $ProcId -Force -ErrorAction SilentlyContinue
}

function Grab([Inj+RECT]$r) {
  $bmp = New-Object System.Drawing.Bitmap ($r.R-$r.L), ($r.B-$r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size)
  $g.Dispose()
  return $bmp
}
# Splash = black background; Workbench = light gray. Average a patch low in the
# emulated area (window coords ~ (200..300, 500..540)).
function PatchBrightness([System.Drawing.Bitmap]$bmp) {
  $sum = 0; $n = 0
  for ($y = 500; $y -lt 540; $y += 8) { for ($x = 200; $x -lt 300; $x += 10) {
    if ($x -lt $bmp.Width -and $y -lt $bmp.Height) {
      $c = $bmp.GetPixel($x, $y); $sum += ([int]$c.R + $c.G + $c.B); $n++
    } } }
  if ($n -eq 0) { return 255 }
  return [int]($sum / (3 * $n))
}

$others = @(Get-Process winuae -ErrorAction SilentlyContinue)
if ($others.Count) { Write-Host "note: $($others.Count) other WinUAE running - leaving them alone" }

$p = Start-Process -FilePath $exe -ArgumentList '-f', $cfg, '-nodirectinput', '-norawinput' `
                   -WorkingDirectory $wd -PassThru
Write-Host "our pid=$($p.Id); waiting for the splash (pixel-detect, max ${MaxBootSec}s)"
Start-Sleep -Seconds 30
$p.Refresh()
$h = $p.MainWindowHandle
if ($h -eq [IntPtr]::Zero) { Write-Host "no window"; Stop-OurWinUAE -ProcId $p.Id; exit 1 }
[Inj]::SetTarget($h)

$target = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -First 1
if ($target) { $tx = $target.Bounds.X + 30; $ty = $target.Bounds.Y + 30 }
else { $tx = 30; $ty = 30; Write-Host "WARNING: one monitor only" }
[Inj]::SetWindowPos($h, [IntPtr](-1), $tx, $ty, 0, 0, 0x0001) | Out-Null
Start-Sleep -Milliseconds 600
$r = New-Object Inj+RECT; [Inj]::GetWindowRect($h, [ref]$r) | Out-Null

# Engage WinUAE's mouse capture NOW, while the Amiga is still BOOTING --
# keyboard input is only processed while the mouse is captured, and a click
# after the game is up counts as "any other key" on the splash (proven:
# ACTION 0xff). A click into the Workbench desktop during boot is harmless
# and the capture persists for the whole session.
if ([Inj]::ForceFocus($h)) {
  [Inj]::SetCursorPos([int](($r.L + $r.R) / 2), [int](($r.T + $r.B) / 2)) | Out-Null
  Start-Sleep -Milliseconds 250
  [Inj]::Click()
  Write-Host "boot-time capture click done"
} else {
  Write-Host "WARNING: could not focus for the boot-time capture click"
}

$deadline = (Get-Date).AddSeconds($MaxBootSec)
$splash = $false
while ((Get-Date) -lt $deadline) {
  if ($p.HasExited) { Write-Host "WinUAE exited early"; exit 1 }
  $bmp = Grab $r
  $b = PatchBrightness $bmp
  $bmp.Dispose()
  Write-Host ("  t+{0}s brightness={1}" -f [int]((Get-Date) - $p.StartTime).TotalSeconds, $b)
  if ($b -lt 40) { $splash = $true; break }
  Start-Sleep -Seconds 5
}
if (-not $splash) {
  Write-Host "splash never detected; leaving a last shot and stopping"
  $bmp = Grab $r; $bmp.Save("$outdir\press_y_timeout.png", [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
  [Inj]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0003) | Out-Null
  Stop-OurWinUAE -ProcId $p.Id; exit 1
}
Write-Host "SPLASH DETECTED - confirming focus"
$bmp = Grab $r; $bmp.Save("$outdir\press_y_00_splash.png", [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()

# The splash treats ANY input as a key -- a click into the emulated area is
# "any other key to exit" and kills the run. So: try SetForegroundWindow bare
# first; if that is refused, click the TITLE BAR (top strip), which focuses
# the window without sending a mouse event into the emulation.
$focusOk = $false
for ($try = 1; $try -le 4 -and -not $focusOk; $try++) {
  [Inj]::SetWindowPos($h, [IntPtr](-1), 0, 0, 0, 0, 0x0003) | Out-Null
  Start-Sleep -Milliseconds 400
  [Inj]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 500
  if ([Inj]::GetForegroundWindow() -eq $h) { $focusOk = $true; break }
  [Inj]::GetWindowRect($h, [ref]$r) | Out-Null
  [Inj]::SetCursorPos([int](($r.L + $r.R) / 2), $r.T + 8) | Out-Null
  Start-Sleep -Milliseconds 250
  [Inj]::Click()
  Start-Sleep -Milliseconds 700
  [Inj]::SetForegroundWindow($h) | Out-Null
  Start-Sleep -Milliseconds 500
  if ([Inj]::GetForegroundWindow() -eq $h) { $focusOk = $true; break }
  Write-Host "  focus attempt $try failed"
}
if (-not $focusOk) {
  Write-Host "ABORTING: could not focus our WinUAE; refusing to inject."
  [Inj]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0003) | Out-Null
  Stop-OurWinUAE -ProcId $p.Id; exit 2
}
Write-Host "focus confirmed - pressing 'y' (scan 0x$('{0:X2}' -f $Scan))"
# Delivery is flaky (~50% of runs the scancode never reaches WinUAE). The
# Amiga side writes PROGDIR:ffe_input.log on the FIRST key event, and PROGDIR
# is a host directory -- so poll that file and retry the press until it
# appears. The caller must delete the file before the run.
$il = 'I:\GITHUB\Amiga_Frontier_2_First_Encouters\winuae\work\ffe_input.log'
for ($try = 1; $try -le 8; $try++) {
  if (-not [Inj]::ForceFocus($h)) { Write-Host "  focus refused (try $try)"; Start-Sleep -Seconds 2; continue }
  [Inj]::Key($Scan, $false); Start-Sleep -Milliseconds 150; [Inj]::Key($Scan, $true)
  Start-Sleep -Seconds 3
  if (Test-Path $il) { Write-Host "  key DELIVERED on try $try"; break }
  Write-Host "  no key event seen, retrying"
}

# After the splash 'y', give the menu ~12 s of intro, then press '1'
# (scancode 0x02) -- "start new game" -- with the same delivery check: the
# input log line count must grow.
Start-Sleep -Seconds 12
$before = (Get-Content $il -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
for ($try = 1; $try -le 8; $try++) {
  if (-not [Inj]::ForceFocus($h)) { Write-Host "  focus refused for '1' (try $try)"; Start-Sleep -Seconds 2; continue }
  [Inj]::Key([uint16]0x02, $false); Start-Sleep -Milliseconds 150; [Inj]::Key([uint16]0x02, $true)
  Start-Sleep -Seconds 3
  $now = (Get-Content $il -ErrorAction SilentlyContinue | Measure-Object -Line).Lines
  if ($now -gt $before) { Write-Host "  '1' DELIVERED on try $try"; break }
  Write-Host "  '1' not seen, retrying"
}

foreach ($dt in 2, 6, 12, 20, 30, 45, 60, 75, 90) {
  Start-Sleep -Seconds ($dt - ($(if ($dt -eq 2) { 0 } else { $prev })))
  $prev = $dt
  [Inj]::GetWindowRect($h, [ref]$r) | Out-Null
  $bmp = Grab $r
  $b = PatchBrightness $bmp
  $bmp.Save(("$outdir\press_y_{0:D2}s.png" -f $dt), [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  Write-Host ("  +{0}s after y: brightness={1}" -f $dt, $b)
}

[Inj]::SetWindowPos($h, [IntPtr](-2), 0, 0, 0, 0, 0x0003) | Out-Null
Stop-OurWinUAE -ProcId $p.Id
Write-Host "our pid stopped, others untouched"
$il = 'I:\GITHUB\Amiga_Frontier_2_First_Encouters\winuae\work\ffe_input.log'
if (Test-Path $il) { Write-Host "--- ffe_input.log ---"; Get-Content $il } else { Write-Host "NO ffe_input.log" }
