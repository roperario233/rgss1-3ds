# ruby-spike — M2: can mruby host the game's Ruby on a New 3DS?

Milestone M2 in `../3DS-COMPAT-MATRIX.md` §6.2: the highest-risk unknown of the
port. The game's logic is 124 Ruby scripts (850 KB), so the engine needs a Ruby
that fits the 3DS. This spike tries **mruby 4.0.0** first (small, embeddable, and
already shown to build for 3DS); CRuby is the fallback if mruby falls short.

## What is here

| File | Role |
|---|---|
| `build_config_3ds.rb` | mruby build: a **PC** build (mrbc + mruby, for testing) and a **3DS** build (`libmruby.a`), with the same 36 gems |
| `build_mruby.ps1` | builds both, using a private Ruby + rake (see below) |
| `romfs/spike.rb` | **part 1**: language checks + micro-benchmarks, RGSS-shaped; runs unchanged on PC and 3DS |
| `romfs/spike2.rb` | **part 2**: 600-frame map-shaped workload at the game's live-set size, incremental vs generational GC |
| `source/main.c` | the 3DS host: menu, starts mruby, compiles the script **on the device**, times parse / codegen / run, counts mruby's memory exactly |
| `build.ps1` | builds `ruby_spike.3dsx` |
| `check_game_scripts.rb` | compiles the game's own scripts with mruby (and CRuby, as a reference) on the PC |
| `count_live_set.rb` | counts the Ruby objects the game keeps alive (database, each map, translations) |
| `gems/mruby-onig-regexp-3ds/` | Regexp for mruby: upstream mruby-onig-regexp + Onigmo 6.2.0, built without `configure`, plus one upstream fix (`patch_upstream.rb`) |
| `gems/mruby-ttm-core/` | engine core in C: `.rgssad` archive reader, `Marshal.load` (format 4.8), zlib inflate, `TTM.load_script`, µs timer, allocation counter |
| `patches/mruby_gc_alloc_counter.rb` | adds an object-allocation counter to mruby's `gc.c` (applied by `build_mruby.ps1`) |
| `romfs/rgss_headless.rb` | **part 3**: RGSS1's built-in classes without graphics/sound, the `RPG` data module, and the shims the game needs on mruby |
| `romfs/harness.rb`, `romfs/part3.rb` | **part 3**: boots the game's own scripts from the archive and times real map frames |
| `test_marshal.rb` | checks `Marshal.load` + archive reader against CRuby on all 180 data files |
| `profile_pc.rb` | part 3 on the PC with a per-phase profile and allocations by type |

## Run it on the 3DS

Copy `ruby_spike.3dsx` to `SD:/3ds/ttm3ds_ruby/ruby_spike.3dsx` and start it
from the Homebrew Launcher (open **Download Play**). A menu appears:

- **X — part 3**, the game's own scripts (about 3 minutes). Needs the game's
  archive copied from **your own copy** of the game to
  `SD:/3ds/ttm3ds_ruby/To the Moon.rgssad` (36.5 MB; the file of that name in
  the game's install folder). Nothing from the game is in the app.
- **A — part 2**, frame times under the GC (about 2 minutes; lines appear as it goes)
- **Y — part 1**, language checks (about 5 seconds)
- **START** — exit

Each run shows its lines on the top screen and a summary on the bottom screen,
and saves everything to `SD:/3ds/ttm3ds_ruby/result_part<N>.txt`.

## Rebuild

```powershell
cd 'C:\Users\davao\OneDrive\Desktop\3ds project\ruby-spike'
powershell -ExecutionPolicy Bypass -File build_mruby.ps1   # once, ~2 min: mruby for PC + 3DS
powershell -ExecutionPolicy Bypass -File build.ps1         # the .3dsx

# run the same test on the PC, and the game-script compile check
$m = 'C:\Users\davao\devkitpro\src\mruby-4.0.0\build\host\bin\mruby.exe'
$r = 'C:\Users\davao\devkitpro\tools\rubyinstaller-3.4.11-1-x64\bin\ruby.exe'
$env:PATH = "C:\Strawberry\c\bin;$env:PATH"
& $m romfs\spike.rb
& $r check_game_scripts.rb
```

Toolchain additions (private, no admin, nothing installed system-wide — delete the
folders to remove them):

```
C:\Users\davao\devkitpro\tools\rubyinstaller-3.4.11-1-x64\   Ruby 3.4.11 + rake (mruby's build system)
C:\Users\davao\devkitpro\src\mruby-4.0.0\                   mruby source; build\host and build\3ds
```

The PC build compiles with Strawberry Perl's MinGW gcc (`C:\Strawberry\c\bin`).

## Results so far (PC side, 2026-10-19)

**The game's scripts compile under mruby: 124/124 (850 KB)**, with no Ruby 1.8-only
syntax anywhere — CRuby 3.4 also parses all 124. The parser is not a blocker.

**`spike.rb`: 13/13 checks pass on mruby**, matching CRuby 3.4 exactly (CRuby was
used to confirm every expected value). Covered: class/`super`/`alias` chaining
(the RGSS add-on idiom), modules and `Comparable`, Hash/Array work, `format` and
string building, an RGSS `Table` with `_dump`/`_load` via `pack`, `rescue`/`retry`/
`ensure`/`catch`, `eval` inside an object (the event *Script* command),
`instance_variable_get/set`, `define_method`, integer/float semantics, and
integers past 32 bits.

**What the engine must supply** (not in mruby, needed by the game):

| Missing | Used by the game | Plan |
|---|---|---|
| `Regexp` | 178 literals in 8 scripts — 161 in `097_UMS new` (the message system's text codes), 7 in `055_Window_Message` | **done in part 2:** `gems/mruby-onig-regexp-3ds` (Onigmo) |
| `Marshal` | 56 references in 5 scripts (save/load, utilities) — and every `.rxdata` | write `Marshal.load/dump` for format 4.8 in C (M5 already plans this) |
| `Hash#index`, `Array#nitems`, `Object#type`, `String#each` | Ruby 1.8 names | small Ruby shims, if the scripts use them |

**Ruby 1.8 semantics that differ** (mruby behaves like modern Ruby): `str[0]` and
`?a` give a String, not an Integer; `String#size` counts characters. mkxp-z faces the
same with CRuby 3, so this is a script-shim question, not an mruby one.

**Size:** the 3DS app is 947 KB of code, of which mruby — VM, on-device compiler and
36 gems — is about **0.75 MB**.

## Results on hardware (New 3DS XL, .3dsx via HBL, 2026-10-19)

Raw log: `results/n3dsxl-2026-10-19.txt`. CPU confirmed at **804 MHz + L2**;
heap available **91 MB** (+ 32 MB linear).

| Measure | 3DS | PC (host mruby) | 3DS ÷ PC |
|---|---|---|---|
| checks | **13/13** | 13/13 | — |
| `mrb_open` | 2.6 ms, 55 KB | — | — |
| compile (parse + codegen) | **678 KB/s** → the game's 850 KB in **~1.3 s** | 23,826 KB/s | 35× |
| 300k loop iterations (block + 3 calls each) | 1,341 ms (~1.1 µs per call) | 33 ms | 41× |
| 100k `Game_Battler.new` (2 objects, 4 ivars each) | 1,590 ms (~16 µs each) | 34 ms | 47× |
| frame sim (40 sprites + 60 event commands) | **1.40 ms/frame** of a 25 ms budget | 0.03 ms | 47× |
| full `GC.start`, 200k live objects | **416 ms** (~2 µs per live object) | 10 ms | 42× |

**Verdict: mruby is viable as the host.** It runs the language surface the game
uses correctly, starts in milliseconds, costs 0.75 MB of code, and can compile all
of the game's scripts at boot in about a second (or cache bytecode and skip that).
There is no reason to fall back to CRuby for compatibility.

**The open risk is performance, chiefly GC pauses.** The 3DS runs mruby ~40–47×
slower than this PC. A light frame costs 1.4 ms, but a real map frame does much
more. A stop-the-world collection costs ~2 µs per live object — 416 ms at 200k.
RGSS keeps only the current map plus the database live (the whole game has ~70k
event commands across 166 maps, §3 of the matrix), so the real live set is likely
tens of thousands of objects, i.e. a ~50–100 ms full collection. mruby's GC is
incremental (and has a generational mode), so in play that work is spread over
frames rather than taken at once — **but that has to be measured, not assumed.**

**Known issue in this run:** the "malloc peak 0 KB" line is wrong — devkitARM's
newlib leaves `mallinfo().usmblks` at zero. `main.c` now counts mruby's bytes
exactly through its own `mrb_basic_alloc_func()`; the next run reports
"mruby peak".

## Part 2 (2026-10-19)

### The game's live set (`count_live_set.rb`, PC)

RGSS keeps the database plus the **current** map alive; a selected translation
keeps its whole `lang.rxdata` alive too. Counted as mruby heap objects on the 3DS
(Floats count — they are boxed on 32-bit; 31-bit integers and Symbols do not):

| Resident | Objects |
|---|---|
| database (13 files) | 22,268 (43% objects, 37% arrays, 16% strings, 4% Table/Color/Tone) |
| a map: median / 90th pct / largest (Map001) | 1,499 / 5,112 / 12,794 |
| translation, largest (it) | 33,750 |
| methods defined by the scripts | 1,026 |
| **total: English, largest map** | **~36,000** |
| **total: with a translation** | **~70,000** |

The busiest map (Map096) has 124 events. At part 1's ~2 µs per live object, a
full collection would take ~75 ms in English and ~145 ms translated — if it
happened all at once. Part 2 measures what actually happens.

### What `spike2.rb` runs

A live set of 38k objects (then +34k for the translated case), shaped like the
census, and 600 frames of a map-shaped workload sized to Map096: 124 characters
moving and animating (Game_Character-style, including RGSS's `+= 1.5` Float
animation counter), their sprites, 20 easing pictures (Float maths), an event
interpreter and a message window. Each case runs with mruby's **incremental** GC
and with its **generational** GC, with 0 and 2,000 extra short-lived objects per
frame, and reports avg / p99 / max frame time and frames over the 25 ms budget.
A first run with the GC disabled gives the Ruby work alone. The workload is a
model written for this test, not the game's scripts.

### Regexp

`gems/mruby-onig-regexp-3ds` adds Regexp, MatchData, pattern `sub`/`gsub`/`scan`/
`split`/`slice!` and `$~`/`$1..$9`, using Onigmo — the engine CRuby uses. Part 1
now checks the message system's patterns (`gsub!` with `$1` in the block,
non-greedy groups, `slice!(/./m)`) and times a 30-pass message preparation.
Cost: +0.5 MB of code (mostly Unicode tables); the app is now 1.47 MB.

That check found an **upstream bug**: `gsub` reuses one MatchData, whose capture
cache was never cleared, so `$1` in a `gsub` block kept the *first* match's value
for every replacement. `patch_upstream.rb` fixes it (one line, applied by
`build_mruby.ps1`); worth reporting to mattn/mruby-onig-regexp.

PC results with Regexp: part 1 **15/15** (was 13/13 + Regexp missing); message
preparation 0.26 ms on the PC.

### Results on hardware (New 3DS XL, 804 MHz + L2, 2026-10-19)

Raw logs: `results/n3dsxl-2026-10-19-part2.txt`, `results/n3dsxl-2026-10-19-part1-regexp.txt`.

**Part 1 with Regexp: 15/15.** The message-system patterns work on the device.
Preparing a message costs **10.1 ms for 30 regex passes** (~0.34 ms per pass over
a ~200-character text). mruby turns every regex literal into a `Regexp.compile`
call each time it runs, but compiling is ~10% of a pass (measured on the PC: 1 µs
compile vs 9 µs `gsub!`), so no regex cache is needed. A message with ~50 passes
costs ~17 ms once, when it opens — not per frame.

**Part 2, frame times (ms) at the game's live-set size:**

| Case | avg | p99 | max | frames > 25 ms |
|---|---|---|---|---|
| Ruby work alone (GC off) | 5.4 | 6.2 | 6.4 | 0 |
| English (39k live): **incremental** | 5.7 | 10.7 | 14.7 | **0** |
| English: incremental + 2k garbage/frame | 24.3 | 35.2 | 35.6 | 63 |
| English: generational | 5.8 | 10.4 | 76.0 | 3 |
| English: generational + 2k | 23.8 | 93.3 | 97.2 | 73 |
| Translated (73k live): **incremental** | 5.6 | 11.7 | 16.1 | **0** |
| Translated: incremental + 2k | 25.1 | 44.0 | 44.7 | 80 |
| Translated: generational | 5.8 | 12.9 | 132.6 | 1 |
| Translated: generational + 2k | 24.8 | 73.8 | 194.0 | 51 |

Full `GC.start` with the real live set: **29 ms** (English), **87 ms** (translated).
mruby's memory peaked at **11.9 MB of 92 MB**; the live sets cost 1.6 / 2.5 MB.

**What it says:**

1. **The Ruby work fits.** The busiest map's worth of character, sprite, picture,
   interpreter and message updates costs **5.4 ms — 22% of the 25 ms budget.**
2. **Use the incremental GC (mruby's default); never the generational one.** At
   the workload's own garbage rate the incremental collector kept *every* frame
   under budget (max 14.7 ms English, 16.1 ms translated). Generational mode
   periodically does a full stop-the-world collection: 76–194 ms spikes.
3. **Allocation rate is the limit to watch.** 2,000 extra allocating operations
   per frame (format, string concatenation, small arrays, Floats) cost ~19 ms per
   frame on their own — ~9.5 µs each with GC work — and push frames over budget.
   So the engine has room for roughly 1,500–2,000 short-lived objects per frame
   beyond the base work. RGSS creates Floats (boxed on the 3DS) in character
   animation, picture easing and screen tone fades; if the real scripts allocate
   heavily, NaN boxing (`MRB_NAN_BOXING`: 8-byte values, Floats stored inline as
   full doubles) removes all Float garbage and is the first lever to try.

**M2 verdict: mruby is the host.** Language coverage, memory (12 MB peak of
92 MB), on-device compile (~1.3 s for all scripts), Regexp and frame time all
check out on hardware. What this model cannot tell is the *real* scripts'
per-frame cost and allocation rate — that needs the real scripts running on the
device, which needs `Marshal` (to load `Scripts.rxdata` and the maps).

## Part 3: the game's own scripts, headless (2026-10-19)

The question part 2's model could not answer: what do **the real scripts** cost
per frame? Part 3 runs them. Boot is what RGSS does: the player's
`To the Moon.rgssad` is indexed (979 files), `Data/Scripts.rxdata` is read,
and all 124 scripts are inflated and run in order at top level. The game's own
`MAIN` then loads the database, sets up the start map and runs its own scene
loop. `rgss_headless.rb` stands in for RGSS's built-in classes: every Sprite,
Window, Viewport and Bitmap keeps its state, nothing is drawn, so the time
between two `Graphics.update` calls is one frame's Ruby work. The harness then
moves the player through the heaviest maps — 300 frames each — pressing
"confirm" every 20 frames so messages and choices advance.

| Map | Why it is in the test |
|---|---|
| 010 → 009 | the start map (the intro moves on to 009 by itself) |
| 096 | most events: 124, on a single 20×15 screen |
| 029, 008, 062 | most autorun/parallel events (26, 17, 16), run every frame |
| 129 | 177×27 map with 83 events, 21 parallel |
| 077 | most event commands (3,642) |
| 001 | most data objects |

### What it took to run the game on mruby

- **`Marshal.load` in C** (`gems/mruby-ttm-core/src/marshal.c`): every type RMXP
  data uses, with object links. **Checked against CRuby on all 180 data files**
  read straight out of the archive: identical object graphs (`test_marshal.rb`).
  All of them load in ~1 s on the PC.
- **The archive reader in C** (`rgssad.c`): the `key*7+3` scheme verified by
  `tools/rgssad_extract.py`; indexing the 36.5 MB archive takes 3 ms.
- **Four shims** for the game itself (in `rgss_headless.rb`): `MKXP.data_directory`
  (the game was last built for mkxp), a small in-memory file store (it writes and
  reads back `music.txt`/`window.txt`), `Symbol#id2name`, and `module_function`
  with no arguments — a no-op in mruby, which the game's `TR` module relies on;
  rebuilt here on mruby's `method_added` hook.
- Stubs for mkxp's extra `Input` constants (mouse buttons), `Win32API` (answers 0)
  and `File`/`Dir`.

### Results on the PC (`results/pc-2026-10-19-part3.txt`)

**9 maps, 2,424 frames, 0 errors.** Ruby time per frame on this PC:

| Map | events | avg ms | max ms | objects allocated / frame (avg / max) |
|---|---|---|---|---|
| 096 | 124 | 1.53 | 6.6 | 1,550 / 3,080 |
| 062 | 81 | 0.86 | 7.7 | 1,322 / 3,179 |
| 001 | 58 | 0.73 | 6.5 | 898 / 2,330 |
| 029 | 60 | 0.43 | 17.4 | 657 / **16,112** |
| others | 10–83 | 0.23–0.43 | ≤ 5.7 | ≤ 930 / 2,167 |

Part 2 measured the 3DS at ~27× this PC for this kind of Ruby. **If that holds,
Map096 needs ~40 ms per frame on the 3DS — over the 25 ms budget — and Map029's
16k-allocation frame is a ~0.5 s hitch.** The hardware run settles it.

**Where the time goes** (`profile_pc.rb`, Map096): per-character work. `Game_Event#update`
(124 calls) and `Sprite_Character#update` (125 calls) take ~0.7 ms each on the PC —
~5.5 µs per character, each — and everything else (interpreter, messages, the mouse
scripts, screen) is under 0.1 ms. Map096 is exactly one screen, so all 124 events
are visible; the game's own anti-lag script cannot skip any. **The garbage is Arrays
and Strings, not Floats** (allocations sampled by type), so NaN boxing would not help;
fewer temporary objects in the per-character paths would.

### First run on hardware (`results/n3dsxl-2026-10-19-part3.txt`)

**0 errors: the game's own scripts run on the 3DS.** The 3DS ran them ~26.5x
slower than the PC, as predicted:

| Map | avg ms | frames > 25 ms | worst ms | map load |
|---|---|---|---|---|
| 096 | **40.5** | **300 / 300** | 150 | 0.9 s |
| 062 | 23.2 | 44 | 54 | 1.5 s |
| 001 | 22.5 | 4 | 318 | 1.9 s |
| 029 | 12.5 | 3 | **562** | 1.6 s |
| others | 7-13 | 1-4 | 37-160 | 0.3-1.9 s |

Boot: archive index 1.9 s, scripts 2.0 s. mruby peaked at 11.8 MB.

## Speed work, round 1 (2026-10-19)

Tools first: a **line-level sampling profiler** (`gems/mruby-ttm-core/src/profiler.c`,
PC build only, on mruby's debug hook; `profile_lines_pc.rb`) and a **state digest**
per map (player, events, switches, variables, self switches, pictures). The run is
deterministic (seeded `rand`, scripted input), so any change that alters the game's
behaviour changes a digest; checked to be sensitive (one frame fewer changes 7 of 9).

Changes, each measured on the PC (Map096 avg, ms):

| Change | Where | Map096 |
|---|---|---|
| (start) | | 1.53 |
| `Color`, `Tone`, `Rect`, `Table` in C; sprites create `Color`/`Tone` lazily | `gems/mruby-ttm-rgss` (engine code, kept) | ~1.25 |
| `MRB_HIGH_PROFILE`: 4096-entry method cache, bigger heap pages | `build_config_3ds.rb`, `Makefile` | ~1.12 |
| `a != b` compiled inline (was a call to a bytecode `BasicObject#!=`); `Array#include?` in C | `patches/mruby_codegen_neq.rb`, `mruby-ttm-core` | ~1.00 |
| two To the Moon patches (below) | `romfs/ttm_patches.rb` | **~0.74** |

**Map096 is ~2x faster; at the measured ratio that is ~20 ms on the 3DS.**
Allocations on Map096 fell from ~1,600 to ~970 per frame; the Map029 spike frame from
16k objects / 10-17 ms to 12.6k / 4 ms (PC).

**To the Moon patches** (`romfs/ttm_patches.rb`) are search-and-replace edits made to
the player's own scripts as they load, each swapping one exact line for a call to an
engine helper -- the engine ships none of the game's code, and a patch whose text is
not found is skipped. **All state digests are identical with and without them.**
1. *Caterpillar*: every refresh (switch 1 changes, party refresh, map setup) re-scanned
   every event's name with a regex. Names never change: the scan is cached per name.
2. *Rach checker*: events 3-6 looped over every event on the map each frame comparing
   names against six constants. Only events with those names do anything, so the loop
   runs over those, found once per map.

Also: the archive index reads each entry header with one seek + one read (the 3DS
does a service call per SD read; it took 1.9 s), and the test plan ends with Map096
again **with the GC paused**, to show what collection costs per frame on the device.
On the PC, collection is negligible (0.77 ms paused vs 0.72 ms).

### Second run on hardware (`results/n3dsxl-2026-10-19-part3-round1.txt`)

**All ten state digests are identical to the PC run's**: the 3DS runs the game's
scripts exactly as the PC does -- C value classes, Marshal, the patches and the
`!=` compiler change included.

| Map | avg ms (run 1 -> 2) | p99 ms | frames > 25 ms (of 300) |
|---|---|---|---|
| 096 | 40.5 -> **24.5** | 57.7 | 300 -> 9 |
| 062 | 23.2 -> 18.7 | 53.2 | 44 -> 4 |
| 001 | 22.5 -> 13.9 | 51.0 | 4 -> 4 |
| 029 | 12.5 -> 9.2 | 80.6 | 3 -> 4 |
| others | 7-13 -> 6-9 | 14-46 | 1-4 -> 2-3 |

**What is left is stutter, not average speed:**
- **The first frame on every map** takes 93-196 ms and allocates 12-23k objects
  (map-entry work). In the game this frame sits inside the screen transition.
- **GC tails.** Map096 with the GC paused: avg 25.1 ms (no faster), but p99 29 ms
  instead of 58. Collection is cheap on average and lumpy: a few incremental
  steps per second take 30-50 ms. The cure is to run GC work in the frame's idle
  time (after the frame, before vsync), which needs the real `Graphics.update`.
- Map029's weather/caterpillar frame: 562 -> 215 ms.

Also: archive indexing is still 1.7 s (each SD seek+read is a service call; an
index cache on the SD card fixes it), scripts compile in 2.1 s (a bytecode cache
fixes it), map loads take 0.8-1.9 s, and `mrb_open` grew to 53 ms with the bigger
method cache and heap pages -- all one-time costs.

**M2 verdict, final: mruby hosts the game.** All 124 scripts run on the 3DS with
0 errors and identical behaviour, in 14 MB, at or under 25 ms per frame on average
on every map tested. The remaining work -- frame-time stutter, boot and map-load
time -- is performance tuning for the real engine (M10), and the stutter fixes
depend on the real frame loop that M3 builds.
