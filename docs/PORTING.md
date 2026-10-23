# Porting another RPG Maker XP game

The engine is not a To the Moon emulator: it implements **RGSS1** (RPG Maker XP)
and runs whatever the game's own scripts ask of it. To the Moon is the game it was
built against, so its needs are the ones that are certainly covered — everything
else is a matter of finding the gap and filling it.

## Is your game a candidate?

| | |
|---|---|
| **Yes** | RPG Maker XP projects: `Game.rgssad` (or plain `Data/*.rxdata`), RGSS1 scripts |
| **No** | RPG Maker VX / VX Ace (RGSS2/3 — different API, different data), MV/MZ (JavaScript) |
| **Maybe** | heavily scripted XP projects using Windows DLLs, Steam, or their own C extensions |

## The 20-minute check

You do not need a console to find out whether a game boots:

1. Copy the game into `work/game/` (the archive, `Fonts/`, `lang.dat` if it has one).
2. `engine\build_mruby.ps1` once, then run the harness:
   `mruby.exe pc\run_pc.rb` from `engine\` — it boots the game, drives it with the
   confirm key, and saves screenshots to `pc\shots\`.
3. Read `work/pc_data/` (saves it writes) and the console output. Most failures are
   a missing Windows API, a Ruby 1.8 syntax form, or a nil the game never expected.

Only when the PC run looks right is it worth building a 3dsx/CIA.

## What a new game usually needs

1. **Fonts.** If the game ships `Fonts/`, the first `Font` name loads from there;
   otherwise the app's shared face (`shared_font()` in `engine/source/main.c`) is
   used. A game that draws its own text into pictures needs nothing.
2. **Audio.** `python tools\pack_audio.py <folder or zip>` → `audio.ttma` (about 60%
   of the original size). Without it the game runs silent.
3. **Windows-only scripts.** `engine/romfs/rgss/compat.rb` already stubs `Win32API`,
   `user32` cursor calls, `MKXP`, `ENV['AV_APPDATA']`, and the Steam achievement
   hook. A game using another DLL (a minigame engine, a video player) needs a Ruby
   stub with the same effect, and `ttm_patches.rb` is where load-time rewrites go.
4. **Ruby 1.8 syntax.** Older projects use forms mruby refuses; `TTMPatch.legacy`
   rewrites them and logs `legacy syntax: …`. Anything it cannot fix shows up as a
   `SyntaxError` with the script name.
5. **`defined?`** — mruby here has no `defined?` keyword: `defined? x` is parsed as
   a call to a method named `defined?` whose argument is evaluated first, so a guard
   like `if defined? foo` raises when `foo` is missing. The fix is to remove the
   guard (see the Steam hook patch in `ttm_patches.rb`).
6. **The screen.** RGSS1 is 640×480; the app draws it stretched to 400×240 or 4:3
   with SELECT. Games that read `Graphics.width`/`height` are fine.
7. **Memory and speed.** The texture heap is the wall on long sessions; the engine
   skips a texture rather than crashing, and logs `texture memory full`. Ruby runs
   ~25× slower than on a PC, so 40 fps with frame skip is the target; anything the
   game does per frame in Ruby is the first thing to look at.

## Traps this port hit (worth knowing before you start)

| Symptom | Cause | Fix |
|---|---|---|
| `undefined method 'x' for nil` on the first map | a script assumes the party/actors exist | guard the lookup (the minisodes needed this) |
| Dialogue boxes with no text | no font was loaded (game has no `Fonts/`, shared path wrong) | `shared_font()`; check `log.txt` for `font:` |
| Everything draws but nothing animates | `Bitmap#blt`/`update` timing differences | compare against the PC harness output |
| `nil cannot be converted to Integer` | old mouse script reading a Win32 cursor | replace with `Input.mouse_x/y` |
| Game hangs at credits | a Steam call on a guarded line | stub `_steam_achievement_unlock`, drop the guard |
| `legacy syntax: …` in the log | Ruby 1.8 hash literals or `foo (a, b)` | handled automatically, but check the log |

## Testing on hardware

- `log.txt` is the truth: it records the archive index, scripts compiled, every map
  load with its time, `gfx:` texture lines, `perf:` every five seconds, and any
  `ERROR` with a backtrace.
- The PC harness (`engine/pc/*.rb`) is the contract; add a check for the behaviour
  you fixed, the way `ending_check.rb` does for the credits.
- Record what you tried in `docs/COMPATIBILITY.md` and open a
  [port report](../../.github/ISSUE_TEMPLATE/port-report.md) with the log tail.

## Please do not

- Publish builds that contain a game's data. The engine CIAs that carry the game
  (and its music) exist only on machines that own it.
- Open a PR with game files: `tools/check_repo.py` and CI reject them.
