#!/usr/bin/env python3
"""
make_cia_assets.py -- build the HOME-menu icon and the banner for the CIA.

The HOME menu needs an icon (48x48) and a banner (256x128, shown on the top
screen when the title is selected). Both are composed from the game's own art --
the moon scene that To the Moon opens on -- plus its real window skin and font,
so the installed title looks like a real release rather than a placeholder.

Outputs (into this folder):
    icon.png      48x48
    banner.png    256x128
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))

from make_ui_mockup import blit, draw_window, load_png, render_text_layer  # noqa: E402
from scale_assets import box_filter, encode_png                            # noqa: E402

RAW = os.path.join(ROOT, "work", "raw")


def crop_region(src, sw, sh, x, y, w, h):
    """Copy a sub-rectangle out of an RGBA buffer (clamped)."""
    out = bytearray(w * h * 4)
    for row in range(h):
        sy = y + row
        if sy < 0 or sy >= sh:
            continue
        x0 = max(0, x)
        x1 = min(sw, x + w)
        if x1 <= x0:
            continue
        s0 = (sy * sw + x0) * 4
        d0 = (row * w + (x0 - x)) * 4
        out[d0:d0 + (x1 - x0) * 4] = src[s0:s0 + (x1 - x0) * 4]
    return out


def write_png(name, w, h, rgba):
    path = os.path.join(HERE, name)
    with open(path, "wb") as fh:
        fh.write(encode_png(w, h, rgba))
    print("[+] %-12s %dx%d  %d bytes" % (name, w, h, os.path.getsize(path)))


def write_silent_wav(name, seconds=1.0, rate=32768, channels=2, bits=16):
    """bannertool's makebanner wants a tune as well as an image.

    A silent WAV keeps the build free of the game's copyrighted music; swap in a
    track from your own copy (Audio/BGM/...ogg) if you want a personal build with
    sound, but do not distribute that.
    """
    import struct
    frames = int(rate * seconds)
    data = b"\x00" * (frames * channels * bits // 8)
    hdr = (b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
           + b"fmt " + struct.pack("<IHHIIHH", 16, 1, channels, rate,
                                   rate * channels * bits // 8,
                                   channels * bits // 8, bits)
           + b"data" + struct.pack("<I", len(data)))
    path = os.path.join(HERE, name)
    with open(path, "wb") as fh:
        fh.write(hdr + data)
    print("[+] %-12s %d bytes (silent %.1fs %d Hz)" % (name, os.path.getsize(path),
                                                       seconds, rate))


def main():
    sw, sh, scene = load_png(os.path.join(RAW, "Graphics", "Pictures", "0moonBG.png"))
    skin_w, skin_h, skin = load_png(
        os.path.join(RAW, "Graphics", "Windowskins", "windowskin.png"))
    print("[i] scene %dx%d, windowskin %dx%d" % (sw, sh, skin_w, skin_h))

    # ---- icon: the moon, cropped 96x96 then box-filtered to 48x48 ----------
    cx, cy = 300, 94                       # the moon in 0moonBG.png
    crop = crop_region(scene, sw, sh, cx - 48, cy - 48, 96, 96)
    iw, ih, icon = box_filter(crop, 96, 96, 2)
    write_png("icon.png", iw, ih, icon)

    # ---- banner: a 256x128 crop of the sky, plus the game's dialogue box ----
    bw, bh = 256, 128
    banner = crop_region(scene, sw, sh, 172, 0, bw, bh)
    draw_window(banner, bw, bh, skin, skin_w, skin_h, 8, 68, 240, 54)
    items = [
        {"text": "To the Moon", "x": 20, "y": 74, "size": 16},
        {"text": "3DS port - M1 hardware test", "x": 21, "y": 96, "size": 10},
    ]
    tw, th, text = render_text_layer(items, bw, bh, "cia_banner")
    blit(banner, bw, bh, text, tw, th, 0, 0)
    write_png("banner.png", bw, bh, banner)

    print("[i] feed these to: bannertool makebanner -i banner.png -o banner.bnr")
    print("[i] and the SMDH from the build (smdhtool) is the -icon argument")
    write_silent_wav("banner.wav")
    return 0


if __name__ == "__main__":
    sys.exit(main())
