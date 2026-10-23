# ttm3ds_test — M1 hardware bring-up test

A small homebrew app that puts the **real To the Moon art, the real dialogue box
and the real font** on a 3DS, so the port's biggest open question — *is the
dialogue readable at 3DS resolution?* — gets answered on actual hardware instead
of in theory.

It is also the first toolchain proof: if this builds and runs, devkitARM,
libctru, citro2d, the GPU texture path and romfs all work on your machine and
your console.

## What it shows

The bottom screen always shows three colour-keyed numbers (no font needed), plus a
bar in the bottom-right corner: **green = New 3DS family detected (the 804 MHz clock
boost was requested), red = Old 3DS family**.

| Swatch | Meaning |
|---|---|
| **red** | current mode number |
| **green** | frames per second |
| **blue** | **application RAM this process was given, in MB** — `124` for the 3dsx and the default CIA, `178` if the experimental extended-memory CIA ever starts |

| Mode | Top screen | What it tells you |
|---|---|---|
| 0 | option A: 320×240 centred, 40 px pillarbox each side | the recommended layout; zero game-script changes needed |
| 1 | option A view on top, **dialogue native on the bottom screen** | option B: much more readable text, but needs the game's lines re-wrapped (see below) |
| 2 | option C: 400×240 at 0.625×, cropped | bigger text, but the frame loses its top/bottom composition |
| 3 | native 640×480 scaled to 0.5× **by the GPU** (bilinear) | compare against the offline box filter in modes 0–2 |
| 4 | native 640×480 at 1:1, cropped | reference: the text size the PC version shows |

**Controls:** `A` / `→` = next mode, `B` / `←` = previous, `START` = exit.

Press **START** then the **HOME button** to quit normally.

## Honest note on option B

When the mock-ups were built, option B's native-size text **overflowed the
message box**: To the Moon's dialogue lines were written for the PC window's
448 px text width, so at 22 px on a 320 px-wide screen only ~32 characters fit.
That is why option B is a *phase-2 polish* item in the compatibility matrix — it
needs the game's own scripts to re-wrap text, not just engine work.

## Build

**Both artifacts are already built** — `ttm3ds_test.cia` (2.5 MB) and
`ttm3ds_test.3dsx` (2.3 MB) are in this folder.

```powershell
# rebuild everything: .cia (HOME-menu title, 124 MB) + .3dsx (Homebrew Launcher)
cd 'C:\Users\davao\OneDrive\Desktop\3ds project\3ds-app'
powershell -ExecutionPolicy Bypass -File build_cia.ps1

# .3dsx only, faster to iterate on
powershell -ExecutionPolicy Bypass -File build.ps1

# regenerate the icon/banner/silent-tune first, if you want to change them
python cia\make_cia_assets.py

# EXPERIMENTAL extended-memory build -> ttm3ds_test_178mb.cia (black-screens on
# the test console, see below)
powershell -ExecutionPolicy Bypass -File build_cia.ps1 -SystemModeExt 178MB
```

Full toolchain details are in **`../SETUP-3DS.md`**. `build.ps1` exists because
devkitPro's recipes pass paths unquoted, so the build has to happen in a
space-free directory (`C:\ttm3ds`) rather than in "3ds project".

## Why a CIA and not just the 3dsx

The 3dsx is fine for iterating, but **only an installed title can ask for a
different memory mode**. With `build_cia.ps1 -SystemModeExt 178MB`, `cia/app.rsf`
sets:

```yaml
SystemModeExt : 178MB    # Legacy/124MB/178MB
CpuSpeed      : 804MHz
EnableL2Cache : true
```

On a New 3DS that means **178 MB of application RAM instead of 124 MB** (the
console reboots into extended memory mode when the title launches, like Smash
Bros). The mode lives in the title's extended header, which a `.3dsx` does not
have. `tools/inspect_cia.py` reads it back out of the built CIA to prove it:

```
New 3DS system mode : 2  ->  178 MB  <-- EXTENDED MEMORY MODE
804 MHz clock       : yes      L2 cache: yes
SMDH long title     : 'To the Moon 3DS'
```

**But on hardware it does not start yet.** On the test New 3DS XL the 178 MB CIA
gave black screens, while the identical 124 MB CIA ran. No retail game uses this
"Dev1" mode and the HOME menu does not run in it, so the default build is 124 MB
and extended memory is an open question for later.

## Deploy

**Option 1 — install the CIA (recommended).** Copy `ttm3ds_test.cia` to the SD card
(e.g. `SD:/cias/`) and install it once with **FBI** or **GodMode9**. It then lives on
the HOME menu as *To the Moon 3DS* and launches directly, with 124 MB of RAM.

**Option 2 — the 3dsx.** Copy `ttm3ds_test.3dsx` to
`SD:/3ds/ttm3ds_test/ttm3ds_test.3dsx`, boot, and pick it in the **Homebrew
Launcher**. Faster to iterate on, capped at 124 MB.

Either way there is nothing else to copy: the textures are packed inside the
`.cia`/`.3dsx` romfs. (The app also falls back to
`SD:/3ds/ttm3ds_test/*.ttmtex` if a romfs is not present — that is the pattern the
real port will use for the player's own game data.)

## Layout

```
3ds-app/
├─ source/main.c        the app (single file, ~300 lines, commented)
├─ Makefile             standard devkitpro 3DS makefile (+ SMDH metadata)
├─ build.ps1            builds ttm3ds_test.3dsx
├─ build_cia.ps1        builds the .cia (calls build.ps1, then makerom/bannertool)
├─ build_assets.py      composes the screens -> romfs/*.ttmtex
├─ cia/
│  ├─ app.rsf           title config: SystemModeExt 178MB, 804MHz, permissions
│  ├─ make_cia_assets.py  builds icon.png / banner.png / banner.wav below
│  ├─ icon.png          48x48 HOME-menu icon (the moon)
│  ├─ banner.png        256x128 banner (scene + dialogue box + title)
│  └─ banner.wav        1 s of silence (bannertool requires a tune)
├─ romfs/               generated textures (safe to delete and regenerate)
│  ├─ a_option_a.ttmtex   320x240  option A
│  ├─ b_option_b.ttmtex   320x240  option B (bottom screen)
│  ├─ c_option_c.ttmtex   400x240  option C
│  ├─ full_frame.ttmtex   640x480  native frame
│  └─ digits.ttmtex       160x24   baked 0-9 so no font library is needed
└─ test/stubs/          fake 3ds.h / citro2d.h for host syntax checking ONLY
```

## Two hardware details this app already handles

Both are real porting hazards, documented in the matrix:

1. **PICA200 textures must be power-of-two.** 320×240 and 400×240 are not, so
   each is allocated at 512×256 (and the 640×480 frame at 1024×512) with the
   image in the top-left corner and the visible area selected with UVs.
2. **The 640×480 frame is exactly at the 1024 px texture limit** after rounding,
   which is why the matrix treats the texture limit as a design constraint
   rather than a footnote.

Texture budget on screen at once: ~3.5 MB of pixel data for all five, against
~6 MB of VRAM — already a reason to slice/stream assets rather than load them all.

## Status

- **Builds cleanly** with devkitARM r68 / gcc 16.1.0 + libctru 2.7.0 / citro2d 1.7.0,
  and packages into an installable title with makerom v0.19.0 + bannertool 1.2.0.
- Verified artifacts:
  - `ttm3ds_test.3dsx` (2.3 MB) — romfs confirmed inside (the `TTMRGBA1` magic
    appears 6 times: 5 textures + 1 string literal).
  - `ttm3ds_test.cia` (2.5 MB) — valid CIA header (`0x2020`), NCCH present, romfs
    packed by makerom (6 files, 2.27 MB), SMDH title *To the Moon 3DS*, and the
    extended header reports **SystemMode 1 = 124 MB**, 804 MHz, L2 cache on
    (read back with `tools/inspect_cia.py`). `ttm3ds_test_178mb.cia` is the same
    but reports **SystemMode 2 = 178 MB**.
- Four fixes were needed when this was first built against the real SDK — worth
  knowing if you touch this code:
  1. citro2d's sub-texture type is `Tex3DS_SubTexture` (with `width`/`height`
     fields), not `C2D_SubTexture`.
  2. devkitPro's `%.elf` rule carries **no prerequisites**, so the Makefile must
     state `$(OUTPUT).elf : $(OFILES)` — otherwise make links before compiling and
     `ld` reports "cannot find main.o".
  3. The SMDH icon has to be a prerequisite of the `.3dsx`, or 3dsxtool fails with
     "Cannot open SMDH file".
  4. `bannertool makebanner` rejects an image-only invocation: it also wants a
     tune. `cia/banner.wav` is one second of silence, so the build carries none of
     the game's copyrighted music.
- Two more fixes after the first run on a New 3DS XL showed **black screens**:
  5. `gfxInitDefault()` was missing. Without it GSP and the framebuffers are never
     set up, and `C3D_FrameBegin(C3D_FRAME_SYNCDRAW)` waits forever for a VBlank.
  6. Textures were uploaded as plain RGBA rows. The PICA200 wants 8x8 Morton-order
     tiles with RGBA8 stored as A,B,G,R bytes, and samples tile row 0 at v = 1.0,
     so `tex_load()` now swizzles on load and sub-textures use `top = 1.0`.
- **Runs on hardware** (New 3DS XL, Luma3DS, 2026-10-19): the `.3dsx` and the
  124 MB CIA both draw the scene exactly as the PC mock-up, at 59 fps, reporting
  124 MB and the green New-3DS bar. The 178 MB CIA black-screens (see above).
- No PNG decoder and no font library on the device, by design: every pixel is
  pre-composed on the PC, so a failure here means a toolchain/GPU problem, not
  an asset problem.
- If a texture fails to load, the top screen shows a **red bar** instead of the
  image — that means `romfs` was not packed, not that the GPU failed.
