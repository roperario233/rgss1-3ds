#!/usr/bin/env python3
"""Print the script section list extracted from Scripts.rxdata."""
import json
import os
import sys

path = sys.argv[1] if len(sys.argv) > 1 else os.path.join("work", "scripts", "index.json")
data = json.load(open(path, encoding="utf-8"))
print("%-4s %-38s %8s  %s" % ("id", "name", "bytes", "file"))
total = 0
for e in data:
    total += e.get("bytes", 0)
    print("%-4s %-38s %8s  %s"
          % (e.get("id"), e.get("name"), e.get("bytes", "-"), e.get("file", "FAILED")))
print("\n%d sections, %d bytes total" % (len(data), total))
