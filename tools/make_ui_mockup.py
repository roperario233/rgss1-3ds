#!/usr/bin/env python3
"""
make_ui_mockup.py -- answer the "is the dialogue readable on a 3DS?" question
before writing any engine code.

It builds the game's real message window at its real geometry (Window_Message is
`super(80, 304, 480, 160)`, 22 px font, 32 px line height), over real game art,
with real dialogue lines pulled from the map data, and renders every candidate
3DS layout so they can be compared side by side on a PC:

    A_full_640x480.png       what the PC version shows (reference)
    A_half_320x240.png       option A content: 0.5x box filter (2:1, crisp)
    A_3ds_top_400x240.png    option A as the 3DS top screen actually shows it
                             (320x240 centred, 40 px pillarbox each side)
    C_crop_400x240.png       option C: 0.625x scaled, bottom 240 rows kept
    B_bottom_320x240.png     option B: half-scale scene on the bottom screen
                             with the dialogue redrawn at native text size

Text is rasterised on the PC with the game's own OpenSans.ttf via
render_text.ps1 (System.Drawing), because Python cannot load a TTF unaided.
"""

import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from rxdata_dump import RubyObject, load            # noqa: E402
from scale_assets import box_filter, decode_png, encode_png  # noqa: E402

RAW = os.path.join("work", "raw")
OUT = os.path.join("work", "preview")
FONT = os.path.join("work", "fonts", "OpenSans.ttf")

# The game's own message window geometry, from work/scripts/055_Window_Message.rb
WIN_X, WIN_Y, WIN_W, WIN_H = 80, 304, 480, 160
INSET = 16                      # Window_Base contents inset
FONT_SIZE = 22                  # RGSS default
LINE_H = 32


# ---------------------------------------------------------------------------
# drawing helpers
# ---------------------------------------------------------------------------

def new_canvas(w, h, colour=(0, 0, 0, 255)):
    c = bytearray(w * h * 4)
    for i in range(0, len(c), 4):
        c[i:i + 4] = bytes(colour)
    return c


def blit(dst, dw, dh, src, sw, sh, dx, dy, sx=0, sy=0, w=None, h=None):
    w = sw if w is None else w
    h = sh if h is None else h
    for y in range(h):
        ty = dy + y
        if ty < 0 or ty >= dh:
            continue
        srow = (sy + y) * sw * 4
        drow = ty * dw * 4
        for x in range(w):
            tx = dx + x
            if tx < 0 or tx >= dw:
                continue
            si = srow + (sx + x) * 4
            a = src[si + 3]
            if a == 0:
                continue
            di = drow + tx * 4
            if a == 255:
                dst[di:di + 4] = src[si:si + 4]
            else:
                ia = 255 - a
                dst[di] = (src[si] * a + dst[di] * ia) // 255
                dst[di + 1] = (src[si + 1] * a + dst[di + 1] * ia) // 255
                dst[di + 2] = (src[si + 2] * a + dst[di + 2] * ia) // 255
                dst[di + 3] = min(255, a + dst[di + 3] * ia // 255)


def stretch_nearest(src, sw, sh, sx, sy, w, h, tw, th):
    out = bytearray(tw * th * 4)
    for y in range(th):
        syy = sy + min(h - 1, y * h // th)
        srow = syy * sw * 4
        drow = y * tw * 4
        for x in range(tw):
            sxx = sx + min(w - 1, x * w // tw)
            si = srow + sxx * 4
            out[drow + x * 4:drow + x * 4 + 4] = src[si:si + 4]
    return out


def draw_window(dst, dw, dh, skin, sw, sh, bx, by, bw, bh, c=16):
    """RMXP-style 9-slice: 16 px corners, tiled edges, stretched centre."""
    for cx, cy, ox, oy in ((0, 0, 0, 0), (sw - c, 0, bw - c, 0),
                           (0, sh - c, 0, bh - c), (sw - c, sh - c, bw - c, bh - c)):
        blit(dst, dw, dh, skin, sw, sh, bx + ox, by + oy, cx, cy, c, c)
    for x in range(c, bw - c, c):
        wd = min(c, bw - c - x)
        blit(dst, dw, dh, skin, sw, sh, bx + x, by, c, 0, wd, c)
        blit(dst, dw, dh, skin, sw, sh, bx + x, by + bh - c, c, sh - c, wd, c)
    for y in range(c, bh - c, c):
        ht = min(c, bh - c - y)
        blit(dst, dw, dh, skin, sw, sh, bx, by + y, 0, c, c, ht)
        blit(dst, dw, dh, skin, sw, sh, bx + bw - c, by + y, sw - c, c, c, ht)
    iw, ih = bw - 2 * c, bh - 2 * c
    if iw > 0 and ih > 0:
        centre = stretch_nearest(skin, sw, sh, c, c, sw - 2 * c, sh - 2 * c, iw, ih)
        blit(dst, dw, dh, centre, iw, ih, bx + c, by + c, 0, 0, iw, ih)


# ---------------------------------------------------------------------------
# real dialogue out of the map data (UMS markup stripped for display)
# ---------------------------------------------------------------------------

import re  # noqa: E402

UMS_PATTERNS = [
    (re.compile(r"\\nm\[\s*([^\]]*?)\s*\]"), r"\1"),
    (re.compile(r"\\e\[\d+\]"), ""),
    (re.compile(r"\\ignr"), ""),
    (re.compile(r"\\\."), ""),
    (re.compile(r"\\\\"), r"\\"),
    (re.compile(r"\\[a-z]\b"), ""),
]


def strip_ums(text):
    for rx, rep in UMS_PATTERNS:
        text = rx.sub(rep, text)
    return text.strip()


def collect_dialogue(map_path, want_lines=3):
    """Return (speaker_name, [text lines]) from the first real conversation."""
    m = load(map_path)
    name, lines = None, []
    for ev in (m.ivars.get("@events") or {}).values():
        if not isinstance(ev, RubyObject):
            continue
        for pg in (ev.ivars.get("@pages") or []):
            if not isinstance(pg, RubyObject):
                continue
            for cmd in (pg.ivars.get("@list") or []):
                if not isinstance(cmd, RubyObject) or cmd.ivars.get("@code") != 101:
                    continue
                params = cmd.ivars.get("@parameters") or []
                if not params:
                    continue
                raw = str(params[0])
                m2 = re.search(r"\\nm\[\s*([^\]]*?)\s*\]", raw)
                if m2 and name is None:
                    name = m2.group(1)
                    continue
                text = strip_ums(raw)
                if len(re.sub(r"[^A-Za-z]", "", text)) >= 15:
                    lines.append(text)
                if len(lines) >= want_lines:
                    return name, lines
    return name, lines


def render_text_layer(items, w, h, tag):
    """Rasterise text items with the game's font via System.Drawing."""
    os.makedirs(OUT, exist_ok=True)
    spec = os.path.join(OUT, "spec_%s.json" % tag)
    png = os.path.join(OUT, "text_%s.png" % tag)
    with open(spec, "w", encoding="utf-8") as fh:
        json.dump({"w": w, "h": h, "font": os.path.abspath(FONT), "items": items},
                  fh, ensure_ascii=False)
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
           "-File", os.path.join(HERE, "render_text.ps1"),
           "-SpecPath", os.path.abspath(spec), "-OutPath", os.path.abspath(png)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode != 0 or not os.path.exists(png):
        raise SystemExit("text rendering failed:\n%s\n%s" % (res.stdout, res.stderr))
    with open(png, "rb") as fh:
        return decode_png(fh.read())


def name_items(name, x, y, spacing=40):
    """UMS draws the speaker name character by character with wide spacing."""
    return [{"text": ch, "x": x + i * spacing, "y": y, "size": FONT_SIZE}
            for i, ch in enumerate(name)]


def load_png(path):
    with open(path, "rb") as fh:
        return decode_png(fh.read())


def main():
    os.makedirs(OUT, exist_ok=True)
    name, lines = collect_dialogue(os.path.join(RAW, "Data", "Map001.rxdata"))
    if not lines:
        name, lines = collect_dialogue(os.path.join(RAW, "Data", "Map002.rxdata"))
    if not lines:
        raise SystemExit("no dialogue found in the map data")
    lines = lines[:3]
    print("[i] speaker: %r" % name)
    for i, ln in enumerate(lines):
        print("[i] line %d : %r" % (i + 1, ln))

    scene_w, scene_h, scene = load_png(os.path.join(RAW, "Graphics", "Pictures", "0moonBG.png"))
    skin_w, skin_h, skin = load_png(os.path.join(RAW, "Graphics", "Windowskins", "windowskin.png"))
    print("[i] scene %dx%d, windowskin %dx%d" % (scene_w, scene_h, skin_w, skin_h))

    # ---- the game's real 640x480 frame -----------------------------------
    tx = WIN_X + INSET + 4
    ty = WIN_Y + INSET
    items = name_items(name or "Lily", tx, ty)
    for i, ln in enumerate(lines):
        items.append({"text": ln, "x": tx, "y": ty + LINE_H * (i + 1), "size": FONT_SIZE})
    tw, th, text_rgba = render_text_layer(items, 640, 480, "full")

    frame = new_canvas(640, 480)
    blit(frame, 640, 480, scene, scene_w, scene_h, 0, 0, 0, 0, 640, 480)
    draw_window(frame, 640, 480, skin, skin_w, skin_h, WIN_X, WIN_Y, WIN_W, WIN_H)
    blit(frame, 640, 480, text_rgba, tw, th, 0, 0)

    with open(os.path.join(OUT, "A_full_640x480.png"), "wb") as fh:
        fh.write(encode_png(640, 480, frame))

    # ---- option A: half scale (integer 2:1, crisp) ------------------------
    hw, hh, half = box_filter(frame, 640, 480, 2)
    with open(os.path.join(OUT, "A_half_320x240.png"), "wb") as fh:
        fh.write(encode_png(hw, hh, half))

    # as the 3DS top screen shows it: 320x240 centred, 40 px pillarbox
    top = new_canvas(400, 240)
    blit(top, 400, 240, half, hw, hh, 40, 0)
    with open(os.path.join(OUT, "A_3ds_top_400x240.png"), "wb") as fh:
        fh.write(encode_png(400, 240, top))

    # 2x nearest zoom, so the 11 px text is comfortable to inspect on a PC
    zoom = stretch_nearest(half, hw, hh, 0, 0, hw, hh, hw * 2, hh * 2)
    with open(os.path.join(OUT, "A_half_320x240_zoom2x.png"), "wb") as fh:
        fh.write(encode_png(hw * 2, hh * 2, zoom))

    # ---- option C: 0.625 scale (5:8), bottom 240 rows kept ---------------
    cw, ch = 400, 300
    scaled = stretch_nearest(frame, 640, 480, 0, 0, 640, 480, cw, ch)
    crop = scaled[60 * cw * 4:]                     # rows 60..299
    with open(os.path.join(OUT, "C_crop_400x240.png"), "wb") as fh:
        fh.write(encode_png(cw, 240, crop))

    # ---- option B: half-scale scene, dialogue native on the bottom screen -
    b_items = name_items(name or "Lily", 44, 144, spacing=32)
    b_items.append({"text": lines[0], "x": 44, "y": 176, "size": FONT_SIZE})
    if len(lines) > 1:
        b_items.append({"text": lines[1], "x": 44, "y": 208, "size": FONT_SIZE})
    bw, bh, b_text = render_text_layer(b_items, 320, 240, "bottom")
    bottom = new_canvas(320, 240)
    blit(bottom, 320, 240, half, hw, hh, 0, 0)
    draw_window(bottom, 320, 240, skin, skin_w, skin_h, 24, 128, 272, 104)
    blit(bottom, 320, 240, b_text, bw, bh, 0, 0)
    with open(os.path.join(OUT, "B_bottom_320x240.png"), "wb") as fh:
        fh.write(encode_png(320, 240, bottom))

    print("\n[+] wrote 6 images to %s:" % OUT)
    for f in sorted(os.listdir(OUT)):
        if f.endswith(".png") and not f.startswith("text_"):
            print("      %s" % f)
    print("""
[i] How to judge them (a New 3DS XL's pixels are ~0.31 mm; a 1080p desktop
    monitor's are ~0.28 mm, so viewing these at 1:1 is a fair simulation):
      A_full_640x480.png      reference: what the PC version looks like
      A_half_320x240.png      option A content, view at 1:1
      A_3ds_top_400x240.png   option A exactly as the top screen shows it
      A_half_320x240_zoom2x   the same, 2x zoom, for comfort
      C_crop_400x240.png      option C: 0.625x, cropped (bigger text, less view)
      B_bottom_320x240.png    option B: dialogue native on the bottom screen""")
    return 0


if __name__ == "__main__":
    sys.exit(main())
