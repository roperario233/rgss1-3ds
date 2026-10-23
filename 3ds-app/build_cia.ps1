# build_cia.ps1 -- build ttm3ds_test.cia (an installable HOME-menu title).
#
# Why a CIA rather than the .3dsx:
#   * it installs to the HOME menu, so there is no Homebrew Launcher step
#   * only a title can pick its New 3DS memory mode (SystemModeExt in
#     cia/app.rsf). A .3dsx cannot ask for one at all.
#
# Usage:  powershell -ExecutionPolicy Bypass -File build_cia.ps1
#         powershell -ExecutionPolicy Bypass -File build_cia.ps1 -SystemModeExt 178MB
#
# -SystemModeExt picks the New 3DS memory mode. The default, 124MB, is the one
# verified on hardware (New 3DS XL, 2026-10-19). 178MB (extended memory) built
# fine but black-screened on that console, so it is experimental: it writes
# ttm3ds_test_178mb.cia instead. Every variant keeps the same title ID, so
# installing one replaces whichever build is on the console.
#
# The toolchain is private to this machine; nothing outside
# C:\Users\davao\devkitpro is touched.

param(
    [ValidateSet('178MB', '124MB', 'Legacy')]
    [string]$SystemModeExt = '124MB',
    # -Banner <file.bnr>: package a ready-made banner instead of this app's PNG +
    # silent WAV. Run 19 uses it to try the game's animated banner in a 2.6 MB
    # title, so the HOME menu's reaction can be seen in seconds, not after a
    # 129 MB install.
    [string]$Banner = '',
    # -OutName: the .cia name to leave in this folder (default: ttm3ds_test.cia,
    # or ttm3ds_test_<mode>.cia for the other memory modes).
    [string]$OutName = '',
    # -Title: the SMDH short title under the HOME-menu icon (distinguishes a
    # diagnostic build from the real title).
    [string]$Title = ''
)

$ErrorActionPreference = 'Stop'

$src        = Split-Path -Parent $MyInvocation.MyCommand.Path
$devkitpro = if ($env:TTM_DEVKITPRO) { $env:TTM_DEVKITPRO } else { 'C:/Users/davao/devkitpro/root/opt/devkitpro' }
$ciaTools = if ($env:TTM_CIA_TOOLS) { $env:TTM_CIA_TOOLS } else { 'C:/Users/davao/devkitpro/tools/cia' }
$makerom    = "$ciaTools\makerom\makerom.exe"
$bannertool = "$ciaTools\bannertool\windows-x86_64\bannertool.exe"
$scratch = if ($env:TTM_SCRATCH) { $env:TTM_SCRATCH } else { 'C:/ttm3ds' }

Write-Host "== checking CIA tools =="
foreach ($f in @($makerom, $bannertool)) {
    if (-not (Test-Path $f)) { throw "missing: $f" }
    Write-Host ("  ok  " + $f)
}
foreach ($f in @("$src\cia\app.rsf", "$src\cia\banner.png", "$src\cia\banner.wav")) {
    if (-not (Test-Path $f)) { throw "missing: $f" }
}

# ---- 1. build the ELF (and the .3dsx, which stays useful for HBL testing) ----
Write-Host "== building the app =="
& powershell -ExecutionPolicy Bypass -File "$src\build.ps1" -Title $Title
$elf = "$scratch\ttm3ds_test.elf"
$smdh = "$scratch\ttm3ds_test.smdh"
foreach ($f in @($elf, $smdh)) {
    if (-not (Test-Path $f)) { throw "build did not produce $f" }
}

# ---- 2. banner (top-screen art shown when the title is selected) ------------
# bannertool requires a tune as well as an image; cia/banner.wav is 1 s of
# silence so the build carries none of the game's copyrighted music. With
# -Banner, a finished .bnr (CBMD) is used as it is.
Write-Host "== building the banner =="
if ($Banner) {
    if (-not (Test-Path $Banner)) { throw "missing banner: $Banner" }
    Copy-Item $Banner "$scratch\banner.bnr" -Force
    Write-Host ("  prebuilt: " + $Banner + " (" + (Get-Item $Banner).Length + " bytes)")
} else {
    & $bannertool makebanner -i "$src\cia\banner.png" -a "$src\cia\banner.wav" -o "$scratch\banner.bnr"
}
if (-not (Test-Path "$scratch\banner.bnr")) { throw "bannertool failed" }

# ---- 3. package the CIA -----------------------------------------------------
# makerom must run inside the scratch dir: app.rsf says "RootPath: romfs", which
# is relative to the working directory.
Write-Host "== packaging the CIA (SystemModeExt $SystemModeExt) =="
Push-Location $scratch
try {
    $log = & $makerom -v -f cia -o 'ttm3ds_test.cia' -target t -exefslogo `
        -elf 'ttm3ds_test.elf' -icon 'ttm3ds_test.smdh' -banner 'banner.bnr' `
        -rsf "$src\cia\app.rsf" "-DAPP_SYSTEM_MODE_EXT=$SystemModeExt" `
        -major 0 -minor 0 -micro 0 2>&1
    $log | Out-File -FilePath "$src\build_cia.log" -Encoding utf8
    $log | Select-Object -Last 20 | Write-Host
} finally {
    Pop-Location
}

$out = "$scratch\ttm3ds_test.cia"
if (-not (Test-Path $out)) {
    Write-Host ""
    throw "CIA build failed - full log in $src\build_cia.log"
}

$name = if ($OutName) { $OutName }
        elseif ($SystemModeExt -eq '124MB') { 'ttm3ds_test.cia' }
        else { "ttm3ds_test_$($SystemModeExt.ToLower()).cia" }
Copy-Item $out "$src\$name" -Force
$size = [math]::Round((Get-Item $out).Length / 1KB, 1)
Write-Host ""
Write-Host ("== OK: " + $src + "\" + $name + " (" + $size + " KB) ==")
Write-Host ""
Write-Host "To install it once:"
Write-Host "  1. copy $name to the SD card (anywhere, e.g. /cias/)"
Write-Host "  2. open FBI (or GodMode9) and install it from that path"
Write-Host "  3. it now appears on the HOME menu; launch it from there"
Write-Host ""
if ($SystemModeExt -eq '178MB') {
    Write-Host "EXPERIMENTAL: extended memory mode black-screened on the test console."
    Write-Host "If it does start, the blue number on the bottom screen reads 178."
} else {
    Write-Host "The blue number on the bottom screen should read 124."
}
