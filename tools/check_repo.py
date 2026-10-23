#!/usr/bin/env python3
"""
check_repo.py -- refuse to publish game data.

The whole legal footing of this repository is "no game content": no archive, no
extracted files, no fonts, no music, no text dumps, and no build that carries them
(the CIAs do). `.gitignore` keeps the obvious ones out of a commit; this is the check
that proves it, for a human before pushing and for CI on every push.

It asks git what would be committed (`git ls-files`) and, outside a repository,
walks the tree with the same ignore list. A tracked file fails if

  * its extension is one that only ever holds game data or build output, or
  * its name is lang.dat, or
  * it is larger than 5 MB (nothing legitimate here is; the biggest is a 276 KB
    stb header).

Usage:  python tools/check_repo.py [--root DIR]
Exit:   0 clean, 1 something would be published that must not be.
"""

import argparse
import fnmatch
import os
import subprocess
import sys

FORBIDDEN_EXT = {
    ".rgssad", ".rgss2a", ".rgss3a",   # RPG Maker archives
    ".rxdata", ".rvdata", ".rvdata2",  # game data files
    ".ttma", ".bnr",                   # the engine's audio pack, HOME banners
    ".cia", ".3dsx", ".elf", ".smdh",  # build output that may embed the game
    ".ttf", ".otf",                    # the game's fonts
    ".log",                            # runs carry paths and game text
    ".zip", ".7z", ".rar",             # the game package
    ".pem", ".key",                    # never a key either
}
FORBIDDEN_NAMES = {"lang.dat"}
MAX_BYTES = 5 * 1024 * 1024

IGNORED_DIRS = {
    ".git", "work", "tools/pycgfx", "engine/pc_data", "engine/pc/shots",
    "3ds-app/romfs", "3ds-app/build", "__pycache__",
}


def ignore_patterns(root):
    """The .gitignore patterns, so the pre-git walk reports what git would commit."""
    patterns = []
    try:
        with open(os.path.join(root, ".gitignore"), encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line and not line.startswith(("#", "!")):
                    patterns.append(line)
    except OSError:
        pass
    return patterns


def ignored(rel, patterns):
    parts = rel.split("/")
    for pat in patterns:
        if pat.endswith("/"):
            d = pat.rstrip("/")
            if any(fnmatch.fnmatchcase(p, d) for p in parts[:-1]) or \
               fnmatch.fnmatchcase(rel, d) or \
               any(fnmatch.fnmatchcase("/".join(parts[:i]), d) for i in range(1, len(parts))):
                return True
        elif fnmatch.fnmatchcase(rel, pat) or fnmatch.fnmatchcase(parts[-1], pat):
            return True
    return False


def tracked_files(root):
    """git's view when there is one, else a walk with the same ignore list."""
    try:
        out = subprocess.run(["git", "-C", root, "ls-files", "-z"],
                             capture_output=True, check=True).stdout
        files = [f for f in out.decode("utf-8", "replace").split("\0") if f]
        if files:
            return files, "git ls-files"
    except (OSError, subprocess.CalledProcessError):
        pass

    patterns = ignore_patterns(root)
    files = []
    for base, dirs, names in os.walk(root):
        rel = os.path.relpath(base, root).replace(os.sep, "/")
        rel = "" if rel == "." else rel
        dirs[:] = sorted(d for d in dirs
                         if (rel + "/" + d).lstrip("/") not in IGNORED_DIRS)
        for n in names:
            path = (rel + "/" + n).lstrip("/")
            if not ignored(path, patterns):
                files.append(path)
    return files, "directory walk (.gitignore applied)"


def main():
    ap = argparse.ArgumentParser(description="Fail if game data would be published.")
    ap.add_argument("--root", default=os.getcwd())
    args = ap.parse_args()

    files, how = tracked_files(args.root)
    problems = []
    for rel in files:
        path = os.path.join(args.root, rel)
        ext = os.path.splitext(rel)[1].lower()
        name = os.path.basename(rel).lower()
        if ext in FORBIDDEN_EXT:
            problems.append("%-52s %s file" % (rel, ext))
            continue
        if name in FORBIDDEN_NAMES:
            problems.append("%-52s game data file" % rel)
            continue
        try:
            size = os.path.getsize(path)
        except OSError:
            continue
        if size > MAX_BYTES:
            problems.append("%-52s %.1f MB (limit %.1f)"
                            % (rel, size / 1048576.0, MAX_BYTES / 1048576.0))

    total = sum(os.path.getsize(os.path.join(args.root, f))
                for f in files if os.path.exists(os.path.join(args.root, f)))
    print("checking %d files (%.1f MB) via %s" % (len(files), total / 1048576.0, how))
    if problems:
        print("\nwould publish something it must not:")
        for p in problems:
            print("  " + p)
        print("\nsee .gitignore and CONTRIBUTING.md")
        return 1
    print("clean: no game data, no build output, nothing oversized")
    return 0


if __name__ == "__main__":
    sys.exit(main())
