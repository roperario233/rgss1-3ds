#!/usr/bin/env python3
"""List the exact RGSS API surface the game calls, with hit counts."""
import collections
import os
import re

SCRIPT_DIR = os.path.join("work", "scripts")
PATTERNS = {
    "Audio.xxx": r"Audio\.(\w+)",
    "Audio::XXX": r"Audio::(\w+)",
    "Input.xxx": r"Input\.(\w+)",
    "Input::XXX": r"Input::(\w+)",
    "Graphics.xxx": r"Graphics\.(\w+)",
    "RPG::X": r"RPG::(\w+)",
    "Class.new": r"\b([A-Z]\w+)\.new\b",
}

counters = {k: collections.Counter() for k in PATTERNS}
for fname in sorted(os.listdir(SCRIPT_DIR)):
    if not fname.endswith(".rb"):
        continue
    text = open(os.path.join(SCRIPT_DIR, fname), encoding="utf-8",
                errors="replace").read()
    for key, rx in PATTERNS.items():
        for m in re.findall(rx, text):
            counters[key][m] += 1

for key, counter in counters.items():
    print("== %s" % key)
    for name, n in counter.most_common():
        print("   %-30s %d" % (name, n))
    print()
