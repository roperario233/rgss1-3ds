---
name: Port report
about: A game that boots, plays, or breaks on this engine
title: "[port] "
labels: port
---

**Game and version**
(e.g. *To the Moon*, Steam 2023-07-03; or your own project)

**Engine / RGSS version**
RGSS1 (RPG Maker XP) / RGSS2 (VX) / RGSS3 (VX Ace) — only RGSS1 is supported.

**Console**
New 3DS / New 2DS XL, Luma version, and the build you installed (CIA or 3dsx, with
the first line of `log.txt`, which names the run).

**What happens**
Where it stops: title screen, first map, a particular scene. Does it boot on the PC
harness (`engine/pc/run_pc.rb`)? A game that fails there is an engine gap; one that
only fails on hardware is usually memory or timing.

**The log**
Paste the last ~20 lines of `SD:/3ds/ttm3ds/log.txt`, including any `ERROR …` line
and its backtrace, or say that the file ends without one (a freeze rather than an
exception).

**What you tried**
Stubs added, patches, `pack_audio` output, fonts, etc.

**Anything visual**
A photo helps if the screen is wrong; please don't attach game art to the issue.
