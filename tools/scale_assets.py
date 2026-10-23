#!/usr/bin/env python3
"""
scale_assets.py -- build 3DS-ready assets from the extracted game graphics.

Why this exists (see 3DS-COMPAT-MATRIX.md §5.1):
  * the game's images decode to 589.4 MB of RGBA, against ~64 MB of usable RAM
  * the 3DS GPU cannot hold a texture larger than 1024 px in either dimension
  * the final render target is half-size anyway, so scaling 0.5x offline costs
    nothing visually and cuts decoded memory by 4x

What it does, with no third-party dependencies (pure Python + zlib):
  1. decodes the game's PNGs itself (palette 1/2/4/8-bit, RGB8, RGBA8, grey8,
     non-interlaced -- which is everything this game contains)
  2. box-filters them to 0.5x with premultiplied-alpha averaging (no dark halos)
  3. writes raw RGBA8888 blobs the 3DS app can upload straight into a texture
     (no PNG decoder needed on the device)
  4. optionally slices anything still over 1024 px into <=1024-px pieces
  5. writes preview PNGs so the result can be inspected on the PC
  6. writes manifest.json + assets.h for the C side

Usage
-----
    python scale_assets.py --files "Graphics/Pictures/0moonBG.png" --preview
    python scale_assets.py --all --slice --out work/3ds_assets
"""

import argparse
import json
import os
import struct
import sys
import time
import zlib

PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
CHANNELS = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}


def decode_png(data):
    """Return (width, height, rgba_bytes). Non-interlaced, bit depth 1/2/4/8."""
    if data[:8] != PNG_MAGIC:
        raise ValueError("not a PNG")
    pos = 8
    idat = []
    palette = b""
    trns = b""
    w = h = bitdepth = colortype = interlace = None
    while pos + 8 <= len(data):
        (length,) = struct.unpack(">I", data[pos:pos + 4])
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            w, h, bitdepth, colortype, _comp, _filt, interlace = \
                struct.unpack(">IIBBBBB", chunk)
        elif ctype == b"PLTE":
            palette = chunk
        elif ctype == b"tRNS":
            trns = chunk
        elif ctype == b"IDAT":
            idat.append(chunk)
        elif ctype == b"IEND":
            break
    if interlace:
        raise ValueError("interlaced PNG not supported")
    if bitdepth not in (1, 2, 4, 8) or colortype not in CHANNELS:
        raise ValueError("unsupported PNG (colour type %s, depth %s)"
                         % (colortype, bitdepth))

    raw = zlib.decompress(b"".join(idat))
    channels = CHANNELS[colortype]
    bpp = max(1, channels * bitdepth // 8)
    stride = (w * channels * bitdepth + 7) // 8
    if len(raw) < (stride + 1) * h:
        raise ValueError("truncated PNG data")

    out = bytearray(stride * h)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        ft = raw[p]
        p += 1
        line = bytearray(raw[p:p + stride])
        p += stride
        if ft == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif ft == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif ft == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
        elif ft == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        elif ft != 0:
            raise ValueError("bad PNG filter type %d" % ft)
        out[y * stride:(y + 1) * stride] = line
        prev = line

    return w, h, _to_rgba(out, w, h, stride, bitdepth, colortype, palette, trns)


def _to_rgba(raw, w, h, stride, bitdepth, colortype, palette, trns):
    rgba = bytearray(w * h * 4)
    if colortype == 6 and bitdepth == 8:
        for y in range(h):
            rgba[y * w * 4:(y + 1) * w * 4] = raw[y * stride:y * stride + w * 4]
        return bytes(rgba)
    if colortype == 3:
        n = len(palette) // 3
        pal = [(palette[i * 3], palette[i * 3 + 1], palette[i * 3 + 2],
                trns[i] if i < len(trns) else 255) for i in range(n)]
        mask = (1 << bitdepth) - 1
        for y in range(h):
            o = y * w * 4
            base = y * stride
            for x in range(w):
                if bitdepth == 8:
                    idx = raw[base + x]
                else:
                    bitpos = x * bitdepth
                    byte = raw[base + (bitpos >> 3)]
                    idx = (byte >> (8 - bitdepth - (bitpos & 7))) & mask
                r, g, b, a = pal[idx] if idx < n else (0, 0, 0, 0)
                rgba[o] = r
                rgba[o + 1] = g
                rgba[o + 2] = b
                rgba[o + 3] = a
                o += 4
        return bytes(rgba)
    if colortype == 2 and bitdepth == 8:
        for y in range(h):
            row = raw[y * stride:y * stride + w * 3]
            o = y * w * 4
            for x in range(w):
                rgba[o] = row[x * 3]
                rgba[o + 1] = row[x * 3 + 1]
                rgba[o + 2] = row[x * 3 + 2]
                rgba[o + 3] = 255
                o += 4
        return bytes(rgba)
    if colortype == 0 and bitdepth == 8:
        for y in range(h):
            o = y * w * 4
            base = y * stride
            for x in range(w):
                g = raw[base + x]
                rgba[o] = rgba[o + 1] = rgba[o + 2] = g
                rgba[o + 3] = 255
                o += 4
        return bytes(rgba)
    if colortype == 4 and bitdepth == 8:
        for y in range(h):
            o = y * w * 4
            base = y * stride
            for x in range(w):
                g = raw[base + x * 2]
                rgba[o] = rgba[o + 1] = rgba[o + 2] = g
                rgba[o + 3] = raw[base + x * 2 + 1]
                o += 4
        return bytes(rgba)
    raise ValueError("unsupported PNG conversion (ct=%d bd=%d)" % (colortype, bitdepth))


# ---------------------------------------------------------------------------
# 0.5x box filter with premultiplied alpha (avoids dark fringes on sprites)
# ---------------------------------------------------------------------------

def box_filter(rgba, w, h, factor=2):
    nw = (w + factor - 1) // factor
    nh = (h + factor - 1) // factor
    out = bytearray(nw * nh * 4)
    for y in range(nh):
        y0, y1 = y * factor, min(y * factor + factor, h)
        for x in range(nw):
            x0, x1 = x * factor, min(x * factor + factor, w)
            sr = sg = sb = sa = 0
            n = 0
            for yy in range(y0, y1):
                base = yy * w * 4
                for xx in range(x0, x1):
                    i = base + xx * 4
                    a = rgba[i + 3]
                    sr += rgba[i] * a
                    sg += rgba[i + 1] * a
                    sb += rgba[i + 2] * a
                    sa += a
                    n += 1
            o = (y * nw + x) * 4
            if sa:
                out[o] = sr // sa
                out[o + 1] = sg // sa
                out[o + 2] = sb // sa
            out[o + 3] = sa // n
    return nw, nh, bytes(out)


def slice_vertical(rgba, w, h, max_dim=1024):
    """Split a tall image into pieces of at most max_dim rows."""
    if h <= max_dim:
        return [(0, h, rgba)]
    pieces = []
    y = 0
    while y < h:
        y1 = min(y + max_dim, h)
        pieces.append((y, y1, rgba[y * w * 4:y1 * w * 4]))
        y = y1
    return pieces


# ---------------------------------------------------------------------------
# PNG writing (previews, so the result can be checked on the PC)
# ---------------------------------------------------------------------------

def encode_png(w, h, rgba):
    raw = bytearray()
    stride = w * 4
    for y in range(h):
        raw.append(0)                      # filter type 0
        raw += rgba[y * stride:(y + 1) * stride]

    def chunk(tag, payload):
        return (struct.pack(">I", len(payload)) + tag + payload
                + struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    return (PNG_MAGIC + chunk(b"IHDR", ihdr)
            + chunk(b"IDAT", zlib.compress(bytes(raw), 6)) + chunk(b"IEND", b""))


def sanitize(name):
    return name.replace("\\", "/").replace("/", "__").replace(" ", "_")


def main():
    ap = argparse.ArgumentParser(description="Build 0.5x 3DS assets from game PNGs.")
    ap.add_argument("--raw", default=os.path.join("work", "raw"))
    ap.add_argument("--out", default=os.path.join("work", "3ds_assets"))
    ap.add_argument("--files", help="comma/semicolon separated paths relative to --raw")
    ap.add_argument("--all", action="store_true", help="process every PNG (slow)")
    ap.add_argument("--slice", action="store_true",
                    help="split outputs taller/wider than --max-dim")
    ap.add_argument("--max-dim", type=int, default=1024)
    ap.add_argument("--preview", action="store_true", help="also write preview PNGs")
    ap.add_argument("--no-raw", action="store_true", help="previews only, no .rgba")
    a = ap.parse_args()

    if a.files:
        names = [x.strip().replace("\\", "/")
                 for x in a.files.replace(";", ",").split(",") if x.strip()]
    else:
        found = []
        for root, _dirs, files in os.walk(a.raw):
            for f in files:
                if f.lower().endswith(".png"):
                    found.append(os.path.relpath(os.path.join(root, f), a.raw)
                                 .replace("\\", "/"))
        found.sort()
        if not a.all:
            print("[i] %d PNGs are available; pass --files or --all." % len(found))
            print("[i] e.g. --files \"Graphics/Pictures/0moonBG.png\"")
            return 1
        names = found

    os.makedirs(a.out, exist_ok=True)
    manifest = []
    tot_in = tot_full = tot_out = 0
    t0 = time.time()
    for i, rel in enumerate(names, 1):
        src = os.path.join(a.raw, *rel.split("/"))
        if not os.path.exists(src):
            print("[!] missing: %s" % rel)
            continue
        data = open(src, "rb").read()
        try:
            w, h, rgba = decode_png(data)
        except Exception as exc:  # noqa: BLE001
            print("[!] %-58s FAILED: %s" % (rel, exc))
            continue
        nw, nh, half = box_filter(rgba, w, h, 2)
        base = sanitize(rel)
        pieces = (slice_vertical(half, nw, nh, a.max_dim) if a.slice
                  else [(0, nh, half)])
        written = []
        for idx, (y0, y1, piece) in enumerate(pieces):
            fn = base + ("" if len(pieces) == 1 else ".s%d" % idx) + ".rgba"
            if not a.no_raw:
                with open(os.path.join(a.out, fn), "wb") as fh:
                    fh.write(piece)
            written.append({"file": fn, "y": y0, "w": nw, "h": y1 - y0,
                            "bytes": len(piece)})
        if a.preview:
            with open(os.path.join(a.out, base + ".preview.png"), "wb") as fh:
                fh.write(encode_png(nw, nh, half))
        manifest.append({
            "source": rel, "src_w": w, "src_h": h, "w": nw, "h": nh,
            "src_file_bytes": len(data),
            "decoded_before": w * h * 4, "decoded_after": len(half),
            "pieces": written,
            "over_limit_before": (w > a.max_dim or h > a.max_dim),
            "over_limit_after": any(p["w"] > a.max_dim or p["h"] > a.max_dim
                                    for p in written),
        })
        tot_in += len(data)
        tot_full += w * h * 4
        tot_out += len(half)
        print("[%3d/%3d] %-56s %4dx%-5d -> %4dx%-4d%s"
              % (i, len(names), rel, w, h, nw, nh,
                 ("  (%d pieces)" % len(pieces)) if len(pieces) > 1 else ""))

    with open(os.path.join(a.out, "manifest.json"), "w", encoding="utf-8") as fh:
        json.dump({"scale": 0.5, "max_dim": a.max_dim, "assets": manifest}, fh, indent=1)

    with open(os.path.join(a.out, "assets.h"), "w", encoding="utf-8") as fh:
        fh.write("/* generated by tools/scale_assets.py -- do not edit */\n")
        fh.write("#pragma once\n\n")
        fh.write("typedef struct { const char *name; int w, h; } ttm_asset_t;\n\n")
        fh.write("static const ttm_asset_t TTM_ASSETS[] = {\n")
        for m in manifest:
            fh.write('    { "%s", %d, %d },\n'
                     % (sanitize(m["source"]), m["w"], m["h"]))
        fh.write("};\n\n#define TTM_ASSET_COUNT %d\n" % len(manifest))

    print("\n[+] %d assets in %.1fs" % (len(manifest), time.time() - t0))
    print("[+] source PNG bytes      : %10d" % tot_in)
    print("[+] decoded before (RGBA) : %10.1f MB" % (tot_full / 1048576.0))
    print("[+] decoded after  (RGBA) : %10.1f MB  (%.1f%% of before)"
          % (tot_out / 1048576.0, 100.0 * tot_out / max(1, tot_full)))
    over = [m for m in manifest if m["over_limit_after"]]
    if over:
        print("[!] %d outputs still exceed %d px (rerun with --slice)" % (len(over), a.max_dim))
    print("[+] wrote %s" % a.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
