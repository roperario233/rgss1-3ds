#!/usr/bin/env python3
"""Describe the shape of a translation file (lang/<code>/lang.rxdata)."""
import collections
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rxdata_dump import RubyObject, Sym, Table, load  # noqa: E402


def walk(o, strings, depth=0):
    if depth > 12:
        return
    if isinstance(o, str):
        strings.append(o)
        return
    if isinstance(o, (list, tuple)):
        for v in o:
            walk(v, strings, depth + 1)
        return
    if isinstance(o, dict):
        for k, v in o.items():
            walk(v, strings, depth + 1)
        return
    if isinstance(o, RubyObject):
        for v in o.ivars.values():
            walk(v, strings, depth + 1)


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    except Exception:  # noqa: BLE001
        pass
    path = sys.argv[1]
    data = load(path)
    print("top level: %s" % (data.cls if isinstance(data, RubyObject)
                             else type(data).__name__))
    if isinstance(data, RubyObject):
        print("ivars:")
        for k, v in data.ivars.items():
            kind = v.cls if isinstance(v, RubyObject) else type(v).__name__
            n = len(v) if isinstance(v, (list, dict, str)) else "-"
            print("   %-20s %-14s items=%s" % (k, kind, n))
    strings = []
    walk(data, strings)
    print("\n%d strings total" % len(strings))
    lengths = collections.Counter(len(s) for s in strings)
    print("length histogram (len: count), top 10: %s"
          % lengths.most_common(10))
    print("\n10 longest strings:")
    for s in sorted(strings, key=len, reverse=True)[:10]:
        print("   [%4d] %s" % (len(s), s[:180].replace("\n", "\\n")))


if __name__ == "__main__":
    main()
