# Focus-free FFE run driver: waits for the splash by pixel, then delivers
# keys by dropping PROGDIR:ffe_autokey files (decimal Amiga RAW codes) that
# the platform layer's pump polls. Stops ONLY the WinUAE it started, verified
# by PID and executable path.
param(
  [string]$Config = "ffe-020-aga.uae",
  [int]   $MaxBootSec = 150,
  # A STRING, not [int[]], and split below. `powershell -File` passes every
  # argument as a string and binds them to an [int[]] by CONCATENATION: the
  # three keys 21, 1, 82 arrived as the single value 21182 and the harness
  # dropped one nonsense key ("autokey 21182 dropped"). Measured twice, two
  # runs wasted. A string parameter cannot be mangled that way.
  [string] $Keys = "21,1",         # 0x15 'y', then 0x01 '1'
  [int]   $GapSec = 15,
  [int]   $WatchSec = 90,
  [string]$AutoMouse = "",         # "x y click" dropped after the keys
  [switch]$Sweep,                  # click-grid calibration over the console
  # The Work: directory the config mounts. Must match the config's
  # filesystem2 line: ffe-020-aga-alllocal.uae uses a LOCAL copy on C:, because
  # a loaded I: NAS stopped WinUAE from ever opening a window (2026-09-25).
  [string]$WorkDir = "",
  # WinUAE runs from the I: NAS and pages its 10 MB exe in over SMB; with the
  # NAS loaded, 75 s was not enough to open a window (2026-09-25).
  [int]$WindowSec = 75
)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class W {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }

  // WHY NOT Process.MainWindowHandle (2026-08-06). It returned IntPtr.Zero for
  // 75 seconds while WinUAE was demonstrably alive and its own boot log had got
  // as far as enumerating monitors, sound devices and the filesystem charset --
  // three runs in a row, after nine that had worked. MainWindowHandle only
  // reports a window Windows considers the process's "main" one, and WinUAE's
  // display window does not always qualify. run-pc-dump.ps1 already refuses to
  // trust it for the same reason from the other direction (there it kept
  // returning the CONSOLE window of a -mconsole build).
  //
  // Enumerating the process's own visible top-level windows is the reliable
  // answer, and it is the same call the PC harness makes.
  delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Auto)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);

  public static IntPtr FindEmuWindow(uint want) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr p) {
      uint pid; GetWindowThreadProcessId(h, out pid);
      if (pid != want || !IsWindowVisible(h)) return true;
      StringBuilder sb = new StringBuilder(256);
      GetClassName(h, sb, sb.Capacity);
      // The biggest visible window the process owns wins, which is the display
      // rather than any tool window; a class match alone is brittle across
      // WinUAE versions.
      RECT r; if (!GetWindowRect(h, out r)) return true;
      if ((r.R - r.L) < 160 || (r.B - r.T) < 100) return true;
      found = h;
      return false;
    }, IntPtr.Zero);
    return found;
  }
}
"@
$ROOT = 'I:\GITHUB\Amiga_Frontier_2_First_Encouters'
$exe  = "$ROOT\tools\winuae281\winuae.exe"
$wd   = "$ROOT\tools\winuae281"
$cfg  = "$ROOT\winuae\$Config"
$WORK = "$ROOT\winuae\work"
if ($WorkDir -ne "") { $WORK = $WorkDir }   # NOT $Work: PowerShell names are case-insensitive, $Work IS $WORK

function Stop-OurWinUAE {
  param([int]$ProcId)
  $q = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcId" -ErrorAction SilentlyContinue
  if (-not $q) { return }
  if (-not $q.ExecutablePath)     { Write-Host "REFUSING to stop pid ${ProcId}: cannot read its path"; return }
  if ($q.ExecutablePath -ne $exe) { Write-Host "REFUSING to stop pid ${ProcId}: $($q.ExecutablePath) is not ours"; return }
  Stop-Process -Id $ProcId -Force -ErrorAction SilentlyContinue
}

function Grab([W+RECT]$r) {
  $bmp = New-Object System.Drawing.Bitmap ($r.R-$r.L), ($r.B-$r.T)
  $g = [System.Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L, $r.T, 0, 0, $bmp.Size); $g.Dispose()
  return $bmp
}
function PatchBrightness([System.Drawing.Bitmap]$bmp) {
  $sum = 0; $n = 0
  for ($y = 500; $y -lt 540; $y += 8) { for ($x = 200; $x -lt 300; $x += 10) {
    if ($x -lt $bmp.Width -and $y -lt $bmp.Height) {
      $c = $bmp.GetPixel($x, $y); $sum += ([int]$c.R + $c.G + $c.B); $n++
    } } }
  if ($n -eq 0) { return 255 }
  return [int]($sum / (3 * $n))
}

# SELF-HEAL THE BOOT SCRIPT: run_probe.ps1 rewrites Work:run to launch its
# probe and leaves it that way -- a whole evening was lost to "boots" that
# were silently running ffe_xlattest instead of the game. This harness now
# owns the script it depends on.
# DIRECT launch, not `Run`: the boot volume's C:Run was found broken
# (silently loads nothing) after one of the many mid-write emulator
# kills -- the game blocks the boot shell, which is fine, nothing else
# needs that CLI.
# LF-ONLY line endings: AmigaDOS treats a CR as part of the command, so a
# CRLF script makes the shell try to load ffe_game-with-a-trailing-CR --
# a silent no-op that cost a whole night: the phantom broken C:Run and
# corrupt boot.hdf diagnoses were both THIS.
$runScript = "Stack 1000000`nCD Work:`nEcho >Work:boot_marker NOLINE `"booted`"`nWork:ffe_game`n"
Set-Content -Path "$WORK\run" -Value $runScript -Encoding ascii -NoNewline
Remove-Item "$WORK\boot_marker" -ErrorAction SilentlyContinue

# The C loggers APPEND (a Guru reboot relaunches ffe_game and "w" would wipe
# the crashed run's evidence), so the harness owns log lifetime: fresh run,
# fresh files.
Remove-Item "$WORK\ffe_input.log","$WORK\ffe_autokey","$WORK\ffe_game.log",
            "$WORK\ffe_enter.log","$WORK\ffe_dispatch.log","$WORK\ffe_trace.log",
            "$WORK\ffe_frame.raw","$WORK\ffe_crash.log","$WORK\ffe_dump.log",
            "$WORK\ffe_samples.log" -ErrorAction SilentlyContinue
# ZOMBIE SWEEP: a previous harness run that died mid-way leaves ITS instance
# holding boot.hdf, and every later boot fights the locked hardfile -- two
# such orphans caused both "the environment died at night" episodes. Any
# instance whose ExecutablePath is OUR winuae.exe is ours by definition
# (the user's projects run their own copies), so stopping them is safe and
# stays inside the PID+path rule.
Get-CimInstance Win32_Process -Filter "Name='winuae.exe'" -ErrorAction SilentlyContinue | ForEach-Object {
  if ($_.ExecutablePath -eq $exe) {
    Write-Host "stopping orphaned instance $($_.ProcessId) (our path)"
    Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue
  }
}
$others = @(Get-Process winuae -ErrorAction SilentlyContinue)
if ($others.Count) { Write-Host "note: $($others.Count) other WinUAE running - leaving them alone" }

$p = Start-Process -FilePath $exe -ArgumentList '-f', $cfg, '-nodirectinput', '-norawinput' `
                   -WorkingDirectory $wd -PassThru
Write-Host "our pid=$($p.Id); waiting for the splash (pixel-detect)"
# POLL for the window instead of sleeping a fixed 25 s, and say WHICH failure it
# was (2026-08-06). The fixed sleep failed twice in a row after a spell of heavy
# disk load, printing a bare "no window" that is ambiguous between three things:
# the emulator died, it is merely slow, or it never got a window at all. The
# host has a network drive for its work directory, so "slow" is routine and a
# hard 25 s is a guess. Polling also cuts the wait on a fast boot, and the
# distinction below is the whole point -- a process that EXITED is a boot
# failure to diagnose, a process still alive after the deadline is a timeout to
# lengthen.
$h = [IntPtr]::Zero
$deadline = (Get-Date).AddSeconds($WindowSec)
while ((Get-Date) -lt $deadline) {
  Start-Sleep -Milliseconds 500
  if ($p.HasExited) {
    Write-Host "WinUAE EXITED before showing a window (code $($p.ExitCode)) -- see tools\winuae281\winuaebootlog.txt"
    exit 1
  }
  $p.Refresh()
  $h = $p.MainWindowHandle
  if ($h -eq [IntPtr]::Zero) { $h = [W]::FindEmuWindow([uint32]$p.Id) }
  if ($h -ne [IntPtr]::Zero) { break }
}
if ($h -eq [IntPtr]::Zero) {
  Write-Host "no window after ${WindowSec}s, but the process is STILL ALIVE -- a timeout, not a crash"
  Stop-OurWinUAE -ProcId $p.Id
  exit 1
}
# LAST non-primary: the first one hosts the user's Don't Sleep window, which
# occludes anything parked at its top-left and blinds the pixel-detect
$target = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Select-Object -Last 1
# 0x0001 SWP_NOSIZE | 0x0010 SWP_NOACTIVATE -- NOACTIVATE matters: without it
# the move yanks focus away from whatever the developer is typing in, and a
# focused WinUAE is also the thing that used to swallow the host mouse.
if ($target) { [W]::SetWindowPos($h, [IntPtr](-2), $target.Bounds.X + 30, $target.Bounds.Y + 30, 0, 0, 0x0011) | Out-Null }
Start-Sleep -Milliseconds 500
$r = New-Object W+RECT; [W]::GetWindowRect($h, [ref]$r) | Out-Null

# LOG-DRIVEN startup, no screen capture: window captures proved unreliable
# (black frames at night blinded the pixel-detect and killed live sessions).
# The game's own log is the truth channel: presence = the game is up; the
# ACTION lines confirm each key actually registered, and un-registered keys
# are simply re-dropped (a key pressed before its screen listens evaporates
# harmlessly).
$deadline = (Get-Date).AddSeconds($MaxBootSec)
$up = $false
while ((Get-Date) -lt $deadline) {
  if ($p.HasExited) { Write-Host "WinUAE exited early"; exit 1 }
  # ffe_dump.log, not ffe_game.log: the latter's directory entry on the I:
  # share can end up unreadable from Windows (a host-side read during a run
  # poisons it), and Test-Path then THROWS rather than returning false.
  if (Test-Path "$WORK\ffe_dump.log" -ErrorAction SilentlyContinue) { $up = $true; break }
  Start-Sleep -Seconds 5
}
if (-not $up) { Write-Host "game never started (no log)"; Stop-OurWinUAE -ProcId $p.Id; exit 1 }
Write-Host "GAME UP (log present)"
Start-Sleep -Seconds 8    # let the splash settle

# key -> the ACTION hex the logger prints when it registers. Keys with no
# entry here (function keys, for instance) are dropped once and not waited for.
$actionOf = @{ 21 = "-> 79"; 1 = "-> 31" }
$KeyList = $Keys.Split(",") | Where-Object { $_.Trim() -ne "" } | ForEach-Object { [int]$_.Trim() }
Write-Host "key sequence: $($KeyList -join ' ')"
foreach ($k in $KeyList) {
  $marker = $actionOf[$k]
  $done = $false
  for ($try = 0; $try -lt 8 -and -not $done; $try++) {
    Set-Content -Path "$WORK\ffe_autokey" -Value "$k" -Encoding ascii
    Write-Host "autokey $k dropped (try $($try+1))"
    Start-Sleep -Seconds 6
    if ($null -eq $marker) { $done = $true }
    elseif (Select-String -Path "$WORK\ffe_dump.log" -Pattern ([regex]::Escape($marker)) -Quiet -ErrorAction SilentlyContinue) {
      Write-Host "  registered ($marker)"; $done = $true
    }
  }
  if (-not $done) { Write-Host "  key $k NEVER registered" }
  Start-Sleep -Seconds $GapSec
}

if ($AutoMouse -ne "") {
  Start-Sleep -Seconds 20
  Set-Content -Path "$WORK\ffe_automouse" -Value $AutoMouse -Encoding ascii
  Write-Host "automouse '$AutoMouse' dropped"
  for ($i = 0; $i -lt 15; $i++) {
    Start-Sleep -Seconds 1
    if (-not (Test-Path "$WORK\ffe_automouse")) { Write-Host "  consumed after $($i+1)s"; break }
  }
}

if ($Sweep) {
  # click-grid calibration: wait for the new game to settle, then click each
  # grid point in turn; afterwards the ACTION lines in ffe_game.log (logged
  # with present numbers) name the points that hit a real control
  Start-Sleep -Seconds 25
  foreach ($y in @(160, 172, 184, 196)) {
    foreach ($x in @(8, 20, 32, 288, 300, 312)) {
      Set-Content -Path "$WORK\ffe_automouse" -Value "$x $y 1" -Encoding ascii
      Write-Host "sweep click ($x,$y)"
      Start-Sleep -Seconds 6
    }
  }
  Write-Host "--- sweep ACTION correlation ---"
  Get-Content "$WORK\ffe_dump.log" -ErrorAction SilentlyContinue | Select-String "ACTION"
}

for ($t = 0; $t -lt $WatchSec; $t += 15) {
  Start-Sleep -Seconds 15
  $bmp = Grab $r; $b = PatchBrightness $bmp
  # keep the last screen-copy as evidence; CopyFromScreen is reliable where
  # PrintWindow on WinUAE is sometimes black
  $bmp.Save("$ROOT\build\amiga-out\autokey_watch.png", [System.Drawing.Imaging.ImageFormat]::Png)
  $bmp.Dispose()
  Write-Host ("  +{0}s brightness={1}" -f ($t + 15), $b)
}

Stop-OurWinUAE -ProcId $p.Id
Write-Host "our pid stopped, others untouched"
if (Test-Path "$WORK\ffe_input.log") { Write-Host "--- ffe_input.log ---"; Get-Content "$WORK\ffe_input.log" }
else { Write-Host "NO ffe_input.log" }
