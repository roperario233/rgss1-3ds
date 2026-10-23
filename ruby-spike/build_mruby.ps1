# build_mruby.ps1 -- build mruby 4.0.0 for the PC (mrbc, mruby) and the 3DS
# (libmruby.a) from build_config_3ds.rb.
#
# Usage:  powershell -ExecutionPolicy Bypass -File build_mruby.ps1 [-Clean]
#
# Everything lives in the private toolchain folder; nothing is installed:
#   C:\Users\davao\devkitpro\tools\rubyinstaller-3.4.11-1-x64   host Ruby + rake
#   C:\Users\davao\devkitpro\src\mruby-4.0.0                    mruby source + build\
# Host C compiler: Strawberry Perl's MinGW gcc (C:\Strawberry\c\bin).

param([switch]$Clean)

$ErrorActionPreference = 'Stop'

$here  = Split-Path -Parent $MyInvocation.MyCommand.Path
$engine = Join-Path (Split-Path -Parent $here) "engine"   # gems and mruby patches live there
$ruby = if ($env:TTM_RUBY) { $env:TTM_RUBY } else { 'C:/Users/davao/devkitpro/tools/rubyinstaller-3.4.11-1-x64/bin' }
$mruby = if ($env:TTM_MRUBY_SRC) { $env:TTM_MRUBY_SRC } else { 'C:/Users/davao/devkitpro/src/mruby-4.0.0' }
$gcc = if ($env:TTM_STRAWBERRY) { $env:TTM_STRAWBERRY } else { 'C:/Strawberry/c/bin' }

foreach ($p in @("$ruby\ruby.exe", "$mruby\Rakefile", "$gcc\gcc.exe")) {
    if (-not (Test-Path $p)) { throw "missing: $p" }
}

# ---- Regexp sources for gems\mruby-onig-regexp-3ds (pinned) -----------------
$srcRoot = if ($env:TTM_SRC) { $env:TTM_SRC } else { 'C:/Users/davao/devkitpro/src' }
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
# upstream fix: $1 inside a gsub block went stale after the first match
& "$ruby\ruby.exe" "$engine\gems\mruby-onig-regexp-3ds\patch_upstream.rb" $binding
if ($LASTEXITCODE -ne 0) { throw "patching mruby-onig-regexp failed" }
# object-allocation counter in mruby's gc.c (TTM.obj_allocs)
& "$ruby\ruby.exe" "$engine\patches\mruby_gc_alloc_counter.rb" $mruby
if ($LASTEXITCODE -ne 0) { throw "patching mruby gc.c failed" }
# `a != b` compiled inline instead of a call to BasicObject#!=
& "$ruby\ruby.exe" "$engine\patches\mruby_codegen_neq.rb" $mruby
if ($LASTEXITCODE -ne 0) { throw "patching mruby codegen.c failed" }

# our own gems, mirrored to a space-free path ("3ds project" has a space)
$gems = "$srcRoot\ttm-gems"
if (Test-Path $gems) { Remove-Item -Recurse -Force $gems }
Copy-Item "$engine\gems" $gems -Recurse

$env:PATH             = "$ruby;$gcc;$env:PATH"
$env:MRUBY_CONFIG     = "$here\build_config_3ds.rb"
$env:DEVKITPRO = if ($env:TTM_DEVKITPRO) { $env:TTM_DEVKITPRO } else { 'C:/Users/davao/devkitpro/root/opt/devkitpro' }
$env:TTM_GEMS         = $gems
$env:TTM_ONIG_BINDING = $binding
$env:TTM_ONIGMO       = $onigmo

# Windows PowerShell turns any stderr line from a native tool (ar prints
# "creating ...") into a terminating error under 'Stop', so relax it for rake
# and judge the build by its exit code instead.
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

$lib = "$mruby\build\3ds\lib\libmruby.a"
if (-not (Test-Path $lib)) { throw "3DS libmruby.a was not produced - see build_mruby.log" }
Write-Host ""
Write-Host ("== OK: " + $lib + " (" + [math]::Round((Get-Item $lib).Length / 1KB) + " KB) ==")
