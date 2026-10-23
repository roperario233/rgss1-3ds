# Games tried on this engine

Add a row when you try a game — pass or fail. A failure with a `log.txt` tail is as
useful as a success: it is how the gaps get found.

| Game | Engine / RGSS | Status | Notes |
|---|---|---|---|
| **To the Moon** (with `lang.dat`) | RGSS1 / XP | **finishes** — title, saves, 11 languages, credits, epilogue | the reference port; see `RUN*.md` |
| **SigCorp - Holiday Special** | RGSS1 / XP | **plays** | own game; no `Fonts/` (shared face); Steam achievements stubbed; `START + SELECT` to quit (no in-game exit) |
| **SigCorp Minisode 2** | RGSS1 / XP | **plays** | as above; its save system is disabled by design |
| *your game* | | | |

**Status values:** `boots` (title and first map draw) → `plays` (you can walk,
talk, save) → `finishes` (credits/epilogue).

## What “plays/finishes” includes here

- 640×480 drawn to the 400×240 top screen (stretched, or 4:3 with SELECT); simple
  stereoscopic depth from the 3D slider.
- Saves read *and* written (RGSS `Marshal`), in `SD:/3ds/ttm3ds/games/<folder>/`.
- Audio from `audio.ttma` (BGM, BGS, ME, 12 SE voices).
- Bottom-screen menu (the game's own, at half size) where the game has one.
- 124 MB New 3DS mode, 804 MHz, L2 cache.

## Known gaps that affect all games

| Gap | Effect | Workaround |
|---|---|---|
| 40 fps target with frame skip | a game that assumes 60 fps plays slow, not broken | none yet (60 fps interpolation is designed) |
| Texture heap | long sessions with huge panoramas/tilesets can skip textures | none yet; the log says `texture memory full` |
| Steam achievements | no-ops (logged once) | — |
| DLL minigames (video, custom engines) | will not run | needs a Ruby reimplementation |
| Old 3DS / 2DS | not supported yet | New 3DS only |
