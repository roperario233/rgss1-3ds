#!/usr/bin/env python3
"""
inventory.py -- build the 3DS porting inventory for an RPG Maker XP game.

Scans the extracted scripts for the API surface the game actually uses, and
measures the resource footprint (textures, audio, data) against what a
Nintendo 3DS can hold.

Inputs (produced by rgssad_extract.py and rxdata_dump.py):
    work/scripts/*.rb            the game's Ruby source
    work/rgssad_manifest.json    archive manifest (sizes, PNG dimensions)
    work/raw/Graphics/**         extracted images
    work/tables/maps_summary.json

Outputs:
    work/inventory.json          machine-readable results
    stdout                       human-readable summary
"""

import argparse
import json
import os
import re
import struct
import sys
import zipfile

# ---------------------------------------------------------------------------
# what we look for, grouped by the decision it feeds
# ---------------------------------------------------------------------------

GROUPS = {
    "windows_interop": [
        r"Win32API", r"kernel32", r"user32", r"gdi32", r"UtilsSteam",
        r"\bUtils\b", r"\bSteam\b", r"Registry", r"GetSystemMetrics",
    ],
    "resolution_640x480": [
        r"Graphics\.resize_screen", r"Graphics\.width", r"Graphics\.height",
        r"Graphics\.snap_to_bitmap", r"\b640\b", r"\b480\b",
    ],
    "rgss_renderer": [
        r"\bTilemap\b", r"\bPlane\b", r"\bViewport\b", r"\bSprite\b",
        r"\bBitmap\b", r"\bWindow_\w+", r"blend_type", r"hue_change",
        r"radial_blur", r"\bblur\b", r"\bzoom\b", r"\bangle\b", r"\bwave_\w+",
        r"\bopacity\b", r"\bsrc_rect\b", r"stretch_blt", r"draw_text",
        r"\btone\b", r"\bcolor\b",
    ],
    "ruby_surface": [
        r"\beval\b", r"\bMarshal\b", r"\bZlib\b", r"ObjectSpace",
        r"\bThread\b", r"\bfork\b", r"\brequire\b", r"Time\.now", r"File\.",
        r"\bIO\b", r"Dir\.", r"system\(", r"define_method", r"alias_method",
        r"__FILE__", r"__LINE__",
    ],
    "audio_api": [
        r"Audio\.bgm", r"Audio\.bgs", r"Audio\.se", r"Audio\.me",
        r"Audio\.setup", r"\.volume\s*=", r"\.pitch\s*=", r"\.fade\b",
    ],
    "input_api": [r"\bInput\.", r"Input::"],
    "graphics_api": [
        r"Graphics\.frame_rate", r"Graphics\.transition", r"Graphics\.freeze",
        r"Graphics\.brightness", r"Graphics\.play_movie", r"Graphics\.wait",
        r"Graphics\.update",
    ],
    "persistence": [r"load_data", r"save_data", r"Marshal\.dump", r"File\.open",
                    r"FileTest"],
    "font_text": [r"Font\.new", r"\bFont\b", r"text_size", r"draw_text"],
    "concurrency": [r"Thread\.new", r"Thread\.start", r"Mutex"],
    "networking": [r"Socket", r"TCPSocket", r"UDPSocket", r"open-uri",
                   r"http", r"Net::"],
}

COMPILED = {g: [(p, re.compile(p)) for p in pats] for g, pats in GROUPS.items()}
EXAMPLES_PER_PATTERN = 3


def scan_scripts(script_dir):
    results = {}
    files = sorted(f for f in os.listdir(script_dir) if f.endswith(".rb"))
    totals = {g: 0 for g in GROUPS}
    for fname in files:
        path = os.path.join(script_dir, fname)
        with open(path, encoding="utf-8", errors="replace") as fh:
            lines = fh.readlines()
        per_file = {}
        for group, pats in COMPILED.items():
            for pat, rx in pats:
                hits = [(i + 1, ln.rstrip()) for i, ln in enumerate(lines)
                        if rx.search(ln)]
                if not hits:
                    continue
                totals[group] += len(hits)
                per_file.setdefault(group, {})[pat] = {
                    "count": len(hits),
                    "examples": [{"line": n, "text": t[:160]}
                                 for n, t in hits[:EXAMPLES_PER_PATTERN]],
                }
        if per_file:
            results[fname] = per_file
    return files, results, totals


# ---------------------------------------------------------------------------
# image sizes (PNG via IHDR, JPEG via SOF markers)
# ---------------------------------------------------------------------------

def jpeg_size(data):
    if data[:2] != b"\xff\xd8":
        return None
    i = 2
    while i + 9 < len(data):
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        if marker in (0xD8, 0xD9) or 0xD0 <= marker <= 0xD7:
            i += 2
            continue
        seg_len = struct.unpack(">H", data[i + 2:i + 4])[0]
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7,
                      0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            h, w = struct.unpack(">HH", data[i + 5:i + 9])
            return (w, h)
        i += 2 + seg_len
    return None


def png_size(data):
    if len(data) < 24 or data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    return struct.unpack(">II", data[16:24])


def scan_graphics(raw_dir, max_file_bytes=8 * 1024 * 1024):
    """Measure every extracted image; decoded bytes = w*h*4 (RGBA in RAM)."""
    images = []
    for root, _dirs, files in os.walk(raw_dir):
        for f in files:
            if not f.lower().endswith((".png", ".jpg", ".jpeg")):
                continue
            p = os.path.join(root, f)
            size = os.path.getsize(p)
            dim = None
            if size <= max_file_bytes:
                with open(p, "rb") as fh:
                    head = fh.read(256 * 1024)
                dim = png_size(head) or jpeg_size(head)
            rel = os.path.relpath(p, raw_dir).replace("\\", "/")
            images.append({"path": rel, "file_bytes": size,
                           "w": dim[0] if dim else None,
                           "h": dim[1] if dim else None,
                           "decoded_bytes": (dim[0] * dim[1] * 4) if dim else None})
    return images


def scan_audio(path):
    """List Audio/* entries and sizes from a zip or a game folder."""
    entries = []
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as zf:
            for i in zf.infolist():
                name = i.filename.replace("\\", "/")
                if "/Audio/" in name and not name.endswith("/"):
                    entries.append((name, i.file_size))
    else:
        for root, _dirs, files in os.walk(path):
            for f in files:
                p = os.path.join(root, f)
                if os.sep + "Audio" + os.sep in p:
                    entries.append((p.replace("\\", "/"), os.path.getsize(p)))
    by_type = {}
    for name, size in entries:
        parts = name.split("/")
        kind = parts[parts.index("Audio") + 1] if "Audio" in parts else "?"
        ext = os.path.splitext(name)[1].lower()
        d = by_type.setdefault(kind, {"count": 0, "bytes": 0, "exts": {}})
        d["count"] += 1
        d["bytes"] += size
        d["exts"][ext] = d["exts"].get(ext, 0) + 1
    return entries, by_type


# ---------------------------------------------------------------------------
# 3DS reference numbers (public hardware/API documentation)
# ---------------------------------------------------------------------------

THREEDS = {
    "cpu": "ARM11 MPCore, 268 MHz (Old 3DS) / 804 MHz (New 3DS)",
    "isa": "ARMv6K (32-bit), VFPv2",
    "ram_total": "128 MB (Old 3DS) / 256 MB (New 3DS) FCRAM",
    "app_ram_budget": "~64 MB (Old) / ~124 MB (New), ~176 MB New-3DS extended mode",
    "gpu": "PICA200, OpenGL ES 1.1 class, no GLSL",
    "vram": "~6 MB",
    "max_texture": 1024,
    "top_screen": "400x240",
    "bottom_screen": "320x240",
    "audio": "DSP via NDSP (no hardware Ogg decode)",
    "storage": "SD card (slow, ~2-4 MB/s sequential)",
}


def fmt(b):
    if b is None:
        return "-"
    b = float(b)
    for unit in ("B", "KB", "MB", "GB"):
        if b < 1024:
            return "%.1f %s" % (b, unit)
        b /= 1024
    return "%.1f TB" % b


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--scripts", default=os.path.join("work", "scripts"))
    ap.add_argument("--raw", default=os.path.join("work", "raw"))
    ap.add_argument("--manifest", default=os.path.join("work", "rgssad_manifest.json"))
    ap.add_argument("--maps", default=os.path.join("work", "tables", "maps_summary.json"))
    ap.add_argument("--game", default="To.the.Moon.v2023.07.03.Incl.ALL.DLC.zip")
    ap.add_argument("--out", default=os.path.join("work", "inventory.json"))
    a = ap.parse_args()

    report = {"threeds_reference": THREEDS}

    files, per_file, totals = scan_scripts(a.scripts)
    total_src = sum(os.path.getsize(os.path.join(a.scripts, f)) for f in files)
    print("=" * 78)
    print("SCRIPT SURFACE  (%d script sections, %s of Ruby source)"
          % (len(files), fmt(total_src)))
    print("=" * 78)
    for group, n in sorted(totals.items(), key=lambda kv: -kv[1]):
        print("  %-22s %6d matches" % (group, n))
    report["scripts"] = {"file_count": len(files), "source_bytes": total_src,
                         "group_totals": totals, "per_file": per_file}

    images = scan_graphics(a.raw)
    sized = [i for i in images if i["decoded_bytes"]]
    total_decoded = sum(i["decoded_bytes"] for i in sized)
    total_files = sum(i["file_bytes"] for i in images)
    over = [i for i in sized if i["w"] > THREEDS["max_texture"]
            or i["h"] > THREEDS["max_texture"]]
    tilesets = [i for i in sized if "/Tilesets/" in i["path"]]
    ts_decoded = sum(i["decoded_bytes"] for i in tilesets)
    print("\n" + "=" * 78)
    print("GRAPHICS  (%d images, %s on disk, %s decoded to RGBA)"
          % (len(images), fmt(total_files), fmt(total_decoded)))
    print("=" * 78)
    print("  over 1024px (PICA200 limit): %d  -> %s decoded"
          % (len(over), fmt(sum(i["decoded_bytes"] for i in over))))
    print("  tilesets decoded:            %s across %d files"
          % (fmt(ts_decoded), len(tilesets)))
    print("  3DS VRAM reference:          %s" % THREEDS["vram"])
    print("  3DS app RAM budget:          %s" % THREEDS["app_ram_budget"])
    print("\n  top 15 images by decoded size:")
    for i in sorted(sized, key=lambda x: -x["decoded_bytes"])[:15]:
        flag = "  <-- OVER 1024px" if (i["w"] > 1024 or i["h"] > 1024) else ""
        print("    %5dx%-6d %10s  %s%s"
              % (i["w"], i["h"], fmt(i["decoded_bytes"]), i["path"], flag))
    report["graphics"] = {"image_count": len(images), "file_bytes": total_files,
                          "decoded_bytes": total_decoded,
                          "tileset_decoded_bytes": ts_decoded,
                          "over_texture_limit": over, "images": images}

    entries, by_type = scan_audio(a.game)
    total_audio = sum(s for _n, s in entries)
    print("\n" + "=" * 78)
    print("AUDIO  (%d files, %s)" % (len(entries), fmt(total_audio)))
    print("=" * 78)
    for kind, d in sorted(by_type.items()):
        print("  %-6s %5d files  %10s  %s"
              % (kind, d["count"], fmt(d["bytes"]), d["exts"]))
    report["audio"] = {"file_count": len(entries), "total_bytes": total_audio,
                       "by_type": by_type}

    manifest = {}
    if os.path.exists(a.manifest):
        manifest = json.load(open(a.manifest, encoding="utf-8"))
    data_entries = [e for e in manifest.get("entries", [])
                    if e["name"].lower().startswith("data")]
    data_bytes = sum(e["size"] for e in data_entries)
    print("\n" + "=" * 78)
    print("DATA  (%d .rxdata files, %s)" % (len(data_entries), fmt(data_bytes)))
    print("=" * 78)
    for e in sorted(data_entries, key=lambda x: -x["size"])[:10]:
        print("  %10s  %s" % (fmt(e["size"]), e["name"]))
    report["data"] = {"file_count": len(data_entries), "total_bytes": data_bytes}

    if os.path.exists(a.maps):
        ms = json.load(open(a.maps, encoding="utf-8"))
        hist = ms.get("command_histogram_total", {})
        print("\n" + "=" * 78)
        print("MAPS  (%d maps, %d events, %d pages)"
              % (ms.get("map_count", 0), ms.get("total_events", 0),
                 ms.get("total_pages", 0)))
        print("=" * 78)
        for code, n in sorted(hist.items(), key=lambda kv: -kv[1])[:20]:
            print("  code %-5s %7d" % (code, n))
        report["maps"] = {"map_count": ms.get("map_count"),
                          "total_events": ms.get("total_events"),
                          "total_pages": ms.get("total_pages"),
                          "command_histogram_total": hist}

    print("\n" + "=" * 78)
    print("RESOLUTION GEOMETRY")
    print("=" * 78)
    scale_fit = min(400 / 640, 240 / 480)
    print("  game frame 640x480 (4:3) -> top screen 400x240 (5:3)")
    print("  scale to fit: %.3f => %.0fx%.0f (pillarboxed)"
          % (scale_fit, 640 * scale_fit, 480 * scale_fit))
    print("  scale 0.5 (integer, crisp): 320x240 -> fits the BOTTOM screen exactly")
    print("  RMXP default font 22px at 0.5 scale = 11px on a 400x240 panel")
    report["geometry"] = {"scale_to_fit": scale_fit,
                          "half_scale_frame": "320x240",
                          "bottom_screen_native": "320x240"}

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as fh:
        json.dump(report, fh, indent=1)
    print("\n[+] wrote %s" % a.out)


if __name__ == "__main__":
    sys.exit(main())
