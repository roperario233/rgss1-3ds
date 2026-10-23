# Build it yourself

From a fresh clone to a CIA, on Windows. Nothing here needs the game's files to
compile the engine; they are only needed to build the reference app that carries
the game inside it.

## 1. What you need

| Tool | Why |
|---|---|
| **devkitPro** (devkitARM, libctru, citro2d, citro3d) | the 3DS toolchain |
| **Ruby 3.4** | runs the mruby build config and the two mruby patches |
| **Strawberry Perl's gcc** | mruby's host build (`C:\Strawberry\c\bin`) |
| **make, curl, tar** | devkitPro ships make; Windows ships curl and tar |
| **Python 3 + Pillow, ffmpeg** | asset packers (`pack_audio.py` needs ffmpeg) |
| **makerom, bannertool** | the CIA: not in devkitPro's repos, see `3DS-COMPAT-MATRIX.md` |

Paths default to this machine's layout. Anywhere else, set them once:

```powershell
$env:TTM_DEVKITPRO = 'C:/devkitPro'          # has devkitARM/, libctru/, tools/
$env:TTM_STRAWBERRY = 'C:/Strawberry/c/bin'  # host gcc
$env:TTM_RUBY       = 'C:/Ruby34/bin'        # ruby.exe
```

## 2. mruby and the C gems (once, or after changing a gem)

```powershell
cd engine
powershell -ExecutionPolicy Bypass -File build_mruby.ps1
```

This fetches `mruby-onig-regexp` (pinned commit) and Onigmo 6.2.0 with `curl`/`tar`,
applies `patches/`, and builds mruby for the PC (`host/`) and the 3DS (`3ds/`).
`build_cia.ps1` refuses to package a stale `libmruby.a`, so re-run it whenever a gem
changes.

## 3. The game's files (only for a build that carries the game)

```powershell
# the extracted game, as the packers and the PC harness expect it
work\game\To the Moon.rgssad
work\game\Fonts\OpenSans.ttf      (+ the Korean/Chinese faces if you want them)
work\game\lang.dat                (optional: the translations)
# the download itself, for the packers
To.the.Moon.v2023.07.03.Incl.ALL.DLC.zip

python tools\pack_audio.py  <the game's folder or the zip>   # -> work\audio\audio.ttma
python tools\pack_romfs.py  <the zip>                        # -> work\romfs_extra\ (game + minisodes)
```

`work/` is ignored by git and stays on your machine: **the CIA you build this way
contains the game and must never be published** (see `LICENSE` and `README.md`).

## 4. Build

```powershell
cd engine
powershell -ExecutionPolicy Bypass -File build.ps1            # engine\ttm3ds.3dsx (~24 MB with audio)
powershell -ExecutionPolicy Bypass -File build_cia.ps1        # engine\ttm3ds.cia  (129 MB, game inside)
powershell -ExecutionPolicy Bypass -File build_cia.ps1 -EngineOnly   # ttm3ds-engine.cia (no game, 2 MB)
powershell -ExecutionPolicy Bypass -File build_cia.ps1 -AnimatedBanner  # opt-in; this console refused it
```

`-WithMinisodes` packs the two Sigmund games into romfs; `-NoAudio` builds the
engine alone (it then reads `SD:/3ds/ttm3ds/audio.ttma`); `-NoCopy` keeps the
scratch build out of `engine/`.

## 5. Check before you run it

```powershell
# ~250 assertions, no console needed
powershell -ExecutionPolicy Bypass -File tools\run_checks.ps1

# the repo must carry no game data
python tools\check_repo.py

# what a CIA really contains: mode, clock, banner, and whether it carries the game
python tools\inspect_cia.py engine\ttm3ds.cia
python tools\inspect_banner.py engine\ttm3ds.cia
python tools\check_cia_clean.py engine\ttm3ds-engine.cia
```

## Troubleshooting

- **“the gems are newer than libmruby.a”** — run `build_mruby.ps1`; the CIA build
  refuses to ship a stale renderer.
- **Paths with spaces** — `build.ps1` mirrors sources into `C:\ttm3ds_engine`
  because devkitPro's recipes quote nothing.
- **No sound** — the DSP needs `SD:/3ds/dspfirm.cdc`; run the **DSP1** homebrew once.
- **Black screen on launch** — 178 MB (extended memory) is known to black-screen on
  the test console; the builds use 124 MB.
- **A build that boots but shows no game** — that is the engine-only CIA: copy the
  game's folder to `SD:/3ds/ttm3ds/`. `log.txt` says what it looked for.
