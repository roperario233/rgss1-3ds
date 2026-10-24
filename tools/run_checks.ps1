# run_checks.ps1 -- every PC check in one command (no console needed).
#
#   powershell -ExecutionPolicy Bypass -File tools\run_checks.ps1
#   powershell -ExecutionPolicy Bypass -File tools\run_checks.ps1 -All      # + the round-trips
#
# The host mruby comes from engine\build_mruby.ps1, under $env:TTM_DEVKITPRO\build\engine,
# or from -Mruby.
#
# The checks replay a real game: each opens the archive, boots the game's own scripts
# and compares what the engine draws against it. This repository carries no game data
# (LICENSE), so they need your copy -- work/game/, or one line in work/game_dir.txt
# saying where it is (docs/PORTING.md, step 1). With no game there is nothing to
# replay, so this says so and exits 2 instead of printing twenty failures. CI runs
# tools/check_repo.py, which needs no game.
#
# Each check prints its own "N checks passed, M failed" line and a "  FAIL  name" line
# per failed assertion; this collects them and exits non-zero if any check failed.

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

# the same two files pc/game_dir.rb reads, resolved here so the run can start or refuse
# (a relative path in game_dir.txt is read from engine/, where the checks run)
$gameDir = Join-Path $root 'work/game'
$txt = Join-Path $root 'work/game_dir.txt'
if (Test-Path $txt) {
    $line = Get-Content $txt -TotalCount 1
    if ($line -and $line.Trim()) {
        $p = $line.Trim()
        if (-not [System.IO.Path]::IsPathRooted($p)) { $p = Join-Path $engine $p }
        $gameDir = $p
    }
}
if (-not (Get-ChildItem -Path $gameDir -Filter *.rgssad -ErrorAction SilentlyContinue)) {
    Write-Host ''
    Write-Host "no game in $gameDir"
    Write-Host 'These checks replay a real game; this repository carries none (see LICENSE).'
    Write-Host 'Put your own copy in work/game/, or write its folder on one line in'
    Write-Host 'work/game_dir.txt -- read from engine/, so ../work/game or an absolute'
    Write-Host 'path (docs/PORTING.md, step 1).'
    exit 2
}

# a check whose fixture is missing is skipped, not failed, with the command that makes it
$needs = @{
    'audio_check.rb' = @('work/audio/audio.ttma', 'work/audio/refs.txt')
    'png_check.rb'   = @('work/rgssad_manifest.json')
}
$hint = @{
    'audio_check.rb' = 'python tools\pack_audio.py <the game>  (refs.txt: see audio_check.rb)'
    'png_check.rb'   = 'python tools\rgssad_extract.py extract --no-write --manifest work\rgssad_manifest.json <the game''s .rgssad>'
}

# every *_check.rb, then minisode_check for each episode that is actually present;
# -All adds the round-trips, which replay a save rather than a whole game
$runs = @()
Get-ChildItem (Join-Path $engine 'pc') -Filter '*_check.rb' | Sort-Object Name | ForEach-Object {
    if ($_.Name -eq 'minisode_check.rb') { return }
    $runs += @{ name = $_.Name; args = @() }
}
foreach ($g in @(@('hs1', 'work/Sigmund Holiday Special 1'), @('hs2', 'work/Sigmund Holiday Special 2'))) {
    if (Get-ChildItem -Path (Join-Path $root $g[1]) -Filter *.rgssad -ErrorAction SilentlyContinue) {
        $runs += @{ name = 'minisode_check.rb'; args = @($g[0]) }
    }
}
if ($All) {
    foreach ($n in @('marshal_roundtrip.rb', 'save_roundtrip.rb')) {
        if (Test-Path (Join-Path $engine "pc/$n")) { $runs += @{ name = $n; args = @() } }
    }
}

Push-Location $engine
$failed = 0
$skipped = 0
$rows = @()
# mruby writes progress and expected traces to stderr (several checks assert on an
# error path), so stderr must not abort the run: the verdict is the exit code, a
# "  FAIL  " line, or a non-zero count in the summary -- never the word "failed" in
# "0 failed", so the match is case-sensitive and anchored.
$ErrorActionPreference = 'Continue'
try {
    foreach ($r in $runs) {
        $label = if ($r.args.Count) { "$($r.name) $($r.args -join ' ')" } else { $r.name }
        $missing = @()
        if ($needs.ContainsKey($r.name)) {
            foreach ($f in $needs[$r.name]) {
                if (-not (Test-Path (Join-Path $root $f))) { $missing += $f }
            }
        }
        if ($missing.Count) {
            $skipped++
            $rows += [pscustomobject]@{ Check = $label; Result = 'skip'
                                        Summary = "needs $($missing -join ', ')" }
            Write-Host ("skip  {0}: no {1} -- {2}" -f $r.name, ($missing -join ', '), $hint[$r.name])
            continue
        }
        $out = & $Mruby "pc/$($r.name)" @($r.args) 2>&1 | Out-String
        $code = $LASTEXITCODE
        $summary = ''
        if ($out -cmatch '(\d+) (?:checks )?passed, (\d+) failed') {
            $summary = "$($Matches[1]) passed, $($Matches[2]) failed"
        }
        $bad = ($code -ne 0) -or ($out -cmatch '  FAIL  ') -or ($summary -cmatch ', [1-9]\d* failed')
        if ($bad) { $failed++ }
        $rows += [pscustomobject]@{
            Check   = $label
            Result  = if ($bad) { 'FAIL' } else { 'ok' }
            Summary = $summary
        }
        if ($bad) { Write-Host $out }
    }
} finally {
    Pop-Location
}

$rows | Format-Table -AutoSize | Out-String -Width 100 | Write-Host
Write-Host ("{0} passed, {1} failed, {2} skipped of {3}" -f ($rows.Count - $failed - $skipped), $failed, $skipped, $rows.Count)
if ($failed) { exit 1 }

