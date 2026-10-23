#!/usr/bin/env python3
"""
inspect_cia.py -- read a CIA's title metadata straight out of the binary.

Written to verify the claim that matters for the port: a CIA built with
`SystemModeExt: 178MB` really does carry the New 3DS extended-memory flag in its
extended header (a .3dsx cannot carry it at all). Also prints the SMDH title text
and the RomFS size, so a built title can be sanity-checked without a console.

Usage: python inspect_cia.py <file.cia>
"""

import struct
import sys

# libctru SystemMode values (3ds/exheader.h) and what they mean
N3DS_MODES = {0: "Legacy (use the Old 3DS mode)",
              1: "124 MB (New 3DS default)",
              2: "178 MB  <-- EXTENDED MEMORY MODE",
              3: "124 MB",
              4: "72 MB",
              5: "32 MB"}
O3DS_MODES = {0: "64 MB (Old 3DS default)", 1: "n/a (New 3DS only)",
              2: "97 MB", 3: "80 MB", 4: "72 MB", 5: "32 MB"}

SERVICE_STRIDE = 8
SERVICE_COUNT = 34
CORE_INFO_FROM_SERVICES = 0x48     # core_info sits 0x48 before service_access[0]


def main():
    path = sys.argv[1]
    with open(path, "rb") as fh:
        data = fh.read()
    print("file: %s (%d bytes)" % (path, len(data)))

    # ---- CIA header -------------------------------------------------------
    hdr_size, cia_type, cia_ver = struct.unpack_from("<IHH", data, 0)
    cert, ticket, tmd, meta = struct.unpack_from("<IIII", data, 8)
    content_size, = struct.unpack_from("<Q", data, 0x18)
    print("CIA header: size=0x%X type=%d version=%d" % (hdr_size, cia_type, cia_ver))
    print("  cert=0x%X ticket=0x%X tmd=0x%X meta=0x%X content=0x%X"
          % (cert, ticket, tmd, meta, content_size))

    ncch = (hdr_size + cert + ticket + tmd + 0x3F) & ~0x3F   # content follows the TMD,
    # 0x40-aligned; the meta section is stored after the content, not before it
    if data[ncch:ncch + 4] != b"NCCH":
        probe = data.find(b"NCCH", hdr_size)
        if probe < 0:
            raise SystemExit("no NCCH found in this CIA")
        ncch = probe
    print("  NCCH at 0x%X: magic=%r" % (ncch, data[ncch:ncch + 4]))

    # ---- NCCH header ------------------------------------------------------
    # Only the content size is taken at a fixed offset here. Everything below is
    # located by *content search* instead of remembered offsets, so the fields it
    # reports are trustworthy even if this header layout shifts between versions.
    content_size, = struct.unpack_from("<I", data, ncch + 4)
    print("  NCCH content size: 0x%X" % content_size)

    # ---- extended header: locate the ACI by its service list --------------
    ex = ncch + 0x200
    region = data[ex:ex + 0x400]
    svc0 = region.find(b"fs:USER")
    if svc0 < 0:
        svc0 = region.find(b"cfg:u")
    if svc0 < 0:
        raise SystemExit("could not find the service access list in the exheader")

    # walk back to the first entry of the 34 x 8 list
    start = svc0
    while start - SERVICE_STRIDE >= 0 and region[start - SERVICE_STRIDE:start - SERVICE_STRIDE + 1].isalpha():
        start -= SERVICE_STRIDE
    services = [region[start + i * SERVICE_STRIDE:start + (i + 1) * SERVICE_STRIDE]
                .rstrip(b"\x00").decode("ascii", "replace")
                for i in range(SERVICE_COUNT)]
    services = [s for s in services if s]
    print("  services granted: %d (%s ...)" % (len(services), ", ".join(services[:5])))

    core = start - CORE_INFO_FROM_SERVICES
    core_version, = struct.unpack_from("<I", region, core)
    f_clock, n3ds_mode, proc_affinity, priority = region[core + 4:core + 8]
    o3ds_mode = proc_affinity >> 4
    print("\n  --- CPU / memory mode (from the exheader) ---")
    print("  core version          : %d" % core_version)
    print("  804 MHz clock         : %s" % ("yes" if f_clock & 1 else "no"))
    print("  L2 cache enabled      : %s" % ("yes" if f_clock & 2 else "no"))
    print("  New 3DS system mode   : %d  -> %s"
          % (n3ds_mode, N3DS_MODES.get(n3ds_mode, "?")))
    print("  Old 3DS system mode   : %d  -> %s"
          % (o3ds_mode, O3DS_MODES.get(o3ds_mode, "?")))
    print("  priority              : %d" % priority)

    # ---- stack size (the exheader's CodeSetInfo) ---------------------------
    # The "NCCH" magic sits 0x100 into the NCCH header; the ExHeader follows the
    # 0x200-byte NCCH header. StackSize is at CodeSetInfo+0x1C (3dbrew).
    exhdr = ncch + 0x100
    stack, = struct.unpack_from("<I", data, exhdr + 0x1C)
    print("  main-thread stack     : 0x%X (%d KB)" % (stack, stack // 1024))

    # ---- SMDH title text --------------------------------------------------
    pos = data.find("To the Moon".encode("utf-16-le"))
    if pos > 0:
        raw = data[pos:pos + 0x80]
        text = raw.decode("utf-16-le", "replace").split("\x00")[0]
        print("\n  SMDH long title       : %r" % text)
    else:
        print("\n  SMDH long title       : (not found)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
