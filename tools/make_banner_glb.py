#!/usr/bin/env python3
"""
make_banner_glb.py -- build the banner's 3D scene as a .glb for pycgfx.

pycgfx turns glTF into the CGFX a HOME-menu banner renders, so the whole scene
is authored here instead of in a modelling tool: two textured plates, one spin
animation, and nothing else. No Blender, no extra dependency -- a .glb is a JSON
document plus one binary buffer, and this repo already writes its own binary
formats (ttmtex, ttma, the RGSSAD reader).

The scene, in the banner camera's own units
-------------------------------------------
A 3DS banner is 256x128 but it is not a picture: the HOME menu renders a small
3D scene. pycgfx ships the camera the menu uses (banner-camera.gltf):

    yfov 30 degrees, aspect 5:3, at z = +44.786, znear 26.5

so at z = 0 the visible rectangle is 40 x 24 units. Everything below is sized
against that:

    Backdrop   48 x 27 units at z = -8    the game's night sky, full frame
    Logo       group node, spun about Y   the only moving thing
      Plaque   32 x 12.8 x 1.2 units      the wordmark slab

The rotation is why this is a 3D scene at all: a flat PNG cannot turn.

Two details that bite
---------------------
* Quaternions: a single 0 -> 360 degree key pair is a no-op, because the two
  quaternions are the same rotation and slerp takes the short way. The spin is
  therefore keyed every 45 degrees, which also gives pycgfx's Euler conversion
  something to unwrap.
* pycgfx resizes any texture wider or taller than 256 px, so both textures are
  authored at exactly 256x128 by tools/make_banner_assets.py.

Usage:  python tools/make_banner_glb.py [--seconds 10]
Output: work/banner/banner.glb
"""

import argparse
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
ASSETS = os.path.join(ROOT, "work", "banner")

CAM_Z = 44.786                 # pycgfx's banner camera, from banner-camera.gltf
YFOV = 0.523599                # 30 degrees
ASPECT = 5.0 / 3.0             # the banner's 256 x 128 ... is 2:1, but the menu
                               # camera is 5:3; the visible box below uses it
VIEW_H = 2.0 * CAM_Z * math.tan(YFOV / 2.0)   # ~24.0 units tall at z = 0
VIEW_W = VIEW_H * ASPECT                      # ~40.0 units wide at z = 0

PLAQUE_W, PLAQUE_H, PLAQUE_D = 32.0, 12.8, 1.2
PLAQUE_Y = 0.8                 # a little above centre: the menu's shadow sits low
BACKDROP_W, BACKDROP_H = 48.0, 27.0
BACKDROP_Z = -8.0


class Builder:
    """Collects vertices, then serialises one buffer plus glTF accessors."""

    def __init__(self):
        self.bin = bytearray()
        self.views = []
        self.accessors = []

    def _align(self, n=4):
        while len(self.bin) % n:
            self.bin.append(0)

    def view(self, data, target=None):
        self._align()
        offset = len(self.bin)
        self.bin += data
        v = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target:
            v["target"] = target
        self.views.append(v)
        return len(self.views) - 1

    def accessor(self, values, comp_type, type_name, target=None, bounds=False):
        fmt = {5126: "f", 5123: "H", 5125: "I"}[comp_type]
        width = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}[type_name]
        flat = [c for v in values for c in (v if width > 1 else (v,))]
        data = struct.pack("<%d%s" % (len(flat), fmt), *flat)
        acc = {"bufferView": self.view(data, target), "componentType": comp_type,
               "count": len(values), "type": type_name}
        if bounds:
            acc["min"] = [min(v[i] if width > 1 else v for v in values)
                          for i in range(width)]
            acc["max"] = [max(v[i] if width > 1 else v for v in values)
                          for i in range(width)]
        self.accessors.append(acc)
        return len(self.accessors) - 1


def quad(a, b, c, d):
    """Two triangles for the quad a-b-c-d, wound counter-clockwise."""
    return [a, b, c, a, c, d]


def slab(w, h, d, y):
    """A box centred on (0, y, 0): the front face separate from the other five."""
    x0, x1 = -w / 2.0, w / 2.0
    y0, y1 = y - h / 2.0, y + h / 2.0
    z0, z1 = -d / 2.0, d / 2.0

    front = ([((x0, y0, z1), (0, 0, 1), (0.0, 1.0)),
              ((x1, y0, z1), (0, 0, 1), (1.0, 1.0)),
              ((x1, y1, z1), (0, 0, 1), (1.0, 0.0)),
              ((x0, y1, z1), (0, 0, 1), (0.0, 0.0))], [0, 1, 2, 0, 2, 3])

    # back, then the four sides; each face gets its own vertices so the normals
    # stay hard-edged (a turned slab must not shade like a cushion)
    rest_v, rest_i = [], []

    def face(verts, normal, uvs):
        base = len(rest_v)
        for (x, yy, z), uv in zip(verts, uvs):
            rest_v.append(((x, yy, z), normal, uv))
        rest_i.extend([base, base + 1, base + 2, base, base + 2, base + 3])

    face([(x1, y0, z0), (x0, y0, z0), (x0, y1, z0), (x1, y1, z0)], (0, 0, -1),
         [(0, 1), (1, 1), (1, 0), (0, 0)])
    face([(x0, y0, z0), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0)], (-1, 0, 0),
         [(0, 1), (1, 1), (1, 0), (0, 0)])
    face([(x1, y0, z1), (x1, y0, z0), (x1, y1, z0), (x1, y1, z1)], (1, 0, 0),
         [(0, 1), (1, 1), (1, 0), (0, 0)])
    face([(x0, y1, z1), (x1, y1, z1), (x1, y1, z0), (x0, y1, z0)], (0, 1, 0),
         [(0, 1), (1, 1), (1, 0), (0, 0)])
    face([(x0, y0, z0), (x1, y0, z0), (x1, y0, z1), (x0, y0, z1)], (0, -1, 0),
         [(0, 1), (1, 1), (1, 0), (0, 0)])
    return front, (rest_v, rest_i)


def backdrop_quad(w, h):
    x0, x1 = -w / 2.0, w / 2.0
    y0, y1 = -h / 2.0, h / 2.0
    verts = [((x0, y0, 0.0), (0, 0, 1), (0.0, 1.0)),
             ((x1, y0, 0.0), (0, 0, 1), (1.0, 1.0)),
             ((x1, y1, 0.0), (0, 0, 1), (1.0, 0.0)),
             ((x0, y1, 0.0), (0, 0, 1), (0.0, 0.0))]
    return verts, [0, 1, 2, 0, 2, 3]


def add_primitive(b, verts, indices, material):
    positions = [v[0] for v in verts]
    normals = [v[1] for v in verts]
    uvs = [v[2] for v in verts]
    return {
        "attributes": {
            "POSITION": b.accessor(positions, 5126, "VEC3", 34962, bounds=True),
            "NORMAL": b.accessor(normals, 5126, "VEC3", 34962),
            "TEXCOORD_0": b.accessor(uvs, 5126, "VEC2", 34962),
        },
        "indices": b.accessor(indices, 5123, "SCALAR", 34963),
        "material": material,
    }


def read_png(name):
    path = os.path.join(ASSETS, name)
    if not os.path.exists(path):
        raise SystemExit("missing %s -- run tools/make_banner_assets.py first"
                         % path)
    with open(path, "rb") as fh:
        return fh.read()


def build(seconds):
    b = Builder()

    (front_verts, front_idx), (rest_verts, rest_idx) = slab(
        PLAQUE_W, PLAQUE_H, PLAQUE_D, 0.0)
    bd_verts, bd_idx = backdrop_quad(BACKDROP_W, BACKDROP_H)

    wordmark = add_primitive(b, front_verts, front_idx, 0)   # material 0
    plaque = add_primitive(b, rest_verts, rest_idx, 1)       # material 1
    backdrop = add_primitive(b, bd_verts, bd_idx, 2)         # material 2

    img_wordmark = b.view(read_png("wordmark.png"))
    img_backdrop = b.view(read_png("backdrop.png"))

    # the spin: 45-degree keys, so slerp cannot take the short way round
    steps = 8
    times = [round(seconds * i / steps, 4) for i in range(steps + 1)]
    quats = []
    for i in range(steps + 1):
        half = math.pi * (i / steps)          # half of the 0..360 degree angle
        quats.append((0.0, math.sin(half), 0.0, math.cos(half)))
    a_time = b.accessor(times, 5126, "SCALAR", bounds=True)
    a_rot = b.accessor(quats, 5126, "VEC4")

    gltf = {
        "asset": {"version": "2.0",
                  "generator": "ttm3ds tools/make_banner_glb.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "Banner", "children": [1, 2]},
            {"name": "Backdrop", "mesh": 1,
             "translation": [0.0, 0.6, BACKDROP_Z]},
            {"name": "Logo", "children": [3]},
            {"name": "Plaque", "mesh": 0,
             "translation": [0.0, PLAQUE_Y, 0.0]},
        ],
        "meshes": [
            {"name": "Plaque", "primitives": [wordmark, plaque]},
            {"name": "Backdrop", "primitives": [backdrop]},
        ],
        "materials": [
            {"name": "Wordmark",
             "pbrMetallicRoughness": {
                 "baseColorTexture": {"index": 0, "texCoord": 0},
                 "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                 "metallicFactor": 0.0, "roughnessFactor": 0.75}},
            {"name": "PlaqueEdge",
             "pbrMetallicRoughness": {
                 "baseColorFactor": [0.035, 0.055, 0.11, 1.0],
                 "metallicFactor": 0.0, "roughnessFactor": 0.6}},
            {"name": "Backdrop",
             "pbrMetallicRoughness": {
                 "baseColorTexture": {"index": 1, "texCoord": 0},
                 "baseColorFactor": [1.0, 1.0, 1.0, 1.0],
                 "metallicFactor": 0.0, "roughnessFactor": 1.0}},
        ],
        "textures": [{"sampler": 0, "source": 0}, {"sampler": 0, "source": 1}],
        "images": [{"bufferView": img_wordmark, "mimeType": "image/png"},
                   {"bufferView": img_backdrop, "mimeType": "image/png"}],
        "samplers": [{"magFilter": 9729, "minFilter": 9729,
                      "wrapS": 33071, "wrapT": 33071}],
        "animations": [{
            "name": "Spin",
            "samplers": [{"input": a_time, "output": a_rot,
                          "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0,
                          "target": {"node": 2, "path": "rotation"}}],
        }],
        "accessors": b.accessors,
        "bufferViews": b.views,
        "buffers": [{"byteLength": len(b.bin)}],
    }
    return gltf, bytes(b.bin), times


def write_glb(path, gltf, binary):
    js = json.dumps(gltf, separators=(",", ":")).encode("utf-8")
    js += b" " * (-len(js) % 4)
    bin_chunk = binary + b"\x00" * (-len(binary) % 4)
    total = 12 + 8 + len(js) + 8 + len(bin_chunk)
    with open(path, "wb") as fh:
        fh.write(struct.pack("<4sII", b"glTF", 2, total))
        fh.write(struct.pack("<I4s", len(js), b"JSON") + js)
        fh.write(struct.pack("<I4s", len(bin_chunk), b"BIN\x00") + bin_chunk)


def main():
    ap = argparse.ArgumentParser(description="Build the banner scene as a .glb.")
    ap.add_argument("--seconds", type=float, default=10.0,
                    help="one full turn, in seconds (default 10)")
    args = ap.parse_args()

    if not os.path.isdir(ASSETS):
        raise SystemExit("missing %s -- run tools/make_banner_assets.py first"
                         % ASSETS)

    gltf, binary, times = build(args.seconds)
    os.makedirs(ASSETS, exist_ok=True)
    out = os.path.join(ASSETS, "banner.glb")
    write_glb(out, gltf, binary)

    print("[+] banner.glb        %d bytes" % os.path.getsize(out))
    print("[i] view at z=0: %.1f x %.1f units; plaque %.1f x %.1f of it"
          % (VIEW_W, VIEW_H, PLAQUE_W, PLAQUE_H))
    print("[i] spin: one turn in %.1f s, keyed every 45 degrees (%d keys)"
          % (args.seconds, len(times)))
    return 0


if __name__ == "__main__":
    sys.exit(main())

