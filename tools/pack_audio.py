#!/usr/bin/env python3
"""
pack_audio.py -- shrink To the Moon's Audio/ folder into one file for the 3DS.

    python tools\\pack_audio.py <the game's folder, or the zip it came in>
        [--out work\\audio\\audio.ttma] [--rate 32000]
        [--q-music 1] [--q-bgs 0] [--q-se 1] [--jobs 8]

Reads the game's own Audio/{BGM,BGS,ME,SE} (the main game only, not the
holiday-special folders), re-encodes every sound with ffmpeg's libvorbis --
at most --rate Hz, channels as the source, quality --q-* -- and writes them
into one file the engine reads (gems/mruby-ttm-audio, "pack"):

    "TTMA"  u32 version (1)  u32 count  u32 index bytes
    count x { u16 name bytes, name ("bgm/name": folder and name, lower case,
              no extension), u32 offset, u32 length, u8 channels, u8 0,
              u16 0, u32 rate, u32 frames }
    the Ogg Vorbis streams, at their offsets

One name, one entry: when a name exists in several formats the .ogg wins,
then .wav, .mp3, .wma (every mp3/wma the game ships also has an .ogg).

One file instead of ~800 small ones: no FAT cluster slack, and no slow
open() on the SD card for each first play. Lower sample rates also cut the
console's decode work, and keep a 96 kHz sound played at pitch 150 inside the
DSP's range.

Self-check: every packed stream is decoded back with ffmpeg, its frame count
goes into the index, and its length must match the source's within 1 %
(or 50 ms: Vorbis rounds a short sound up to whole blocks, so a 0.5 s switch
click comes back 30 ms longer, the extra being silence). Prints sizes before
and after.

Everything stays on this machine: the pack is made from the player's own copy
and is never shipped with the engine (compatibility matrix, section 7).
"""

import argparse
import concurrent.futures as cf
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile

KINDS = ("BGM", "BGS", "ME", "SE")
EXT_ORDER = (".ogg", ".wav", ".mp3", ".wma")


def find_ffmpeg():
    exe = shutil.which("ffmpeg")
    if not exe:
        sys.exit("ffmpeg not found on PATH (winget install Gyan.FFmpeg)")
    return exe


def collect(src):
    """{ "bgm/name": (display path, bytes loader) } for the main game's Audio/."""
    found = {}   # key -> (rank, path, loader)

    def offer(kind, filename, path, loader):
        stem, ext = os.path.splitext(filename)
        ext = ext.lower()
        if ext not in EXT_ORDER:
            return
        key = (kind.lower() + "/" + stem).lower()
        rank = EXT_ORDER.index(ext)
        if key not in found or rank < found[key][0]:
            found[key] = (rank, path, loader)

    if os.path.isdir(src):
        for kind in KINDS:
            d = os.path.join(src, "Audio", kind)
            if not os.path.isdir(d):
                continue
            for f in os.listdir(d):
                p = os.path.join(d, f)
                if os.path.isfile(p):
                    offer(kind, f, p, lambda p=p: open(p, "rb").read())
    else:
        z = zipfile.ZipFile(src)
        names = [i.filename for i in z.infolist() if not i.is_dir() and "/Audio/" in i.filename]
        if not names:
            sys.exit("no Audio/ folder in " + src)
        # the main game is the shallowest folder holding an Audio/ (the
        # holiday specials sit in sub-folders of it)
        root = min({n.split("/Audio/")[0] for n in names}, key=lambda r: r.count("/"))
        for n in names:
            if n.split("/Audio/")[0] != root:
                continue
            rest = n.split("/Audio/")[1].split("/")
            if len(rest) != 2 or rest[0] not in KINDS:
                continue
            offer(rest[0], rest[1], n, lambda n=n: z.read(n))
    return {k: (v[1], v[2]) for k, v in found.items()}


def probe(ffmpeg, path):
    """(channels, rate, frames) of a file, by decoding it to raw s16."""
    out = subprocess.run([ffmpeg, "-v", "error", "-i", path, "-f", "s16le", "-acodec", "pcm_s16le", "-"],
                         capture_output=True)
    if out.returncode != 0:
        raise RuntimeError(out.stderr.decode("utf-8", "replace").strip()[-300:])
    info = subprocess.run([ffmpeg, "-hide_banner", "-i", path], capture_output=True).stderr.decode("utf-8", "replace")
    ch, rate = 2, 44100
    for line in info.splitlines():
        if "Audio:" in line:
            parts = [p.strip() for p in line.split("Audio:")[1].split(",")]
            for p in parts:
                if p.endswith(" Hz"):
                    rate = int(p.split()[0])
                elif p in ("mono",):
                    ch = 1
                elif p in ("stereo",):
                    ch = 2
            break
    return ch, rate, len(out.stdout) // (2 * ch)


def encode(ffmpeg, key, raw, args, tmp):
    kind = key.split("/")[0]
    q = {"bgm": args.q_music, "me": args.q_music, "bgs": args.q_bgs, "se": args.q_se}[kind]
    safe = "".join(c if c.isalnum() else "_" for c in key)
    src = os.path.join(tmp, safe + ".src")
    dst = os.path.join(tmp, safe + ".ogg")
    with open(src, "wb") as f:
        f.write(raw)
    ch, rate, frames = probe(ffmpeg, src)
    ch = min(ch, 2)
    out_rate = min(rate, args.rate)
    r = subprocess.run([ffmpeg, "-v", "error", "-y", "-i", src, "-vn", "-map_metadata", "-1",
                        "-ac", str(ch), "-ar", str(out_rate), "-c:a", "libvorbis", "-q:a", str(q),
                        "-f", "ogg", dst], capture_output=True)
    if r.returncode != 0:
        raise RuntimeError(r.stderr.decode("utf-8", "replace").strip()[-300:])
    och, orate, oframes = probe(ffmpeg, dst)
    data = open(dst, "rb").read()
    os.remove(src)
    os.remove(dst)
    src_s, out_s = frames / rate, oframes / orate
    ok = och == ch and orate == out_rate and abs(src_s - out_s) <= max(0.01 * src_s, 0.050)
    return {"key": key, "data": data, "channels": och, "rate": orate, "frames": oframes,
            "src_bytes": len(raw), "src_s": src_s, "out_s": out_s, "ok": ok}


def write_pack(path, entries):
    entries = sorted(entries, key=lambda e: e["key"])
    entry_fixed = struct.calcsize("<IIBBHII")
    offset = 16 + sum(2 + len(e["key"].encode("utf-8")) + entry_fixed for e in entries)
    index = bytearray()
    for e in entries:
        name = e["key"].encode("utf-8")
        index += struct.pack("<H", len(name)) + name
        index += struct.pack("<IIBBHII", offset, len(e["data"]), e["channels"], 0, 0, e["rate"], e["frames"])
        offset += len(e["data"])
    with open(path, "wb") as f:
        f.write(b"TTMA" + struct.pack("<III", 1, len(entries), len(index)))
        f.write(index)
        for e in entries:
            f.write(e["data"])
    return os.path.getsize(path)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("source", help="the game's install folder, or the zip it came in")
    ap.add_argument("--out", default=os.path.join("work", "audio", "audio.ttma"))
    ap.add_argument("--rate", type=int, default=32000, help="highest sample rate kept (Hz)")
    ap.add_argument("--q-music", type=float, default=1, help="Vorbis quality for BGM and ME (-1..10)")
    ap.add_argument("--q-bgs", type=float, default=0, help="Vorbis quality for BGS")
    ap.add_argument("--q-se", type=float, default=1, help="Vorbis quality for SE")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 4)
    args = ap.parse_args()

    ffmpeg = find_ffmpeg()
    files = collect(args.source)
    if not files:
        sys.exit("no sounds found under Audio/ in " + args.source)
    print("%d sounds (%s)" % (len(files), ", ".join(
        "%s %d" % (k, sum(1 for n in files if n.startswith(k.lower() + "/"))) for k in KINDS)))

    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    tmp = tempfile.mkdtemp(prefix="ttma_")
    entries, failed = [], []
    try:
        with cf.ThreadPoolExecutor(args.jobs) as pool:
            futs = {pool.submit(encode, ffmpeg, k, loader(), args, tmp): (k, path)
                    for k, (path, loader) in files.items()}
            for i, fut in enumerate(cf.as_completed(futs), 1):
                k, path = futs[fut]
                try:
                    e = fut.result()
                except Exception as ex:      # noqa: BLE001 -- report and carry on
                    failed.append((k, str(ex)))
                    continue
                if not e["ok"]:
                    failed.append((k, "length %.3f s -> %.3f s" % (e["src_s"], e["out_s"])))
                entries.append(e)
                if i % 100 == 0:
                    print("  %d/%d" % (i, len(files)))
    finally:
        shutil.rmtree(tmp, ignore_errors=True)

    size = write_pack(args.out, entries)
    src_total = sum(e["src_bytes"] for e in entries)
    print()
    for kind in KINDS:
        es = [e for e in entries if e["key"].startswith(kind.lower() + "/")]
        a, b = sum(e["src_bytes"] for e in es), sum(len(e["data"]) for e in es)
        print("  %-3s %4d sounds  %7.1f MB -> %6.1f MB" % (kind, len(es), a / 1048576, b / 1048576))
    print("  all %4d sounds  %7.1f MB -> %6.1f MB  (%s, %d bytes)" % (
        len(entries), src_total / 1048576, size / 1048576, args.out, size))
    ok = sum(1 for e in entries if e["ok"])
    print("round trip: %d/%d streams decoded back with the source's length" % (ok, len(files)))
    for k, why in sorted(failed):
        print("  FAIL  %s: %s" % (k, why))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
