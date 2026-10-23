# Run 20 state (2026-10-24)

The launcher gets a face, and the minisodes get their text. `source/main.c` and
`tools/pack_romfs.py`; no gem or game-script change.

## The two reports

1. **No dialogue text in either Sigmund minisode.** The message windows and name
   tabs appeared, the letters did not. Cause: the minisodes are whole games with
   **no `Fonts/` folder**, so `boot.rb` loads the shared face from
   `$ttm_font_file` — and the app pointed that at `SD:/3ds/ttm3ds/Fonts/OpenSans.ttf`.
   A CIA-only install has nothing on the card; the fonts travel inside the title
   at `romfs:/game/Fonts/`. `Font.ttm_face` stayed nil, so every `draw_text` drew
   no glyphs.
   - Fix: `shared_font()` returns the first that exists —
     `romfs:/game/Fonts/OpenSans.ttf`, then `APP_DIR "Fonts/OpenSans.ttf"` — and
     boot's `font:` line names which one it used.
   - The text was never missing: `pc/minisode_check.rb` reaches the first lines
     of both episodes and asserts the text and that it was drawn (13/13 each),
     with `pc/shots/hs1_dialogue.png` and `hs2_dialogue.png` showing them.
2. **The launcher felt bland** (and one entry read `SigCorp Minisode 2.rgssad`).
   - `tools/pack_romfs.py` copied only a file named `Game.ini`; Minisode 2 ships
     `SigCorp Minisode 2.ini`. Every `*.ini` beside an archive is packed now, and
     `add_game` drops a `.rgssad` suffix from its last-resort title.
   - The list moved from `printf` on libctru's 8x8 console to a citro2d screen.

## What is in run 20

- **The launcher** (`draw_launcher`, citro2d, both screens):
  - bottom 320x240: navy gradient, "CHOOSE A GAME", one rounded row per game
    (42 px, radius 8); the selected row filled in the game's gold with dark text
    and a small triangle; others `#1b2433` with off-white; footer
    "A select · START+SELECT quit";
  - top 400x240: "TO THE MOON 3DS" with a gold rule, a drawn crescent, the chosen
    game's name large (scaled to fit) and its subtitle — "the main game" or
    "a Sigmund minisode";
  - input unchanged: D-pad + A, tap a row (mapped from the new row geometry),
    START+SELECT quits; the last choice still comes from `last-game.txt`.
  - if the two render targets or the text buffer cannot be made (no VRAM), the
    launcher falls back to the old console list by itself and says so in
    `log.txt`, so a VRAM problem can never leave a blank screen.
- **The console is on demand.** It is no longer created at startup (the two
  cannot share the bottom screen); `ensure_console()` makes it for the
  missing-files, `romfs:/main.rb missing` and error screens, and the launcher's
  render targets are deleted before the game starts. `log.txt` records
  `ui: launcher targets up/down, N KB VRAM free` so a VRAM problem is visible.
- **Font:** `shared_font()` as above. `log.txt` for a minisode should now show
  `font: romfs:/game/Fonts/OpenSans.ttf (this game carries no Fonts/ of its own)`.

## Verified on the PC

- `pc/boot_check.rb` **16/16**; `pc/minisode_check.rb` hs1 **13/13** and hs2
  **13/13** (the dialogue assertions included); `pc/controls_check.rb` **34/34**.
- `build.ps1`: **0 warnings**. `tools/pack_romfs.py`: 104.0 MB, and
  `romfs_extra/games/` now holds `Sigmund Holiday Special 1/Game.ini`,
  `Siggy - Holiday Special.ini` and `Sigmund Minisode 2/SigCorp Minisode 2.ini`.
- `engine/ttm3ds.cia`: **135,328,704 bytes**, flat banner (run 19's default),
  containing the new launcher and the shared-font path; `engine/ttm3ds.3dsx`
  25 MB. `inspect_cia.py`: 124 MB mode, 804 MHz, L2, 1 MB stack, unchanged.

## To check on the console

1. Install `engine/ttm3ds.cia` with FBI (it replaces the current title; saves and
   `save/lang` on the card are untouched).
2. **Launcher**: three rows on the bottom — `To the Moon`,
   `SigCorp - Holiday Special`, `SigCorp Minisode 2` — with the selected one in
   gold and a triangle, and the top screen showing the same title large with its
   subtitle. D-pad moves, A launches, tapping a row launches, START+SELECT quits.
3. **Minisode text**: start *SigCorp Minisode 2*; the intro's dialogue must show
   its name tab and typed text (it begins ~20 s in, after the snowy exterior).
   Then *SigCorp - Holiday Special* (its first line is ~80 s in).
4. **`log.txt`**: for a minisode, `font: romfs:/game/Fonts/OpenSans.ttf (this game
   carries no Fonts/ of its own)` and no `text will not show` warning; the
   `launcher: <title>` line should read `SigCorp Minisode 2`.
5. If the launcher were ever blank or garbled, it falls back to the console list
   on its own and `log.txt`'s `ui:` lines say why; send those and the first line.

## Next

1. Run 21: the original-3DS profile (64 MB target, `envIsNew3DS()` budgets,
   vsync-accurate pacing) and the measured comparison.
2. Then the optional 60 fps mode (interpolation), New 3DS only.
3. Then layered 3D.
