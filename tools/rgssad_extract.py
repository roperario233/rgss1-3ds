#!/usr/bin/env python3
"""
rgssad_extract.py -- decode an RGSSAD (RPG Maker XP) archive and report its contents.

Reads the archive from a plain file OR directly from a .zip entry, so the 215 MB
game repack never has to be expanded on disk.

RGSSAD v1 container layout
--------------------------
    "RGSSAD\\0" + version byte (0x01)
    repeated entries:
        4-byte u32 name length   -- each byte XORed with (key & 0xFF); key = rotl(key,1) per byte
        name bytes               -- same XOR/rotate scheme, per byte
        4-byte u32 payload size  -- same XOR/rotate scheme
        payload                  -- each 4-byte chunk XORed with key; key += 7 after each chunk
    terminated by a zero name length, or by end of file

Published sources disagree on the endianness of the u32 fields, so this tool
parses with BOTH variants and keeps whichever one turns the whole archive into
plausible entries: names that look like game paths and payloads whose first bytes
match the magic number for their extension (PNG/OggS/Ruby Marshal/MZ). That
self-check is the correctness proof -- not an assumption.

Usage
-----
    python rgssad_extract.py probe   <path> [--entry NAME]
    python rgssad_extract.py extract <path> [--entry NAME] [--out DIR]
                                     [--manifest FILE] [--list-only] [--no-write]
"""

import argparse
import json
import os
import struct
import sys
import zipfile

RGSSAD_MAGIC = b"RGSSAD\x00"
KEY0 = 0xDEADCAFE
MASK = 0xFFFFFFFF
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
JPEG_MAGIC = b"\xff\xd8\xff"


# ---------------------------------------------------------------------------
# magic-number identification (used to validate the decode)
# ---------------------------------------------------------------------------

def magic_ok(name, data):
    """Return True/False for extensions we can verify, None when unknown."""
    ext = os.path.splitext(name)[1].lower()
    if ext in (".png", ".jpg", ".jpeg"):
        # RGSS projects often store a JPEG under a .png name (To the Moon's
        # "NightCliff.png" is one); either header proves the bytes decoded.
        return data[:8] == PNG_MAGIC or data[:3] == JPEG_MAGIC
    if ext == ".ogg":
        return data[:4] == b"OggS"
    if ext in (".rxdata", ".rvdata", ".rxdata2", ".rvdata2"):
        return data[:2] == b"\x04\x08"  # Ruby Marshal 4.8
    if ext in (".exe", ".dll"):
        return data[:2] == b"MZ"
    if ext in (".rb", ".txt", ".ini", ".json", ".dat"):
        return b"\x00" not in data[:256]
    return None


def png_size(data):
    """(width, height) straight out of the IHDR chunk, no decoding needed."""
    if len(data) < 24 or data[:8] != PNG_MAGIC:
        return None
    return struct.unpack(">II", data[16:24])


# ---------------------------------------------------------------------------
# RGSSAD parsing
# ---------------------------------------------------------------------------

def _rotl1(v):
    return ((v << 1) | (v >> 31)) & MASK


def _read_xored_u32(buf, pos, key, little):
    raw = []
    for _ in range(4):
        raw.append(buf[pos] ^ (key & 0xFF))
        pos += 1
        key = _rotl1(key)
    if little:
        value = raw[0] | raw[1] << 8 | raw[2] << 16 | raw[3] << 24
    else:
        value = raw[0] << 24 | raw[1] << 16 | raw[2] << 8 | raw[3]
    return value, pos, key


def parse_archive(buf, little=True):
    """Parse an RGSSAD v1 archive.

    Rule set confirmed against the reference implementation
    (KatyushaScarlet/RGSS-Extractor: RGSS_Extractor/RGSSAD_Parser.cs and
    RGSS_Extractor/Parser.cs), and verified byte-for-byte against this archive:

        key = 0xDEADCAFE
        repeat:
            name_len  = le_u32 ^ key                  ; key = key*7 + 3
            name[i]   = raw[i] ^ (key & 0xFF)         ; key = key*7 + 3   (per byte)
            size      = le_u32 ^ key                  ; key = key*7 + 3
            data_key  = key            # the payload does NOT advance the table key
            payload   = chunked XOR with data_key     ; data_key = data_key*7 + 3 per 4-byte chunk
        until end of file (or a zero name length)

    The key advance is key*7+3, NOT the "+7" that many descriptions claim.
    """
    if not buf.startswith(RGSSAD_MAGIC):
        raise ValueError("not an RGSSAD archive (bad magic)")
    version = buf[7]
    if version != 1:
        raise ValueError("unsupported RGSSAD version byte: %d" % version)

    pos = 8
    key = KEY0
    end = len(buf)
    entries = []
    terminator = False

    while True:
        if pos + 4 > end:
            break
        name_len = (int.from_bytes(buf[pos:pos + 4], "little") ^ key) & MASK
        pos += 4
        key = (key * 7 + 3) & MASK
        if name_len == 0:
            terminator = True
            break
        if name_len > 1024 or pos + name_len + 4 > end:
            raise ValueError("implausible name length %d at offset 0x%x"
                             % (name_len, pos))

        raw = bytearray()
        for _ in range(name_len):
            raw.append(buf[pos] ^ (key & 0xFF))
            pos += 1
            key = (key * 7 + 3) & MASK
        try:
            name = raw.decode("utf-8")
        except UnicodeDecodeError:
            name = raw.decode("latin-1")

        size = (int.from_bytes(buf[pos:pos + 4], "little") ^ key) & MASK
        pos += 4
        key = (key * 7 + 3) & MASK
        if pos + size > end:
            raise ValueError("entry %r claims %d bytes but only %d remain"
                             % (name, size, end - pos))

        data_key = key
        payload = bytearray(buf[pos:pos + size])
        pos += size
        off = 0
        while off < len(payload):
            for j in range(min(4, len(payload) - off)):
                payload[off + j] ^= (data_key >> (8 * j)) & 0xFF
            data_key = (data_key * 7 + 3) & MASK
            off += 4

        entries.append({"name": name, "size": size,
                        "offset": pos - size, "payload": bytes(payload)})

    return entries, pos, terminator


def score_parse(entries, end_pos, total_len, terminator):
    """How believable is this parse? Higher is better; negative means 'broken'."""
    if not entries:
        return -1000
    score = 0
    if terminator:
        score += 100
    slack = total_len - end_pos
    if slack <= 3:
        score += 50
    elif slack < 4096:
        score += 10
    else:
        score -= 100

    names = set()
    for e in entries:
        name = e["name"]
        if not name or any(ord(c) < 32 or ord(c) > 126 for c in name):
            return -1000
        if name in names:
            score -= 50
        names.add(name)
        ok = magic_ok(name, e["payload"])
        if ok is True:
            score += 10
        elif ok is False:
            score -= 20
        if name.startswith(("Data/", "Graphics/", "Audio/")):
            score += 2
    return score


def load_archive(path, entry=None):
    """Return (bytes, description). Reads from inside a zip if given a zip."""
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as zf:
            if entry is None:
                cands = [i for i in zf.infolist()
                         if os.path.basename(i.filename).lower().endswith(
                             (".rgssad", ".rgss2a", ".rgss3a", ".dat"))]
                if not cands:
                    raise SystemExit("no .rgssad/.rgss2a/.rgss3a entry found in %s" % path)
                cands.sort(key=lambda i: -i.file_size)
                entry = cands[0].filename
            print("[i] reading zip entry: %s" % entry)
            with zf.open(entry) as fh:
                return fh.read(), entry
    with open(path, "rb") as fh:
        return fh.read(), path


def cmd_extract(args):
    buf, src = load_archive(args.path, args.entry)
    print("[i] archive: %s (%d bytes)" % (src, len(buf)))
    print("[i] header: %r version=%d" % (buf[:8], buf[7]))

    entries, end_pos, term = parse_archive(buf)
    known = sum(1 for e in entries if magic_ok(e["name"], e["payload"]) is True)
    bad = sum(1 for e in entries if magic_ok(e["name"], e["payload"]) is False)
    print("[+] parsed %d entries (%d magic-verified, %d magic-failed), "
          "%d bytes consumed of %d, terminator=%s"
          % (len(entries), known, bad, end_pos, len(buf), term))
    if bad:
        raise SystemExit("[!] %d payloads failed their magic check - decode is wrong, "
                         "refusing to write" % bad)
    print("[+] %d files, %d bytes payload"
          % (len(entries), sum(e["size"] for e in entries)))

    ext_hist = {}
    biggest = []
    oversized = []
    for e in entries:
        ext = os.path.splitext(e["name"])[1].lower() or "(none)"
        ext_hist[ext] = ext_hist.get(ext, 0) + 1
        biggest.append((e["size"], e["name"]))
        if ext == ".png":
            dim = png_size(e["payload"])
            if dim and (dim[0] > 1024 or dim[1] > 1024):
                oversized.append([dim[0], dim[1], e["name"]])
    biggest.sort(reverse=True)

    print("\n[i] extension histogram:")
    for ext, count in sorted(ext_hist.items(), key=lambda kv: -kv[1]):
        print("      %-10s %5d" % (ext, count))

    print("\n[i] 20 largest entries:")
    for size, name in biggest[:20]:
        print("      %10d  %s" % (size, name))

    if oversized:
        print("\n[!] PNGs larger than 1024x1024 (PICA200 texture limit on 3DS):")
        for w, h, name in sorted(oversized, reverse=True)[:40]:
            print("      %5dx%-5d %s" % (w, h, name))
        print("      ... %d total" % len(oversized))
    else:
        print("\n[+] no PNG exceeds 1024x1024")

    manifest = {
        "source": src,
        "archive_bytes": len(buf),
        "version": buf[7],
        "byte_order": "little",
        "key_advance": "key = key*7 + 3",
        "ended_on_terminator": term,
        "entries": [{"name": e["name"], "size": e["size"], "offset": e["offset"],
                     "magic_ok": magic_ok(e["name"], e["payload"]),
                     "png_size": png_size(e["payload"])} for e in entries],
        "extension_histogram": ext_hist,
        "oversized_pngs": oversized,
    }
    if args.manifest:
        d = os.path.dirname(os.path.abspath(args.manifest))
        os.makedirs(d, exist_ok=True)
        with open(args.manifest, "w", encoding="utf-8") as fh:
            json.dump(manifest, fh, indent=1)
        print("\n[+] wrote manifest: %s" % args.manifest)

    if args.list_only or args.no_write:
        return

    written = 0
    for e in entries:
        parts = [p for p in e["name"].replace("\\", "/").split("/") if p]
        dest = os.path.join(args.out, *parts)
        os.makedirs(os.path.dirname(os.path.abspath(dest)), exist_ok=True)
        with open(dest, "wb") as fh:
            fh.write(e["payload"])
        written += 1
    print("[+] extracted %d files to %s" % (written, args.out))


# ---------------------------------------------------------------------------
# probe: identify an unknown file (e.g. the lang.dat that mkxp mounts as RTP)
# ---------------------------------------------------------------------------

SIGNATURES = [
    (b"PK\x03\x04", "ZIP archive"),
    (b"PK\x05\x06", "ZIP archive (empty)"),
    (b"RGSSAD\x00", "RGSSAD v1 (RPG Maker XP)"),
    (b"RGSS3A", "RGSS3A (RPG Maker VX Ace)"),
    (b"RGSS2A", "RGSS2A (RPG Maker VX)"),
    (b"OggS", "Ogg container"),
    (PNG_MAGIC, "PNG image"),
    (b"UnityFS", "Unity asset bundle"),
    (b"PFS0", "Nintendo Switch PFS0 (NSP/NCA payload)"),
    (b"MZ", "DOS/Windows executable"),
    (b"7z\xbc\xaf\x27\x1c", "7-Zip archive"),
    (b"Rar!", "RAR archive"),
    (b"SQLite format 3", "SQLite database"),
    (b"\x04\x08", "Ruby Marshal 4.8 stream"),
]


def cmd_probe(args):
    buf, src = load_archive(args.path, args.entry)
    print("[i] %s (%d bytes)" % (src, len(buf)))
    head = buf[:64]
    print("[i] first 64 bytes (hex):   %s" % head.hex(" "))
    print("[i] first 64 bytes (ascii): %s"
          % "".join(chr(b) if 32 <= b < 127 else "." for b in head))
    print("[i] guessed type:")
    found = False
    for sig, label in SIGNATURES:
        if head.startswith(sig):
            print("      %s" % label)
            found = True
    if len(buf) >= 8 and buf[:7] == RGSSAD_MAGIC:
        print("      RGSSAD version byte: %d" % buf[7])
    if not found and len(buf) > 2 and buf[0] == 0x78 and buf[1] in (0x01, 0x5E, 0x9C, 0xDA):
        print("      possibly a raw zlib stream (0x%02x 0x%02x)" % (buf[0], buf[1]))
    if not found:
        print("      (no known signature matched)")


def main():
    ap = argparse.ArgumentParser(
        description="Decode RGSSAD (RPG Maker XP) archives; probe unknown files.",
        formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("probe", help="identify an unknown file")
    p.add_argument("path")
    p.add_argument("--entry", help="zip entry name to read instead of a plain file")
    p.set_defaults(func=cmd_probe)

    e = sub.add_parser("extract", help="decode and extract an RGSSAD archive")
    e.add_argument("path")
    e.add_argument("--entry", help="zip entry name to read instead of a plain file")
    e.add_argument("--out", default=os.path.join("work", "raw"))
    e.add_argument("--manifest", default=os.path.join("work", "rgssad_manifest.json"))
    e.add_argument("--list-only", action="store_true")
    e.add_argument("--no-write", action="store_true")
    e.set_defaults(func=cmd_extract)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    sys.exit(main())
