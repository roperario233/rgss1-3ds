#!/usr/bin/env python3
"""
check_cia_clean.py -- refuse to publish a CIA that carries the game.

The engine-only CIA is the one artifact this project publishes, and its whole value
is that it contains none of To the Moon: no archive, no audio pack, no translations,
no banner drawn from the game's sky. A wrong build flag (or a stray copy in
engine/romfs) would silently put 129 MB of Freebird's work in a release, so this
reads the file and says no.

It scans the raw bytes for the markers that only appear when game data is inside --
the RomFS tables name every file, so `lang.dat` and `Game.ini` show up as plain
strings, and the archives and the audio pack have fixed magics -- and it caps the
size, because a clean engine CIA is about 3 MB.

    python tools/check_cia_clean.py engine/ttm3ds-engine.cia

Exit: 0 clean, 1 would publish game data (or the file is not a CIA at all).
"""

import argparse
import os
import struct
import sys

CIA_HEADER = 0x2020          # the CIA header's first u32: its fixed size
MAX_BYTES = 12 * 1024 * 1024     # a clean engine CIA is ~2 MB; the game one is 129 MB
MAX_ROMFS = 2 * 1024 * 1024      # the engine's romfs is 16 KB; the game's is 129 MB

# What only appears when the game's *data* is inside. Two kinds, both specific:
#
#  * data magics -- an archive's first bytes, the audio pack's header. The engine's
#    own source mentions those names, but never followed by these version bytes.
#  * RomFS entry names -- a RomFS directory table stores each name as
#    (u32 length)(name bytes), so a length-prefixed name is a *file in the app*,
#    while a name inside a .rb string is not. That distinction is the whole check:
#    the engine's Ruby talks about "lang.dat" and "Graphics/Pictures" too.
DATA_MARKERS = [
    (b"RGSSAD\x00", "an RGSS archive"),
    (b"TTMA\x01\x00\x00\x00", "the packed game audio"),
]
ENTRY_NAMES = [
    b"lang.dat", b"Game.ini", b"audio.ttma",
    b"Graphics", b"Data", b"Audio", b"Pictures", b"Fonts",
]


def main():
    ap = argparse.ArgumentParser(description="Fail if a CIA carries game data.")
    ap.add_argument("cia")
    ap.add_argument("--max-mb", type=float, default=MAX_BYTES / 1048576.0)
    args = ap.parse_args()

    if not os.path.exists(args.cia):
        print("no such file: %s" % args.cia)
        return 1
    with open(args.cia, "rb") as f:
        data = f.read()

    if struct.unpack_from("<I", data, 0)[0] != CIA_HEADER or b"NCCH" not in data[:0x100000]:
        print("%s is not a CIA (no 0x2020 header / no NCCH inside)" % args.cia)
        return 1

    size_mb = len(data) / 1048576.0
    problems = []
    if size_mb > args.max_mb:
        problems.append("%-20s %.1f MB > %.1f MB: too big to be engine-only"
                        % ("whole file", size_mb, args.max_mb))

    # ---- the RomFS: the only place game data could hide ---------------------
    ncch = data.find(b"NCCH", 0, 0x100000)
    romfs_mb = None
    if ncch < 0x100:
        problems.append("%-20s no NCCH header found" % "structure")
    else:
        part = ncch - 0x100                      # the NCCH header's first byte
        romfs_off = struct.unpack_from("<I", data, part + 0x1B0)[0] * 0x200
        romfs_size = struct.unpack_from("<I", data, part + 0x1B4)[0] * 0x200
        romfs_mb = romfs_size / 1048576.0
        start = part + romfs_off
        region = data[start:start + romfs_size] if romfs_size else b""
        if romfs_size > MAX_ROMFS:
            problems.append("%-20s %.1f MB of files packed into the app"
                            % ("romfs", romfs_mb))
        for marker, what in DATA_MARKERS:
            if marker in region:
                problems.append("%-20s %s is inside the romfs" % ("romfs", what))
        for name in ENTRY_NAMES:
            if struct.pack("<I", len(name)) + name in region:
                problems.append("%-20s the romfs carries a %r entry"
                                % ("romfs", name.decode()))
        if struct.pack("<I", 18) + b"To the Moon.rgssad" in region:
            problems.append("%-20s the romfs carries the archive" % "romfs")

    print("%s: %.2f MB total, romfs %s"
          % (args.cia, size_mb,
             ("%.0f KB" % (romfs_mb * 1024)) if romfs_mb is not None else "?"))
    if problems:
        print("\nthis CIA would publish the game's data:")
        for p in problems:
            print("  " + p)
        print("\nrebuild with:  build_cia.ps1 -EngineOnly   (and check engine/romfs)")
        return 1
    print("clean: no game data in the romfs, and small enough to be the engine alone")
    return 0


if __name__ == "__main__":
    sys.exit(main())
