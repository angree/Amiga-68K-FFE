# Click at Amiga screen coords when WinUAE has the mouse TRAPPED (relative mode).
# Slams the pointer to (0,0) with large negative deltas, then steps to the target.
# Usage: click_rel.ps1 -X 342 -Y 12 [-NoClick]
param([int]$X = 0, [int]$Y = 0, [switch]$NoClick)
Add-Type @"
using System; using System.Runtime.InteropServices;
public class Rel {
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, IntPtr e);
  public const uint MOVE=0x0001, LDOWN=0x0002, LUP=0x0004;
}
"@
$p = Get-Process winuae -ErrorAction SilentlyContinue |
     Where-Object { $_.MainWindowHandle -ne [IntPtr]::Zero } | Select-Object -First 1
if (-not $p) { Write-Output "ERROR: WinUAE not running"; exit 1 }
[void][Rel]::SetForegroundWindow($p.MainWindowHandle)
Start-Sleep -Milliseconds 500

# slam to top-left corner
for ($i = 0; $i -lt 12; $i++) {
  [Rel]::mouse_event([Rel]::MOVE, -120, -120, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 25
}
Start-Sleep -Milliseconds 300

# step to target in small increments (large single deltas get scaled oddly)
$stepX = 0; $stepY = 0
while ($stepX -lt $X -or $stepY -lt $Y) {
  $dx = [Math]::Min(16, $X - $stepX); if ($dx -lt 0) { $dx = 0 }
  $dy = [Math]::Min(16, $Y - $stepY); if ($dy -lt 0) { $dy = 0 }
  [Rel]::mouse_event([Rel]::MOVE, $dx, $dy, 0, [IntPtr]::Zero)
  $stepX += $dx; $stepY += $dy
  Start-Sleep -Milliseconds 20
}
Start-Sleep -Milliseconds 400
if (-not $NoClick) {
  [Rel]::mouse_event([Rel]::LDOWN, 0, 0, 0, [IntPtr]::Zero)
  Start-Sleep -Milliseconds 140
  [Rel]::mouse_event([Rel]::LUP, 0, 0, 0, [IntPtr]::Zero)
}
Write-Output "relative-moved to amiga($X,$Y)$(if (-not $NoClick) { ' + click' })"
