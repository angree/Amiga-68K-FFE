# One bisection step for an Amiga visual regression (2026-10-15).
#   build\bisect_step.ps1 -Commit <sha>
# Checks <sha> out in the worktree C:\temp\amibis (ref\ junctioned, read-only use),
# regenerates the derived inputs (staticscan, convert_static, extract_support,
# massgen), builds with FFE_SRC pointing there, runs the intro on our WinUAE and
# reports whether the frames at intro clock 300k..500k contain any pixel.
param([Parameter(Mandatory)][string]$Commit, [int]$Lo = 300000, [int]$Hi = 500000)
$ErrorActionPreference = "Continue"
$WT = 'C:\temp\amibis'
if (-not (Test-Path $WT)) { git -C C:\temp\amiffe worktree add --detach $WT $Commit | Out-Null }
git -C $WT checkout -q --detach $Commit
# ref\jjffe is only partly tracked: COPY the untracked files out of the real ref\ (read-only use)
Get-ChildItem C:\temp\amiffe\ref\jjffe -File | Where-Object { -not (Test-Path (Join-Path "$WT\ref\jjffe" $_.Name)) } | ForEach-Object { Copy-Item $_.FullName "$WT\ref\jjffe" }
Push-Location $WT
New-Item -ItemType Directory -Force build\datamap,build\data-be,build\xlat-full | Out-Null
py tools\datamap\staticscan.py | Select-Object -Last 1
# old staticscan.py writes to a hard-coded I: path -- take its output from wherever it landed
if (-not (Test-Path build\datamap\staticscan.tsv)) { Copy-Item I:\GITHUB\Amiga_Frontier_2_First_Encouters\build\datamap\staticscan.tsv build\datamap\ }
py tools\datamap\convert_static.py | Select-Object -Last 1
New-Item -ItemType Directory -Force build\xlat-full | Out-Null
py tools\translator\massgen.py | Select-Object -Last 2
if (Test-Path tools\datamap\extract_support.py) { py tools\datamap\extract_support.py | Select-Object -Last 1 }
Pop-Location
# the CURRENT wrapper (older ones ignore FFE_SRC and build some other tree)
wsl -e bash -c "FFE_SRC=/mnt/c/temp/amibis bash /mnt/c/temp/amiffe/build/build-amiga-local.sh ffe_game 2>&1 | tail -2"
Remove-Item C:\temp\ffe\work\ffe_f8arm -ErrorAction SilentlyContinue
Remove-Item C:\temp\ffe\work\seq\* -ErrorAction SilentlyContinue   # never judge a stale series
& C:\temp\amiffe\winuae\harness\soft_run.ps1 -Keys "21" -GapSec 8 -WatchSec 30 | Out-Null
$res = py -c @"
import os
d=r'C:\temp\ffe\work\seq'; n=0; lit=0
for l in open(os.path.join(d,'index.txt')):
    p=l.split()
    c=int(p[1])
    if $Lo<=c<=$Hi and os.path.exists(os.path.join(d,p[0]+'.raw')):
        px=open(os.path.join(d,p[0]+'.raw'),'rb').read()[776:]
        n+=1; lit+= any(px)
print('frames',n,'lit',lit)
"@
"COMMIT $Commit -> $res"
