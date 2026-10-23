#!/usr/bin/env python3
"""
make_engine_brand.py -- the engine-only release's own icon and banner.

Everything here is drawn from nothing: no game art, no game music, no font taken
from the game. The palette is the launcher's (navy #12141F, gold #E8C87A), so the
installed title looks like the app it opens. That is what makes the engine-only CIA
publishable while the game-carrying CIA is not.

Writes into work/brand/ (generated, therefore gitignored -- not source):

  icon.png     48x48    HOME-menu icon          -> Makefile APP_ICON / SMDH
  banner.png  256x128   top-screen banner       -> bannertool makebanner -i
  banner.wav   1 s      silence, 32 kHz stereo  -> bannertool makebanner -a

All art is drawn at 4x and box-filtered down, which is why the crescent has a clean
edge at 48 px.

Usage:  python tools/make_engine_brand.py [--out DIR]
"""

import argparse
import os
import sys
import wave

from PIL import Image, ImageDraw, ImageFont

NAVY = (18, 20, 34)          # the launcher's background
NAVY_LIGHT = (30, 34, 54)    # its lighter panel
GOLD = (232, 200, 122)       # its accent
GREY = (150, 160, 190)       # its secondary text
SS = 4                       # supersample factor


def _font(size):
    """A bold sans if Windows has one, else Pillow's built-in scalable face."""
    for name in ("arialbd.ttf", "segoeuib.ttf", "arial.ttf", "segoeui.ttf"):
        path = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts", name)
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size)
            except OSError:
                pass
    try:
        return ImageFont.load_default(size=size)   # Pillow >= 10.1
    except TypeError:
        return ImageFont.load_default()


def _crescent(draw, cx, cy, r, colour, cut_dx, cut_dy, cut_r):
    """A moon: a disc with a second disc punched out of it, offset up and right."""
    draw.ellipse([cx - r, cy - r, cx + r, cy + r], fill=colour)
    draw.ellipse([cx + cut_dx - cut_r, cy + cut_dy - cut_r,
                  cx + cut_dx + cut_r, cy + cut_dy + cut_r], fill=NAVY)


def icon(path, size=48):
    w = size * SS
    im = Image.new("RGBA", (w, w), NAVY + (255,))
    d = ImageDraw.Draw(im)
    # a rounded square, so the tile reads as an app and not as a square splash
    d.rounded_rectangle([0, 0, w - 1, w - 1], radius=int(w * 0.18), fill=NAVY_LIGHT)
    _crescent(d, w * 0.50, w * 0.50, w * 0.34, GOLD, w * 0.13, -w * 0.08, w * 0.28)
    im = im.resize((size, size), Image.LANCZOS)
    im.save(path)
    return path


def banner(path, w=256, h=128):
    W, H = w * SS, h * SS
    im = Image.new("RGBA", (W, H), NAVY + (255,))
    d = ImageDraw.Draw(im)
    # the launcher's panel edge: a hairline of gold along the bottom
    d.rectangle([0, H - int(H * 0.020), W, H], fill=GOLD)

    _crescent(d, W * 0.17, H * 0.50, H * 0.30, GOLD, H * 0.11, -H * 0.07, H * 0.25)

    title = _font(int(H * 0.235))
    sub = _font(int(H * 0.098))
    d.text((W * 0.33, H * 0.335), "RGSS1 3DS", font=title, fill=GOLD)
    d.text((W * 0.335, H * 0.585), "RPG Maker XP runtime", font=sub, fill=GREY)

    im = im.resize((w, h), Image.LANCZOS)
    im.save(path)
    return path


def silence(path, seconds=1, rate=32768, channels=2):
    """The 1 s of silence bannertool insists on: a banner may have no tune."""
    frames = rate * seconds
    with wave.open(path, "wb") as f:
        f.setnchannels(channels)
        f.setsampwidth(2)
        f.setframerate(rate)
        f.writeframes(b"\x00\x00" * channels * frames)
    return path


def main():
    ap = argparse.ArgumentParser(description="Draw the engine-only CIA's icon and banner.")
    ap.add_argument("--out", default=os.path.join("work", "brand"))
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    made = [
        icon(os.path.join(args.out, "icon.png")),
        banner(os.path.join(args.out, "banner.png")),
        silence(os.path.join(args.out, "banner.wav")),
    ]
    for p in made:
        print("%-28s %8d B" % (p, os.path.getsize(p)))
    print("\nnothing from the game is in these: run build_cia.ps1 -EngineOnly")
    return 0


if __name__ == "__main__":
    sys.exit(main())
