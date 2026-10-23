# Third-party components

This repository contains this project's own code. It **does not** contain the game
or RPG Maker. What it builds on, and where each piece comes from:

| Component | License | In this repo? | Where |
|---|---|---|---|
| **mruby** | MIT (© mruby developers) | no — built from a local `mruby-4.0.0` tree | `engine/build_mruby.ps1`, `engine/mruby_config.rb` |
| **mruby-onig-regexp** | MIT (© mattn) | no — fetched at a pinned commit | `build_mruby.ps1`; our patch layer is `engine/gems/mruby-onig-regexp-3ds/` |
| **Onigmo** 6.2.0 | BSD 2-clause (see Onigmo's `COPYING`) | no — fetched from the tarball in the pinned mruby-onig-regexp commit | `build_mruby.ps1` |
| **stb** (`stb_image`, `stb_truetype`, `stb_vorbis`, …) | public domain / MIT | **yes** | `engine/gems/*/third_party/stb/` (their `LICENSE` is beside them) |
| **devkitPro / libctru / citro2d / citro3d** | zlib | no — the SDK | toolchain |
| **makerom**, **bannertool** | their own (see upstream) | no | CIA packaging |
| **pycgfx** (glTF → CGFX for banners) | **no license published** | **no — never redistributed** | optional; `tools/make_banner.py` needs it for animated banners |
| **To the Moon**, its data, art, music, fonts, text | © Freebird Games | **no, ever** | you supply your own copy |
| **RPG Maker XP / RGSS** | © Gotcha Gotcha Games / Enterbrain | no | — |

## Notes

- The `stb` headers are the only third-party **source** vendored here; their license
  text is included next to them.
- Onigmo is compiled from source fetched at build time, so this repository does not
  redistribute it, but any binary build of this engine links it — keep Onigmo's
  `COPYING` with such a build.
- `tools/pycgfx/` is in `.gitignore` for a reason: it publishes no license, so it is
  used locally and is not part of this project's source or releases.
- Nothing in this repository grants any rights to To the Moon or RPG Maker XP.
