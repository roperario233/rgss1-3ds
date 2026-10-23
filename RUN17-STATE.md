# Run 17 state (2026-10-23)

The HOME-menu banner. Built and verified on the PC; the console checklist is
below. Nothing in the engine changed: this run is the title's packaging.

> **Run 19 update:** this console's HOME menu would not render the animated
> banner — a build carrying it showed a blank tile and hung, while the flat M1
> banner worked. `engine/ttm3ds.cia` now ships the **flat** banner; the animated
> one is `ttm3ds_anim.cia` (`build_cia.ps1 -AnimatedBanner`). See
> `RUN19-STATE.md`.

## What is in run 17

- **The banner is a scene, not a picture.** A 3DS banner is 256x128, but the
  HOME menu renders it as a small 3D scene through its own camera
  (`tools/pycgfx/banner-camera.gltf`: 30 degrees at z = 44.786, so 40x24 units
  at z = 0). This one is two textured plates: the game's own `0moonBG.png`,
  cropped around its moon, darkened and vignetted, behind a plaque carrying a
  recreated "To the Moon 3DS" wordmark. The plaque turns once every ten
  seconds, which is the only reason a flat PNG would not do.
- **The tools** (all in `tools/`, all runnable on their own):
  - `make_banner_assets.py` draws the two 256x128 textures (Pillow, 4x
    supersampled) and a flat `preview.png` to judge them by.
  - `make_banner_glb.py` writes the scene as a .glb itself — two meshes, three
    materials, 45-degree rotation keys (a single 0..360 key pair slerps
    nowhere) — so no modelling tool is needed.
  - `pycgfx/main.py` (already in the repo) converts that to the CGFX the menu
    renders; bannertool LZ11-compresses it into the CBMD.
  - `make_banner_tone.py` keeps 2.8 s of the game's title theme and crossfades
    the next 0.5 s back over its head with equal-power gains, so the loop is
    inaudible (wrap step 1,175, ordinary steps nearby 2,382). 2.8 s and two
    channels are the platform's rule: 3dbrew's CBMD page says a CXI banner's
    BCWAV must be 2 channels and 3 seconds or less, or the menu beeps and
    clicks or the banner fails to load.
  - `make_banner.py` runs all of it, calls bannertool, and then flips the
    CWAV's loop flag (`INFO+0x09`) — the one thing a WAV cannot say. It refuses
    a loop longer than 3 s. Result: `work/banner/banner.bnr`, 383,384 bytes.
  - `inspect_banner.py` reads a `.bnr` or a CIA's banner back: CBMD → CGFX
    offsets → LZ11 → `CANM`, then the CWAV's INFO block (`animated (CANM)`,
    rate, channels, length, loop).
- **The build.** `build_cia.ps1` copies `work/banner/banner.bnr` in when it
  exists, and builds the M1 flat banner when it does not, so a checkout without
  the game still builds. `engine/ttm3ds.cia` is now **129.3 MB** (129 MB + the
  374 KB banner); the 3dsx is unchanged, because a `.3dsx` has no banner at all.
- The title metadata is untouched: 124 MB New 3DS mode, 804 MHz, L2,
  `StackSize 0x100000`, `To the Moon 3DS`.

## Verified on the PC

- `python tools/make_banner.py`: textures 256x128, .glb 52,040 bytes, CGFX
  149,992 uncompressed / 16,056 on disk (under the 512 KB the menu accepts),
  tone 2.8 s at 32,768 Hz stereo, banner 383,384 bytes.
- `python tools/inspect_banner.py work\banner\banner.bnr`: one CGFX of 14
  language slots, **animated (CANM)**, and a 367,192-byte CWAV that **loops
  0..91,750** frames (2.80 s) — under the menu's 3 s / 2 channel rule.
- `python tools/inspect_banner.py engine\ttm3ds.cia`: the same banner found at
  0x1BD500 in the ExeFS, animated, looping, ending 383,384 bytes later — the
  bytes are identical to `work\banner\banner.bnr` (same SHA-256), i.e. the CIA
  really carries it.
- `python tools/inspect_cia.py engine\ttm3ds.cia`: 124 MB, 804 MHz, L2, 1 MB
  stack, `To the Moon 3DS` — unchanged.
- `build.ps1` (inside `build_cia.ps1`): `build.log` has **0 warnings**. No gem
  changed, so `build_mruby.ps1` was not re-run, and the PC game checks were not
  repeated: the engine's sources and data are the same bytes as run 16.

## To check on the console

1. Install `ttm3ds.cia` (**129.3 MB**) with FBI over the old one.
2. Sit on the tile in the HOME menu: the plaque should **turn** and the title
   theme should **play, looping** the 2.8 s phrase (the old banner was a still
   with a second of silence). If the menu beeps or the tile fails to draw, the
   audio length is the first suspect — but 2.8 s is inside the documented 3 s.
3. Press A: the game starts exactly as in run 16 (the launcher lists the three
   games, saves and language are untouched).
4. If the tile is still or silent, the banner did not reach the console: send
   `log.txt` (that only proves the game started) and, if you can, the CIA's
   size — the PC-side proof is the `inspect_banner.py` output above.

## Next

1. Run 21: the original-3DS profile (64 MB target, `envIsNew3DS()` budgets,
   vsync-accurate pacing) and the measured comparison.
2. Then the optional 60 fps mode (interpolation), New 3DS only.
3. Then layered 3D.

(Run 18 turned out to be the controls in the keyboard minigames, run 19 the
ending's Steam achievement hook, run 20 the launcher and the minisodes' text: see
`RUN18-STATE.md`, `RUN19-STATE.md` and `RUN20-STATE.md`.)
