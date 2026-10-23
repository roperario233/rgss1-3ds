# Install

There are two ways to run this. The **engine-only CIA** is the one you can install
by scanning a code; it carries no game data, so you bring your own copy.

## The engine-only CIA (recommended)

![QR code for the engine CIA](qr-install.png)

- **By QR**: on the 3DS, open **FBI → Remote Install → Scan QR**, point the camera
  at the code above (display it on a phone or PC), accept the download.
- **From a file**: copy `ttm3ds-engine.cia` anywhere on the SD card, then in FBI
  pick *Install from file* and choose it.
- **By hand**: `python tools/make_qr.py <url>` regenerates the code for any URL you
  host the file at.

Then:

1. Launch **RGSS1 3DS** from the HOME menu. With no game on the card it says so;
   that is expected.
2. Copy your game into `SD:/3ds/ttm3ds/<folder>/`:
   - `<folder>/Your Game.rgssad`
   - `<folder>/Game.ini` (the launcher reads its `Title=` for the name)
   - `<folder>/Fonts/` if the game has one, `lang.dat` if you have translations
   - `SD:/3ds/ttm3ds/audio.ttma` if you made one (`tools/pack_audio.py`)
3. Start it again: the launcher lists every game it finds. Tap one or press A.

## The 3dsx (no install, quick iteration)

Copy `engine/ttm3ds.3dsx` to `SD:/3ds/ttm3ds/` and start it from the Homebrew
Launcher. It reads its files from the card (and can read `romfs` if you built it
with `-WithMinisodes`). Handy while developing; a `.3dsx` cannot ask for the 124 MB
New 3DS mode, so the CIA is the better way to play.

## Saves and settings

| What | Where |
|---|---|
| The main game's saves and `lang` | `SD:/3ds/ttm3ds/save/` |
| Any other game's saves | `SD:/3ds/ttm3ds/games/<folder>/` |
| The log | `SD:/3ds/ttm3ds/log.txt` — rewritten every launch |
| The last game chosen | `SD:/3ds/ttm3ds/last-game.txt` |

## Sound

The DSP needs its firmware at `SD:/3ds/dspfirm.cdc`. If sound is missing, run the
**DSP1** homebrew once; the log then says `audio: NDSP up`.

## Uninstalling

FBI → *Titles* → pick **RGSS1 3DS** → *Delete title*. The SD folders above are
untouched; delete `SD:/3ds/ttm3ds/` yourself if you want the saves gone too.

## Old 3DS

Not supported yet: the builds ask for the New 3DS's 124 MB mode, 804 MHz clock and
L2 cache. An Old 3DS profile is designed but not built.
