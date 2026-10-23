#!/usr/bin/env python3
"""
build_assets.py -- pack the test screens into the app's romfs as raw textures.

The 3DS app deliberately has no PNG decoder and no font library: everything it
draws arrives here as a ready-to-upload texture. Format (.ttmtex):

    8 bytes  magic "TTMRGBA1"
    u32      width
    u32      height
    w*h*4    RGBA8888 pixels, row-major, top-down

Note the 3DS GPU needs POWER-OF-TWO texture dimensions, so the app rounds each
image up (320x240 -> 512x256, 400x240 -> 512x256, 640x480 -> 1024x512) and
crops with UVs. That is handled on the C side; here we just write the pixels.

Usage: python build_assets.py
"""

import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))

from make_ui_mockup import (FONT, OUT, render_text_layer,  # noqa: E402
                            load_png)
from scale_assets import encode_png, decode_png            # noqa: E402

ROMFS = os.path.join(HERE, "romfs")
DIGITS_W, DIGITS_H = 16, 24          # 10 glyphs, 16 px cells


def write_ttmtex(path, w, h, rgba):
    with open(path, "wb") as fh:
        fh.write(b"TTMRGBA1")
        fh.write(w.to_bytes(4, "little"))
        fh.write(h.to_bytes(4, "little"))
        fh.write(rgba)


def make_digit_strip():
    """Bake 0-9 into one strip so the app needs no font API."""
    items = [{"text": str(d), "x": 2 + d * DIGITS_W, "y": 2, "size": 18}
             for d in range(10)]
    w, h, rgba = render_text_layer(items, DIGITS_W * 10, DIGITS_H, "digits")
    # force the glyphs to a flat colour so they blend predictably
    out = bytearray(rgba)
    for i in range(0, len(out), 4):
        a = out[i + 3]
        out[i] = out[i + 1] = out[i + 2] = 255
        out[i + 3] = a
    return w, h, bytes(out)


def main():
    os.makedirs(ROMFS, exist_ok=True)

    # make_ui_mockup writes the composed screens as PNGs; rebuild them first
    rc = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "make_ui_mockup.py")],
                        cwd=ROOT)
    if rc.returncode != 0:
        raise SystemExit("make_ui_mockup.py failed")

    screens = [
        ("A_half_320x240.png", "a_option_a.ttmtex"),
        ("C_crop_400x240.png", "c_option_c.ttmtex"),
        ("B_bottom_320x240.png", "b_option_b.ttmtex"),
        ("A_full_640x480.png", "full_frame.ttmtex"),
    ]
    manifest = []
    for src, dst in screens:
        w, h, rgba = load_png(os.path.join(OUT, src))
        write_ttmtex(os.path.join(ROMFS, dst), w, h, rgba)
        manifest.append({"file": dst, "source": src, "w": w, "h": h,
                         "bytes": w * h * 4})
        print("[+] %-22s %4dx%-4d -> %s" % (src, w, h, dst))

    w, h, digits = make_digit_strip()
    write_ttmtex(os.path.join(ROMFS, "digits.ttmtex"), w, h, digits)
    manifest.append({"file": "digits.ttmtex", "source": "baked 0-9",
                     "w": w, "h": h, "bytes": w * h * 4})
    print("[+] %-22s %4dx%-4d -> %s" % ("digits (0-9)", w, h, "digits.ttmtex"))

    with open(os.path.join(ROMFS, "assets.json"), "w", encoding="utf-8") as fh:
        json.dump(manifest, fh, indent=1)
    total = sum(m["bytes"] for m in manifest)
    print("\n[+] %d textures, %.2f MB of pixel data in %s"
          % (len(manifest), total / 1048576.0, ROMFS))
    print("[i] the app rounds each to power-of-two when allocating: "
          "512x256 (0.5 MB), 1024x512 (2 MB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
