# Cross-build an Amiga target through WSL, mounting I: first if it is missing.
#
# WHY THIS EXISTS. The distro automounts only C: and D: -- /etc/wsl.conf here
# carries `[boot] systemd=true' and nothing else, so `[automount] mountFsTab'
# is off and I: is never mounted at boot. Every time the distro instance is
# recycled the mount is gone again, and build-amiga.sh then fails with
#
#     bash: /mnt/i/.../build/build-amiga.sh: No such file or directory
#
# which reads exactly like a missing script. Worse, an earlier failure mode had
# the share drop MID-BUILD and hang the link silently for five hours.
#
# The fix has to be in ONE wsl invocation: mounting in a separate call is not
# enough, because the distro can be recycled between two calls and take the
# mount with it. So the mount check and the build run in the same shell.
#
# `wsl --shutdown' is deliberately NOT used to make the config change take
# effect: the developer runs other projects in WSL and shutting the whole
# subsystem down would kill their work. This script makes the mount idempotent
# instead, which needs no restart of anything.
#
#   powershell -File build\wsl-build.ps1 ffe_xlattest
#   powershell -File build\wsl-build.ps1 ffe_game
param(
  [Parameter(Mandatory = $true)][string]$Target,
  [int]$TimeoutSec = 900
)
$ErrorActionPreference = "Stop"

$script = "/mnt/i/GITHUB/Amiga_Frontier_2_First_Encouters/build/build-amiga.sh"

# Root only for the mount; the build itself runs as the normal user, because
# that is who owns the toolchain's caches under $HOME.
$mount = "mountpoint -q /mnt/i || mount -t drvfs 'I:' /mnt/i"
wsl -u root -e bash -c "$mount; exec su - $(wsl -e bash -c 'id -un') -c 'bash $script $Target'"
$rc = $LASTEXITCODE

if ($rc -ne 0) { Write-Host "build-amiga.sh $Target FAILED (exit $rc)" }
exit $rc
