#!/usr/bin/env python3
"""pack_romfs.py -- everything the app can carry, prepared for its romfs.

build.ps1 / build_cia.ps1 copy the result into the app, so a CIA can be the only
file the console needs. Two parts:

  game/    the main game: To the Moon.rgssad (34.8 MB), lang.dat (17 MB) and the
           fonts, so the launcher's first entry needs nothing on the SD card
           (saves still go to SD:/3ds/ttm3ds/, where they always were)
  games/   the two Sigmund minisodes: each is a whole RGSS1 game -- archive,
           Game.ini, and only the sounds the main game's audio pack lacks. Of
           HS1's 257 sounds 229 are already in it and of HS2's 277, 225, because
           the lookup is by "kind/name" with the extension ignored, so nothing
           is stored twice.

Usage:
    python tools/pack_romfs.py [the game's zip] [--out work/romfs_extra] [--no-game]

The zip is the one this project was set up from (it holds the install folder,
lang.dat and the two Sigmund folders). Nothing is extracted twice: the files are
read straight out of it.
"""
import argparse
import os
import shutil
import sys
import zipfile

DEFAULT_ZIP = "To.the.Moon.v2023.07.03.Incl.ALL.DLC.zip"
OUT = os.path.join("work", "romfs_extra")
EPISODES = ["Sigmund Holiday Special 1", "Sigmund Holiday Special 2"]


def sound_key(rel):
    """"BGM/x.ogg" -> "bgm/x" (audio.c's key: kind/name, no extension)."""
    return os.path.splitext(rel)[0].replace("\\", "/").lower()


def write(z, name, dest):
    os.makedirs(os.path.dirname(dest), exist_ok=True)
    with open(dest, "wb") as fh:
        fh.write(z.read(name))
    return z.getinfo(name).file_size


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("zip", nargs="?", default=DEFAULT_ZIP)
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--no-game", action="store_true", help="the minisodes only")
    args = ap.parse_args()

    if not os.path.exists(args.zip):
        raise SystemExit("no %s (pass the download's zip as the first argument)" % args.zip)
    z = zipfile.ZipFile(args.zip)

    roots = sorted({n.rsplit("/", 1)[0] for n in z.namelist()
                    if n.endswith("To the Moon.rgssad")})
    if not roots:
        raise SystemExit("no To the Moon.rgssad in %s" % args.zip)
    root = roots[0] + "/"

    if os.path.isdir(args.out):
        shutil.rmtree(args.out)
    os.makedirs(args.out)

    total = 0
    if not args.no_game:
        dest = os.path.join(args.out, "game")
        n = 0
        for name in [root + "To the Moon.rgssad", root + "lang.dat", root + "Game.ini"] + \
                    [x for x in z.namelist() if x.startswith(root + "Fonts/") and not x.endswith("/")]:
            rel = name[len(root):]
            total += write(z, name, os.path.join(dest, *rel.split("/")))
            n += 1
        print("[+] game/                       %d files, %6.1f MB (archive, lang.dat, Game.ini, fonts)"
              % (n, sum(z.getinfo(x).file_size for x in z.namelist()
                        if x.startswith(root + "Fonts/") and not x.endswith("/")) / 1048576.0
                 + (z.getinfo(root + "To the Moon.rgssad").file_size
                    + z.getinfo(root + "lang.dat").file_size
                    + z.getinfo(root + "Game.ini").file_size) / 1048576.0))

    main_keys = {sound_key(x[len(root + "Audio/"):]) for x in z.namelist()
                 if x.startswith(root + "Audio/") and not x.endswith("/")}
    print("[i] the main game's audio: %d sounds (the app already carries them)" % len(main_keys))

    for name in EPISODES:
        src = root + name + "/"
        names = [x for x in z.namelist() if x.startswith(src) and not x.endswith("/")]
        if not names:
            print("[!] %s is not in the zip -- skipped" % name)
            continue
        dest = os.path.join(args.out, "games", name)
        os.makedirs(dest)
        copied = bytes_copied = 0
        for x in names:
            rel = x[len(src):]
            if not (rel.endswith(".rgssad") or rel.startswith("Audio/") or
                    (rel.endswith(".ini") and "/" not in rel)):
                continue                            # every .ini: Minisode 2's is
                                                    # "SigCorp Minisode 2.ini", not
                                                    # Game.ini (run 19: its tile
                                                    # showed the .rgssad name)
            if rel.startswith("Audio/") and sound_key(rel[len("Audio/"):]) in main_keys:
                continue                            # the main pack already has it
            bytes_copied += write(z, x, os.path.join(dest, *rel.split("/")))
            copied += 1
        total += bytes_copied
        print("[+] games/%-28s %3d files, %6.1f MB" % (name, copied, bytes_copied / 1048576.0))

    print("[+] %s: %.1f MB in total" % (args.out, total / 1048576.0))
    print("[i] build_cia.ps1 packs it into romfs:/ (build.ps1 needs -WithMinisodes)")


if __name__ == "__main__":
    sys.exit(main())
