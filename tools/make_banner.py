#!/usr/bin/env python3
"""
make_banner.py -- build the HOME-menu banner from the game's own art and music.

Five steps, each of which can also be run on its own, plus one patch bannertool
cannot do from a WAV.

    tools/make_banner_assets.py   the backdrop and the wordmark       (Pillow)
    tools/make_banner_glb.py      the turning plaque as a .glb
    tools/pycgfx/main.py          the .glb as the CGFX the menu renders (LZ11'd
                                  into the banner by bannertool)
    tools/make_banner_tone.py     the title theme, crossfaded to a 2.8 s loop
                                  (3 s is the menu's limit); 2 channels
    bannertool makebanner         all of it as work/banner/banner.bnr

The patch: a WAV cannot say "loop", so bannertool writes the CWAV's loop flag as
0 and the tune plays once. The CWAV it writes already has the loop points (0 to
the last frame), so this flips the flag at INFO+0x09 -- one byte -- and the menu
plays the phrase round and round.

`engine/build_cia.ps1` then packs whatever is at `work/banner/banner.bnr` (and
falls back to the flat banner in `3ds-app/cia/` when this has not been run).
Both the art and the tune come from your own copy of the game, so the result is
for this console only, exactly as romfs:/audio.ttma is.

Usage:  python tools/make_banner.py [--seconds 16] [--bannertool PATH]
"""

import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "work", "banner")

GLB = os.path.join(OUT, "banner.glb")
CGFX = os.path.join(OUT, "banner.cgfx")
WAV = os.path.join(OUT, "tone.wav")
BNR = os.path.join(OUT, "banner.bnr")

DEFAULT_BANNERTOOL = os.path.join(
    "C:", os.sep, "Users", "davao", "devkitpro", "tools", "cia",
    "bannertool", "windows-x86_64", "bannertool.exe")


def run(args, what):
    print("[.] %s" % what, flush=True)
    result = subprocess.run(args, cwd=ROOT)
    if result.returncode:
        raise SystemExit("%s failed (exit %d)" % (what, result.returncode))


def build_cgfx():
    run([sys.executable, os.path.join(HERE, "pycgfx", "main.py"), GLB, CGFX],
        "pycgfx: banner.glb -> banner.cgfx")
    if not os.path.exists(CGFX):
        raise SystemExit("pycgfx produced no CGFX (is gltflib installed?)")
    size = os.path.getsize(CGFX)
    print("[+] %-16s %d bytes" % ("banner.cgfx", size))
    if size > 0x80000:
        raise SystemExit("the CGFX is over 512 KB, which the 3DS will not accept")


def loop_cwav():
    """Set the CWAV's loop flag, so the menu repeats the tune seamlessly."""
    with open(BNR, "rb") as fh:
        data = bytearray(fh.read())

    cwav, = struct.unpack_from("<I", data, 0x84)
    if data[cwav:cwav + 4] != b"CWAV":
        raise SystemExit("the banner has no CWAV at +0x%X" % cwav)
    info = cwav + struct.unpack_from("<I", data, cwav + 0x18)[0]
    if data[info:info + 4] != b"INFO":
        raise SystemExit("the CWAV has no INFO block at +0x%X" % info)

    loop_start, loop_end = struct.unpack_from("<II", data, info + 0x10)
    data[info + 0x09] = 1
    with open(BNR, "wb") as fh:
        fh.write(data)
    print("[+] loop flag set: frames %d..%d, so the phrase repeats"
          % (loop_start, loop_end))


def main():
    ap = argparse.ArgumentParser(description="Build the HOME-menu banner.")
    ap.add_argument("--seconds", type=float, default=2.8,
                    help="length of the tune's loop (default 2.8; 3 is the maximum)")
    ap.add_argument("--bannertool", default=DEFAULT_BANNERTOOL,
                    help="path to bannertool.exe")
    args = ap.parse_args()

    # 3dbrew, CBMD: "The included BCWAV total channels must be 2, and the length
    # of the audio must be 3 seconds or less, otherwise the sound will play
    # incorrectly (beeping/clicking) or the model may fail to load."
    if args.seconds > 3.0:
        raise SystemExit("the HOME menu accepts at most 3 s of banner audio "
                         "(asked for %.1f s)" % args.seconds)

    if not os.path.exists(args.bannertool):
        raise SystemExit("missing bannertool: %s" % args.bannertool)
    os.makedirs(OUT, exist_ok=True)

    run([sys.executable, os.path.join(HERE, "make_banner_assets.py")],
        "Pillow: backdrop.png, wordmark.png, preview.png")
    run([sys.executable, os.path.join(HERE, "make_banner_glb.py")],
        "glTF: banner.glb")
    build_cgfx()
    run([sys.executable, os.path.join(HERE, "make_banner_tone.py"),
         "--seconds", "%.3f" % args.seconds],
        "ffmpeg: title.ogg -> tone.wav")

    run([args.bannertool, "makebanner", "-ci", CGFX, "-a", WAV, "-o", BNR],
        "bannertool: banner.bnr")
    if not os.path.exists(BNR):
        raise SystemExit("bannertool produced no banner")
    loop_cwav()

    print("")
    run([sys.executable, os.path.join(HERE, "inspect_banner.py"), BNR],
        "check: what the banner really contains")
    print("")
    print("[i] %s is ready; build_cia.ps1 -AnimatedBanner packs it" % BNR)
    print("[i] (the console's HOME menu would not render it: RUN19-STATE.md)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
