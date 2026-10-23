#!/usr/bin/env python3
"""
make_qr.py -- draw the QR code FBI scans to install the engine CIA.

FBI's *Remote Install > Scan QR* wants a plain URL in the code: it downloads that
file and installs it, so the code should point at a release asset that will not move.
The default is this repository's v0.20 asset.

Needs `qrcode` (pip install "qrcode[pil]"); Pillow is already a dependency of the
other tools here.

Usage:
  python tools/make_qr.py                                  # the v0.20 release asset
  python tools/make_qr.py <url> [-o docs/qr-install.png]
"""

import argparse
import os
import sys

DEFAULT_URL = ("https://github.com/roperario233/rgss1-3ds/releases/download/"
               "v0.20/ttm3ds-engine.cia")


def main():
    ap = argparse.ArgumentParser(description="Draw the install QR code.")
    ap.add_argument("url", nargs="?", default=DEFAULT_URL)
    ap.add_argument("-o", "--out", default=os.path.join("docs", "qr-install.png"))
    ap.add_argument("--box", type=int, default=8, help="pixels per module")
    args = ap.parse_args()

    try:
        import qrcode
    except ImportError:
        print('missing: qrcode -- pip install "qrcode[pil]"')
        return 1

    # error correction M survives a photographed screen; the border is the quiet
    # zone scanners need, and the box size keeps it readable on a phone display.
    qr = qrcode.QRCode(error_correction=qrcode.constants.ERROR_CORRECT_M,
                       box_size=args.box, border=4)
    qr.add_data(args.url)
    qr.make(fit=True)
    img = qr.make_image(fill_color="black", back_color="white")

    os.makedirs(os.path.dirname(args.out) or ".", exist_ok=True)
    img.save(args.out)
    print("%-28s %dx%d px" % (args.out, img.size[0], img.size[1]))
    print("encodes: %s" % args.url)
    print("\nFBI > Remote Install > Scan QR, then point the camera at %s" % args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
