# Run 16 state (2026-10-23)

Built and verified on the PC; the console checklist is below.

## What is in run 16

- **The Holiday Specials.** The download's two `Sigmund Holiday Special`
  folders are whole RGSS1 games of their own (their own `.rgssad`, scripts,
  maps, and `Audio/` as loose Oggs). The engine now runs any of them the way it
  runs the main game, and they needed five small compatibility additions, all
  found by running them (`pc/minisode_check.rb`):
  - Ruby 1.8 source that mruby refuses (arrow-less hash literals, a space before
    a call's arguments): a script that will not compile is rewritten and
    compiled again, logged `legacy syntax: …` (3 scripts in each minisode).
  - `ENV['AV_APPDATA']` and `Hash#index`, which mkxp/Ruby 1.8 had
    (`compat.rb`).
  - The older mouse script's `user32` cursor calls, replaced with
    `Input.mouse_x/y` exactly as the main game's own later build does
    (`ttm_patches.rb`, "Mouse 1").
  - Their `Caterpillar` reading the party's leader with an empty starting party
    — guarded the way the main game's later build guards it.
  - No `Fonts/` folder: the app's shared `Fonts/OpenSans.ttf` stands in, logged
    `font: … (this game carries no Fonts/ of its own)`.
- **The launcher** (`source/main.c`). The app scans `SD:/3ds/ttm3ds/`, one
  folder below it, and `romfs:/games/` for `*.rgssad`, and lists them on the
  bottom screen when there is more than one: tap a title or D-pad + A,
  `START + SELECT` quits, and the last choice is remembered in `last-game.txt`.
  One game on the card boots straight in as before. The main game keeps
  `SD:/3ds/ttm3ds/` for its saves; a minisode writes in
  `SD:/3ds/ttm3ds/games/<its folder>/`.
- **Inside the app.** `tools/pack_romfs.py` writes `work/romfs_extra/` and
  `build_cia.ps1` packs it: `romfs:/game/` (the main game's archive, `lang.dat`,
  `Game.ini` and the three fonts) and `romfs:/games/` (both episodes — archives,
  `Game.ini`, and only the sounds the main audio pack lacks). **The CIA (129 MB)
  is the only file the console needs**; saves stay in `SD:/3ds/ttm3ds/save/`.
  The 3dsx (23.9 MB) carries none of it (a `.3dsx` is loaded into RAM whole), so
  that route reads everything from the card, where a copy always wins.

## Verified on the PC

- `pc/minisode_check.rb hs1`: **10/10** — archive opens, all 114 scripts load,
  reaches map 29 and draws it, 7 screenshots, no bottom-screen menu, English, the
  shared font stood in, `Mouse.pos` returns `-20,-20` not nil, writes confined to
  `work/pc_hs1/`, both compatibility patches applied.
- `pc/minisode_check.rb hs2`: **10/10**, same checks (146 drawables on map 29).
- Shots: `pc/shots/hs1_0060.png` (Freebird splash), `hs1_0360.png` (Sigmund
  Corp. street), `hs2_0360.png` (minisode 2's snowy opening).
- Every earlier check unchanged: `bottom_menu_check` 18/18, `tone_check` 19/19,
  `effects_check` 16/16, `boot_check` 16/16, `controls_check` 16/16,
  `fileio_check` 19/19, `lang_check` 47/47, `lang_screen_check` 13/13.
- `build_mruby.ps1` and `build.ps1` clean (**0 warnings**); `build_cia.ps1` OK;
  `tools/inspect_cia.py`: 124 MB (New) / 64 MB (Old) modes, 804 MHz, L2,
  "To the Moon 3DS".

## To check on the console

1. Install `ttm3ds.cia` (**129 MB**) with FBI. **Nothing has to be copied to the
   SD card** — the main game (`To the Moon.rgssad`, `lang.dat`, `Fonts/`) and
   both minisodes are inside the app. Saves still go to `SD:/3ds/ttm3ds/save/`,
   where they always were. (For the 3dsx route, the files stay on the card; a
   copy there always wins over the built-in one.)
2. Start the app: the bottom screen lists **three** games — `To the Moon`,
   `SigCorp - Holiday Special`, `SigCorp Minisode 2` — with the main game first
   and the last choice preselected.
3. Tap **To the Moon** (or press A): it must boot as before, its existing saves
   still offered by Continue, and the language screen still on a first run.
4. Tap **SigCorp Minisode 2**: dialogue starts about **20 seconds** in (after
   the snowy exterior). The message box, the name tab and the typed text must
   all show.
5. Tap **SigCorp - Holiday Special**: its intro is longer — the first line is
   about **80 seconds** in (logo, street, then the office). Then `START +
   SELECT` quits (it has no in-game quit).
6. `log.txt` should show `launcher: <title> (…)`, and for an episode
   `legacy syntax: …` three times, `font: … (this game carries no Fonts/ of its
   own)`, then `msg: …` lines as the dialogue starts — that is the proof the
   game is producing text. If a minisode shows a message box with no text,
   send `log.txt`: the `msg:` lines say whether the text reached the window.
7. The bottom screen stays dark while a minisode plays (they have no menu); it
   comes back as the console on an error or on quit.
8. The main game's saves and settings under `SD:/3ds/ttm3ds/save/` must be
   untouched by any of the above.

## Next

1. Run 21: the original-3DS profile (64 MB target, `envIsNew3DS()` budgets,
   vsync-accurate pacing) and the measured comparison.
2. Then the optional 60 fps mode (interpolation), New 3DS only.
3. Then layered 3D.

(Run 17 turned out to be the HOME-menu banner, run 18 the keyboard minigames'
controls, run 19 the ending's Steam achievement hook, run 20 the launcher and the
minisodes' text: see `RUN17-STATE.md`, `RUN18-STATE.md`, `RUN19-STATE.md` and
`RUN20-STATE.md`.)
