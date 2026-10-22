# Click a standard Win32 button (by caption) inside the winuae1 process windows.
# Robust for WinUAE's host dialogs (Pixel Format Detection, GUI buttons, message boxes).
# For the Amiga emulation screen itself use coordinate clicks (click_xy.ps1) instead.
# Usage: click_button.ps1 -Text Yes [-ProcName winuae1]
param(
  [Parameter(Mandatory=$true)][string]$Text,
  [string]$ProcName = "winuae1"
)

Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public class Wb {
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  public const uint BM_CLICK = 0x00F5;
}
"@

$targetPids = (Get-Process -Name $ProcName -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
if (-not $targetPids) { Write-Output "ERROR: no process '$ProcName'"; exit 1 }

$script:hit = [IntPtr]::Zero
$norm = { param($s) ($s -replace '&','').Trim().ToLower() }
$want = & $norm $Text

$childCb = [Wb+EnumProc]{
  param($h,$l)
  $cn = New-Object System.Text.StringBuilder 256
  [void][Wb]::GetClassName($h,$cn,256)
  if ($cn.ToString() -match 'Button') {
    $tb = New-Object System.Text.StringBuilder 256
    [void][Wb]::GetWindowText($h,$tb,256)
    if ((& $norm $tb.ToString()) -eq $want) { $script:hit = $h; return $false }
  }
  return $true
}

$topCb = [Wb+EnumProc]{
  param($h,$l)
  $pid2 = 0
  [void][Wb]::GetWindowThreadProcessId($h,[ref]$pid2)
  if ($targetPids -contains [int]$pid2) {
    [void][Wb]::SetForegroundWindow($h)
    [void][Wb]::EnumChildWindows($h,$childCb,[IntPtr]::Zero)
    if ($script:hit -ne [IntPtr]::Zero) { return $false }
  }
  return $true
}

[void][Wb]::EnumWindows($topCb,[IntPtr]::Zero)

if ($script:hit -eq [IntPtr]::Zero) { Write-Output "ERROR: button '$Text' not found"; exit 2 }
[void][Wb]::SendMessage($script:hit,[Wb]::BM_CLICK,[IntPtr]::Zero,[IntPtr]::Zero)
Write-Output "OK clicked '$Text'"
