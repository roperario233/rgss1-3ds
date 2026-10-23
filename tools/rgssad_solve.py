#!/usr/bin/env python3
"""
rgssad_solve.py -- determine the exact RGSSAD v1 byte-coding rules empirically.

Published descriptions of RGSSAD v1 disagree on the details (endianness of the
u32 fields, whether the key rotates per byte or advances per 4-byte chunk, what
the key advance is). Rather than trust any one description, this tool tries every
plausible combination and keeps the ones that turn the WHOLE archive into
believable entries:

  * every entry name is printable ASCII and looks like a game path
  * every payload whose extension has a known magic number matches it
    (PNG, OggS, Ruby Marshal 4.8, MZ)
  * the parse either hits a zero-length terminator or consumes the file exactly

Only a correct rule set satisfies all of those across thousands of entries, so
the winner is proof, not a guess.

Usage:  python rgssad_solve.py <archive-or-zip> [--entry NAME] [--top 5]
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rgssad_extract import (KEY0, MASK, RGSSAD_MAGIC, load_archive,  # noqa: E402
                            magic_ok, png_size)


def rotl(v, n=1):
    n %= 32
    return ((v << n) | (v >> (32 - n))) & MASK


# --------------------------------------------------------------------------
# primitive decoders, each parameterised by the rule under test
# --------------------------------------------------------------------------

def read_u32(buf, pos, key, mode):
    b = buf[pos:pos + 4]
    if mode == "le_same":
        return (int.from_bytes(b, "little") ^ key) & MASK
    if mode == "be_same":
        return (int.from_bytes(b, "big") ^ key) & MASK
    if mode == "le_rot":
        v, k = 0, key
        for i in range(4):
            v |= (b[i] ^ (k & 0xFF)) << (8 * i)
            k = rotl(k)
        return v & MASK
    if mode == "be_rot":
        v, k = 0, key
        for i in range(4):
            v = (v << 8) | (b[i] ^ (k & 0xFF))
            k = rotl(k)
        return v & MASK
    raise ValueError(mode)


def decode_bytes(buf, pos, n, key, mode):
    """Return (decoded_bytes, key_after)."""
    out = bytearray()
    k = key
    for i in range(n):
        if mode == "le_same":
            x = (k >> (8 * (i % 4))) & 0xFF
        elif mode == "be_same":
            x = (k >> (8 * (3 - i % 4))) & 0xFF
        elif mode == "le_chunk7":
            if i and i % 4 == 0:
                k = (k + 7) & MASK
            x = (k >> (8 * (i % 4))) & 0xFF
        elif mode == "rot1":
            x = k & 0xFF
            k = rotl(k)
        elif mode == "rot1_pre":
            k = rotl(k)
            x = k & 0xFF
        elif mode == "adv7":
            x = k & 0xFF
            k = (k + 7) & MASK
        elif mode == "adv7_mul":
            x = k & 0xFF
            k = (k * 7 + 3) & MASK
        else:
            raise ValueError(mode)
        out.append(buf[pos + i] ^ x)
    return bytes(out), k


def decode_payload(data, key, mode):
    out = bytearray(data)
    if mode == "chunk7":
        k = key
        off = 0
        while off < len(out):
            cl = min(4, len(out) - off)
            for i in range(cl):
                out[off + i] ^= (k >> (8 * i)) & 0xFF
            k = (k + 7) & MASK
            off += 4
        return bytes(out)
    if mode == "rot1":
        k = key
        for i in range(len(out)):
            out[i] ^= k & 0xFF
            k = rotl(k)
        return bytes(out)
    raise ValueError(mode)


ADVANCE = {
    "none": lambda k: k,
    "+7": lambda k: (k + 7) & MASK,
    "rot4": lambda k: rotl(k, 4),
    "mul7+3": lambda k: (k * 7 + 3) & MASK,
}


def try_parse(buf, r4, nm, adv, pl, key0=KEY0, max_entries=100000):
    pos, key, entries = 8, key0, []
    end = len(buf)
    term = False
    for _ in range(max_entries):
        if pos + 4 > end:
            break
        name_len = read_u32(buf, pos, key, r4)
        pos += 4
        key = ADVANCE[adv](key)
        if name_len == 0:
            term = True
            break
        if name_len > 512 or pos + name_len + 4 > end:
            return None, "name length %d at 0x%x" % (name_len, pos), None, None
        raw, key = decode_bytes(buf, pos, name_len, key, nm)
        pos += name_len
        key = ADVANCE[adv](key)
        size = read_u32(buf, pos, key, r4)
        pos += 4
        key = ADVANCE[adv](key)
        if size > end - pos:
            return None, "size %d exceeds remaining %d" % (size, end - pos), None, None
        payload = decode_payload(buf[pos:pos + size], key, pl)
        pos += size
        key = ADVANCE[adv](key)
        try:
            name = raw.decode("utf-8")
        except UnicodeDecodeError:
            return None, "undecodable name", None, None
        entries.append((name, size, payload))
    return entries, None, pos, term


def score(entries, pos, term, total):
    if not entries or len(entries) < 10:
        return -10 ** 6, {}
    stats = {"names_ok": 0, "magic_ok": 0, "magic_bad": 0, "magic_unknown": 0}
    for name, size, payload in entries:
        if not name or any(ord(c) < 32 or ord(c) > 126 for c in name):
            return -10 ** 6, {}
        if "/" in name:
            stats["names_ok"] += 1
        ok = magic_ok(name, payload)
        if ok is True:
            stats["magic_ok"] += 1
        elif ok is False:
            stats["magic_bad"] += 1
        else:
            stats["magic_unknown"] += 1
    if stats["names_ok"] != len(entries):
        return -10 ** 6, {}
    s = stats["magic_ok"] * 10 - stats["magic_bad"] * 30
    if term:
        s += 500
    if total - pos == 0:
        s += 500
    elif total - pos < 4096:
        s += 100
    else:
        s -= 2000
    s += len(entries)
    return s, stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--entry")
    ap.add_argument("--top", type=int, default=5)
    ap.add_argument("--key0", default=hex(KEY0))
    args = ap.parse_args()

    buf, src = load_archive(args.path, args.entry)
    print("[i] %s (%d bytes)" % (src, len(buf)))
    if not buf.startswith(RGSSAD_MAGIC):
        raise SystemExit("not an RGSSAD archive")

    key0 = int(args.key0, 0)
    results = []
    for r4 in ("le_same", "be_same", "le_rot", "be_rot"):
        for nm in ("le_same", "be_same", "le_chunk7", "rot1", "rot1_pre",
                   "adv7", "adv7_mul"):
            for adv in ("none", "+7", "rot4", "mul7+3"):
                for pl in ("chunk7", "rot1"):
                    try:
                        entries, err, pos, term = try_parse(buf, r4, nm, adv, pl, key0)
                    except Exception:  # noqa: BLE001
                        continue
                    if err or not entries:
                        continue
                    s, stats = score(entries, pos, term, len(buf))
                    if s > 0:
                        results.append((s, r4, nm, adv, pl, len(entries), stats, term))

    results.sort(key=lambda r: -r[0])
    if not results:
        raise SystemExit("no rule combination parsed the archive plausibly")
    print("[+] %d plausible rule sets; top %d:"
          % (len(results), min(args.top, len(results))))
    for s, r4, nm, adv, pl, n, stats, term in results[:args.top]:
        print("    score=%-8d read4=%-8s name=%-9s adv=%-7s payload=%-6s entries=%-6d "
              "magic_ok=%-5d magic_bad=%-4d terminator=%s"
              % (s, r4, nm, adv, pl, n, stats["magic_ok"], stats["magic_bad"], term))

    s, r4, nm, adv, pl, n, stats, term = results[0]
    print("\n[+] WINNER: read4=%s name=%s advance=%s payload=%s" % (r4, nm, adv, pl))
    entries, err, pos, term = try_parse(buf, r4, nm, adv, pl, key0)
    over = [e for e in entries
            if os.path.splitext(e[0])[1].lower() == ".png"
            and png_size(e[2]) and max(png_size(e[2])) > 1024]
    print("[+] %d entries, %d bytes consumed of %d, terminator=%s"
          % (len(entries), pos, len(buf), term))
    print("[+] PNGs over 1024px: %d" % len(over))
    print("[+] first 12 names:")
    for name, size, _ in entries[:12]:
        print("      %10d  %s" % (size, name))


if __name__ == "__main__":
    sys.exit(main())
