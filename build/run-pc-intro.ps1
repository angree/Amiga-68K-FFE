# Drive the PC REFERENCE build to the INTRO screen ('y' only) and capture its dump.
#
# Why this exists: the Amiga viewport is measurably reaching the rasteriser with
# plausible camera-space coordinates, and eyeballing "is that a landing pad?"
# has already produced one retracted claim. build\m0-pc\run\ffesdl-dump.exe runs
# the SAME game logic on x86 and writes the SAME FFEFRM01 file, so a pixel diff
# against it is the only check that settles the question.
#
# Two house rules are honoured here exactly as launch_ffe.ps1 honours them:
#
#   - the window is parked on a SECONDARY monitor. The developer records the
#     primary screen for a timelapse.
#   - the process is stopped by PID *and* by verifying its ExecutablePath is the
#     dump build we started. Never by name -- several unrelated emulated and
#     native games run on this machine at once, and a stray kill has already
#     destroyed someone else's session once.
#
#   powershell -File build\run-pc-dump.ps1 [-WatchSec 60] [-Out build\pcref.raw]
param(
  [int]$WatchSec = 60,
  [string]$Out = "build\pcref-intro.raw",
  # Which reference build to drive. Both write ffe_frame.raw; ffesdl-probe.exe
  # additionally writes pc_proj.log, the projection input log that the Amiga's
  # FfeProjLog is compared against. The exe must sit in the run directory --
  # that is where MISSION.DAT, the CABIN bitmaps and SDL.dll are.
  [string]$ExeName = "ffesdl-dump.exe"
)
$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Windows.Forms

$RUN = "I:\GITHUB\Amiga_Frontier_2_First_Encouters\build\m0-pc\run"
$EXE = Join-Path $RUN $ExeName
$RAW = Join-Path $RUN "ffe_frame.raw"

if (-not (Test-Path $EXE)) { Write-Host "MISSING $EXE -- run build\build-pc-dump.py first"; exit 1 }

# STALE-BINARY GUARD. build-pc-probe.py links into build\m0-pc-probe\ while the
# harness runs out of build\m0-pc\run\, so the staging copy is a separate step --
# and forgetting it cost a whole run that reported "the hook never fired" when
# the hook was simply not in the executable being run.
$built = "I:\GITHUB\Amiga_Frontier_2_First_Encouters\build\m0-pc-probe\$ExeName"
if (Test-Path $built) {
  $tb = (Get-Item $built).LastWriteTime
  $tr = (Get-Item $EXE).LastWriteTime
  if ($tb -gt $tr) {
    Copy-Item $built $EXE -Force
    Write-Host "staged a newer $ExeName from m0-pc-probe ($tb)"
  }
}

Add-Type @"
using System;using System.Collections.Generic;using System.Runtime.InteropServices;using System.Text;
public class PW {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk,byte scan,uint flags,IntPtr extra);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h,uint msg,IntPtr wp,IntPtr lp);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f,IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h,out uint pid);
  [DllImport("user32.dll",CharSet=CharSet.Auto)] static extern int GetClassName(IntPtr h,StringBuilder s,int n);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool AttachThreadInput(uint from,uint to,bool attach);
  [DllImport("user32.dll")] static extern bool BringWindowToTop(IntPtr h);
  [DllImport("user32.dll")] static extern bool ShowWindow(IntPtr h,int cmd);
  [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
  delegate bool EnumProc(IntPtr h,IntPtr p);

  // Windows refuses SetForegroundWindow to a process that does not already own
  // the foreground -- which is exactly our case when the harness is driven from
  // a script. Borrowing the foreground thread's input queue lifts that lock.
  // NOTE: this sends NO keystrokes; the ALT-key trick that is usually suggested
  // for the same purpose would fire a key into whatever the developer has in
  // front, which is precisely what the focus check exists to prevent.
  public static bool Raise(IntPtr h) {
    IntPtr fg = GetForegroundWindow();
    uint self = GetCurrentThreadId();
    uint other = 0;
    uint fgpid;
    if (fg != IntPtr.Zero) other = GetWindowThreadProcessId(fg, out fgpid);
    bool attached = (other != 0 && other != self && AttachThreadInput(self, other, true));
    ShowWindow(h, 9);            // SW_RESTORE
    BringWindowToTop(h);
    bool ok = SetForegroundWindow(h);
    if (attached) AttachThreadInput(self, other, false);
    return ok;
  }

  // Find the process's SDL window by CLASS, not by Process.MainWindowHandle.
  // A -mconsole build owns a console window too, and MainWindowHandle returns
  // whichever happens to exist when it is polled -- one run grabbed the console,
  // the keys went nowhere and the game sat on the licence splash for the whole
  // watch while the harness reported "focused".
  public static IntPtr FindGameWindow(uint want) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr p) {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid != want || !IsWindowVisible(h)) return true;
      StringBuilder sb = new StringBuilder(256);
      GetClassName(h, sb, sb.Capacity);
      string c = sb.ToString();
      if (c.IndexOf("SDL", StringComparison.OrdinalIgnoreCase) >= 0) { found = h; return false; }
      return true;
    }, IntPtr.Zero);
    return found;
  }

  // Every window the process owns, not just the game's. A -mconsole build also
  // owns a console window, and that one was landing on the PRIMARY monitor --
  // the screen the developer records. Both get parked on a secondary screen.
  public static List<IntPtr> AllWindows(uint want) {
    List<IntPtr> all = new List<IntPtr>();
    EnumWindows(delegate(IntPtr h, IntPtr p) {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid == want && IsWindowVisible(h)) all.Add(h);
      return true;
    }, IntPtr.Zero);
    return all;
  }
}
"@

# keybd_event, not SendKeys. The first attempt used SendKeys and the reference
# sat on the licence splash for the whole run -- SDL reads the keyboard through
# the low-level input path, and a synthesised WM_CHAR never reaches it. This
# drives the same layer a real key press does, with the scancode alongside the
# virtual key because SDL 1.2 maps from the scancode.
#
# SUPERSEDED by Send-KeyPost below, kept only as documentation of why: this
# drives the GLOBAL input queue, so it demands the foreground and yanks it off
# whatever the developer is doing. SDL 1.2's windib driver handles plain
# WM_KEYDOWN/WM_KEYUP in its window procedure, so the keys can be POSTED to the
# game's window instead -- no focus, no interruption, and no risk of a stray
# keystroke landing in the developer's editor.
function Send-Key ([byte]$vk, [byte]$scan) {
  [PW]::keybd_event($vk, $scan, 0x0008, [IntPtr]::Zero)   # KEYEVENTF_SCANCODE
  Start-Sleep -Milliseconds 60
  [PW]::keybd_event($vk, $scan, 0x000A, [IntPtr]::Zero)   # + KEYEVENTF_KEYUP
}

function Send-KeyPost ([IntPtr]$h, [byte]$vk, [byte]$scan) {
  $down = [IntPtr](1 -bor ($scan -shl 16))
  $up   = [IntPtr](1 -bor ($scan -shl 16) -bor 0xC0000000)
  [PW]::PostMessage($h, 0x0100, [IntPtr]$vk, $down) | Out-Null   # WM_KEYDOWN
  Start-Sleep -Milliseconds 60
  [PW]::PostMessage($h, 0x0101, [IntPtr]$vk, $up) | Out-Null     # WM_KEYUP
}

function Get-DumpColours ($path) {
  if (-not (Test-Path $path)) { return -1 }
  try {
    $b = [IO.File]::ReadAllBytes($path)
    if ($b.Length -ne 64776) { return -1 }
    $seen = New-Object bool[] 256
    for ($i = 776; $i -lt $b.Length; $i++) { $seen[$b[$i]] = $true }
    return ($seen | Where-Object { $_ }).Count
  } catch { return -1 }
}

# A stale dump from an earlier run would be indistinguishable from a fresh one,
# and "the reference says X" off a stale file is exactly the failure this whole
# harness family exists to prevent.
if (Test-Path $RAW) { Remove-Item $RAW -Force }

$p = Start-Process -FilePath $EXE -WorkingDirectory $RUN -PassThru
Write-Host "started pc reference pid=$($p.Id)"

# Refuse to touch a PID whose executable cannot be read back -- PIDs are
# recycled, so a PID match alone is not evidence.
function Stop-OurPcRef ($proc) {
  if ($null -eq $proc -or $proc.HasExited) { return }
  $path = $null
  try { $path = (Get-CimInstance Win32_Process -Filter "ProcessId=$($proc.Id)").ExecutablePath } catch { }
  if ($null -eq $path) {
    Write-Host "REFUSING to stop pid $($proc.Id): its ExecutablePath cannot be read"
    return
  }
  if ($path -ne $EXE) {
    Write-Host "REFUSING to stop pid $($proc.Id): it is '$path', not our dump build"
    return
  }
  Stop-Process -Id $proc.Id -Force
  Write-Host "stopped our pid $($proc.Id), nothing else touched"
}

try {
  $h = [IntPtr]::Zero
  for ($i = 0; $i -lt 60 -and $h -eq [IntPtr]::Zero; $i++) {
    Start-Sleep -Milliseconds 250
    $h = [PW]::FindGameWindow([uint32]$p.Id)
  }
  if ($h -eq [IntPtr]::Zero) { Write-Host "no SDL window appeared"; Stop-OurPcRef $p; exit 2 }
  Write-Host ("found SDL window {0:x}" -f [int64]$h)

  $target = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -First 1
  if ($target) {
    $off = 40
    foreach ($w in [PW]::AllWindows([uint32]$p.Id)) {
      [PW]::SetWindowPos($w, [IntPtr]::Zero, $target.WorkingArea.X + $off, $target.WorkingArea.Y + $off, 0, 0, 0x0001) | Out-Null
      $off += 60
    }
    Write-Host "parked $(([PW]::AllWindows([uint32]$p.Id)).Count) window(s) incl. the console on $($target.DeviceName) (never the primary monitor)"
  } else {
    Write-Host "WARNING: only one monitor found"
  }

  # Keys go to the game only once it is genuinely focused. A failed raise would
  # type 'y' and '1' into whatever the developer has in front.
  Start-Sleep -Seconds 3
  # NO FOCUS IS TAKEN. The keys are posted straight to the game's own window, so
  # the developer keeps the keyboard and the desktop stays still. This also
  # retires the old failure mode entirely: a posted message can only ever reach
  # the window it is addressed to, so there is no way for a key to land in an
  # editor, and no reason to refuse the run when something else is in front.
  Write-Host "posting 'y' (accept) then '1' (new commander) -- focus untouched"

  # Retry rather than fire once and hope. The splash is a 2-colour frame, so the
  # dump's own colour count says whether the key landed -- an observable check,
  # not a sleep long enough to "probably" work.
  $deadline = (Get-Date).AddSeconds($WatchSec)
  $stage = 0
  while ((Get-Date) -lt $deadline) {
    $c = Get-DumpColours $RAW
    Write-Host ("  stage={0} dumpcolours={1}" -f $stage, $c)
    if ($stage -eq 0) {
      Send-KeyPost $h 0x59 0x15                    # 'y'
      if ($c -gt 4) { $stage = 1 }          # past the 2-colour licence splash
    } elseif ($stage -eq 1) {
      # INTRO VARIANT: never send '1'. We want the frame the developer
      # photographed -- the "A FRONTIER DEVELOPMENTS PRODUCTION" intro --
      # which is what our Amiga run reaches with keys 21,21 and no '1'.
      # Let the intro animate a little, then keep the dump as it stands.
      if ($c -ge 8) { Write-Host "  intro-like frame captured (colours=$c)"; break }
    } else {
      break
    }
    Start-Sleep -Seconds 4
  }
}
finally {
  Stop-OurPcRef $p
  # Nothing to hand back: this harness never takes the focus in the first place.
}

if (Test-Path $RAW) {
  Copy-Item $RAW $Out -Force
  Write-Host "captured $Out ($((Get-Item $Out).Length) bytes)"
} else {
  Write-Host "NO DUMP WRITTEN"
  exit 4
}
