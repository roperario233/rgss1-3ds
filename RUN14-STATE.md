# Run 14 state (2026-10-23)

Built and verified on the PC; the console checklist is below.

## What is in run 14

- **Frozen frame back (VRAM).** Screen targets are depth-less (525 KB freed), the
  frozen frame is 512x256 RGB565 (256 KB, was 1 MB), and depth-less targets are
  cleared colour-only (`C2D_TargetClear` would clear depth, and citro3d builds
  that fill from a NULL pointer). `log.txt` has
  `gfx: VRAM … KB free after setup (3D on)` and logs any failed allocation.
  Expected: about 742 KB free, no "no VRAM" line.
- **Quiet boot.** With the game found, the version/heap/clock lines go to
  `log.txt` only; `ttm_bottom_start` presents one frame in the menu's dark
  colour (18, 20, 34).
- **Heart-monitor strip.** `TTMBottomMenu::VIEW = [0, 0, 640, 480]` (the whole
  menu at 0.5), plus the game's own `Window_MenuHeartMonitor` made inside
  `Graphics.ttm_on_bottom`. The strip follows game variable 72 (era 0..4) and
  scrolls its overlay by variable 73; an era change remakes it.
- **Simple 3D.** Left/right top targets, `gfxSet3D(true)`, whole frame at x-d /
  x+d, `TTM_DEPTH_PX = 10` at full slider. At 0 it is flat; the bottom is flat.
- **CIA.** `engine/cia/app.rsf` + `engine/build_cia.ps1` -> `engine/ttm3ds.cia`
  (24.2 MB), 124 MB mode, 804 MHz + L2, StackSize 0x100000, UniqueId 0xff3fe,
  services `dsp::DSP` and `ptm:sysm` added to the M1 list.

## Verified on the PC

- `pc/bottom_menu_check.rb`: **18/18** (strip geometry, era pictures, remake on
  variable 72 = 3, restore, and all the stylus flows).
- `pc/tone_check.rb` 19/19, `pc/effects_check.rb` 16/16,
  `pc/boot_check.rb` 16/16, `pc/controls_check.rb` 16/16.
- `build_mruby.ps1` and `build.ps1` clean; `build_cia.ps1` OK;
  `tools/inspect_cia.py`: 124 MB, 804 MHz, L2, "To the Moon 3DS";
  the CIA contains `dsp::DSP` and `ptm:sysm`.
- `pc/shots/bottom_characters.png`: the strip is at the top, showing era 2
  ("PATIENT OFFLINE" as it scrolls), over the half-size menu.

## To check on the console

1. Boot: bottom screen dark, no text.
2. `SD:/3ds/ttm3ds/log.txt`: `gfx: VRAM … KB free` and no "no VRAM" line.
3. Scene changes fade again: title -> new game, and a map transfer that uses a
   *named* transition (the mask path runs for the first time).
4. The bottom menu shows the strip (the era's pictures, the overlay moving) and
   the tabs / Save / Load / Exit Game still take taps.
5. The strip changes after the story sets variable 72 (e.g. Lily's talk).
6. 3D slider: the picture sits behind the screen; at 0 flat; `perf:` `dr` close
   to run 13.
7. `ttm3ds.cia` installs with FBI, launches from HOME, has sound, and Exit Game
   > Shutdown returns to HOME. The 804 MHz clock is kept after a return from
   HOME (`ptm:sysm` is granted).

## Next

1. Languages.
2. The Holiday Specials.
3. Layered 3D (text and menus in front of the map).
