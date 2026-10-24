# RPG Maker XP on the Nintendo 3DS — an RGSS1 runtime

*An AI-assisted port. To the Moon is its reference game.*

An **RGSS1 runtime** that runs a game's own scripts, data and audio on the New 3DS:
mruby for Ruby, C for the graphics (citro2d/citro3d) and the sound (NDSP). The app
carries **none** of the game's files: you supply your own copy, and it runs
unchanged apart from a handful of load-time patches.

*To the Moon* is the reference title: it boots, plays, saves, loads, finishes (credits
and epilogue included), and the two Sigmund minisodes run as games of their own. The
same app is built to run **any** RPG Maker XP project — see
[Bring your own game](#bring-your-own-game).

---

## How this was built — AI-assisted, and what that means

This port was pair-programming with an AI agent. I set the goals, made the design
calls, and tested every build on a real New 3DS XL; **the engine's C, the Ruby
compatibility layer, the test harness and all of this documentation were written
with Cline driving Claude**, across ~20 documented “runs”. There is no emulator in
the loop, so a change only counted when the console's `log.txt` or a photo said it
worked.

What that means if you build on it:

- **The tests are the specification.** ~250 assertions in `engine/pc/` run without
  a console (`run_pc.rb`, `minisode_check.rb`, `controls_check.rb`,
  `ending_check.rb`, `lang_check.rb`, …). They found most of this port's bugs
  before hardware did.
- **The paper trail is real.** `RUN14` … `RUN20` record each fix next to the console
  log that found it — e.g. the ending's
  `undefined method '_steam_achievement_unlock'`, and the minisodes' invisible text.
- **AI-written code can be confidently wrong.** Trust the tests and the logs over
  the prose — including this README.

## Install

**Engine-only CIA (recommended):**

![QR code for the engine CIA](docs/qr-install.png)

1. On the 3DS open **FBI → Remote Install → Scan QR** and scan the code above (or
   copy `ttm3ds-engine.cia` from the
   [latest release](https://github.com/roperario233/rgss1-3ds/releases/latest) to the
   SD card and pick it in FBI).
2. Launch **RGSS1 3DS** from the HOME menu once: it will say no game was found.
3. Copy your own game into `SD:/3ds/ttm3ds/<folder>/` — the archive and its
   `Game.ini` (and `Fonts/`, `lang.dat`, `audio.ttma` if you have them); the
   launcher then lists it. Saves go to `SD:/3ds/ttm3ds/games/<folder>/`.

**Or build it yourself** — [docs/BUILD.md](docs/BUILD.md) walks from a fresh clone
to a CIA, including a build that carries your game inside the app (which is how the
reference CIA is made, and why those binaries are never published).

## Bring your own game

**Supported:** RGSS1 / RPG Maker XP archives (`*.rgssad`). **Not supported:** VX and
VX Ace (RGSS2/3 — a different API and data format).

What usually needs work for a new game:

| Subsystem | Where to look |
|---|---|
| Windows-only scripts (`Win32API`, `user32`, DLLs, Steam) | stubs in `engine/romfs/rgss/compat.rb`; every one To the Moon needed is listed in [3DS-COMPAT-MATRIX.md](3DS-COMPAT-MATRIX.md) |
| Fonts | the game's `Fonts/OpenSans.ttf`, else the app's shared face |
| Audio | `python tools/pack_audio.py <game folder or zip>` → `audio.ttma` |
| Ruby 1.8 syntax (older projects) | rewritten at load (`engine/romfs/rgss/ttm_patches.rb`, `legacy`) |
| Screen, memory, speed | 640×480 only; New 3DS 124 MB mode; 40 fps target with frame skip |

Workflow: run the PC harness first (`engine/pc/run_pc.rb`) — it needs no console and
catches most of it — then `build.ps1`, then `build_cia.ps1`. The long version,
including the traps this port hit, is [docs/PORTING.md](docs/PORTING.md).

Games already tried are recorded in [docs/COMPATIBILITY.md](docs/COMPATIBILITY.md) —
add yours with a [port report](.github/ISSUE_TEMPLATE/port-report.md).

## What runs today

- **The whole game**, from the title screen to the credits and the epilogue,
  including its saves (RGSS `Marshal` read *and* written here), the heart-monitor
  menu on the bottom screen, simple stereoscopic depth from the 3D slider, and
  scene fades.
- **11 languages** from the game's own `lang.dat`, with the game's language screen
  and per-language fonts (Korean and Chinese draw in their own faces).
- **Both Sigmund minisodes** as independent games, with their own saves folder.
- **A launcher** for the games it finds (card and built-in), with the last choice
  remembered.
- **A HOME-menu banner** on the tile, plus 804 MHz + L2 cache and the 124 MB New
  3DS mode; the animated banner is opt-in because this console's HOME menu refused
  it (`RUN19-STATE.md`).
- **~250 PC assertions** that run without a console, plus the `RUN*.md` records of
  every console bug and its fix.

## Known limits

- **New 3DS / New 2DS XL only** for now (804 MHz, L2, 124 MB mode). An Old 3DS
  profile is designed, not built.
- **RGSS1 only** — VX / VX Ace are out of scope.
- **640×480** RGSS1 screen, drawn to the 3DS's 400×240 top screen (stretched, or
  4:3 with SELECT).
- **Texture memory** is the binding constraint on long sessions: the shuttle/bridge
  montage fills it (`RUN19-STATE.md`); the engine skips rather than crashes.
- **Steam achievements** are a logged no-op; **DLL minigames** need porting.
- **`tools/pycgfx/` is not in this repo** (it has no license). `tools/make_banner.py`
  needs it for animated banners; the flat banner needs nothing.

## Build

```powershell
# once: mruby + the C gems for the PC and the 3DS (needs devkitPro, Ruby, curl/tar)
powershell -ExecutionPolicy Bypass -File engine\build_mruby.ps1

# the app: engine\ttm3ds.3dsx (Homebrew Launcher), then the CIA
powershell -ExecutionPolicy Bypass -File engine\build.ps1
powershell -ExecutionPolicy Bypass -File engine\build_cia.ps1

# the PC harness, no console needed (needs your own copy of the game: work/game/,
# or one line in work/game_dir.txt)
powershell -ExecutionPolicy Bypass -File tools\run_checks.ps1
```

Paths default to this machine's layout and can be overridden with `TTM_DEVKITPRO`,
`TTM_STRAWBERRY` and `TTM_RUBY`. Details, including where the game's files go:
[docs/BUILD.md](docs/BUILD.md).

## Where everything is

| Path | What |
|---|---|
| `engine/` | the runtime: `gems/` (archive reader, Marshal, graphics, audio in C), `romfs/rgss/` (the Ruby side), `source/main.c`, `pc/` (the harness) — start with [engine/README.md](engine/README.md) |
| `3ds-app/`, `ruby-spike/` | the two earlier milestones (hardware path, mruby as the Ruby host) |
| `tools/` | asset packers, the banner pipeline, the repo guard, the QR generator |
| `3DS-COMPAT-MATRIX.md` | what To the Moon needed and why — the porting map |
| `docs/` | build, porting, compatibility, install, AI workflow |
| `RUN14` … `RUN20` | the console-driven fix trail |

## Not affiliated, and no game data

*To the Moon* is © Freebird Games; RPG Maker XP / RGSS is © Gotcha Gotcha
Games / Enterbrain. This project is not affiliated with or endorsed by either.
**No game data, art, music, fonts or text is included** — build outputs that carry
them (the 129 MB CIA, the audio pack, the banner made from the game's sky) stay on
the machine that owns the game and are never published; `tools/check_repo.py` and
CI enforce that.

Third-party components (mruby, stb, Onigmo) and their licenses: [THIRD-PARTY.md](THIRD-PARTY.md).

## License

[MIT](LICENSE) — for this project's own code. The game and RPG Maker are not
covered by it.

