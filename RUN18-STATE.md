# Run 18 state (2026-10-23)

The keyboard's minigames, told with the 3DS's buttons. Engine C code unchanged;
the work is in `romfs/rgss/compat.rb`, one argument in `romfs/rgss/rgss.rb` and
one line in `romfs/main.rb`.

## What is in run 18

- **The zombie phase (map 129).** Its Plants-vs-Zombies parody shoots with the
  keyboard's **W A S D** — RGSS's R, X, Y and Z. `source/main.c` reports only
  X (3DS X) and R (3DS R), so two of the four directions could never fire, and
  the two instructions on screen taught keys the console does not have
  (`instruction_shootplants`: "PRESS 'W' 'A' 'S' 'D' ON THE KEYBOARD TO SHOOT.",
  and `MG PvZ Overlay Instr`, a picture of a keyboard).
  - While the phase's overlay picture (41, `MG PvZ Overlay Instr`) is up, each
    D-pad direction also presses its key: **up W (18), left A (14), down S (15),
    right D (16)**. The arrows are still reported too, so walking/turning is
    unchanged; outside the minigame the pad is untouched.
  - This lives in `TTMControls.keys_for` (`compat.rb`), and `romfs/main.rb`
    wraps the app's key source with it. The PC runner does not use that source,
    so on the PC real WASD still works and nothing is remapped.
  - Both pictures are repainted as they load: the banner keeps its black band
    and gold capitals but says **"PRESS A DIRECTION ON THE D-PAD TO SHOOT."**;
    the keyboard becomes a D-pad cross with four white arrows (drawn from
    rectangles — Bitmap has no polygon).
- **The horse (map 29).** "Hold SPACE to gallop:" becomes **"Hold A to
  gallop:"**, with an **A** on the key cap. The event already waits on Button C,
  which is the 3DS's A, so only the art was wrong.
- **The whack-a-mole's question (map 71).** It asked "Use mouse (Recommended)" /
  "Use keyboard/gamepad"; the mouse answer transfers to map 125, where the mallet
  follows `Mouse.pos` — a pointer the 3DS reports off screen (-20,-20), so that
  route was unplayable. The choice now reads **"Use the gamepad (Recommended)"**
  / **"Show the controls"**, and both answers enter map 82 (the arrow-key game);
  the hints it shows say "Move mallet: D-pad" and "Hit: A button".

## Verified on the PC

- `pc/controls_check.rb`: **34/34** (18 new). It checks: no overlay means plain
  arrows; with the overlay up each D-pad direction also presses 18/14/15/16;
  two directions at once give two keys; erasing the overlay stops the remap;
  all three pictures are repainted (and saved to
  `pc/shots/controls_shootplants.png`, `controls_mg_pvz_overlay_instr.png`,
  `controls_instruction_horse_gallop.png`); and map 71's choice is rewritten
  with both answers transferring to 82 and the hints reworded.
- Everything else unchanged: `boot_check` 16/16, `bottom_menu_check` 18/18,
  `lang_screen_check` 13/13, `minisode_check` hs1 **13/13** and hs2 **13/13**,
  `tone_check` 19/19, `effects_check` 16/16, `fileio_check` 19/19,
  `lang_check` 47/47. `build.ps1`: **0 warnings** in `build.log`.
- `engine/ttm3ds.cia` rebuilt: **129.3 MB**, the run 17 banner still in it
  (animated CGFX, 2.80 s loop), 124 MB / 804 MHz / L2 / 1 MB stack unchanged.
  `engine/ttm3ds.3dsx` rebuilt too (24 MB), so both routes carry the fix.

## To check on the console

1. Install the new `ttm3ds.cia` (129.3 MB) with FBI over the old one.
2. **The zombie phase** (Johnny's childhood memories; Dr. Watts says "Don't come
   any closer! I got plants!"). The banner should read **"PRESS A DIRECTION ON
   THE D-PAD TO SHOOT."** and the picture beside it should be a **D-pad**, not a
   keyboard. Press Up, Left, Down and Right: each direction should throw a
   plant — that is the four directions that used to be two. Holding a direction
   walks as before.
3. **The whack-a-mole** (the arcade/carnival scene). The question should read
   **"Use the gamepad (Recommended)" / "Show the controls"**; either answer starts
   the arrow-key game, and "Show the controls" first shows **D-pad** and **A
   button**.
4. **The horse** (the race in the memories): the panel should say **"Hold A to
   gallop:"** with an A on the key; hold A to gallop.
5. Everything else should behave exactly as in run 17 (same saves, language,
   launcher and banner).

## Next

1. Run 21: the original-3DS profile (64 MB target, `envIsNew3DS()` budgets,
   vsync-accurate pacing) and the measured comparison.
2. Then the optional 60 fps mode (interpolation), New 3DS only.
3. Then layered 3D.

(Run 19 turned out to be the ending's Steam achievement hook, run 20 the launcher
and the minisodes' text: see `RUN19-STATE.md` and `RUN20-STATE.md`.)
