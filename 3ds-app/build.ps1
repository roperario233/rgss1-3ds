# build.ps1 -- build ttm3ds_test.3dsx with the private devkitPro root.
#
# Why this script exists:
#   * devkitPro's own makefile recipes pass paths unquoted, so a build directory
#     containing a space (this project lives in "3ds project") fails. The script
#     mirrors the app into a space-free scratch directory and builds there.
#   * it points DEVKITPRO/DEVKITARM at the private root instead of the default
#     C:\devkitPro, so nothing on the system is touched.
#
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1
#         powershell -ExecutionPolicy Bypass -File build.ps1 -Title "TTM Banner Test"
#
# -Title overrides the SMDH short title (the name under the HOME-menu icon), so a
# diagnostic build cannot be mistaken for the real title (run 19).

param(
    [string]$Title = ''
)

$ErrorActionPreference = 'Stop'

$src       = Split-Path -Parent $MyInvocation.MyCommand.Path
$devkitpro = if ($env:TTM_DEVKITPRO) { $env:TTM_DEVKITPRO } else { 'C:/Users/davao/devkitpro/root/opt/devkitpro' }
$msysBin = "C:/msys64/usr/bin;$devkitpro/msys2/usr/bin"
$privateBin = Join-Path (Split-Path -Parent (Split-Path -Parent $devkitpro)) 'usr/bin'
$scratch   = 'C:\ttm3ds'

Write-Host "== checking toolchain =="
$required = @(
    "$devkitpro/devkitARM/bin/arm-none-eabi-gcc.exe",
    "$devkitpro/devkitARM/3ds_rules",
    "$devkitpro/devkitARM/base_rules",
    "$devkitpro/libctru/lib/libctru.a",
    "$devkitpro/libctru/lib/libcitro2d.a",
    "$devkitpro/tools/bin/3dsxtool.exe",
    "$privateBin/make.exe"
)
foreach ($f in $required) {
    if (-not (Test-Path $f)) { throw "missing: $f" }
    Write-Host ("  ok  " + $f.Replace($devkitpro, '...'))
}

if (-not (Test-Path "$src/romfs")) {
    throw "romfs/ is missing - run 'python build_assets.py' first"
}

Write-Host "== mirroring into a space-free build dir ($scratch) =="
if (Test-Path $scratch) { Remove-Item -Recurse -Force $scratch }
New-Item -ItemType Directory -Force $scratch | Out-Null
Copy-Item "$src/source"  "$scratch/source"  -Recurse
Copy-Item "$src/romfs"   "$scratch/romfs"   -Recurse
Copy-Item "$src/Makefile" "$scratch/Makefile"

$env:DEVKITPRO = $devkitpro
$env:DEVKITARM = "$devkitpro/devkitARM"
$env:PATH      = "$privateBin;$msysBin;$env:PATH"

Write-Host "== make =="
Push-Location $scratch
try {
    if ($Title) {
        Write-Host ("  APP_TITLE=" + $Title)
        & make "APP_TITLE=$Title" 2>&1 | Tee-Object -FilePath "$src/build.log"
    } else {
        & make 2>&1 | Tee-Object -FilePath "$src/build.log"
    }
} finally {
    Pop-Location
}

$out = "$scratch/ttm3ds_test.3dsx"
if (-not (Test-Path $out)) {
    Write-Host ""
    throw "build failed - full log in $src\build.log"
}

Copy-Item $out "$src/ttm3ds_test.3dsx" -Force
$size = [math]::Round((Get-Item $out).Length / 1KB, 1)
Write-Host ""
Write-Host ("== OK: " + $src + "\ttm3ds_test.3dsx (" + $size + " KB) ==")
Write-Host "Copy it to the SD card as /3ds/ttm3ds_test/ttm3ds_test.3dsx"
