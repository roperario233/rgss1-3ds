# Run 14 handoff: heart-monitor strip, clean boot screen, transitions fixed, CIA, simple 3D

Read this before you touch the code. It assumes you have not seen the earlier conversation.

## The project in brief
- **What it is:** an RPG Maker XP (RGSS1) runtime for the New 3DS. It runs the player's own copy of *To the Moon* on mruby, with C gems for graphics and audio (citro2d and citro3d on the console). Root: `<this checkout>\`. The engine is in `engine/`. Read `engine/README.md` first.
- **Key files for this task:**
  - `engine/gems/mruby-ttm-gfx/src/backend_c3d.c`: the 3DS renderer (VRAM, screens, freeze and transition).
  - `engine/gems/mruby-ttm-gfx/src/graphics.c`: the draw passes, including the bottom-screen pass and `Graphics.ttm_bottom_view`.
  - `engine/source/main.c`: app startup, console, log.
  - `engine/romfs/rgss/ttm_patches.rb`: `TTMBottomMenu`, the game's menu on the bottom screen.
  - `engine/pc/bottom_menu_check.rb`: the PC test for that menu.
  - `work/scripts/111_GUBID-TTM Menu.rb`: the game's own menu script, for reference only (the engine ships none of the game's code).
  - `3ds-app/build_cia.ps1`, `3ds-app/cia/app.rsf`, `3ds-app/cia/banner.png|banner.wav`: the working CIA pipeline from milestone M1.
- **Builds** (PowerShell):
  - `engine/build.ps1` builds `engine/ttm3ds.3dsx`, mirroring the sources to `C:\ttm3ds_engine` because devkitPro can't handle spaces in paths.
  - The PC checks run with `<devkitPro root>\build\engine\host\bin\mruby.exe pc\<check>.rb`, from inside `engine/`.
  - The CIA tools are under `<devkitPro root>\tools\cia\`.
- **How the user tests:**
  - There is no emulator. The user runs each build on a real New 3DS XL (Luma3DS, FBI, Homebrew Launcher) and sends phone photos.
  - Verify everything you can on the PC first. On the console, put key numbers in `SD:/3ds/ttm3ds/log.txt`.
  - 178 MB "extended memory" CIAs black-screen on this console, so CIAs must use **124MB**.
- **Rules:**
  - Game patches must not change the game's behaviour.
  - Builds that contain the game's music (`audio.ttma` in romfs) are for the user's own console only. Never publish or upload them.

## What the user reported (run 13 on hardware) and decided
1. **The bottom-screen menu is "almost perfect" but has no heart-monitor strip.** `TTMBottomMenu::VIEW = [96, 90, 448, 342]` at 0.7 leaves it out on purpose.
   - The game's menu draws the strip with `Window_MenuHeartMonitor` at y 0×72, using pictures `heart<v72>`, `heartoverlay<v72>` and `heartoveralygraphic<v72>` (sic). It scrolls by game variable 73.
   - The story changes variable 72, e.g. Map002 event 23 (after Lily's "Tommy and Sarah can show you around") sets it to 2. Values 0 to 4 exist as pictures.
   - **User's choice: show the game's whole menu screen at exactly half size**, with the strip on top, and have it follow variable 72.
2. **Something "pops out" on the bottom screen at the opening.** It is the text console showing the boot lines and `gfx: no VRAM for the frozen frame: transitions not drawn`. It stays up from launch until the first frame.
   - The message is a real bug: `Graphics.freeze`/`transition` fades are not drawn on the console at all.
   - **Likely cause (unverified; confirm with the new log line):** VRAM is two 3 MB banks, and an allocation cannot span both. Each bank already holds one 2 MB frame texture plus a top-screen colour or depth buffer, so neither bank has the 1 MB that the 1024×512 RGB565 frozen-frame texture needs (`backend_c3d.c` `ttm_gfx3ds_init`).
3. **The user wants a CIA** that launches from the HOME menu without the Homebrew Launcher.
4. **The user asked whether 3D works.** **User's choice: simple depth now.** The whole picture sits behind the screen, scaled by the 3D slider. Layered depth (text and menus in front of the map) is a later phase, after languages.

## Work, in order

### 1. VRAM: get the frozen frame back and make room for a right-eye target (`backend_c3d.c`)
- **Screen targets without depth buffers.** The frame targets are already depth-less (`frame_target(..., -1)`) and citro2d draws into them fine on hardware.
  - Add `screen_target(gfxScreen_t, gfx3dSide_t, int h)`: `C3D_RenderTargetCreate(240, h, GPU_RB_RGBA8, -1)`, then `C3D_RenderTargetSetOutput` with the display-transfer flags `C2D_CreateScreenTarget` uses (check citro2d's `base.c` in `<devkitPro root>/root/opt/devkitpro`: RGBA8 in, RGB8 out, no scaling).
  - Use it for the top-left target (replaces `C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT)`), the new top-right target, and the bottom target in `ttm_bottom_start`.
- **Smaller frozen frame: a 512×256 RGB565 texture** (256 KB instead of 1 MB). The top screen shows at most 400×240, so a fade from it looks the same.
  - `gfx_frame_begin`'s freeze copy draws `frame_tex[cur]` (640×480) scaled into it (0.8 — 0.533).
  - Both paths in `gfx_frozen_draw` (plain fade, and mask transition) draw it scaled back up to 640×480.
  - Generalize `draw_frame_part` to take the texture size, or add a small `draw_frozen()`.
- **Expected budget:** about 5.4 of 6 MB, each bank under 3 MB.
  - Frames: 2 — 2048 KB.
  - Top-left, top-right and bottom targets: 384 + 384 + 300 KB.
  - Frozen frame: 256 KB.
- **Logging:**
  - `ttm_gfx3ds_init` logs `gfx: VRAM <vramSpaceFree()/1024> KB free after setup`, and any failed allocation, through `ttm_gfx_log_fn`.
  - In `main.c`, set `ttm_gfx_log_fn` and `ttm_audio_log_fn` **before** calling `ttm_gfx3ds_init()` (today they are set after it).
  - Failure lines go to log.txt, not the console.

### 2. A clean bottom screen at boot (`main.c`, `backend_c3d.c`)
- When the game is found, the boot lines (version, heap, clock) go only to `log.txt`. The console stays for the missing-files message and for errors.
- `ttm_bottom_start()` presents one cleared frame on the new bottom target, in the menu's dark colour `C2D_Color32(18,20,34,255)`: `C3D_FrameBegin` — `C2D_TargetClear` — `C2D_SceneBegin` — `C3D_FrameEnd`. The bottom screen is then dark from launch until the title.
- Change the boot banner and log string to "run 14 build".

### 3. Heart-monitor strip on the bottom screen (`romfs/rgss/ttm_patches.rb`, `TTMBottomMenu`)
- Set `VIEW = [0, 0, 640, 480]`, so `Graphics.ttm_bottom_view` returns `[0.5, 0, 0]`. The stylus mapping already uses `@scale/@dx/@dy`, so nothing else changes there.
- Add `make_monitor`, called in `initialize`. Inside `Graphics.ttm_on_bottom { @monitor = Window_MenuHeartMonitor.new }` it makes the strip with the game's own class and remembers `@heart = $game_variables[72]`.
  - Wrap it in `rescue`: on a missing picture, log once (`ttm_log`) and leave the strip out instead of crashing.
- In `update`: if `$game_variables[72] != @heart`, dispose and remake the monitor. The game's `refresh` makes new sprites without freeing the old ones, so do not call `refresh`. Then call `@monitor.update`, where the game's code scrolls the overlay by variable 73.
- `dispose` frees `@monitor` too.
- Update the class header comment, which says the strip "is left out", and the matching `engine/README.md` text (around line 105, plus the touch-screen section).

### 4. Simple 3D (`backend_c3d.c` `ttm_gfx3ds_init`, `gfx_frame_end`)
- At init: `gfxSet3D(true)`, then make `top_r = screen_target(GFX_TOP, GFX_RIGHT, 400)`.
- Each frame:
  - `float d = osGet3DSliderState() * TTM_DEPTH_PX / 2`, with `#define TTM_DEPTH_PX 10` (total parallax at full slider; tune it after the user looks).
  - Clear both top targets.
  - Draw the frame to the left eye at x — d and to the right eye at x + d, in both layouts (stretched 400×240, and 4:3 at x = 40).
  - At slider 0 both eyes are identical, so the picture is flat.
- The bottom screen stays flat.
- The cost is one extra textured quad and one extra display transfer per frame. Watch `dr` on the `perf:` lines.
- Document it in the README as "simple depth; layered 3D later".

### 5. The CIA (new: `engine/cia/app.rsf`, `engine/build_cia.ps1`)
- **`engine/cia/app.rsf`**: copy `3ds-app/cia/app.rsf` and change it as follows.
  - Title `To the Moon 3DS`, ProductCode `CTR-P-TTM3`, **UniqueId `0xff3fe`**. The M1 test app keeps `0xff3ff`, so both can be installed.
  - SystemModeExt **124MB**, hard-coded or defaulted. CpuSpeed 804MHz, EnableL2Cache true.
  - **Add `dsp::DSP` to ServiceAccessControl.** The engine's sound uses NDSP, and the M1 list lacks it.
  - **`StackSize: 0x100000`.** A CIA takes its main-thread stack from the exheader, not `__stacksize__`, and mruby's parser needs 1 MB (the M1 value was 0x40000).
  - Keep `DirectSdmc`. Game files and saves stay in `SD:/3ds/ttm3ds/`, shared with the 3dsx.
- **`engine/build_cia.ps1`**, modeled on `3ds-app/build_cia.ps1`:
  1. Run `build.ps1`. It leaves `ttm3ds.elf`, `ttm3ds.smdh` and `romfs/` (with `audio.ttma`) in `C:\ttm3ds_engine`; confirm the `.smdh` is produced.
  2. Run bannertool on `3ds-app/cia/banner.png` and `banner.wav`.
  3. Run makerom with `-f cia -target t -exefslogo -elf -icon -banner -rsf`, from inside the scratch directory because of `RootPath: romfs`.
  4. Copy the result to `engine/ttm3ds.cia` and write `engine/build_cia.log`.
- Check it with `python tools/inspect_cia.py engine/ttm3ds.cia`: 124 MB, 804 MHz, the right title.
- In the README's "Run it on the 3DS" section, add the CIA route:
  - Install it with FBI. It launches from HOME, and Exit Game > Shutdown returns to HOME.
  - Like the 3dsx, it carries the game's music, so it is for this console only.
  - The 3dsx stays for quick iteration.

## Verification
- **PC:**
  - Extend `pc/bottom_menu_check.rb`:
    - The strip exists, is on the bottom screen, and lies within 320×240 at 0.5.
    - Setting `$game_variables[72] = 2` remakes it with the `heart2` pictures.
    - Taps on the tabs, Save, Load and Exit Game still land.
    - Then look at `pc/shots/bottom_*.png`.
  - Rerun `tone_check.rb`, `effects_check.rb`, `boot_check.rb` and `controls_check.rb`. They should give the same results as before.
- **Build:** `build.ps1` must compile with no new warnings in `build.log`. Then run `build_cia.ps1` and `inspect_cia.py`.
- **Hand to the user (run 14 checklist, with exact SD paths):**
  1. At boot, the bottom screen is dark with no text.
  2. `log.txt` has the `gfx: VRAM … free` line and no "no VRAM" line.
  3. Scene changes fade again (title — new game, map transfers).
  4. The bottom menu shows the strip with Johnny's bed and a moving heartbeat, and taps still land.
  5. The strip changes after Lily's talk upstairs.
  6. With the 3D slider up, the picture sits behind the screen; at 0 it is flat. `perf:` fps is about the same as run 13.
  7. `ttm3ds.cia` installs with FBI and launches from HOME, sound plays, and Exit Game > Shutdown returns to HOME.
- Afterwards, update the memory note `run11-audio-pending.md` (run 14 state; next: languages, then the Holiday Specials, then layered 3D).
