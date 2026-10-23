#!/usr/bin/env python3
"""
rxdata_dump.py -- read RPG Maker XP (Ruby Marshal 4.8) .rxdata files.

Subcommands
-----------
    scripts <Data/Scripts.rxdata>   -> work/scripts/NNN_Name.rb + work/scripts/index.json
    tables  <Data>                  -> work/tables/*.json  (data tables, summarised)
    maps    <Data>                  -> work/tables/maps_summary.json
    dump    <file.rxdata>           -> JSON on stdout

Only the Marshal features RMXP data actually uses are implemented: nil/true/false,
Fixnum, Bignum, Float, String, Symbol, Array, Hash, object ('o'), user-defined
('u', i.e. Table), user-marshal ('U'), ivar-wrapped ('I'), extended ('e'),
Struct, Class/Module, and links ('@'/';').

Correctness is proven downstream: script sources come out of Scripts.rxdata as
zlib streams, so if link handling or the Table layout were wrong, decompression
would fail loudly instead of silently producing plausible junk.
"""

import argparse
import hashlib
import json
import os
import struct
import sys
import zlib

MASK = 0xFFFFFFFF


class Sym(str):
    """A Ruby Symbol (kept distinct from String for clarity)."""
    __slots__ = ()


class RubyObject:
    def __init__(self, cls):
        self.cls = cls
        self.ivars = {}

    def __repr__(self):
        return "RubyObject(%s, %d ivars)" % (self.cls, len(self.ivars))


class Table:
    __slots__ = ("dim", "x", "y", "z", "data")

    def __init__(self, dim, x, y, z, data):
        self.dim, self.x, self.y, self.z, self.data = dim, x, y, z, data

    @property
    def count(self):
        return len(self.data)


class MarshalReader:
    """Minimal Ruby Marshal 4.8 reader."""

    def __init__(self, data, register_all=False):
        self.d = data
        self.objects = []
        self.symbols = []
        self.register_all = register_all
        self.major, self.minor = data[0], data[1]
        if (self.major, self.minor) != (4, 8):
            raise ValueError("unsupported Marshal version %d.%d" % (self.major, self.minor))
        self.p = 2

    # ---- primitives -----------------------------------------------------
    def byte(self):
        b = self.d[self.p]
        self.p += 1
        return b

    def read(self, n):
        if self.p + n > len(self.d):
            raise ValueError("truncated stream at %d (want %d bytes)" % (self.p, n))
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def long(self):
        """Ruby Marshal variable-length integer. Multi-byte payloads are
        LITTLE-endian (proven against Scripts.rxdata: a deflated script of
        1165 bytes is encoded as c=2, 8d 04)."""
        c = self.byte()
        if c == 0:
            return 0
        if c > 127:                       # negative short form
            c -= 256
            if c < -4:
                return c + 5
            n = -c
            x = 0
            for i in range(n):
                x |= self.byte() << (8 * i)
            return -x
        if c > 4:
            return c - 5
        x = 0
        for i in range(c):
            x |= self.byte() << (8 * i)
        return x

    def _register(self, value, kind):
        if kind in ("immediate", "symbol"):
            return value
        if self.register_all or kind in ("string", "array", "hash", "object",
                                         "userdef", "usermarshal", "struct",
                                         "class", "regexp", "float", "bignum"):
            self.objects.append(value)
        return value

    # ---- composite ------------------------------------------------------
    def obj(self):
        t = chr(self.byte())
        if t == "0":
            return self._register(None, "immediate")
        if t == "T":
            return self._register(True, "immediate")
        if t == "F":
            return self._register(False, "immediate")
        if t == "i":
            return self._register(self.long(), "immediate")
        if t == "l":
            sign = self.byte()
            n = self.long()
            x = 0
            for i in range(n):
                x |= int.from_bytes(self.read(2), "little") << (16 * i)
            if sign == ord("-"):
                x = -x
            return self._register(x, "bignum")
        if t == "f":
            n = self.long()
            return self._register(float(self.read(n).decode("latin-1")), "float")
        if t == '"':
            n = self.long()
            return self._register(self.read(n).decode("latin-1"), "string")
        if t == ":":
            n = self.long()
            s = Sym(self.read(n).decode("utf-8", "replace"))
            self.symbols.append(s)
            return s
        if t == ";":
            return self.symbols[self.long()]
        if t == "@":
            return self.objects[self.long()]
        if t == "[":
            arr = []
            self._register(arr, "array")
            for _ in range(self.long()):
                arr.append(self.obj())
            return arr
        if t in ("{", "}"):
            h = {}
            self._register(h, "hash")
            for _ in range(self.long()):
                k = self.obj()
                if isinstance(k, (list, dict, RubyObject)):
                    k = str(k)
                h[k] = self.obj()
            if t == "}":
                h["__default__"] = self.obj()
            return h
        if t == "o":
            cls = str(self.obj())
            o = RubyObject(cls)
            self._register(o, "object")
            for _ in range(self.long()):
                name = str(self.obj())
                o.ivars[name] = self.obj()
            return o
        if t == "u":
            cls = str(self.obj())
            n = self.long()
            return self._userdef(cls, self.read(n))
        if t == "U":
            cls = str(self.obj())
            return self._register({"__class__": cls, "__value__": self.obj()},
                                  "usermarshal")
        if t == "I":
            base = self.obj()
            ivars = {}
            for _ in range(self.long()):
                ivars[str(self.obj())] = self.obj()
            if isinstance(base, RubyObject):
                base.ivars.update(ivars)
                return base
            return self._register({"__value__": base, "__ivars__": ivars}, "object")
        if t == "e":
            mod = str(self.obj())
            return self._register({"__extended__": mod, "__value__": self.obj()},
                                  "object")
        if t == "S":
            o = RubyObject(str(self.obj()))
            self._register(o, "struct")
            for _ in range(self.long()):
                o.ivars[str(self.obj())] = self.obj()
            return o
        if t in ("c", "m", "M"):
            return self._register({"__class_ref__": str(self.obj())}, "class")
        if t == "/":
            n = self.long()
            return self._register({"__regexp__": self.read(n).decode("latin-1")},
                                  "regexp")
        raise ValueError("unknown Marshal type %r at offset %d" % (t, self.p - 1))

    def _userdef(self, cls, raw):
        """RPG Maker's Table class: dim, x, y, z, count, then int16 LE data."""
        if cls == "Table":
            dim, x = struct.unpack("<II", raw[0:8])
            off, y, z = 8, 1, 1
            if dim >= 2:
                y = struct.unpack("<I", raw[off:off + 4])[0]
                off += 4
            if dim == 3:
                z = struct.unpack("<I", raw[off:off + 4])[0]
                off += 4
            count = struct.unpack("<I", raw[off:off + 4])[0]
            off += 4
            data = list(struct.unpack("<%dh" % count, raw[off:off + count * 2]))
            return self._register(Table(dim, x, y, z, data), "userdef")
        return self._register({"__class__": cls, "__raw_len__": len(raw)}, "userdef")


def load(path, register_all=False):
    with open(path, "rb") as fh:
        return MarshalReader(fh.read(), register_all=register_all).obj()


# ---------------------------------------------------------------------------
# summarising objects into JSON-safe structures
# ---------------------------------------------------------------------------

def summarize(obj, depth=0, max_items=40, max_str=200):
    if depth > 6:
        return "..."
    if isinstance(obj, Table):
        return {"__class__": "Table", "dim": obj.dim, "x": obj.x, "y": obj.y,
                "z": obj.z, "count": obj.count, "bytes": obj.count * 2}
    if isinstance(obj, RubyObject):
        d = {"__class__": obj.cls}
        for k, v in obj.ivars.items():
            d[k] = summarize(v, depth + 1, max_items, max_str)
        return d
    if isinstance(obj, list):
        return [summarize(v, depth + 1, max_items, max_str) for v in obj[:max_items]]
    if isinstance(obj, dict):
        return {str(k): summarize(v, depth + 1, max_items, max_str)
                for k, v in list(obj.items())[:max_items]}
    if isinstance(obj, Sym):
        return str(obj)
    if isinstance(obj, str):
        return obj if len(obj) <= max_str else obj[:max_str] + "...(%d chars)" % len(obj)
    return obj


# ---------------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------------

def cmd_scripts(args):
    """Scripts.rxdata is [[id, name, zlib_deflated_source], ...]."""
    data = None
    for policy in (False, True):
        try:
            data = load(args.path, register_all=policy)
            break
        except Exception as exc:  # noqa: BLE001
            print("[!] parse failed (register_all=%s): %s" % (policy, exc))
    if data is None:
        raise SystemExit("could not parse Scripts.rxdata with either link policy")
    if not isinstance(data, list):
        raise SystemExit("expected an Array at top level, got %s" % type(data).__name__)
    print("[+] top level: Array of %d script sections (link policy register_all=%s)"
          % (len(data), policy))

    os.makedirs(args.out, exist_ok=True)
    index, ok, bad = [], 0, 0
    for pos, item in enumerate(data):
        if not isinstance(item, list) or len(item) < 3:
            bad += 1
            continue
        sid, name, code = item[0], str(item[1]), item[2]
        try:
            src = zlib.decompress(code.encode("latin-1"))
            try:
                text, enc = src.decode("utf-8"), "utf-8"
            except UnicodeDecodeError:
                text, enc = src.decode("latin-1"), "latin-1"
        except Exception as exc:  # noqa: BLE001
            bad += 1
            index.append({"id": sid, "name": name, "error": "%s" % exc})
            continue
        ok += 1
        safe = "".join(c if (c.isalnum() or c in "._- ") else "_" for c in name).strip()
        fname = "%03d_%s.rb" % (pos + 1, safe or "script")
        # newline="": write the source's own line endings. Through Windows' text
        # mode a CRLF source becomes "\r\r\n" and every line reads back doubled,
        # which moves every line number away from the engine's backtraces.
        with open(os.path.join(args.out, fname), "w", encoding="utf-8", newline="") as fh:
            fh.write(text)
        index.append({"order": pos, "id": sid, "name": name, "file": fname,
                      "bytes": len(src), "encoding": enc,
                      "sha1": hashlib.sha1(src).hexdigest()})

    print("[+] %d scripts decompressed, %d failed" % (ok, bad))
    for e in index:
        if "error" in e:
            print("      FAILED id=%s name=%s: %s" % (e["id"], e["name"], e["error"]))
    with open(os.path.join(args.out, "index.json"), "w", encoding="utf-8") as fh:
        json.dump(index, fh, indent=1)
    print("[+] wrote %s" % os.path.join(args.out, "index.json"))
    print("[+] total script source: %d bytes" % sum(e.get("bytes", 0) for e in index))


def cmd_maps(args):
    import glob
    from collections import Counter
    files = sorted(glob.glob(os.path.join(args.data, "Map[0-9]*.rxdata")))
    print("[i] %d map files" % len(files))
    records, total_codes = [], Counter()
    total_events = total_pages = 0
    for f in files:
        m = load(f)
        rec = {"file": os.path.basename(f)}
        if isinstance(m, RubyObject):
            rec["class"] = m.cls
            rec["width"] = m.ivars.get("@width")
            rec["height"] = m.ivars.get("@height")
            rec["tileset_id"] = m.ivars.get("@tileset_id")
            t = m.ivars.get("@data")
            if isinstance(t, Table):
                rec["table"] = {"dim": t.dim, "x": t.x, "y": t.y, "z": t.z,
                                "count": t.count, "bytes": t.count * 2}
            ev = m.ivars.get("@events")
            codes = Counter()
            pages = 0
            if isinstance(ev, dict):
                rec["event_count"] = len(ev)
                total_events += len(ev)
                for e in ev.values():
                    if not isinstance(e, RubyObject):
                        continue
                    for pg in (e.ivars.get("@pages") or []):
                        if not isinstance(pg, RubyObject):
                            continue
                        pages += 1
                        for cmd in (pg.ivars.get("@list") or []):
                            if isinstance(cmd, RubyObject):
                                codes[cmd.ivars.get("@code")] += 1
            rec["page_count"] = pages
            rec["command_histogram"] = {str(k): v for k, v in sorted(codes.items())}
            total_pages += pages
            total_codes.update(codes)
        records.append(rec)

    out = {"map_count": len(files), "total_events": total_events,
           "total_pages": total_pages,
           "command_histogram_total": {str(k): v for k, v in sorted(total_codes.items())},
           "maps": records}
    d = os.path.dirname(os.path.abspath(args.out))
    os.makedirs(d, exist_ok=True)
    with open(args.out, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=1)
    print("[+] %d maps, %d events, %d pages" % (len(files), total_events, total_pages))
    print("[+] event command codes used (code: count):")
    for code, n in sorted(total_codes.items()):
        print("      %5d : %d" % (code, n))
    print("[+] wrote %s" % args.out)


def cmd_tables(args):
    import glob
    files = sorted(glob.glob(os.path.join(args.data, "*.rxdata")))
    keep = []
    for f in files:
        b = os.path.basename(f)
        if b.startswith("Map") and b != "MapInfos.rxdata":
            continue
        if b == "Scripts.rxdata":
            continue
        keep.append(f)
    os.makedirs(args.out, exist_ok=True)
    for f in keep:
        base = os.path.splitext(os.path.basename(f))[0]
        try:
            data = load(f)
        except Exception as exc:  # noqa: BLE001
            print("[!] %-22s FAILED: %s" % (base, exc))
            continue
        with open(os.path.join(args.out, base + ".json"), "w", encoding="utf-8") as fh:
            json.dump(summarize(data, max_items=args.max_items), fh,
                      indent=1, ensure_ascii=False)
        kind = data.cls if isinstance(data, RubyObject) else type(data).__name__
        n = len(data) if isinstance(data, (list, dict)) else "-"
        print("[+] %-22s %-24s items=%s" % (base, kind, n))


def cmd_dump(args):
    data = load(args.path)
    print(json.dumps(summarize(data, max_items=args.max_items), indent=1,
                     ensure_ascii=False))


def main():
    ap = argparse.ArgumentParser(description="Read RPG Maker XP .rxdata files.")
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("scripts", help="extract all scripts from Scripts.rxdata")
    s.add_argument("path")
    s.add_argument("--out", default=os.path.join("work", "scripts"))
    s.set_defaults(func=cmd_scripts)

    m = sub.add_parser("maps", help="summarise every Map*.rxdata")
    m.add_argument("data", help="path to the Data folder")
    m.add_argument("--out", default=os.path.join("work", "tables", "maps_summary.json"))
    m.set_defaults(func=cmd_maps)

    t = sub.add_parser("tables", help="dump the data tables to JSON")
    t.add_argument("data", help="path to the Data folder")
    t.add_argument("--out", default=os.path.join("work", "tables"))
    t.add_argument("--max-items", type=int, default=40)
    t.set_defaults(func=cmd_tables)

    d = sub.add_parser("dump", help="dump one .rxdata file as JSON")
    d.add_argument("path")
    d.add_argument("--max-items", type=int, default=40)
    d.set_defaults(func=cmd_dump)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    sys.exit(main())
