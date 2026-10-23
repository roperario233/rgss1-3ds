#!/usr/bin/env python3
"""Locate where the game's English dialogue text actually lives."""
import glob
import os

NEEDLES = [b"Johnny", b"Lily", b"River", b"Doctor", b"moon", b"memory"]

print("=== work/raw/Data/*.rxdata ===")
for f in sorted(glob.glob(os.path.join("work", "raw", "Data", "*.rxdata"))):
    data = open(f, "rb").read()
    hits = []
    for n in NEEDLES:
        i = data.find(n)
        if i >= 0:
            hits.append((n, i))
    if hits:
        print("%-28s %d/%d needles" % (os.path.basename(f), len(hits), len(NEEDLES)))
        for n, i in hits[:3]:
            ctx = data[max(0, i - 60):i + 90]
            print("    %-8s @%-8d %r" % (n.decode(), i, ctx))

print("\n=== work/scripts/*.rb ===")
for f in sorted(glob.glob(os.path.join("work", "scripts", "*.rb"))):
    data = open(f, "rb").read()
    hits = [n for n in NEEDLES if data.find(n) >= 0]
    if hits:
        print("%-42s %s" % (os.path.basename(f), [h.decode() for h in hits]))
