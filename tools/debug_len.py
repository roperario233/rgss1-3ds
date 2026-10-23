#!/usr/bin/env python3
"""Throwaway: inspect how a Marshal string length is encoded in Scripts.rxdata."""
import sys

import sys
import zlib

d = open(sys.argv[1], "rb").read()
start = 24                      # right after '"' 0x02 0x8d 0x04
obj = zlib.decompressobj()
out = obj.decompress(d[start:])
consumed = len(d) - start - len(obj.unused_data)
print("zlib stream at %d consumed %d bytes -> %d bytes of Ruby source"
      % (start, consumed, len(out)))
print("source head: %r" % out[:60])

prefix = d[21:24]
print("\nlength prefix bytes: %s" % prefix.hex(" "))
print("  c=%d, payload=%s" % (prefix[0], prefix[1:].hex(" ")))
print("  big-endian   -> %d" % int.from_bytes(prefix[1:], "big"))
print("  little-endian-> %d" % int.from_bytes(prefix[1:], "little"))
print("  true length  -> %d" % consumed)

print("\nnext record should start at offset %d:" % (start + consumed))
nxt = start + consumed
print("  bytes: %s" % d[nxt:nxt + 16].hex(" "))
print("  ascii: %s" % "".join(chr(b) if 32 <= b < 127 else "." for b in d[nxt:nxt + 16]))
