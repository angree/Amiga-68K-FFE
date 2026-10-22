# Build, PROVE the binary is the one just built, then run.
#
# Why this exists: I: is an SMB share and its attribute cache lies. Three probe
# rounds in the 2026-08-01 session were read off a STALE binary -- the build
# reported "deployed" while the emulator still loaded the previous one, and the
# missing log lines looked like a code bug. The fix is not to trust timestamps
# at all: the caller names a MARKER string that the new build must contain, and
# the run only starts once that marker is found inside winuae\work\ffe_game.
#
#   powershell -File winuae\harness\build_run.ps1 -Marker YZTN -WatchSec 220
param(
  [Parameter(Mandatory=$true)][string]$Marker,
  [int]$WatchSec = 220,
  # A STRING, passed straight through to autokey_run.ps1, which splits it.
  # NOT [int[]]: `powershell -File` binds multiple string arguments to an
  # [int[]] by CONCATENATION, so "-Keys 21,1,82" became the single key 21182
  # and two whole runs delivered nothing. Measured, not guessed.
  [string]$Keys = "21,1",
  [int]$GapSec = 15,
  [string]$Extra = ""
)
$ROOT = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent

# The WSL side of the same share drops under load ("Transport endpoint is not
# connected"); remounting is cheap and harmless when it is already mounted.
wsl -u root -e bash -c "mount -t drvfs I: /mnt/i 2>/dev/null" | Out-Null

# The share drops under a long build ("Transport endpoint is not connected",
# "can't create ...: No such file or directory"). That is an I/O fault, not a
# code fault, so remount and try again rather than reporting a build error.
$dep = "0"
for ($try = 1; $try -le 3 -and "$dep".Trim() -ne "1"; $try++) {
  Write-Host "building (attempt $try)..."
  $dep = wsl -e bash -c "cd /mnt/i/GITHUB/Amiga_Frontier_2_First_Encouters && bash build/build-amiga.sh ffe_game > /tmp/b.log 2>&1; grep -c deployed /tmp/b.log"
  if ("$dep".Trim() -ne "1") {
    Write-Host "  build did not deploy; errors:"
    wsl -e bash -c "grep -nE 'error|Error|Transport|No such file' /tmp/b.log | head -8"
    wsl -u root -e bash -c "umount -l /mnt/i 2>/dev/null; mount -t drvfs I: /mnt/i 2>/dev/null" | Out-Null
  }
}
if ("$dep".Trim() -ne "1") { Write-Host "BUILD FAILED after 3 attempts"; exit 1 }

$bytes = [IO.File]::ReadAllBytes("$ROOT\winuae\work\ffe_game")
$text  = [Text.Encoding]::ASCII.GetString($bytes)
if (-not $text.Contains($Marker)) {
  Write-Host "STALE BINARY: marker '$Marker' is not in winuae\work\ffe_game -- refusing to run"
  exit 2
}
Write-Host "marker '$Marker' present, binary is fresh ($($bytes.Length) bytes)"

# One string argument each -- see the -Keys comment in the param block.
$argsList = @("-File", "$ROOT\winuae\harness\autokey_run.ps1", "-WatchSec", "$WatchSec",
              "-GapSec", "$GapSec", "-Keys", "$Keys")
if ($Extra -ne "") { $argsList += $Extra.Split(" ") }
& powershell @argsList
