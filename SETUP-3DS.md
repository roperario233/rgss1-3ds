# SETUP-3DS.md — from nothing to running on your New 3DS XL

Two halves: the **PC toolchain** (builds the app) and the **console** (runs it).

Your console is a **New 3DS XL**: 804 MHz quad-core ARM11, 256 MB FMRAM with
about **124 MB usable by an app** (176 MB in extended-memory mode), PICA200 GPU,
~6 MB VRAM, 400×240 top screen and 320×240 bottom screen. That is the target the
compatibility matrix is written against.

---

## 0. Status: the app is already built — start here

A private devkitPro toolchain was installed on this machine (no admin, no changes
to the system MSYS2 — delete `<devkitPro root>` to remove it entirely):

```
<devkitPro root>\
  pacman.conf, curlx.cmd      repo config + download wrapper (the CDN rejects
                              scripted user-agents, so downloads use curl with a
                              browser UA)
  root\opt\devkitpro\         devkitARM r68 / gcc 16.1.0, libctru 2.7.0,
                              citro3d 1.7.1, citro2d 1.7.0, 3dstools, general-tools
  root\usr\bin\make.exe       MSYS2's make, kept private
  cache\                      91.6 MB of packages (safe to delete)
```

**The build is done and verified.** Two artifacts are already in the project:

```
3ds-app\ttm3ds_test.cia        2.5 MB  INSTALLABLE TITLE: HOME-menu icon, banner,
                                      124 MB, 804 MHz + L2 cache
3ds-app\ttm3ds_test.3dsx       2.3 MB  Homebrew-Launcher build (same app, 124 MB)
3ds-app\ttm3ds_test_178mb.cia  2.5 MB  EXPERIMENTAL extended-memory build
```

**Run on hardware (New 3DS XL, 2026-10-19):** the `.3dsx` and the 124 MB CIA both
show the scene correctly at 59 fps and report 124 MB. The 178 MB extended-memory
CIA **black-screens** on that console. That "Dev1" mode is used by no retail game
and does not run the HOME menu, so it stays experimental
(`build_cia.ps1 -SystemModeExt 178MB`) until it is shown working.

**To rebuild after any change** (one command, from PowerShell):

```powershell
cd '<this checkout>\3ds-app'
powershell -ExecutionPolicy Bypass -File build_cia.ps1    # .cia + .3dsx
# or just the .3dsx:
powershell -ExecutionPolicy Bypass -File build.ps1
```

`build.ps1` checks the toolchain, mirrors the app into a **space-free** directory
(`C:\ttm3ds` — devkitPro's recipes pass paths unquoted, so "3ds project" would
break them), sets `DEVKITPRO`/`DEVKITARM`, runs `make`, and copies the `.3dsx`
back. The full log is written to `3ds-app\build.log`.

**Next: go to Part 3** (copy it to the SD card and run it). Parts 1, 2 and 6 are
the console-side steps and the from-scratch install instructions for another
machine.

---

## Part 1 — the console side

You need custom firmware and a way to launch homebrew:

1. **CFW (Luma3DS + boot9strap)**. If your console is already running Luma3DS,
   you're set. If not, that is a well-documented but separate process
   (3ds.hacks.guide) — it is outside this project's scope and I can't guide you
   through it safely in a document.
2. **Homebrew Launcher** — Luma3DS opens it in place of the **Download Play**
   app (needs `boot.3dsx` in the SD root, which the hacks guide puts there).
   If Download Play opens normally instead: with it open, press
   **L + Down + Select** — *Miscellaneous options* — *Switch the hb. title to the
   current app*, back out, close Download Play and open it again.
3. A way to move files onto the SD card (card reader, or FTP).

**Deploy layout** (do this after building):

```
SD:/3ds/ttm3ds_test/ttm3ds_test.3dsx
```

The Homebrew Launcher scans `/3ds/<folder>/`, so the folder name is what you will
see in the menu. The textures are packed inside the `.3dsx` (romfs), so nothing
else needs copying.

---

## Part 2 — installing devkitPro from scratch (skip if Part 0 applies)

> On this machine the toolchain is already installed in a private root, so you can
> go straight to Part 3. The steps below are for a fresh machine, or if you would
> rather use the official devkitPro installation instead of the private root.

### 2.1 Install

1. Get the installer from
   `https://github.com/devkitPro/pacman/releases/latest` →
   **`devkitpro-pacman-installer.exe`**.
2. Run it (it needs administrator rights) and accept the default location
   `C:\devkitPro`. It installs a private MSYS2 plus devkitPro's `pacman`.
3. Open **devkitPro MSYS2** from the Start menu (not plain MSYS2, not PowerShell)
   and install the 3DS toolchain:

```bash
pacman -Syu            # update the package databases (may need a second run)
pacman -S 3ds-dev      # devkitARM + libctru + citro2d + citro3d + tools
```

4. Check it worked:

```bash
arm-none-eabi-gcc --version
make --version
echo $DEVKITARM
```

`DEVKITARM` should print `/opt/devkitpro/devkitARM`.

### 2.2 Build the test app

**On this machine, use the script** — it finds the private toolchain, mirrors the
app into a space-free directory and copies the result back:

```powershell
powershell -ExecutionPolicy Bypass -File '3ds-app\build.ps1'
```

If you installed devkitPro the official way instead, either point the script's
`$devkitpro` variable at it, or build by hand:

**From the devkitPro MSYS2 shell** (it has `make`, `sh` and the
environment variables already set):

```bash
cd "<this checkout>/3ds-app"
python ../build_assets.py      # regenerate romfs textures (plain Python 3)
make                           # -> ttm3ds_test.3dsx
```

If `python` is not on the MSYS2 path, run `build_assets.py` from PowerShell
first, then `make` from the MSYS2 shell — the assets only need rebuilding when
the mock-up composition changes.

**Alternative: from PowerShell.** This works, but `make` recipes are POSIX shell
scripts, so `sh.exe` must be reachable:

```powershell
$env:DEVKITPRO = 'C:\devkitPro'
$env:DEVKITARM = 'C:\devkitPro\devkitARM'
$env:PATH = "C:\devkitPro\tools\bin;C:\devkitPro\msys2\usr\bin;$env:PATH"
cd '<this checkout>\3ds-app'
make
```

### 2.3 Run it

- **CIA (recommended):** see **Part 3** — one install, then it launches from the
  HOME menu.
- **3dsx:** copy `3ds-app\ttm3ds_test.3dsx` to `SD:/3ds/ttm3ds_test/` and launch it
  from the Homebrew Launcher (open **Download Play**; see Part 1). Quicker to
  iterate on, but capped at 124 MB.

```powershell
# from PowerShell, with the SD card mounted as, say, E:
New-Item -ItemType Directory -Force 'E:\3ds\ttm3ds_test' | Out-Null
Copy-Item '3ds-app\ttm3ds_test.3dsx' 'E:\3ds\ttm3ds_test\'
Copy-Item '3ds-app\ttm3ds_test.cia' 'E:\cias\'
```

---

## Part 3 — install the CIA (recommended: no Homebrew Launcher)

`3ds-app\ttm3ds_test.cia` is a real title. Install it once and it lives on the HOME
menu with an icon ("To the Moon 3DS") and a banner.

1. Copy `ttm3ds_test.cia` to the SD card, e.g. `SD:/cias/ttm3ds_test.cia`.
2. Install it with **FBI** (navigate to the `.cia` — *Install and delete CIA*) or
   **GodMode9** (select the `.cia` — *CIA image options* — *Install game image*).
   This is the only time you need homebrew.
3. It now appears on the HOME menu — launch it from there.

**What to expect on launch:** the scene appears straight away. The **blue number**
on the bottom screen reads **124** (MB of application RAM) for both the CIA and
the 3dsx — that readout is the measurement, not a guess.

`ttm3ds_test_178mb.cia` asks for extended memory (178 MB) instead. On the test
console it gave **black screens** after launch; if that happens, hold the power
button to switch off and reinstall `ttm3ds_test.cia`.

If the console hangs at the logo, or the HOME menu shows a blank banner, the title
was built referencing a service the system lacks; tell me and I will adjust the RSF
(`3ds-app/cia/app.rsf`).

---

## Part 4 — what to look for

| Observation | Meaning |
|---|---|
| App starts, shows the moon scene and a dialogue box | toolchain, romfs, texture upload and the GPU path all work |
| Bottom-left number changes when you press A/→ | input and the frame loop work |
| A second number under it | frames per second (expect ~60: it draws two quads) |
| **Green** bar bottom-right | New 3DS family detected — the 804 MHz clock was requested |
| **Red** bar bottom-right | Old 3DS family, or model detection failed |
| Red bar across the top of the top screen | a texture failed to load — romfs was not packed |

Compare modes with `A`:

- **Mode 0** — option A. Judge the dialogue readability at 1:1.
- **Mode 1** — option B. Note that the text overflows: the game's lines assume a
  448 px text width, so this option needs the game's own scripts changed.
- **Mode 2** — option C. Bigger text, but the frame is cropped.
- **Mode 3** — the GPU's own 0.5× downscale, to compare against the offline box
  filter used in the other modes.
- **Mode 4** — the native 640×480 frame at 1:1, for reference.

Write down what you think of modes 0×2: that decides the port's UI.

---

## Part 5 — notes for later

- **New 3DS clock speed.** The app calls `osSetSpeedupEnable(true)`. Luma3DS also
  has a *New 3DS CPU: Clock+L2* option; enabling it there makes the boost apply
  to everything, not just this app.
- **`.cia` install** (icon on the home menu instead of the Homebrew Launcher):
  needs `makerom`/`bannertool` plus an icon and banner, and a valid ticket. Not
  needed for testing; ask me when you want it.
- **`3dslink`** lets you build-and-run over Wi-Fi instead of copying the SD card:
  `pacman -S 3dstools`, then `3dslink ttm3ds_test.3dsx -a <console-ip>`.
- **If `make` fails**, paste the output to me. First-build nits are expected
  because this app was written against the documented APIs but never compiled
  with devkitARM (the machine it was written on has no devkitPro installed).

---

## Part 6 — what this does *not* do yet

This is the M1 bring-up test, not the game. It proves the pipeline and settles the
resolution question. The game itself needs the engine milestones in
`3DS-COMPAT-MATRIX.md` §6.2 — starting with **M2, the Ruby host spike**, which is
the one unknown that can still invalidate the whole project.
