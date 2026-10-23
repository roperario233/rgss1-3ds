#!/usr/bin/env python3
"""
make_banner_assets.py -- compose the two textures the animated banner needs.

A HOME-menu banner is a tiny 3D scene, not a picture: a camera-facing backdrop
plate behind a plaque that turns. Both are textures, so this script writes them
at exactly the size the PICA200 wants -- power-of-two and at most 256 px wide,
which is also the point where pycgfx would start resizing them behind our back:

    work/banner/backdrop.png   256x128  the game's own night sky, darkened
    work/banner/wordmark.png   256x128  the plaque face (NINTENDO 3DS + title)
    work/banner/preview.png    256x128  both flattened, to eyeball the art

The wordmark is drawn here rather than cropped from anything: it is a recreation
in the game's own font, not a Nintendo asset. It is still only for this
machine's console -- the CIA carries the game's data either way.

Pillow is the only dependency. It is already required by pycgfx (the glTF ->
CGFX converter), so this adds nothing new to the toolchain. Everything is drawn
at 4x and reduced with LANCZOS, because the banner is only 256x128 on screen and
the edges of the lettering are what sell it.

Usage:  python tools/make_banner_assets.py
"""

import os
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "work", "banner")

MOON_BG = os.path.join(ROOT, "work", "raw", "Graphics", "Pictures", "0moonBG.png")
FONT = os.path.join(ROOT, "work", "game", "Fonts", "OpenSans.ttf")

W, H = 256, 128             # the banner's top-screen size, and both textures
SS = 4                      # supersample factor for the plaque art
PAD = 32                    # room for the outline, in supersampled pixels


# ---------------------------------------------------------------------------
# small drawing helpers
# ---------------------------------------------------------------------------

def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


def vgradient(size, top, bottom):
    """A vertical RGBA gradient, drawn row by row (Pillow has no gradient)."""
    w, h = size
    img = Image.new("RGBA", size, (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    for y in range(h):
        d.line([(0, y), (w, y)], fill=lerp(top, bottom, y / max(1, h - 1)))
    return img


def spaced_text(text, font, spacing, stroke_w=0, stroke_fill=None):
    """Render text with manual letter spacing (Pillow has no tracking).

    Every glyph is placed on the baseline at its own advance, so the result is
    the same size whatever stroke is applied -- that is what lets the outline
    and the fill line up exactly.
    """
    advances = [font.getlength(ch) + spacing for ch in text]
    total = sum(advances) - (spacing if advances else 0)
    asc, desc = font.getmetrics()
    layer = Image.new("RGBA", (int(total) + 2 * PAD, asc + desc + 2 * PAD),
                      (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    x = PAD
    for ch, adv in zip(text, advances):
        d.text((x, PAD + asc), ch, font=font, anchor="ls",
               fill=(255, 255, 255, 255), stroke_width=stroke_w,
               stroke_fill=stroke_fill)
        x += adv
    return layer


def colorized_text(text, font, spacing, top, bottom, outline=None, outline_w=0,
                   weight=0):
    """Lettering filled with a vertical gradient, optionally fattened/outlined.

    The fill is the glyph alpha used as a mask over the gradient, so the letters
    keep their antialiased edges instead of turning into a solid block.

    `weight` strokes the glyphs in their own bottom colour before the gradient
    goes on. Open Sans is the only face the game ships, so this is how the big
    mark gets the heavier weight the 3DS wordmark needs without another font.
    """
    fill_layer = spaced_text(text, font, spacing)
    mask = fill_layer.split()[3]
    out = Image.new("RGBA", fill_layer.size, (0, 0, 0, 0))
    out.paste(vgradient(fill_layer.size, top, bottom), (0, 0), mask)
    if weight:
        fat = spaced_text(text, font, spacing, weight, bottom)
        fat.alpha_composite(out)
        out = fat
    if outline and outline_w:
        base = spaced_text(text, font, spacing, outline_w, outline)
        base.alpha_composite(out)
        out = base
    return out


def rounded_gradient(size, radius, top, bottom):
    grad = vgradient(size, top, bottom)
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, size[0] - 1, size[1] - 1],
                                           radius=radius, fill=255)
    out = Image.new("RGBA", size, (0, 0, 0, 0))
    out.paste(grad, (0, 0), mask)
    return out


def ink(img):
    """Crop a rendered layer to its actual pixels.

    Pillow's text layers carry the font's full ascent/descent as empty space,
    which makes them impossible to stack by eye; cropping to the alpha box turns
    every block into something whose height and position are exact.
    """
    box = img.split()[3].getbbox()
    return img.crop(box) if box else img


def font_for_ink_height(text, target_h):
    """The font size that makes `text` exactly target_h pixels tall."""
    probe = ImageFont.truetype(FONT, 100)
    box = probe.getbbox(text)
    return ImageFont.truetype(FONT, max(1, round(100 * target_h /
                                                (box[3] - box[1]))))


def stack_centred(blocks, width, height, gaps):
    """Paste blocks top to bottom, centred, as one vertically centred column."""
    total = sum(b.height for b in blocks) + sum(gaps)
    y = (height - total) // 2
    out = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    for block, gap in zip(blocks, gaps + [0]):
        out.alpha_composite(block, ((width - block.width) // 2, y))
        y += block.height + gap
    return out



# ---------------------------------------------------------------------------
# the plaque face: NINTENDO 3DS, recreated
# ---------------------------------------------------------------------------

def build_wordmark():
    cw, ch = W * SS, H * SS
    # opaque, not transparent: a HOME-menu material with no alphaMode ignores the
    # texture's alpha channel, so the rounded corners are painted in the plate's
    # own edge colour instead of turning into black notches when it spins
    canvas = Image.new("RGBA", (cw, ch), (9, 15, 30, 255))

    # dark glass plate, inset 2 px of the final size, with a hairline edge
    inset = 2 * SS
    plate = rounded_gradient((cw - 2 * inset, ch - 2 * inset), radius=26,
                             top=(27, 42, 74, 255), bottom=(9, 15, 30, 255))
    canvas.paste(plate, (inset, inset))
    edge = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    ImageDraw.Draw(edge).rounded_rectangle(
        [inset, inset, cw - inset - 1, ch - inset - 1], radius=26,
        outline=(150, 181, 224, 80), width=2)
    canvas.alpha_composite(edge)

    # a soft moon-blue glow in the upper right, so the plate is not flat
    glow = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([cw * 0.58, -ch * 0.55, cw * 1.15, ch * 0.62],
                                 fill=(120, 170, 255, 60))
    canvas.alpha_composite(glow.filter(ImageFilter.GaussianBlur(15 * SS)))

    # The plate is a column of exact blocks: each is cropped to its ink, so the
    # gaps below are real pixels and the whole thing is centred as one unit.
    nin = ink(colorized_text("NINTENDO", font_for_ink_height("NINTENDO", 34),
                             14, (255, 255, 255, 255), (206, 220, 240, 255)))
    rule = Image.new("RGBA", (260, 7), (230, 0, 18, 235))

    # the big mark: outline, red gradient, then a white-to-nothing gradient over
    # the same glyphs as a top bevel (same layer size, so it lines up exactly)
    f3 = font_for_ink_height("3DS", 170)
    big = colorized_text("3DS", f3, 12, (255, 128, 122, 255), (176, 0, 14, 255),
                         outline=(10, 14, 24, 255), outline_w=12, weight=7)
    glyph = spaced_text("3DS", f3, 12)
    bevel = Image.new("RGBA", glyph.size, (0, 0, 0, 0))
    bevel.paste(vgradient(glyph.size, (255, 255, 255, 165),
                          (255, 255, 255, 0)), (0, 0), glyph.split()[3])
    big.alpha_composite(bevel)
    big = ink(big)

    sub = ink(colorized_text("TO THE MOON",
                             font_for_ink_height("TO THE MOON", 28), 12,
                             (226, 236, 250, 255), (150, 172, 205, 255)))

    canvas.alpha_composite(stack_centred([nin, rule, big, sub], cw, ch,
                                         [20, 26, 30]))
    return canvas.resize((W, H), Image.LANCZOS)


# ---------------------------------------------------------------------------
# the backdrop: the game's own sky, cropped around its moon
# ---------------------------------------------------------------------------

def build_backdrop():
    src = Image.open(MOON_BG).convert("RGBA")
    sw, sh = src.size

    # the moon sits near (320, 95) in the 640x480 picture. The plaque covers the
    # middle of the frame, so the crop puts the moon high in the right-hand
    # corner -- the one part of the backdrop that stays visible while it turns.
    crop = (32, 63, 352, 223)
    sx, sy = sw / 640.0, sh / 480.0
    crop = (int(crop[0] * sx), int(crop[1] * sy),
            int(crop[2] * sx), int(crop[3] * sy))
    sky = src.crop(crop).resize((W, H), Image.LANCZOS)

    # darken it and tint the menu's navy, so white lettering reads on top
    img = Image.blend(sky, Image.new("RGBA", sky.size, (8, 14, 30, 255)), 0.55)
    img = img.filter(ImageFilter.GaussianBlur(0.6))

    # the moon is the only bright thing; lift it back, and then some
    luma = sky.convert("L").point(lambda v: 255 if v > 120 else 0)
    img.paste(Image.blend(img, Image.new("RGBA", img.size, (255, 246, 214, 255)),
                          0.70), (0, 0), luma.filter(ImageFilter.GaussianBlur(2)))

    # vignette: dark at the edges, so the turning plaque is the subject
    vig = Image.new("L", img.size, 0)
    ImageDraw.Draw(vig).ellipse([-W * 0.25, -H * 0.55, W * 1.25, H * 1.55],
                                fill=255)
    vig = vig.filter(ImageFilter.GaussianBlur(18))
    dark = Image.new("RGBA", img.size, (0, 0, 0, 255))
    return Image.composite(img, Image.blend(img, dark, 0.55), vig)


def build_preview(backdrop, wordmark):
    """Flat approximation of the scene: enough to judge the art on the PC.

    The plaque is 32 x 12.8 world units in a 40 x 24 unit view, so on the 256 x
    128 banner it covers 80% of the width and half the height -- that is the
    proportion used here, not the texture's own 2:1.
    """
    out = backdrop.copy()
    pw, ph = 205, 82
    x0, y0 = (W - pw) // 2, (H - ph) // 2
    plate = wordmark.resize((pw, ph), Image.LANCZOS)
    shadow = Image.new("RGBA", out.size, (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle(
        [x0 + 3, y0 + 4, x0 + pw + 3, y0 + ph + 4], radius=10, fill=(0, 0, 0, 150))
    out.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(3)))
    out.alpha_composite(plate, (x0, y0))
    return out


def save(name, img):
    path = os.path.join(OUT, name)
    img.save(path)
    print("[+] %-16s %dx%d  %d bytes" % (name, img.width, img.height,
                                        os.path.getsize(path)))


def main():
    for path in (MOON_BG, FONT):
        if not os.path.exists(path):
            raise SystemExit("missing %s (the game's files live in work/)" % path)
    os.makedirs(OUT, exist_ok=True)

    backdrop = build_backdrop()
    wordmark = build_wordmark()
    save("backdrop.png", backdrop)
    save("wordmark.png", wordmark)
    save("preview.png", build_preview(backdrop, wordmark))
    print("[i] both are 256x128 power-of-two, under pycgfx's 256 px limit")
    return 0


if __name__ == "__main__":
    sys.exit(main())

