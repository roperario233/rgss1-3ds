# run_checks.ps1 -- every PC check in one command (no console needed).
#
#   powershell -ExecutionPolicy Bypass -File tools\run_checks.ps1
#   powershell -ExecutionPolicy Bypass -File tools\run_checks.ps1 -All      # + the long round-trips
#
# The host mruby comes from build_mruby.ps1 (engine\build_mruby.ps1), under
# $env:TTM_DEVKITPRO\build\engine, or from -Mruby. Each check prints its own
# "N checks passed, M failed" line; this collects them and exits non-zero if any
# check failed, so CI and contributors see the same verdict.

param(
    [string]$Mruby = '',
    [switch]$All
)

$ErrorActionPreference = 'Stop'

$tools  = Split-Path -Parent $MyInvocation.MyCommand.Path
$root   = Split-Path -Parent $tools
$engine = Join-Path $root 'engine'

if (-not $Mruby) {
    $devkit = if ($env:TTM_DEVKITPRO) { $env:TTM_DEVKITPRO } else { 'C:\Users\davao\devkitpro' }
    $Mruby  = Join-Path $devkit 'build/engine/host/bin/mruby.exe'
}
if (-not (Test-Path $Mruby)) {
    throw "no host mruby at $Mruby -- run engine\build_mruby.ps1, or pass -Mruby <path>"
}
if (-not (Test-Path (Join-Path $root 'work/game/To the Moon.rgssad'))) {
    Write-Host "== note: work/game/ holds no game, so the checks that need it will fail"
}

# every *_check.rb, then minisode_check for each episode; -All adds the round-trips
$runs = @()
Get-ChildItem (Join-Path $engine 'pc') -Filter '*_check.rb' | Sort-Object Name | ForEach-Object {
    if ($_.Name -eq 'minisode_check.rb') { return }
    $runs += @{ name = $_.Name; args = @() }
}
$runs += @{ name = 'minisode_check.rb'; args = @('hs1') }
$runs += @{ name = 'minisode_check.rb'; args = @('hs2') }
if ($All) {
    foreach ($n in @('marshal_roundtrip.rb', 'save_roundtrip.rb', 'png_check.rb',
                     'audio_check.rb', 'slice_check.rb', 'tilemap_check.rb',
                     'tile_event_check.rb', 'plane_check.rb')) {
        if (Test-Path (Join-Path $engine "pc/$n")) { $runs += @{ name = $n; args = @() } }
    }
}

Push-Location $engine
$failed = 0
$rows = @()
try {
    foreach ($r in $runs) {
        $out = & $Mruby "pc/$($r.name)" @($r.args) 2>&1 | Out-String
        $code = $LASTEXITCODE
        $summary = ''
        if ($out -match '(\d+) checks passed, (\d+) failed') {
            $summary = "$($Matches[1]) passed, $($Matches[2]) failed"
        }
        $bad = ($code -ne 0) -or ($out -match 'FAIL')
        if ($bad) { $failed++ }
        $rows += [pscustomobject]@{
            Check   = if ($r.args.Count) { "$($r.name) $($r.args -join ' ')" } else { $r.name }
            Result  = if ($bad) { 'FAIL' } else { 'ok' }
            Summary = $summary
        }
        if ($bad) { Write-Host $out }
    }
} finally {
    Pop-Location
}

$rows | Format-Table -AutoSize | Out-String -Width 100 | Write-Host
Write-Host ("{0} of {1} checks passed" -f ($rows.Count - $failed), $rows.Count)
if ($failed) { exit 1 }
