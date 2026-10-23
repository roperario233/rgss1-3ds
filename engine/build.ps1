# build.ps1 -- build ttm3ds.3dsx, the To the Moon 3DS engine app.
# Run build_mruby.ps1 first (it builds the engine's libmruby.a for the 3DS).
#
# Like the other apps' build scripts: mirrors the app into a space-free scratch
# directory (devkitPro's recipes pass paths unquoted; this project lives in
# "3ds project") and points DEVKITPRO/DEVKITARM at the private root.
#
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1 [-NoAudio] [-WithMinisodes] [-NoCopy]
#
# The game's sounds go inside the app: if ..\work\audio\audio.ttma exists
# (tools\pack_audio.py makes it from your copy of the game) it is packed into
# the 3dsx's romfs as romfs:/audio.ttma, so the SD card needs no separate
# audio file. Such a 3dsx contains the game's music: it is for your own
# console only, never for sharing (compatibility matrix, section 7). -NoAudio
# builds the engine alone; it then reads sdmc:/3ds/ttm3ds/audio.ttma if present.
#
# The two Sigmund minisodes can go inside the app too: if
# ..\work\romfs_games\ exists (tools\pack_minisodes.py makes it from the same
# copy of the game), -WithMinisodes packs its folders as romfs:/games/<name>/ so
# the launcher offers them with nothing to copy to the card. It is off by
# default because a .3dsx is loaded into RAM whole by the Homebrew Launcher and
# the two episodes are 40 MB; build_cia.ps1 turns it on, since a CIA's romfs is
# a separate partition that is read on demand. -NoCopy leaves the .3dsx in the
# scratch directory (build_cia.ps1 uses it so the small 3dsx in engine/ stays).

param([switch]$NoAudio, [switch]$WithMinisodes, [switch]$NoCopy,
      [string]$Icon = '', [string]$Title = '', [string]$Description = '')

$ErrorActionPreference = 'Stop'

$src        = Split-Path -Parent $MyInvocation.MyCommand.Path
# Portability: TTM_DEVKITPRO is the devkitPro root (the folder holding devkitARM/,
# libctru/, tools/); the rest follow from it. TTM_MRUBYBUILD is where
# build_mruby.ps1 put its trees, TTM_SCRATCH the space-free build directory. The
# defaults are this machine's private toolchain, so a clone works without setting
# anything here.
$devkitpro  = if ($env:TTM_DEVKITPRO) { $env:TTM_DEVKITPRO } else { 'C:/Users/davao/devkitpro/root/opt/devkitpro' }
$msysBin    = "C:/msys64/usr/bin;$devkitpro/msys2/usr/bin"
$privateBin = Join-Path (Split-Path -Parent (Split-Path -Parent $devkitpro)) 'usr/bin'
$mrubyLib   = if ($env:TTM_MRUBYBUILD) { "$($env:TTM_MRUBYBUILD)/3ds/lib/libmruby.a" } else { 'C:/Users/davao/devkitpro/build/engine/3ds/lib/libmruby.a' }
$scratch    = if ($env:TTM_SCRATCH) { $env:TTM_SCRATCH } else { 'C:/ttm3ds_engine' }

Write-Host "== checking toolchain =="
$required = @(
    "$devkitpro/devkitARM/bin/arm-none-eabi-gcc.exe",
    "$devkitpro/libctru/lib/libctru.a",
    "$devkitpro/libctru/lib/libcitro2d.a",
    "$devkitpro/portlibs/3ds/lib/libz.a",
    "$devkitpro/tools/bin/3dsxtool.exe",
    # make is checked once PATH is set, below (it may come from msys2 or the private root)
    $mrubyLib
)
foreach ($f in $required) {
    if (-not (Test-Path $f)) { throw "missing: $f (libmruby.a: run build_mruby.ps1; devkitPro: set TTM_DEVKITPRO)" }
}
# make is checked once PATH is set, below (it may come from msys2 or the private root)

Write-Host "== mirroring into $scratch =="
if (Test-Path $scratch) { Remove-Item -Recurse -Force $scratch }
New-Item -ItemType Directory -Force $scratch | Out-Null
Copy-Item "$src/source"   "$scratch/source" -Recurse
Copy-Item "$src/romfs"    "$scratch/romfs"  -Recurse
$pack = Join-Path (Split-Path -Parent $src) 'work/audio/audio.ttma'
if (-not $NoAudio -and (Test-Path $pack)) {
    Copy-Item $pack "$scratch/romfs/audio.ttma"
    Write-Host ("== sounds inside the app: " + $pack + " (" + [math]::Round((Get-Item $pack).Length / 1MB, 1) + " MB) ==")
} elseif (-not $NoAudio) {
    Write-Host "== no $pack (python tools\pack_audio.py <the game>): the app reads sdmc:/3ds/ttm3ds/audio.ttma =="
}
$extra = Join-Path (Split-Path -Parent $src) 'work/romfs_extra'
if ($WithMinisodes -and (Test-Path $extra)) {
    Copy-Item "$extra/*" "$scratch/romfs" -Recurse -Force
    $sz = (Get-ChildItem $extra -Recurse -File | Measure-Object -Property Length -Sum).Sum
    Write-Host ("== the game and the minisodes inside the app: " + $extra + " (" + [math]::Round($sz / 1MB, 1) + " MB) ==")
} elseif ($WithMinisodes) {
    Write-Host "== no $extra (python tools\pack_romfs.py): everything comes from the SD card =="
} else {
    Write-Host "== the game and the minisodes not packed (the CIA build packs them; -WithMinisodes does it here too) =="
}
Copy-Item "$src/Makefile" "$scratch/Makefile"
# -Icon / -Title / -Description change the SMDH metadata (the HOME menu's tile):
# the engine-only release is "RGSS1 3DS" with its own icon, the game build keeps
# this default. build_cia.ps1 -EngineOnly passes all three.
$iconSrc = if ($Icon) { $Icon } else { "$src/icon.png" }
if (-not (Test-Path $iconSrc)) { throw "missing icon: $iconSrc" }
Copy-Item $iconSrc "$scratch/icon.png"
if ($Title)       { $env:TTM_APP_TITLE = $Title }
if ($Description) { $env:TTM_APP_DESCRIPTION = $Description }

$env:DEVKITPRO = $devkitpro
$env:DEVKITARM = "$devkitpro/devkitARM"
$env:PATH      = "$privateBin;$msysBin;$env:PATH"
if (-not (Get-Command make -ErrorAction SilentlyContinue)) {
    throw "missing: make.exe (msys64, or devkitPro's msys2/usr/bin, on PATH)"
}

Write-Host "== make =="
Push-Location $scratch
$ErrorActionPreference = 'Continue'
try {
    & make 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath "$src/build.log"
} finally {
    Pop-Location
    $ErrorActionPreference = 'Stop'
}

$out = "$scratch/ttm3ds.3dsx"
if (-not (Test-Path $out)) { throw "build failed - see $src\build.log" }
$size = [math]::Round((Get-Item $out).Length / 1KB, 1)
if ($NoCopy) {
    Write-Host ""
    Write-Host ("== OK: " + $out + " (" + $size + " KB, left in the scratch dir) ==")
    return
}
Copy-Item $out "$src/ttm3ds.3dsx" -Force
Write-Host ""
Write-Host ("== OK: " + $src + "\ttm3ds.3dsx (" + $size + " KB) ==")
