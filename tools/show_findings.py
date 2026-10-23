#!/usr/bin/env python3
"""Print the detailed findings for one or more inventory groups.

Usage: python show_findings.py windows_interop,resolution_640x480 [--active]

With --active, comment lines are skipped, so only live code is reported.
"""
import json
import os
import sys

args = [a for a in sys.argv[1:] if not a.startswith("--")]
flags = set(a for a in sys.argv[1:] if a.startswith("--"))
active_only = "--active" in flags
summary = "--summary" in flags

path = os.path.join("work", "inventory.json")
inv = json.load(open(path, encoding="utf-8"))
groups = args[0].split(",") if args else ["windows_interop"]

if summary:
    for group in groups:
        agg = {}
        files = {}
        for fname, per_file in inv["scripts"]["per_file"].items():
            if group not in per_file:
                continue
            for pat, d in per_file[group].items():
                agg[pat] = agg.get(pat, 0) + d["count"]
                files.setdefault(pat, set()).add(fname)
        print("=" * 78)
        print("GROUP %s -- aggregated over all %d script files"
              % (group, inv["scripts"]["file_count"]))
        print("=" * 78)
        for pat, n in sorted(agg.items(), key=lambda kv: -kv[1]):
            print("  %-30s %6d hits in %3d files" % (pat, n, len(files[pat])))
        print()
    sys.exit(0)

for group in groups:
    print("=" * 78)
    print("GROUP: %s%s" % (group, "  (live code only)" if active_only else ""))
    print("=" * 78)
    hits = 0
    for fname, per_file in sorted(inv["scripts"]["per_file"].items()):
        if group not in per_file:
            continue
        rows = []
        for pat, d in sorted(per_file[group].items(), key=lambda kv: -kv[1]["count"]):
            exs = [e for e in d["examples"]
                   if not (active_only and e["text"].strip().startswith("#"))]
            if active_only and not exs:
                continue
            rows.append((pat, d["count"], exs))
        if not rows:
            continue
        print("\n-- %s" % fname)
        for pat, count, exs in rows:
            hits += count
            print("   %-28s x%d" % (pat, count))
            for ex in exs:
                print("        %5d: %s" % (ex["line"], ex["text"]))
    print("\n[%s] matches shown: %d\n" % (group, hits))
