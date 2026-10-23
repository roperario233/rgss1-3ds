# Run 19 state (2026-10-23)

The ending no longer ends the app. No engine C change: `romfs/rgss/compat.rb`,
`romfs/rgss/ttm_patches.rb` and a new PC check.

## What the console said

A run to the credits stopped with (from `SD:/3ds/ttm3ds/log.txt`, a **run 16**
build):

```
ERROR #<NoMethodError: undefined method '_steam_achievement_unlock' for Interpreter>
  123_MAIN:2:in _ttm_unlock_achievement
  (eval):1:in eval
  097_UMS new:801:in command_355
  ...
```

The credit roll's Script Call runs `if defined? _ttm_unlock_achievement` →
`_ttm_unlock_achievement`, and that method calls
`_steam_achievement_unlock("ACH_DEBUG_MOON")` — a Steamworks function the PC
build supplies and a 3DS does not have.

## What was really wrong

The game guards the call correctly, but **this mruby has no `defined?` keyword**:
`defined? x` is parsed as a call to a method named `defined?` whose argument is
evaluated first. Proved with the PC build:

- `defined? nope` → `NoMethodError: undefined method 'nope'` (the argument runs,
  and that is the console's error, on `123_MAIN:2`);
- `def nope; end; defined? nope` → `NoMethodError: undefined method 'defined?'`.

So the guard is itself a landmine, and defining the Steam function alone would
only move the error to `defined?`. It is the game's **only** `defined?`; every
other check in its scripts is `method_defined?` (safe).

## What is in run 19

- **A Steam-less shim** (`compat.rb`): `_steam_achievement_unlock(name = nil)`
  does nothing but names the achievement once in `log.txt`
  (`achievement: ACH_DEBUG_MOON (no Steam here)`). It also covers
  `122_Achievements#unlock`, which calls the function directly.
- **The guard in `MAIN` is dropped** (`ttm_patches.rb`, a `find`/`replace` on the
  script's own text): the call becomes unconditional, the rest of the method is
  byte-for-byte the game's.
- **Map 167's Script Call is rewritten as the map loads**
  (`compat.rb`, `TTMControls.ending_scripts`): the `if defined? … / … / end`
  block becomes the call it guarded; its two continuation lines become blank.
- Nothing gameplay-visible is lost: the achievement is Steam bookkeeping, the
  game has no in-game achievement list, and switch 999 (set by
  `Achievements.flip_gate_switch`) is not read anywhere.

## Verified on the PC

- `pc/ending_check.rb`: **15/15**. It reads the game's own
  `_ttm_unlock_achievement` out of `Data/Scripts.rxdata`, applies the real patch,
  evals the patched method and calls it; loads map 167 through `load_data` (so
  the rewrite runs) and evals the event's Script Call; loads the game's
  `Achievements` script and calls `Achievements.unlock(:finish)`. Each path runs
  without an exception and reaches the shim.
- Every earlier check unchanged: `controls_check` 34/34, `boot_check` 16/16,
  `bottom_menu_check` 18/18, `lang_screen_check` 13/13, `lang_check` 47/47,
  `minisode_check` hs1 and hs2 13/13, `tone_check` 19/19, `effects_check` 16/16,
  `fileio_check` 19/19.
- `build.ps1`: **0 warnings**. `engine/ttm3ds.cia` 129.3 MB (run 17's banner and
  run 18's controls included), 124 MB / 804 MHz / L2 / 1 MB stack unchanged;
  `engine/ttm3ds.3dsx` rebuilt (24 MB).

## To check on the console

1. Install the new `ttm3ds.cia` (129.3 MB) with FBI over the old one.
2. Continue a save near the end, or play to the credits. The credit roll should
   **start and run** instead of dropping to the HOME menu.
3. `log.txt` should show, right after `audio: bgm bgm/ending - (for river)…`:
   `achievement: ACH_DEBUG_MOON (no Steam here)` — once — and **no** `ERROR`
   line after it.
4. The credits should end in the epilogue (map 166) and the game should finish
   as it does on the PC.
5. If it stops again, the new `log.txt` ends with a different `ERROR` line (or
   stops without one): send that, and the same treatment follows.

## The blank tile and the hang (console report, after the first run 19 install)

The user installed the run 19 CIA: the HOME menu showed the title with **no
banner and no icon**, and launching it **hung** (forced shutdown). What was
checked here:

- the CIA file on the card was the full **135,562,176 bytes**, and FBI reported
  the install succeeded;
- the artifact itself is sound: every ExeFS hash matches (`.code`, `banner`
  383,384, `icon` 14,016, `logo` 8,192), the icon's pixel data is present in
  both SMDH copies, the banner parses (CBMD → LZ11 → CGFX, CWAV), and the
  header/NCCH/meta sizes agree.

The one thing that differs from the banner known to render on this console (the
M1 test CIA's) is the content of the banner itself:

| | M1 test CIA (worked) | run 19 CIA (blank + hang) |
|---|---|---|
| CGFX | 71,040 B, no `CENV`/`LUTS`/`CANM` | 149,992 B, with `CENV` + `LUTS` (lighting) + `CANM` (animation) |
| tune | 1.00 s, loop flag 0 | 2.80 s, loop flag 1 (the one-byte patch) |

The animated banner has **never** been seen on this console (run 17 was built
but the console still had run 16). It is the prime suspect.

### Built to settle it (all four verified with `inspect_cia.py` / `inspect_banner.py`)

- `engine/ttm3ds.cia` — 135,562,176 B, **animated** banner (unchanged content,
  rebuilt with the run 19 build string).
- `engine/ttm3ds_flat.cia` — 135,316,416 B, **static** M1 banner
  (`build_cia.ps1 -FlatBanner`). Same app, same romfs, same runs 17-19 code.
- `3ds-app/ttm3ds_test_anim.cia` — **2.7 MB**, the M1 test app carrying the
  animated banner (`3ds-app/build_cia.ps1 -Banner …\banner.bnr -OutName …
  -Title "TTM Banner Test"`). Installing it takes seconds, so the HOME menu's
  verdict costs nothing, and it installs under its own name — **TTM Banner
  Test** — so it cannot be mistaken for the game's tile.
- `3ds-app/ttm3ds_test.cia` — **2.6 MB**, the M1 app with its own static
  banner; this exact title has rendered on this console before.

### Also in this build: the log now names the build

`source/main.c` writes the log's first line **before `gfxInitDefault()`**, and
it says **“run 19 build”**. A hang before the graphics come up now still leaves
a dated `log.txt`; if the file is absent, the app never started at all.

### The test ladder

1. Delete the broken title in FBI, then install **`ttm3ds_test_anim.cia`**
   (2.7 MB).
   - tile animates and the app starts → the banner is fine; the problem is the
     129 MB title, go to 2;
   - tile blank / menu hangs → **the animated banner** is the culprit; use the
     flat build (3) and we revisit the CGFX later.
2. Install **`ttm3ds_flat.cia`** (129 MB). If it shows art and boots, ship it.
3. If both 129 MB builds fail while the 2.7 MB one works, the difference is the
   romfs/title size, not the banner — next step is trimming the built-in data or
   revisiting the 124 MB mode ceiling, with `log.txt` now saying how far boot got.

### Result on the console

- **Step 2 — `ttm3ds_test_anim.cia` (“TTM Banner Test”): crashes.** So the
  animated banner itself is what this console will not take — not the 129 MB
  title, not the install, not the game code.
- **Step 3 — `ttm3ds_flat.cia` (129 MB, static M1 banner): works.** The game
  boots with everything from runs 17-19 inside it.

### What ships now

- `engine/ttm3ds.cia` is the **flat** build: the same app and romfs, the M1
  static banner. This is the one to install.
- `engine/ttm3ds_anim.cia` is the animated one, kept for another console or a
  later look at the CGFX; `build_cia.ps1` only makes it with `-AnimatedBanner`.

### Still open: the music

The crash had the animation and the looped tune in the same banner, so the tune
is not yet cleared. `3ds-app/ttm3ds_test_tune.cia` (2.7 MB, tile **“TTM Tune
Test”**) is the static art with the 2.8 s **looped** tune: if its tile renders
and the music plays, the shipped build can have music too (the flat CGFX plus
`tone.wav` and the loop flag); if it crashes, the looped CWAV is the second thing
this console will not take and the flat banner stays silent.

## Seen in the same log, not the crash

At map 160 (the shuttle/bridge montage) the texture heap filled:
`gfx: texture memory full, 1516 textures not made so far; … 41 KB free`, then
recovered on the next map. It did not end the run, but if that scene shows
missing art on the console it is a separate item: shedding a scene's textures
when it is done with them.

## Next

1. Run 21: the original-3DS profile (64 MB target, `envIsNew3DS()` budgets,
   vsync-accurate pacing) and the measured comparison.
2. Then the optional 60 fps mode (interpolation), New 3DS only.
3. Then layered 3D.

(Run 20 turned out to be the graphical launcher and the minisodes' missing text:
see `RUN20-STATE.md`.)
