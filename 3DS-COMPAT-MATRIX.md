# To the Moon — Nintendo 3DS: compatibility matrix (Step 1 deliverable)

**Purpose.** Decide whether a 3DS port of *To the Moon* is viable, and if so what it
actually costs, **before** writing engine code. Everything below was produced by
reading the game's own data on this machine — nothing is assumed.

**Verdict in one line:** the game is a *portable* RGSS project (no live Windows API,
no threads, no networking, no shader effects), but the **texture/memory budget and the
640×480 — 400×240 problem are hard limits** that force both a renderer rewrite and an
asset pre-processing step. Feasible, but it is a 6×12 month engine project, not a conversion.

**Target console (confirmed): New 3DS XL** — 804 MHz quad-core ARM11, 256 MB FMRAM
with **~124 MB usable by an app** (176 MB in extended-memory mode), PICA200 GPU,
~6 MB VRAM. Where this document quotes "Old 3DS" numbers (268 MHz / 64 MB), that is
the fallback case, not the target. The New 3DS XL is roughly **3× the CPU clock and
2× the memory budget** of the Old 3DS, which is what makes M2 (§5.3) plausible at all.

---

## 1. What the input artifact actually is

| Check | Result |
|---|---|
| File analysed | `To.the.Moon.v2023.07.03.Incl.ALL.DLC.zip` (215,066,532 bytes) |
| Is it a Switch NSP? | **No.** It is a **PC (Windows) Steam-unlocked repack** — `STEAMUNLOCKED … .url`, `steam_settings/`, `steam_api.dll.bak`, `_Redist/` |
| Game executable | `To the Moon.exe` (8,087,552 B) + `RGSS102E.dll` — RPG Maker XP, RGSS 1.02E |
| Engine actually shipped | `mkxp-console.exe` (8,455,168 B) + `mkxp.conf` + `preload/ruby18_comp.rb` + `preload/win32_wrap.rb` + `fluidsynth.dll` — the **mkxp / mkxp-z family** (Ruby 1.8 compat shim + Win32API-as-FFI shim) |
| `Game.ini` | `Library=RGSS102E.dll`, `Scripts=Data\Scripts.rxdata` |
| `mkxp.conf` | `execName=To the Moon`, `RTP=lang.dat`, `fontSub=Arial>Open Sans`, `fontSub=New Times Roman>Open Sans` |
| Loose `Data/` folder | **Absent** — all data is inside `To the Moon.rgssad` (36,514,651 B) |
| Steam integration | `steam_api.dll` + `UtilsSteam.dll` + `Utils.dll` + `steam_appid.txt` (206440) — native DLLs, **not** called from Ruby (see §4) |

**Consequence:** the Switch NSP is irrelevant to this project (it is a Unity build).
The PC data is the porting input, and it is RPG Maker XP — exactly what an RGSS
interpreter consumes.

---

## 2. The archive decode (and how it was proven)

`To the Moon.rgssad` is **RGSSAD v1**. The algorithm was *derived from the data*, not
assumed — published descriptions of this format disagree, and the common one (key
rotating left per byte, `+7` per chunk) **does not match this file**:

```
key = 0xDEADCAFE
repeat:
    name_len = le_u32 ^ key                    ; key = key*7 + 3
    name[i]  = raw[i] ^ (key & 0xFF)           ; key = key*7 + 3   (per byte)
    size     = le_u32 ^ key                    ; key = key*7 + 3
    data_key = key          # the payload does NOT advance the table key
    payload  = per-4-byte-chunk XOR with data_key ; data_key = data_key*7 + 3
until end of file
```

**Proof of correctness** (three independent checks, all passing):
1. `0xDEADCAFE*7+3` ends in `0xF5`; `0xB1 ^ 0xF5 = 'D'` — the first entry name starts `Data/…`.
2. 979 entries parsed, **946 magic-verified, 0 magic-failed** (PNG/OggS/Marshal/MZ).
3. The parse consumes **exactly** 36,514,651 bytes — the whole file, no slack.

Archive contents: **765 PNG, 181 RXData, 33 JPG**, no audio (audio ships loose in the
game folder, see §3).

`lang.dat` (17,209,465 B) is **not** game data — it is a ZIP with 851 entries: the
localisation pack for 11 languages (`de, pt-BR, ru, es, fr, tr, it, pl, ko, zh-CN, sk`),
each holding `lang.rxdata`, `meta.rxdata` and localised `Graphics/Pictures` PNGs.

---

## 3. What the game is made of

| Dimension | Measured |
|---|---|
| Scripts | **124 sections**, 871,310 bytes of Ruby source (all decompressed, 0 failures) |
| Data tables | 181 `.rxdata`, 5.7 MB (167 maps + 14 tables); `Tilesets.rxdata` 242 KB, `Scripts.rxdata` 173 KB |
| Maps | **166 maps, 5,357 events, 8,342 pages** |
| Graphics | **798 images**, 29.1 MB on disk — **589.4 MB decoded to RGBA** |
| Audio | **802 files, 71.2 MB** — BGM 52 ogg (42.3 MB), BGS 51 ogg (7.8 MB), ME 7 ogg (5.7 MB), SE 691 (676 ogg, 12 wav, 2 mp3, 1 wma) (15.4 MB) |
| Fonts | `OpenSans.ttf` 221 KB (used via font substitution), `NanumBarunGothic.ttf` 4.2 MB (ko), `WenQuanYi_Zen_Hei.ttf` 9.0 MB (zh-CN) |
| Dialogue | English text lives in the map data as `101` Show Text parameters using **UMS** markup (`\ignr`, `\e[n]`, `\nm[ name ]`, `\.` pauses); translations are injected at runtime from `lang/<code>/lang.rxdata` by `TTMTranslator` + `EventPatchUtil.patch_map` (`118_TTMTranslator.rb`) |
| Third-party RGSS systems in use | UMS (Ultimate Message System: `Paragrapher`, `Formatted_Text`), a mouse-input system, `Caterpillar` (party follow), a pause menu, a language selector, `TTMTranslator` |

### Event-command histogram (which engine features are actually exercised)

| Code | Count | Meaning |
|---|---|---|
| 509 | 23,941 | continuation list for **Set Move Route** |
| 0 | 10,461 | empty/blank command |
| 106 | 6,910 | Wait |
| 209 | 5,154 | Set Move Route |
| 401 | 4,524 | branch/loop end |
| 101 | 3,717 | **Show Text** |
| 121 | 2,397 | Control Switches |
| 250 | 1,878 | **Play SE** |
| 108 | 1,277 | Comment |
| 111 | 1,264 | Conditional Branch |
| 232 / 231 | 1,076 / 975 | Move Picture / Show Picture |
| 117 | 1,054 | Call Common Event |
| 210 | 1,096 | Wait for Move Completion |
| 223 | 983 | Change Screen Colour Tone |
| 122 | 1,080 | Control Variables |
| 207 | 779 | Show Animation |
| 204 | 610 | Change Map Settings |
| 355 / 655 | 154 / 23 | **Script calls** |

**Interpretation:** the game is movement + picture + text driven. Combat, shops and
the RPG systems exist as stock scripts but are barely used; the visual load is
*Sprites, Pictures, Planes and the message system*, not tilemaps.

---

## 4. Compatibility matrix

Legend: **native** = works as-is on 3DS hardware · **rewrite** = same behaviour, new
3DS implementation · **stub** = no 3DS equivalent, safe to disable · **script change** =
requires editing the game's Ruby source.

### 4.1 Ruby host — the highest-risk unknown

| Feature | Measured use | 3DS verdict | Notes |
|---|---|---|---|
| `eval` | **5 live sites** (`Game_Character 2:675`, `Interpreter 3:525`, `Interpreter 7:543`, `UMS new:1601`, `GUBID menu:159`, `Caterpillar:491`) | **rewrite** | Rules out a "tiny interpreter" — a real `eval` is mandatory (event code 355 and the game's own dispatchers depend on it) |
| `Marshal` | **heavy** — the save format *is* Marshal (`Scene_Save/Load`, `Window_SaveFile`, `TtM Util`, `UMS new`) | **rewrite** | Must be byte-compatible with RGSS 1.x saves, including `Table`, `RPG::*` objects and links |
| `File` / `Dir` / `FileTest` | save files, `lang` setting file, music setting file | **native** (after mapping to SD paths) | |
| `alias_method`, `Proc`, `Struct` | 20+ sites, standard RMXP style | **native** | needs full Ruby metaprogramming |
| `Thread`, `Mutex`, `fork` | **0 live** | **native (unused)** | great news: libctru has no `pthread.h`, so a single-threaded Ruby host is acceptable |
| `Zlib`, `ObjectSpace`, `require` | **0 live** | **native (unused)** | shrinks the port |
| Sockets / networking | **0 live** | **native (unused)** | |

**Implication:** the host must be **CRuby** (mkxp-z's own binding targets MRI; its
`mruby` binding is not shipped). `mruby` lacks `Marshal` and default `eval`, so choosing
it means implementing both from third-party gems. This single decision most affects the
project's shape — **test it first (M2)**.

### 4.2 RGSS object model

| Class | Measured use | 3DS verdict | Work |
|---|---|---|---|
| `Bitmap` | **106 `Bitmap.new`** + 119 refs | **rewrite** | RAM-backed surfaces: `blt`, `stretch_blt`, `fill_rect`, `draw_text`, `text_size`, `clear`, `get_pixel` |
| `Sprite` | 20 `new`, 26 refs | **rewrite** | `src_rect`, `x/y`, `zoom_x/y` (10), `angle` (12), `opacity` (128), `blend_type` (20), `tone` (41), `color` (178), `mirror` |
| `Viewport` | 7 refs, 2 files | **rewrite** | `Spriteset_Map` builds three `Viewport.new(0,0,640,480)` |
| `Plane` | 2 refs, 1 file | **rewrite** | panorama / fog scrolling |
| `Tilemap` | **1 ref, 1 file** | **rewrite (small)** | class must exist, but the game barely uses it — no need for a heavily optimised tilemap path |
| `Window` framework | **184 `Window_*` refs in 56 files** | **native** (pure RGSS over `Bitmap`) | only `Window`'s primitives (windowskin blitting, cursor, contents) touch the engine |
| `Graphics` | `transition` 32, `freeze` 28, `update` 24, `frame_count` 21, `frame_rate` 11, `frame_reset` 10 | **rewrite** | `frame_rate` is 40 in RMXP; no `resize_screen`, no `play_movie`, no `brightness` |
| `Input` | 295 refs (`trigger` 96, `repeat` 37, `update` 21, `press` 6, `dir4` 2) + `C B UP DOWN LEFT RIGHT L R A F9` | **rewrite** | direct button mapping; **`mouse_x/mouse_y` + `MOUSELEFT/RIGHT/MIDDLE` are referenced** — map to the bottom-screen touch panel |
| `Audio` | `bgm_play/stop/fade`, `bgs_*`, `me_*`, `se_play/stop` | **rewrite** | module-function style (RGSS 1.x), small surface |
| `Color`, `Tone`, `Rect`, `Table` | 63 / 8 / 42 / 1 `new` | **native** (pure data) | `Table` also needs Marshal support |
| `RPG::Cache` | **89 refs** | **native** (pure RGSS) | but it is the choke point for asset loading — see §5 |
| `RPG::Weather` | 2 refs | **rewrite (small)** | rain / snow particles |

### 4.3 Drawing features actually required

| Feature | Use | 3DS verdict |
|---|---|---|
| `draw_text` / `text_size` | 128 / 40 | **rewrite** (font rasteriser) |
| `opacity`, `color`, `tone` | 128 / 178 / 41 | **rewrite** via vertex-colour modulation (PICA200 has no shaders) |
| `blend_type` (normal / add / sub) | 20 | **native** — PICA200 supports alpha/add/subtract blending |
| `zoom`, `angle` | 10 / 12 | **native** — vertex transforms |
| `src_rect`, `stretch_blt` | 11 / 1 | **native** — textured quad |
| `hue_change`, `blur`, `radial_blur`, `wave_*` | **0** | **not needed** — removes the hardest shader work |
| `Graphics.transition` (fades) | 32 | **rewrite** (full-screen blend pass) |
| Screen tone / flash / shake (223/224/225) | 983 / 38 / 27 | **rewrite** (tone = vertex colour, flash = quad, shake = viewport offset) |
| Show Animation (207) / `RPG::Animation` | 779 | **rewrite** (frame-by-frame bitmap playback) |

### 4.4 Windows / Steam / platform interop

| Item | Measured | Verdict |
|---|---|---|
| `Win32API` | **29 references, every one inside a `#` comment — 0 live** | **nothing to port** |
| `user32`, `kernel32`, `GetSystemMetrics` | same — all commented | **nothing to port** |
| `Utils.dll`, `UtilsSteam.dll` | shipped, never called from Ruby | **drop** |
| Steam achievements | via the native Steam DLLs, not Ruby | **stubbed** (`compat.rb`: `_steam_achievement_unlock` is a logged no-op, and run 19 removes the game's `defined?` guards, which this mruby cannot parse — without it the credit roll ended the game) |
| The game's "Mouse 1/2/3" scripts | live `alias_method` hooks, but their Win32API input is commented out | **rewrite as touch input** (optional; fits the bottom screen well) |

### 4.5 Media, fonts, saves

| Item | Measured | 3DS verdict |
|---|---|---|
| BGM/BGS/ME/SE | 802 files, 71.2 MB, overwhelmingly **Ogg Vorbis** (786 ogg, 12 wav, 2 mp3, 1 wma) | **rewrite**: stream Ogg from SD, decode with Tremor/libvorbisidec, output via NDSP. No MIDI anywhere — **no SoundFont needed** (the bundled `fluidsynth.dll` is for the PC build) |
| Fonts | `Arial` / `Times New Roman` / `Tahoma` requested, substituted to **Open Sans** (221 KB) by `mkxp.conf` | **done**: Open Sans is the default face; the two CJK fonts load by name, on demand, from the player's `Fonts/` (§10.22) |
| Save files | `Marshal` + `File` / `FileTest`, `save1.rxdata`…`save4.rxdata`, plus `lang` and music setting files | **rewrite**: write to the SD card under `/3ds/<app>/` |
| Config | `mkxp.conf` (legacy ini) — `fullscreen`, `vsync`, `smoothScaling`, `fontSub` | **drop**; replace with a 3DS config |
| Localisation | `lang/<code>/{lang,meta}.rxdata` + localised `Graphics/Pictures` PNGs; `TTMTranslator` patches map data at load time; `"en"` is implicit (base data) | **done** (§10.22): `lang.dat` is read as an extra archive, so the game's own translator, language screen and 11 languages work unchanged; the pack stays on the SD card and nothing is unpacked |

---

## 5. The four hard limits (ranked by how much they constrain the design)

### 5.1 Texture memory and the 1024 px texture limit — the real blocker

| Measurement | Value |
|---|---|
| All graphics decoded to RGBA | **589.4 MB** |
| Of which images over 1024 px (PICA200 limit) | **35 images, 141.3 MB decoded** |
| Tilesets alone | **51.2 MB decoded across 7 files** |
| Largest single image | `Graphics/Tilesets/Bridge.png` **256 — 12500** (12.2 MB decoded) |
| Also over the limit | `Tileset Dreamscape.png` 256×11201, `town2.png` 256×9332, `Tileset.png` 256×6096, panoramas up to 1800×864, `Light 2.png` 1024×1200, `shuttle.png` 604×1729 |
| 3DS VRAM | **~6 MB total** |
| 3DS app RAM | **~64 MB (Old) / ~124 MB (New 3DS XL, the target) / ~178 MB in extended memory mode** — and that 178 MB mode is reachable **only by an installed title (CIA)**, never by a `.3dsx`, because it is a field in the title's extended header. Verified by building one (`SystemMode 2`), see §10.3 |
| Texture granularity | **PICA200 textures must be POWER-OF-TWO.** A 320×240 or 400×240 target really costs 512×256, and the 640×480 frame costs 1024×512 — verified while building the M1 test app. Budget ~1.3× the naive area, and note the native frame lands *exactly* on the 1024 px limit |

Three consequences, each with a concrete answer:

1. **Tilesets cannot be uploaded as single textures.** The 256×12500 RMXP "mega surface"
   must be split into …1024-tall pieces (13 slices for the largest) and the tile renderer
   must index across slices. This is exactly the "mega surface" special case mkxp itself
   documents — the 3DS port has to implement it as *N* real textures.
2. **Nothing close to the full asset set can be resident.** The engine needs an
   LRU/streaming `RPG::Cache` (which the game already uses in 89 places) with an explicit
   budget, plus eviction, rather than "load everything on demand and keep it".
3. **Pre-scaling assets by 0.5 offline is the highest-value optimisation available.**
   Because the final render target is 0.5× anyway (§6), re-encoding the assets at half
   resolution in a build step cuts decoded memory **4×** (589 MB — 147 MB; tilesets
   51.2 MB — 12.8 MB; the largest tileset becomes 128×6250, still needing slicing but only
   3 MB), improves fill-rate, and gives crisper output than runtime scaling. This should
   be a *Step-2 build tool*, not an afterthought.

### 5.2 Resolution and text legibility

The game is 640×480 (4:3). The 3DS top screen is 400×240 (5:3); the bottom is 320×240 (4:3).

| Option | Geometry | Text size (RMXP 22 px font) | Cost |
|---|---|---|---|
| **A. Half-scale, pillarboxed** | 640×480 — **320×240** (integer 2:1, crisp) centred on the top screen with 40 px bars each side | **11 px** | zero script changes; smallest text |
| **B. Half-scale + message layer on the bottom screen** | game view on top (320×240 + bars), dialogue/UI rendered natively on the 320×240 bottom screen — which is *exactly* half of 640×480, so the mapping is still a clean 2:1 | 11 px in the world, **larger/redrawn** in the message layer | requires editing `Window_Message` / UMS / `Scene_Map` (game-script change) |
| **C. 0.625 scale with crop/pan** | 640×480 — 400×300 at 0.625, showing 240 of the 300 rows (crop or follow the player) | **13.75 px** | loses vertical composition in a deliberately composed, story-driven game; engine-level viewport change |

**Measured, not guessed.** `tools/make_ui_mockup.py` renders the game's real message
window (`super(80, 304, 480, 160)` from `055_Window_Message.rb`), at the real 22 px font
with the game's own `OpenSans.ttf`, with **real dialogue pulled from the map data**,
over real art — at every candidate layout. The results are in `work/preview/`:

| Result | Consequence |
|---|---|
| **Option A is readable** at 11 px (and a New 3DS XL's pixels are slightly *larger* than a 1080p monitor's, so 1:1 viewing on a PC is a fair simulation) | option A can ship with **zero game-script changes** — it is the correct first target |
| **Option B's native-size text overflows the box** | the game's lines are authored for the PC window's **448 px text width**; at 22 px only ~32 characters fit a 320 px screen. Option B therefore requires **re-wrapping the game's own dialogue**, not just engine work — a phase-2 item |
| Option C gives 13.75 px text but crops the frame | keeps the game's composition only if the camera pans; rejected unless A/B prove unreadable |

**Recommendation:** **A first** (it is the only option that needs no game-script edits and
proves the engine), then **B** as the polish target — it is also the option that makes the
3DS version feel native rather than shrunken. Option C should be rejected unless play
testing shows A/B are unreadable.

Note: there is **no** integer upscale path from 320×240 to 400×240 (400/320 = 1.25), so
"render small and scale up" is not available on the top screen. Any enlargement must
happen in the game's own drawing code.

### 5.3 The Ruby host

`eval` + `Marshal` + full metaprogramming are required (§4.1). libctru exposes **no
`pthread.h`** (its public headers are `3ds.h`, `3ds/`, `arpa/`, `netdb.h`, `netinet/`,
`poll.h`, `sys/`), so CRuby needs a pthread shim over `svcCreateThread`/`3ds/thread.h`.
Mitigating factor: the game uses **no threads, no Zlib, no `require`, no `ObjectSpace`,
no sockets**, so a single-threaded, statically linked, feature-trimmed CRuby is enough.
This is still the biggest schedule risk and should be de-risked before any renderer work.

### 5.4 The renderer

mkxp's renderer is OpenGL with shaders; the PICA200 has no GLSL. However the game needs
**none** of the shader-only effects (0 uses of `hue_change`, `blur`, `radial_blur`,
`wave_*`). What remains — textured quads with `src_rect`, `zoom`, `angle`, `opacity`,
`tone`/`color` modulation, and normal/add/sub blending — maps directly onto citro3d
(vertex transforms + vertex colours + texture stages). And because `Tilemap` appears
**once**, there is no need for a high-performance tile path; a tile-atlas cache that
uploads only the visible tiles each frame is sufficient.

---

## 6. Verdict and recommended route

### 6.1 Route assessment (revised by the evidence)

| Route | Viability | Reasoning |
|---|---|---|
| **A. Play it handheld without porting** | **Recommended if the goal is to play the game** | Official Unity builds exist on Switch (2020) and Android/iOS; they need no work at all |
| **B. Full mkxp-z — 3DS port** | Possible, largest scope | mkxp-z's own CI targets only Windows/Linux/macOS; it carries OpenGL, PhysFS, OpenAL/FluidSynth, SDL2 and a full CRuby — all of which must be re-targeted. No `mkxp+3ds` project exists anywhere |
| **C. Purpose-built RGSS player for this game (recommended)** | **Most realistic** | The measured surface is small: ~12 engine classes, ~15 drawing operations, **no threads/zlib/network/shader effects**, **no live Win32API**. The stock scripts (`Game_*`, `Window_*`, `Scene_*`, `Interpreter*`) are pure RGSS and can be reused verbatim once the primitives exist |

**Recommendation: Route C**, built on a trimmed CRuby host, with the asset pre-scaling
tool from §5.1. It reuses the game's own 871 KB of Ruby unchanged (which is where the
game actually lives), and it can be structured so the engine stays generic enough to run
other RGSS 1.x projects later.

### 6.2 Milestone ladder (each milestone ends with something runnable on hardware)

| # | Milestone | Exit criterion | Rough effort |
|---|---|---|---|
| M0 | devkitPro + devkitARM + libctru + citro3d toolchain, 3DSX build | a triangle renders at 400×240 on hardware | 1×2 days |
| M1 | Texture + sprite batching on citro3d (2D quads, `src_rect`, zoom, angle, blend, vertex colour) | a static scene of sprites/pictures drawn from PNGs at 60 fps | 1×3 weeks |
| **M2** | **Ruby host spike: CRuby 3.1 (static, pthread shim) vs mruby (+ marshal/eval gems)** | a 20-line RGSS-ish script runs, prints, allocates, and reports RAM use | **2×6 weeks — highest risk, do first** |
| M3 | `Bitmap`, `Sprite`, `Viewport`, `Plane`, `Window` + `Font`/`draw_text`/`text_size` | the game's own `Window_*` classes render correctly | 4×10 weeks |
| M4 | RGSSAD reader in C++ (algorithm already solved — §2) + 0.5× asset pre-scaling tool | the real game data loads from the user's own `rgssad` | 1×2 weeks |
| M5 | `RPG::*` data classes, `Table`, `Marshal` save/load | a save file round-trips in the same format as the PC version | 2×4 weeks |
| M6 | `Tilemap` with sliced mega-surface textures + visible-tile atlas cache | maps draw with correct tiles and acceptable fps | 2×4 weeks |
| M7 | Audio: Ogg streaming from SD — Tremor — NDSP; BGM/BGS/ME/SE with fades | music and SE play with correct channels and fades | 2×3 weeks |
| M8 | Input mapping + touch→`Input.mouse_x/y` | menus and dialogue are fully navigable | ~1 week |
| M9 | Resolution/UI: option A, then option B (§5.2) | dialogue is comfortably readable on hardware | 2×6 weeks |
| M10 | Performance: New-3DS 804 MHz mode, frame-rate tuning, cache budget | stable ~40 fps (RMXP's native rate) in heavy scenes | 2×4 weeks |

**Total: roughly 6×12 months of part-time work for someone already comfortable with
C/C++, ARM and a 3DS toolchain** — or 3×6 months full-time. The order matters: M2 before
M3/M6, because if the Ruby host cannot be made to fit, the renderer work is wasted.

**And the deliverable is a CIA, not a 3dsx:** extended memory mode (178 MB instead of
124 MB) is a title-only extended-header field, so the finished port has to be an
installed title — see §10.4. The M1 test app in `3ds-app/` already builds both, which
is how that conclusion was verified rather than assumed.

---

## 7. Legal and licensing (settle before any distribution)

1. **The analysed copy is a pirated repack** (`STEAMUNLOCKED`, `steam_settings/`,
   `steam_api.dll.bak`). Work from a legitimately purchased Steam/GOG copy instead — same
   data, no ambiguity, and it is the only version whose provenance you can state.
2. **Never ship game data.** The `.rgssad`, audio, fonts and the game's own Ruby scripts
   are Freebird Games' copyright. The correct model (as in the original plan) is:
   distribute **engine only**, and have each player copy their own `To the Moon.rgssad`
   and `Audio/` to their SD card.
3. **mkxp-z is GPL-2.0-or-later** (effectively GPLv3 with its default OpenSSL option), so
   any port linking its code must be GPL and ship its source. A clean-room engine
   (Route C) avoids that coupling — but then it must not copy mkxp code either.
4. Keep this analysis private: `work/` holds extracted game data and is **not** for
   committing, uploading or sharing (add `work/` to `.gitignore` if this becomes a repo).

---

## 8. Reproducing this analysis

Everything is in `tools/` and needs only Python 3.12 (already installed; no Ruby, no
toolchain):

```powershell
# 1. decode the archive straight out of the zip (no need to expand 215 MB)
python tools\rgssad_extract.py extract 'To.the.Moon.v2023.07.03.Incl.ALL.DLC.zip'
python tools\rgssad_extract.py probe   'To.the.Moon.v2023.07.03.Incl.ALL.DLC.zip' --entry '.../lang.dat'

# 2. dump the 124 scripts and the data tables
python tools\rxdata_dump.py scripts 'work\raw\Data\Scripts.rxdata'
python tools\rxdata_dump.py maps    'work\raw\Data'
python tools\rxdata_dump.py tables  'work\raw\Data'

# 3. the compatibility inventory and its detail reports
python tools\inventory.py
python tools\show_findings.py windows_interop,resolution_640x480 --active
python tools\show_findings.py rgss_renderer,graphics_api,audio_api --summary
python tools\api_surface.py

# 4. helpers
python tools\extract_lang.py
python tools\dump_events.py 'work\raw\Data\Map001.rxdata' --codes 101,355
python tools\inspect_lang.py 'work\raw_lang\de_lang.rxdata'
python tools\find_fonts.py
python tools\list_scripts.py
```

Artifacts: `work/rgssad_manifest.json` (all 979 archive entries with sizes, offsets and
PNG dimensions), `work/scripts/*.rb` (the game's Ruby, in load order), `work/tables/*.json`,
`work/inventory.json` (machine-readable version of §3→§5), plus the `*.txt` reports in the
project root.

---

## 9. Open questions and the next concrete step

| Question | Why it matters | How to settle it |
|---|---|---|
| Does CRuby fit and run on ARM11? | gates the whole project | **M2 spike** — build CRuby 3.1 static for devkitARM with a pthread shim; measure binary size and baseline RAM |
| Can mruby + `mruby-marshal` + `mruby-eval` substitute? | a much smaller host if it works | same spike with mruby (`MixedVictor/3ds-ruby` is an existing proof mruby builds for 3DS) |
| What is the real *working set* per scene? | decides Old-3DS vs New-3DS-only | instrument the PC build's texture loads per map, or compute it from `MapInfos` + the manifest |
| Is 11 px text acceptable? | decides UI option A vs B | render a real message box at 0.5 scale and look at it on hardware |
| Old 3DS or New 3DS only? | 64 MB vs 124/176 MB, 268 vs 804 MHz | follows from the working-set measurement |
| Does the tile-atlas approach hold 40 fps? | tilemap + large panoramas are the heaviest path | M6 prototype |

**Suggested next action:** run the **M2 Ruby-host spike** (one to two weeks) before
anything else. It is the only unknown that can invalidate the project, it needs no
renderer, and it yields a definitive answer: *CRuby on 3DS — yes or no.*

---

## 10. Addendum: what was built on top of Step 1

After the matrix was written, two further pieces were built and verified:

### 10.1 UI mock-ups that settle §5.2 (`work/preview/`)

`tools/make_ui_mockup.py` + `tools/render_text.ps1` compose the game's real dialogue
box — real geometry, real 22 px font, real dialogue from `Data/Map001.rxdata`, real
art — and render every candidate 3DS layout:

```
A_full_640x480.png         reference (what the PC shows)
A_half_320x240.png         option A content, view at 1:1
A_3ds_top_400x240.png      option A exactly as the top screen shows it
A_half_320x240_zoom2x.png  the same, 2x zoom, for comfortable inspection
C_crop_400x240.png         option C (0.625x, cropped)
B_bottom_320x240.png       option B (dialogue native on the bottom screen)
```

Verified by inspection: the palette-based windowskin decodes correctly, the box
chrome and UMS name spacing render, and option A's 11 px text is legible. Option B
demonstrably overflows (see §5.2).

### 10.2 A hardware test app you can run (`3ds-app/`)

`3ds-app/` is a complete devkitARM/citro2d homebrew app that displays those
pre-composed screens on real hardware, cycles the five layouts with `A`, shows a
frame-rate counter, and requests the New 3DS 804 MHz clock. It contains **no PNG
decoder and no font library** on purpose — everything is pre-composed on the PC and
packed as raw RGBA into romfs — so a failure on hardware isolates the toolchain and
GPU path. Build and deploy steps are in `SETUP-3DS.md`.

Asset side (verified): `tools/scale_assets.py` decodes the game's PNGs itself
(palette 1/2/4/8-bit, RGB8/RGBA8/grey8, non-interlaced — which is exactly what the
798 images contain), box-filters them 0.5× with premultiplied alpha, slices anything
over 1024 px, and reports the memory win: **decoded RGBA drops to 25 % of the
original** (589.4 MB — ~147 MB for the full set).

### 10.3 Toolchain and build status

- A **private devkitPro toolchain** (devkitARM r68 / gcc 16.1.0, libctru 2.7.0,
  citro3d 1.7.1, citro2d 1.7.0, 3dstools, general-tools) is installed in
  `<devkitPro root>` — **no admin rights, no changes to the system MSYS2**.
  MSYS2's `pacman` was pointed at devkitPro's `dkp-libs` / `dkp-windows` repos with
  a private root, and downloads were routed through a small curl wrapper because
  `pkg.devkitpro.org` answers 403 to scripted user-agents. Delete the folder to
  uninstall completely.
- The app **compiles and links**: `ttm3ds_test.3dsx` (2.3 MB) with all five romfs
  textures verified inside it. Three fixes were needed against the real SDK — the
  sub-texture type, the missing `$(OUTPUT).elf : $(OFILES)` dependency, and the
  SMDH prerequisite; all are recorded in `3ds-app/README.md`.
- **Run on hardware (New 3DS XL, Luma3DS, 2026-10-19).** After two more fixes
  (`gfxInitDefault()` was missing; textures had to be swizzled into the PICA200's
  tiled layout) the `.3dsx` and a 124 MB CIA both show the scene identical to the
  PC mock-up at 59 fps. **The 178 MB CIA black-screens** — see §10.4.
- The M2 Ruby-host spike has not been attempted; it remains the project's critical
  path.

### 10.4 The port must ship as a CIA, not a 3dsx (new architectural conclusion)

Two independent reasons, both now verified rather than assumed:

1. **Extended memory mode is title-only.** libctru's `exheader.h` documents the
   modes — `SYSMODE_N3DS_PROD = 1 (124 MB)` and `SYSMODE_DEV1 = 2 (97 MB / 178 MB)`
   — and that field lives in the *extended header*, which a `.3dsx` does not have.
   makerom's RSF exposes it as `SystemModeExt`, and a test title built with
   `SystemModeExt: 178MB` was read back and confirmed to carry `SystemMode 2`
   (`tools/inspect_cia.py`). That is **+54 MB over the default**, which directly
   relieves the texture budget in §5.1. It also sets `CpuSpeed: 804MHz` and
   `EnableL2Cache: true` at the title level instead of asking at runtime.
2. **The legal split needs a title.** A CIA can be installed with the engine only,
   reading the player's own `To the Moon.rgssad` and `Audio/` from the SD card
   (§7). Shipping the same thing as a `.3dsx` would work too, but without the
   extra RAM — and the extra RAM is the point.

The `.3dsx` stays useful for fast iteration; the CIA is what ships.

**Hardware correction (2026-10-19):** on the test New 3DS XL the 178 MB CIA gives
black screens, while the identical CIA built with `SystemModeExt: 124MB` runs. No
retail game uses this "Dev1" mode and the HOME menu does not run in it. Until a
178 MB build is shown running, **write M3–M10 memory budgets against 124 MB**
(the full 0.5×-scaled asset set is ~147 MB decoded, §10.2, so this means streaming
rather than keeping everything resident). Getting extended memory to work is an
open question for M10, not a dependency of anything earlier.

### 10.5 CIA toolchain

`makerom` and `bannertool` are **not** in devkitPro's repositories (verified against
the `dkp-libs` / `dkp-windows` package databases), so they were fetched from GitHub
into the same private folder and are driven by `3ds-app/build_cia.ps1`:

```
<devkitPro root>\tools\cia\
  makerom\makerom.exe                         v0.19.0  (3DSGuy/Project_CTR)
  bannertool\windows-x86_64\bannertool.exe    1.2.0    (diasurgical/bannertool)
```

Two gotchas worth recording: `bannertool makebanner` refuses an image-only
invocation (it also wants a tune — a silent 1 s WAV is generated for that), and
makerom's RSF needs the full `AccessControlInfo` / `SystemCallAccess` /
`ServiceAccessControl` block, which `3ds-app/cia/app.rsf` carries with comments.

### 10.6 M2 Ruby-host spike: first results (`ruby-spike/`)

mruby **4.0.0** was built for the PC and the 3DS (devkitARM) with 36 core gems,
using a private Ruby 3.4 for its build system. PC-side results, 2026-10-19:

- **All 124 game scripts (850 KB) compile under mruby** — and CRuby 3.4 parses all
  124 too: the game contains no Ruby 1.8-only syntax. The parser is not a blocker.
- An RGSS-shaped test script (class/`super`/`alias` chains, RPG data classes, a
  `Table` with `_dump`/`_load`, `eval` inside an object, metaprogramming) passes
  **13/13** on mruby, with every expected value confirmed against CRuby.
- **Gaps the engine must fill:** `Regexp` (178 literals, 161 of them in the custom
  message system `097_UMS new`) and `Marshal` (56 references in 5 scripts, plus all
  `.rxdata`). Ruby 1.8 semantics (`str[0]` as Integer, byte-counted `size`) differ,
  as they do for mkxp-z on CRuby 3.
- mruby on the 3DS — VM, on-device compiler and gems — is about **0.75 MB of code**.

**On hardware (New 3DS XL, 804 MHz + L2, 2026-10-19):** 13/13 checks pass;
`mrb_open` takes 2.6 ms and 55 KB; on-device compile runs at **678 KB/s**, so all
850 KB of game scripts compile at boot in **~1.3 s**; a light frame (40 sprites +
60 event commands) costs **1.4 ms** of the 25 ms budget. mruby runs ~40-47x slower
than a current desktop. The one worrying number is the **full GC pause: 416 ms with
200k live objects** (~2 us per live object).

**M2 verdict: mruby is viable as the host — no CRuby fallback is needed for
compatibility.** The remaining risk is performance under GC. RGSS keeps only the
current map plus the database live, so the real live set is likely tens of
thousands of objects (~50-100 ms per full collection), and mruby's incremental /
generational GC spreads that work across frames. M2 part 2 measures exactly that:
the real live-set size from the data, then worst-case and p99 frame times on the
3DS under both GC modes. Details and raw logs: `ruby-spike/README.md`,
`ruby-spike/results/`.

**M2 part 2 (PC side, 2026-10-19).** The live set was counted from the data
(`ruby-spike/count_live_set.rb`): the database is 22,268 heap objects, the largest
map 12,794 (median 1,499), the script methods 1,026 — **~36k objects in English,
~70k with a translation resident** (TTMTranslator keeps the whole `lang.rxdata`).
That is a fifth to a third of part 1's 200k stress case. **Regexp is solved:**
`ruby-spike/gems/mruby-onig-regexp-3ds` builds mruby-onig-regexp with Onigmo (the
engine CRuby uses) without `configure`, for +0.5 MB of code; testing the message
system's patterns found and fixed an upstream bug (`$1` inside a `gsub` block kept
the first match's value). Frame times under the GC at the real live-set size, with
the incremental and the generational collector, come from the next hardware run
(`ruby_spike.3dsx`, part 2).

**M2 part 2 on hardware (2026-10-19) and verdict.** With a live set of the game's
real size, a 600-frame model of the busiest map (124 events, sprites, pictures,
interpreter, message window) costs **5.4 ms of Ruby per frame** (22% of 40 fps).
With mruby's **incremental** GC every frame stayed under budget (max 14.7 ms
English, 16.1 ms translated); the **generational** GC caused 76-194 ms spikes and
must not be used. Full collections of the real live set take 29 ms (English) and
87 ms (translated); mruby peaked at 12 MB of the 92 MB heap. The limit to watch is
allocation rate: ~2,000 extra short-lived objects per frame use up the remaining
budget (~9.5 us each with GC), so heavy Float use in the real scripts (boxed on the
3DS) is the main risk — NaN boxing is the first remedy. Regexp works on the device
(15/15; ~0.34 ms per message pass, ~17 ms once per message).

**Verdict: M2 passes — mruby is the Ruby host; CRuby is not needed.** The one
thing a model cannot answer is the real scripts' per-frame cost and allocation
rate. The next step is to run them: `Marshal.load` (M5's core, pulled forward),
stub RGSS graphics classes, and a headless `Scene_Map` loop on the device that
loads the player's own data and reports per-frame Ruby time on real maps.

**M2 part 3: the game's own scripts run on mruby (PC, 2026-10-19).** With
`Marshal.load` and the `.rgssad` reader written in C (`ruby-spike/gems/mruby-ttm-core`;
Marshal verified identical to CRuby on all 180 data files) and RGSS's built-in
classes stubbed without graphics, all 124 scripts boot from the archive and the
game's own `MAIN` runs its own scene loop: **9 maps, 2,424 frames, 0 errors**.
That took four shims: `MKXP.data_directory` (the 2023 build targets mkxp), an
in-memory file store, `Symbol#id2name`, and argument-less `module_function` (a
no-op in mruby). The PC profile moves the risk from GC to **per-character Ruby
work**: Map096 (124 events on one screen) costs 1.53 ms/frame on the PC, ~5.5 us
per character in `Game_Event#update` and again in `Sprite_Character#update`; at the
~27x PC-to-3DS ratio of part 2 that is ~40 ms on the 3DS, over budget, and one
Map029 frame allocates 16k objects. The garbage is Arrays and Strings, not Floats.
The 3DS run (`ruby_spike.3dsx`, part 3) gives the real numbers.

**M2 part 3 on hardware and speed round 1 (2026-10-19).** On the New 3DS XL the
game's scripts ran with **0 errors**, ~26.5x slower than the PC: Map096 averaged
40.5 ms (every frame over budget), Map062/001 ~23 ms, with stutters up to 562 ms and
1-2 s map loads. A line-level profiler (PC) and a deterministic **state digest**
(identical digests = identical game behaviour) drove round 1: RGSS's Color/Tone/
Rect/Table moved to C (engine code), mruby's high-memory method-cache profile, `!=`
compiled inline, `Array#include?` in C, and two To the Moon patches applied to the
player's scripts at load (Caterpillar's per-refresh regex over event names, cached;
the "Rach checker" per-frame loop narrowed to the six event names it acts on) --
digests unchanged. Map096 is ~2x faster on the PC, which projects to **~20 ms on
the 3DS**; the second hardware run checks it and measures GC cost on the device.

**M2 closed (2026-10-19).** Second hardware run after speed round 1: Map096
40.5 -> 24.5 ms average, Map062 23.2 -> 18.7, Map001 22.5 -> 13.9, others 6-9 ms;
all ten state digests identical to the PC's, so the 3DS runs the game's scripts
exactly as the PC does. **mruby hosts the game** -- 0 errors, 14 MB, average frame
at or under budget on every map tested. Carried to M3/M10: per-map first-frame
spikes (93-196 ms, inside the screen transition), GC tails (p99 ~50 ms; schedule GC
work in the frame's idle time, which needs M3's real frame loop), boot (archive
index 1.7 s -> index cache; script compile 2.1 s -> bytecode cache) and map loads
(0.8-1.9 s). Next: M3, the RGSS graphics classes on citro2d.

### 10.7 M3 started: the engine (`engine/`, 2026-10-19)

The engine now has its own folder, with the M2 gems moved into it. New:
`mruby-ttm-gfx`, RGSS1's Bitmap (stb_image, stb_truetype with the game's Open
Sans), Viewport, Sprite, Window (windowskin layout) and Graphics in C, with one
shared core and two backends -- a software renderer on the PC that saves PNG
screenshots, and citro2d on the 3DS (640x480 drawn into a VRAM target, shown at
half size on the top screen). **On the PC the engine runs the game's opening from
the player's archive**: starfield and logo, title screen, credits, and the first
scene with the game's own message box, name tab and dialogue text, all drawn by
the game's own scripts. One RGSS detail surfaced: equal-z ordering must follow
mkxp's "an object whose z changes goes on top" rule, or the message system's name
tab hides the speaker's name. Not drawn yet: map tiles (M6), panoramas/fogs,
tone, transitions; no sound (M7). The first 3DS build (`engine/ttm3ds.3dsx`) is
awaiting a hardware run.

### 10.8 M3 first hardware run: an asset must not be able to end the game
(2026-10-20)

The first run on the console got through boot, the archive index, the title and
the credits, and then exited on the map load with:

```
ERROR #<RGSSError: cannot decode image …Tileset… outofmem (472708 bytes: 89 50 4e 47 0d 0a 1a 0a 00 00 00 0d)>
  118_TTMTranslator / rgss.rb:RPG::Cache.load_bitmap
  028_Spriteset_Map:21: @tilemap.tileset = RPG::Cache.tileset($game_map.tileset_name)
  076_Scene_Map
```

`graphics/Tilesets/Tileset.png` is **256×6096** (§5.1's mega surface). stb's
`outofmem` means a `malloc` failed — in this stb version the size-validation
paths say `too large`, so it really was the heap — and the leading bytes in the
message are an intact PNG signature + IHDR, so the archive read was fine. The
bug is *when* the 1024 px limit was checked: `backend_c3d.c` refused the texture
at *draw* time, after `bitmap.c` had already decoded 6 MB of RGBA for an image
the PICA200 can never sample. One asset, no fallback, `main.c` reports the
exception and the game exits.

The fix moves the question in front of the decode:

1. `gfx.h` gains `gfx_max_tex_dim()` (3DS: 1024, PC: 0 = unlimited) and
   `gfx_mem_free_kb()` (`mallinfo()`, 3DS only). Because the *backend* declares
   the limit, the PC's path — and therefore its reference screenshots — is
   untouched: this is the same "one shared core, two backends" rule the
   clipping and z-order work already follows.
2. `bitmap.c` asks `stbi_info_from_memory()` (header only, allocates nothing)
   first; an image over the limit is **not decoded** — it is logged once per
   asset and becomes a blank 32×32 bitmap. A decode that fails with `outofmem`
   or `too large` does the same, logging the free heap so the next run reports
   how much RAM was left (the number that says whether §5.1's memory budget,
   not just the texture limit, is the binding constraint).
3. `RPG::Cache.load_bitmap` (rgss.rb) catches `RGSSError`/`Errno::ENOENT` and
   caches the same blank stand-in, so no asset — tileset, windowskin, face
   graphic from a translation pack — can abort the game.
4. `$ttm_strict_assets = true` puts the fatal error back for debugging.

Consequence for the milestone: with the oversized images skipped the map load
completes instead of exiting, so the opening scene and its message box are
reachable on the console; the tiles, road and car stay invisible until M6
(sliced mega-surface textures + a real `Tilemap`), which is also where
`Vehicles`/`shuttle`-style sheets come back (cell-level loading rather than one
1143 px texture). Whether the scene then runs within the memory budget is what
the next hardware run measures — `gfx_mem_free_kb()` now reports the free heap
on every skip and every failed decode.

### 10.9 The save path, and what only a real save could show (2026-10-20)

M5's save/load half is in: `Marshal.dump` now sits beside `Marshal.load` in
`engine/gems/mruby-ttm-core/src/marshal.c`. A 9,000-frame PC run used to end with

```
096_TtM Util:15:in write_save_data: undefined method 'dump' for Module (NoMethodError)
```

→ the game's autosave (`$ttm.s()`, called from an event through `command_355`'s
`eval`) is twelve `Marshal.dump(obj, file)` calls plus UMS's `$ams` — and it now
runs to the end of the loop and reads its own save back.

**The writer reproduces CRuby's bytes**, checked rather than asserted.
`work/probe_marshal.rb` dumps the constructs a To the Moon save contains with the
real Ruby (`rubyinstaller-3.4.11`, `work/marshal_fixtures/`), and
`engine/pc/marshal_roundtrip.rb` loads each fixture with the engine and writes it
back: **24 checks, all byte-identical** — primitives, `w_long` at the ±2**30
boundary, Bignums (also in the `l` form itself, via values CRuby also calls
Bignums), Ruby's own float text (shortest exact digits *and* Ruby's scientific
notation: `1e15`, not `1000000000000000`), `Struct`, class and module *values*,
user `_dump`, symbols, Hashes with a default, shared objects, cycles and object
links, plus thirteen streams through one file object. The one deliberate
difference is stated and tested: a String here has no encoding instance variable
(RGSS 1.x / Ruby 1.8 has none; CRuby 3.x wraps a UTF-8 string in `I` with
`:E => true`).

Two of those checks are worth naming because they pin things a model can get
wrong, and both were confirmed against the oracle: Ruby links **Floats and
Bignums** in the object table but not Fixnums or Symbols (so an `@` index counts
what it should), and a class/module *value* is written next to `c`/`m` as a plain
length-prefixed string — **not** as a symbol, unlike the class path next to
`o`/`u`/`S`/`U`, which does share the symbol table.

**The first real save found two bugs**, in the two places a synthetic fixture
would not have looked:

1. **`c`/`m` were written as symbols.** `$game_system` holds two class values
   (`@default_formatter = Paragrapher::Formatter`, `@default_artist =
   Paragrapher::Artist` — 110_modern algebra), so the engine wrote a stream its
   own reader then misparsed: it read the symbol's `:` tag as a length and
   reported `undefined class/module …` on load. Fixed on the writer (`c`/`m`
   carry a raw string), and `class_value`/`module_value`/`class_links`/
   `module_links` fixtures now cover it.
2. **The engine's `File` shim was character-based.** `rgss/compat.rb`'s `MemIO`
   sliced with `String#[]` and counted with `String#size`, which under
   `MRB_UTF8_STRING` are *characters* — right for the game's text, wrong for a
   save. One byte of a stream that happens to look like a UTF-8 sequence and the
   reader consumed one byte too many, so every following stream was read from the
   wrong offset (`Game_Screen` re-dumped 1 byte short; `actors` then failed with
   `incompatible marshal file format`). `MemIO` is `byteslice`/`bytesize`
   throughout now, and `File.write` returns bytes.

The error message for a missing class was also making a C mistake worth fixing:
`mrb_raisef(..., "%s", mrb_sym_name_len(...))` runs past the name, because the
symbol pool has no NUL after it — the message printed the *next* symbol as well.
It passes a String with `%S` now.

**Where it stands.** `engine/pc/save_roundtrip.rb` runs the game, lets it write
its own save and walks that file one stream at a time:

```
save: the game wrote its own save during the run
save: save/save4.rxdata, 121800 bytes
  characters 35 / frame_count 6 / system 4737 / switches 628 / variables 109 /
  self_switches 62 / screen 15834 / actors 1760 / party 1812 / troop 28 /
  map 95917 / player 869 / ams 3      -- all "dumps identically"
save: 13/13 streams re-dump identically, 0 bytes left over
```

The 95,917-byte `$game_map` is the interesting one: a `Table` through its own
`_dump`, `RPG::Event` page lists, and events whose `@map` points back at the map
(an `@` link, or the dump recurses to death). `engine/pc/run_pc.rb` reports the
same file at the end of a normal run (`121800 bytes, 13 streams, 0 bytes left
over`, autosave at 4,435 frames of play time on map 38). What is left on this
path is the *scenes*: `Scene_Save`/`Scene_Load` are the game's own scripts and
only need this plumbing, but nothing has driven them from the menu yet.

### 10.10 The game's files are real files now, and the PC Ruby reads our save
(2026-10-20)

§10.9 proved the save *format*; what it proved it on was an in-memory file store,
so on the console nothing actually persisted. Both halves are closed here.

**Storage.** `gems/mruby-ttm-core/src/fileio.c` adds the seven operations RGSS's
`File`/`Dir` need and mruby-io cannot supply on the console →
`file_write`, `file_size`, `file_mtime`, `file_delete`, `dir_mkdir`,
`dir_entries`, `dir?` — and `rgss/compat.rb`'s `TTMFiles` is built on them
instead of a hash, mapping **every** path the game passes under one writable root
(`$ttm_data_dir`): `sdmc:/3ds/ttm3ds/` on the console, `work/pc_data/` on the PC.
A write makes its parent folders, so the game's `Dir.mkdir($savePath)` and
`File.open("save/save4.rxdata", "wb")` work whether or not it remembers the first
one; a write that fails is logged (it goes to `log.txt`) and does not take the
game with it. `File#mtime` is a `Time` because `Window_SaveFile` shows one under
each slot, and reads stay on the byte-based `MemIO` §10.9 fixed.

`engine/pc/fileio_check.rb` checks that layer on its own: **19 checks, 0 failed**
→ the mapping (including `..` and drive letters being neutralized so nothing can
climb out of the folder), a 1,024-byte round trip through every byte value
(the character trap again), `File.size`, `mtime`, `Dir.mkdir` creating parents
and tolerating repeats, `Dir.entries`, `ENOENT` for a missing file — and the
language setting still reading as `"en"` with no file, which is what keeps the
game off its language screen. It writes only under `scratch/` and can therefore
be run **on the console** as well.

**The other side of M5's claim.** With the save now a real file,
`work/probe_save_read.rb` has the *real* Ruby read it: CRuby 3.4.11 loads all
thirteen streams in the game's own order and re-dumps each one →
**13/13 byte-identical, 0 bytes left over**, with the same sizes and offsets
`pc/save_roundtrip.rb` printed from inside the engine. That includes the 95 KB
`$game_map`, whose `Table` travels as user marshal (the editor's own
`Map009.rxdata` shows the header convention `[dim, xsize, ysize, zsize, size]`
= `3, 20, 15, 3, 900`, which is what our `_dump` writes too), the `Color`/`Tone`
streams as four little-endian doubles, Ruby's own float text, class values,
object links and cycles. In other words: a save written in the engine's format is
one a PC copy of the game can load, and the check runs in the other language,
against the real file.

Two things that came out of writing that probe, worth knowing for the next one:
CRuby's `Marshal.load(io)` cannot be retried after an `undefined class/module`
error — the retry has to re-parse from a *positioned* `StringIO`, because the
failed attempt has already moved the stream; and CRuby reports a missing class
name truncated (`undefined class/module RPG::` for `RPG::EventCommand`), so the
class-defining loop has to cope with a prefix and keep going.

**What this leaves on the save path:** the *scenes*. `Scene_Save`/`Scene_Load`
are the game's own scripts and now have everything they call; nothing has driven
them from the menu yet, and no save has crossed the console/PC boundary for real
(the file layer is what makes that possible now).



### 10.11 Second hardware run: the file layer on the console (2026-10-20)

New 3DS XL, `ttm3ds.3dsx` with the file layer of §10.10. Four photos plus `log.txt`.

**What worked.** The app booted (heap 76,144 KB, linear 49,152 KB) and opened the
archive (979 files). The title screen drew at half size, with *Begin / Load / Exit*.
In-game, the GUBID menu drew with readable text: the Characters/Notes/Items tabs
and Save/Load/Exit Game. On a map, the message box (*"Wyles household ahead."*),
the characters and the event sprites all drew. `log.txt` appeared on the SD card,
so writing to the card works. Asset skips were survived as §10.8 intended: eight
over-limit images were skipped and the game kept running.

**What crashed: the save/load screens.** `Load` on the title screen, and both
`Save` and `Load` in the menu, ended in
`NoMethodError: undefined method 'strftime' for Time` at
`046_Window_SaveFile:84`. mruby's `Time` has every field but not the formatter.
The PC never hit this because no PC run opened a slot window with a file in it,
and only a slot with a file reaches that line (`if @file_exist`). So the crash
itself shows that **a save file already existed on the card**: the save half ran
on the console. Fixed by a `Time#strftime` in `rgss/compat.rb` (`%Y %m %d %H %M
%S` and the other common codes; `%z` is left out because mruby's `utc_offset`
has the wrong sign). `pc/save_roundtrip.rb` now also builds the slot window,
runs one frame each of `Scene_Save` and `Scene_Load`, and loads the slot the way
choosing it does: **4/4** (map 8 at 18,8 before and after). The console's
`File#mtime` gets a fallback too. libctru's `stat` on `sdmc:` leaves `st_mtime`
at 0, which would date every slot 1970, so `TTM.file_mtime` falls back to
`archive_getmtime` on the 3DS.

**The memory number was misreported.** `gfx_mem_free_kb` returned
`mallinfo().fordblks`, which is only the free space inside the memory malloc has
already taken from the heap, not the heap's headroom. It printed 2,635 KB at the
title screen, then 35,300 KB later in the same run. It now returns that plus the
part of the heap malloc has not taken yet (`envGetHeapSize() - arena`). The
next run's first `gfx:` line is the §5.1 number; this run's values are only a
lower bound.

**Over-limit images in this run.** It is not only tilesets:

| Image | Size | Kind |
|---|---|---|
| `Pictures/Title Scroll` | 640×1500 | picture |
| `Tilesets/town2` | 256×9332 | tileset |
| `Tilesets/Tileset` | 256×6096 | tileset |
| `Panoramas/NightCliff` | 1645×575 | panorama (Plane) |
| `Characters/Light (x4Size)` | 1024×1200 | character sheet |
| `Characters/Vehicles`, `Vehicles 2` | 1143×686 | character sheets |
| `Pictures/heartoverlay2` | 1390×51 | picture |

So M6's texture slicing has to work in the `Bitmap`/sprite backend for every
drawable, not only inside `Tilemap`.

**What this run could not reach: the autosave by walking.** With no `Tilemap`,
the map is black apart from the sprites, so the event that autosaves cannot be
found on foot. On the console, the menu's `Save` is the way to test save and
load until M6.

**Top-screen layout changed (user request).** The frame now fills the top screen
corner to corner. It is stretched from 640×480 to 400×240, so 0.625 across and
0.5 down, which makes the picture 5:3 and 25% wider than the game drew it.
**SELECT** switches back to layout option A (4:3 at half size, centred) and the
game never sees that key. This applies to the `backend_c3d.c` frame draw only.
The PC renderer and its screenshots still show the game's own 640×480.

### 10.12 Third hardware run: save, load and full screen confirmed (2026-10-20)

- **Save and load work on the console.** The menu's Save wrote `File1` (play time
  00:02:58) next to the game's own `Auto-Save` slot (00:02:04). The slot windows
  draw the party sprites, the play time and a date. Load on the title screen
  listed both, put the cursor on the newer one (`Scene_Load` chooses by `mtime`,
  so the timestamps sort correctly), and put the game back on the map. M5's
  console half is done.
- **Full-screen 5:3 top screen works**, and the stretched text stays readable on
  the load and save screens.
- **The memory budget (§5.1), measured properly at last:** **71,041 KB** of heap
  headroom at the first skipped image (the title screen), and **48,906 KB** at
  the lowest point seen (on a map, `NightCliff`). Memory is not the binding
  constraint. M6 can decode a full tileset on the CPU (`town2`, 256×9332 RGBA,
  is 9.3 MB) and slice it into …1024 px textures without streaming. The linear
  heap (48 MB, where textures live) is the budget to watch, not the main heap.
- **Slot dates read 2011/01/01 00:02 and 00:03**, i.e. the 3DS factory date plus
  roughly the play time. Most likely the console's own clock is unset or was
  reset, not an engine bug. The app now writes `clock YYYY/MM/DD HH:MM` (the
  console's time at startup) as the first line of `log.txt` to settle it.
- The map is still black apart from sprites, as expected until M6 `Tilemap`.

### 10.13 M6 step A: oversized images drawn as slices (2026-10-20)

The 1024 px skip of §10.8 is replaced by slicing. A bitmap larger than the
texture limit keeps its single CPU pixel buffer, but its texture copy is a grid
of textures of at most 1024 px a side (`ttm_bitmap.tiles`, made on first draw,
refilled when the pixels change). `ttm_bitmap_draw` in `bitmap.c` is now the
only path from a bitmap to the screen, for sprites and windows alike. It cuts
the quad's texture rect at slice edges and places each piece at its offset
inside the quad (scaled, rotated about the quad's corner, measured from the
right when mirrored), so callers never see the limit. The backends gained a
stride on `gfx_tex_upload`, so a slice uploads straight from the big buffer.
Slice counts at 1024: `town2` 10, `Tileset` 6, `Title Scroll` 2, `NightCliff` 2,
`Light (x4Size)` 2, `Vehicles`/`Vehicles 2` 2 each, `heartoverlay2` 2. Each is
logged once with the free main heap and the free linear (texture) heap.

`pc/slice_check.rb` draws all eight images in 256 px slices and as one texture
and compares the frames: **11/11**. The cases are a picture across slice rows,
tileset strips, 32 px tiles at 2× across slice edges, a sheet at half size, a
mirrored sheet, a panorama partly off screen, an add-blended half-opacity strip,
bush depth across a slice edge, zoom 1.3 and a 30° rotation. All are identical
pixel for pixel except zoom 1.3, where 730 pixels differ. Those rows are 13 px
apart, exactly where a pixel centre lands on a texel edge, so they are float
ties, and no pixel goes black (no gaps). A 900-frame PC game run with the
console's 1024 px slicing forced shows the title scroll drawing from its two
slices. The other checks are unchanged: `fileio_check` 19/19,
`marshal_roundtrip` 24/24, `save_roundtrip` 13/13 + 4/4.

Waiting on the console: whether slicing holds on the PICA200, and the
linear-heap figures as the tilesets load.

### 10.14 Fourth hardware run: slicing works; texture memory was the next wall (2026-10-20)

**Slicing holds on the PICA200.** The title's sky scroll and the car
(`Vehicles`, 1143×686) drew on the console. All eight oversized images loaded,
2×10 slices each.

**But texture memory ran out.** The `tex` figure on each `gfx:` line fell from
44,049 KB to 33,809, 29,841, 24,073, 11,785 and 6,335. After that, every new
texture failed (`out of texture memory (127x32)`, `(162x32)`, `(640x480)`).
`town2` also failed to decode on the second map visit, with stb saying
`no SOI` for a valid PNG. The cause is the same for both. A `Bitmap` is a
small object to mruby's collector but holds megabytes it cannot see: pixels in
the main heap and textures in the 48 MB linear heap. The game drops message
and name-tab bitmaps without `dispose`, and the collector, which runs by object
count, frees them long after both heaps have run dry. Worse, a texture that
failed was kept as a dead texture, so its sprite never drew again. That is why
a character and some of the dialogue vanished, and why the name tab drew as an
empty box.

Fixes:
- **Textures are a cache over the CPU pixels** (`bitmap.c`). Bitmaps holding
  textures sit on one list, most recently drawn first. When a texture cannot be
  made, bitmaps *not drawn in the current frame* give theirs up, oldest first,
  and it is tried again. Their pixels stay, and they upload again when next
  drawn. Nothing the GPU still has to draw this frame is freed. A texture that
  still cannot be made is simply retried next frame, never kept dead.
- **Forced collections** (`graphics.c`, between frames). When the linear heap
  is under 8 MB, the main heap under 16 MB, or a texture could not be made, a
  full `mrb_full_gc` runs, at most once a second. The same happens before
  decoding a big image when RAM is short, and once more after a failed decode.
  A decode failure of an image whose header read fine now counts as "out of
  memory" (a blank bitmap and a log line), whatever stb calls it.
- **Reproducible on the PC.** `$ttm_tex_budget_kb` makes the software renderer
  pretend texture memory is that size, padded to powers of two as on the 3DS.
  `Graphics.ttm_tex_stats` returns `[bitmaps holding textures, KB, textures not
  made, forced collections]`, and `run_pc.rb` logs it on each screenshot line.

PC run, 4,000 frames to the car scene, 1024 px slices, 44 MB budget: **0
textures not made**, 1 forced collection, peak 363 bitmaps holding 35.5 MB.
The car scene draws with the message box and the "Dr. Watts" name tab. With a
deliberately impossible 16 MB budget, the held total never exceeds the budget
and the game keeps running. Textures fail only when one frame genuinely needs
more than 16 MB. All other checks unchanged.

### 10.15 M6 step B: `Tilemap` in C (2026-10-20)

`gems/mruby-ttm-gfx/src/tilemap.c` replaces the Ruby stub, with the same API
that `Spriteset_Map` uses (`tileset`, `autotiles[i]=`, `map_data`,
`priorities`, `ox`/`oy`, `update`, `dispose`). `flash_data` is kept but not
drawn.

- **Tiles:** only the visible 21×16 cells are drawn. Map coordinates wrap, as
  RGSS's tilemap does. Ids … 384 are drawn from the tileset through
  `ttm_bitmap_draw`, so a sliced tileset works unchanged. Ids 48×383 are
  autotiles. Each sheet is assembled once into a 256-wide bitmap of its 48
  patterns per animation frame, from 16 px quarters with RMXP's pattern table.
  It is rebuilt when the sheet changes, recognised by a new per-bitmap serial
  number, because malloc reuses addresses across map changes. A 32 px high
  autotile is a plain tile animated sideways. Animation advances every 16
  `update`s.
- **z, as RGSS:** priority-0 tiles are at z 0. A priority-p tile on map row y is
  at (y·32 — oy) + 32 + 32p, against a character's `screen_z` of its row + 32
  (+31 when tall). This is done with twenty row layers, drawables at
  z = row·32 — oy + 64; a priority-p tile goes into the layer of row y + p — 1.
  Their z is set directly as `oy` moves, so they keep their creation order, and
  a sprite at the same z (always made later) draws on top.
- **Texture padding:** a bitmap is now cut on whichever of a 1024, 512 or 256 px
  grid pads least to powers of two (`grid_for` in `bitmap.c`). At the console's
  limit: Title Scroll 3,840 KB (was 6,144), NightCliff 3,744, Vehicles 3,456
  each, town2 9,344, and a 640×480 bitmap 1,280 KB (was 2,048).
- **Collections:** a merely full texture heap no longer forces one. Eviction
  already takes dead bitmaps' textures first, since they are never drawn. A
  forced collection now runs only for low RAM or a texture that could not be
  made even after eviction.
- **fps on the console:** once a second, the bottom screen's last line shows
  `NN fps  render N.N ms  tex N KB free`.

`pc/tilemap_check.rb`, 5 maps (car scene 038, lighthouse 008, Wyles house 001,
the two maps with the most autotile samples, 041 and 045), each viewed around
its middle at an odd ox/oy with 256 px slices: **17/17**. That covers 363 plain
tileset cells and 335,000 pixels identical to the tileset, 16 autotile cells of
patterns 0/47 identical to the sheet, and a priority-1 tile in 5 maps
correctly over a sprite at z→1 and under one at its own z. The 4,000-frame PC
run, with a 44 MB texture budget and 1024 px limit, made 0 failed textures and
0 forced collections. Screenshots show the car scene (road, guardrails, grass,
cliffs, pond, trees) and the Wyles house (walls, rugs, stairs, piano, furniture)
drawn as in the game. Other checks unchanged: slices 11/11, files 19/19,
marshal 24/24, save 13/13 + 4/4.

### 10.16 Fifth hardware run: maps on the console; 30 fps traced to pacing (2026-10-20)

**Maps draw on hardware.** The run covered the Wyles house with the opening
credits, the car scene, the dreamscape paths and the menu. Characters and
dialogue stayed visible, and the texture heap never ran out. The lowest `tex`
figure was about 7.9 MB free, recovering as eviction worked. Maps whose
background is a panorama (`NRS1001`, `NightCliff`) show black around the
tiles, which is Step C.

**fps:** a flat 30 in the house, 30×40 in the car scene, 38×40 in the
dreamscape maps, and 26×33 in the menu. Movement felt slow, and it *was* slow:
RGSS advances the game one update per frame, so at 30 fps everything runs at
75 % speed. The flat 30 came from the pacing, not from the work.
`C3D_FrameBegin(C3D_FRAME_SYNCDRAW)` waits for the screen's 60 Hz refresh, so
any frame costing more than 16.7 ms took 33.3 ms. Fixes:
- **Clock pacing.** `C3D_FrameBegin(0)` plus a 40 Hz timer, so a frame takes
  max(its cost, 25 ms). The timer stays behind when late, resyncing only after
  a stall of more than four frames.
- **RGSS's frame skip.** When the coming frame would be late (now + the last
  drawn frame's cost > its due time), `Graphics.update` runs the game's update
  without drawing, at most twice in a row. The game keeps its speed and a heavy
  scene shows fewer frames, not slow motion.
- **A status line that says where time goes:** `drawn/updates fps`, `rb` = the
  game's Ruby per update, `dr` = drawing per drawn frame (ms), `tx` = free
  texture KB. It is now under 40 columns; the old one wrapped and scrolled the
  log on every print.
- **Ruby trimmed where it is ours.** On the PC (profiler, house map) the game's
  Ruby costs about 0.6 ms a frame, spread across its own scripts (anti-lag
  13 %, characters 17 %, character sprites 10 %, the map 7 %, mouse scripts
  9 %). The ARM11 runs it roughly 30× slower. Our share went:
  `RPG::Sprite#x=`/`y=` pass-throughs removed, and `RPG::Sprite#update`
  returns at once unless a battle effect runs (the game never starts one).

**`Vehicles 2` failed to decode once (`no SOI`) with 42 MB free.** That reason
is stale: `stbi_info` tries JPEG first and leaves "no SOI" behind, and a PNG
decode that fails without naming a reason reports it. `ttm_stbi_clear_failure()`
now clears it before each decode. A failure retries once after a collection
*from a fresh read of the archive*, and logs both attempts' reasons and the
file's first bytes. The PC decodes the same file every time (`slice_check`),
so the next console log should say whether the bytes or the memory were at
fault.

### 10.17 M6 step C: `Plane` in C — panoramas and fogs (2026-10-20)

`gems/mruby-ttm-gfx/src/plane.c` replaces the Ruby stub, and with it the last
use of `TTMDisposable`, which is removed. `Spriteset_Map` uses two planes: the
panorama (z →1000, `ox`/`oy` = display ÷ 8) and the fog (z 3000, zoom, opacity,
blend, tone).
- The bitmap is repeated over the plane's viewport (the clip rectangle, now
  readable through `ttm_clip_current`), or over the screen without one. It is
  shifted by `ox`/`oy` (screen pixels, wrapping) plus the viewport's
  `ox`/`oy`, so a screen shake moves it with the map, and each repeat is
  scaled by `zoom_x`/`zoom_y`.
- `opacity`, `blend_type` and `color` work as on a Sprite. `tone` is accepted
  and not drawn, the same gap sprites have. That matters for fogs, which the
  game tints with `fog_tone`.
- Every repeat goes through `ttm_bitmap_draw`, so `NightCliff` (1645 px, 8
  slices at 512) works unchanged, and `ttm_draw` drops the pieces outside the
  viewport.
- Assumption: with zoom … 1, `ox`/`oy` are taken as screen pixels, not texels.
  Only the fog zooms, and the game's fog zoom is normally 100 %.

`pc/plane_check.rb`, every 5th pixel against the texel worked out from the
bitmap directly: **6/6**, 12,288 pixels each, 0 differing. The cases are
scrolled, wrapped past twice its width and once its height, negative offsets,
inside a 300×200 viewport (black outside), zoom 2, and half opacity. The
4,000-frame PC run (1024 px limit, 44 MB texture budget) had 0 failed textures
and 0 forced collections. Frame 900, the lighthouse cliff, now shows the night
sky, moon, clouds, sea and island behind the lighthouse, where the console
showed black. The Wyles house's windows show the night-sky panorama. Other
checks unchanged: tilemap 17/17, slices 11/11, files 19/19, marshal 24/24,
save 13/13 + 4/4.

### 10.18 Run 9: maps play, then RAM runs out; decoded pixels become a cache (2026-10-21)

Console run 9 (banded images, PNG by rows) loaded and drew every tileset,
panorama and oversized sheet in its log, then stopped on the bedroom map with
`RGSSError: failed to create bitmap 510 x 84: out of memory`. The log shows
why: free RAM fell 67 — 54 — 42 — 30 — 12 — 9.5 MB as images arrived and
never recovered, and at the end even a 640×480 picture failed to decode with
9.5 MB "free". `RPG::Cache` keeps every image the game loads, and each bitmap
held its pixels in RAM besides its textures. On the PC, 4,000 frames in, that
is 37.8 MB of pixels, 27.6 MB of them file images that nothing has drawn into.

`bitmap.c` now treats those pixels as a cache. When RAM is under 20 MB free,
or an allocation fails, the least recently read file images give their pixels
up. Those with current textures go first, since they still draw. Anything that
needs the pixels again decodes the file first. Bitmaps the game drew into are
never shed. Bands went from 128 KB to 64 KB, and failure lines now log free
RAM, the largest block malloc can give, and pixel bytes (all / sheddable).
`pc/shed_check.rb` 25/25. The 4,000-frame PC run with a 16 MB pixel budget
held 10.6 MB instead of 37.8 MB (81 images shed, 31 decoded again, 0 failed),
and all 40 screenshots match the unbudgeted run byte for byte. The other
checks are unchanged, and the pixel-exact ones also pass under a 4 MB budget.
What run 10 has to show: whether the "px … sheddable" part of RAM really is
most of it on the console, or whether something other than pixels (Ruby
objects, map data) is what fills the heap.

### 10.19 Run 11 build: sound (M7), tile events, fast-forward and the HOME menu (2026-10-23)

After run 10 the user reported the game playing through with three faults: a
"moving table" that never showed, parts that ran too fast, and a frame rate
that stayed low after a visit to the HOME menu. All three were traced to a
cause in the code or the game's data before anything was changed.

| Fault | Cause | Fix |
|---|---|---|
| Objects missing | `RPG::Cache.tile` returned a blank bitmap. An event whose graphic is a tileset tile draws through it (`024_Sprite_Character:37`), and **488 event pages on 69 maps** do: books, flowers, vases, stairs, the furniture the game moves | the tile's 32×32 cell of the tileset, as RGSS builds it |
| Runs too fast | Common event 85, "FAST GFX UPDT", runs in parallel all game: RGSS L held — `Graphics.frame_rate = 160`; RGSS A held — 40. The 3DS's L was mapped to RGSS L *and* was the timeline button, so looking at the timeline left the game in fast-forward until Y was pressed | L is the engine's own key 99 (`Input::TTM_TIMELINE`), never RGSS's L. `Graphics.frame_reset`, a no-op until now, also resets the pacing, so the frames after a stall the game expects are no longer run back to back |
| Slow after HOME | `PTMSYSM_ConfigureNew3DSCPU(3)` was called once at start-up. libctru's `apt.o` calls `__ctru_speedup_config()` on every resume, which applies only what `osSetSpeedupEnable()` set, so each return from HOME left the console at **268 MHz without L2** | `osSetSpeedupEnable(true)`; an apt hook logs `apt: resumed` and resets the frame pacing |

**Sound.** The user chose to shrink the audio on the PC first. The main
game's `Audio/` is **268 sounds, 35.5 MB** (the 71 MB in §3 counted the two
holiday specials as well). `tools/pack_audio.py` re-encodes them with ffmpeg's
libvorbis at most 32 kHz into one file, `audio.ttma`, of **22.1 MB**:

| Kind | Sounds | Before | After |
|---|---|---|---|
| BGM | 29 | 24.8 MB | 15.9 MB |
| BGS | 16 | 1.9 MB | 1.1 MB |
| ME | 5 | 4.3 MB | 2.5 MB |
| SE | 218 | 4.5 MB | 2.6 MB |

Each name is packed once; the .ogg wins where a name also exists as
.wav/.mp3/.wma. Every packed stream is decoded back and its length compared
with the original's: **268/268**. One 0.5 s click comes back 30 ms longer,
which is silent Vorbis end padding. None of the game's files carry loop tags,
so music loops the whole file.

`gems/mruby-ttm-audio` follows the graphics gem's pattern: one shared core and
two backends.
- **Core (`audio.c`).** The game's calls only queue commands, and RGSS/mkxp
  rules apply. Playing the same BGM again only changes its volume. BGM and
  BGS loop. An ME silences the BGM, which fades back in over a second
  afterwards. Fades are linear, volume is linear and pitch is a rate. SEs
  play on 12 voices, each sound decoded once into a least-recently-used
  cache. A missing sound is logged once and stays silent.
- **3DS backend (`backend_ndsp.c`).** NDSP, fed by a thread on core 2. Its
  linear memory (192 KB of stream buffers and a 4 MB SE pool) is reserved
  at start-up, before textures can fill the linear heap.
- **PC backend (`backend_null.c`).** Silent, but it decodes on a clock the
  caller advances, so looping, fades and the ME's end are tested with the
  console's decoder.

Decoding is stb_vorbis (v1.22, the commit the image code's stb headers come
from) on both machines.

**What the game asks for.** `work/audio_refs.py` lists every sound the game's
data names: map autoplay, event codes 241/245/249/250, move-route SEs,
`System`, animation timings, and the thunder in the weather script. That is
235 names. **209** are files the game ships, and all of them are in the pack.
The other **26** are not in the game at all: RTP battle MEs, unused skill
animations, and map 127's `Ending TTM`. They are logged and silent, as a
sound the PC version cannot find would be.

**PC verification**

| Check | Result |
|---|---|
| `pc/audio_check.rb` | **29/29**: every stream decoded to its end by stb_vorbis; every shipped sound present; RGSS's behaviour on the virtual clock; all 218 SEs within the 4 MB pool |
| `pc/tile_event_check.rb` | **7/7**: all 201 distinct event tiles pixel for pixel against their tileset cells, also with 256 px slices under a 4 MB RAM budget |
| `pc/controls_check.rb` | **16/16**, now including "L held is not RGSS's L" |
| A 4,000-frame PC run | the game's own calls play the title theme over the sea, *For River*, and *Between a Squirrel and a Tree* with night crickets; SEs come from the cache; 0 missing, 0 errors |
| All earlier checks | unchanged: tilemap 17/17, slices 11/11, shed 25/25, plane 6/6, tone 19/19, keep 14/14, files 19/19, marshal 24/24, save 13/13 + 4/4 |

The two decoders disagree only on silent end padding. On `004-System04`
(6,183 frames in the game's own file) stb_vorbis gives exactly 6,183 and
ffmpeg 6,528; on `002-System02` ffmpeg is exact and stb adds 256 frames. So
lengths are compared within half a Vorbis long block (1,024 frames).

**What run 11 has to show on the console:**
- the books, flowers and furniture on the house maps;
- the timeline on L without the game speeding up;
- the frame rate after HOME and back, plus `apt: resumed` in the log;
- music, ambience and the menu and text sounds;
- the `audio:` lines in `log.txt`: the thread's core, its share of that core
  (decoding included), and the SE cache.

### 10.20 Run 11b build: Show Animation, weather, and the sound inside the app (2026-10-23)

**Show Animation** (event command 207, **841 uses** across 19 animations) was
the effect still missing. It covers:
- emotion balloons over characters: "...", "!", "?", "Excited", "Curse",
  "Panic" (about 460 uses);
- target flashes: "White flash" and "Multi color flash";
- "Control" and "MEMENTO DETECTED" on the screen;
- "Tile Flip".

`RPG::Sprite#animation` only counted down. It is now RGSS1's own code,
ported into `rgss.rb`:
- 16 cell sprites showing 192×192 cells of the animation's sheet;
- a frame every 2 game frames, with RGSS's placement, zoom, angle, mirror,
  opacity and blend;
- position 3 on the screen;
- each timing's SE and flash on its frame;
- the cell sprites disposed at the end, and `loop_animation` as well.

Three changes from RGSS, all for the 3DS:
- Character sprites still return at once from `update` when no effect is
  running (0.23 µs per idle sprite on the PC).
- Only a sprite that has shown an animation carries the `x=`/`y=` overrides
  that move its cells (`TTMAnimFollow`, added with `extend`).
- An animation sheet stays in `RPG::Cache` when its last animation ends, so
  the next balloon needs no decode.

**Weather was never missing.** The game's script 094 (ccoa's weather, 19
types) reopens `RPG::Weather` after the engine's stub and draws with our
Sprite and Bitmap. The game uses two types: swirling leaves (type 8; maps 16,
17, 50, 90, 154) and falling ash (type 18; map 41). Both draw on the PC. One
cost is fixed: that script keeps 500 hidden sprites on every map, and the
renderer looked up each one's viewport every frame. Hidden drawables are now
skipped before that lookup (`graphics.c`).

**The sound inside the app.** `build.ps1` copies `work/audio/audio.ttma` into
the app's romfs. `main.c` reads `romfs:/audio.ttma`, else the SD card copy.
The 3dsx is 24.4 MB. Such a build contains the game's music and is for the
owner's console only (§7); `build.ps1 -NoAudio` builds the engine alone.

**PC verification**

| Check | Result |
|---|---|
| `pc/effects_check.rb` | **16/16**: cells per frame, placement against RGSS's formula, the balloon drawn, cells following the character, 20 frames in 40 game frames then disposed, position 3, the timing's SE, White flash, all 100 database animations to their end, idle cost; leaves and ash on screen and moving |
| 4,000-frame PC run | 49 of 50 screenshots byte-identical to the run before; the 50th (frame 3,200, the car scene) differs only by the game's own "Curse" balloon over Eva |
| All other checks | unchanged: tilemap 17, slices 11, shed 25, plane 6, tone 19, keep 14, files 19, marshal 24, save 13 + 4, controls 16, tile events 7, audio 29 |

### 10.21 Run 12 build: start-up caches, faster map changes, touch buttons (2026-10-23)

**Start-up.** The M2 hardware run measured two costs before the title screen
appears: **1.9 s** to index the archive (979 seeks and reads across 36 MB on
the SD card) and **2.0 s** to compile the game's scripts. Both are now cached
in `SD:/3ds/ttm3ds/cache/`:
- `archive.idx` holds the finished index (45 KB). It is used while the
  archive's size and the CRC of its first and last 4 KB match. The card's
  files have no usable mtime on the 3DS (§10.11).
- `NNN_<crc>.mrb` holds each script's mruby bytecode with its debug info, so
  backtraces keep script names and lines. The file name carries the CRC of
  the *patched* source, so a changed patch compiles afresh.

Both paths run a script the way `mrb_load_nstring_cxt` does (at the top
level, with target class Object). A damaged or stale file is rejected by
mruby's own header and compiled again.

| PC | Cold start | Warm start |
|---|---|---|
| index | built | from the cache |
| 116 scripts | 106 ms compiled | 21 ms from bytecode |

The game runs identically from bytecode: a 4,000-frame run gives **50/50
screenshots byte-identical** with a cold cache and with a warm one.
`pc/boot_check.rb`: **16/16**. It covers:
- index equality and reads through the cached index;
- rebuilds for another size, other content or a cut-short file;
- bytecode reuse;
- backtraces from cached code;
- a damaged file compiled again;
- syntax errors still raised.

On the PC the cached index is no faster: its disk makes 979 seeks nearly
free. The console's numbers come from run 12's `archive:` and `scripts:` log
lines.

**Map changes** (`pc/mapload_profile.rb`: each `transfer_player` timed in its
parts, with the line profiler on). Two findings, both fixed:
- **Weather.** The game's weather script makes 500 sprites on every map load,
  weather or not, and moves all 500 whenever the map scrolls. Inside
  `module RPG`, its `Sprite.new` is `RPG::Sprite`, with Ruby on creation and
  dispose (~10% of a map change). A `ttm_patches.rb` patch now makes plain
  `::Sprite`s, only as many as `max=` asks for (the game asks for at most
  52). The 1,000 random numbers the original drew are still drawn, in the
  same order, so nothing else the game does at random changes.
- **Pictures.** `Sprite_Picture#dispose` disposes RPG::Cache's own picture
  bitmaps on every map change, so the next map read and decoded them again.
  That decoding, reading and disposing was over a third of a map change.
  `bitmap.c` now keeps any disposed file image of 16 KB or more for reuse,
  in 32 slots within the same 16 MB (it was … 1 MB, 4 slots).

Together these cut the profiled map-change time by about 30% (490 — 344
samples). For example, from map 9 to 8 went from 80 to 60 ms on the PC. What
remains is mostly first decodes of images. Each map change now logs
`map load: A -> B in N ms`.

**Touch buttons.** Once the game runs, the bottom screen is drawn with
citro2d and has four buttons plus a status row:
- **Menu**: RGSS B.
- **Save**: the game's own `save_calling` path to `Scene_Save`.
- **Timeline**: held.
- **Confirm**: RGSS C.

Scene_Map reports each frame whether Menu and Save can be used
(`Graphics.ttm_touch_state`, patched in after the game's last script), so
they grey out during cutscenes. Off the map, Menu is "back" and Save is off.
Before the game starts and after an error, the bottom screen is the text
console again. `pc/touch_check.rb`: **7/7** in the running game:
- Save greyed for all 4,359 cutscene frames, and doing nothing when pressed
  there;
- the save screen opening 2 frames after a press on free map time;
- B returning to the map;
- off-map state, including during transitions;
- map loads logged.

All earlier checks are unchanged. keep_check now has 15 checks, the
threshold check rewritten for 16 KB.

### 10.22 Run 15: the language pack, translations, a face per name (2026-10-23)

**What was missing.** The game ships English inside its data, and its other
eleven languages live in `lang.dat` — a 17 MB ZIP in the install folder, which
mkxp mounts beside the `.rgssad` as an extra archive. The engine had no ZIP
reader, so `load_data("lang/index.rxdata")` could only fail; `rgss.rb` answered
it with an empty index instead (`TTM_LANG_INDEX`), which made `LANGUAGES` just
`["en"]`, and `compat.rb` reported the `save/lang` setting as present and `"en"`
whatever happened. That is what kept the game off its language screen and the
port in English.

**The pack.** 851 entries: 54 folders and 797 files — `lang/index.rxdata`
(4.1 KB: the codes and the pictures that carry text), `lang/<code>/lang.rxdata`
(the dialogue, one replacement per event), `lang/<code>/meta.rxdata` (the menu's
words and, for Korean and Chinese, the font), and 78×90 localised PNGs per
language. 768 entries are deflated, 29 stored. The largest is a localised
tileset, 3.9 MB deflated.

`gems/mruby-ttm-core/src/langzip.c` reads it the way `rgssad.c` reads the game:

| Call | What |
|---|---|
| `TTM.lang_open(path)` | the end-of-central-directory record (scanned back over a possible comment), then the directory itself in one read; 797 entries, ~10 ms on the PC. Entries are sorted and searched with `bsearch`, names normalised like the archive's (lower case, `/`) |
| `TTM.lang_read(name)` | the *local* header's name/extra lengths give the data offset (they may differ from the directory's), then one bounded read and `inflateInit2(-MAX_WBITS)` — raw deflate, as a ZIP stores it; inflated straight into the returned String |
| `TTM.lang_has?`, `TTM.lang_files`, `TTM.lang_close` | the checks and the log |

No unpacking and no index cache: the directory is one read of ~60 KB, and an
entry costs its own bytes, so a 17 MB pack adds nothing to startup and only what
is used to RAM.

**The game's own code, unchanged.** `rgss.rb`'s `load_data` and
`TTM.image_data` now ask the pack first for `lang/...` paths, so:
- `TR.load_index` finds the real index: `LANGUAGES` is `en` + 11;
- `TR.load_language(code)` builds the game's `TTMTranslator` from
  `lang/<code>/lang.rxdata` and `meta.rxdata`, swaps `$data_items` and patched
  `$data_common_events`, and the `load_data` override in `118_TTMTranslator`
  patches each map as it loads (195 text blocks on map 2 become 465 German
  lines);
- `118_TTMTranslator`'s `Bitmap#initialize` override asks for
  `lang/<code>/Graphics/...` and falls back to the game's picture, which is how a
  localised picture never depends on its text being in the same place.

**The setting.** `TTMFiles.exist?("save/lang")` now answers `false` when a pack
is open (`$ttm_lang_pack_on`, set by `boot.rb`) and no file has been written yet
→ which is exactly MAIN's first-run test, so `Scene_Language` appears. The choice
is written as a real file under `save/` and is what a later run reads. With no
pack the old behaviour stays: present, reading `"en"`, straight to the map.

**A face per name.** The language screen draws each language's own name in its
own font (`Window_Language#font_name`), and Korean and Chinese name fonts the PC
version shipped in `Fonts/` (NanumBarunGothic 4.2 MB, WenQuanYi Zen Hei 9.0 MB).
`font.c` had a single global face, so `font.c` now keeps a list:
`Font.ttm_face = bytes` is still the default face (Open Sans, standing in for
Arial and Times New Roman), `Font.ttm_face(name, bytes)` adds a named one, each
with its own glyph cache, and `ttm_text_width/height/draw` take the name from the
`Font` they are drawing with. A name with no file of its own draws in the default
face, so the nine Latin languages and the Russian and Turkish ones need no extra
files at all. The Ruby side (`rgss.rb`'s `Font#name=` / `Font.default_name=`)
loads a CJK face from the game's `Fonts/` the first time a `Font` asks for it, and
logs `font: NanumBarunGothic, 4087 KB, in 0 ms`. Both cost RAM only while the
pack is there and something has asked for them (13 MB with both loaded).

**Tests.** `pc/lang_check.rb` **47/47**: the 797 files and 54 folders, a
deflated entry byte-identical to `tools/extract_lang.py`'s copy, a stored PNG's
signature, case-blind lookups, a folder and a missing entry, the German menu
words and items, map 2's blocks and lines, a localised picture that is 640x480
where the game's is 542x473 (so the size says which file was read, through the
translator's own `Bitmap` path), the setting written and read back, both CJK
faces and the pixels their glyphs cover, and the no-pack fallback.
`pc/lang_screen_check.rb` **13/13**: the game really opens on `Scene_Language`
with 12 rows, each in its own font, and choosing German writes `save/lang` and
carries on into the German intro. The earlier checks are unchanged (18/18, 19/19,
16/16, 16/16, 16/16, 19/19), so the English path did not move.

**What this leaves.** The localised tilesets are the pack's biggest entries (a
256x11201 PNG, 3.9 MB) and go through the same slicing path as the game's own
(§10.8, M6): they decode like the English one and cost the same RAM, which is the
budget to watch in a translated run. The Holiday Specials and layered 3D are
next.

### 10.23 Run 16: the Holiday Specials, and the launcher (2026-10-23)

**They are two games.** The download's `Sigmund Holiday Special 1` and `2`
folders are not content inside To the Moon: each is a complete RGSS1 project →
an `RGSSAD v1` archive (11.3 MB / 332 files, 12.9 MB / 533 files), 114 scripts
of its own, its own maps, and `Audio/` as loose Oggs beside the archive. Their
script sets are the same family as the main game (stock RGSS1 + the
"Commercial" scripts + `UMS new` + `Caterpillar` + `Rach checker` + `Pause` +
`Weather`) but without the GUBID menu, `Achievements` or `TTMTranslator`. So the
engine runs them as *games*, not as content: a different archive, a different
writable folder, nothing else. The archive reader, `Marshal`, the renderer and
the audio needed no change — `Audio` already fell back to `Audio/<kind>/…` in
the game folder when no pack is there (§10.19).

**The launcher** (`source/main.c`). The app lists every `*.rgssad` in `APP_DIR`
and one folder below it, titles each from the `.ini` beside it (`[Game] Title=`,
which is where `SigCorp - Holiday Special` comes from), and:

| Games on the card | What happens |
|---|---|
| 0 | the missing-files console, now naming the minisode folders |
| 1 | boots straight in, exactly as before |
| 2+ | the bottom screen lists them; tap a row (touch `y / 8` picks the console line) or D-pad + A; `START + SELECT` quits; the choice is written to `last-game.txt` and preselected next time |

The main game keeps `APP_DIR` as its data folder, so **no existing save moves**;
anything else writes in `APP_DIR games/<its folder>/`. `$ttm_game_id` and
`$ttm_font_file` join the globals the app hands mruby, and a minisode's audio
pack is looked for in its own folder first (there is none: loose `Audio/`).

**The episodes inside the app.** `tools/pack_romfs.py` writes
`work/romfs_extra/`, which `build_cia.ps1` packs as `romfs:/game/` (the main
game: archive, `lang.dat`, `Game.ini`, fonts) and `romfs:/games/<name>/` (each
episode's archive, `Game.ini`, and only the sounds the main pack lacks — the
lookup is by `kind/name`, so of HS1's 257 sounds 229 are already in
`romfs:/audio.ttma`, of HS2's 277, 225). 104 MB, giving a 129 MB CIA that needs
nothing on the SD card but saves. `scan_games` looks at `romfs:/game/` and
`romfs:/games/` *after* the card, so a copy on the SD card replaces the built-in
one. `build.ps1` keeps it off by default (`-WithMinisodes` opts in), because the
Homebrew Launcher loads a `.3dsx` into RAM whole: a 130 MB 3dsx would not fit,
while a CIA's romfs is a partition read on demand.

**The dialogue that looked missing.** Both episodes' intros are long: at 40
updates a second the first message is at frame **801** in minisode 2 (20 s) and
frame **3160** in the Holiday Special (79 s) — logo, walk, office scene first.
On the PC the message window opens and the text draws (name tab, typewriter,
`pc/shots/hs1_dialogue.png` and `hs2_dialogue.png`); `pc/minisode_check.rb` now
asserts it. For a console run, `ttm_patches.rb` appends a `msg:` line to
`log.txt` for each of the first 20 messages — after "Mouse 3", which is the last
script in all three games to define `Interpreter#command_101` (the class is
RMXP's `Interpreter`, not RMVX's `Game_Interpreter`; an earlier patch would be
replaced by the later definition).

**What the minisodes actually needed** — found by running them on the PC
(`pc/minisode_check.rb`), not by reading them:

| Gap | What it was | Fix |
|---|---|---|
| Ruby 1.8 syntax | `{'a', 'b', 'c', 'd'}` as a hash of pairs (1.8 accepted the omitted arrows), and `Bitmap.new (w, h)` / `blt (x, y, …)` with a space before the argument list. Three scripts each: `UMS new`, `Mouse 3`, `modern algebra para formater` | `TTMPatch.legacy`: a script that fails to compile is rewritten (dotted and bare calls lose the space; an arrow-less hash literal becomes `Hash[...]`) and compiled again, logged as `legacy syntax: …`. Nothing has run at that point, so it can only turn "will not load" into "loads as on the PC" |
| `ENV` | `ENV['AV_APPDATA']` for the save folder (Utils.dll's `AVSetEnv`) | a one-key `ENV` in `compat.rb` answering `"."`, since every path is mapped under one root anyway |
| `Hash#index` | the mouse script asks a Hash for the key of a value (Ruby 1.8; gone in 1.9) | `Hash#index` in `compat.rb` |
| The mouse | their older `Mouse.pos` reads the cursor through `user32` and takes a `nil` from every failed call, so `[gx, gy].pack('ll')` raised on frame 1 | the main game's *own* later build replaced those two lines with `Input.mouse_x`/`mouse_y`; the same two lines are patched in ("Mouse 1"), which keeps the pointer "outside the window" and the D-pad walking |
| The party | `$data_system.party_members` is `[]` in both minisodes, and their `Caterpillar` reads `$game_party.actors[0].id` whenever the player is centred, so the boot `moveto` aborted before a frame was drawn | the main game's later build guards exactly that lookup (its line is commented out); the same guard is patched in. The main game's copy starts with `#`, which the patch's leading newline cannot match |
| A font | neither minisode has a `Fonts/` folder | `boot.rb` falls back to `$ttm_font_file` (`APP_DIR Fonts/OpenSans.ttf`) and logs it |

**Tests.** `pc/minisode_check.rb hs1|hs2`: **10/10 each** — the archive opens,
every script loads, the game reaches a map and draws it (89 / 146 drawables),
screenshots are saved, no bottom-screen menu is made, it stays English, the
shared font stood in, `Mouse.pos` returns a position, every write stays inside
its own data folder, and the two compatibility patches applied.
`pc/shots/hs1_*.png` and `hs2_*.png` show the Freebird splash, the Sigmund Corp.
street and minisode 2's snowy opening. All earlier checks are unchanged, and
`build.ps1` has no new warnings.

**What this leaves.** The minisodes' bottom screen is dark (they have no menu to
put there), and they have no saves by design — `START + SELECT` is the way out.
The main game's title screen still carries unused "T Moonshadow"/"T Light"
pictures; nothing in the engine reads them.

