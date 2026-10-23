#!/usr/bin/env python3
"""Throwaway debugger: find where the Marshal reader desynchronises."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rxdata_dump import MarshalReader  # noqa: E402


def main():
    path = sys.argv[1]
    d = open(path, "rb").read()
    r = MarshalReader(d)
    hist = []
    orig = r.obj

    def traced():
        p = r.p
        t = chr(d[p]) if p < len(d) else "?"
        v = orig()
        hist.append((p, t, r.p, type(v).__name__,
                     (v[:24] if isinstance(v, (str, list)) else v)))
        return v

    r.obj = traced
    try:
        r.obj()
        print("[+] parsed cleanly, %d objects" % len(hist))
    except Exception as exc:  # noqa: BLE001
        print("[!] %s at offset %d" % (exc, r.p))
    print("\n[-] last 25 reads (start, type, end, pytype, value):")
    for h in hist[-25:]:
        print("   %8d %s %8d %-9s %r" % h)
    lo = max(0, r.p - 48)
    print("\n[-] bytes around failure offset %d:" % r.p)
    print("   " + d[lo:r.p + 16].hex(" "))
    print("   " + "".join(chr(b) if 32 <= b < 127 else "." for b in d[lo:r.p + 16]))


if __name__ == "__main__":
    main()
