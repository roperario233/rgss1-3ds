# build.ps1 -- build ruby_spike.3dsx (M2 mruby spike) with the private devkitPro
# root. Run build_mruby.ps1 once first: it produces the 3DS libmruby.a.
#
# Like 3ds-app\build.ps1, it mirrors the app into a space-free scratch directory
# (devkitPro's recipes pass paths unquoted, and this project lives in
# "3ds project") and points DEVKITPRO/DEVKITARM at the private root.
#
# Usage:  powershell -ExecutionPolicy Bypass -File build.ps1

$ErrorActionPreference = 'Stop'

$src        = Split-Path -Parent $MyInvocation.MyCommand.Path
$devkitpro = if ($env:TTM_DEVKITPRO) { $env:TTM_DEVKITPRO } else { 'C:/Users/davao/devkitpro/root/opt/devkitpro' }
$msysBin    = 'C:\msys64\usr\bin'
$privateBin = Join-Path (Split-Path -Parent (Split-Path -Parent $devkitpro)) 'usr/bin'
$mrubyLib = if ($env:TTM_MRUBY_SRC) { "$env:TTM_MRUBY_SRC/build/3ds/lib/libmruby.a" } else { 'C:/Users/davao/devkitpro/src/mruby-4.0.0/build/3ds/lib/libmruby.a' }
$scratch    = 'C:\ttm3ds_ruby'

Write-Host "== checking toolchain =="
$required = @(
    "$devkitpro/devkitARM/bin/arm-none-eabi-gcc.exe",
    "$devkitpro/devkitARM/3ds_rules",
    "$devkitpro/libctru/lib/libctru.a",
    "$devkitpro/tools/bin/3dsxtool.exe",
    "$privateBin/make.exe",
    $mrubyLib
)
foreach ($f in $required) {
    if (-not (Test-Path $f)) { throw "missing: $f (libmruby.a: run build_mruby.ps1)" }
    Write-Host ("  ok  " + $f.Replace($devkitpro, '...'))
}

Write-Host "== mirroring into a space-free build dir ($scratch) =="
if (Test-Path $scratch) { Remove-Item -Recurse -Force $scratch }
New-Item -ItemType Directory -Force $scratch | Out-Null
Copy-Item "$src/source"   "$scratch/source" -Recurse
Copy-Item "$src/romfs"    "$scratch/romfs"  -Recurse
Copy-Item "$src/Makefile" "$scratch/Makefile"

$env:DEVKITPRO = $devkitpro
$env:DEVKITARM = "$devkitpro/devkitARM"
$env:PATH      = "$privateBin;$msysBin;$env:PATH"

Write-Host "== make =="
Push-Location $scratch
$ErrorActionPreference = 'Continue'      # compiler warnings arrive on stderr
try {
    & make 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath "$src/build.log"
} finally {
    Pop-Location
    $ErrorActionPreference = 'Stop'
}

$out = "$scratch/ruby_spike.3dsx"
if (-not (Test-Path $out)) {
    Write-Host ""
    throw "build failed - full log in $src\build.log"
}

Copy-Item $out "$src/ruby_spike.3dsx" -Force
$size = [math]::Round((Get-Item $out).Length / 1KB, 1)
Write-Host ""
Write-Host ("== OK: " + $src + "\ruby_spike.3dsx (" + $size + " KB) ==")
Write-Host "Copy it to the SD card as /3ds/ttm3ds_ruby/ruby_spike.3dsx"
