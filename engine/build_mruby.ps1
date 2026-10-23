# build_mruby.ps1 -- build mruby 4.0.0 for the To the Moon 3DS engine: the PC
# build (mruby.exe, used to run the game on the PC with the software renderer)
# and the 3DS build (libmruby.a), from mruby_config.rb.
#
# Usage:  powershell -ExecutionPolicy Bypass -File build_mruby.ps1 [-Clean]
#
# Same private toolchain as ruby-spike/build_mruby.ps1, separate build dir:
#   C:\Users\davao\devkitpro\build\engine\{host,3ds}
# Gems come from engine/gems, mirrored to a space-free path first.

param([switch]$Clean)

$ErrorActionPreference = 'Stop'

$here  = Split-Path -Parent $MyInvocation.MyCommand.Path
$ruby  = if ($env:TTM_RUBY) { $env:TTM_RUBY } else { 'C:/Users/davao/devkitpro/tools/rubyinstaller-3.4.11-1-x64/bin' }
$mruby = if ($env:TTM_MRUBY_SRC) { $env:TTM_MRUBY_SRC } else { 'C:/Users/davao/devkitpro/src/mruby-4.0.0' }
$gcc   = if ($env:TTM_STRAWBERRY) { $env:TTM_STRAWBERRY } else { 'C:/Strawberry/c/bin' }
$build = if ($env:TTM_MRUBYBUILD) { $env:TTM_MRUBYBUILD } else { 'C:/Users/davao/devkitpro/build/engine' }

foreach ($p in @("$ruby\ruby.exe", "$mruby\Rakefile", "$gcc\gcc.exe")) {
    if (-not (Test-Path $p)) { throw "missing: $p" }
}

# ---- upstream sources and patches (shared with the M2 spike) -------------------
$srcRoot    = if ($env:TTM_SRC) { $env:TTM_SRC } else { 'C:/Users/davao/devkitpro/src' }
$bindingSha = '4962d7eb42de079f6b3faec4b40560eb2941eeec'
$binding    = "$srcRoot\mruby-onig-regexp-$($bindingSha.Substring(0, 8))"
$onigmo     = "$srcRoot\onigmo\onigmo-6.2.0"
if (-not (Test-Path $binding)) {
    Write-Host "== fetching mruby-onig-regexp $bindingSha =="
    $tgz = "$srcRoot\mruby-onig-regexp-$bindingSha.tar.gz"
    & curl.exe -sSL -o $tgz "https://github.com/mattn/mruby-onig-regexp/archive/$bindingSha.tar.gz"
    & tar.exe -xzf $tgz -C $srcRoot
    Rename-Item "$srcRoot\mruby-onig-regexp-$bindingSha" $binding
}
if (-not (Test-Path $onigmo)) {
    New-Item -ItemType Directory -Force "$srcRoot\onigmo" | Out-Null
    & tar.exe -xzf "$binding\onigmo-6.2.0.tar.gz" -C "$srcRoot\onigmo"
}
& "$ruby\ruby.exe" "$here\gems\mruby-onig-regexp-3ds\patch_upstream.rb" $binding
if ($LASTEXITCODE -ne 0) { throw "patching mruby-onig-regexp failed" }
& "$ruby\ruby.exe" "$here\patches\mruby_gc_alloc_counter.rb" $mruby
if ($LASTEXITCODE -ne 0) { throw "patching mruby gc.c failed" }
& "$ruby\ruby.exe" "$here\patches\mruby_codegen_neq.rb" $mruby
if ($LASTEXITCODE -ne 0) { throw "patching mruby codegen.c failed" }

# gems, mirrored to a space-free path ("3ds project" has a space)
$gems = "$srcRoot\ttm-engine-gems"
if (Test-Path $gems) { Remove-Item -Recurse -Force $gems }
Copy-Item "$here\gems" $gems -Recurse

$env:PATH             = "$ruby;$gcc;$env:PATH"
$env:MRUBY_CONFIG     = "$here\mruby_config.rb"
$env:MRUBY_BUILD_DIR  = $build
$env:DEVKITPRO        = if ($env:TTM_DEVKITPRO) { $env:TTM_DEVKITPRO } else { 'C:/Users/davao/devkitpro/root/opt/devkitpro' }
$env:TTM_GEMS         = $gems
$env:TTM_ONIG_BINDING = $binding
$env:TTM_ONIGMO       = $onigmo

Push-Location $mruby
$ErrorActionPreference = 'Continue'
try {
    if ($Clean) { & "$ruby\ruby.exe" -S rake deep_clean }
    & "$ruby\ruby.exe" -S rake -j4 all 2>&1 | ForEach-Object { "$_" } |
        Tee-Object -FilePath "$here\build_mruby.log"
    $rc = $LASTEXITCODE
} finally {
    Pop-Location
    $ErrorActionPreference = 'Stop'
}
if ($rc -ne 0) { throw "rake failed (exit $rc) - see build_mruby.log" }

$lib = "$build\3ds\lib\libmruby.a"
if (-not (Test-Path $lib)) { throw "3DS libmruby.a was not produced - see build_mruby.log" }
Write-Host ""
Write-Host ("== OK: $build\host\bin\mruby.exe and " + $lib + " ==")
