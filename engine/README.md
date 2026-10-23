# engine: the RGSS1 runtime for the New 3DS

An RPG Maker XP (RGSS1) runtime for the New 3DS that runs **the player's own copy
of To the Moon**: its archive, its font and its own Ruby scripts, unchanged except
for a few load-time patches. The app contains none of the game's files.

Built on the results of M1 (`../3ds-app`, the hardware path) and M2
(`../ruby-spike`, mruby as the Ruby host). This folder is M3 onward.

## Layout

| Path | What |
|---|---|
| `gems/mruby-ttm-core/` | archive reader (`.rgssad`), the language pack reader (`lang.dat`, a ZIP), `Marshal.load`/`dump`, the game's own files (`File`/`Dir` on real storage), zlib, `TTM.load_script`, timing, the PC line profiler |
| `gems/mruby-ttm-rgss/` | `Color`, `Tone`, `Rect`, `Table` in C (`include/ttm_values.h` for other gems) |
| `gems/mruby-ttm-gfx/` | **M3**: `Bitmap` (PNG/JPEG/BMP via stb_image, text via stb_truetype), `Viewport`, `Sprite`, `Window`, `Graphics` — shared C, two backends: `backend_soft.c` (PC, saves PNGs) and `backend_c3d.c` (3DS, citro2d) |
| `gems/mruby-ttm-audio/` | **M7**: `Audio` (BGM, BGS, ME, SE) from the audio pack, decoded with stb_vorbis — shared core `audio.c`, two backends: `backend_ndsp.c` (3DS, NDSP on its own thread) and `backend_null.c` (PC, silent but decoding) |
| `../tools/pack_audio.py` | the game's `Audio/` re-encoded smaller with ffmpeg into one file, `audio.ttma` |
| `../tools/pack_romfs.py` | everything the app can carry, for its romfs: the main game (archive, `lang.dat`, fonts) and the two minisodes, with only the sounds the audio pack lacks |
| `../tools/make_banner.py` | the HOME-menu banner: the turning plaque over the game's night sky with the title theme looped, built once into `work/banner/banner.bnr` (it runs the three `make_banner_*.py` tools and `tools/pycgfx/`, then flips the CWAV's loop flag) |
| `../tools/inspect_banner.py` | what a `.bnr` — or the banner inside a CIA — really carries: the CGFX's size and whether it is animated, the tune's rate, channels, length and loop |
| `gems/mruby-onig-regexp-3ds/` | Regexp (Onigmo), built without `configure` |
| `patches/` | two mruby patches: allocation counter, `!=` compiled inline |
| `mruby_config.rb`, `build_mruby.ps1` | mruby for the PC and the 3DS (own build dir: `<devkitPro root>/build/engine`) |
| `romfs/rgss/` | the Ruby side of RGSS: `compat.rb` (platform shims), `rgss.rb` (Font, Input, Audio, RPG module, `load_data`), `ttm_patches.rb` |
| `romfs/boot.rb`, `romfs/main.rb` | start the game from the player's files |
| `source/main.c`, `Makefile`, `build.ps1` | the 3DS app, `ttm3ds.3dsx` |
| `cia/app.rsf`, `build_cia.ps1` | the same app as an installable CIA (`ttm3ds.cia`, HOME menu) |
| `pc/run_pc.rb` | run the game on the PC with the software renderer, saving screenshots |
| `pc/marshal_roundtrip.rb` | the writer against bytes CRuby wrote (`work/marshal_fixtures/`) |
| `pc/save_roundtrip.rb` | run the game, let it write its own save, walk that file stream by stream |
| `pc/fileio_check.rb` | the file layer on its own (also runs on the console) |
| `pc/lang_check.rb` | the language pack, the translations, localised pictures, the CJK faces |
| `pc/lang_screen_check.rb` | the game's own language screen, choosing German, the German game |
| `pc/minisode_check.rb` | one of the two Sigmund minisodes, run as its own game (`hs1`/`hs2`) |

## Save files

`Marshal.load` and `Marshal.dump` are both in `gems/mruby-ttm-core/src/marshal.c`.
The game's save is twelve Marshal streams in one file (`083_Scene_Save.rb`'s
`write_save_data`, called through `096_TtM Util.rb`, plus UMS's `$ams`) that
`084_Scene_Load.rb` reads back with a dozen-odd `Marshal.load(file)` calls, so a
stream has to leave the file object standing exactly at its own end.

Two things that only a real save showed, both found by `pc/save_roundtrip.rb`:

- a class or module *value* (`$game_system` holds `@default_formatter =
  Paragrapher::Formatter`) is written as a plain length-prefixed string next to
  `c`/`m`, not as a symbol — the symbol only belongs next to `o`/`u`/`S`/`U`,
  where the class path shares the symbol table with the instance-variable names;
- `rgss/compat.rb`'s `MemIO` (the engine's `File`) sliced with `String#[]` and
  counted with `String#size`, which under `MRB_UTF8_STRING` are *characters*:
  one byte of a save that happens to look like a UTF-8 sequence and every later
  stream is read from the wrong offset. File content is `byteslice`/`bytesize`
  throughout now.

The files themselves are real ones: `TTM.file_write/file_size/file_mtime/
file_delete/dir_mkdir/dir_entries/dir?` (`gems/mruby-ttm-core/src/fileio.c`) are
what `File`/`Dir`/`FileTest` are built on, and every path the game passes is
mapped under `$ttm_data_dir` — the app's folder on the SD card on the console
(`sdmc:/3ds/ttm3ds/`), a scratch folder on the PC. A write makes its parent
folders, so the game's `Dir.mkdir($savePath)` and `File.open("save/save4.rxdata",
"wb")` work whether or not it remembers the first one, and a write that fails is
logged rather than fatal. `pc/fileio_check.rb` checks that layer on its own (19
checks, including a 1,024-byte binary round trip through every byte value), and
it can be run on the console, where `$ttm_data_dir` is the SD card.

**Verification.** `pc/marshal_roundtrip.rb` compares the writer against streams
the real CRuby wrote (`work/probe_marshal.rb` -> `work/marshal_fixtures/`): 24
checks, byte for byte — primitives, both sides of the ±2**30 fixnum line,
Bignums, Ruby's float text, Structs, class and module values, user marshal,
symbols, Hashes with a default, shared objects, cycles, object links, and
thirteen streams through one file object. The only deliberate difference is that
a String here carries no encoding instance variable (CRuby 3.x wraps a UTF-8
string in `I` with `:E => true`; RGSS 1.x has no such thing). `pc/save_roundtrip.rb`
then runs the game itself and walks the save *it* wrote — 121,800 bytes, thirteen
streams, every one re-dumping to exactly the bytes it came from, 0 bytes left
over. And the other side of M5's claim, with the real Ruby reading that same
file: `work/probe_save_read.rb` loads all thirteen streams with CRuby 3.4.11 and
re-dumps each one — **13/13 byte-identical, 0 bytes left over**, the same sizes
and offsets the engine printed. So a save written here is a save a PC copy of the
game can read.

## Run it on the 3DS

1. Copy `ttm3ds.3dsx` to `SD:/3ds/ttm3ds/ttm3ds.3dsx`.
2. The game's files, from **your own install of To the Moon**:
   `To the Moon.rgssad` and `Fonts/OpenSans.ttf`, into `SD:/3ds/ttm3ds/`
   (or into `SD:/3ds/ttm3ds_ruby/`, where the M2 test already has the archive —
   the app looks in both).
   **To play in another language**, also copy `lang.dat` (the install folder's
   17 MB translation pack) into the same folder, and — for Korean and Chinese —
   `Fonts/NanumBarunGothic.ttf` and `Fonts/WenQuanYi_Zen_Hei.ttf`. With
   `lang.dat` there, the first run opens on the game's own language screen (12
   entries); the choice is remembered in `save/lang`, so later runs go straight
   into that language. Without `lang.dat` the game is English, as before.

   **The two Sigmund minisodes** (run 16) are whole RGSS1 games of their own,
   not content inside the main game. The **CIA carries everything**: its romfs
   holds the main game (`To the Moon.rgssad`, `lang.dat`, `Fonts/`) *and* both
   episodes, so installing `ttm3ds.cia` is the only step — the SD card needs
   nothing but your saves. The **3dsx does not**: the Homebrew Launcher loads a
   `.3dsx` into RAM whole, so that route reads its files from the card — the
   game's own (`To the Moon.rgssad`, `lang.dat`, `Fonts/`) and, if you want the
   episodes, `Sigmund Holiday Special 1/` and `2/` (each with its `.rgssad` and
   `Audio/`) in `SD:/3ds/ttm3ds/`. A folder of the same name on the card always
   wins over the built-in copy, so the two routes coexist.
3. Sound: `python tools\pack_audio.py <the game's folder, or its zip>` makes
   `work\audio\audio.ttma` (22 MB from the game's 35.5 MB), and `build.ps1`
   packs it **inside** `ttm3ds.3dsx` (romfs:/audio.ttma; the app is then
   24 MB). Nothing else to copy: an old `SD:/3ds/ttm3ds/audio.ttma` can be
   deleted. The DSP needs its firmware at `SD:/3ds/dspfirm.cdc`: if it is not
   there, run the **DSP1** homebrew once. Without it the game runs silent,
   and `log.txt` says so.

   A 3dsx built this way contains the game's music: it is for your own
   console only, never for sharing (matrix §7). `build.ps1 -NoAudio` builds
   the engine alone, which then reads `SD:/3ds/ttm3ds/audio.ttma` if present.
4. Start **To the Moon 3DS** from the Homebrew Launcher.

**Or install it as a CIA** (run 14: no Homebrew Launcher step). `build_cia.ps1`
packs `ttm3ds.elf`, `ttm3ds.smdh` and `romfs/` with makerom into
`engine/ttm3ds.cia` (129 MB, 124 MB New 3DS mode, 804 MHz + L2). Copy it to the
SD card, install it with **FBI**, then launch it from the HOME menu; **Exit Game
> Shutdown** returns to HOME. It reads and writes the same `SD:/3ds/ttm3ds/`
folder as the 3dsx, and it carries the game's music, so it is for this console
only too. Run `build_mruby.ps1` first after changing a gem: the renderer lives
in `libmruby.a`, and `build_cia.ps1` refuses to package a stale one. 178 MB
(extended memory) black-screened on this console, so it is not offered.

**The animated banner is opt-in.** It is built by `python tools\make_banner.py`
from the game's own sky and title theme, but this console's HOME menu would not
render it: a build carrying it showed a blank tile and hung (run 19), while the
flat M1 banner renders and the game boots. So `engine/ttm3ds.cia` carries the
**flat** banner, and `build_cia.ps1 -AnimatedBanner` writes `ttm3ds_anim.cia` for
another console. `python tools\inspect_banner.py engine\ttm3ds.cia` prints which
banner a build carries (`static (no CANM)` here).

Buttons: **A** confirm, **B** / **START** cancel and menu, **D-pad / Circle Pad**
move, **hold L** the timeline (the PC game's "mouse at the top of the screen"),
**SELECT** full screen / 4:3, **START + SELECT** quit. In the zombie phase the
D-pad also throws plants in its four directions, and the whack-a-mole is played
with the D-pad and A. The **3D slider** puts
the whole picture behind the screen (simple depth, not layered: text and menus
sit at the same depth as the map); at 0 it is flat. Log:
`SD:/3ds/ttm3ds/log.txt`.

**Which game** (run 16). The app runs any RGSS1 archive it finds in
`SD:/3ds/ttm3ds/` or in one folder below it, so the two minisodes above simply
appear. With **one** game it boots straight in, exactly as before. With **two or
more** the bottom screen is a launcher — one row per game, titled from each
game's own `.ini` (`SigCorp - Holiday Special`, `SigCorp Minisode 2`) — and you
tap a title or use D-pad + A; the last choice is kept in `last-game.txt` and is
preselected next time. A minisode writes in `SD:/3ds/ttm3ds/games/<its folder>/`
(`save/`, `music.txt`, `window.txt`), so it can never touch the main game's
saves, and its `Fonts/` come from the app's shared `Fonts/OpenSans.ttf` because
the minisodes carry none. They have no bottom-screen menu of their own (that
patch hangs off a script only the main game has), so the bottom screen stays
dark while one plays.

**Touch screen** (from run 13): while you play, the bottom screen is the
game's own menu: the Characters / Notes / Items tabs and lists, and the Save /
Load / Exit Game row, on its Sigmund Corp. background. It is built from the
game's own window classes (`ttm_patches.rb`, `TTMBottomMenu`) and shown at
exactly half size -- the whole 640x480 menu (`VIEW = [0, 0, 640, 480]`) -- with
the game's heart-monitor strip across the top. The strip is the game's own
`Window_MenuHeartMonitor`: its pictures follow game variable 72 (the era:
heart0..4, overlay0..4, graphic0..4) and its overlay scrolls by variable 73.
The buttons keep driving the map. Use the stylus for the menu:

| Tap | Does | Veiled (and buzzes) |
|---|---|---|
| a tab | shows that list | never |
| a row of Notes / Items | shows its description; drag a list to scroll it | never |
| **Save** | the game's own save screen, opened as its save event command opens it; cancel returns to the map | while an event runs, or when the menu or saving is disabled |
| **Load**, **Exit Game** | their screens; cancel returns to the map, not to the top screen's menu | while an event runs, or when the menu is disabled |

Off the map (the top screen's own menu, the save and load screens) the bottom
screen is dimmed and does nothing. On the title screen it is empty. B still
opens the menu on the top screen, as before. Before the game starts the bottom
screen is dark -- the boot lines are in `log.txt` from run 14 -- and after an
error it is the text console again.
`pc/bottom_menu_check.rb`: **18/18** in the running game (including the strip,
its era pictures and its remake when variable 72 changes), with screenshots in
`pc/shots/bottom_*.png` (graphics.c draws the bottom screen on the PC too).

The speed figures that run 12 showed on the bottom screen now go to `log.txt`.
Every fifth second there is a `perf:` line with `drawn/updates fps`, `rb` (the
game's Ruby per update), `dr` (drawing per drawn frame) and free texture
memory. Any second whose `rb` is over 100 ms gets a `perf: slow,` line.

L is *only* the timeline. It is not RGSS's L key: the game's common event 85
("FAST GFX UPDT") sets `Graphics.frame_rate = 160` while RGSS's L is held and
puts 40 back only when RGSS's A is pressed, so until run 11 every look at the
timeline left the game in fast-forward. Page-up in item lists (the only other
use of RGSS's L) is gone with it.

There is no mouse: the pointer is reported outside the window (as mkxp does),
the game's cursor sprite is kept hidden (`ttm_patches.rb`, "Mouse 3"), and the
English instructions that name the mouse or the keyboard are rewritten for the
3DS's own controls as they load (`compat.rb`, `TTMControls`): the timeline
message on map 22; the two tutorial banners `Tut_MenuCancel` /
`Tut_InteractItem`, repainted with a button in their own colours; the zombie
phase's "PRESS 'W' 'A' 'S' 'D'" banner and its keyboard, replaced by a D-pad;
and "Hold SPACE to gallop" on the horse, which is A here.

The zombie phase (map 129) is the one place the game reads the keyboard's
W A S D directly (RGSS's R/X/Y/Z): the 3DS reports only two of those, so two of
its four shooting directions could never fire. While the phase's overlay picture
is up, each D-pad direction also presses its key — up W, left A, down S, right D
— so all four aim; the arrows still reach the game, and nothing changes outside
the minigame. Map 71's "Use mouse (Recommended)" / "Use keyboard/gamepad"
question now offers the gamepad and "Show the controls", and both answers enter
the arrow-key whack-a-mole (map 82) instead of the mouse-only map 125.
`pc/controls_check.rb`: **34/34**, and it saves the repainted pictures to
`pc/shots/controls_*.png`.

The game's 640×480 screen fills the top screen corner to corner (400×240,
5:3), which makes the picture 25% wider than the game drew it. **SELECT**
switches to the game's own 4:3 shape at half size, centred (layout option A),
and back.

**What the app writes back to `SD:/3ds/ttm3ds/`** (real files, `rgss/compat.rb`'s
`TTMFiles` maps every path the game's `File`/`Dir` touches under that one folder):

| Path | Problem it would show if missing |
|---|---|
| `log.txt` | nothing was written to the SD card at all (a permissions/folder problem) |
| `save/save1.rxdata` … `save4.rxdata` | the game's saves — `save4` is the autosave an event makes |
| `save/music.txt`, `save/window.txt` | the two settings files the game creates on a first run |
| `save/lang` | the language chosen on the language screen (`de`, `pt-BR`, …) — delete it to see the screen again |

**The hardware run to write down** (`SETUP-3DS.md` Part 4 is the M1 version of
this list):

| What to look at | What it decides |
|---|---|
| Do the oversized images show: the sky scrolling down at the title, the vehicles, lighting and heart overlay? | that slicing (M6 step A) works on the PICA200, not only in the PC renderer |
| The `gfx:` lines in `log.txt` — one per sliced image: `N slices of 1024 px, NNN KB free, NNN KB tex` | the main heap and the texture (linear) heap left as the biggest images arrive; textures are the budget to watch |
| Do the maps draw (ground, walls, water), and do characters pass behind trees and in front of them? | `Tilemap` (M6 step B) and its priority z rule on hardware |
| The `perf:` lines in `log.txt` (`drawn/updates fps, rb N.N, dr N.N ms, tx NNNNNK`), in a busy map | the game's speed (updates should stay at 40 while frames drawn may drop: frame skip), and where a frame's time goes (`rb` the game's Ruby per update, `dr` drawing per drawn frame, in ms) |
| Is the message box text readable (layout option A), and does the name tab show? | §5.2's UI choice, and §10.7's z-order rule on hardware |
| Can you walk into a map that triggers the autosave, then find `save/save4.rxdata` on the card with a plausible size (~100 KB+)? | that the save path is real on the console, not just on the PC |
| A second run: does the title screen offer **Continue**? | that `Scene_Load` reads the file back — the load half, on hardware |


## Build

```powershell
cd <this checkout>/engine
powershell -ExecutionPolicy Bypass -File build_mruby.ps1   # mruby + gems, PC and 3DS
powershell -ExecutionPolicy Bypass -File build.ps1         # ttm3ds.3dsx
powershell -ExecutionPolicy Bypass -File build_cia.ps1     # ttm3ds.cia (after the gems)

# the HOME-menu banner, once: the turning plaque and the title theme (run 17)
# (needs the game's own art and music; without it build_cia.ps1 uses the flat one)
python ..\tools\make_banner.py
python ..\tools\inspect_banner.py ..\work\banner\banner.bnr

# the game on the PC, with screenshots in pc/shots/
<mruby> pc\run_pc.rb

# the save format and the game's own save (both print a pass/fail line per stream)
ruby ..\work\probe_marshal.rb              # rewrite the CRuby fixtures first
<mruby> pc\marshal_roundtrip.rb
<mruby> pc\save_roundtrip.rb
<mruby> pc\fileio_check.rb

# oversized images drawn as slices, against one texture, pixel for pixel
<mruby> pc\slice_check.rb

# panoramas and fogs (Plane), pixel for pixel against their bitmaps
<mruby> pc\plane_check.rb

# the tilemap against the game's own maps and tilesets
<mruby> pc\tilemap_check.rb

# a disposed image's pixels reused by the next load of the same file
<mruby> pc\keep_check.rb

# the row-by-row PNG decoder against stb, on every image in the archive
<mruby> pc\png_check.rb

# image pixels shed when RAM is short and decoded again on demand
<mruby> pc\shed_check.rb

# tone, viewport colour and flash, freeze and transition
<mruby> pc\tone_check.rb

# ...and the real Ruby reading the save the engine just wrote (work/pc_data/)
ruby ..\work\probe_save_read.rb

# events whose graphic is a tile (books, vases, furniture) against the tileset
<mruby> pc\tile_event_check.rb

# sound: pack the game's audio, list what its data asks for, then check both
python ..\tools\pack_audio.py "..\To.the.Moon.v2023.07.03.Incl.ALL.DLC.zip"
python ..\work\audio_refs.py
<mruby> pc\audio_check.rb

# Show Animation (balloons, flashes) and the game's own weather
<mruby> pc\effects_check.rb

# the start-up caches (archive index, compiled scripts)
<mruby> pc\boot_check.rb

# where a map change's time goes (line profiler)
<mruby> pc\mapload_profile.rb

# the game's menu on the bottom screen (the heart-monitor strip included),
# worked with a scripted stylus
<mruby> pc\bottom_menu_check.rb

# a message's choice and name windows, redrawn only when they change: run it
# both ways, the checksums must match
<mruby> pc\choice_check.rb
<mruby> pc\choice_check.rb unpatched
```

**Run 13** (from the run 12 photos):
- **Exit Game crashed the console.** The exception screen came up on Exit Game >
  Shutdown, at PC `0x00218308` and FAR `0xC`. In the ELF, that is newlib's
  `_close_r`, called from `_fclose_r` through `_fwalk_sglue`: the exit-time
  close of every open `FILE`. The audio pack (`romfs:/audio.ttma`) was still
  open when `main.c` called `romfsExit()`, so newlib closed it through a romfs
  device that no longer existed. The audio gem now closes the pack in its
  `final` hook, during `mrb_close`. (The archive was already closed there.)
- **Choices ran at 4 fps** (`rb 238.4`). UMS re-runs `reset_window` every
  frame while text is over an event, and that rebuilt the name and choice
  windows each time: two new Bitmaps per frame, and the choices drawn letter
  by letter through regexps. `ttm_patches.rb` ("UMS new") now redraws each
  window only when its text or font changes. `pc/choice_check.rb`: the same
  contents checksum both ways, 2 Bitmaps per frame → 0, and Ruby per frame
  3.95 → 0.54 ms on the PC.
- **The bottom screen** is the game's menu (above), in place of run 12's four
  buttons.


**Start-up caches** (run 12). `SD:/3ds/ttm3ds/cache/` holds the archive's
index (`archive.idx`, 45 KB) and the game's scripts compiled to mruby
bytecode (`NNN_<crc>.mrb`, 820 KB together). On the console those took 1.9 s
and 2.0 s of every start (M2). The first start makes them, and later starts
read them. A changed archive, patch or engine makes new ones by itself, and the
folder can be deleted at any time.

## Status (M3, 2026-10-19)

**On the PC the engine runs the game's opening:** the starfield and Freebird Games
logo, the title screen (moon, lighthouse, Begin / Load / Exit), the credits
sequence, and the first scene with **the game's own message box, name tab and
dialogue text** ("Well excuse me for heroically evading that squirrel coming out
of nowhere!"), drawn by the game's own message system (UMS).

Two details that mattered:
- **z order ties.** The message system gives the name window and its tab the same
  z; the name only shows if an object whose z *changes* moves on top of its z group
  (mkxp's rule, which the game was built against), not plain creation order.
- **Clipping is done in shared code** — unrotated quads are trimmed to the
  viewport / window before reaching a backend — so both backends behave the same.

**Not drawn yet:** a sprite's colour while it also has a tone (on the 3DS the
tone takes all six combiner stages) and a viewport flash with no colour.
Sound came in run 11 (M7); Show
Animation effects in run 12. Weather was never missing: the game's own
script (ccoa's) replaces `RPG::Weather` and draws with our Sprite and Bitmap.

**Save and load work end to end (2026-10-20).** `Marshal.dump` is in
`marshal.c` beside the loader, and it is byte-identical to CRuby's own for
everything a save contains (see "Save files" above). A 9,000-frame PC run now
gets past the game's autosave — `$ttm.s()` from an event, which used to end the
run with `undefined method 'dump' for Module` — and the 121,800-byte file it
writes reads back as thirteen streams with nothing left over, through the
engine's own `File` shim. Those files are real ones now too (SD card on the
console, a scratch folder on the PC), and the save the engine writes loads in
**CRuby 3.4.11** stream for stream, re-dumping to the same bytes — so a console
save is one a PC copy of the game can read. What is left on this path is the
*scenes*: `Scene_Save`/`Scene_Load` are the game's own scripts and only need this
plumbing, but nothing has driven them from the menu yet.

**An asset can no longer end the game (fix after the M3 hardware run).** The 3DS
refuses textures over 1024 px on a side, but that was checked only when the
*texture* was made — *after* `bitmap.c` had decoded the image into RAM. Loading a
map therefore decoded `Graphics/Tilesets/Tileset.png` (256×6096, **6 MB** of
RGBA that the PICA200 could never sample), the allocation failed, and the game
exited with `RGSSError: cannot decode image … outofmem` before the opening scene
(the file's own `89 50 4e 47 …` head in that message is intact, so the archive
read was fine — stb only says `outofmem` when a `malloc` fails). Now the backend
reports its limit (`gfx_max_tex_dim()`: 1024 on the 3DS, 0 = unlimited on the PC,
so PC output is unchanged) and it is asked **before** the decode, so an image
that can never be a texture is skipped and logged instead of decoded, and a
decode that fails with `outofmem`/`too large` substitutes a blank 32×32 bitmap —
RGSS's stand-in for an image that is not there — logging the free heap
(`gfx_mem_free_kb()`, `mallinfo()`, 3DS only) so the next run reports the
headroom. `RPG::Cache.load_bitmap` carries the same net in Ruby, so no single
asset can abort the game. `$ttm_strict_assets = true` restores the hard failure
when chasing an asset bug, and `$ttm_max_tex_dim` overrides the limit (0 = none)
— which is how the skip path is exercised on the PC, where the software renderer
has no limit at all. What gets skipped is exactly what M6 brings back:
tilesets (256×6096 … 256×12500), panoramas and the big character sheets
(`Vehicles` 1143×686, `shuttle` 604×1729, `Light 2` 1024×1200).

**Images over 1024 px are drawn as slices (M6 step A, 2026-10-20).** That
skip is gone. A bitmap larger than the backend's texture limit keeps one pixel
buffer on the CPU, as every bitmap does, but its texture copy is a grid of
slices of at most 1024 px. `ttm_bitmap_draw` (bitmap.c) is the only way sprites
and windows draw a bitmap: it cuts the quad at slice edges and places each piece
inside the original quad, scaled, rotated and mirrored the same way. So nothing
above it knows slices exist, and Step B's `Tilemap` gets `town2` (256×9332, ten
slices) for free. `$ttm_max_tex_dim` now sets the slice size, and
`pc/slice_check.rb` uses it to draw the game's own eight oversized images in
256 px slices and as one texture. **11/11**, identical pixel for pixel wherever
the pieces land on whole pixels. At zoom 1.3, 730 pixels differ, all where a
pixel centre falls exactly on a texel edge (rounding ties, none of them gaps).
A rotated sprite differs in none.

**Texture memory is a cache (after console run 4).** Textures are copies of
pixels the CPU keeps, so a bitmap not drawn this frame gives its textures up
when a new one cannot be made, and a forced garbage collection runs between
frames when memory is low. The collector cannot see the megabytes a `Bitmap`
holds, and the game leaves its message bitmaps to it. `$ttm_tex_budget_kb`
makes the PC renderer pretend the 3DS's limit, and `Graphics.ttm_tex_stats`
reports what textures hold (matrix §10.14).

**A map change no longer decodes its tileset again (after console run 7).**
`Spriteset_Map#dispose` disposes the tileset and autotiles on every map
change, and the next map loads them again. On the console the second decode of
the 256×6096 `Tileset` (~13 MB of RAM in big pieces) could fail in a heap the
map change had just fragmented. The bitmap was then a blank 32×32, and the map
drew only its autotiles: grass, dirt, water and wall borders, but no road,
trees or furniture, with the panorama showing through. The failure was silent,
because each image's log line was printed only once, at its first, successful
load. Now a dispose of an image of 1 MB or more whose pixels are still the
file's keeps them, up to 16 MB in total (`bitmap.c`, "kept images"), and
`Bitmap.new` of that file takes them back without decoding. Kept images are
freed before a large decode of another file when RAM is short, when RAM falls
under 16 MB, and before a retried decode. Every decode failure is now logged.
`pc/keep_check.rb`: **14/14** (reuse, same pixels, ten map changes on one
decode, and nothing the game drew into is ever reused).

**No image needs a large block of RAM (after console run 8).** Keeping
disposed tilesets was not enough: the next run's log had 22 decode failures,
all `outofmem`, with 20 to 42 MB free. Even a 732×552 fog (1.6 MB) failed.
The heap was fragmented: plenty free in total, but no single hole as big as
an image, and stb needs two of them (the inflated rows, then the pixels). Two
changes fix it:
- **Pixels live in bands** (`gfx.h`, `ttm_row`): a bitmap is a stack of
  allocations of at most 128 KB, so the 256×6096 tileset is 48 blocks instead
  of one 6 MB block, and any 128 KB hole will do. Every pixel access goes
  through `ttm_row(b, y)`.
- **PNGs are decoded row by row** (`png.c`): zlib inflates one scanline at a
  time, which is unfiltered and written straight into its band. A decode
  needs two scanlines and zlib's 40 KB on top of the pixels. Every PNG in the
  game (8-bit grey, RGB, RGBA, palette at 1/2/4/8 bits, none interlaced) goes
  this way; JPEG and anything unusual still use stb. `pc/png_check.rb`
  decodes all 798 images in the archive both ways: **identical, every pixel**
  (154 million), and no PNG falls back to stb.
Texture slices also prefer the smaller grid when the padding ties (a
256×6096 tileset is 24 textures of 256 KB instead of six of 1 MB), since the
linear heap fragments too (run 8: a texture not made with 11 MB free).

**Decoded pixels are a cache too (after console run 9).** Run 9 got through
the title and onto the maps with every tileset and texture drawing, then
stopped on the bedroom map with `RGSSError: failed to create bitmap 510 x 84:
out of memory`, a message-box bitmap of 170 KB. The log's free RAM had gone
67 → 54 → 42 → 30 → 12 → 9.5 MB as images arrived and never came back:
`RPG::Cache` keeps every image the game loads (as RGSS does), and every
bitmap kept its pixels in RAM as well as its textures. A 4,000-frame PC run
ends holding 37.8 MB of pixels, and 27.6 MB of that is images still exactly
as decoded from their files. Those can always be decoded again, so they are
now on a list by when their pixels were last read. When RAM is short (under
20 MB free, or an allocation fails) the least recent give their pixels up.
Images whose textures are current go first, since they draw from those
without the pixels. Anything that needs the pixels again (an upload after
their textures were evicted, a `blt` from them, `get_pixel`, the tilemap's
autotiles) decodes the file first. Bitmaps the game drew into are never
shed. Between frames only images left unread for a second are shed, so one
the game blts from every few frames is not decoded over and over. Texture
eviction spares shed images where it can. Bands are 64 KB now (run 9 failed
on 80 KB with 9.5 MB "free"), and failure lines in the log report free RAM,
the largest block malloc can give, and pixel bytes (all / sheddable).
`$ttm_ram_budget_kb` makes the PC pretend that much RAM for pixels.
`pc/shed_check.rb`: **25/25** (the budget holds, every shed image comes back
pixel for pixel, a shed image draws from its textures, and from a new decode
once those are gone, `blt` and `dup` from shed images, dispose, nothing drawn
into is ever shed). The slice, tilemap, plane, tone and controls checks also
pass unchanged under a 4 MB budget, where the tilemap check alone sheds 63
images and decodes 35 again.

**Tone, viewport colour/flash, freeze and transition (2026-10-20).** 826 of
the game's 989 screen-tone changes use gray, so gray matters. The PC renderer
does it per pixel with RGSS's formula. On the 3DS there are no shaders, so the
PICA200's six texture-combiner stages do it: three for the luma
(R·0.299 + G·0.587 + B·0.114), one to move toward it by gray, one to add the
tone, one to subtract it. A viewport's tone needs the pixels already drawn, so
the frame ping-pongs between two VRAM render targets. A sprite's or fog's
tone runs the same combiners on its own quad. `Graphics.freeze` copies the
frame last shown into a third target (RGB565), and the screen shows it until
`Graphics.transition` fades to the new scene. Image transitions
(`Graphics/Transitions`) write each pixel's opacity into the frame's alpha
channel and blend the frozen frame by destination alpha. `pc/tone_check.rb`:
**19/19** against RGSS's formulas.

**Maps draw (M6 step B, 2026-10-20).** `Tilemap` is C now (`tilemap.c`). It
draws the visible 21×16 cells straight from the tileset through
`ttm_bitmap_draw`, so `town2`'s ten slices need nothing special. Autotiles are
assembled once per sheet into a 256-wide bitmap of all 48 patterns per
animation frame (RMXP's quarter table), and animate every 16 `update`s. RGSS's
z rule is kept: priority-0 tiles at z 0, and a priority-p tile on row y at
(y·32 − oy) + 32 + 32p, using one drawable per screen row (twenty "row layers").
A character sprite at the same z still draws on top, so characters pass in
front of and behind trees by where they stand. `pc/tilemap_check.rb` compares
five of the game's maps with their tilesets: **17/17**. That is every plain
tile pixel for pixel (363 cells, 335,045 pixels), autotile patterns 0 and 47
against the sheet, and a priority-1 tile against a sprite one z below and at
its z. All of it runs with 256 px slices at an ox/oy that is not a multiple of
32. Each big bitmap is cut on whichever of a 1024, 512 or 256 px grid pads
least to powers of two (`grid_for`). Title Scroll takes 3.8 MB of textures
instead of 6, and a 640×480 bitmap 1.25 MB instead of 2. On the 3DS, the
bottom screen's last line shows fps, render time and free texture memory
once a second.

**Panoramas and fogs draw (M6 step C, 2026-10-20).** `Plane` is C now
(`plane.c`). It repeats its bitmap over its viewport, shifted by ox/oy (screen
pixels, wrapping) plus the viewport's ox/oy, scaled by zoom, with opacity, blend
type and color. Tone is kept, not drawn. Each repeat goes through
`ttm_bitmap_draw`, so a 1645 px panorama's slices need nothing special.
`pc/plane_check.rb` checks every 5th pixel against the bitmap itself: **6/6**
(scrolled, wrapped past its size, negative offsets, a smaller viewport, zoom 2,
half opacity). The lighthouse cliff now has its sky, moon and sea, and the
house's windows show the night sky.

**On the 3DS:** the engine runs the game through the title, the menu, maps
(sprites only until `Tilemap`) and save/load; see the matrix §10.11–§10.12.

**Run 11: sound, and three bugs from run 10 (2026-10-23).** See the matrix §10.19.
- *Tile events.* `RPG::Cache.tile` was a blank bitmap, so the 488 event pages
  on 69 maps that show a tileset tile (books, flowers, vases, the furniture the
  game moves) were invisible. It is now the tile's 32×32 cell of the tileset,
  as in RGSS. `pc/tile_event_check.rb`: **7/7** (all 201 distinct tiles pixel
  for pixel, as normal and with 256 px slices under a 4 MB RAM budget).
- *Fast-forward.* L was RGSS's L as well as the timeline (above).
  `Graphics.frame_reset` now resets the pacing too; it was a no-op, so the
  frames after a stall the game expects were run back to back.
- *Slow after HOME.* The 804 MHz clock was set by hand once at start-up;
  libctru reapplies only what `osSetSpeedupEnable` asked for, so every return
  from HOME left the game at 268 MHz without L2. `main.c` now uses it, and
  logs `apt: resumed` each time.
- *Sound (M7).* `gems/mruby-ttm-audio` plays the game's own calls: BGM and
  BGS loop, ME silences the BGM and fades it back in, fades, pitch, 12 SE
  voices with a 4 MB cache of decoded sounds. On the 3DS the decoding runs on
  a thread on core 2; the game's frames never wait for it.
  `pc/audio_check.rb`: **29/29**. A 4,000-frame PC run plays the title theme,
  *For River* and *Between a Squirrel and a Tree* from the game's own calls,
  with 0 missing sounds.

**Run 14: the heart strip, a quiet boot, transitions back, simple 3D, a CIA (2026-10-23).**
- *Frozen frame (VRAM).* `Graphics.freeze` had never drawn on the console: VRAM is
  two 3 MB banks (libctru's `vramAlloc` cannot cross them), the top screen's
  target held 192 KB of depth and the bottom's 150 KB, and after the two 2 MB
  frame textures neither bank had the 1 MB the 1024x512 RGB565 frozen frame
  asked for. The screen targets are now made without depth, with the colour-only
  clear a depth-less target needs (`C2D_TargetClear` clears depth too, and
  citro3d builds that fill from a NULL pointer), and the frozen frame is 512x256
  RGB565 (256 KB), drawn back up at 640x480. `log.txt` now records
  `gfx: VRAM ... KB free after setup` and any failed allocation: about 742 KB is
  left, each bank under 3 MB.
- *Quiet boot.* With the game found the version, heap and clock lines go to
  `log.txt` only, and `ttm_bottom_start` presents one frame in the menu's dark
  colour, so the console's text no longer sits on the bottom screen from launch
  until the first frame. The console stays for the missing-files message and for
  errors.
- *The strip.* The bottom menu now shows the game's whole 640x480 menu at half
  size (`VIEW = [0, 0, 640, 480]`), and its own `Window_MenuHeartMonitor` is made
  inside `Graphics.ttm_on_bottom`. Its pictures (`heart<v72>`, `heartoverlay<v72>`,
  `heartoveralygraphic<v72>`) follow game variable 72 and the overlay scrolls by
  variable 73; changing 72 disposes and remakes it (the game's `refresh` leaks its
  old sprites). `pc/bottom_menu_check.rb`: **18/18**, and `bottom_*.png` shows it.
- *Simple 3D.* The top screen has a left and a right target, `gfxSet3D` is on,
  and the frame goes to the left eye at x-d and the right at x+d
  (`TTM_DEPTH_PX = 10` at full slider, in the screen's 400x240 space), so the
  whole picture sits behind the screen; at 0 it is flat. The bottom screen stays
  flat. Layered depth (text and menus in front) is a later phase.
- *The CIA.* `engine/cia/app.rsf` + `build_cia.ps1`: 124 MB, 804 MHz, L2,
  `StackSize 0x100000` (a CIA's stack comes from the exheader, not
  `__stacksize__`), UniqueId `0xff3fe`, and the services the engine adds on top
  of the M1 list: `dsp::DSP` (NDSP) and `ptm:sysm` (`osSetSpeedupEnable` after
  HOME). `tools/inspect_cia.py` reads back 124 MB, 804 MHz and "To the Moon 3DS".
- *Tests:* `pc/bottom_menu_check.rb` 18/18; `tone_check` 19/19; `effects_check`
  16/16; `boot_check` 16/16; `controls_check` 16/16. The 3DS build has no new
  warnings in `build_mruby.log` or `build.log`.

**Run 16: the Holiday Specials, and a launcher (2026-10-23).**
- *They are two games, not content.* `Sigmund Holiday Special 1` and `2` are
  whole RGSS1 projects: their own `RGSSAD v1` archives (11.3 MB / 332 files and
  12.9 MB / 533 files), their own 114-odd scripts, their own maps and their own
  `Audio/` as loose Oggs. So the engine runs them the way it runs the main game —
  a different archive, a different folder to write in, nothing else — and the
  archive reader, `Marshal`, the graphics and the audio needed no changes at all.
- *The launcher* (`source/main.c`). The app scans `APP_DIR` and one folder below
  it for `*.rgssad`, titles each from the `.ini` beside its archive
  (`[Game] Title=`), and boots straight in when there is only one game. With two
  or more it lists them on the bottom screen's console: tap a row (the touch
  y-coordinate picks the line) or D-pad + A, `START + SELECT` quits, and the
  choice is remembered in `last-game.txt`. The main game keeps `APP_DIR` as its
  data folder — **existing saves do not move** — while a minisode gets
  `APP_DIR games/<its folder>/`.
- *What the minisodes needed* (found by running them, `pc/minisode_check.rb`):
  - **Ruby 1.8 source.** Their scripts carry two forms mruby's parser refuses:
    a hash literal with the arrows left out (`{'a', 'b', 'c', 'd'}` meant
    `{'a'=>'b', 'c'=>'d'}`) and a space between a method name and its argument
    list (`Bitmap.new (w, h)`, `blt (x, y, …)`). A script that fails to compile
    is rewritten and compiled again (`TTMPatch.legacy`), and logged as
    `legacy syntax: …`; nothing has run at that point, so the rewrite can only
    turn "will not load" into "loads as it did on the PC". Three scripts in each
    minisode need it (`UMS new`, `Mouse 3`, `modern algebra para formater`).
  - **`ENV` and `Hash#index`**, which mkxp and Ruby 1.8 had and mruby has not:
    the minisodes read `ENV['AV_APPDATA']` for their save folder (answered as
    `"."`, since every path is mapped under one root anyway) and the mouse script
    asks a Hash for the key of a value (`compat.rb`).
  - **The mouse.** Their older `Mouse.pos` reads the cursor through `user32` and
    takes a `nil` on every failed call, which raised "nil cannot be converted to
    Integer" on the first frame. The main game's own later build replaced those
    two lines with `Input.mouse_x`/`mouse_y`; the same two lines are patched in
    here (`ttm_patches.rb`, "Mouse 1"), which is why the pointer reports the
    engine's "outside the window" and the D-pad walks.
  - **The party.** Both minisodes' data start with `party_members []`, and their
    `Caterpillar` reads the party's leader whenever the player is centred, so
    the boot-time `moveto` aborted before a frame was drawn. The main game's
    later build guards exactly that lookup (its line is commented out); the same
    guard is patched in, and the main game's copy is untouched (its line starts
    with `#`, which the patch's leading newline cannot match).
  - **A font.** The minisodes carry no `Fonts/`, so `boot.rb` falls back to
    `$ttm_font_file` (`APP_DIR Fonts/OpenSans.ttf`) and logs
    `$ttm_font_shared`.
- *Tests.* `pc/minisode_check.rb hs1` and `hs2`: **10/10 each** — the archive
  opens, all scripts load, the game reaches a map and draws it, screenshots are
  saved, no bottom-screen menu is made, it stays English, the shared font stood
  in, the mouse reports a position instead of `nil`, its writes stay in its own
  folder, and the two compatibility patches applied. `pc/shots/hs1_*.png` and
  `hs2_*.png` show the Freebird splash, the Sigmund Corp. street and minisode 2's
  snowy opening. Every earlier check is unchanged (18/18, 19/19, 16/16, 16/16,
  16/16, 19/19, 47/47, 13/13), and the 3DS build has no new warnings.
- *Inside the app.* `tools/pack_romfs.py` writes `work/romfs_extra/` from the
  same download and `build_cia.ps1` packs it: `romfs:/game/` (the main game's
  `To the Moon.rgssad`, `lang.dat`, `Game.ini` and the three fonts) and
  `romfs:/games/<name>/` (both episodes — archives, `Game.ini`, and only the
  sounds the audio pack lacks: of HS1's 257 sounds 229 are already in it, of
  HS2's 277, 225). 104 MB, so the CIA is 129 MB and the SD card needs nothing
  but saves. The launcher scans the card first, then `romfs:/game/` and
  `romfs:/games/`, so a copy on the card wins. `build.ps1` keeps it off by
  default (`-WithMinisodes` opts in): the Homebrew Launcher loads a `.3dsx` into
  RAM whole, so a 130 MB 3dsx is not a 3dsx, while a CIA's romfs is a separate
  partition read on demand.
- *Dialogue.* Both episodes talk, but their intros are long: at 40 updates a
  second the first line arrives at frame **801** in minisode 2 (20 s) and frame
  **3160** in the Holiday Special (79 s) — the Freebird logo, the walk up to the
  building and the office scene all come first. A `msg:` line now goes to
  `log.txt` for each of the first 20 messages (`ttm_patches.rb`, appended to
  "Mouse 3", the last script that defines `Interpreter#command_101`), so a
  console run says whether the game is producing dialogue at all. Both
  `pc/minisode_check.rb` runs now assert it, and their
  `pc/shots/hs1_dialogue.png` / `hs2_dialogue.png` show a name tab and typed
  text.
- *Not done:* the minisodes' bottom screen stays dark (they have no menu), and
  their saves — they have none, by design (`Holiday Special 1`'s readme says so);
  `START + SELECT` is the way out.

**Run 15: languages (2026-10-23).**
- *The pack.* `lang.dat` is the install folder's ZIP of translations (851
  entries, 11 languages: `de es fr it ko pl pt-BR ru sk tr zh-CN`) plus an
  index. mkxp mounts it beside the `.rgssad`, and the game's own `TR` module
  reads `lang/index.rxdata`, `lang/<code>/{lang,meta}.rxdata` and
  `lang/<code>/Graphics/...` as ordinary files. `gems/mruby-ttm-core/src/langzip.c`
  does the same: it parses the central directory once (one read, no unpacking)
  and inflates a single entry when it is asked for, so the pack costs no RAM
  until something is drawn from it. `boot.rb` opens `$ttm_lang_pack` or
  `$ttm_game_dir + "lang.dat"`; a pack that will not open is logged and the game
  runs in English (an asset must not end the game, §10.8).
- *The game's own language screen.* `load_data` and `TTM.image_data` consult the
  pack for `lang/...`, so `LANGUAGES` is `en` + the pack's 11 and every
  translation, item list, common-event block and localised picture arrives
  through the code the game already has (`TTMTranslator`,
  `EventPatchUtil.patch_map`). `compat.rb` stops reporting the `save/lang`
  setting as `"en"` when a pack is present, so a first run reaches
  `Scene_Language`; the choice is written as a real file and read back on the
  next start. Without a pack the setting still reads `"en"`: the English path is
  exactly what it was.
- *Fonts by name.* The engine had one face (Open Sans, standing in for Arial and
  Times New Roman). The language screen draws each language's own name in that
  language's font, so `font.c` now keeps a face per name, each with its own
  glyph cache: `Font.ttm_face(name, bytes)` adds one, `Font#name=` loads the two
  CJK files from the game's `Fonts/` the first time a `Font` asks for them, and a
  name with no file of its own (the Latin languages) draws in the default face.
  The Korean and Chinese faces cost 4 MB and 8.8 MB of RAM once loaded -- only
  when a pack is there and the language screen or a CJK language has asked for
  them; `log.txt` has `font: ... KB`.
- *Tests:* `pc/lang_check.rb` **47/47** (the ZIP's 797 files and 54 folders, a
  deflated and a stored entry byte-identical, an entry the pack lacks, the German
  menu words and items, map 2's 195 text blocks rewritten with 465 German lines,
  a localised picture 640x480 where the game's is 542x473, the setting file, both
  CJK faces and their glyphs, and the no-pack fallback);
  `pc/lang_screen_check.rb` **13/13** -- the game opens on its language screen,
  12 rows each in its own font, choosing German writes `save/lang`, and the game
  carries on into the German intro (`pc/shots/lang_screen.png`,
  `lang_after_choice.png`). The earlier checks are unchanged: `bottom_menu_check`
  18/18, `tone_check` 19/19, `effects_check` 16/16, `boot_check` 16/16,
  `controls_check` 16/16, `fileio_check` 19/19. The 3DS build has no new
  warnings.


**Run 17: the HOME-menu banner (2026-10-23).**
- *Why it is a scene.* A 3DS banner is not a picture: the HOME menu renders a
  small 3D scene, 256x128, through its own camera (`tools/pycgfx/banner-camera.gltf`:
  30 degrees at z = 44.786, so 40x24 units at z = 0). This one is two textured
  plates — the game's `0moonBG.png` cropped round its moon and darkened, and a
  plaque carrying a recreated "To the Moon 3DS" wordmark — with the plaque spun
  once every ten seconds. `tools/make_banner_glb.py` writes that scene as a .glb
  itself (a .glb is JSON plus one buffer), so no modelling tool is in the chain;
  `tools/pycgfx/main.py` turns it into the CGFX the menu renders, which
  `bannertool` LZ11-compresses into the CBMD (150 KB uncompressed, 16 KB on disk).
  Both textures are 256x128 power-of-two, the limit above which pycgfx would
  resize them behind our back.
- *The tune, and the platform's three seconds.* `tools/make_banner_tone.py` keeps
  2.8 s of the game's title theme and crossfades the following 0.5 s back over
  its head with square-root gains, so the loop point is inaudible (the wrap's
  step is 1,175 where ordinary steps nearby reach 2,382). The three seconds are
  not taste: 3dbrew's CBMD page says a CXI banner's BCWAV must have two channels
  and be no longer than 3 s, or the menu beeps and clicks or the banner fails to
  load — `tools/make_banner.py` refuses a longer loop. bannertool writes the WAV
  as PCM, so the CWAV is 367 KB; the driver flips its loop flag at `INFO+0x09` —
  the one thing a WAV cannot say — and the menu repeats the phrase.


- *The build.* `build_cia.ps1` packs `work/banner/banner.bnr` when it is there and
  builds the M1 flat banner otherwise, so a checkout without the game still
  builds. The CIA is 129.3 MB (129 MB + the 374 KB banner);
  `tools/inspect_banner.py` reads the banner back out of the ExeFS —
  `animated (CANM)`, 32,768 Hz stereo, 2.80 s, loops 0..91,750 — and the title
  metadata is unchanged (124 MB, 804 MHz, L2, 1 MB stack). That inspector walks
  CBMD, the 14 CGFX offsets, the LZ11 block, `CANM` inside the CGFX and the
  CWAV's INFO block, for a .bnr or a .cia: it is the PC-side answer to "does the
  HOME menu animate and play", which otherwise needs watching the handheld.


**Run 18: the keyboard's minigames, told with the 3DS's buttons (2026-10-23).**
- *The zombie phase* (map 129). Its Plants-vs-Zombies parody shoots with the
  keyboard's W A S D — RGSS's R, X, Y and Z — and the 3DS reported only X and R,
  so two of the four directions could never fire. Its own two instructions
  (`instruction_shootplants`, "PRESS 'W' 'A' 'S' 'D' ON THE KEYBOARD TO SHOOT.",
  and `MG PvZ Overlay Instr`, a picture of a keyboard) taught keys the console
  does not have. Now, while that overlay picture (41) is up, each D-pad direction
  also presses its key — up W, left A, down S, right D — so all four aim. The
  arrows still reach the game, so walking is unchanged, and outside the minigame
  the pad is untouched: `TTMControls.keys_for` (`compat.rb`) wraps the app's key
  source in `main.rb`, which the PC runner does not use (there, WASD is real).
- *The pictures.* Both zombie-phase pictures are repainted as they load: the
  banner keeps its black band and gold capitals ("PRESS A DIRECTION ON THE
  D-PAD TO SHOOT."), and the keyboard becomes a D-pad cross with four white
  arrows, drawn from rectangles because Bitmap has no polygon. The horse's
  "Hold SPACE to gallop" (map 29) becomes "Hold A to gallop" with an A on the
  key cap: its event waits on Button C, which is A here, so only the art was
  wrong.
- *The whack-a-mole's question* (map 71). "Use mouse (Recommended)" transferred
  to map 125, where the mallet follows `Mouse.pos` — a pointer the 3DS reports
  off screen, so the game was unplayable. Both answers now enter map 82 (the
  arrow-key game), as "Use the gamepad (Recommended)" and "Show the controls",
  and the hints it teaches say "D-pad" and "A button" instead of "Arrow keys"
  and "Enter/Space/C".
- *Tests.* `pc/controls_check.rb` **34/34** (18 new: the four keys, two at once,
  the overlay coming and going, all three pictures and map 71's choice); shots
  `pc/shots/controls_shootplants.png`, `controls_mg_pvz_overlay_instr.png`,
  `controls_instruction_horse_gallop.png`. The rest unchanged: `boot_check`
  16/16, `bottom_menu_check` 18/18, `lang_screen_check` 13/13, `minisode_check`
  hs1 and hs2 13/13, `tone_check` 19/19, `effects_check` 16/16, `fileio_check`
  19/19, `lang_check` 47/47. `build.ps1` **0 warnings**; `ttm3ds.cia` stays
  129.3 MB with the run 17 banner, and the 3dsx was rebuilt too.


**Run 19: the ending's Steam achievement hook (2026-10-23).**
- *What the console said.* A run to the credits ended with
  `ERROR #<NoMethodError: undefined method '_steam_achievement_unlock' for
  Interpreter>` and a backtrace through `123_MAIN:2:in _ttm_unlock_achievement`,
  `(eval):1:in eval`, `command_355`: the credit roll's Script Call. The game asks
  Steam for "ACH_DEBUG_MOON"; the PC build gets that function from Steamworks, and
  a 3DS has neither Steam nor the function.
- *Why the game's own guard did not save it.* The call is guarded with `if
  defined? _steam_achievement_unlock`, which is correct CRuby. This mruby has no
  `defined?` keyword: `defined? x` is parsed as a call to a method named
  `defined?` whose argument is evaluated first, so the guard raised on the
  missing Steam function — and would have raised on `defined?` itself once a
  shim existed. Both were reproduced with the PC build before the fix.
- *The fix*, three small pieces and no engine C change:
  - `compat.rb` defines `_steam_achievement_unlock(name = nil)` as a no-op that
    names the achievement once in `log.txt` ("achievement: ACH_DEBUG_MOON (no
    Steam here)"). It also serves `122_Achievements#unlock`, which calls the
    Steam function directly.
  - `ttm_patches.rb` drops the guard around the body in `MAIN` (the call becomes
    unconditional; the method exists in every build of this game).
  - `TTMControls.ending_scripts` (`compat.rb`) rewrites map 167's Script Call as
    the map loads — the body the guard protected, without the guard; its two
    continuation lines become blank.
- *Tests.* `pc/ending_check.rb` **15/15**. It takes the game's own
  `_ttm_unlock_achievement` out of `Data/Scripts.rxdata`, applies the real patch,
  evals and calls it; loads map 167 through `load_data` so the map rewrite runs,
  and evals the event's call; and loads the game's `Achievements` script and
  calls `unlock(:finish)`. All earlier checks unchanged: controls 34/34, boot
  16/16, bottom_menu 18/18, lang_screen 13/13, lang 47/47, minisode hs1/hs2
  13/13, tone 19/19, effects 16/16, fileio 19/19. `build.ps1` **0 warnings**;
  `ttm3ds.cia` is 129.3 MB with the run 17 banner and the run 18 controls.
- *Seen in the same log, not the crash.* At map 160 (the shuttle/bridge montage)
  the texture heap filled: `texture memory full, 1516 textures not made so far`.
  It recovered on the next map, so it did not end the run; if that scene shows
  missing art on the console, that is a separate item (shedding textures when a
  scene is done with them).
- *Follow-up: the animated banner is out.* The first run 19 install showed no
  icon or banner and hung on launch, though the CIA was hash-valid and the
  install complete. A 2.7 MB test title carrying just that banner
  (`3ds-app/ttm3ds_test_anim.cia`) crashed the same way, while the same app with
  the M1 flat banner (`ttm3ds_flat.cia`) booted — so the animated CGFX is what
  this console's HOME menu will not take. `ttm3ds.cia` now carries the flat
  banner and the animated one moved to `ttm3ds_anim.cia` behind
  `build_cia.ps1 -AnimatedBanner`. The log's first line now says `run 19 build`
  and is written **before** `gfxInitDefault`, so a black-screen hang still leaves
  evidence. The looped tune is being cleared separately
  (`3ds-app/ttm3ds_test_tune.cia`); details in `RUN19-STATE.md`.


**Run 20: the launcher, and the minisodes' text (2026-10-24).**
- *Why the minisodes had no dialogue.* They are whole games with no `Fonts/`
  folder, so `boot.rb` falls back to `$ttm_font_file` — and the app pointed that
  at `SD:/3ds/ttm3ds/Fonts/OpenSans.ttf`, which a CIA-only install never has
  (the fonts travel at `romfs:/game/Fonts/`). `Font.ttm_face` stayed nil, so every
  `draw_text` drew zero glyphs: the windows and name tabs appeared, the letters
  did not. `shared_font()` now takes the first that exists — `romfs:/game/Fonts/`
  first, then the card — and boot logs which one it used. The text itself was
  never missing: the PC runs of both minisodes reach their first lines
  (`Dr. Watts — "Hey Eva,"` / `Dr. Eva Rosalene — "Well, a`) and assert the line
  and that it was drawn, 13/13 each.
- *Why one title read "SigCorp Minisode 2.rgssad".* `tools/pack_romfs.py` copied
  only a file named `Game.ini`, and Minisode 2 ships `SigCorp Minisode 2.ini`.
  Every `*.ini` beside an archive is packed now, and the launcher's last-resort
  title drops a `.rgssad` suffix, so no entry can show an extension.
- *The launcher.* The list was `printf` on libctru's 8x8 console. It is citro2d
  now, in the game's navy and gold: a gradient, the crescent, rounded rows with
  the selected one filled gold and a small triangle, scaled system text, and a
  second screen above with the chosen game large and its subtitle ("the main
  game" / "a Sigmund minisode"). Tap a row, or D-pad + A; START+SELECT quits.
  The text console is no longer created at startup — `ensure_console()` makes it
  for the missing-files and error screens — so the two never share the bottom
  screen. The launcher's render targets are deleted before the game starts, and
  `log.txt` records the VRAM either side of that (`ui: launcher targets up/down,
  N KB VRAM free`), and if the targets cannot be made at all the launcher falls
  back to the console list by itself.
- *Tests.* `boot_check` 16/16, `minisode_check` hs1 and hs2 13/13 (their dialogue
  assertions included), `controls_check` 34/34; `build.ps1` **0 warnings**. The
  CIA is 135,328,704 bytes (the flat banner of run 19), the 3dsx 25 MB.

