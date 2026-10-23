# Display-mode test run (2026-10-16): boots one of the ffe-test-*.uae machines
# with Work: = C:\temp\ffe\wtest (the release layout: ffe_game + FFE\ + ffe-prefs),
# runs the Work:run script given in -Run, takes PrintWindow shots at the given
# seconds and stops ONLY the WinUAE it started (PID + executable path).
# The window is parked on a NON-primary monitor; no input is sent to anything.
#   test_mode.ps1 -Config ffe-test-aga.uae -Run "Work:ffe-prefs VIDEO=NTSC" -Shots "60,90" -Tag ntsc
param(
  [string]$Config = "ffe-test-aga.uae",
  [string]$Run = "",
  [string]$Shots = "60,90,120",
  [string]$Tag = "t",
  [string]$WorkDir = "E:\ffe\wtest",
  [string]$Game = "Work:ffe_game",
  [int]$Boot = 0,
  [string]$OutDir = "E:\ffe\wtest-shots"
)
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System; using System.Text; using System.Runtime.InteropServices;
public class TM {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h,IntPtr a,int x,int y,int cx,int cy,uint f);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint f);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
  public delegate bool EnumProc(IntPtr h, IntPtr p);
  [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc f, IntPtr p);
  [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] static extern bool IsIconic(IntPtr h);
  static EnumProc keep;   // rooted: an unrooted delegate can be collected mid-scan
  static IntPtr found; static uint want;
  static bool Cb(IntPtr h, IntPtr p) {
    uint pid; GetWindowThreadProcessId(h, out pid);
    if (pid != want || !IsWindowVisible(h)) return true;
    if (IsIconic(h)) { found = h; return false; }   // started minimized
    RECT r; if (!GetWindowRect(h, out r)) return true;
    if ((r.R - r.L) < 160 || (r.B - r.T) < 100) return true;
    found = h; return false;
  }
  public static IntPtr Find(uint p) { want = p; found = IntPtr.Zero; keep = Cb; EnumWindows(keep, IntPtr.Zero); return found; }
}
"@
$ROOT = 'I:\GITHUB\Amiga_Frontier_2_First_Encouters'
$exe  = "E:\tools\winuae281\winuae.exe"   # local copy, off the busy I: NAS
$cfg  = "E:\GITHUB\amiffe\winuae\$Config"
function Stop-OurWinUAE([int]$ProcId) {
  $q = Get-CimInstance Win32_Process -Filter "ProcessId=$ProcId" -ErrorAction SilentlyContinue
  if (-not $q) { return }
  if (-not $q.ExecutablePath) { Write-Host "REFUSING to stop pid ${ProcId}: cannot read its path"; return }
  if ($q.ExecutablePath -ne $exe) { Write-Host "REFUSING to stop pid ${ProcId}: $($q.ExecutablePath) is not ours"; return }
  Stop-Process -Id $ProcId -Force -ErrorAction SilentlyContinue
  Write-Host "stopped our pid $ProcId"
}
New-Item -ItemType Directory -Force $OutDir | Out-Null
foreach ($f in 'stdout.txt','prefs.txt','version.txt','boot_marker') { Remove-Item "$WorkDir\$f" -ErrorAction SilentlyContinue }
$lines = @('Stack 1000000', 'CD Work:', 'Echo >Work:boot_marker NOLINE "booted"',
           'Version >Work:version.txt')
if ($Run -ne "") { $lines += ($Run + ' >Work:prefs.txt') }
$lines += ($Game + ' >Work:stdout.txt')
($lines -join "`n") + "`n" | Set-Content -NoNewline -Encoding ascii "$WorkDir\run"

# FOCUS (2026-10-16): the first runs of this script let WinUAE take the
# foreground and the developer's typing went into the emulated Amiga. The
# configs start it minimized (win32.start_minimized=true); it is shown again
# with SW_SHOWNOACTIVATE.
# ONE WinUAE on the whole PC (owner's rule, C:\temp\briefs\EMU_RULES.md): wait until no
# winuae.exe runs and C:\temp\winuae.lock is absent, then hold the lock for the run.
$lock = 'C:\temp\winuae.lock'
while ((Get-Process winuae -ErrorAction SilentlyContinue) -or (Test-Path $lock)) { Write-Host "waiting for the emulator slot"; Start-Sleep 60 }
Set-Content $lock "AmiFFE OS matrix test $Config $(Get-Date -Format s)" -Encoding ascii
$fg0 = [TM]::GetForegroundWindow()
$p = Start-Process -FilePath $exe -ArgumentList @('-nodirectinput', '-norawinput', '-f', "`"$cfg`"") -WorkingDirectory (Split-Path $exe) -PassThru
Write-Host "started pid $($p.Id) config $Config"
$sec = [System.Windows.Forms.Screen]::AllScreens | Where-Object { -not $_.Primary } | Sort-Object { $_.Bounds.X } | Select-Object -First 1
$h = [IntPtr]::Zero
$t0 = Get-Date
$times = $Shots.Split(',') | ForEach-Object { [int]$_ }
try {
  while (((Get-Date) - $t0).TotalSeconds -lt 90 -and $h -eq [IntPtr]::Zero) {
    Start-Sleep -Milliseconds 300; $h = [TM]::Find([uint32]$p.Id)
  }
  if ($h -eq [IntPtr]::Zero) { Write-Host "no window"; return }
  [void][TM]::ShowWindow($h, 4)   # SW_SHOWNOACTIVATE; never SetForegroundWindow (EMU_RULES)
  if ($sec) { [void][TM]::SetWindowPos($h, [IntPtr]::Zero, $sec.Bounds.X + 40, $sec.Bounds.Y + 40, 0, 0, 0x0015) }
  for ($i = 0; $i -lt 20; $i++) {
    Start-Sleep -Milliseconds 250
  }
  foreach ($s in $times) {
    while (((Get-Date) - $t0).TotalSeconds -lt $s) {
      # WinUAE may raise itself when the RTG screen opens (2026-10-22). Taking the
      # focus back would need SetForegroundWindow, which the shared-PC rules forbid
      # (and it is refused from a background process anyway), so keyboard input is
      # cut instead: -nodirectinput -norawinput, and ffe_nokeys in the test Work:.
      Start-Sleep -Milliseconds 250
    }
    $r = New-Object TM+RECT; [void][TM]::GetWindowRect($h, [ref]$r)
    $bmp = New-Object System.Drawing.Bitmap ($r.R - $r.L), ($r.B - $r.T)
    $g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
    [void][TM]::PrintWindow($h, $dc, 2); $g.ReleaseHdc($dc); $g.Dispose()
    $png = "$OutDir\$Tag-$s.png"; $bmp.Save($png); $bmp.Dispose()
    Write-Host "shot $png"
  }
} finally { Stop-OurWinUAE $p.Id; Start-Sleep 2; Remove-Item $lock -ErrorAction SilentlyContinue }
Start-Sleep 2
foreach ($f in 'version.txt','prefs.txt','stdout.txt') {
  if (Test-Path "$WorkDir\$f") { Write-Host "--- $f"; Get-Content "$WorkDir\$f" | Select-Object -First 40 }
}
