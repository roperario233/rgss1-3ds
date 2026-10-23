# Run 15 state (2026-10-23)

Built and verified on the PC; the console checklist is below.

## What is in run 15

- **Languages.** The engine reads the game's `lang.dat` (a 17 MB ZIP of
  translations, 11 languages) as an extra archive, like mkxp does:
  `gems/mruby-ttm-core/src/langzip.c` parses the central directory once and
  inflates single entries on demand. The game's own `TR` / `TTMTranslator` code
  then does the rest, unchanged: `LANGUAGES` is `en` + 11, picks swap
  `$data_items` and the common events, maps are patched as they load, and the
  pictures with text come from `lang/<code>/Graphics/...`.
- **The language screen.** With `lang.dat` beside the archive and no
  `save/lang`, the first run opens on the game's own `Scene_Language`; the choice
  is written to `SD:/3ds/ttm3ds/save/lang` and later runs go straight into it.
  Delete that file to see the screen again. Without `lang.dat` the game is
  English exactly as before.
- **Fonts by name.** `font.c` keeps a face per Font name (each with its own glyph
  cache). Korean and Chinese load `Fonts/NanumBarunGothic.ttf` and
  `Fonts/WenQuanYi_Zen_Hei.ttf` the first time a Font asks for them, so the
  language screen can draw 한국어 and 中文 (简体) in their own fonts. A name with
  no file of its own (Arial, Times New Roman — the Latin languages) draws in Open
  Sans, the default face. `log.txt` has `font: <name>, N KB, in N ms` for each
  face loaded.
- **Boot log.** `lang: 797 files in …/lang.dat, opened in N ms`, or
  `lang: no lang.dat -- English only`, or `lang: … is not usable (…) -- English
  only` (a bad pack must not end the game).
- **The console's "copy these files" message** now lists `lang.dat` and the two
  CJK fonts as the optional language files.

## Verified on the PC

- `pc/lang_check.rb`: **47/47** — the ZIP (797 files, 54 folders; a deflated
  entry byte-identical to `tools/extract_lang.py`'s copy, a stored PNG, a folder,
  a missing entry, case-blind paths), the German menu words and items, map 2's
  195 text blocks rewritten to 465 German lines, a localised picture that is
  640x480 where the game's is 542x473 (through the translator's own `Bitmap`
  path), `save/lang` written and read back, both CJK faces and the pixels their
  glyphs cover, and the no-pack fallback (setting reads `en`, screen skipped).
- `pc/lang_screen_check.rb`: **13/13** — the game opens on `Scene_Language`, 12
  rows each in its own font, choosing German writes the setting, and the game
  carries on into the German intro. Shots: `pc/shots/lang_screen.png`,
  `lang_after_choice.png`.
- Earlier checks unchanged: `bottom_menu_check` 18/18, `tone_check` 19/19,
  `effects_check` 16/16, `boot_check` 16/16, `controls_check` 16/16,
  `fileio_check` 19/19.
- `build_mruby.ps1`, `build.ps1` (no warnings) and `build_cia.ps1` clean;
  `tools/inspect_cia.py`: 124 MB, 804 MHz, L2, "To the Moon 3DS".

## To check on the console

1. Copy `lang.dat` (and, for Korean/Chinese, the two `Fonts/*.ttf`) into
   `SD:/3ds/ttm3ds/`, then start the game with no `save/lang` there: the
   language screen should appear, all 12 rows readable — 한국어 and 中文 (简体)
   included (that is the CJK font test).
2. `log.txt`: `lang: 797 files …` and, after the screen draws, two `font:` lines
   with their sizes; no error.
3. Pick **Deutsch**: the game continues into the intro, and the menus, messages
   and item names are German (`save/lang` now exists and says `de`).
4. Restart: it goes straight in, no language screen.
5. Pick **한국어** (delete `save/lang` first): the whole game's text should be
   Korean, with no missing-glyph boxes — the face is 4 MB and should stay loaded.
6. Watch `perf:` `dr` against run 14: the pack adds nothing per frame; the only
   new cost is the CJK faces' RAM (13 MB with both loaded) while a translated
   language is in use.
7. In a translated run, a map change still logs `map load: … in N ms`, and the
   localised tileset (256x11201) decodes and slices as the English one does.

## Next

1. The Holiday Specials.
2. Layered 3D (text and menus in front of the map).
