#!/usr/bin/env python3
"""
inspect_banner.py -- read a HOME-menu banner straight out of a .bnr or a .cia.

`bannertool makebanner` writes a CBMD: a small header, one LZ11-compressed CGFX
per language (the 3D scene the HOME menu renders) and, optionally, a CWAV (the
tune it plays while the tile is selected). Nothing in the file says whether the
scene actually moves -- the animation lives inside the CGFX -- so this walks the
whole chain and reports what is really there:

    banner: 383,384 bytes
      CGFX 0: 149,992 bytes uncompressed (16,056 on disk, LZ11)
              animated (CANM): the scene moves
      tune  : 367,192 bytes, 32,768 Hz, 2 channels, PCM16, 2.80 s
              loops 0..91,750 frames (CWAV header 0x40 bytes)

That is the check the console cannot give without three minutes of watching the
HOME menu: a static banner has no CANM section, and a silent one has no CWAV.

Usage:  python tools/inspect_banner.py work\\banner\\banner.bnr
        python tools/inspect_banner.py engine\\ttm3ds.cia

For a .cia the banner is the ExeFS `banner` entry, so it is found by its magic.
The CBMD layout is bannertool's (source/3ds/cbmd.cpp): "CBMD", a zero, the 14
CGFX offsets, 0x44 bytes of padding, then the CWAV offset.
"""

import struct
import sys

CBMD_NUM_CGFXS = 14
CBMD_HEADER = 0x88          # 4 + 4 + 14*4 + 0x44 + 4
CGFX_OFFSETS = 0x08
CWAV_OFFSET = 0x84

ENCODINGS = {0: "PCM8", 1: "PCM16", 2: "DSP ADPCM", 3: "IMA ADPCM"}


def lz11_decompress(src):
    """Decompress a 3DS LZ11 block (the format bannertool wraps the CGFX in)."""
    if not src or src[0] != 0x11:
        raise ValueError("not an LZ11 block")
    size = src[1] | (src[2] << 8) | (src[3] << 16)
    out = bytearray()
    i = 4
    while len(out) < size:
        flags = src[i]
        i += 1
        for bit in range(8):
            if len(out) >= size:
                break
            if not flags & (0x80 >> bit):
                out.append(src[i])
                i += 1
                continue

            b = src[i]
            i += 1
            ind = b >> 4
            if ind == 0:                        # 2-byte length, 0x11..0x110
                n = ((b & 0xF) << 4) | (src[i] >> 4)
                disp = ((src[i] & 0xF) << 8) | src[i + 1]
                i += 2
                n += 0x11
            elif ind == 1:                      # 3-byte length, 0x111..0x1110
                n = ((b & 0xF) << 12) | (src[i] << 4) | (src[i + 1] >> 4)
                disp = ((src[i + 1] & 0xF) << 8) | src[i + 2]
                i += 3
                n += 0x111
            else:                               # 1-byte length, 3..0x10
                n = ind + 1
                disp = ((b & 0xF) << 8) | src[i]
                i += 1

            if disp == 0 or disp > len(out):
                raise ValueError("LZ11 back-reference out of range")
            for _ in range(n):
                out.append(out[-disp])
    return bytes(out[:size])


def find_cbmd(data):
    """The CBMD offset: the file's own magic, or the ExeFS banner inside a CIA."""
    start = 0
    if data.find(b"NCCH") > 0:
        start = data.find(b"NCCH")
    pos = data.find(b"CBMD", start)
    while pos >= 0:
        if pos + CBMD_HEADER <= len(data):
            offsets = struct.unpack_from("<%dI" % CBMD_NUM_CGFXS, data,
                                         pos + CGFX_OFFSETS)
            if any(pos + o + 4 <= len(data) and data[pos + o] == 0x11
                   for o in offsets if o):
                return pos
        pos = data.find(b"CBMD", pos + 4)
    raise SystemExit("no CBMD banner found (not a .bnr, or a CIA built before run 14)")


def read_cgfx(data, base, offset, end, index):
    blob = data[base + offset:base + end]
    raw = lz11_decompress(blob)
    if raw[:4] != b"CGFX":
        raise ValueError("LZ11 block at +0x%X is not a CGFX" % offset)
    animated = b"CANM" in raw
    print("  CGFX %-2d: %s bytes uncompressed (%s on disk, LZ11)"
          % (index, _n(len(raw)), _n(len(blob))))
    print("           %s" % ("animated (CANM): the scene moves"
                            if animated else
                            "static (no CANM): the image does not move"))
    return raw


def cwav_extent(data, base, offset):
    """Where the CWAV ends, from the file size in its own header."""
    file_size, = struct.unpack_from("<I", data, base + offset + 0x0C)
    return offset + file_size


def read_cwav(data, base, offset):
    magic = data[base + offset:base + offset + 4]
    if magic != b"CWAV":
        print("  tune  : (offset 0x%X is %r, not CWAV)" % (offset, magic))
        return
    head = data[base + offset:base + offset + 0x40]
    header_size, = struct.unpack_from("<H", head, 0x06)
    file_size, = struct.unpack_from("<I", head, 0x0C)
    info_off, = struct.unpack_from("<I", head, 0x18)

    info = base + offset + info_off
    encoding = data[info + 0x08]
    loop = data[info + 0x09]
    rate, loop_start, loop_end = struct.unpack_from("<III", data, info + 0x0C)
    channels, = struct.unpack_from("<I", data, info + 0x1C)

    seconds = loop_end / rate if rate and loop_end else 0.0
    print("  tune  : %s bytes, %s Hz, %d channel%s, %s, %.2f s"
          % (_n(file_size), "{:,}".format(rate), channels,
             "" if channels == 1 else "s", ENCODINGS.get(encoding, "?%d" % encoding),
             seconds))
    if loop:
        print("           loops %s..%s frames (CWAV header 0x%X bytes)"
              % ("{:,}".format(loop_start), "{:,}".format(loop_end), header_size))
    else:
        print("           no loop (CWAV header 0x%X bytes)" % header_size)


def _n(value):
    return "{:,}".format(value)


def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: python tools/inspect_banner.py <file.bnr|file.cia>")
    path = sys.argv[1]
    with open(path, "rb") as fh:
        data = fh.read()

    base = find_cbmd(data)
    kind = "CIA" if data.find(b"NCCH") > 0 else "banner"
    print("file: %s (%s bytes)" % (path, _n(len(data))))
    print("%s: CBMD at 0x%X" % (kind, base))

    offsets = struct.unpack_from("<%dI" % CBMD_NUM_CGFXS, data, base + CGFX_OFFSETS)
    present = [(i, o) for i, o in enumerate(offsets) if o]
    cwav, = struct.unpack_from("<I", data, base + CWAV_OFFSET)

    # each CGFX runs until the next one, or until the CWAV if that follows it
    bounds = sorted([o for _, o in present] + ([cwav] if cwav else []))
    print("  CGFX images: %d of %d language slots"
          % (len(present), CBMD_NUM_CGFXS))
    for index, offset in present:
        later = [b for b in bounds if b > offset]
        end = later[0] if later else len(data) - base
        read_cgfx(data, base, offset, end, index)

    if cwav:
        read_cwav(data, base, cwav)
        print("  banner ends: %s bytes after the CBMD"
              % _n(cwav_extent(data, base, cwav)))
    else:
        print("  tune  : none (silent banner)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
