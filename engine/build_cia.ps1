# build_cia.ps1 -- build ttm3ds.cia: the engine as an installable HOME-menu title.
#
# Why a CIA rather than the .3dsx:
#   * it installs to the HOME menu, so there is no Homebrew Launcher step
#   * only a title can pick its New 3DS memory mode (SystemModeExt in
#     cia/app.rsf). A .3dsx cannot ask for one at all.
#
# Usage:  powershell -ExecutionPolicy Bypass -File build_cia.ps1
#
# The renderer and the shared graphics live in the mruby gems, which
# build_mruby.ps1 compiles into libmruby.a; build.ps1 only compiles source/ and
# mirrors romfs/. If a gem is newer than libmruby.a this stops and says so.
#
# 124MB is the mode verified on hardware (New 3DS XL, 2026-10-19); 178MB
# (extended memory) black-screened on that console, so it is not offered.
#
# Like the 3dsx, a CIA built with audio.ttma in romfs contains the game's music:
# it is for this console only, never for sharing. The animated banner (built by
# tools/make_banner.py) carries the title theme for the same reason.
#
# The toolchain is private to this machine; nothing outside
# C:\Users\davao\devkitpro is touched.

param(
    [ValidateSet('124MB', 'Legacy')]
    [string]$SystemModeExt = '124MB',
    # -AnimatedBanner: the pycgfx CGFX + 2.8 s looped tune, written as
    # ttm3ds_anim.cia. It is opt-in because it is the one banner this console
    # would not take: installed, the HOME menu showed a blank tile and hung
    # (run 19), while the flat M1 banner below worked. See RUN19-STATE.md.
    [switch]$AnimatedBanner
)

$ErrorActionPreference = 'Stop'

$src        = Split-Path -Parent $MyInvocation.MyCommand.Path
$root       = Split-Path -Parent $src
$devkitpro  = if ($env:TTM_DEVKITPRO) { $env:TTM_DEVKITPRO } else { 'C:/Users/davao/devkitpro/root/opt/devkitpro' }
$ciaTools   = if ($env:TTM_CIA_TOOLS) { $env:TTM_CIA_TOOLS } else { 'C:/Users/davao/devkitpro/tools/cia' }
$makerom    = "$ciaTools/makerom/makerom.exe"
$bannertool = "$ciaTools/bannertool/windows-x86_64/bannertool.exe"
$scratch    = if ($env:TTM_SCRATCH) { $env:TTM_SCRATCH } else { 'C:/ttm3ds_engine' }   # space-free build dir (build.ps1 uses it too)
$mrubyLib   = if ($env:TTM_MRUBYBUILD) { "$($env:TTM_MRUBYBUILD)/3ds/lib/libmruby.a" } else { 'C:/Users/davao/devkitpro/build/engine/3ds/lib/libmruby.a' }
$bannerPng  = "$root\3ds-app\cia\banner.png"   # fallback: flat art, no game data
$bannerWav  = "$root\3ds-app\cia\banner.wav"   # fallback: 1 s of silence
$bannerBnr  = "$root\work\banner\banner.bnr"   # tools/make_banner.py writes this

# The finished banner (a turning plaque over the game's night sky, with the title
# theme in a seamless loop) is built once by tools/make_banner.py, because it
# needs the game's own art and music. Without it -- a fresh checkout, or a build
# on a machine without the game -- the flat banner from milestone M1 stands in.
$customBanner = (Test-Path $bannerBnr) -and $AnimatedBanner

Write-Host "== checking the tools and the assets =="
foreach ($f in @($makerom, $bannertool, "$src\cia\app.rsf")) {
    if (-not (Test-Path $f)) { throw "missing: $f" }
    Write-Host ("  ok  " + $f)
}
if ($customBanner) {
    Write-Host ("  ok  " + $bannerBnr + "  (the animated banner)")
} else {
    foreach ($f in @($bannerPng, $bannerWav)) {
        if (-not (Test-Path $f)) { throw "missing: $f" }
        Write-Host ("  ok  " + $f + "  (the flat fallback banner)")
    }
}

# ---- 0. is libmruby.a newer than the gems? -----------------------------------
# backend_c3d.c, graphics.c and the other gems are compiled by build_mruby.ps1,
# not by build.ps1: packaging without it would ship the previous renderer.
if (-not (Test-Path $mrubyLib)) { throw "missing $mrubyLib -- run build_mruby.ps1 first" }
$newestGem = Get-ChildItem "$src\gems" -Recurse -File -Include *.c,*.h,*.rake |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($newestGem -and (Get-Item $mrubyLib).LastWriteTime -lt $newestGem.LastWriteTime) {
    throw ("the gems are newer than libmruby.a (" + $newestGem.FullName +
           ") -- run build_mruby.ps1 first")
}

# ---- 1. the ELF, the SMDH and romfs/ (with audio.ttma and the minisodes) ------
# -WithMinisodes: the two Sigmund minisodes go in this app's romfs (40 MB), so
# the console needs nothing but the CIA. -NoCopy keeps the small 3dsx in engine/
# as it is -- a .3dsx is loaded into RAM whole, so the episodes do not go there.
Write-Host "== building the app (build.ps1 -WithMinisodes) =="
& powershell -ExecutionPolicy Bypass -File "$src\build.ps1" -WithMinisodes -NoCopy
$elf  = "$scratch\ttm3ds.elf"
$smdh = "$scratch\ttm3ds.smdh"
foreach ($f in @($elf, $smdh, "$scratch\romfs")) {
    if (-not (Test-Path $f)) { throw "build.ps1 did not produce $f" }
}

# ---- 2. the banner (top-screen art when the title is selected) --------------
# tools/make_banner.py has already run bannertool and the tune's loop patch, so
# this only copies the finished .bnr in. The fallback builds the M1 flat banner:
# bannertool wants a tune as well as an image, and banner.wav is 1 s of silence,
# so that route carries none of the game's music.
Write-Host "== the banner =="
if ($customBanner) {
    Copy-Item $bannerBnr "$scratch\banner.bnr" -Force
    Write-Host "  animated CGFX banner, title theme looped (tools/make_banner.py)"
} else {
    & $bannertool makebanner -i $bannerPng -a $bannerWav -o "$scratch\banner.bnr"
    Write-Host "  flat banner, silent (run tools/make_banner.py for the animated one)"
}
if (-not (Test-Path "$scratch\banner.bnr")) { throw "bannertool failed" }

# ---- 3. package the CIA -----------------------------------------------------
# makerom must run inside the scratch dir: cia/app.rsf says "RootPath: romfs",
# which is relative to the working directory.
Write-Host "== packaging the CIA (SystemModeExt $SystemModeExt) =="
Push-Location $scratch
try {
    $log = & $makerom -v -f cia -o 'ttm3ds.cia' -target t -exefslogo `
        -elf 'ttm3ds.elf' -icon 'ttm3ds.smdh' -banner 'banner.bnr' `
        -rsf "$src\cia\app.rsf" "-DAPP_SYSTEM_MODE_EXT=$SystemModeExt" `
        -major 0 -minor 0 -micro 0 2>&1
    $log | Out-File -FilePath "$src\build_cia.log" -Encoding utf8
    $log | Select-Object -Last 20 | Write-Host
} finally {
    Pop-Location
}

$out = "$scratch\ttm3ds.cia"
if (-not (Test-Path $out)) {
    Write-Host ""
    throw "CIA build failed - full log in $src\build_cia.log"
}

$name = if ($SystemModeExt -eq 'Legacy') { 'ttm3ds_legacy.cia' }
        elseif ($AnimatedBanner) { 'ttm3ds_anim.cia' }
        else { 'ttm3ds.cia' }
Copy-Item $out "$src\$name" -Force
$size = [math]::Round((Get-Item $out).Length / 1MB, 1)
Write-Host ""
Write-Host ("== OK: " + $src + "\" + $name + " (" + $size + " MB) ==")
Write-Host ""
Write-Host "To install it once:"
Write-Host "  1. copy $name to the SD card (anywhere, e.g. /cias/)"
Write-Host "  2. open FBI (or GodMode9) and install it from that path"
Write-Host "  3. it appears on the HOME menu; launch it from there"
Write-Host ""
Write-Host "The game's files stay at SD:/3ds/ttm3ds/ (the same folder the 3dsx uses),"
Write-Host "and Exit Game > Shutdown returns to the HOME menu."
Write-Host ""
Write-Host "This CIA carries the game's music (romfs:/audio.ttma and the banner's"
Write-Host "title theme): it is for this console only, never for sharing."
if ($SystemModeExt -eq 'Legacy') {
    Write-Host ""
    Write-Host "Legacy mode: the New 3DS 124MB mode is not requested."
}
